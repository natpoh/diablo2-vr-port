// [render] monitor_eye: the window shows one eye - see vr/monitor_eye.h. bearstonem/d2r-pcvr's idea (MIT).

#include "monitor_eye.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>

#include "gpu_fault.h"
#include "lens_mask_shape.h"
#include "monitor_eye_math.h"
#include "monitor_layers.h"

namespace monitoreye {
namespace {

void (*g_log)(const char*) = nullptr;
void Line(const char* fmt, ...) {
    if (!g_log) return;
    char b[512];
    va_list va;
    va_start(va, fmt);
    vsnprintf(b, sizeof b, fmt, va);
    va_end(va);
    g_log(b);
}

std::atomic<int> g_setting{kAsBefore};
std::atomic<bool> g_fit{true}, g_drawnOnly{false};
std::atomic<bool> g_cropLens{false};   // [render] monitor_eye_crop_lens (ConfigureLens)

// -- our list: a ring of allocators on one fence; the queue may change (the left eye's hold, the present) -----------------------
constexpr int kAllocs = 8;   // (two lists a frame at most: four frames - DXGI lets the GPU fall three behind)
ID3D12Device* g_dev = nullptr;
ID3D12CommandAllocator* g_alloc[kAllocs] = {};
UINT64 g_allocDone[kAllocs] = {};
ID3D12GraphicsCommandList* g_list = nullptr;
ID3D12Fence* g_fence = nullptr;
HANDLE g_event = nullptr;
UINT64 g_fenceV = 0;
ID3D12CommandQueue* g_lastQueue = nullptr;
int g_allocAt = 0, g_openAlloc = 0;
bool g_listFailed = false;

// -- the wanted eye's picture kept (the back buffer's size and format), at rest readable by a copy and a pixel shader ---------------
constexpr D3D12_RESOURCE_STATES kRest = D3D12_RESOURCE_STATE_COPY_SOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
ID3D12Resource* g_kept = nullptr;
int g_keptEye = -1;
ULONGLONG g_keptAt = 0;

// -- eyes by turns: the eye made this frame, for this frame's present -----------------------------------------------------------
ID3D12Resource* g_made = nullptr;
int g_madeEye = -1;

// -- what the lens shows of each eye (SetLens), its grid of hidden corners, and the box of the window's shape inside it --------------
struct LensKept {
    bool ok = false;
    std::vector<float> xy;
    std::vector<uint32_t> idx;
    uint32_t gen = 0;
    float drawn[4] = {}, seen[4] = {};
    bool seenOk = false;
    uint32_t version = 0;          // moves with every change above
    // the grid (made at a present: it needs the picture's shape), and the box for one window
    uint32_t gridVersion = ~0u, gw = 0, gh = 0;
    double gridAspect = 0;
    std::vector<uint8_t> hidden;
    ULONGLONG builtAt = 0;
    uint32_t boxVersion = ~0u, boxW = 0, boxH = 0;
    double boxAspect = 0;
    bool boxOk = false;
    Rect box{};
};
LensKept g_lens[2];
uint32_t g_lensVersion = 0;
uint32_t g_boxTold = 0;
std::atomic<bool> g_console{false};   // D2RLoader's console open (ConsoleKey, the draw thread): no crop while it is
constexpr uint32_t kGrid = 128;   // cells across the picture's width (2304 px: 18 px a cell)

// The box of the window's shape inside what the lens shows of `eye`'s picture (bw x bh), for a window ww x wh; false: none known (the whole
// picture as before). The grid is made again when the lens or the picture's shape changes (at most 4 times a second), the box when the
// window's shape changes.
bool LensBox(int eye, uint32_t bw, uint32_t bh, uint32_t ww, uint32_t wh, Rect* out) {
    if (eye < 0 || eye > 1 || !bw || !bh || !ww || !wh) return false;
    LensKept& k = g_lens[eye];
    if (!k.ok) return false;
    const double picAspect = (double)bw / bh;
    if (k.gridVersion != k.version || std::fabs(k.gridAspect - picAspect) > 1e-4) {
        const ULONGLONG now = GetTickCount64();
        if (k.gridVersion != ~0u && now - k.builtAt < 250 && k.boxOk) { *out = k.box; return true; }   // (a lens that moves all the time: the last box meanwhile)
        k.gw = kGrid;
        k.gh = (uint32_t)std::lround(kGrid / picAspect);
        if (k.gh < 16) k.gh = 16;
        if (k.gh > 4 * kGrid) k.gh = 4 * kGrid;
        const lensshape::Tan drawn{k.drawn[0], k.drawn[1], k.drawn[2], k.drawn[3]};
        const lensshape::Tan seen{k.seen[0], k.seen[1], k.seen[2], k.seen[3]};
        k.hidden = lensshape::Corners(k.xy.data(), (uint32_t)(k.xy.size() / 2), k.idx.data(), (uint32_t)k.idx.size(), drawn, k.seenOk ? &seen : nullptr,
                                      k.gw, k.gh, nullptr);
        k.gridVersion = k.version;
        k.gridAspect = picAspect;
        k.builtAt = now;
        k.boxVersion = ~0u;
    }
    const double aspect = (double)ww / wh;
    if (k.boxVersion != k.gridVersion || std::fabs(k.boxAspect - aspect) > 1e-4 || k.boxW != bw || k.boxH != bh) {
        k.boxOk = VisibleBox(k.hidden, k.gw, k.gh, aspect, picAspect, &k.box);
        k.boxVersion = k.gridVersion;
        k.boxAspect = aspect;
        k.boxW = bw;
        k.boxH = bh;
        if (k.boxOk && g_boxTold++ < 4)
            Line("vrcam: monitor eye - the %s eye's picture %ux%u: what the lens shows of it in the window's shape (%ux%u) is x %.3f-%.3f y %.3f-%.3f "
                 "(the masked edges left out)", eye ? "right" : "left", bw, bh, ww, wh, k.box.u, k.box.u + k.box.w, k.box.v, k.box.v + k.box.h);
    }
    if (k.boxOk) *out = k.box;
    return k.boxOk;
}

// -- the crop: a full-screen triangle sampling the eye's middle ---------------------------------------------------------------
const char kShader[] = R"(
cbuffer Crop : register(b0) { float4 g_crop; };   // the part of the picture: top left, size (0..1)
Texture2D g_src : register(t0);
SamplerState g_smp : register(s0);
void VSMain(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD0) {
  uv = float2((id << 1) & 2, id & 2);
  pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}
float4 PSMain(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
  return g_src.SampleLevel(g_smp, g_crop.xy + uv * g_crop.zw, 0.0);
}
)";
ID3D12RootSignature* g_rs = nullptr;
struct Pso { DXGI_FORMAT fmt; ID3D12PipelineState* pso; };
Pso g_pso[4] = {};
ID3DBlob* g_vs = nullptr;
ID3DBlob* g_ps = nullptr;
ID3D12DescriptorHeap* g_srvHeap = nullptr;   // shader-visible, a slot per allocator (rewritten once that allocator's list is done)
ID3D12DescriptorHeap* g_rtvHeap = nullptr;
UINT g_srvInc = 0;
bool g_cropFailed = false;

// -- the 10 s line ------------------------------------------------------------------------------------------------------------------
struct Counts {
    uint32_t kept = 0, held = 0, byMade = 0, byKept = 0, none = 0, cropped = 0, behind = 0;
    ULONGLONG since = 0;
} g_n;

void Release(ID3D12Resource*& r) { if (r) { r->Release(); r = nullptr; } }
template <class T> void ReleaseAny(T*& p) { if (p) { p->Release(); p = nullptr; } }

D3D12_RESOURCE_BARRIER Tr(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    return b;
}

bool WaitFence(UINT64 v, DWORD ms) {
    if (!g_fence || g_fence->GetCompletedValue() >= v) return true;
    g_fence->SetEventOnCompletion(v, g_event);
    WaitForSingleObject(g_event, ms);
    return g_fence->GetCompletedValue() >= v;
}

// Everything of a device gone (another device came: the old one's work is done or lost with it).
void DropAll() {
    if (g_fence && g_fenceV) WaitFence(g_fenceV, 200);
    Release(g_kept);
    g_keptEye = -1;
    g_made = nullptr;
    monitorlayers::Drop();   // ([render] monitor_panels: its pipeline and heaps, the same device's)
    for (Pso& p : g_pso) { ReleaseAny(p.pso); p.fmt = DXGI_FORMAT_UNKNOWN; }
    ReleaseAny(g_rs);
    ReleaseAny(g_srvHeap);
    ReleaseAny(g_rtvHeap);
    ReleaseAny(g_list);
    for (ID3D12CommandAllocator*& a : g_alloc) ReleaseAny(a);
    ReleaseAny(g_fence);
    ReleaseAny(g_dev);
    for (UINT64& d : g_allocDone) d = 0;
    g_fenceV = 0;
    g_lastQueue = nullptr;
    g_listFailed = g_cropFailed = false;
}

bool EnsureList(ID3D12CommandQueue* q) {
    ID3D12Device* dev = nullptr;
    if (FAILED(q->GetDevice(__uuidof(ID3D12Device), (void**)&dev)) || !dev) return false;
    if (dev != g_dev) {
        DropAll();
        g_dev = dev;   // (our reference)
    } else {
        dev->Release();
    }
    if (g_list) return true;
    if (g_listFailed) return false;
    g_listFailed = true;   // until it is all made
    for (ID3D12CommandAllocator*& a : g_alloc)
        if (FAILED(g_dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&a))) return false;
    if (FAILED(g_dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&g_fence))) return false;
    if (!g_event) g_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!g_event) return false;
    if (FAILED(g_dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_alloc[0], nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&g_list))) return false;
    g_list->Close();
    for (ID3D12CommandAllocator* a : g_alloc) a->SetName(L"d2rvr monitor eye allocator");   // (the names DRED prints)
    g_list->SetName(L"d2rvr monitor eye list");
    g_fence->SetName(L"d2rvr monitor eye fence");
    g_listFailed = false;
    return true;
}

