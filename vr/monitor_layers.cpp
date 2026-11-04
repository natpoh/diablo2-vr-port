// [render] monitor_panels: the headset's panels onto the monitor's picture too - see vr/monitor_layers.h.

#include "monitor_layers.h"

#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace monitorlayers {
namespace {

void (*g_log)(const char*) = nullptr;
void Line(const char* fmt, ...) {
    if (!g_log) return;
    char b[1024];
    va_list va;
    va_start(va, fmt);
    vsnprintf(b, sizeof b, fmt, va);
    va_end(va);
    g_log(b);
}

std::atomic<bool> g_on{false};

// -- the frame noted (draw thread; Forget may come from the session's end under xr's lock) ---------------------------------------------
SRWLOCK g_lock = SRWLOCK_INIT;
Frame g_frame;
bool g_have = false;
ULONGLONG g_at = 0;
constexpr ULONGLONG kMaxAgeMs = 100;   // a frame older than this is not drawn (the headset's frames stopped: a menu, a load, an end)

// -- the draw: a quad's four corners from its constants, its image sampled ----------------------------------------------------------
const char kShader[] = R"(
cbuffer Quad : register(b0) {
  float4 g_c[4];     // the corners in clip space: top left, top right, bottom left, bottom right
  float4 g_uv;       // the image rect: u0 v0 u1 v1
  float4 g_scale;    // the colour scale (premultiplied: rgb and alpha)
  float4 g_mode;     // x: 1 the image is read decoded from sRGB, y: 1 the picture is linear (scRGB), z: 1 blended (0 opaque), w: 1 unpremultiplied
};
Texture2D g_src : register(t0);
SamplerState g_smp : register(s0);
void VSMain(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD0) {
  pos = g_c[id];
  const float2 k = float2(id & 1, id >> 1);
  uv = lerp(g_uv.xy, g_uv.zw, k);
}
float3 ToSrgb(float3 c) { c = saturate(c); return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055; }
float3 ToLinear(float3 c) { return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }
float4 PSMain(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
  float4 c = g_src.SampleLevel(g_smp, uv, 0.0);
  if (g_mode.w > 0.5) c.rgb *= c.a;   // to premultiplied, as the runtime takes it
  c *= g_scale;
  if (g_mode.z < 0.5) c.a = 1.0;
  float3 rgb = c.a > 1e-5 ? c.rgb / c.a : float3(0.0, 0.0, 0.0);
  if (g_mode.x > 0.5 && g_mode.y < 0.5) rgb = ToSrgb(rgb);          // the picture holds gamma-encoded values (as the game drew them)
  else if (g_mode.x < 0.5 && g_mode.y > 0.5) rgb = ToLinear(rgb);
  return float4(rgb * c.a, c.a);   // premultiplied: ONE, INV_SRC_ALPHA
}
)";
constexpr UINT kConsts = 28;   // 4 corners + uv + scale + mode, float4 each
ID3D12Device* g_dev = nullptr;
ID3D12RootSignature* g_rs = nullptr;
ID3DBlob* g_vs = nullptr;
ID3DBlob* g_ps = nullptr;
struct Pso { DXGI_FORMAT fmt; ID3D12PipelineState* pso; };
Pso g_pso[4] = {};
ID3D12DescriptorHeap* g_srvHeap = nullptr;   // shader-visible: kMaxQuads slots for each of monitor_eye's allocators
ID3D12DescriptorHeap* g_rtvHeap = nullptr;
int g_heapSlots = 0;
UINT g_srvInc = 0;
bool g_failed = false;
// GPU time: two timestamps an allocator slot, resolved into a read-back buffer read once that slot's list is done
ID3D12QueryHeap* g_queries = nullptr;
ID3D12Resource* g_readback = nullptr;
uint64_t* g_readbackPtr = nullptr;
bool g_timed[16] = {};
UINT64 g_freq = 0;

