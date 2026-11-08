// foveation_native_test - [render] foveated end to end on a real D3D12 device, no game, no headset: vr/foveation.cpp through the plugin's own
// hooks (vr/native_d3d12.cpp) as the game's draws reach them. A "game" list draws a full-screen triangle into a full-size colour + depth pair
// with an opaque, depth-tested pipeline whose pixel shader writes each pixel's own position; the picture read back shows where the GPU shaded
// one pixel in 2x2 (the four pixels of a quad carry one value) and where every pixel (four values):
//   - the first eye pass: no rate image yet (the scene's target is learnt) - all whole;
//   - then the left eye: whole round its lens centre, 2x2 at the corners and on its far (outer) side, whole on the near side where the right
//     eye is coarse - the right eye mirrored (the eyes of an asymmetric headset, -57 / +45 degrees);
//   - a pipeline without depth testing (the interface, effects): whole; [render] foveated switched off live: whole again;
//   - with the D3D12 debug layer (Windows' Graphics Tools) no error from it.
// The GPU: WARP when it has variable rate shading tier 2, else the PC's own; neither: SKIPPED (exit 0, said so). Exit 0 when every check holds.
//   foveation_native_test            (debug layer when installed)
//   foveation_native_test --plain    (never)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include "MinHook.h"
#include "foveation.h"
#include "native_d3d12.h"
#include "xr.h"
#include "fullscreen_passes.h"

static int g_failed = 0, g_checks = 0;
#define CHECK(cond)                                                                                  \
    do {                                                                                             \
        ++g_checks;                                                                                  \
        if (!(cond)) { ++g_failed; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); }          \
    } while (0)

static void Log(const char* s) { printf("  log: %s\n", s); }

// The pair's eyes as vr/xr.cpp would give them: each wider on its outer side, drawn as the runtime's own view. The eye tracker: off until
// g_tracker; then the gaze at g_gazeU / g_gazeV of the picture (both eyes), g_gazeOk false: no tracked sample this frame.
static bool g_tracker = false, g_gazeOk = false;
static float g_gazeU = .5f, g_gazeV = .5f;
namespace xr {
bool PairLens(int eye, EyeLens* out) {
    const float k = 3.14159265f / 180.0f;
    const float outer = tanf(57 * k), inner = tanf(45 * k), v = tanf(50 * k);
    *out = EyeLens{};
    out->drawn[0] = eye == 0 ? -outer : -inner;
    out->drawn[1] = eye == 0 ? inner : outer;
    out->drawn[2] = v;
    out->drawn[3] = -v;
    for (int i = 0; i < 4; ++i) out->seen[i] = out->drawn[i];
    out->seenOk = true;
    out->ok = true;
    out->gazeAvail = g_tracker;
    out->gazeOk = g_tracker && g_gazeOk;
    out->gaze[0] = out->drawn[0] + g_gazeU * (out->drawn[1] - out->drawn[0]);
    out->gaze[1] = out->drawn[2] - g_gazeV * (out->drawn[2] - out->drawn[3]);
    return true;
}
}  // namespace xr

constexpr UINT W = 256, H = 256;

static bool Tier2(IDXGIAdapter* ad) {
    ID3D12Device* d = nullptr;
    if (FAILED(D3D12CreateDevice(ad, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&d)) || !d) return false;
    D3D12_FEATURE_DATA_D3D12_OPTIONS6 o{};
    const bool ok = SUCCEEDED(d->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6, &o, sizeof o)) && o.VariableShadingRateTier >= D3D12_VARIABLE_SHADING_RATE_TIER_2;
    d->Release();
    return ok;
}

static ID3DBlob* Compile(const char* src, const char* entry, const char* target) {
    ID3DBlob* code = nullptr;
    ID3DBlob* err = nullptr;
    if (FAILED(D3DCompile(src, strlen(src), nullptr, nullptr, nullptr, entry, target, 0, 0, &code, &err))) {
        printf("shader %s: %s\n", entry, err ? (const char*)err->GetBufferPointer() : "?");
        return nullptr;
    }
    return code;
}

static const char* kShaders =
    "float4 VS(uint id : SV_VertexID) : SV_Position {\n"
    "    float2 uv = float2((id << 1) & 2, id & 2);\n"
    "    return float4(uv * float2(2, -2) + float2(-1, 1), 0.5, 1);\n"
    "}\n"
    "uint PS(float4 p : SV_Position) : SV_Target { return (uint)p.x + (uint)p.y * 4096u; }\n"
    // a pixel shader of its own for the stand-in "grass lighting" (another hash: taken as fullscreenpasses::kGrassLighting by TestKind)
    "uint PSG(float4 p : SV_Position) : SV_Target { return (uint)p.x + (uint)p.y * 4096u + 1u; }\n";

