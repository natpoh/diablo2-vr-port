// fog_before_fx_test - vr/fog_before_fx.cpp on a D3D12 device of its own (WARP), a list recorded as the game records its frame's end:
//   the scene (R11G11B10) drawn with the depth-stencil bound, made a shader resource; the game's own heap, graphics root signature and
//   table, a compute root signature of the game's with a root constant and a root UAV; the "resolve" (a pipeline marked as the game's,
//   reading the scene through the game's table) bound and drawn - the sinks told as the hooks tell them. Then:
//   1. on: what the resolve wrote is the scene fogged by the game's formula at the depth's distance (the step ran before its draw, and the
//      game's heap and table were bound again: the resolve read the scene through them);
//   2. the game's compute state given back: its dispatch after the resolve, with no root argument set again, writes its own constant;
//   3. the void (depth 0): left as it is; 4. off: the scene as it is;
//   5. [fog] game_fog_fx_depth: the game's effects (a weighted-blended colour target, a half-size additive one) drawn again for their depth
//      by twins of their pipelines, and fogged by it at the step - near ones barely, far ones nearly whole, over the void faded out; off:
//      as they are.
#include "fog_before_fx.h"

#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <windows.h>
#include "native_d3d12.h"   // (after <d3d12.h>: it names D3D12 types without including it)

#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>
#include <vector>

// (the hooks are not in this test: the sinks are called as they would call them)
static std::unordered_map<uint64_t, ID3D12Resource*> g_rtvs;
static std::unordered_map<uint64_t, ID3D12Resource*> g_srvs;   // the game's R11G11B10 views, as the hook keeps them
static unsigned g_srvInc = 0;
static ID3D12CommandQueue* g_q = nullptr;
namespace natived3d {
void SetOurs(bool) {}
ID3D12Resource* RtvResource(uint64_t h) { const auto it = g_rtvs.find(h); return it != g_rtvs.end() ? it->second : nullptr; }
void TrackSceneViews(bool) {}
ID3D12Resource* SrvResource(uint64_t h) { const auto it = g_srvs.find(h); return it != g_srvs.end() ? it->second : nullptr; }
unsigned SrvIncrement() { return g_srvInc; } unsigned RtvIncrement() { return 32; }   // (the sinks' handles are the test's own: a call with `single` steps by this)
ID3D12CommandQueue* GameQueue() { return g_q; }
}  // namespace natived3d

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)
static void Log(const char* s) { std::printf("  %s\n", s); }

static std::vector<uint8_t> Compile(const char* src, const char* target) {
    ID3DBlob *code = nullptr, *err = nullptr;
    if (FAILED(D3DCompile(src, strlen(src), nullptr, nullptr, nullptr, "main", target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err))) {
        std::printf("compile: %s\n", err ? (const char*)err->GetBufferPointer() : "?");
        return {};
    }
    std::vector<uint8_t> v((const uint8_t*)code->GetBufferPointer(), (const uint8_t*)code->GetBufferPointer() + code->GetBufferSize());
    code->Release();
    return v;
}
static ID3D12RootSignature* RootSig(ID3D12Device* dev, const D3D12_ROOT_SIGNATURE_DESC& d) {
    ID3DBlob* b = nullptr;
    ID3D12RootSignature* r = nullptr;
    if (SUCCEEDED(D3D12SerializeRootSignature(&d, D3D_ROOT_SIGNATURE_VERSION_1, &b, nullptr)))
        dev->CreateRootSignature(0, b->GetBufferPointer(), b->GetBufferSize(), __uuidof(ID3D12RootSignature), (void**)&r);
    if (b) b->Release();
    return r;
}
static ID3D12Resource* Tex(ID3D12Device* dev, DXGI_FORMAT f, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES s, const D3D12_CLEAR_VALUE* cv,
                           UINT16 mips = 1) {
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = 64;
    d.Height = 64;
    d.DepthOrArraySize = 1;
    d.MipLevels = mips;
    d.Format = f;
    d.SampleDesc.Count = 1;
    d.Flags = flags;
    ID3D12Resource* r = nullptr;
    dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, s, cv, __uuidof(ID3D12Resource), (void**)&r);
    return r;
}
static ID3D12Resource* Buffer(ID3D12Device* dev, D3D12_HEAP_TYPE type, UINT64 size, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES s) {
    D3D12_HEAP_PROPERTIES hp{type};
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = size;
    d.Height = d.DepthOrArraySize = d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    d.Flags = flags;
    ID3D12Resource* r = nullptr;
    dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, s, nullptr, __uuidof(ID3D12Resource), (void**)&r);
    return r;
}
static D3D12_RESOURCE_BARRIER Tr(ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b) {
    D3D12_RESOURCE_BARRIER x{};
    x.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    x.Transition = {r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, a, b};
    return x;
}

static const char* kVs = "float4 main(uint id : SV_VertexID) : SV_Position { float2 p = float2((id << 1) & 2, id & 2); return float4(p * 2 - 1, 0, 1); }";
static const char* kResolvePs =
    "Texture2D<float3> S : register(t0); float4 main(float4 p : SV_Position) : SV_Target { return float4(S.Load(int3(p.xy, 0)), 1); }";
static const char* kGameCs = "cbuffer C : register(b0) { uint v; }; RWByteAddressBuffer O : register(u0); [numthreads(1,1,1)] void main() { O.Store(0, v); }";