// -- the 10 s line ------------------------------------------------------------------------------------------------------------------
struct Counts {
    uint32_t presents = 0, quads = 0, gpuN = 0, notedNoTex = 0, oldFrame = 0;
    double cpuUs = 0.0, cpuMaxUs = 0.0, gpuUs = 0.0, gpuMaxUs = 0.0;
    char names[512] = {};   // the panels drawn, each once
    ULONGLONG since = 0;
} g_n;

template <class T> void ReleaseAny(T*& p) { if (p) { p->Release(); p = nullptr; } }

double UsNow() {
    static LARGE_INTEGER f{};
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart * 1e6 / (double)f.QuadPart;
}

void AddName(const char* nm) {
    if (!nm || !*nm) return;
    const size_t len = strlen(nm);
    for (const char* p = g_n.names; *p;) {   // once each
        const char* end = strstr(p, ", ");
        const size_t l = end ? (size_t)(end - p) : strlen(p);
        if (l == len && !strncmp(p, nm, len)) return;
        if (!end) break;
        p = end + 2;
    }
    const size_t at = strlen(g_n.names);
    if (at + len + 3 >= sizeof g_n.names) return;
    snprintf(g_n.names + at, sizeof g_n.names - at, "%s%s", at ? ", " : "", nm);
}

void Tell() {
    const ULONGLONG now = GetTickCount64();
    if (!g_n.since) g_n.since = now;
    if (now - g_n.since < 10000) return;
    if (g_n.presents)
        Line("vrcam: monitor panels, 10 s: drawn on the monitor at %u presents, %u quads (%.1f a present): %s; CPU %.3f ms a present (most %.3f), "
             "GPU %.3f ms a present (most %.3f, %u timed); quads with no image %u, frames too old %u",
             g_n.presents, g_n.quads, (double)g_n.quads / g_n.presents, g_n.names[0] ? g_n.names : "-", g_n.cpuUs / g_n.presents / 1000.0,
             g_n.cpuMaxUs / 1000.0, g_n.gpuN ? g_n.gpuUs / g_n.gpuN / 1000.0 : 0.0, g_n.gpuMaxUs / 1000.0, g_n.gpuN, g_n.notedNoTex, g_n.oldFrame);
    g_n = Counts{};
    g_n.since = now;
}

D3D12_RESOURCE_BARRIER Tr(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    return b;
}

// A typeless image viewed as its swapchain's format; a typed one as it is.
DXGI_FORMAT ViewFormat(DXGI_FORMAT own, int swapchain) {
    switch (own) {
        case DXGI_FORMAT_R8G8B8A8_TYPELESS: case DXGI_FORMAT_B8G8R8A8_TYPELESS: case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        case DXGI_FORMAT_R16G16B16A16_TYPELESS:
            return swapchain ? (DXGI_FORMAT)swapchain : own == DXGI_FORMAT_R8G8B8A8_TYPELESS ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
                                                       : own == DXGI_FORMAT_B8G8R8A8_TYPELESS ? DXGI_FORMAT_B8G8R8A8_UNORM_SRGB
                                                       : own == DXGI_FORMAT_R10G10B10A2_TYPELESS ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_R16G16B16A16_FLOAT;
        default: return own;
    }
}
bool Srgb(DXGI_FORMAT f) { return f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || f == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB; }
bool Linear(DXGI_FORMAT f) { return f == DXGI_FORMAT_R16G16B16A16_FLOAT || f == DXGI_FORMAT_R16G16B16A16_TYPELESS; }
// The back buffer's RTV format (a typeless one: its plain family member).
DXGI_FORMAT TargetFormat(DXGI_FORMAT f) {
    switch (f) {
        case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        default: return f;
    }
}