struct Ctx {
    ID3D12Device* dev;
    ID3D12CommandQueue* q;
    ID3D12CommandAllocator* a;
    ID3D12GraphicsCommandList* l;
    ID3D12Resource *colour, *depth, *readback;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv, dsv;
    ID3D12RootSignature* rs;
    ID3D12Fence* f;
    UINT64 fv;
    HANDLE e;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;
};

static std::vector<uint32_t> Run(Ctx& c);
// One eye's pass of the "game": foveation told the eye first, as vrcam's SetEye does.
static std::vector<uint32_t> Pass(Ctx& c, int eye, ID3D12PipelineState* pso) {
    foveation::BeginEye(eye, true);
    c.a->Reset();
    c.l->Reset(c.a, nullptr);
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {c.colour, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET};
    c.l->ResourceBarrier(1, &b);
    const float zero[4] = {0, 0, 0, 0};
    c.l->ClearRenderTargetView(c.rtv, zero, 0, nullptr);
    c.l->ClearDepthStencilView(c.dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    c.l->OMSetRenderTargets(1, &c.rtv, FALSE, &c.dsv);
    const D3D12_VIEWPORT vp{0, 0, (float)W, (float)H, 0, 1};
    const D3D12_RECT sr{0, 0, (LONG)W, (LONG)H};
    c.l->RSSetViewports(1, &vp);
    c.l->RSSetScissorRects(1, &sr);
    c.l->SetGraphicsRootSignature(c.rs);
    c.l->SetPipelineState(pso);
    c.l->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c.l->DrawInstanced(3, 1, 0, 0);
    std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    c.l->ResourceBarrier(1, &b);
    D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
    dst.pResource = c.readback;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = c.fp;
    src.pResource = c.colour;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    c.l->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    c.l->Close();
    return Run(c);
}
// The list as recorded runs once more as eye `eye` - the replay: [stereo] native_replay submits the left pass's lists again for the right eye
// after vrcam's BeginEye(1) copied the right eye's mask into the image they name.
static std::vector<uint32_t> Replay(Ctx& c, int eye) {
    foveation::BeginEye(eye, true);
    return Run(c);
}
static std::vector<uint32_t> Run(Ctx& c) {
    ID3D12CommandList* lists[] = {c.l};
    c.q->ExecuteCommandLists(1, lists);
    c.q->Signal(c.f, ++c.fv);
    c.f->SetEventOnCompletion(c.fv, c.e);
    WaitForSingleObject(c.e, 10000);
    foveation::EndWorld();
    std::vector<uint32_t> img(W * H);
    uint8_t* p = nullptr;
    const D3D12_RANGE all{0, (SIZE_T)(c.fp.Footprint.RowPitch * H)};
    if (SUCCEEDED(c.readback->Map(0, &all, (void**)&p)) && p) {
        for (UINT y = 0; y < H; ++y) memcpy(&img[y * W], p + y * c.fp.Footprint.RowPitch, W * 4);
        const D3D12_RANGE none{0, 0};
        c.readback->Unmap(0, &none);
    }
    return img;
}

// The 2x2 quad holding pixel (x, y): one value (shaded once) or four (each pixel shaded)
static int Values(const std::vector<uint32_t>& img, UINT x, UINT y) {
    x &= ~1u; y &= ~1u;
    const uint32_t v[4] = {img[y * W + x], img[y * W + x + 1], img[(y + 1) * W + x], img[(y + 1) * W + x + 1]};
    int n = 1;
    for (int i = 1; i < 4; ++i) {
        bool seen = false;
        for (int j = 0; j < i; ++j) seen = seen || v[j] == v[i];
        if (!seen) ++n;
    }
    return n;
}
static int CoarseQuads(const std::vector<uint32_t>& img) {
    int n = 0;
    for (UINT y = 0; y < H; y += 2)
        for (UINT x = 0; x < W; x += 2) n += Values(img, x, y) == 1;
    return n;
}

int main(int argc, char** argv) {
    const bool plain = argc > 1 && strcmp(argv[1], "--plain") == 0;
    setvbuf(stdout, nullptr, _IONBF, 0);
    IDXGIFactory4* fac = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), (void**)&fac))) { printf("no DXGI factory\n"); return 2; }
    IDXGIAdapter* ad = nullptr;
    const char* which = "WARP";
    if (FAILED(fac->EnumWarpAdapter(__uuidof(IDXGIAdapter), (void**)&ad)) || !Tier2(ad)) {
        if (ad) ad->Release();
        ad = nullptr;
        IDXGIAdapter1* hw = nullptr;
        if (SUCCEEDED(fac->EnumAdapters1(0, &hw)) && hw) {
            if (Tier2(hw)) { ad = hw; which = "the PC's GPU"; }
            else hw->Release();
        }
    }
    if (!ad) { printf("SKIPPED: neither WARP nor this PC's GPU has variable rate shading tier 2\n"); return 0; }
    printf("  the GPU: %s (variable rate shading tier 2)\n", which);
    if (MH_Initialize() != MH_OK) { printf("MinHook did not start\n"); return 2; }
    bool debugLayer = false;
    {
        ID3D12Debug* dbg = nullptr;
        if (!plain && SUCCEEDED(D3D12GetDebugInterface(__uuidof(ID3D12Debug), (void**)&dbg)) && dbg) { dbg->EnableDebugLayer(); dbg->Release(); debugLayer = true; }
        printf("  the D3D12 debug layer: %s\n", debugLayer ? "on" : "off");
    }

    foveation::Config cfg;
    cfg.on = true;
    cfg.strength = 1;   // Medium: radius 0.55, 2x2
    foveation::Configure(cfg);
    natived3d::Sinks k;
    foveation::FillSinks(k, &Log);
    CHECK(natived3d::Install(k, &Log));
    Ctx c{};
    if (FAILED(D3D12CreateDevice(ad, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&c.dev))) { printf("no device\n"); return 2; }
    ID3D12InfoQueue* info = nullptr;
    if (debugLayer) c.dev->QueryInterface(__uuidof(ID3D12InfoQueue), (void**)&info);
    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    CHECK(SUCCEEDED(c.dev->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&c.q)));
    // a swap chain on the queue: the plugin's hooks learn the game's queue from it (where our copies go)
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"foveation_native_test";
    RegisterClassW(&wc);
    HWND wnd = CreateWindowExW(0, wc.lpszClassName, L"foveation_native_test", WS_OVERLAPPEDWINDOW, 0, 0, W, H, nullptr, nullptr, wc.hInstance, nullptr);
    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = W; sd.Height = H;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    IDXGISwapChain1* sc = nullptr;
    CHECK(SUCCEEDED(fac->CreateSwapChainForHwnd(c.q, wnd, &sd, nullptr, nullptr, &sc)));
    CHECK(natived3d::GameQueue() == c.q);
    foveation::NoteScreen(c.dev, W, H);

    // the scene's colour (R32_UINT: each pixel's position) and depth, full size
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = W; d.Height = H;
    d.DepthOrArraySize = d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Format = DXGI_FORMAT_R32_UINT;
    d.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    CHECK(SUCCEEDED(c.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr, __uuidof(ID3D12Resource), (void**)&c.colour)));
    d.Format = DXGI_FORMAT_D32_FLOAT;
    d.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE cv{};
    cv.Format = DXGI_FORMAT_D32_FLOAT;
    cv.DepthStencil.Depth = 1.0f;
    CHECK(SUCCEEDED(c.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_DEPTH_WRITE, &cv, __uuidof(ID3D12Resource), (void**)&c.depth)));
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.NumDescriptors = 1;
    ID3D12DescriptorHeap *rtvHeap = nullptr, *dsvHeap = nullptr;
    c.dev->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void**)&rtvHeap);
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    c.dev->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void**)&dsvHeap);
    c.rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
    c.dsv = dsvHeap->GetCPUDescriptorHandleForHeapStart();
    c.dev->CreateRenderTargetView(c.colour, nullptr, c.rtv);
    D3D12_DEPTH_STENCIL_VIEW_DESC dv{};
    dv.Format = DXGI_FORMAT_D32_FLOAT;
    dv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    c.dev->CreateDepthStencilView(c.depth, &dv, c.dsv);
    D3D12_RESOURCE_DESC cd = c.colour->GetDesc();
    UINT64 bytes = 0;
    c.dev->GetCopyableFootprints(&cd, 0, 1, 0, &c.fp, nullptr, nullptr, &bytes);
    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = bytes;
    bd.Height = bd.DepthOrArraySize = bd.MipLevels = 1;
    bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hp.Type = D3D12_HEAP_TYPE_READBACK;
    CHECK(SUCCEEDED(c.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource), (void**)&c.readback)));

    // the pipelines: the game's visible-colour pass (depth tested, no depth write, opaque) and one without depth testing
    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    ID3DBlob *rsb = nullptr, *rse = nullptr;
    CHECK(SUCCEEDED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &rsb, &rse)));
    CHECK(SUCCEEDED(c.dev->CreateRootSignature(0, rsb->GetBufferPointer(), rsb->GetBufferSize(), __uuidof(ID3D12RootSignature), (void**)&c.rs)));
    ID3DBlob* vs = Compile(kShaders, "VS", "vs_5_0");
    ID3DBlob* ps = Compile(kShaders, "PS", "ps_5_0");
    ID3DBlob* psg = Compile(kShaders, "PSG", "ps_5_0");
    if (psg) foveation::TestKind(fullscreenpasses::Fnv(psg->GetBufferPointer(), psg->GetBufferSize()), fullscreenpasses::kGrassLighting);
    if (!vs || !ps) return 2;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC g{};
    g.pRootSignature = c.rs;
    g.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    g.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    g.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    g.SampleMask = ~0u;
    g.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    g.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    g.RasterizerState.DepthClipEnable = TRUE;
    g.DepthStencilState.DepthEnable = TRUE;
    g.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    g.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    g.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    g.NumRenderTargets = 1;
    g.RTVFormats[0] = DXGI_FORMAT_R32_UINT;
    g.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    g.SampleDesc.Count = 1;
    ID3D12PipelineState *opaque = nullptr, *noDepth = nullptr, *grassPass = nullptr;
    CHECK(SUCCEEDED(c.dev->CreateGraphicsPipelineState(&g, __uuidof(ID3D12PipelineState), (void**)&opaque)));
    g.DepthStencilState.DepthEnable = FALSE;
    CHECK(SUCCEEDED(c.dev->CreateGraphicsPipelineState(&g, __uuidof(ID3D12PipelineState), (void**)&noDepth)));
    if (psg) {   // the stand-in grass lighting: full screen, no depth test (the game's is stencil-tested, z-s)
        g.PS = {psg->GetBufferPointer(), psg->GetBufferSize()};
        CHECK(SUCCEEDED(c.dev->CreateGraphicsPipelineState(&g, __uuidof(ID3D12PipelineState), (void**)&grassPass)));
    }
    if (!grassPass) return 2;

    c.dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&c.a);
    c.dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, c.a, nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&c.l);
    c.l->Close();
    c.dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&c.f);
    c.e = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    const UINT cy = H / 2;
    const UINT nearL = (UINT)(W * 0.85f), farL = (UINT)(W * 0.15f);   // the left eye's centre lies right of the middle (the nose)
    // 1. the first pass: the scene's target learnt, no image yet
    {
        const auto img = Pass(c, 0, opaque);
        printf("  first pass: %d quads of %u shaded once\n", CoarseQuads(img), (W / 2) * (H / 2));
        CHECK(CoarseQuads(img) == 0);
        CHECK(img[cy * W + W / 2] == W / 2 + cy * 4096u);   // drawn at all
    }
    // 2. the left eye, 3. the right eye
    {
        const auto left = Pass(c, 0, opaque);
        const auto right = Pass(c, 1, opaque);
        printf("  left eye: %d quads shaded once, right eye: %d\n", CoarseQuads(left), CoarseQuads(right));
        CHECK(Values(left, W / 2, cy) == 4 && Values(right, W / 2, cy) == 4);       // the middle: every pixel
        CHECK(Values(left, 4, 4) == 1 && Values(right, 4, 4) == 1);                 // a corner: once a quad
        CHECK(Values(left, W - 6, H - 6) == 1 && Values(right, W - 6, H - 6) == 1);
        CHECK(Values(left, farL, cy) == 1 && Values(left, nearL, cy) == 4);         // the left eye: its outer side coarse, the nose side whole
        CHECK(Values(right, farL, cy) == 4 && Values(right, nearL, cy) == 1);       // the right eye mirrored
        const int n = CoarseQuads(left);
        CHECK(n > (int)((W / 2) * (H / 2)) / 3 && n < (int)((W / 2) * (H / 2)) * 9 / 10);
    }
    // 4. a pipeline without depth testing: whole
    {
        const auto img = Pass(c, 0, noDepth);
        CHECK(CoarseQuads(img) == 0);
    }
    // 5. switched off live: whole; on again: coarse again
    {
        cfg.on = false;
        foveation::Configure(cfg);
        const auto off = Pass(c, 0, opaque);
        CHECK(CoarseQuads(off) == 0);
        cfg.on = true;
        foveation::Configure(cfg);
        const auto on = Pass(c, 0, opaque);
        CHECK(CoarseQuads(on) > 0);
    }
    // 6. an eye tracker: the sharp area follows the gaze - whole on the first pass and after a jump (Bear's rule), the lens centre while no
    //    tracked sample comes
    {
        g_tracker = true;
        g_gazeOk = true;
        g_gazeU = .2f; g_gazeV = .5f;
        const auto first = Pass(c, 0, opaque);    // the bank is made; the gaze "came back": whole
        CHECK(CoarseQuads(first) == 0);
        const auto at = Pass(c, 0, opaque);
        CHECK(Values(at, (UINT)(W * .2f), cy) == 4 && Values(at, (UINT)(W * .85f), cy) == 1);   // sharp where the eye looks, the lens side coarse
        CHECK(Values(at, 4, 4) == 1);
        const auto right = Pass(c, 1, opaque);   // the right eye's own first gaze pass: whole, then on its gaze too
        CHECK(CoarseQuads(right) == 0);
        const auto right2 = Pass(c, 1, opaque);
        CHECK(Values(right2, (UINT)(W * .2f), cy) == 4 && Values(right2, (UINT)(W * .85f), cy) == 1);
        g_gazeU = .8f; g_gazeV = .3f;              // a saccade: whole once, then there
        CHECK(CoarseQuads(Pass(c, 0, opaque)) == 0);
        const auto moved = Pass(c, 0, opaque);
        CHECK(Values(moved, (UINT)(W * .8f), (UINT)(H * .3f)) == 4 && Values(moved, (UINT)(W * .2f), cy) == 1);
        g_gazeU = .82f; g_gazeV = .32f;            // a small move: followed at once
        CHECK(Values(Pass(c, 0, opaque), (UINT)(W * .82f), (UINT)(H * .32f)) == 4);
        g_gazeOk = false;                          // tracking lost: the lens centre (the left eye's, right of the middle)
        const auto lost = Pass(c, 0, opaque);
        CHECK(Values(lost, (UINT)(W * .6f), cy) == 4 && Values(lost, 4, 4) == 1 && Values(lost, (UINT)(W * .15f), cy) == 1);
        cfg.gaze = false;                          // foveated_gaze off live: the lens centre even with a gaze
        foveation::Configure(cfg);
        g_gazeOk = true;
        g_gazeU = .2f;
        Pass(c, 0, opaque);                        // (the image made again without the bank)
        const auto noGaze = Pass(c, 0, opaque);
        CHECK(Values(noGaze, (UINT)(W * .15f), cy) == 1 && Values(noGaze, (UINT)(W * .6f), cy) == 4);
        cfg.gaze = true;
        foveation::Configure(cfg);
        g_tracker = false;
    }
    // 7. the replay: the left eye's list run again after BeginEye(1) takes the right eye's mask (the image it names, its contents swapped)
    {
        const auto left = Pass(c, 0, opaque);
        const auto right = Replay(c, 1);
        CHECK(Values(left, (UINT)(W * .15f), cy) == 1 && Values(left, (UINT)(W * .85f), cy) == 4);
        CHECK(Values(right, (UINT)(W * .15f), cy) == 4 && Values(right, (UINT)(W * .85f), cy) == 1);   // the right eye's, from the left's list
        CHECK(Values(right, W / 2, cy) == 4 && Values(right, 4, 4) == 1);
    }
    // 8. the heavy full-screen passes ([render] foveated_fullscreen) and the grass lighting at a coarser rate ([render] grass_lighting_coarse):
    //    a stand-in grass lighting pass - no depth test, so Bear's rule leaves it whole
    {
        const auto whole = Pass(c, 0, grassPass);
        CHECK(CoarseQuads(whole) == 0);                                  // foveated_fullscreen 0: as before
        cfg.fullscreen = 1;
        foveation::Configure(cfg);
        const auto wide = Pass(c, 0, grassPass);
        CHECK(Values(wide, W / 2 + 20, cy) == 4 && Values(wide, 4, 4) == 1);   // the sharp centre sharp, the edge coarse
        CHECK(Values(wide, (UINT)(W * .15f), cy) == 1 && Values(wide, (UINT)(W * .85f), cy) == 4);   // the left eye's own centre
        CHECK(CoarseQuads(wide) == CoarseQuads(Pass(c, 0, opaque)));     // the same mask as the opaque world's
        const auto wideRight = Replay(c, 1);                             // ... and the replay's right eye takes the right eye's
        CHECK(Values(wideRight, (UINT)(W * .15f), cy) == 4 && Values(wideRight, (UINT)(W * .85f), cy) == 1);
        cfg.fullscreen = 0;
        cfg.grassCoarse = 2;                                             // 2x2 everywhere, the centre too
        foveation::Configure(cfg);
        const auto g2 = Pass(c, 0, grassPass);
        CHECK(CoarseQuads(g2) == (int)((W / 2) * (H / 2)));
        CHECK(CoarseQuads(Pass(c, 0, opaque)) < (int)((W / 2) * (H / 2)) && Values(Pass(c, 0, opaque), W / 2, cy) == 4);   // other passes untouched
        cfg.grassCoarse = 1;                                             // 2x1: two pixels side by side shaded once, rows apart
        foveation::Configure(cfg);
        const auto g1 = Pass(c, 0, grassPass);
        const UINT x = W / 2, y = cy;
        CHECK(g1[y * W + x] == g1[y * W + x + 1] && g1[y * W + x] != g1[(y + 1) * W + x]);
        cfg.fullscreen = 1;                                              // both: the coarser per axis (2x1 in the middle, 2x2 at the edges)
        foveation::Configure(cfg);
        const auto both = Pass(c, 0, grassPass);
        CHECK(both[y * W + x] == both[y * W + x + 1] && both[y * W + x] != both[(y + 1) * W + x] && Values(both, 4, 4) == 1);
        cfg.on = false;                                                  // grass_lighting_coarse alone, foveated off
        cfg.grassCoarse = 2;
        foveation::Configure(cfg);
        CHECK(CoarseQuads(Pass(c, 0, grassPass)) == (int)((W / 2) * (H / 2)));
        CHECK(CoarseQuads(Pass(c, 0, opaque)) == 0);                     // nothing else coarse without foveated
        cfg.grassCoarse = 0;
        foveation::Configure(cfg);
        CHECK(CoarseQuads(Pass(c, 0, grassPass)) == 0);
        cfg.on = true;
        cfg.fullscreen = 0;
        foveation::Configure(cfg);
    }
    // 10. a card without variable rate shading tier 2 (forced: foveation::NoteNoVrs - what the plugin's load or the device's own answer says
    //     there): foveated and the grass rate on, every pass whole - nothing followed, nothing bound, nothing copied
    {
        foveation::NoteNoVrs();
        cfg.on = true;
        cfg.grassCoarse = 2;
        cfg.fullscreen = 2;
        foveation::Configure(cfg);
        const auto opaqueImg = Pass(c, 0, opaque);
        const auto grassImg = Pass(c, 1, grassPass);
        printf("  no VRS tier 2 (forced): %d and %d quads shaded once (0 expected)\n", CoarseQuads(opaqueImg), CoarseQuads(grassImg));
        CHECK(CoarseQuads(opaqueImg) == 0 && CoarseQuads(grassImg) == 0);
        CHECK(foveation::VrsTier2() == 0 && !foveation::Usable());
    }
    // 9. not a pair (a flat picture, a menu): whole
    {
        foveation::BeginEye(0, false);
        foveation::EndWorld();
        // (Pass tells BeginEye(eye, true) itself; a list drawn with no eye begun:)
        c.a->Reset();
        c.l->Reset(c.a, nullptr);
        c.l->Close();
    }
    if (info) {
        const UINT64 n = info->GetNumStoredMessages();
        int errors = 0;
        for (UINT64 i = 0; i < n; ++i) {
            SIZE_T len = 0;
            info->GetMessage(i, nullptr, &len);
            std::vector<char> buf(len);
            auto* m = (D3D12_MESSAGE*)buf.data();
            if (SUCCEEDED(info->GetMessage(i, m, &len)) && (m->Severity == D3D12_MESSAGE_SEVERITY_ERROR || m->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION)) {
                ++errors;
                printf("  debug layer: %s\n", m->pDescription);
            }
        }
        printf("  debug layer: %llu messages, %d errors\n", (unsigned long long)n, errors);
        CHECK(errors == 0);
        info->Release();
    }
    printf("%s: %d checks, %d failed\n", g_failed ? "FAILED" : "PASSED", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