// Our list on the next allocator, once that allocator's last list is done - never waited for on the draw thread: a GPU that far behind
// leaves this present as it is (counted).
ID3D12GraphicsCommandList* Open(ID3D12CommandQueue* q) {
    if (gpufault::Removed() || !EnsureList(q)) return nullptr;
    const int a = g_allocAt;
    if (g_fence->GetCompletedValue() < g_allocDone[a]) { ++g_n.behind; return nullptr; }
    monitorlayers::SlotDone(a);   // ([render] monitor_panels: that list's GPU time, if it drew any)
    if (FAILED(g_alloc[a]->Reset()) || FAILED(g_list->Reset(g_alloc[a], nullptr))) return nullptr;
    g_openAlloc = a;
    g_allocAt = (a + 1) % kAllocs;
    return g_list;
}
// On `q`; after our last list when that went on another queue (so the one fence keeps counting up in order).
void Submit(ID3D12CommandQueue* q) {
    g_list->Close();
    if (g_lastQueue && g_lastQueue != q && g_fenceV) q->Wait(g_fence, g_fenceV);
    ID3D12CommandList* l = g_list;
    q->ExecuteCommandLists(1, &l);
    q->Signal(g_fence, ++g_fenceV);
    g_allocDone[g_openAlloc] = g_fenceV;
    g_lastQueue = q;
}