bool Make(ID3D12Device* dev, int slots) {
    if (dev != g_dev) { Drop(); g_dev = dev; g_dev->AddRef(); }
    if (g_rs && g_srvHeap && g_heapSlots >= slots) return true;
    if (g_failed) return false;
    g_failed = true;   // until it is all made
    if (!g_vs || !g_ps) {
        HMODULE dc = LoadLibraryExW(L"d3dcompiler_47.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);   // (kept loaded)
        const pD3DCompile compile = dc ? (pD3DCompile)GetProcAddress(dc, "D3DCompile") : nullptr;
        if (!compile) { Line("vrcam: monitor panels - no shader compiler (d3dcompiler_47.dll): the panels are not drawn on the monitor"); return false; }
        for (int i = 0; i < 2; ++i) {
            ID3DBlob* err = nullptr;
            ID3DBlob** out = i ? &g_ps : &g_vs;
            if (FAILED(compile(kShader, sizeof kShader - 1, "d2rvr_monitor_panels", nullptr, nullptr, i ? "PSMain" : "VSMain", i ? "ps_5_0" : "vs_5_0",
                               D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out, &err))) {
                Line("vrcam: monitor panels - the shader did not compile (%s): the panels are not drawn on the monitor", err ? (const char*)err->GetBufferPointer() : "?");
                if (err) err->Release();
                return false;
            }
            if (err) err->Release();
        }
    }
    if (!g_rs) {
        using SerializeFn = HRESULT(WINAPI*)(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);
        HMODULE d12 = GetModuleHandleW(L"d3d12.dll");
        const SerializeFn serialize = d12 ? (SerializeFn)GetProcAddress(d12, "D3D12SerializeRootSignature") : nullptr;
        if (!serialize) return false;
        D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
        D3D12_ROOT_PARAMETER rp[2] = {};
        rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rp[0].Constants = {0, 0, kConsts};
        rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rp[1].DescriptorTable = {1, &range};
        rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC smp{};
        smp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        smp.AddressU = smp.AddressV = smp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        smp.MaxLOD = D3D12_FLOAT32_MAX;
        smp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        const D3D12_ROOT_SIGNATURE_DESC rsd{2, rp, 1, &smp, D3D12_ROOT_SIGNATURE_FLAG_NONE};
        ID3DBlob* rsBlob = nullptr;
        ID3DBlob* rsErr = nullptr;
        const bool rsOk = SUCCEEDED(serialize(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr)) &&
                          SUCCEEDED(g_dev->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), __uuidof(ID3D12RootSignature), (void**)&g_rs));
        if (rsBlob) rsBlob->Release();
        if (rsErr) rsErr->Release();
        if (!rsOk) return false;
    }
    if (!g_srvHeap || g_heapSlots < slots) {
        ReleaseAny(g_srvHeap);
        D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, (UINT)slots * kMaxQuads, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
        if (FAILED(g_dev->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void**)&g_srvHeap))) return false;
        g_srvHeap->SetName(L"d2rvr monitor panels srv");
        g_heapSlots = slots;
    }
    if (!g_rtvHeap) {
        D3D12_DESCRIPTOR_HEAP_DESC rd{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
        if (FAILED(g_dev->CreateDescriptorHeap(&rd, __uuidof(ID3D12DescriptorHeap), (void**)&g_rtvHeap))) return false;
        g_rtvHeap->SetName(L"d2rvr monitor panels rtv");
    }
    g_srvInc = g_dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    if (!g_queries && slots <= 16) {   // (the time is a nicety: without it the panels are drawn all the same)
        D3D12_QUERY_HEAP_DESC qd{D3D12_QUERY_HEAP_TYPE_TIMESTAMP, (UINT)(2 * 16), 0};
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC bd{};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bd.Width = 2 * 16 * sizeof(uint64_t);
        bd.Height = bd.DepthOrArraySize = bd.MipLevels = 1;
        bd.SampleDesc.Count = 1;
        bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (SUCCEEDED(g_dev->CreateQueryHeap(&qd, __uuidof(ID3D12QueryHeap), (void**)&g_queries)) &&
            SUCCEEDED(g_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource), (void**)&g_readback)) &&
            SUCCEEDED(g_readback->Map(0, nullptr, (void**)&g_readbackPtr))) {
            g_readback->SetName(L"d2rvr monitor panels time");
        } else {
            ReleaseAny(g_queries);
            ReleaseAny(g_readback);
            g_readbackPtr = nullptr;
        }
    }
    g_failed = false;
    return true;
}

