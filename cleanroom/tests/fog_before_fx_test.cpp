// fog_before_fx_test - vr/fog_before_fx.cpp on a D3D12 device of its own (WARP), a list recorded as the game records its frame's end:
//   the scene (R11G11B10) drawn with the depth-stencil bound, made a shader resource; the game's own heap, graphics root signature and
//   table, a compute root signature of the game's with a root constant and a root UAV; the "resolve" (a pipeline marked as the game's,
//   reading the scene through the game's table) bound and drawn - the sinks told as the hooks tell them. Then:
//   1. on: what the resolve wrote is the scene fogged by the game's formula at the depth's distance (the step ran before its draw, and the
//      game's heap and table were bound again: the resolve read the scene through them);
//   2. the game's compute state given back: its dispatch after the resolve, with no root argument set again, writes its own constant;
//   3. the void (depth 0): left as it is; 4. off: the scene as it is.
#include "fog_before_fx.h"
#include "native_d3d12.h"

#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>
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
unsigned SrvIncrement() { return g_srvInc; }
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
    D3D12_DEPTH_STENCIL_VIEW_DESC dvd{DXGI_FORMAT_D24_UNORM_S8_UINT, D3D12_DSV_DIMENSION_TEXTURE2D};
    dev->CreateDepthStencilView(depth, &dvd, dsv);
    const D3D12_CPU_DESCRIPTOR_HANDLE rtvHalf{0xF4A3F000ull};   // (a handle for the sinks only)
    g_rtvs[rtvHalf.ptr] = halfTarget;
    // a full-size R11G11B10 target of the scene's size bound with the depth after it, which the resolve does not read (told to the sinks only)
    ID3D12Resource* decoy = Tex(dev, DXGI_FORMAT_R11G11B10_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr);
    const D3D12_CPU_DESCRIPTOR_HANDLE rtvDecoy{0xDEC0F000ull};
    g_rtvs[rtvDecoy.ptr] = decoy;
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
    bool halfLayer = false;        // the game's half-size effects target (R11G11B10 too) bound with the same depth after the scene
    bool decoyAfter = false;       // a full-size R11G11B10 target the resolve does not read, bound with the same depth after the scene
    bool depthUntold = false;      // no barrier of the depth this frame (its state from an earlier frame's: stale)
    bool depthElsewhere = false;   // the depth's barrier to its read state told on another list this frame
    int sceneElse = 0;             // the scene's barrier to a shader resource told on another list: 1 this frame; 2 then a later one to a
                                   // render target told there too (the next frame's, recorded early); 3 last frame (stale)
    bool curveOn = false;   // [fog] game_fog_our_curve: our effect's curve (20 - 80, curve 1.5, strength 0.9) in place of the exponential
    const float cStart = 20.0f, cEnd = 80.0f, cCurve = 1.5f, cStrength = 0.9f;
    // one frame's end as the game records it; the resolve's output at pixel (px, py) and the game's dispatch's word back
    auto frame = [&](bool on, float depthValue, float px, float py, float rgb[3], uint32_t* gameWord) {
        fogfx::Params p;
        p.on = on;
        p.plane = plane;
        p.density = density;
        std::memcpy(p.colour, fog, sizeof fog);
        p.nearPlane = nearPlane;
        p.ourCurve = curveOn;
        p.start = cStart; p.end = cEnd; p.curve = cCurve; p.strength = cStrength;
        fogfx::Configure(p);
        fogfx::Frame();   // (a present between frames)
        alloc->Reset();
        cl->Reset(alloc, nullptr);
        k.fxReset(cl);
        // the scene drawn (cleared) with the depth bound
        k.fxTargets(sceneElsewhere ? (ID3D12GraphicsCommandList*)(uintptr_t)0x5CE7E : cl, 1, &rtvScene, false, depth);
        if (halfLayer) k.fxTargets(sceneElsewhere ? (ID3D12GraphicsCommandList*)(uintptr_t)0x5CE7E : cl, 1, &rtvHalf, false, depth);
        if (decoyAfter) k.fxTargets(sceneElsewhere ? (ID3D12GraphicsCommandList*)(uintptr_t)0x5CE7E : cl, 1, &rtvDecoy, false, depth);
        cl->OMSetRenderTargets(1, &rtvScene, FALSE, &dsv);
        const float c[4] = {grey, grey, grey, 1};
        cl->ClearRenderTargetView(rtvScene, c, 0, nullptr);
        cl->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, depthValue, 0, 0, nullptr);
        auto* other = (ID3D12GraphicsCommandList*)(uintptr_t)0x5CE7F;
        D3D12_RESOURCE_BARRIER b = Tr(scene, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        if (sceneElse != 3) k.fxBarrier(sceneElse ? other : cl, 1, &b);   // (3: told last frame, below)
        cl->ResourceBarrier(1, &b);
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
        k.fxTargets(cl, 1, frameIsR11 ? &rtvFrame : &rtvOut, false, depth);
        cl->OMSetRenderTargets(1, &rtvOut, FALSE, &dsv);
        const D3D12_VIEWPORT vp{0, 0, 64, 64, 0, 1};
        const D3D12_RECT sr{0, 0, 64, 64};
        cl->RSSetViewports(1, &vp);
        cl->RSSetScissorRects(1, &sr);
        cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        k.fxDraw(cl, 3, 1);
        cl->DrawInstanced(3, 1, 0, 0);
        // the game's dispatch after it: its pipeline bound, no root argument set again
        cl->SetPipelineState(gameCs);
        cl->Dispatch(1, 1, 1);
        D3D12_RESOURCE_BARRIER bb[3] = {Tr(out, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE),
                                        Tr(gameOut, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
                                        Tr(scene, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)};
        cl->ResourceBarrier(3, bb);
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
        g_q->ExecuteCommandLists(1, lists);
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
        const float dist = std::sqrt(x * x + y * y + z * z + 1.0f);
        const float t = curveOn ? 1.0f - std::pow(std::fmin(1.0f, std::fmax(0.0f, (dist - cStart) / (cEnd - cStart))), cCurve) * cStrength
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
    // 3: the void
    frame(true, 0.0f, 32, 32, got, &word);
    std::printf("  on, the void: %.4f %.4f %.4f (the scene's %.4f)\n", got[0], got[1], got[2], grey);
    CHECK(std::fabs(got[0] - grey) < 0.01f && std::fabs(got[2] - grey) < 0.01f && word == 0x5EED1234u);
    // 4: off
    frame(false, nearPlane / 45.0f, 32, 32, got, &word);
    std::printf("  off: %.4f %.4f %.4f (the scene's %.4f)\n", got[0], got[1], got[2], grey);
    CHECK(std::fabs(got[0] - grey) < 0.01f && word == 0x5EED1234u);
    if (g_fail) { std::printf("fog_before_fx_test: %d FAILED\n", g_fail); return 1; }
    std::printf("fog_before_fx_test: the scene fogged before the resolve by the game's formula, the void, off and states not known on the resolve's "
                "list left alone, the game's heap, table and compute state given back%s: all passed\n",
                debugLayer ? " (the debug layer, GPU-based validation: no error)" : "");
    return 0;
}