void CopyPicture(ID3D12GraphicsCommandList* list, ID3D12Resource* from, D3D12_RESOURCE_STATES fromState, ID3D12Resource* to,
                 D3D12_RESOURCE_STATES toState) {
    D3D12_RESOURCE_BARRIER b[2] = {Tr(from, fromState, D3D12_RESOURCE_STATE_COPY_SOURCE), Tr(to, toState, D3D12_RESOURCE_STATE_COPY_DEST)};
    const UINT nb = fromState == D3D12_RESOURCE_STATE_COPY_SOURCE || fromState == kRest ? 1 : 2;   // (kRest is a copy source already)
    list->ResourceBarrier(nb, nb == 2 ? b : b + 1);
    D3D12_TEXTURE_COPY_LOCATION t{}, f{};
    t.pResource = to; t.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    f.pResource = from; f.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    list->CopyTextureRegion(&t, 0, 0, 0, &f, nullptr);
    D3D12_RESOURCE_BARRIER e[2] = {Tr(from, D3D12_RESOURCE_STATE_COPY_SOURCE, fromState), Tr(to, D3D12_RESOURCE_STATE_COPY_DEST, toState)};
    list->ResourceBarrier(nb, nb == 2 ? e : e + 1);
}

// The kept picture for a back buffer of `bd`: made again when its size or format changes.
bool EnsureKept(const D3D12_RESOURCE_DESC& bd) {
    if (g_kept) {
        const D3D12_RESOURCE_DESC k = g_kept->GetDesc();
        if (k.Width == bd.Width && k.Height == bd.Height && k.Format == bd.Format) return true;
        WaitFence(g_fenceV, 100);   // (our lists that read it: done before it goes - a size change, rare)
        Release(g_kept);
        g_keptEye = -1;
    }
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = bd.Width;
    d.Height = bd.Height;
    d.DepthOrArraySize = d.MipLevels = 1;
    d.Format = bd.Format;
    d.SampleDesc.Count = 1;
    if (FAILED(g_dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, kRest, nullptr, __uuidof(ID3D12Resource), (void**)&g_kept))) {
        g_kept = nullptr;
        return false;
    }
    g_kept->SetName(L"d2rvr monitor eye kept");
    Line("vrcam: monitor eye - the eye's kept picture made, %llux%u (format %d)", (unsigned long long)d.Width, d.Height, (int)d.Format);
    return true;
}
bool KeepInto(ID3D12GraphicsCommandList* list, ID3D12Resource* bb, int eye) {
    if (!EnsureKept(bb->GetDesc())) return false;
    CopyPicture(list, bb, D3D12_RESOURCE_STATE_PRESENT, g_kept, kRest);
    g_keptEye = eye;
    g_keptAt = GetTickCount64();
    return true;
}