ID3D12PipelineState* PsoFor(DXGI_FORMAT fmt) {
    for (const Pso& p : g_pso)
        if (p.pso && p.fmt == fmt) return p.pso;
    Pso* slot = nullptr;
    for (Pso& p : g_pso)
        if (!p.pso) { slot = &p; break; }
    if (!slot) return nullptr;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
    d.pRootSignature = g_rs;
    d.VS = {g_vs->GetBufferPointer(), g_vs->GetBufferSize()};
    d.PS = {g_ps->GetBufferPointer(), g_ps->GetBufferSize()};
    D3D12_RENDER_TARGET_BLEND_DESC& b = d.BlendState.RenderTarget[0];
    b.BlendEnable = TRUE;
    b.SrcBlend = D3D12_BLEND_ONE;
    b.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    b.BlendOp = D3D12_BLEND_OP_ADD;
    b.SrcBlendAlpha = D3D12_BLEND_ZERO;   // (the picture's own alpha kept)
    b.DestBlendAlpha = D3D12_BLEND_ONE;
    b.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    b.LogicOp = D3D12_LOGIC_OP_NOOP;
    b.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    d.SampleMask = UINT_MAX;
    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;   // (a quad is seen from both sides)
    d.RasterizerState.DepthClipEnable = TRUE;             // (behind the eye: clipped at z 0)
    d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.NumRenderTargets = 1;
    d.RTVFormats[0] = fmt;
    d.SampleDesc.Count = 1;
    if (FAILED(g_dev->CreateGraphicsPipelineState(&d, __uuidof(ID3D12PipelineState), (void**)&slot->pso))) {
        slot->pso = nullptr;
        Line("vrcam: monitor panels - no pipeline for the back buffer's format %d: the panels are not drawn on the monitor", (int)fmt);
        return nullptr;
    }
    slot->fmt = fmt;
    return slot->pso;
}

}  // namespace

void SetLogger(void (*log)(const char*)) { g_log = log; }

void Configure(bool on) {
    if (g_on.exchange(on) == on) return;
    if (on)
        Line("vrcam: monitor panels - on ([render] monitor_panels 1): the panels the headset shows (the menu's panel with the inventory, the trader, "
             "the stash; the toolbar, the map, the laser...) drawn over the monitor's picture where that eye sees them - the window only");
    else
        Line("vrcam: monitor panels - off ([render] monitor_panels 0): the monitor shows the eye's picture without the panels, as before");
}

bool On() { return g_on.load(std::memory_order_relaxed); }

void Note(const Frame& f) {
    if (!On()) return;
    AcquireSRWLockExclusive(&g_lock);
    g_have = f.eyeOk[0] || f.eyeOk[1];
    if (g_have) {
        g_frame.queue = f.queue;
        for (int e = 0; e < 2; ++e) { g_frame.eyeOk[e] = f.eyeOk[e]; g_frame.eye[e] = f.eye[e]; g_frame.tan[e] = f.tan[e]; }
        g_frame.n = f.n < kMaxQuads ? f.n : kMaxQuads;
        for (uint32_t i = 0; i < g_frame.n; ++i) g_frame.quad[i] = f.quad[i];
        g_at = GetTickCount64();
    }
    ReleaseSRWLockExclusive(&g_lock);
}

void Forget() {
    AcquireSRWLockExclusive(&g_lock);
    g_have = false;
    g_frame.n = 0;
    ReleaseSRWLockExclusive(&g_lock);
}