// --debug: the D3D12 debug layer with GPU-based validation (the Graphics Tools feature); every error or corruption it reports fails the test
int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // (a crash leaves every line before it)
    const bool debugLayer = argc > 1 && std::strcmp(argv[1], "--debug") == 0;
    if (debugLayer) {
        ID3D12Debug* dbg = nullptr;
        ID3D12Debug1* dbg1 = nullptr;
        if (FAILED(D3D12GetDebugInterface(__uuidof(ID3D12Debug), (void**)&dbg)) || !dbg) {
            std::printf("fog_before_fx_test: --debug - no D3D12 debug layer here (Graphics Tools not installed)\n");
            return 1;
        }
        dbg->EnableDebugLayer();
        if (SUCCEEDED(dbg->QueryInterface(__uuidof(ID3D12Debug1), (void**)&dbg1)) && dbg1) {
            dbg1->SetEnableGPUBasedValidation(TRUE);
            dbg1->Release();
        }
        dbg->Release();
        std::printf("  the debug layer with GPU-based validation\n");
    }
    using CreateFactoryFn = HRESULT(WINAPI*)(UINT, REFIID, void**);
    HMODULE dxgi = LoadLibraryA("dxgi.dll");
    auto createFactory = (CreateFactoryFn)GetProcAddress(dxgi, "CreateDXGIFactory2");
    IDXGIFactory4* fac = nullptr;
    IDXGIAdapter* warp = nullptr;
    ID3D12Device* dev = nullptr;
    CHECK(createFactory && SUCCEEDED(createFactory(0, __uuidof(IDXGIFactory4), (void**)&fac)) &&
          SUCCEEDED(fac->EnumWarpAdapter(__uuidof(IDXGIAdapter), (void**)&warp)) &&
          SUCCEEDED(D3D12CreateDevice(warp, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&dev)));
    if (!dev) return 1;
    ID3D12InfoQueue* info = nullptr;
    if (debugLayer) dev->QueryInterface(__uuidof(ID3D12InfoQueue), (void**)&info);
    // the debug layer's errors since the last call, printed (none expected)
    auto layerErrors = [&]() {
        int errors = 0;
        if (!info) return 0;
        for (UINT64 i = 0, n = info->GetNumStoredMessages(); i < n; ++i) {
            SIZE_T len = 0;
            info->GetMessage(i, nullptr, &len);
            std::vector<char> m(len);
            auto* msg = (D3D12_MESSAGE*)m.data();
            if (FAILED(info->GetMessage(i, msg, &len))) continue;
            if (msg->Severity == D3D12_MESSAGE_SEVERITY_ERROR || msg->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION) {
                ++errors;
                std::printf("  the debug layer: %.*s\n", (int)msg->DescriptionByteLength, msg->pDescription);
            }
        }
        info->ClearStoredMessages();
        return errors;
    };
    const D3D12_COMMAND_QUEUE_DESC qd{D3D12_COMMAND_LIST_TYPE_DIRECT};
    ID3D12CommandAllocator* alloc = nullptr;
    ID3D12GraphicsCommandList* cl = nullptr;
    ID3D12Fence* fence = nullptr;
    dev->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&g_q);
    dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&alloc);
    dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&cl);
    cl->Close();
    dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&fence);
    UINT64 fv = 0;

    fogfx::SetLog(&Log);
    natived3d::Sinks k;
    fogfx::FillSinks(k);

    // the game's: the scene, its depth, the resolve's target, the heaps and views
    D3D12_CLEAR_VALUE dcv{DXGI_FORMAT_D24_UNORM_S8_UINT};
    dcv.DepthStencil = {0.0f, 0};
    // (two mips, as PC1's scene, 2026-10-28 17:16: the step writes its first)
    ID3D12Resource* scene = Tex(dev, DXGI_FORMAT_R11G11B10_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, 2);
    ID3D12Resource* depth = Tex(dev, DXGI_FORMAT_R24G8_TYPELESS, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, D3D12_RESOURCE_STATE_DEPTH_WRITE, &dcv);
    // the game's half-size effects target: R11G11B10, half the scene's size, bound with the scene's depth (told to the sinks only)
    ID3D12Resource* halfTarget = nullptr;
    {
        D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
        D3D12_RESOURCE_DESC d{};
        d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        d.Width = 32;
        d.Height = 32;
        d.DepthOrArraySize = d.MipLevels = 1;
        d.Format = DXGI_FORMAT_R11G11B10_FLOAT;
        d.SampleDesc.Count = 1;
        d.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, __uuidof(ID3D12Resource), (void**)&halfTarget);
    }
    ID3D12Resource* out = Tex(dev, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr);
    ID3D12Resource* readback = Buffer(dev, D3D12_HEAP_TYPE_READBACK, 64 * 64 * 16, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12Resource* gameOut = Buffer(dev, D3D12_HEAP_TYPE_DEFAULT, 256, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12Resource* gameRead = Buffer(dev, D3D12_HEAP_TYPE_READBACK, 256, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    CHECK(scene && depth && out && readback && gameOut && gameRead);
    ID3D12DescriptorHeap *rtvHeap = nullptr, *dsvHeap = nullptr, *srvHeap = nullptr;
    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2};
    dev->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void**)&rtvHeap);
    hd = {D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1};
    dev->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void**)&dsvHeap);
    hd = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    dev->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void**)&srvHeap);
    const UINT rtvInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    const D3D12_CPU_DESCRIPTOR_HANDLE rtvScene = rtvHeap->GetCPUDescriptorHandleForHeapStart(), rtvOut{rtvScene.ptr + rtvInc};
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsvHeap->GetCPUDescriptorHandleForHeapStart();
    dev->CreateRenderTargetView(scene, nullptr, rtvScene);
    dev->CreateRenderTargetView(out, nullptr, rtvOut);
    g_rtvs[rtvScene.ptr] = scene;
    g_rtvs[rtvOut.ptr] = out;
    // the game's frame target is R11G11B10 too, bound with the scene's depth at the resolve (PC1 2026-10-28): told to the sinks as the
    // resolve's target in 2d (the draw itself goes to `out`, read back)
    ID3D12Resource* frameTarget = Tex(dev, DXGI_FORMAT_R11G11B10_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr);
    const D3D12_CPU_DESCRIPTOR_HANDLE rtvFrame{0xF4A3E000ull};   // (a handle for the sinks only)
    g_rtvs[rtvFrame.ptr] = frameTarget;
    // ... and a real view of it, to fill it with a lit picture (2d': the scene the resolve's tables name read black, the lit world in that target)
    ID3D12DescriptorHeap* frameRtvHeap = nullptr;
    {
        D3D12_DESCRIPTOR_HEAP_DESC fh{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1};
        dev->CreateDescriptorHeap(&fh, __uuidof(ID3D12DescriptorHeap), (void**)&frameRtvHeap);
    }
    const D3D12_CPU_DESCRIPTOR_HANDLE rtvFrameReal = frameRtvHeap->GetCPUDescriptorHandleForHeapStart();
    dev->CreateRenderTargetView(frameTarget, nullptr, rtvFrameReal);
    D3D12_DEPTH_STENCIL_VIEW_DESC dvd{DXGI_FORMAT_D24_UNORM_S8_UINT, D3D12_DSV_DIMENSION_TEXTURE2D};
    dev->CreateDepthStencilView(depth, &dvd, dsv);
    const D3D12_CPU_DESCRIPTOR_HANDLE rtvHalf{0xF4A3F000ull};   // (a handle for the sinks only)
    g_rtvs[rtvHalf.ptr] = halfTarget;
    // a full-size R11G11B10 target of the scene's size bound with the depth after it, which the resolve does not read (told to the sinks only)
    ID3D12Resource* decoy = Tex(dev, DXGI_FORMAT_R11G11B10_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr);
    const D3D12_CPU_DESCRIPTOR_HANDLE rtvDecoy{0xDEC0F000ull};
    g_rtvs[rtvDecoy.ptr] = decoy;
    // the effects' weighted colour target (RGBA16F): bound in one call with the decoy, the decoy is the effects' additive target (2e'')
    ID3D12Resource* accumTarget = Tex(dev, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr);
    const D3D12_CPU_DESCRIPTOR_HANDLE rtvAccumFx{0xACC0F000ull};
    g_rtvs[rtvAccumFx.ptr] = accumTarget;
    D3D12_SHADER_RESOURCE_VIEW_DESC sv{DXGI_FORMAT_R11G11B10_FLOAT, D3D12_SRV_DIMENSION_TEXTURE2D, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING};
    sv.Texture2D.MipLevels = 1;
    dev->CreateShaderResourceView(scene, &sv, srvHeap->GetCPUDescriptorHandleForHeapStart());
    g_srvInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    g_srvs[srvHeap->GetCPUDescriptorHandleForHeapStart().ptr] = scene;   // (the resolve's t0 here: the scene, as the hook would note it)

    // the game's resolve: its root signature (a table: t0), its pipeline (marked as the game's resolve)
    const D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
    D3D12_ROOT_PARAMETER gp{};
    gp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    gp.DescriptorTable = {1, &range};
    ID3D12RootSignature* gRoot = RootSig(dev, {1, &gp, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE});
    const std::vector<uint8_t> vs = Compile(kVs, "vs_5_0"), ps = Compile(kResolvePs, "ps_5_0"), gcs = Compile(kGameCs, "cs_5_0");
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = gRoot;
    pd.VS = {vs.data(), vs.size()};
    pd.PS = {ps.data(), ps.size()};
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.SampleMask = ~0u;
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT;
    pd.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    pd.SampleDesc.Count = 1;
    ID3D12PipelineState* resolve = nullptr;
    CHECK(gRoot && SUCCEEDED(dev->CreateGraphicsPipelineState(&pd, __uuidof(ID3D12PipelineState), (void**)&resolve)));
    fogfx::MarkResolve(resolve);
    fogfx::ResetSceneCheck(0);   // (the game starts with nothing written until a lit picture is proven: here the scenes are lit grey)
    // the game's compute: a root constant (b0) and a root UAV (u0)
    D3D12_ROOT_PARAMETER cp[2] = {};
    cp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    cp[0].Constants = {0, 0, 1};
    cp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    cp[1].Descriptor = {0, 0};
    ID3D12RootSignature* cRoot = RootSig(dev, {2, cp, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE});
    D3D12_COMPUTE_PIPELINE_STATE_DESC cd{cRoot, {gcs.data(), gcs.size()}};
    ID3D12PipelineState* gameCs = nullptr;
    CHECK(cRoot && SUCCEEDED(dev->CreateComputePipelineState(&cd, __uuidof(ID3D12PipelineState), (void**)&gameCs)));

    const float nearPlane = 0.05f, plane = 30.0f, density = 0.5f, fog[3] = {0.25f, 0.125f, 0.0625f}, grey = 0.5f;
    bool sceneElsewhere = false;   // the scene bound on another list than the resolve's (the game records on several threads)
    bool frameIsR11 = false;       // the resolve's own target R11G11B10, bound with the same depth
    // [the submission order] 0.209.15's flat game: the scene drawn and made readable on another list (A, submitted first), the resolve's list
    // setting nothing on it, and a list recorded BEFORE the resolve's but submitted after it (D: the next frame's, recorded early) putting it
    // back to a render target - "the last barrier recorded" says RENDER_TARGET, the GPU runs it as a shader resource. hideD: D's barrier made
    // but not told (a transition the step cannot see)
    bool chainMode = false, hideD = false;
    ID3D12CommandAllocator* allocA = nullptr;
    ID3D12CommandAllocator* allocD = nullptr;
    ID3D12GraphicsCommandList* listA = nullptr;
    ID3D12GraphicsCommandList* listD = nullptr;
    dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&allocA);
    dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&allocD);
    dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocA, nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&listA);
    dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocD, nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&listD);
    listA->Close();
    listD->Close();
    bool sceneBlack = false;       // the scene cleared black ...
    bool frameLit = false;         // ... and the resolve's own target holding a lit picture before it draws (Serg's 0.209.11 probe)
    bool halfLayer = false;        // the game's half-size effects target (R11G11B10 too) bound with the same depth after the scene
    bool decoyAfter = false;       // a full-size R11G11B10 target the resolve does not read, bound with the same depth after the scene
    bool holes = false;            // holes in the depth: one pixel (10, 10); a line of three at (20 - 22, 40)
    bool holeKey = false;          // [fog] game_fog_fx_depth on
    float skyDepth = 0.0f;         // [sky] game_sky: the game's dome's depth band (Params::skyDepth)
    bool depthUntold = false;      // no barrier of the depth this frame (its state from an earlier frame's: stale)
    bool depthElsewhere = false;   // the depth's barrier to its read state told on another list this frame
    int sceneElse = 0;             // the scene's barrier to a shader resource told on another list: 1 this frame; 2 then a later one to a
                                   // render target told there too (the next frame's, recorded early); 3 last frame (stale)
    bool curveOn = false;   // [fog] game_fog_our_curve: our effect's curve (20 - 80, curve 1.5, strength 0.9) in place of the exponential
    const float cStart = 20.0f, cEnd = 80.0f, cCurve = 1.5f, cStrength = 0.9f;
    bool ours = false;      // our effect's fog ([fog] game_fog=0): that curve by the view's depth (D2R_DepthFog.fx FogAmount), not the length
    int planesN = 0;        // [fog] game_fog_planes: that many planes from 20 to 80 (strength 1), softness planesSoft
    float planesSoft = 0.0f;
    // rectangles painted over the cleared frame: their depth and their colour (a silhouette against the sky)
    struct Patch { D3D12_RECT r; float depth; float rgb[3]; };
    std::vector<Patch> patches;
    // one frame's end as the game records it; the resolve's output at pixel (px, py) and the game's dispatch's word back
    auto frame = [&](bool on, float depthValue, float px, float py, float rgb[3], uint32_t* gameWord) {
        fogfx::Params p;
        p.on = on;
        p.plane = plane;
        p.density = density;
        std::memcpy(p.colour, fog, sizeof fog);
        p.nearPlane = nearPlane;
        p.ourCurve = curveOn || ours;
        p.ours = ours;
        p.start = cStart; p.end = cEnd; p.curve = cCurve; p.strength = cStrength;
        p.fxDepth = holeKey;   // (the holes go with the step now, not with this - holeKey off checks a hole with it off)
        p.skyDepth = skyDepth;
        p.planes = planesN;
        p.planesStart = 20.0f;
        p.planesEnd = 80.0f;
        p.planesStrength = 1.0f;
        p.planesSoft = planesSoft;
        fogfx::Configure(p);
        fogfx::Frame();   // (a present between frames)
        alloc->Reset();
        cl->Reset(alloc, nullptr);
        k.fxReset(cl);
        const float c[4] = {sceneBlack ? 0.0f : grey, sceneBlack ? 0.0f : grey, sceneBlack ? 0.0f : grey, 1};
        if (chainMode) {   // the scene drawn on A and made readable there; D recorded now (before the resolve's list), submitted after it
            allocA->Reset();
            listA->Reset(allocA, nullptr);
            k.fxReset(listA);
            k.fxTargets(listA, 1, &rtvScene, false, depth, &dsv);
            listA->ClearRenderTargetView(rtvScene, c, 0, nullptr);
            D3D12_RESOURCE_BARRIER ba = Tr(scene, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            k.fxBarrier(listA, 1, &ba);
            listA->ResourceBarrier(1, &ba);
            CHECK(SUCCEEDED(listA->Close()));
            allocD->Reset();
            listD->Reset(allocD, nullptr);
            k.fxReset(listD);
            D3D12_RESOURCE_BARRIER bd = Tr(scene, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
            // (hideD: told as if on a list never submitted - the recorded order still says a render target, the submissions never see D move it)
            k.fxBarrier(hideD ? (ID3D12GraphicsCommandList*)(uintptr_t)0x5CE90 : listD, 1, &bd);
            listD->ResourceBarrier(1, &bd);
            CHECK(SUCCEEDED(listD->Close()));
        }
        // the scene drawn (cleared) with the depth bound
        if (!chainMode) k.fxTargets(sceneElsewhere ? (ID3D12GraphicsCommandList*)(uintptr_t)0x5CE7E : cl, 1, &rtvScene, false, depth, &dsv);
        if (halfLayer) k.fxTargets(sceneElsewhere ? (ID3D12GraphicsCommandList*)(uintptr_t)0x5CE7E : cl, 1, &rtvHalf, false, depth, &dsv);
        if (decoyAfter) k.fxTargets(sceneElsewhere ? (ID3D12GraphicsCommandList*)(uintptr_t)0x5CE7E : cl, 1, &rtvDecoy, false, depth, &dsv);
        if (!chainMode) {
            cl->OMSetRenderTargets(1, &rtvScene, FALSE, &dsv);
            cl->ClearRenderTargetView(rtvScene, c, 0, nullptr);
        }
        const float lit[4] = {frameLit ? 0.6f : 0.0f, frameLit ? 0.5f : 0.0f, frameLit ? 0.4f : 0.0f, 1};
        cl->ClearRenderTargetView(rtvFrameReal, lit, 0, nullptr);
        cl->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, depthValue, 0, 0, nullptr);
        if (holes) {
            const D3D12_RECT hr[2] = {{10, 10, 11, 11}, {20, 40, 23, 41}};
            cl->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 2, hr);
        }
        for (const Patch& pt : patches) {
            const float pc[4] = {pt.rgb[0], pt.rgb[1], pt.rgb[2], 1};
            cl->ClearRenderTargetView(rtvScene, pc, 1, &pt.r);
            cl->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, pt.depth, 0, 1, &pt.r);
        }
        auto* other = (ID3D12GraphicsCommandList*)(uintptr_t)0x5CE7F;
        D3D12_RESOURCE_BARRIER b = Tr(scene, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        if (sceneElse != 3 && !chainMode) k.fxBarrier(sceneElse ? other : cl, 1, &b);   // (3: told last frame, below)
        if (!chainMode) cl->ResourceBarrier(1, &b);
        if (sceneElse == 2) {
            D3D12_RESOURCE_BARRIER nx = Tr(scene, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
            k.fxBarrier(other, 1, &nx);
        }
        // the depth read by the resolve's pass (its test off): DEPTH_READ | PIXEL_SHADER_RESOURCE
        const D3D12_RESOURCE_STATES depthRead = D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        b = Tr(depth, D3D12_RESOURCE_STATE_DEPTH_WRITE, depthRead);
        if (!depthUntold) {
            k.fxBarrier(depthElsewhere ? other : cl, 1, &b);
            cl->ResourceBarrier(1, &b);
        }
        // the game's compute state, set before the resolve
        natived3d::RootCall rc{natived3d::RootCall::kComputeRootSignature, 0, (uint64_t)(uintptr_t)cRoot};
        k.fxRoot(cl, rc);
        cl->SetComputeRootSignature(cRoot);
        rc = {natived3d::RootCall::kComputeConstant, 0, 0x5EED1234u, 1, 0};
        k.fxRoot(cl, rc);
        cl->SetComputeRoot32BitConstant(0, 0x5EED1234u, 0);
        rc = {natived3d::RootCall::kComputeUav, 1, gameOut->GetGPUVirtualAddress()};
        k.fxRoot(cl, rc);
        cl->SetComputeRootUnorderedAccessView(1, gameOut->GetGPUVirtualAddress());
        // the resolve: the game's heap, root signature, table, pipeline, targets
        ID3D12DescriptorHeap* heaps[1] = {srvHeap};
        k.fxHeaps(cl, 1, heaps);
        cl->SetDescriptorHeaps(1, heaps);
        rc = {natived3d::RootCall::kGraphicsRootSignature, 0, (uint64_t)(uintptr_t)gRoot};
        k.fxRoot(cl, rc);
        cl->SetGraphicsRootSignature(gRoot);
        rc = {natived3d::RootCall::kGraphicsTable, 0, srvHeap->GetGPUDescriptorHandleForHeapStart().ptr};
        k.fxRoot(cl, rc);
        cl->SetGraphicsRootDescriptorTable(0, srvHeap->GetGPUDescriptorHandleForHeapStart());
        cl->SetPipelineState(resolve);
        k.fxBound(cl, resolve);
        k.fxTargets(cl, 1, frameIsR11 ? &rtvFrame : &rtvOut, false, depth, &dsv);
        cl->OMSetRenderTargets(1, &rtvOut, FALSE, &dsv);
        const D3D12_VIEWPORT vp{0, 0, 64, 64, 0, 1};
        const D3D12_RECT sr{0, 0, 64, 64};
        cl->RSSetViewports(1, &vp);
        cl->RSSetScissorRects(1, &sr);
        cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        k.fxDraw(cl, false, 3, 1, 0, 0, 0);
        cl->DrawInstanced(3, 1, 0, 0);
        // the game's dispatch after it: its pipeline bound, no root argument set again
        cl->SetPipelineState(gameCs);
        cl->Dispatch(1, 1, 1);
        D3D12_RESOURCE_BARRIER bb[3] = {Tr(out, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE),
                                        Tr(gameOut, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
                                        Tr(scene, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)};
        cl->ResourceBarrier(chainMode ? 2 : 3, bb);   // (the submission order: D puts the scene back)
        D3D12_TEXTURE_COPY_LOCATION dst{readback, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT}, src{out, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
        dst.PlacedFootprint.Footprint = {DXGI_FORMAT_R32G32B32A32_FLOAT, 64, 64, 1, 64 * 16};
        src.SubresourceIndex = 0;
        cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        cl->CopyBufferRegion(gameRead, 0, gameOut, 0, 4);
        bb[0] = Tr(out, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        bb[1] = Tr(gameOut, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        bb[2] = Tr(depth, depthRead, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        cl->ResourceBarrier(depthUntold ? 2 : 3, bb);
        CHECK(SUCCEEDED(cl->Close()));
        ID3D12CommandList* lists[1] = {cl};
        if (chainMode) {   // A, then the resolve's, then D - each told to the step's tap first, as the hook tells it
            ID3D12CommandList* la[1] = {listA};
            fogfx::OnExecute(g_q, 1, la);
            g_q->ExecuteCommandLists(1, la);
            fogfx::OnExecute(g_q, 1, lists);
        }
        g_q->ExecuteCommandLists(1, lists);
        if (chainMode) {
            ID3D12CommandList* ld[1] = {listD};
            fogfx::OnExecute(g_q, 1, ld);
            g_q->ExecuteCommandLists(1, ld);
        }
        g_q->Signal(fence, ++fv);
        while (fence->GetCompletedValue() < fv) Sleep(1);
        CHECK(SUCCEEDED(dev->GetDeviceRemovedReason()));
        CHECK(layerErrors() == 0);
        float* px4 = nullptr;
        const D3D12_RANGE all{0, 64 * 64 * 16};
        readback->Map(0, &all, (void**)&px4);
        const float* q = px4 + ((int)py * 64 + (int)px) * 4;
        rgb[0] = q[0]; rgb[1] = q[1]; rgb[2] = q[2];
        readback->Unmap(0, nullptr);
        uint32_t* w = nullptr;
        const D3D12_RANGE four{0, 4};
        gameRead->Map(0, &four, (void**)&w);
        *gameWord = *w;
        gameRead->Unmap(0, nullptr);
    };
    // the game's formula at pixel (px, py), the depth d (reverse-Z, no far plane: z = near / d)
    auto expect = [&](float d, float px, float py, float rgb[3]) {
        const float z = nearPlane / d;
        const float nx = (px + 0.5f) / 64.0f * 2.0f - 1.0f, ny = 1.0f - (py + 0.5f) / 64.0f * 2.0f;
        const float x = nx * z, y = ny * z;
        const float dist = ours ? z : std::sqrt(x * x + y * y + z * z + 1.0f);
        const float t = curveOn || ours ? 1.0f - std::pow(std::fmin(1.0f, std::fmax(0.0f, (dist - cStart) / (cEnd - cStart))), cCurve) * cStrength
                                : std::fmin(1.0f, std::exp(-density * (dist / plane - 1.0f)));
        for (int i = 0; i < 3; ++i) rgb[i] = fog[i] + (grey - fog[i]) * t;
    };
    auto near3 = [](const float a[3], const float b[3]) {   // (R11G11B10: 6 or 5 bits of mantissa)
        for (int i = 0; i < 3; ++i)
            if (std::fabs(a[i] - b[i]) > 0.03f * std::fmax(std::fabs(b[i]), 0.05f)) return false;
        return true;
    };
    float got[3], want[3];
    uint32_t word = 0;
    // 1, 2: on - the scene fogged at 45 units (half way in), at the middle and a corner; the game's compute state given back
    const float pixels[2][2] = {{32, 32}, {3, 60}};
    for (const float* at : pixels) {
        const float d = nearPlane / 45.0f;
        frame(true, d, at[0], at[1], got, &word);
        expect(d, at[0], at[1], want);
        std::printf("  on, pixel %2.0f %2.0f at 45 units: %.4f %.4f %.4f (the game's formula %.4f %.4f %.4f); the game's dispatch wrote %08X\n", at[0], at[1],
                    got[0], got[1], got[2], want[0], want[1], want[2], word);
        CHECK(near3(got, want) && std::fabs(got[0] - grey) > 0.02f);
        CHECK(word == 0x5EED1234u);
        if (at[0] == 32) {   // the probe at the middle: what the step itself read and made (Serg 2026-11-09: its fog thin in the game)
            float pr[80] = {};
            const bool have = fogfx::LastProbe(pr);
            const float wantT = std::fmin(1.0f, std::exp(-density * (std::sqrt(45.0f * 45.0f * (1.0f + 2.0f * (0.5f / 32.0f) * (0.5f / 32.0f)) + 1.0f) / plane - 1.0f)));
            std::printf("  the probe at the middle: depth %.3g -> view depth %.2f, distance %.2f, fog %.3f (want %.3f), scene %.3f -> %.3f\n", pr[0], pr[1],
                        pr[2], 1.0f - pr[3], 1.0f - wantT, pr[4], pr[8]);
            CHECK(have && std::fabs(pr[0] - d) < 1e-6f && std::fabs(pr[1] - 45.0f) < 0.05f && std::fabs(pr[2] - 45.0f) < 0.1f);
            CHECK(std::fabs(pr[3] - wantT) < 0.01f && std::fabs(pr[4] - grey) < 0.01f && std::fabs(pr[8] - got[0]) < 0.03f && pr[7] < 0.5f);
        }
    }
    // 2b: our curve
    curveOn = true;
    for (const float* at : pixels) {
        const float d = nearPlane / 45.0f;
        frame(true, d, at[0], at[1], got, &word);
        expect(d, at[0], at[1], want);
        std::printf("  our curve, pixel %2.0f %2.0f at 45 units: %.4f %.4f %.4f (FogAt's %.4f %.4f %.4f)\n", at[0], at[1], got[0], got[1], got[2], want[0],
                    want[1], want[2]);
        CHECK(near3(got, want) && word == 0x5EED1234u);
    }
    curveOn = false;
    // 2b'': [fog] game_fog_planes - 4 planes at view depths 35, 50, 65, 80 (each adding a quarter of the fog): the middle pixel at 45 units is
    // behind the first only - hard: a quarter of the fog (what is left 0.75); soft (each fading in over the 15 in front of it): the first
    // whole, the second (a third of what is left) 10 of its 15 in - 0.75 x (1 - 1/3 x 2/3); at 100 units behind all four: the fog's colour
    // whole; the void: as it is
    {
        planesN = 4;
        auto planesWant = [&](float keep, float out[3]) { for (int i = 0; i < 3; ++i) out[i] = fog[i] + (grey - fog[i]) * keep; };
        const float d = nearPlane / 45.0f;
        planesSoft = 0.0f;
        frame(true, d, 32, 32, got, &word);
        planesWant(0.75f, want);
        std::printf("  4 hard planes, the middle at 45 units: %.4f %.4f %.4f (want %.4f %.4f %.4f)\n", got[0], got[1], got[2], want[0], want[1], want[2]);
        CHECK(near3(got, want) && word == 0x5EED1234u);
        planesSoft = 1.0f;
        frame(true, d, 32, 32, got, &word);
        planesWant(0.75f * (1.0f - (1.0f / 3.0f) * (10.0f / 15.0f)), want);
        std::printf("  4 soft planes, the middle at 45 units: %.4f %.4f %.4f (want %.4f %.4f %.4f)\n", got[0], got[1], got[2], want[0], want[1], want[2]);
        CHECK(near3(got, want));
        frame(true, nearPlane / 100.0f, 32, 32, got, &word);
        std::printf("  4 soft planes, the middle at 100 units: %.4f %.4f %.4f (the fog's colour %.4f %.4f %.4f)\n", got[0], got[1], got[2], fog[0], fog[1], fog[2]);
        CHECK(near3(got, fog));
        frame(true, 0.0f, 32, 32, got, &word);
        const float g3[3] = {grey, grey, grey};
        std::printf("  4 soft planes, the void: %.4f %.4f %.4f (as it is)\n", got[0], got[1], got[2]);
        CHECK(near3(got, g3));
        CHECK(std::fabs(fogfx::PlaneAlpha(0, 4, 1.0f) - 0.25f) < 1e-5f && std::fabs(fogfx::PlaneAlpha(3, 4, 1.0f) - 1.0f) < 1e-5f &&
              std::fabs(fogfx::PlaneAt(1, 4, 20.0f, 80.0f) - 50.0f) < 1e-4f);
        planesN = 0;
        planesSoft = 0.0f;
    }
    // 2b': our effect's fog - its curve by the view's depth: at the corner (the view's depth 45, the point's length about 1.6 times that) the
    // two differ by far; the post pass is told to leave the drawn pixels to the step (OursDrawn), and not with the game's fog in it
    CHECK(fogfx::Drawn() && !fogfx::OursDrawn());
    ours = true;
    for (const float* at : pixels) {
        const float d = nearPlane / 45.0f;
        frame(true, d, at[0], at[1], got, &word);
        expect(d, at[0], at[1], want);
        float byLength[3];
        ours = false; curveOn = true;
        expect(d, at[0], at[1], byLength);
        ours = true; curveOn = false;
        std::printf("  our effect's fog, pixel %2.0f %2.0f at the view's depth 45: %.4f %.4f %.4f (FogAmount's %.4f %.4f %.4f; by the length it would be "
                    "%.4f)\n", at[0], at[1], got[0], got[1], got[2], want[0], want[1], want[2], byLength[0]);
        CHECK(near3(got, want) && word == 0x5EED1234u);
        if (at[0] < 10) CHECK(!near3(got, byLength));
        CHECK(fogfx::OursDrawn());
    }
    ours = false;
    frame(true, nearPlane / 45.0f, 32, 32, got, &word);
    CHECK(fogfx::Drawn() && !fogfx::OursDrawn());
    // 2c: the scene drawn on another list - found by the depth they share
    sceneElsewhere = true;
    {
        const float d = nearPlane / 45.0f;
        frame(true, d, 32, 32, got, &word);
        expect(d, 32, 32, want);
        std::printf("  the scene on another list, pixel 32 32: %.4f %.4f %.4f (the formula %.4f %.4f %.4f)\n", got[0], got[1], got[2], want[0], want[1], want[2]);
        CHECK(near3(got, want) && word == 0x5EED1234u);
    }
    // 2d: and the resolve's own target R11G11B10 with the same depth (the newest pair noted for it): still the scene, not that target
    frameIsR11 = true;
    {
        const float d = nearPlane / 45.0f;
        frame(true, d, 32, 32, got, &word);
        expect(d, 32, 32, want);
        std::printf("  the resolve's target R11G11B10 too, pixel 32 32: %.4f %.4f %.4f (the formula %.4f %.4f %.4f)\n", got[0], got[1], got[2], want[0],
                    want[1], want[2]);
        CHECK(near3(got, want) && word == 0x5EED1234u);
    }
    // 2d': the scene the resolve's tables name reads black at the probe's points while the resolve's own target holds the lit picture (Serg's
    // 0.209.11 probe): after 20 probes in a row nothing is written into it (the resolve reads it black, not the fog's colour) and the game's
    // pass keeps its fog (Drawn false); a lit scene again - fogged again
    {
        const float d = nearPlane / 45.0f;
        sceneBlack = frameLit = true;
        for (int i = 0; i < 40; ++i) frame(true, d, 32, 32, got, &word);
        float pr[80] = {};
        fogfx::LastProbe(pr);
        std::printf("  the scene black, the resolve's target lit: the probe's scene %.3f %.3f %.3f, the target %.3f %.3f %.3f; the resolve wrote %.4f %.4f "
                    "%.4f; the step drawn for the game's pass: %d\n", pr[4], pr[5], pr[6], pr[12], pr[13], pr[14], got[0], got[1], got[2], fogfx::Drawn() ? 1 : 0);
        CHECK(pr[4] < 0.01f && std::fabs(pr[12] - 0.6f) < 0.02f && std::fabs(pr[13] - 0.5f) < 0.02f);
        CHECK(!fogfx::Drawn() && got[0] < 0.01f && got[1] < 0.01f && got[2] < 0.01f);
        sceneBlack = false;   // (the target still lit: last frame's picture, as the game's would be)
        for (int i = 0; i < 40; ++i) frame(true, d, 32, 32, got, &word);
        frameLit = false;
        expect(d, 32, 32, want);
        std::printf("  the scene lit again: %.4f %.4f %.4f (the formula %.4f %.4f %.4f), drawn %d\n", got[0], got[1], got[2], want[0], want[1], want[2],
                    fogfx::Drawn() ? 1 : 0);
        CHECK(fogfx::Drawn() && near3(got, want));
        // 2d'': the same, and the resolve blends into its own target (weighted OIT over the lit picture there): the step fogs that target -
        // the probe's first read is the target (lit), what it made the target fogged, the second read the tables' scene (black); drawn
        fogfx::MarkResolve(resolve, 1);
        sceneBlack = frameLit = true;
        for (int i = 0; i < 40; ++i) frame(true, d, 32, 32, got, &word);
        fogfx::LastProbe(pr);
        const float tc[3] = {0.6f, 0.5f, 0.4f};
        float wantT[3];
        {
            float e[3];
            expect(d, 32, 32, e);   // (grey fogged: e = fog + (grey - fog) t - the same t for the target's colour)
            const float t = (e[0] - fog[0]) / (grey - fog[0]);
            for (int c = 0; c < 3; ++c) wantT[c] = fog[c] + (tc[c] - fog[c]) * t;
        }
        std::printf("  the resolve blends into its lit target: the probe read %.3f %.3f %.3f, made %.3f %.3f %.3f (want %.3f %.3f %.3f), the tables' "
                    "scene %.3f %.3f %.3f; drawn %d\n", pr[4], pr[5], pr[6], pr[8], pr[9], pr[10], wantT[0], wantT[1], wantT[2], pr[12], pr[13], pr[14],
                    fogfx::Drawn() ? 1 : 0);
        CHECK(std::fabs(pr[4] - 0.6f) < 0.02f && std::fabs(pr[5] - 0.5f) < 0.02f && pr[12] < 0.01f);
        CHECK(near3(pr + 8, wantT) && fogfx::Drawn());
        fogfx::MarkResolve(resolve, -1);
        sceneBlack = false;
        for (int i = 0; i < 40; ++i) frame(true, d, 32, 32, got, &word);
        frameLit = false;
        CHECK(fogfx::Drawn());
        // 2d''': both dark - the tables' scene and the resolve's own target black at the probe's points (Serg's 0.209.13, flat with DLSS: the
        // lit world laid in after the resolve, what we wrote ended up over the flame): no lit picture proven -> after 20 probes nothing written,
        // the game's pass keeps its fog; the scene lit again -> fogged again. A scene once proven lit stays so through dark views (0.209.21, VR F4:
        // the step flipped off and on): both dark after it was proven - still drawing; at the game's start (nothing proven) - nothing written
        sceneBlack = true;
        for (int i = 0; i < 40; ++i) frame(true, d, 32, 32, got, &word);
        std::printf("  both dark after a lit picture was proven: drawn %d (stays)\n", fogfx::Drawn() ? 1 : 0);
        CHECK(fogfx::Drawn());
        fogfx::ResetSceneCheck(2);   // (the game's start: nothing proven, nothing written)
        for (int i = 0; i < 40; ++i) frame(true, d, 32, 32, got, &word);
        std::printf("  both dark from the start: the resolve wrote %.4f %.4f %.4f (nothing of ours), drawn %d\n", got[0], got[1], got[2], fogfx::Drawn() ? 1 : 0);
        CHECK(!fogfx::Drawn() && got[0] < 0.01f && got[1] < 0.01f && got[2] < 0.01f);
        sceneBlack = false;
        for (int i = 0; i < 40; ++i) frame(true, d, 32, 32, got, &word);
        expect(d, 32, 32, want);
        CHECK(fogfx::Drawn() && near3(got, want));
        // 2g: the submission order (0.209.15's flat game: "the scene's state 0x4 (set on another list this frame) ... not drawn"): the scene made
        // readable on A, the resolve's list setting nothing, D (recorded before it, submitted after) putting it back to a render target. At first
        // nothing of ours (the state not proven: the scene as drawn); once the submissions have shown the same state at the resolve 60 times in
        // a row the step draws from it (the debug layer checks every barrier of ours); D's barrier made unseen - the chain lost, nothing again
        chainMode = true;
        frame(true, d, 32, 32, got, &word);
        const float g3[3] = {grey, grey, grey};
        std::printf("  the submission order, its first frame: %.4f %.4f %.4f (the scene as drawn %.4f: not proven yet)\n", got[0], got[1], got[2], grey);
        CHECK(near3(got, g3));
        for (int i = 0; i < 70; ++i) frame(true, d, 32, 32, got, &word);
        std::printf("  ... after 71 frames: %.4f %.4f %.4f (the formula %.4f %.4f %.4f: drawn from the state the submissions proved)\n", got[0], got[1],
                    got[2], want[0], want[1], want[2]);
        CHECK(near3(got, want) && word == 0x5EED1234u);
        hideD = true;
        for (int i = 0; i < 5; ++i) frame(true, d, 32, 32, got, &word);
        std::printf("  ... D's barrier unseen, 5 frames on: %.4f %.4f %.4f (the scene as drawn: the order lost)\n", got[0], got[1], got[2]);
        CHECK(near3(got, g3));
        hideD = false;
        chainMode = false;
        // 2h: a view of the sky only (every probe point on the void - 0.209.16: the middle on the dome, "no lit picture" and the step went off):
        // the sky does not vote - after 30 such frames the step is still drawing
        for (int i = 0; i < 40; ++i) frame(true, d, 32, 32, got, &word);   // (the order lost above: back to the resolve's own states)
        CHECK(fogfx::Drawn());
        for (int i = 0; i < 30; ++i) frame(true, 0.0f, 32, 32, got, &word);
        std::printf("  30 frames of sky only: drawn %d (the sky does not vote)\n", fogfx::Drawn() ? 1 : 0);
        CHECK(fogfx::Drawn());
        // 2i: watching (Serg 2026-11-09: the sinks cost ~0.5 ms of CPU a present): 130 steps in a row with the same scene and depth - only they are
        // watched, the step still fogs as the formula; then the scene's state not known (the layout of 2g, the order not proven) - the full
        // search again at once
        for (int i = 0; i < 130; ++i) frame(true, d, 32, 32, got, &word);
        expect(d, 32, 32, want);
        std::printf("  130 steps in a row: watching %d, %.4f %.4f %.4f (the formula %.4f %.4f %.4f)\n", fogfx::Watching() ? 1 : 0, got[0], got[1], got[2],
                    want[0], want[1], want[2]);
        CHECK(fogfx::Watching() && near3(got, want) && word == 0x5EED1234u);
        chainMode = true;
        frame(true, d, 32, 32, got, &word);
        chainMode = false;
        std::printf("  ... the scene's state not known: watching %d (the full search again)\n", fogfx::Watching() ? 1 : 0);
        CHECK(!fogfx::Watching());
    }
    // 2e: and the game's half-size effects target bound with the same depth after the scene (PC1 2026-10-28: that layer was taken for the
    // scene and fogged - no fog at all): the scene, of the depth's own size, still
    halfLayer = true;
    {
        const float d = nearPlane / 45.0f;
        frame(true, d, 32, 32, got, &word);
        expect(d, 32, 32, want);
        std::printf("  a half-size effects target after the scene, pixel 32 32: %.4f %.4f %.4f (the formula %.4f %.4f %.4f)\n", got[0], got[1], got[2],
                    want[0], want[1], want[2]);
        CHECK(near3(got, want) && word == 0x5EED1234u);
    }
    halfLayer = false;
    frameIsR11 = false;
    sceneElsewhere = false;
    // 2e': a full-size R11G11B10 target bound with the depth after the scene, not read by the resolve (PC1 2026-10-28 17:43: the step ran on
    // the target last bound with the depth every frame and no fog reached the picture) - the resolve's own tables name the scene: it is fogged
    decoyAfter = true;
    {
        const float d = nearPlane / 45.0f;
        frame(true, d, 32, 32, got, &word);
        expect(d, 32, 32, want);
        std::printf("  a decoy target bound with the depth after the scene, the resolve's tables read the scene: %.4f %.4f %.4f (the formula %.4f %.4f "
                    "%.4f)\n", got[0], got[1], got[2], want[0], want[1], want[2]);
        CHECK(near3(got, want) && word == 0x5EED1234u);
    }
    // ... and without the views known (the hook saw none made): the decoy, the last bound with the depth, taken - nothing of ours on what the
    // resolve reads (its state on the list not known: not drawn, the scene as drawn)
    {
        const auto keep = g_srvs;
        g_srvs.clear();
        frame(true, nearPlane / 45.0f, 32, 32, got, &word);
        std::printf("  ... the views not known: %.4f %.4f %.4f (the scene's %.4f - the decoy taken)\n", got[0], got[1], got[2], grey);
        CHECK(std::fabs(got[0] - grey) < 0.01f && word == 0x5EED1234u);
        g_srvs = keep;
    }
    // 2e'': the decoy is the effects' additive target - bound in one call with the effects' RGBA16F target (0.209.14's flat trace: R11G11B10F
    // of the scene's size, so it passed for the scene, was fogged and laid over the flame): never taken - with the views not known the scene,
    // the last one bound with the depth that is not it, is fogged
    {
        const D3D12_CPU_DESCRIPTOR_HANDLE pair[2] = {rtvAccumFx, rtvDecoy};
        k.fxTargets((ID3D12GraphicsCommandList*)(uintptr_t)0x5CE80, 2, pair, false, nullptr, nullptr);
        const auto keep = g_srvs;
        g_srvs.clear();
        const float d = nearPlane / 45.0f;
        frame(true, d, 32, 32, got, &word);
        expect(d, 32, 32, want);
        std::printf("  the decoy is the effects' additive target, the views not known: %.4f %.4f %.4f (the scene fogged, the formula %.4f %.4f %.4f)\n",
                    got[0], got[1], got[2], want[0], want[1], want[2]);
        CHECK(near3(got, want) && word == 0x5EED1234u);
        g_srvs = keep;
    }
    decoyAfter = false;
    // 2f: the fail safe (PC1 2026-10-28 16:52, 0be5454f: a state guessed for the scene, the GPU hung in our copy back into it) - the depth's
    // or the scene's state not set on the resolve's own list: nothing of ours, the scene as drawn, the game's dispatch untouched
    depthUntold = true;
    frame(true, nearPlane / 45.0f, 32, 32, got, &word);
    std::printf("  the depth's state not known on the resolve's list: %.4f %.4f %.4f (the scene's %.4f)\n", got[0], got[1], got[2], grey);
    CHECK(std::fabs(got[0] - grey) < 0.01f && std::fabs(got[2] - grey) < 0.01f && word == 0x5EED1234u);
    depthUntold = false;
    // the scene's and the depth's read states told on another list this frame (PC1, 17:16: none on the resolve's): taken - fogged
    sceneElse = 1;
    depthElsewhere = true;
    {
        const float d = nearPlane / 45.0f;
        frame(true, d, 32, 32, got, &word);
        expect(d, 32, 32, want);
        std::printf("  the scene's and the depth's read states set on another list this frame: %.4f %.4f %.4f (the formula %.4f %.4f %.4f)\n", got[0],
                    got[1], got[2], want[0], want[1], want[2]);
        CHECK(near3(got, want) && word == 0x5EED1234u);
    }
    depthElsewhere = false;
    // ... the scene's last told elsewhere a render target (the next frame's, recorded early): not taken
    sceneElse = 2;
    frame(true, nearPlane / 45.0f, 32, 32, got, &word);
    std::printf("  the scene's last state set elsewhere a render target: %.4f %.4f %.4f (the scene's %.4f)\n", got[0], got[1], got[2], grey);
    CHECK(std::fabs(got[0] - grey) < 0.01f && std::fabs(got[2] - grey) < 0.01f && word == 0x5EED1234u);
    // ... a read state told elsewhere last frame only (stale): not taken
    sceneElse = 1;
    frame(true, nearPlane / 45.0f, 32, 32, got, &word);
    sceneElse = 3;
    frame(true, nearPlane / 45.0f, 32, 32, got, &word);
    std::printf("  the scene's read state set elsewhere only last frame: %.4f %.4f %.4f (the scene's %.4f)\n", got[0], got[1], got[2], grey);
    CHECK(std::fabs(got[0] - grey) < 0.01f && std::fabs(got[2] - grey) < 0.01f && word == 0x5EED1234u);
    sceneElse = 0;
    // ... and again known: fogged
    {
        const float d = nearPlane / 45.0f;
        frame(true, d, 32, 32, got, &word);
        expect(d, 32, 32, want);
        CHECK(near3(got, want) && word == 0x5EED1234u);
    }
    // 2g: [fog] game_fog_fx_depth with the game's fog - a hole in drawn ground (no depth, four drawn neighbours: a dithered far tree's edge,
    // Serg 2026-10-31 "a band of dark specks") and the middle of a line of three (two drawn neighbours: as a rim round a plank against the
    // void, 85.png's dark outlines) fogged by their neighbours' depth; our effect's fog: left to the post pass
    holes = holeKey = true;
    {
        const float d = nearPlane / 45.0f;
        frame(true, d, 10, 10, got, &word);
        expect(d, 10, 10, want);
        std::printf("  a hole in drawn ground, pixel 10 10: %.4f %.4f %.4f (the formula at its neighbours' depth %.4f %.4f %.4f)\n", got[0], got[1], got[2],
                    want[0], want[1], want[2]);
        CHECK(near3(got, want) && std::fabs(got[0] - grey) > 0.02f);
        frame(true, d, 21, 40, got, &word);
        expect(d, 21, 40, want);
        std::printf("  a rim (two drawn neighbours), pixel 21 40: %.4f %.4f %.4f (the formula at its neighbours' depth %.4f %.4f %.4f)\n", got[0], got[1],
                    got[2], want[0], want[1], want[2]);
        CHECK(near3(got, want) && std::fabs(got[0] - grey) > 0.02f);
        ours = true;
        frame(true, d, 10, 10, got, &word);
        std::printf("  ... with our effect's fog, pixel 10 10: %.4f %.4f %.4f (the scene's %.4f)\n", got[0], got[1], got[2], grey);
        CHECK(std::fabs(got[0] - grey) < 0.01f && std::fabs(got[2] - grey) < 0.01f);
        ours = false;
        holeKey = false;
        frame(true, d, 10, 10, got, &word);   // (game_fog_fx_depth off: the hole fogged all the same - it goes with the step)
        expect(d, 10, 10, want);
        CHECK(near3(got, want));
        holes = false;
    }
    // 2h: [sky] game_sky - the game's dome far out (its depth near / 1130, under the band's near / (1130 x 0.98)): left as it is, the
    // game's pass leaves it too (Serg 2026-10-31, 0.192.19: its low part fogged behind the trees - "обводка вокруг объектов"); without the
    // dome told, the same depth is fogged as before; a hole beside the dome (holes on) is the sky's, not fogged by the dome's depth
    {
        const float dome = nearPlane / 1130.0f, band = nearPlane / (1130.0f * 0.98f);
        skyDepth = band;
        frame(true, dome, 32, 32, got, &word);
        std::printf("  the game's dome (its band told), pixel 32 32: %.4f %.4f %.4f (the scene's %.4f)\n", got[0], got[1], got[2], grey);
        CHECK(std::fabs(got[0] - grey) < 0.01f && std::fabs(got[2] - grey) < 0.01f);
        holes = holeKey = true;
        frame(true, dome, 10, 10, got, &word);
        std::printf("  a hole beside the dome only, pixel 10 10: %.4f %.4f %.4f (the scene's %.4f)\n", got[0], got[1], got[2], grey);
        CHECK(std::fabs(got[0] - grey) < 0.01f && std::fabs(got[2] - grey) < 0.01f);
        // ... off the view's middle, the dome's depth there near / (1130 x cos) - past the band by the depth alone (Serg on 0.192.39, 126.webp:
        // a ten-sided disc straight ahead the only sky, the dome round it fogged as ground): the sky by the pixel's view ray, and a hole there
        auto ray = [](float px, float py) {
            const float x = (px + 0.5f) / 32.0f - 1.0f, y = 1.0f - (py + 0.5f) / 32.0f;
            return std::sqrt(1.0f + x * x + y * y);
        };
        frame(true, dome * ray(10, 10), 10, 10, got, &word);
        std::printf("  ... a hole beside the dome off the middle (its depth x%.3f), pixel 10 10: %.4f %.4f %.4f\n", ray(10, 10), got[0], got[1], got[2]);
        CHECK(dome * ray(10, 10) > band && std::fabs(got[0] - grey) < 0.01f && std::fabs(got[2] - grey) < 0.01f);
        holes = holeKey = false;
        frame(true, dome * ray(2, 2), 2, 2, got, &word);
        std::printf("  the dome near the corner (its depth x%.3f), pixel 2 2: %.4f %.4f %.4f\n", ray(2, 2), got[0], got[1], got[2]);
        CHECK(std::fabs(got[0] - grey) < 0.01f && std::fabs(got[2] - grey) < 0.01f);
        frame(true, dome * ray(2, 2) * 1.05f, 2, 2, got, &word);   // (5 % nearer there than the dome: ground, fogged)
        expect(dome * ray(2, 2) * 1.05f, 2, 2, want);
        std::printf("  ... 5 %% nearer than the dome there: %.4f %.4f %.4f (the formula %.4f %.4f %.4f)\n", got[0], got[1], got[2], want[0], want[1], want[2]);
        CHECK(near3(got, want) && std::fabs(got[0] - grey) > 0.02f);
        skyDepth = 0.0f;
        frame(true, dome, 32, 32, got, &word);
        expect(dome, 32, 32, want);
        std::printf("  ... no dome told, the same depth: %.4f %.4f %.4f (the formula %.4f %.4f %.4f)\n", got[0], got[1], got[2], want[0], want[1], want[2]);
        CHECK(near3(got, want) && std::fabs(got[0] - grey) > 0.02f);
    }
    // 2i: the game's fog at a silhouette's edge against the game's dome (Serg on 0.192.34 - 0.192.40: "чтобы туман игры был без полос
    // вокруг объектов"). A dark fence far in the fog (x < 32) against a lighter sky (x > 32); the edge column x = 32 blends the two half and
    // half but holds one depth. Its pixel must lie between the fogged fence and the sky beside it, whichever depth it holds - left whole
    // as the sky, the fence's unfogged half in it was a dark line round every fogged silhouette. Our effect's fog in the step keeps its
    // old answer (the old fog not touched)
    {
        const float dome = nearPlane / 1130.0f, band = nearPlane / (1130.0f * 0.98f), fence = nearPlane / 200.0f;
        const float T[3] = {0.02f, 0.02f, 0.02f}, S[3] = {0.3f, 0.15f, 0.08f};
        const float E[3] = {0.5f * (T[0] + S[0]), 0.5f * (T[1] + S[1]), 0.5f * (T[2] + S[2])};
        skyDepth = band;
        curveOn = true;
        float kFence;
        {   // (the fence's keep by the formula: the scene's grey toward the fog)
            float w[3];
            expect(fence, 31, 32, w);
            kFence = (w[0] - fog[0]) / (grey - fog[0]);
        }
        float Tf[3];
        for (int i = 0; i < 3; ++i) Tf[i] = fog[i] + (T[i] - fog[i]) * kFence;
        auto between = [&](const float v[3]) {
            for (int i = 0; i < 3; ++i)
                if (v[i] < std::fmin(Tf[i], S[i]) - 0.006f || v[i] > std::fmax(Tf[i], S[i]) + 0.006f) return false;
            return true;
        };
        const char* const kEdge[3] = {"the sky's depth", "the fence's depth", "no depth (under the dome)"};
        const float edgeDepth[3] = {dome, fence, 0.0f};
        for (int e = 0; e < 3; ++e) {
            patches = {{{0, 0, 32, 64}, fence, {T[0], T[1], T[2]}}, {{33, 0, 64, 64}, dome, {S[0], S[1], S[2]}},
                       {{32, 0, 33, 64}, edgeDepth[e], {E[0], E[1], E[2]}}};
            frame(true, dome, 32, 32, got, &word);
            std::printf("  the edge with %s: %.4f %.4f %.4f (the fogged fence %.4f %.4f %.4f, the sky %.4f %.4f %.4f)\n", kEdge[e], got[0], got[1],
                        got[2], Tf[0], Tf[1], Tf[2], S[0], S[1], S[2]);
            CHECK(between(got));
            float f[3], s[3];
            frame(true, dome, 30, 32, f, &word);
            frame(true, dome, 34, 32, s, &word);
            CHECK(near3(f, Tf) && std::fabs(s[0] - S[0]) < 0.01f && std::fabs(s[2] - S[2]) < 0.01f);   // (the fence and the sky away from it as before)
        }
        // our effect's fog in the step: as it was - the edge with the sky's depth left whole, the one with the fence's fogged by it
        ours = true;
        patches = {{{0, 0, 32, 64}, fence, {T[0], T[1], T[2]}}, {{33, 0, 64, 64}, dome, {S[0], S[1], S[2]}}, {{32, 0, 33, 64}, dome, {E[0], E[1], E[2]}}};
        frame(true, dome, 32, 32, got, &word);
        std::printf("  ... our effect's fog, the edge with the sky's depth: %.4f %.4f %.4f (left as drawn, %.4f)\n", got[0], got[1], got[2], E[0]);
        CHECK(std::fabs(got[0] - E[0]) < 0.006f && std::fabs(got[1] - E[1]) < 0.006f);
        ours = false;
        curveOn = false;
        patches.clear();
        skyDepth = 0.0f;
    }
    // 3: the void
    frame(true, 0.0f, 32, 32, got, &word);
    std::printf("  on, the void: %.4f %.4f %.4f (the scene's %.4f)\n", got[0], got[1], got[2], grey);
    CHECK(std::fabs(got[0] - grey) < 0.01f && std::fabs(got[2] - grey) < 0.01f && word == 0x5EED1234u);
    // 4: off
    frame(false, nearPlane / 45.0f, 32, 32, got, &word);
    std::printf("  off: %.4f %.4f %.4f (the scene's %.4f)\n", got[0], got[1], got[2], grey);
    CHECK(std::fabs(got[0] - grey) < 0.01f && word == 0x5EED1234u);
    CHECK(!fogfx::Drawn() && !fogfx::OursDrawn());
    // 5: [fog] game_fog_fx_depth - the effects fogged by their own distance (Serg 2026-10-31, a Rogue camp torch at night: the far fires at
    // full light in full fog). The game's effects as it draws them: a weighted-blended colour target (RGBA16F, the scene's size) and the
    // additive light's (R11G11B10, half the size, bound with the full-size depth), each a pipeline of the game's - blended, no depth written
    // - told as made (OnGraphicsPso), its draw told (fxDraw) before it is made: its twin draws the effect's depth into ours, the game's
    // pipeline and targets bound again for its own draw. A resolve of the game's kind reads the scene, the colour and the light:
    // R = S (1 - 0.5) + 0.5 T + G where the colour has weight (T = rgb / a), else S + G.
    {
        const float fxColour[3] = {0.8f, 0.4f, 0.1f}, fxGlow[3] = {1.0f, 0.5f, 0.25f};
        D3D12_CLEAR_VALUE zero16{DXGI_FORMAT_R16G16B16A16_FLOAT};
        ID3D12Resource* accum = Tex(dev, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET, &zero16);
        D3D12_CLEAR_VALUE one16{DXGI_FORMAT_R16G16B16A16_FLOAT, {1, 1, 1, 1}};
        ID3D12Resource* reveal = Tex(dev, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET, &one16);
        ID3D12Resource* glow = nullptr;
        {
            D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
            D3D12_RESOURCE_DESC d{};
            d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            d.Width = 32;
            d.Height = 32;
            d.DepthOrArraySize = d.MipLevels = 1;
            d.Format = DXGI_FORMAT_R11G11B10_FLOAT;
            d.SampleDesc.Count = 1;
            d.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, __uuidof(ID3D12Resource), (void**)&glow);
        }
        CHECK(accum && glow);
        ID3D12DescriptorHeap *fxRtvHeap = nullptr, *fxSrvHeap = nullptr;
        D3D12_DESCRIPTOR_HEAP_DESC fh{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 3};
        dev->CreateDescriptorHeap(&fh, __uuidof(ID3D12DescriptorHeap), (void**)&fxRtvHeap);
        fh = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 3, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
        dev->CreateDescriptorHeap(&fh, __uuidof(ID3D12DescriptorHeap), (void**)&fxSrvHeap);
        const D3D12_CPU_DESCRIPTOR_HANDLE rtvAccum = fxRtvHeap->GetCPUDescriptorHandleForHeapStart(), rtvGlow{rtvAccum.ptr + rtvInc};
        dev->CreateRenderTargetView(accum, nullptr, rtvAccum);
        dev->CreateRenderTargetView(glow, nullptr, rtvGlow);
        const D3D12_CPU_DESCRIPTOR_HANDLE rtvReveal{rtvAccum.ptr + 2 * rtvInc};
        dev->CreateRenderTargetView(reveal, nullptr, rtvReveal);
        g_rtvs[rtvReveal.ptr] = reveal;
        g_rtvs[rtvAccum.ptr] = accum;
        g_rtvs[rtvGlow.ptr] = glow;
        const D3D12_CPU_DESCRIPTOR_HANDLE srv0 = fxSrvHeap->GetCPUDescriptorHandleForHeapStart();
        D3D12_SHADER_RESOURCE_VIEW_DESC v{DXGI_FORMAT_R11G11B10_FLOAT, D3D12_SRV_DIMENSION_TEXTURE2D, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING};
        v.Texture2D.MipLevels = 1;
        dev->CreateShaderResourceView(scene, &v, srv0);
        g_srvs[srv0.ptr] = scene;
        v.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        dev->CreateShaderResourceView(accum, &v, D3D12_CPU_DESCRIPTOR_HANDLE{srv0.ptr + g_srvInc});
        v.Format = DXGI_FORMAT_R11G11B10_FLOAT;
        dev->CreateShaderResourceView(glow, &v, D3D12_CPU_DESCRIPTOR_HANDLE{srv0.ptr + 2 * g_srvInc});
        // the effects' root signature (their depth a root constant) and pipelines; the resolve's (a table of three)
        D3D12_ROOT_PARAMETER ep{};
        ep.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        ep.Constants = {0, 0, 1};
        ID3D12RootSignature* fxRoot = RootSig(dev, {1, &ep, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT});
        const D3D12_DESCRIPTOR_RANGE r3{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0, 0, 0};
        D3D12_ROOT_PARAMETER rp{};
        rp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rp.DescriptorTable = {1, &r3};
        ID3D12RootSignature* resolveRoot = RootSig(dev, {1, &rp, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE});
        const char* fxVs = "cbuffer C : register(b0) { float z; }; float4 main(uint id : SV_VertexID) : SV_Position { float2 p = float2((id << 1) & 2, id & 2); "
                           "return float4(p * 2 - 1, z, 1); }";
        const char* accumPs = "float4 main() : SV_Target { return float4(0.8, 0.4, 0.1, 1.0); }";
        const char* glowPs = "float4 main() : SV_Target { return float4(1.0, 0.5, 0.25, 1.0); }";
        const char* resolvePs = "Texture2D<float3> S : register(t0); Texture2D<float4> A : register(t1); Texture2D<float3> G : register(t2); "
                                "float4 main(float4 p : SV_Position) : SV_Target { int2 q = int2(p.xy); float4 a = A.Load(int3(q, 0)); "
                                "float k = a.a > 0 ? 0.5 : 0.0; float3 t = a.a > 0 ? a.rgb / a.a : 0; "
                                "return float4(S.Load(int3(q, 0)) * (1 - k) + t * k + G.Load(int3(q / 2, 0)), 1); }";
        const std::vector<uint8_t> fvs = Compile(fxVs, "vs_5_0"), aps = Compile(accumPs, "ps_5_0"), gps = Compile(glowPs, "ps_5_0"),
                                   rps = Compile(resolvePs, "ps_5_0");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC ed{};
        ed.pRootSignature = fxRoot;
        ed.VS = {fvs.data(), fvs.size()};
        ed.PS = {aps.data(), aps.size()};
        ed.BlendState.RenderTarget[0] = {TRUE, FALSE, D3D12_BLEND_ONE, D3D12_BLEND_ONE, D3D12_BLEND_OP_ADD, D3D12_BLEND_ONE, D3D12_BLEND_ONE,
                                         D3D12_BLEND_OP_ADD, D3D12_LOGIC_OP_NOOP, D3D12_COLOR_WRITE_ENABLE_ALL};
        ed.SampleMask = ~0u;
        ed.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        ed.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        ed.RasterizerState.DepthClipEnable = TRUE;
        ed.DepthStencilState.DepthEnable = TRUE;   // (tested against the scene's depth, not written: reverse-Z)
        ed.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        ed.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        ed.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        ed.NumRenderTargets = 1;
        ed.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        ed.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
        ed.SampleDesc.Count = 1;
        ID3D12PipelineState *fxAccum = nullptr, *fxGlowPso = nullptr, *fxResolve = nullptr;
        CHECK(fxRoot && SUCCEEDED(dev->CreateGraphicsPipelineState(&ed, __uuidof(ID3D12PipelineState), (void**)&fxAccum)));
        fogfx::OnGraphicsPso(dev, &ed, fxAccum);
        // a hair / foliage pipeline of the effects pass (a leaf card's see-through part): the same targets, its own colour - taken as one of the
        // lit / hair pipelines (in the game: by its pixel shader's hash)
        const float leafColour[3] = {0.2f, 0.6f, 0.2f};
        const float leafAlpha = 0.8f, leafZ = 44.0f, leafW = 1e6f / (1e5f + leafZ * leafZ * leafZ);
        char leafSrc[320];
        std::snprintf(leafSrc, sizeof leafSrc, "struct O { float4 c : SV_Target0; float4 r : SV_Target1; }; O main() { O o; float a = %.6f, w = %.6f; "
                      "o.c = float4(float3(0.2, 0.6, 0.2) * a * w, a * w); o.r = float4(1, 1, 1, a); return o; }", leafAlpha, leafW);
        const std::vector<uint8_t> lps = Compile(leafSrc, "ps_5_0");
        ID3D12PipelineState* fxLeaf = nullptr;
        {
            D3D12_GRAPHICS_PIPELINE_STATE_DESC ld = ed;
            ld.PS = {lps.data(), lps.size()};
            ld.NumRenderTargets = 2;
            ld.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT;
            ld.BlendState.IndependentBlendEnable = TRUE;
            ld.BlendState.RenderTarget[1] = {TRUE, FALSE, D3D12_BLEND_ZERO, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_OP_ADD, D3D12_BLEND_ZERO,
                                             D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_OP_ADD, D3D12_LOGIC_OP_NOOP, D3D12_COLOR_WRITE_ENABLE_ALL};
            CHECK(SUCCEEDED(dev->CreateGraphicsPipelineState(&ld, __uuidof(ID3D12PipelineState), (void**)&fxLeaf)));
        }
        fogfx::MarkFogBehind(fxLeaf);
        const float fireZ = 60.0f, fireW = 1e6f / (1e5f + fireZ * fireZ * fireZ);
        char fireSrc[320];
        std::snprintf(fireSrc, sizeof fireSrc, "struct O { float4 c : SV_Target0; float4 r : SV_Target1; }; O main() { O o; float a = %.6f, w = %.6f; "
                      "o.c = float4(float3(0.8, 0.4, 0.1) * a * w, a * w); o.r = float4(1, 1, 1, a); return o; }", leafAlpha, fireW);
        const std::vector<uint8_t> fps = Compile(fireSrc, "ps_5_0");
        ID3D12PipelineState* fxFarFire = nullptr;
        {
            D3D12_GRAPHICS_PIPELINE_STATE_DESC fd = ed;
            fd.PS = {fps.data(), fps.size()};
            fd.NumRenderTargets = 2;
            fd.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT;
            fd.BlendState.IndependentBlendEnable = TRUE;
            fd.BlendState.RenderTarget[1] = {TRUE, FALSE, D3D12_BLEND_ZERO, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_OP_ADD, D3D12_BLEND_ZERO,
                                             D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_OP_ADD, D3D12_LOGIC_OP_NOOP, D3D12_COLOR_WRITE_ENABLE_ALL};
            CHECK(SUCCEEDED(dev->CreateGraphicsPipelineState(&fd, __uuidof(ID3D12PipelineState), (void**)&fxFarFire)));
        }
        ed.PS = {gps.data(), gps.size()};
        ed.DepthStencilState.DepthEnable = FALSE;   // (the half-size layer: the full-size depth bound, not tested)
        ed.RTVFormats[0] = DXGI_FORMAT_R11G11B10_FLOAT;
        CHECK(SUCCEEDED(dev->CreateGraphicsPipelineState(&ed, __uuidof(ID3D12PipelineState), (void**)&fxGlowPso)));
        fogfx::OnGraphicsPso(dev, &ed, fxGlowPso);
        D3D12_GRAPHICS_PIPELINE_STATE_DESC rd = pd;
        rd.pRootSignature = resolveRoot;
        rd.PS = {rps.data(), rps.size()};
        CHECK(resolveRoot && SUCCEEDED(dev->CreateGraphicsPipelineState(&rd, __uuidof(ID3D12PipelineState), (void**)&fxResolve)));
        fogfx::MarkResolve(fxResolve);

        // one frame's end: the wall at the depth wallD, the effects at effD; out at (px, py) back
        // the game records its effects on another list than its resolve's (PC1 2026-10-31 16:03, 0.192.21: "0 steps fogged the effects")
        ID3D12CommandAllocator* alloc2 = nullptr;
        ID3D12GraphicsCommandList* cl2 = nullptr;
        dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&alloc2);
        dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc2, nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&cl2);
        cl2->Close();
        bool apart = false;   // the scene and the effects on cl2, the resolve on cl (run in that order)
        bool pairTold = false;   // the weighted colour bound in one call with the additive target (the game's binding: the effects' pair noted)
        bool flameOn = true;     // the particle (the flame) drawn
        float leafD = 0.0f;      // the leaf card drawn at this depth (0: not)
        float farFireD = 0.0f;   // a far fire drawn as the game's particles are (its weight and revealage) at this depth (0: not)
        bool stepOn = true;   // (off: the step not asked - the game's fog alone)
        auto fxFrame = [&](bool fxDepth, float wallD, float effD, float px, float py, float rgb[3]) {
            fogfx::Params p;
            p.on = stepOn;
            p.fxDepth = fxDepth;
            p.plane = plane;
            p.density = density;
            std::memcpy(p.colour, fog, sizeof fog);
            p.nearPlane = nearPlane;
            fogfx::Configure(p);
            fogfx::Frame();
            alloc->Reset();
            cl->Reset(alloc, nullptr);
            k.fxReset(cl);
            ID3D12GraphicsCommandList* ec = cl;
            if (apart) {
                alloc2->Reset();
                cl2->Reset(alloc2, nullptr);
                k.fxReset(cl2);
                ec = cl2;
            }
            k.fxTargets(ec, 1, &rtvScene, false, depth, &dsv);
            ec->OMSetRenderTargets(1, &rtvScene, FALSE, &dsv);
            const float c[4] = {grey, grey, grey, 1}, none[4] = {0, 0, 0, 0};
            ec->ClearRenderTargetView(rtvScene, c, 0, nullptr);
            ec->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, wallD, 0, 0, nullptr);
            ec->ClearRenderTargetView(rtvAccum, none, 0, nullptr);
            const float ones[4] = {1, 1, 1, 1};
            ec->ClearRenderTargetView(rtvReveal, ones, 0, nullptr);
            ec->ClearRenderTargetView(rtvGlow, none, 0, nullptr);
            // the effects
            natived3d::RootCall rc{natived3d::RootCall::kGraphicsRootSignature, 0, (uint64_t)(uintptr_t)fxRoot};
            k.fxRoot(ec, rc);
            ec->SetGraphicsRootSignature(fxRoot);
            uint32_t zb = 0;
            std::memcpy(&zb, &effD, 4);
            ec->SetGraphicsRoot32BitConstant(0, zb, 0);
            ec->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            const D3D12_VIEWPORT full{0, 0, 64, 64, 0, 1}, half{0, 0, 32, 32, 0, 1};
            const D3D12_RECT fullR{0, 0, 64, 64}, halfR{0, 0, 32, 32};
            ec->RSSetViewports(1, &full);
            ec->RSSetScissorRects(1, &fullR);
            if (pairTold) {
                const D3D12_CPU_DESCRIPTOR_HANDLE pr[3] = {rtvAccum, rtvReveal, rtvGlow};
                k.fxTargets(ec, 3, pr, false, nullptr, nullptr);
            }
            k.fxTargets(ec, 1, &rtvAccum, false, depth, &dsv);
            ec->OMSetRenderTargets(1, &rtvAccum, FALSE, &dsv);
            if (flameOn) {
                ec->SetPipelineState(fxAccum);
                k.fxBound(ec, fxAccum);
                k.fxDraw(ec, false, 3, 1, 0, 0, 0);
                ec->DrawInstanced(3, 1, 0, 0);
            }
            if (farFireD > 0.0f) {   // (a particle: never sent - fogged by its own distance in the game's own targets)
                const D3D12_CPU_DESCRIPTOR_HANDLE fr2[2] = {rtvAccum, rtvReveal};
                k.fxTargets(ec, 2, fr2, false, depth, &dsv);
                ec->OMSetRenderTargets(2, fr2, FALSE, &dsv);
                uint32_t fz = 0;
                std::memcpy(&fz, &farFireD, 4);
                ec->SetGraphicsRoot32BitConstant(0, fz, 0);
                ec->SetPipelineState(fxFarFire);
                k.fxBound(ec, fxFarFire);
                k.fxDraw(ec, false, 3, 1, 0, 0, 0);
                ec->DrawInstanced(3, 1, 0, 0);
            }
            if (leafD > 0.0f) {   // (told as the game's draw: the step may send it into its own targets)
                const D3D12_CPU_DESCRIPTOR_HANDLE lr[2] = {rtvAccum, rtvReveal};
                k.fxTargets(ec, 2, lr, false, depth, &dsv);
                ec->OMSetRenderTargets(2, lr, FALSE, &dsv);
                uint32_t lz = 0;
                std::memcpy(&lz, &leafD, 4);
                ec->SetGraphicsRoot32BitConstant(0, lz, 0);
                ec->SetPipelineState(fxLeaf);
                k.fxBound(ec, fxLeaf);
                k.fxDraw(ec, false, 3, 1, 0, 0, 0);
                ec->DrawInstanced(3, 1, 0, 0);
            }
            ec->RSSetViewports(1, &half);
            ec->RSSetScissorRects(1, &halfR);
            k.fxTargets(ec, 1, &rtvGlow, false, depth, &dsv);
            ec->OMSetRenderTargets(1, &rtvGlow, FALSE, &dsv);
            ec->SetPipelineState(fxGlowPso);
            k.fxBound(ec, fxGlowPso);
            k.fxDraw(ec, false, 3, 1, 0, 0, 0);
            ec->DrawInstanced(3, 1, 0, 0);
            // the resolve reads them all
            const D3D12_RESOURCE_STATES depthRead = D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            D3D12_RESOURCE_BARRIER rb[5] = {Tr(scene, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
                                            Tr(accum, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
                                            Tr(glow, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
                                            Tr(depth, D3D12_RESOURCE_STATE_DEPTH_WRITE, depthRead),
                                            Tr(reveal, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)};
            k.fxBarrier(ec, 5, rb);
            ec->ResourceBarrier(5, rb);
            if (apart) CHECK(SUCCEEDED(cl2->Close()));
            cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);   // (the resolve's list: its own input state)
            ID3D12DescriptorHeap* heaps[1] = {fxSrvHeap};
            k.fxHeaps(cl, 1, heaps);
            cl->SetDescriptorHeaps(1, heaps);
            rc = {natived3d::RootCall::kGraphicsRootSignature, 0, (uint64_t)(uintptr_t)resolveRoot};
            k.fxRoot(cl, rc);
            cl->SetGraphicsRootSignature(resolveRoot);
            rc = {natived3d::RootCall::kGraphicsTable, 0, fxSrvHeap->GetGPUDescriptorHandleForHeapStart().ptr};
            k.fxRoot(cl, rc);
            cl->SetGraphicsRootDescriptorTable(0, fxSrvHeap->GetGPUDescriptorHandleForHeapStart());
            cl->SetPipelineState(fxResolve);
            k.fxBound(cl, fxResolve);
            k.fxTargets(cl, 1, &rtvOut, false, depth, &dsv);
            cl->OMSetRenderTargets(1, &rtvOut, FALSE, &dsv);
            cl->RSSetViewports(1, &full);
            cl->RSSetScissorRects(1, &fullR);
            k.fxDraw(cl, false, 3, 1, 0, 0, 0);
            cl->DrawInstanced(3, 1, 0, 0);
            D3D12_RESOURCE_BARRIER bb[6] = {Tr(out, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE),
                                            Tr(scene, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
                                            Tr(accum, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
                                            Tr(glow, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
                                            Tr(depth, depthRead, D3D12_RESOURCE_STATE_DEPTH_WRITE),
                                            Tr(reveal, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)};
            cl->ResourceBarrier(6, bb);
            D3D12_TEXTURE_COPY_LOCATION dst{readback, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT}, src{out, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
            dst.PlacedFootprint.Footprint = {DXGI_FORMAT_R32G32B32A32_FLOAT, 64, 64, 1, 64 * 16};
            src.SubresourceIndex = 0;
            cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            bb[0] = Tr(out, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
            cl->ResourceBarrier(1, bb);
            CHECK(SUCCEEDED(cl->Close()));
            ID3D12CommandList* lists[2] = {cl2, cl};
            g_q->ExecuteCommandLists(apart ? 2 : 1, apart ? lists : lists + 1);
            g_q->Signal(fence, ++fv);
            while (fence->GetCompletedValue() < fv) Sleep(1);
            CHECK(SUCCEEDED(dev->GetDeviceRemovedReason()));
            CHECK(layerErrors() == 0);
            float* px4 = nullptr;
            const D3D12_RANGE all{0, 64 * 64 * 16};
            readback->Map(0, &all, (void**)&px4);
            const float* q = px4 + ((int)py * 64 + (int)px) * 4;
            rgb[0] = q[0]; rgb[1] = q[1]; rgb[2] = q[2];
            readback->Unmap(0, nullptr);
        };
        // what the fog leaves at the depth d seen at uv (the game's formula)
        auto keepAt = [&](float d, float u, float w) {
            const float z = nearPlane / d, x = (u * 2.0f - 1.0f) * z, y = (1.0f - w * 2.0f) * z;
            return std::fmin(1.0f, std::exp(-density * (std::sqrt(x * x + y * y + z * z + 1.0f) / plane - 1.0f)));
        };
        // the resolve's output at pixel (32, 32): the scene fogged by the wall (or left: the void), the effects by their own depth when on
        auto fxWant = [&](bool fxDepth, float wallD, float effD, float rgb[3]) {
            const float u = 32.5f / 64.0f, uh = 16.5f / 32.0f;
            const float ts = wallD > 0 ? keepAt(wallD, u, u) : 1.0f;
            const bool shown = effD >= wallD;   // (the colour's draw tested against the wall; the light's not)
            const float d = std::fmax(effD, wallD);
            const float ta = fxDepth ? keepAt(d, u, u) : 1.0f, tg = fxDepth ? keepAt(d, uh, uh) : 1.0f;
            for (int i = 0; i < 3; ++i) {
                const float s = fog[i] + (grey - fog[i]) * ts;
                const float t = fxColour[i] * ta + (wallD > 0 ? fog[i] * (1.0f - ta) : 0.0f);   // (over the void: faded out)
                rgb[i] = (shown ? s * 0.5f + t * 0.5f : s) + fxGlow[i] * tg;
            }
        };
        const float wall = nearPlane / 45.0f;
        struct Case { const char* what; bool fxDepth; float wallD, effD; };
        const Case cases[] = {
            {"near (10 units) in front of the wall at 45", true, wall, nearPlane / 10.0f},
            {"far (40 units) just in front of the wall at 45", true, wall, nearPlane / 40.0f},
            {"behind the wall (60 units): the colour hidden, the light by the wall's depth", true, wall, nearPlane / 60.0f},
            {"near, the key off: as before (unfogged)", false, wall, nearPlane / 10.0f},
            {"near, over the void: as they are", true, 0.0f, nearPlane / 10.0f},
            {"far (40 units) over the void: faded out by their own depth", true, 0.0f, nearPlane / 40.0f},
        };
        for (const Case& c : cases) {
            fxFrame(c.fxDepth, c.wallD, c.effD, 32, 32, got);
            fxWant(c.fxDepth, c.wallD, c.effD, want);
            float plain[3];
            fxWant(false, c.wallD, c.effD, plain);
            std::printf("  the effects %s: %.4f %.4f %.4f (want %.4f %.4f %.4f; unfogged %.4f %.4f %.4f)\n", c.what, got[0], got[1], got[2], want[0],
                        want[1], want[2], plain[0], plain[1], plain[2]);
            CHECK(near3(got, want));
            if (c.fxDepth && c.effD < nearPlane / 30.0f) CHECK(!near3(got, plain));   // (past the plane's 30 units: visibly fogged)
        }
        // 5b: the effects' own depth off, their pair known. Serg 2026-11-09 on 0.209.16: branches, bushes and hair (the effects pass) escaped the
        // fog; on 0.209.17, with the whole weighted colour fogged by the depth behind it, "затененный забор перекрывает огонь" - the flame took
        // the fogged fence's colour. Now per draw: a hair / foliage draw goes into the step's own target and is fogged by the depth behind it;
        // a particle (the flame) stays as drawn. R = S (1 - 0.5) + 0.5 (flame + leaf fogged) / count + G
        pairTold = true;
        {
            const float u = 32.5f / 64.0f;
            const float tl = keepAt(nearPlane / leafZ, u, u);   // (the leaf by its own distance, from its weight and opacity)
            const float lw = leafAlpha * leafW;                 // (its share in the weighted average)
            auto want5b = [&](bool flame, bool leaf, float wallD, float out[3]) {
                const float ts = wallD > 0 ? keepAt(wallD, u, u) : 1.0f;
                // (the additive light: where a particle is, by its distance - the test's flame writes no revealage: unreadable, as drawn; where
                // none is, by the depth behind it - the glow's pixel (16, 16) seen from the full-size pixel (33, 33))
                const float ug = 33.5f / 64.0f;
                const float tg = flame ? 1.0f : wallD > 0 ? keepAt(wallD, ug, ug) : 1.0f;
                for (int i = 0; i < 3; ++i) {
                    const float s = fog[i] + (grey - fog[i]) * ts;
                    const float leafFogged = leafColour[i] * tl + fog[i] * (1.0f - tl);
                    const float n = (flame ? 1.0f : 0.0f) + (leaf ? lw : 0.0f);
                    const float t = n > 0 ? ((flame ? fxColour[i] : 0.0f) + (leaf ? leafFogged * lw : 0.0f)) / n : 0.0f;
                    out[i] = (n > 0 ? s * 0.5f + t * 0.5f : s) + fxGlow[i] * tg;
                }
            };
            struct C5 { const char* what; bool flame; float leaf, wallD; };
            const C5 cs[] = {
                {"the flame alone at 10 units before the wall at 45: as drawn (bright)", true, 0.0f, wall},
                {"a leaf card at 44 before the wall alone: fogged by its own distance", false, nearPlane / leafZ, wall},
                {"the flame at 10 and a leaf at 44: the leaf fogged, the flame as drawn", true, nearPlane / leafZ, wall},
                {"a leaf card at 44 against the sky (the void behind it): fogged by its own distance all the same", false, nearPlane / leafZ, 0.0f},
            };
            for (const C5& c : cs) {
                flameOn = c.flame;
                leafD = c.leaf;
                for (int f = 0; f < 3; ++f) fxFrame(false, c.wallD, nearPlane / 10.0f, 32, 32, got);   // (frames first: the leaf sent to the step's targets once they fold)
                want5b(c.flame, c.leaf > 0.0f, c.wallD, want);
                std::printf("  %s: %.4f %.4f %.4f (want %.4f %.4f %.4f)\n", c.what, got[0], got[1], got[2], want[0], want[1], want[2]);
                CHECK(near3(got, want));
            }
            // standing aside (no lit picture proven - mode 2): nothing of ours at all - the flame and the leaf drawn into the game's targets as
            // they are, the picture pixel-identical to the step not asked (0.209.21, VR F4: with the step flipping off, the leaves were lost)
            {
                flameOn = true;
                leafD = nearPlane / leafZ;
                fogfx::ResetSceneCheck(2);
                float aside[3], off[3];
                for (int f = 0; f < 3; ++f) fxFrame(false, wall, nearPlane / 10.0f, 32, 32, aside);
                stepOn = false;
                for (int f = 0; f < 2; ++f) fxFrame(false, wall, nearPlane / 10.0f, 32, 32, off);
                stepOn = true;
                std::printf("  standing aside: %.5f %.5f %.5f, the step not asked: %.5f %.5f %.5f (identical)\n", aside[0], aside[1], aside[2], off[0],
                            off[1], off[2]);
                CHECK(aside[0] == off[0] && aside[1] == off[1] && aside[2] == off[2] && !fogfx::Drawn());
                fogfx::ResetSceneCheck(0);
                leafD = 0.0f;
                for (int f = 0; f < 3; ++f) fxFrame(false, wall, nearPlane / 10.0f, 32, 32, got);   // (back: the fold, then the leaf sent again)
            }
            // a far fire (60 units, the sky behind it) drawn as the game's particles are: fogged by its own distance, its additive light
            // faded by it (Serg's A/B on 0.209.18: "far fires show as red / orange blobs with halos through the fog")
            flameOn = false;
            leafD = 0.0f;
            farFireD = nearPlane / fireZ;
            for (int f = 0; f < 3; ++f) fxFrame(false, 0.0f, nearPlane / 10.0f, 32, 32, got);
            {
                const float tf = keepAt(nearPlane / fireZ, u, u), ug = 33.5f / 64.0f, tg = keepAt(nearPlane / fireZ, ug, ug);
                for (int i = 0; i < 3; ++i) want[i] = grey * 0.5f + (fxColour[i] * tf + fog[i] * (1.0f - tf)) * 0.5f + fxGlow[i] * tg;
            }
            std::printf("  a far fire (60 units, the sky behind): %.4f %.4f %.4f (want %.4f %.4f %.4f: fogged by its own distance, its light faded)\n",
                        got[0], got[1], got[2], want[0], want[1], want[2]);
            CHECK(near3(got, want));
            farFireD = 0.0f;
            flameOn = true;
            fxFrame(false, 0.0f, nearPlane / 10.0f, 32, 32, got);
            fxWant(false, 0.0f, nearPlane / 10.0f, want);
            std::printf("  ... the flame over the void: %.4f %.4f %.4f (want %.4f %.4f %.4f: as it is)\n", got[0], got[1], got[2], want[0], want[1], want[2]);
            CHECK(near3(got, want));
        }
        pairTold = false;
        // twice in a row on one list's recording: the second frame's depth starts clean (a near effect before, a far one now)
        fxFrame(true, wall, nearPlane / 10.0f, 32, 32, got);
        fxFrame(true, wall, nearPlane / 40.0f, 32, 32, got);
        fxWant(true, wall, nearPlane / 40.0f, want);
        std::printf("  the effects far after near: %.4f %.4f %.4f (want %.4f %.4f %.4f)\n", got[0], got[1], got[2], want[0], want[1], want[2]);
        CHECK(near3(got, want));
        // the effects on another list than the resolve's, as the game records them: fogged by their depth all the same
        apart = true;
        fxFrame(true, wall, nearPlane / 40.0f, 32, 32, got);
        std::printf("  the effects far, drawn on another list than the resolve's: %.4f %.4f %.4f (want %.4f %.4f %.4f)\n", got[0], got[1], got[2],
                    want[0], want[1], want[2]);
        CHECK(near3(got, want));
        apart = false;
    }
    if (g_fail) { std::printf("fog_before_fx_test: %d FAILED\n", g_fail); return 1; }
    std::printf("fog_before_fx_test: the scene fogged before the resolve by the game's formula and by our effect's curve, the void, off and states not known on the resolve's "
                "list left alone, the game's heap, table and compute state given back, the effects by their own depth%s: all passed\n",
                debugLayer ? " (the debug layer, GPU-based validation: no error)" : "");
    return 0;
}