// The crop's root signature, shaders and descriptor heaps, once; its pipeline for each back buffer format.
bool MakeCrop() {
    if (g_rs && g_srvHeap && g_rtvHeap && g_vs && g_ps) return true;
    if (g_cropFailed) return false;
    g_cropFailed = true;   // until it is all made
    if (!g_vs || !g_ps) {
        HMODULE dc = LoadLibraryExW(L"d3dcompiler_47.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);   // (kept loaded)
        const pD3DCompile compile = dc ? (pD3DCompile)GetProcAddress(dc, "D3DCompile") : nullptr;
        if (!compile) { Line("vrcam: monitor eye - no shader compiler (d3dcompiler_47.dll): the eye copied as it is, not cropped to the window"); return false; }
        for (int i = 0; i < 2; ++i) {
            ID3DBlob* err = nullptr;
            ID3DBlob** out = i ? &g_ps : &g_vs;
            if (FAILED(compile(kShader, sizeof kShader - 1, "d2rvr_monitor_eye", nullptr, nullptr, i ? "PSMain" : "VSMain", i ? "ps_5_0" : "vs_5_0",
                               D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out, &err))) {
                Line("vrcam: monitor eye - the crop's shader did not compile (%s): the eye copied as it is", err ? (const char*)err->GetBufferPointer() : "?");
                if (err) err->Release();
                return false;
            }
            if (err) err->Release();
        }
    }
    using SerializeFn = HRESULT(WINAPI*)(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);
    HMODULE d12 = GetModuleHandleW(L"d3d12.dll");
    const SerializeFn serialize = d12 ? (SerializeFn)GetProcAddress(d12, "D3D12SerializeRootSignature") : nullptr;
    if (!serialize) return false;
    D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
    D3D12_ROOT_PARAMETER rp[2] = {};
    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rp[0].Constants = {0, 0, 4};
    rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
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
    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kAllocs, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    D3D12_DESCRIPTOR_HEAP_DESC rd{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
    if (FAILED(g_dev->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void**)&g_srvHeap)) ||
        FAILED(g_dev->CreateDescriptorHeap(&rd, __uuidof(ID3D12DescriptorHeap), (void**)&g_rtvHeap)))
        return false;
    g_srvInc = g_dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    g_srvHeap->SetName(L"d2rvr monitor eye srv");
    g_rtvHeap->SetName(L"d2rvr monitor eye rtv");
    g_cropFailed = false;
    return true;
}
ID3D12PipelineState* CropPso(DXGI_FORMAT fmt) {
    for (const Pso& p : g_pso)
        if (p.pso && p.fmt == fmt) return p.pso;
    Pso* slot = nullptr;
    for (Pso& p : g_pso)
        if (!p.pso) { slot = &p; break; }
    if (!slot) return nullptr;   // (four back buffer formats in one run: not seen)
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
    d.pRootSignature = g_rs;
    d.VS = {g_vs->GetBufferPointer(), g_vs->GetBufferSize()};
    d.PS = {g_ps->GetBufferPointer(), g_ps->GetBufferSize()};
    d.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    d.SampleMask = UINT_MAX;
    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    d.RasterizerState.DepthClipEnable = TRUE;
    d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.NumRenderTargets = 1;
    d.RTVFormats[0] = fmt;
    d.SampleDesc.Count = 1;
    if (FAILED(g_dev->CreateGraphicsPipelineState(&d, __uuidof(ID3D12PipelineState), (void**)&slot->pso))) {
        slot->pso = nullptr;
        Line("vrcam: monitor eye - no crop pipeline for the back buffer's format %d: the eye copied as it is", (int)fmt);
        return nullptr;
    }
    slot->fmt = fmt;
    return slot->pso;
}
// `src` (state `srcState`, viewable as the back buffer's format) - its part `r` - drawn over the whole back buffer.
bool DrawCrop(ID3D12GraphicsCommandList* list, ID3D12Resource* src, D3D12_RESOURCE_STATES srcState, ID3D12Resource* bb, const Rect& r) {
    const D3D12_RESOURCE_DESC bd = bb->GetDesc();
    if (!MakeCrop()) return false;
    ID3D12PipelineState* pso = CropPso(bd.Format);
    if (!pso) return false;
    D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = bd.Format;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = g_srvHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpu = g_srvHeap->GetGPUDescriptorHandleForHeapStart();
    srvCpu.ptr += (SIZE_T)g_openAlloc * g_srvInc;   // (this allocator's slot: its last list is done)
    srvGpu.ptr += (UINT64)g_openAlloc * g_srvInc;
    g_dev->CreateShaderResourceView(src, &sv, srvCpu);
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();   // (read when the list is recorded)
    g_dev->CreateRenderTargetView(bb, nullptr, rtv);
    const bool srcReadable = (srcState & D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) != 0;
    D3D12_RESOURCE_BARRIER b[2] = {Tr(bb, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET),
                                   Tr(src, srcState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)};
    list->ResourceBarrier(srcReadable ? 1 : 2, b);
    list->SetGraphicsRootSignature(g_rs);
    list->SetDescriptorHeaps(1, &g_srvHeap);
    list->SetPipelineState(pso);
    const float c[4] = {r.u, r.v, r.w, r.h};
    list->SetGraphicsRoot32BitConstants(0, 4, c, 0);
    list->SetGraphicsRootDescriptorTable(1, srvGpu);
    list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    const D3D12_VIEWPORT vp{0.0f, 0.0f, (float)bd.Width, (float)bd.Height, 0.0f, 1.0f};
    const D3D12_RECT sc{0, 0, (LONG)bd.Width, (LONG)bd.Height};
    list->RSSetViewports(1, &vp);
    list->RSSetScissorRects(1, &sc);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(3, 1, 0, 0);
    D3D12_RESOURCE_BARRIER e[2] = {Tr(bb, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT),
                                   Tr(src, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, srcState)};
    list->ResourceBarrier(srcReadable ? 1 : 2, e);
    return true;
}