bool Wanted(ID3D12CommandQueue* q, int eye) {
    if (!On() || eye < 0 || eye > 1) return false;
    AcquireSRWLockShared(&g_lock);
    bool any = false;
    if (g_have && g_frame.queue == q && g_frame.eyeOk[eye] && GetTickCount64() - g_at <= kMaxAgeMs)
        for (uint32_t i = 0; i < g_frame.n && !any; ++i) any = g_frame.quad[i].tex && ForEye(g_frame.quad[i].eyes, eye);
    ReleaseSRWLockShared(&g_lock);
    return any;
}

void SlotDone(int slot) {
    if (slot < 0 || slot >= 16 || !g_timed[slot] || !g_readbackPtr || !g_freq) return;
    g_timed[slot] = false;
    const uint64_t a = g_readbackPtr[2 * slot], b = g_readbackPtr[2 * slot + 1];
    if (b <= a) return;
    const double us = (double)(b - a) * 1e6 / (double)g_freq;
    if (us > 100000.0) return;   // (a stale pair: not a time)
    g_n.gpuUs += us;
    if (us > g_n.gpuMaxUs) g_n.gpuMaxUs = us;
    ++g_n.gpuN;
}

uint32_t Draw(ID3D12Device* dev, ID3D12CommandQueue* q, ID3D12GraphicsCommandList* list, int slot, int slots, ID3D12Resource* bb, int eye, const Part& part) {
    Tell();
    if (!On() || !dev || !q || !list || !bb || eye < 0 || eye > 1 || slot < 0 || slot >= slots) return 0;
    const double t0 = UsNow();
    AcquireSRWLockShared(&g_lock);
    if (!g_have || g_frame.queue != q || !g_frame.eyeOk[eye]) { ReleaseSRWLockShared(&g_lock); return 0; }
    if (GetTickCount64() - g_at > kMaxAgeMs) { ++g_n.oldFrame; ReleaseSRWLockShared(&g_lock); return 0; }
    const D3D12_RESOURCE_DESC bd = bb->GetDesc();
    const DXGI_FORMAT target = TargetFormat(bd.Format);
    ID3D12PipelineState* pso = Make(dev, slots) ? PsoFor(target) : nullptr;
    if (!pso) { ReleaseSRWLockShared(&g_lock); return 0; }
    if (!g_freq && FAILED(q->GetTimestampFrequency(&g_freq))) g_freq = 0;
    const bool timed = g_queries && slot < 16 && g_freq;

    // the quads seen from this eye: their corners, and each image once in the list's barriers
    struct Todo { const Quad* q; Corners c; };
    Todo todo[kMaxQuads];
    uint32_t n = 0;
    ID3D12Resource* images[kMaxQuads];
    uint32_t ni = 0;
    for (uint32_t i = 0; i < g_frame.n; ++i) {
        const Quad& qd = g_frame.quad[i];
        if (!ForEye(qd.eyes, eye)) continue;
        if (!qd.tex) { ++g_n.notedNoTex; continue; }
        const Corners c = Project(g_frame.eye[eye], g_frame.tan[eye], part, qd.pose, qd.width, qd.height);
        if (!c.seen) continue;
        todo[n++] = Todo{&qd, c};
        bool known = false;
        for (uint32_t k = 0; k < ni && !known; ++k) known = images[k] == qd.tex;
        if (!known) images[ni++] = qd.tex;
    }
    if (!n) { ReleaseSRWLockShared(&g_lock); return 0; }

    if (timed) list->EndQuery(g_queries, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)(2 * slot));
    // the images from their colour state (the runtime takes them so) to be read, the back buffer to be drawn on - and back after
    D3D12_RESOURCE_BARRIER b[kMaxQuads + 1];
    b[0] = Tr(bb, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    for (uint32_t k = 0; k < ni; ++k) b[k + 1] = Tr(images[k], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    list->ResourceBarrier(ni + 1, b);
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();   // (read when the list is recorded)
    D3D12_RENDER_TARGET_VIEW_DESC rv{};
    rv.Format = target;
    rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    g_dev->CreateRenderTargetView(bb, &rv, rtv);
    list->SetGraphicsRootSignature(g_rs);
    list->SetDescriptorHeaps(1, &g_srvHeap);
    list->SetPipelineState(pso);
    list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    const D3D12_VIEWPORT vp{0.0f, 0.0f, (float)bd.Width, (float)bd.Height, 0.0f, 1.0f};
    const D3D12_RECT sc{0, 0, (LONG)bd.Width, (LONG)bd.Height};
    list->RSSetViewports(1, &vp);
    list->RSSetScissorRects(1, &sc);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = g_srvHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpu = g_srvHeap->GetGPUDescriptorHandleForHeapStart();
    srvCpu.ptr += (SIZE_T)slot * kMaxQuads * g_srvInc;   // (this allocator's slots: its last list is done)
    srvGpu.ptr += (UINT64)slot * kMaxQuads * g_srvInc;
    const bool linearOut = Linear(bd.Format);
    for (uint32_t i = 0; i < n; ++i) {
        const Quad& qd = *todo[i].q;
        const DXGI_FORMAT view = ViewFormat(qd.tex->GetDesc().Format, qd.format);
        D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
        sv.Format = view;
        sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sv.Texture2D.MipLevels = 1;
        D3D12_CPU_DESCRIPTOR_HANDLE c = srvCpu;
        D3D12_GPU_DESCRIPTOR_HANDLE g = srvGpu;
        c.ptr += (SIZE_T)i * g_srvInc;
        g.ptr += (UINT64)i * g_srvInc;
        g_dev->CreateShaderResourceView(qd.tex, &sv, c);
        float k[kConsts];
        for (int v = 0; v < 4; ++v) { k[4 * v] = todo[i].c.c[v].x; k[4 * v + 1] = todo[i].c.c[v].y; k[4 * v + 2] = todo[i].c.c[v].z; k[4 * v + 3] = todo[i].c.c[v].w; }
        k[16] = qd.u0; k[17] = qd.v0; k[18] = qd.u1; k[19] = qd.v1;
        for (int v = 0; v < 4; ++v) k[20 + v] = qd.scale[v];
        k[24] = Srgb(view) ? 1.0f : 0.0f;
        k[25] = linearOut ? 1.0f : 0.0f;
        k[26] = qd.blend ? 1.0f : 0.0f;
        k[27] = qd.unpremultiplied ? 1.0f : 0.0f;
        list->SetGraphicsRoot32BitConstants(0, kConsts, k, 0);
        list->SetGraphicsRootDescriptorTable(1, g);
        list->DrawInstanced(4, 1, 0, 0);
        AddName(qd.name);
    }
    b[0] = Tr(bb, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    for (uint32_t k = 0; k < ni; ++k) b[k + 1] = Tr(images[k], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    list->ResourceBarrier(ni + 1, b);
    if (timed) {
        list->EndQuery(g_queries, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)(2 * slot + 1));
        list->ResolveQueryData(g_queries, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)(2 * slot), 2, g_readback, (UINT64)(2 * slot) * sizeof(uint64_t));
        g_timed[slot] = true;
    }
    ReleaseSRWLockShared(&g_lock);
    const double us = UsNow() - t0;
    ++g_n.presents;
    g_n.quads += n;
    g_n.cpuUs += us;
    if (us > g_n.cpuMaxUs) g_n.cpuMaxUs = us;
    return n;
}

void Drop() {
    for (Pso& p : g_pso) { ReleaseAny(p.pso); p.fmt = DXGI_FORMAT_UNKNOWN; }
    ReleaseAny(g_rs);
    ReleaseAny(g_srvHeap);
    ReleaseAny(g_rtvHeap);
    if (g_readback && g_readbackPtr) g_readback->Unmap(0, nullptr);
    g_readbackPtr = nullptr;
    ReleaseAny(g_readback);
    ReleaseAny(g_queries);
    for (bool& t : g_timed) t = false;
    g_freq = 0;
    g_heapSlots = 0;
    g_failed = false;
    ReleaseAny(g_dev);
}

}  // namespace monitorlayers