void Tell() {
    const ULONGLONG now = GetTickCount64();
    if (!g_n.since) g_n.since = now;
    if (now - g_n.since < 10000) return;
    if (g_n.kept || g_n.byMade || g_n.byKept || g_n.none || g_n.behind)
        Line("vrcam: monitor eye, 10 s: the %s eye's own presents %u (kept), left eye's holds kept %u; the other eye's presents covered by the eye made %u, "
             "by the eye kept %u, left as they were %u (nothing kept yet); cropped to the window %u; left out, the GPU behind %u",
             EyeOf(g_setting.load()) == 1 ? "right" : "left", g_n.kept, g_n.held, g_n.byMade, g_n.byKept, g_n.none, g_n.cropped, g_n.behind);
    g_n = Counts{};
    g_n.since = now;
}

}  // namespace

void SetLogger(void (*log)(const char*)) { g_log = log; }

void Configure(int setting, bool fit, bool drawnOnly) {
    if (setting < kAsBefore || setting > kRight) setting = kLeft;
    const int was = g_setting.exchange(setting);
    const bool fitWas = g_fit.exchange(fit), drawnWas = g_drawnOnly.exchange(drawnOnly);
    if (was == setting && fitWas == fit && drawnWas == drawnOnly) return;
    if (setting == kAsBefore)
        Line("vrcam: monitor eye - off ([render] monitor_eye 0): the window shows each eye as it is presented, as before");
    else
        Line("vrcam: monitor eye - the window shows the %s eye only, in stereo ([render] monitor_eye %d; bearstonem/d2r-pcvr's idea): the other eye's "
             "presents covered with it%s%s", setting == kLeft ? "left" : "right", setting,
             drawnOnly ? "; eyes by turns: its last picture the game drew (monitor_eye_drawn_only)" : "; eyes by turns: the picture made for it this frame",
             fit ? "; cropped to the window's shape when the VR render size is another (monitor_eye_fit)" : "");
}

bool On() { return g_setting.load(std::memory_order_relaxed) != kAsBefore; }

void ConfigureLens(bool cropLens) {
    if (g_cropLens.exchange(cropLens) == cropLens) return;
    Line(cropLens ? "vrcam: monitor eye - [render] monitor_eye_crop_lens 1: the window gets what the lens shows of the eye, in the window's shape (the "
                    "lens mask's edges left out)"
                  : "vrcam: monitor eye - [render] monitor_eye_crop_lens 0: the window gets the whole eye picture (lens mask edges included), as before");
}
bool CropLens() { return g_cropLens.load(std::memory_order_relaxed); }

void ConsoleKey(unsigned vk, bool ctrl, bool repeat) {
    static ConsoleGate gate;   // the draw thread only (MonitorPresent)
    if (!gate.Key(vk, ctrl, repeat)) return;
    g_console.store(gate.open);
    if (On())
        Line(gate.open ? "vrcam: monitor eye - D2RLoader's console opened (Ctrl + `): the window shows the whole picture, not cropped, while it is open"
                       : "vrcam: monitor eye - D2RLoader's console closed: the window's crop to what the lens shows again");
}

void SetLens(int eye, const Lens* lens) {
    if (eye < 0 || eye > 1) return;
    LensKept& k = g_lens[eye];
    if (!lens) {
        if (k.ok) { k.ok = false; k.version = ++g_lensVersion; }
        return;
    }
    bool same = k.ok && k.gen == lens->gen && k.seenOk == lens->seenOk && k.xy.size() == (size_t)lens->nVerts * 2 && k.idx.size() == lens->nIdx;
    for (int i = 0; i < 4 && same; ++i) same = std::fabs(k.drawn[i] - lens->drawn[i]) < 1e-4f && std::fabs(k.seen[i] - lens->seen[i]) < 1e-4f;
    if (same) return;
    k.ok = true;
    k.gen = lens->gen;
    k.seenOk = lens->seenOk;
    k.xy.assign(lens->xy, lens->xy + (lens->xy ? (size_t)lens->nVerts * 2 : 0));
    k.idx.assign(lens->idx, lens->idx + (lens->idx ? lens->nIdx : 0));
    for (int i = 0; i < 4; ++i) { k.drawn[i] = lens->drawn[i]; k.seen[i] = lens->seen[i]; }
    k.version = ++g_lensVersion;
}

void KeepHeld(ID3D12CommandQueue* q, int eye, ID3D12Resource* src) {
    if (EyeOf(g_setting.load()) != eye || !q || !src) return;
    ID3D12GraphicsCommandList* list = Open(q);
    if (!list) return;
    if (KeepInto(list, src, eye)) ++g_n.held;
    Submit(q);
}

void Made(int eye, ID3D12Resource* made) {
    g_made = made;
    g_madeEye = eye;
}

void Present(ID3D12CommandQueue* q, HWND__* wnd, ID3D12Resource* bb, int eye) {
    ID3D12Resource* made = g_made;
    const int madeEye = g_madeEye;
    g_made = nullptr;   // (this frame's only)
    const int want = EyeOf(g_setting.load());
    const bool panels = monitorlayers::On();   // [render] monitor_panels: the headset's panels over the picture too (with monitor_eye 0 as well)
    if ((want < 0 && !panels) || !q || !bb) return;
    if (want >= 0) Tell();
    if (eye < 0) return;
    const D3D12_RESOURCE_DESC bd = bb->GetDesc();
    RECT rc{};
    const bool client = wnd && GetClientRect((HWND)wnd, &rc) && rc.right > 0 && rc.bottom > 0;
    In in{};
    in.want = want;
    in.shown = eye;
    // what the lens shows of the eye the window gets (the masked edges left out), in the window's shape - a crop even when the window has the
    // picture's shape (0.209.18: a square window over a square picture showed the lens mask's black polygon whole)
    Rect lensBox{};
    // D2RLoader's console open: no crop of either kind - the console is drawn into the picture with the window's full rect, a crop magnified it
    // off the window (0.210.1). [render] monitor_eye_crop_lens (its own key, off by default): what the lens shows; monitor_eye_fit: as ever,
    // only a window of another shape than the picture.
    const bool any = client && !g_console.load(std::memory_order_relaxed);
    const bool lens = any && g_cropLens.load() &&
                      LensBox(want >= 0 ? want : eye, (uint32_t)bd.Width, bd.Height, (uint32_t)rc.right, (uint32_t)rc.bottom, &lensBox);
    in.fit = lens || (any && g_fit.load() && FitWanted((uint32_t)bd.Width, bd.Height, (uint32_t)rc.right, (uint32_t)rc.bottom));
    in.drawnOnly = g_drawnOnly.load();
    if (g_kept && want >= 0) {
        const D3D12_RESOURCE_DESC k = g_kept->GetDesc();
        in.keptOk = g_keptEye == want && k.Width == bd.Width && k.Height == bd.Height && k.Format == bd.Format &&
                    GetTickCount64() - g_keptAt <= kKeptMaxMs;
    }
    if (made && madeEye == want && want >= 0) {
        const D3D12_RESOURCE_DESC m = made->GetDesc();
        in.madeOk = m.Width == bd.Width && m.Height == bd.Height;
    }
    const Step st = Decide(in);
    ID3D12Resource* src = st.from == Source::Made ? made : st.from == Source::Kept && (st.keep || g_keptEye == want) ? g_kept : nullptr;
    // the eye whose picture the window gets: the wanted one when it is there (its own present, or put over the other's), else this one's
    const int pic = want >= 0 && (eye == want || src) ? want : eye;
    const bool panelsNow = panels && monitorlayers::Wanted(q, pic);
    if (!st.keep && !src && !panelsNow) {
        if (want >= 0 && eye != want) ++g_n.none;
        return;
    }
    ID3D12GraphicsCommandList* list = Open(q);
    if (!list) return;
    if (st.keep && KeepInto(list, bb, want)) ++g_n.kept;
    if (st.keep && st.from == Source::Kept) src = g_keptEye == want ? g_kept : nullptr;   // (made by the keep just now; its keep failed: nothing to crop from)
    const D3D12_RESOURCE_STATES srcState = st.from == Source::Made ? D3D12_RESOURCE_STATE_RENDER_TARGET : kRest;
    monitorlayers::Part part = monitorlayers::Whole();   // what of the eye's picture the back buffer holds now
    if (src) {
        const Rect r = lens ? lensBox : Crop((uint32_t)bd.Width, bd.Height, (uint32_t)rc.right, (uint32_t)rc.bottom);
        if (st.crop && DrawCrop(list, src, srcState, bb, r)) { ++g_n.cropped; part = {r.u, r.v, r.w, r.h}; }
        else if (src != g_kept || !st.keep) CopyPicture(list, src, srcState, bb, D3D12_RESOURCE_STATE_PRESENT);   // (the eye wanted, its crop failed: as it is)
        if (eye != want) ++(st.from == Source::Made ? g_n.byMade : g_n.byKept);
    }
    if (panelsNow) monitorlayers::Draw(g_dev, q, list, g_openAlloc, kAllocs, bb, pic, part);   // over the picture as the window gets it
    Submit(q);
}

}  // namespace monitoreye
