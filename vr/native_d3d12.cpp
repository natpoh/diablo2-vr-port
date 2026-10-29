// native_d3d12.cpp - see native_d3d12.h.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "MinHook.h"
#include "native_d3d12.h"

namespace natived3d {
namespace {

LogFn g_log = nullptr;
Sinks g_sinks;
void LogF(const char* fmt, ...) {
    if (!g_log) return;
    char b[512];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    g_log(b);
}

thread_local bool t_ours = false;      // between OpenList and Submit: our own list's calls are not the game's
thread_local int t_present = 0;        // inside a Present (Present1 may come through Present): the sink once
thread_local int t_create = 0;         // inside a swap chain's creation (one may make it through the other)
// Inside a list's hooked call: the D3D12 debug layer's list forwards each call to the runtime's, both hooked - only the outer one is told
// (each was told twice: the interface's layer handled twice, the second time from what the first had emptied)
thread_local int t_inList = 0;
struct InList {
    bool outer;
    InList() : outer(t_inList++ == 0) {}
    ~InList() { --t_inList; }
};

std::mutex g_hookMx;
std::vector<void*> g_hooked;   // the functions hooked already (vtables share them)
bool HookOne(void* fn, void* detour, void** orig, const char* what) {
    std::lock_guard<std::mutex> l(g_hookMx);
    if (std::find(g_hooked.begin(), g_hooked.end(), fn) != g_hooked.end()) return true;
    const bool ok = fn && MH_CreateHook(fn, detour, orig) == MH_OK && MH_EnableHook(fn) == MH_OK;
    if (ok) g_hooked.push_back(fn);
    else LogF("vrcam: no ReShade - the hook on %s did not go in", what);
    return ok;
}

// ---- the device: its depth-stencil views (handle -> resource; nullptr = the plugin's own, never told)
using CreateDeviceFn = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
using CreateDsvFn = void(STDMETHODCALLTYPE*)(ID3D12Device*, ID3D12Resource*, const D3D12_DEPTH_STENCIL_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
CreateDeviceFn g_origCreateDevice = nullptr;
CreateDsvFn g_origCreateDsv = nullptr;
SRWLOCK g_viewLock = SRWLOCK_INIT;
std::unordered_map<SIZE_T, ID3D12Resource*> g_views;
std::atomic<bool> g_deviceHooked{false};

std::atomic<uint32_t> g_presents{0}, g_depthBinds{0}, g_unknownViews{0};
std::atomic<bool> g_swapSeen{false};

// The plugin's own resources are named "d2rvr ..." (turn_warp's depth tests): their depth-stencil binds are not the game's scene.
bool Ours(ID3D12Resource* r) {
    static const GUID kName = {0x4cca5fd8, 0x921f, 0x42c8, {0x85, 0x66, 0x70, 0xca, 0xf2, 0xa9, 0xb7, 0x41}};   // WKPDID_D3DDebugObjectNameW
    wchar_t name[64] = L"";
    UINT size = 0;
    if (FAILED(r->GetPrivateData(kName, &size, nullptr)) || size < 5 * sizeof(wchar_t)) return false;
    size = std::min<UINT>(size, sizeof name - sizeof(wchar_t));
    if (FAILED(r->GetPrivateData(kName, &size, name))) return false;
    return wcsncmp(name, L"d2rvr", 5) == 0;
}
// ... and its render-target views ([hud]: which one is the interface's layer)
using CreateRtvFn = void(STDMETHODCALLTYPE*)(ID3D12Device*, ID3D12Resource*, const D3D12_RENDER_TARGET_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
using CreateListFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*, UINT, D3D12_COMMAND_LIST_TYPE, ID3D12CommandAllocator*, ID3D12PipelineState*, REFIID, void**);
CreateRtvFn g_origCreateRtv = nullptr;
CreateListFn g_origCreateList = nullptr;
SRWLOCK g_rtvLock = SRWLOCK_INIT;
std::unordered_map<SIZE_T, ID3D12Resource*> g_rtvs;
std::atomic<unsigned> g_rtvInc{0};
void STDMETHODCALLTYPE HookCreateRtv(ID3D12Device* d, ID3D12Resource* r, const D3D12_RENDER_TARGET_VIEW_DESC* desc, D3D12_CPU_DESCRIPTOR_HANDLE h) {
    g_origCreateRtv(d, r, desc, h);
    AcquireSRWLockExclusive(&g_rtvLock);
    g_rtvs[h.ptr] = r;
    ReleaseSRWLockExclusive(&g_rtvLock);
}
// [fog] game_fog_before_fx: shader-resource views of R11G11B10 2D textures, by slot (TrackSceneViews). Every view made or copied over a slot
// takes it out unless it is one of them; the lock shared for the look (most views are not), exclusive only to change.
using CreateSrvFn = void(STDMETHODCALLTYPE*)(ID3D12Device*, ID3D12Resource*, const D3D12_SHADER_RESOURCE_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
CreateSrvFn g_origCreateSrv = nullptr;
std::atomic<bool> g_trackSrv{false};
std::atomic<unsigned> g_srvInc{0};
SRWLOCK g_srvLock = SRWLOCK_INIT;
std::unordered_map<SIZE_T, ID3D12Resource*> g_srvs;
void NoteSrv(SIZE_T slot, ID3D12Resource* scene) {
    if (scene) {
        AcquireSRWLockExclusive(&g_srvLock);
        g_srvs[slot] = scene;
        ReleaseSRWLockExclusive(&g_srvLock);
        return;
    }
    AcquireSRWLockShared(&g_srvLock);
    const bool had = g_srvs.find(slot) != g_srvs.end();
    ReleaseSRWLockShared(&g_srvLock);
    if (!had) return;
    AcquireSRWLockExclusive(&g_srvLock);
    g_srvs.erase(slot);
    ReleaseSRWLockExclusive(&g_srvLock);
}
ID3D12Resource* SrvAt(SIZE_T slot) {
    AcquireSRWLockShared(&g_srvLock);
    const auto it = g_srvs.find(slot);
    ID3D12Resource* r = it != g_srvs.end() ? it->second : nullptr;
    ReleaseSRWLockShared(&g_srvLock);
    return r;
}
void STDMETHODCALLTYPE HookCreateSrv(ID3D12Device* d, ID3D12Resource* r, const D3D12_SHADER_RESOURCE_VIEW_DESC* desc, D3D12_CPU_DESCRIPTOR_HANDLE h) {
    g_origCreateSrv(d, r, desc, h);
    if (!g_trackSrv.load(std::memory_order_relaxed) || t_ours) return;
    bool scene = false;
    if (r) {
        const D3D12_RESOURCE_DESC rd = r->GetDesc();
        scene = rd.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && rd.Format == DXGI_FORMAT_R11G11B10_FLOAT &&
                (!desc || (desc->ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2D && desc->Format == DXGI_FORMAT_R11G11B10_FLOAT));
    }
    NoteSrv(h.ptr, scene ? r : nullptr);
}
// Views copied from one heap to another (CopyDescriptors / CopyDescriptorsSimple): the copy is of the same resource - a slot a view was made in
// once and copied into later would otherwise still name the first, maybe freed, resource.
using CopyDescFn = void(STDMETHODCALLTYPE*)(ID3D12Device*, UINT, const D3D12_CPU_DESCRIPTOR_HANDLE*, const UINT*, UINT, const D3D12_CPU_DESCRIPTOR_HANDLE*,
                                            const UINT*, D3D12_DESCRIPTOR_HEAP_TYPE);
using CopyDescSimpleFn = void(STDMETHODCALLTYPE*)(ID3D12Device*, UINT, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_DESCRIPTOR_HEAP_TYPE);
CopyDescFn g_origCopyDesc = nullptr;
CopyDescSimpleFn g_origCopyDescSimple = nullptr;
std::atomic<unsigned> g_dsvInc{0};
void CopiedViews(D3D12_DESCRIPTOR_HEAP_TYPE type, SIZE_T to, SIZE_T from, UINT n) {
    if (type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) {
        if (!g_trackSrv.load(std::memory_order_relaxed)) return;
        const SIZE_T inc = g_srvInc.load();
        for (UINT i = 0; i < n; ++i) NoteSrv(to + i * inc, SrvAt(from + i * inc));
        return;
    }
    const bool rtv = type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    if (!rtv && type != D3D12_DESCRIPTOR_HEAP_TYPE_DSV) return;
    const SIZE_T inc = rtv ? g_rtvInc.load() : g_dsvInc.load();
    SRWLOCK* lock = rtv ? &g_rtvLock : &g_viewLock;
    auto& map = rtv ? g_rtvs : g_views;
    AcquireSRWLockExclusive(lock);
    for (UINT i = 0; i < n; ++i) {
        const auto it = map.find(from + i * inc);
        if (it != map.end()) map[to + i * inc] = it->second;
        else map.erase(to + i * inc);
    }
    ReleaseSRWLockExclusive(lock);
}
void STDMETHODCALLTYPE HookCopyDesc(ID3D12Device* d, UINT nDst, const D3D12_CPU_DESCRIPTOR_HANDLE* dst, const UINT* dstSizes, UINT nSrc,
                                    const D3D12_CPU_DESCRIPTOR_HANDLE* src, const UINT* srcSizes, D3D12_DESCRIPTOR_HEAP_TYPE type) {
    g_origCopyDesc(d, nDst, dst, dstSizes, nSrc, src, srcSizes, type);
    if (type != D3D12_DESCRIPTOR_HEAP_TYPE_RTV && type != D3D12_DESCRIPTOR_HEAP_TYPE_DSV &&
        !(type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV && g_trackSrv.load(std::memory_order_relaxed)))
        return;
    // the ranges walked together, one descriptor at a time (a range of either side may span ranges of the other)
    UINT di = 0, si = 0, dOff = 0, sOff = 0;
    const SIZE_T inc = type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV ? g_rtvInc.load() : type == D3D12_DESCRIPTOR_HEAP_TYPE_DSV ? g_dsvInc.load() : g_srvInc.load();
    while (di < nDst && si < nSrc) {
        const UINT dn = dstSizes ? dstSizes[di] : 1, sn = srcSizes ? srcSizes[si] : 1;
        CopiedViews(type, dst[di].ptr + dOff * inc, src[si].ptr + sOff * inc, 1);
        if (++dOff >= dn) { ++di; dOff = 0; }
        if (++sOff >= sn) { ++si; sOff = 0; }
    }
}
void STDMETHODCALLTYPE HookCopyDescSimple(ID3D12Device* d, UINT n, D3D12_CPU_DESCRIPTOR_HANDLE dst, D3D12_CPU_DESCRIPTOR_HANDLE src, D3D12_DESCRIPTOR_HEAP_TYPE type) {
    g_origCopyDescSimple(d, n, dst, src, type);
    CopiedViews(type, dst.ptr, src.ptr, n);
}
// A list made is recording at once, no Reset: a new recording ([hud]: the interface bound on a list let go at the same address is not this one's)
HRESULT STDMETHODCALLTYPE HookCreateList(ID3D12Device* d, UINT mask, D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator* a, ID3D12PipelineState* ps,
                                         REFIID riid, void** out) {
    const HRESULT hr = g_origCreateList(d, mask, type, a, ps, riid, out);
    if (SUCCEEDED(hr) && out && *out && !t_ours && (g_sinks.listReset || g_sinks.fxReset) && type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
        ID3D12GraphicsCommandList* l = nullptr;
        if (SUCCEEDED(((IUnknown*)*out)->QueryInterface(__uuidof(ID3D12GraphicsCommandList), (void**)&l)) && l) {
            if (g_sinks.listReset) g_sinks.listReset(l);
            if (g_sinks.fxReset) g_sinks.fxReset(l);
            l->Release();
        }
    }
    return hr;
}
void STDMETHODCALLTYPE HookCreateDsv(ID3D12Device* d, ID3D12Resource* r, const D3D12_DEPTH_STENCIL_VIEW_DESC* desc, D3D12_CPU_DESCRIPTOR_HANDLE h) {
    g_origCreateDsv(d, r, desc, h);
    ID3D12Resource* keep = r && !Ours(r) ? r : nullptr;
    AcquireSRWLockExclusive(&g_viewLock);
    g_views[h.ptr] = keep;
    ReleaseSRWLockExclusive(&g_viewLock);
}

// ---- the command lists
using OmSetFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, const D3D12_CPU_DESCRIPTOR_HANDLE*, BOOL, const D3D12_CPU_DESCRIPTOR_HANDLE*);
using BarrierFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, const D3D12_RESOURCE_BARRIER*);
using ViewportsFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, const D3D12_VIEWPORT*);
using ResetFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12CommandAllocator*, ID3D12PipelineState*);
using ScissorsFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, const D3D12_RECT*);
using ClearRtvFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, D3D12_CPU_DESCRIPTOR_HANDLE, const FLOAT*, UINT, const D3D12_RECT*);
OmSetFn g_origOmSet = nullptr;
BarrierFn g_origBarrier = nullptr;
ViewportsFn g_origViewports = nullptr;
ResetFn g_origReset = nullptr;
ScissorsFn g_origScissors = nullptr;
ClearRtvFn g_origClearRtv = nullptr;

HRESULT STDMETHODCALLTYPE HookReset(ID3D12GraphicsCommandList* cl, ID3D12CommandAllocator* a, ID3D12PipelineState* ps) {
    const InList in;
    const HRESULT hr = g_origReset(cl, a, ps);
    if (in.outer && !t_ours && g_sinks.listReset && SUCCEEDED(hr)) g_sinks.listReset(cl);
    if (in.outer && !t_ours && g_sinks.fxReset && SUCCEEDED(hr)) g_sinks.fxReset(cl);
    return hr;
}
void STDMETHODCALLTYPE HookScissors(ID3D12GraphicsCommandList* cl, UINT n, const D3D12_RECT* r) {
    const InList in;
    if (in.outer && !t_ours && g_sinks.scissors && n && r) g_sinks.scissors(cl, n, r);
    g_origScissors(cl, n, r);
}
// (before the clear, as ReShade's event: the interface's layer is learnt from its frame-start clear)
void STDMETHODCALLTYPE HookClearRtv(ID3D12GraphicsCommandList* cl, D3D12_CPU_DESCRIPTOR_HANDLE h, const FLOAT* c, UINT n, const D3D12_RECT* r) {
    const InList in;
    if (in.outer && !t_ours && g_sinks.clearRtv && c) g_sinks.clearRtv(cl, &h, c, n, r);
    g_origClearRtv(cl, h, c, n, r);
}

void STDMETHODCALLTYPE HookOmSet(ID3D12GraphicsCommandList* cl, UINT n, const D3D12_CPU_DESCRIPTOR_HANDLE* rts, BOOL single,
                                 const D3D12_CPU_DESCRIPTOR_HANDLE* ds) {
    const InList in;
    if (in.outer && !t_ours && g_sinks.fxTargets) {
        ID3D12Resource* r = nullptr;
        if (ds) {
            AcquireSRWLockShared(&g_viewLock);
            const auto it = g_views.find(ds->ptr);
            r = it != g_views.end() ? it->second : nullptr;
            ReleaseSRWLockShared(&g_viewLock);
        }
        g_sinks.fxTargets(cl, n, rts, single != FALSE, r);
    }
    if (in.outer && !t_ours && g_sinks.bindDepth) {
        ID3D12Resource* r = nullptr;
        bool tell = true;
        if (ds) {
            AcquireSRWLockShared(&g_viewLock);
            const auto it = g_views.find(ds->ptr);
            const bool known = it != g_views.end();
            r = known ? it->second : nullptr;
            ReleaseSRWLockShared(&g_viewLock);
            if (known && !r) tell = false;   // the plugin's own depth-stencil
            else if (!known) g_unknownViews.fetch_add(1, std::memory_order_relaxed);
            else g_depthBinds.fetch_add(1, std::memory_order_relaxed);
        }
        if (tell) g_sinks.bindDepth(cl, r, n);
    }
    if (in.outer && !t_ours && g_sinks.targets) g_sinks.targets(cl, n, rts, single != FALSE);
    g_origOmSet(cl, n, rts, single, ds);
}
void STDMETHODCALLTYPE HookBarrier(ID3D12GraphicsCommandList* cl, UINT n, const D3D12_RESOURCE_BARRIER* b) {
    const InList in;
    if (in.outer && !t_ours && g_sinks.barrier && n && b) g_sinks.barrier(cl, n, b);
    if (in.outer && !t_ours && g_sinks.fxBarrier && n && b) g_sinks.fxBarrier(cl, n, b);
    g_origBarrier(cl, n, b);
}
// [sky] game_sky: does the game draw the dome at all (PC1 2026-10-27: placed and laid, nothing seen even at 50 units)? Its mesh's index count
// is its own; the game's draws of exactly that many are counted. Off (0): one relaxed load a draw.
using DrawIndexedFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, UINT, UINT, INT, UINT);
DrawIndexedFn g_origDrawIndexed = nullptr;
std::atomic<uint32_t> g_watchCount{0}, g_watchDraws{0}, g_watchInstances{0};
void STDMETHODCALLTYPE HookDrawIndexed(ID3D12GraphicsCommandList* cl, UINT n, UINT inst, UINT first, INT base, UINT firstInst) {
    const uint32_t w = g_watchCount.load(std::memory_order_relaxed);
    if (w && n == w && !t_ours) {
        g_watchDraws.fetch_add(1, std::memory_order_relaxed);
        g_watchInstances.fetch_add(inst, std::memory_order_relaxed);
    }
    {   // [fog] game_fog_before_fx: the resolve may be an indexed draw too (PC1 2026-10-28: never seen through DrawInstanced alone)
        const InList in;
        if (in.outer && !t_ours && g_sinks.fxDraw) g_sinks.fxDraw(cl, n, inst);
    }
    g_origDrawIndexed(cl, n, inst, first, base, firstInst);
}

// [fog] game_fog_height: the game's pipelines as they are made (the fog pass's is told to the sink) and bound (the sink may give another)
using CreateGpsoFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*, const D3D12_GRAPHICS_PIPELINE_STATE_DESC*, REFIID, void**);
using CreatePsoFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device2*, const D3D12_PIPELINE_STATE_STREAM_DESC*, REFIID, void**);
using SetPsoFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12PipelineState*);
CreateGpsoFn g_origCreateGpso = nullptr;
CreatePsoFn g_origCreatePso = nullptr;
SetPsoFn g_origSetPso = nullptr;
HRESULT STDMETHODCALLTYPE HookCreateGpso(ID3D12Device* dev, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* d, REFIID riid, void** out) {
    const HRESULT hr = g_origCreateGpso(dev, d, riid, out);
    if (SUCCEEDED(hr) && out && *out && d && !t_ours && g_sinks.graphicsPso && riid == __uuidof(ID3D12PipelineState))
        g_sinks.graphicsPso(dev, d, (ID3D12PipelineState*)*out);
    return hr;
}
HRESULT STDMETHODCALLTYPE HookCreatePso(ID3D12Device2* dev, const D3D12_PIPELINE_STATE_STREAM_DESC* d, REFIID riid, void** out) {
    const HRESULT hr = g_origCreatePso(dev, d, riid, out);
    if (SUCCEEDED(hr) && out && *out && d && !t_ours && g_sinks.streamPso && riid == __uuidof(ID3D12PipelineState))
        g_sinks.streamPso(dev, d, (ID3D12PipelineState*)*out);
    return hr;
}
void STDMETHODCALLTYPE HookSetPso(ID3D12GraphicsCommandList* cl, ID3D12PipelineState* pso) {
    const InList in;
    if (pso && !t_ours && g_sinks.psoSwap) pso = g_sinks.psoSwap(pso);
    g_origSetPso(cl, pso);
    if (in.outer && !t_ours && g_sinks.fxBound) g_sinks.fxBound(cl, pso);
}

// [fog] game_fog_before_fx: the heaps, the root arguments a dispatch of ours changes, and the draws (the resolve's is a DrawInstanced)
using HeapsFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, ID3D12DescriptorHeap* const*);
using RootSigFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12RootSignature*);
using TableFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_DESCRIPTOR_HANDLE);
using ConstFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, UINT, UINT);
using ConstsFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, UINT, const void*, UINT);
using ViewFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS);
using DrawFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, UINT, UINT, UINT);
HeapsFn g_origHeaps = nullptr;
RootSigFn g_origCRootSig = nullptr, g_origGRootSig = nullptr;
TableFn g_origCTable = nullptr, g_origGTable = nullptr;
ConstFn g_origCConst = nullptr;
ConstsFn g_origCConsts = nullptr;
ViewFn g_origCCbv = nullptr, g_origCSrv = nullptr, g_origCUav = nullptr;
DrawFn g_origDraw = nullptr;
void Root(ID3D12GraphicsCommandList* cl, const InList& in, RootCall c) {
    if (in.outer && !t_ours && g_sinks.fxRoot) g_sinks.fxRoot(cl, c);
}
void STDMETHODCALLTYPE HookHeaps(ID3D12GraphicsCommandList* cl, UINT n, ID3D12DescriptorHeap* const* h) {
    const InList in;
    if (in.outer && !t_ours && g_sinks.fxHeaps) g_sinks.fxHeaps(cl, n, h);
    g_origHeaps(cl, n, h);
}
void STDMETHODCALLTYPE HookCRootSig(ID3D12GraphicsCommandList* cl, ID3D12RootSignature* r) {
    const InList in;
    Root(cl, in, {RootCall::kComputeRootSignature, 0, (uint64_t)(uintptr_t)r});
    g_origCRootSig(cl, r);
}
void STDMETHODCALLTYPE HookGRootSig(ID3D12GraphicsCommandList* cl, ID3D12RootSignature* r) {
    const InList in;
    Root(cl, in, {RootCall::kGraphicsRootSignature, 0, (uint64_t)(uintptr_t)r});
    g_origGRootSig(cl, r);
}
void STDMETHODCALLTYPE HookCTable(ID3D12GraphicsCommandList* cl, UINT i, D3D12_GPU_DESCRIPTOR_HANDLE h) {
    const InList in;
    Root(cl, in, {RootCall::kComputeTable, i, h.ptr});
    g_origCTable(cl, i, h);
}
void STDMETHODCALLTYPE HookGTable(ID3D12GraphicsCommandList* cl, UINT i, D3D12_GPU_DESCRIPTOR_HANDLE h) {
    const InList in;
    Root(cl, in, {RootCall::kGraphicsTable, i, h.ptr});
    g_origGTable(cl, i, h);
}
void STDMETHODCALLTYPE HookCConst(ID3D12GraphicsCommandList* cl, UINT i, UINT v, UINT at) {
    const InList in;
    Root(cl, in, {RootCall::kComputeConstant, i, v, 1, at});
    g_origCConst(cl, i, v, at);
}
void STDMETHODCALLTYPE HookCConsts(ID3D12GraphicsCommandList* cl, UINT i, UINT n, const void* d, UINT at) {
    const InList in;
    Root(cl, in, {RootCall::kComputeConstants, i, 0, n, at, d});
    g_origCConsts(cl, i, n, d, at);
}
void STDMETHODCALLTYPE HookCCbv(ID3D12GraphicsCommandList* cl, UINT i, D3D12_GPU_VIRTUAL_ADDRESS a) {
    const InList in;
    Root(cl, in, {RootCall::kComputeCbv, i, a});
    g_origCCbv(cl, i, a);
}
void STDMETHODCALLTYPE HookCSrv(ID3D12GraphicsCommandList* cl, UINT i, D3D12_GPU_VIRTUAL_ADDRESS a) {
    const InList in;
    Root(cl, in, {RootCall::kComputeSrv, i, a});
    g_origCSrv(cl, i, a);
}
void STDMETHODCALLTYPE HookCUav(ID3D12GraphicsCommandList* cl, UINT i, D3D12_GPU_VIRTUAL_ADDRESS a) {
    const InList in;
    Root(cl, in, {RootCall::kComputeUav, i, a});
    g_origCUav(cl, i, a);
}
void STDMETHODCALLTYPE HookDraw(ID3D12GraphicsCommandList* cl, UINT n, UINT inst, UINT first, UINT firstInst) {
    {
        const InList in;
        if (in.outer && !t_ours && g_sinks.fxDraw) g_sinks.fxDraw(cl, n, inst);
    }
    g_origDraw(cl, n, inst, first, firstInst);
}

void STDMETHODCALLTYPE HookViewports(ID3D12GraphicsCommandList* cl, UINT n, const D3D12_VIEWPORT* vp) {
    const InList in;
    if (in.outer && !t_ours && g_sinks.viewport && n && vp) g_sinks.viewport(cl, vp[0].Width, vp[0].Height);
    if (in.outer && !t_ours && g_sinks.viewports && n && vp) g_sinks.viewports(cl, n, vp);
    g_origViewports(cl, n, vp);
}

// The device's views and, from a list of our own made on it, the lists' functions (every graphics list shares them). Once.
void HookDevice(ID3D12Device* dev, const char* when) {
    if (!dev || g_deviceHooked.exchange(true)) return;
    void** vt = *(void***)dev;
    // ID3D12Device: IUnknown 0-2, ID3D12Object 3-6, GetNodeCount 7, ... CreateCommandList 12, ... CreateRenderTargetView 20, CreateDepthStencilView 21
    bool ok = HookOne(vt[21], (void*)&HookCreateDsv, (void**)&g_origCreateDsv, "ID3D12Device::CreateDepthStencilView");
    ok &= HookOne(vt[20], (void*)&HookCreateRtv, (void**)&g_origCreateRtv, "ID3D12Device::CreateRenderTargetView");
    // ... CreateShaderResourceView 18 ([fog] game_fog_before_fx: which scene the game's resolve reads - only looked at while asked)
    ok &= HookOne(vt[18], (void*)&HookCreateSrv, (void**)&g_origCreateSrv, "ID3D12Device::CreateShaderResourceView");
    ok &= HookOne(vt[12], (void*)&HookCreateList, (void**)&g_origCreateList, "ID3D12Device::CreateCommandList");
    // ... CreateSampler 22, CopyDescriptors 23, CopyDescriptorsSimple 24
    ok &= HookOne(vt[23], (void*)&HookCopyDesc, (void**)&g_origCopyDesc, "ID3D12Device::CopyDescriptors");
    ok &= HookOne(vt[24], (void*)&HookCopyDescSimple, (void**)&g_origCopyDescSimple, "ID3D12Device::CopyDescriptorsSimple");
    // ... CreateGraphicsPipelineState 10; ID3D12Device2::CreatePipelineState 47 ([fog] game_fog_height: the game's fog pass's pipeline)
    if (g_sinks.graphicsPso) ok &= HookOne(vt[10], (void*)&HookCreateGpso, (void**)&g_origCreateGpso, "ID3D12Device::CreateGraphicsPipelineState");
    if (g_sinks.streamPso) {
        ID3D12Device2* d2 = nullptr;
        if (SUCCEEDED(dev->QueryInterface(__uuidof(ID3D12Device2), (void**)&d2)) && d2) {
            ok &= HookOne((*(void***)d2)[47], (void*)&HookCreatePso, (void**)&g_origCreatePso, "ID3D12Device2::CreatePipelineState");
            d2->Release();
        }
    }
    g_rtvInc.store(dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV));
    g_dsvInc.store(dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV));
    g_srvInc.store(dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));
    ID3D12CommandAllocator* a = nullptr;
    ID3D12GraphicsCommandList* l = nullptr;
    if (SUCCEEDED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&a)) &&
        SUCCEEDED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, a, nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&l))) {
        void** lt = *(void***)l;
        // ID3D12GraphicsCommandList: IUnknown 0-2, ID3D12Object 3-6, GetDevice 7, GetType 8, Close 9, Reset 10, ... RSSetViewports 21,
        // RSSetScissorRects 22, ResourceBarrier 26, ... OMSetRenderTargets 46, ClearDepthStencilView 47, ClearRenderTargetView 48
        ok &= HookOne(lt[10], (void*)&HookReset, (void**)&g_origReset, "ID3D12GraphicsCommandList::Reset");
        ok &= HookOne(lt[13], (void*)&HookDrawIndexed, (void**)&g_origDrawIndexed, "ID3D12GraphicsCommandList::DrawIndexedInstanced");
        if (g_sinks.psoSwap || g_sinks.fxBound) ok &= HookOne(lt[25], (void*)&HookSetPso, (void**)&g_origSetPso, "ID3D12GraphicsCommandList::SetPipelineState");
        // [fog] game_fog_before_fx: DrawInstanced 12, SetDescriptorHeaps 28, SetComputeRootSignature 29, SetComputeRootDescriptorTable 31,
        // SetGraphicsRootSignature 30, SetGraphicsRootDescriptorTable 32, SetComputeRoot32BitConstant 33, ...Constants 35, SetComputeRootConstantBufferView 37,
        // ...ShaderResourceView 39, ...UnorderedAccessView 41
        if (g_sinks.fxRoot) {
            ok &= HookOne(lt[12], (void*)&HookDraw, (void**)&g_origDraw, "ID3D12GraphicsCommandList::DrawInstanced");
            ok &= HookOne(lt[28], (void*)&HookHeaps, (void**)&g_origHeaps, "ID3D12GraphicsCommandList::SetDescriptorHeaps");
            ok &= HookOne(lt[29], (void*)&HookCRootSig, (void**)&g_origCRootSig, "ID3D12GraphicsCommandList::SetComputeRootSignature");
            ok &= HookOne(lt[30], (void*)&HookGRootSig, (void**)&g_origGRootSig, "ID3D12GraphicsCommandList::SetGraphicsRootSignature");
            ok &= HookOne(lt[31], (void*)&HookCTable, (void**)&g_origCTable, "ID3D12GraphicsCommandList::SetComputeRootDescriptorTable");
            ok &= HookOne(lt[32], (void*)&HookGTable, (void**)&g_origGTable, "ID3D12GraphicsCommandList::SetGraphicsRootDescriptorTable");
            ok &= HookOne(lt[33], (void*)&HookCConst, (void**)&g_origCConst, "ID3D12GraphicsCommandList::SetComputeRoot32BitConstant");
            ok &= HookOne(lt[35], (void*)&HookCConsts, (void**)&g_origCConsts, "ID3D12GraphicsCommandList::SetComputeRoot32BitConstants");
            ok &= HookOne(lt[37], (void*)&HookCCbv, (void**)&g_origCCbv, "ID3D12GraphicsCommandList::SetComputeRootConstantBufferView");
            ok &= HookOne(lt[39], (void*)&HookCSrv, (void**)&g_origCSrv, "ID3D12GraphicsCommandList::SetComputeRootShaderResourceView");
            ok &= HookOne(lt[41], (void*)&HookCUav, (void**)&g_origCUav, "ID3D12GraphicsCommandList::SetComputeRootUnorderedAccessView");
        }
        ok &= HookOne(lt[21], (void*)&HookViewports, (void**)&g_origViewports, "ID3D12GraphicsCommandList::RSSetViewports");
        ok &= HookOne(lt[22], (void*)&HookScissors, (void**)&g_origScissors, "ID3D12GraphicsCommandList::RSSetScissorRects");
        ok &= HookOne(lt[48], (void*)&HookClearRtv, (void**)&g_origClearRtv, "ID3D12GraphicsCommandList::ClearRenderTargetView");
        ok &= HookOne(lt[26], (void*)&HookBarrier, (void**)&g_origBarrier, "ID3D12GraphicsCommandList::ResourceBarrier");
        ok &= HookOne(lt[46], (void*)&HookOmSet, (void**)&g_origOmSet, "ID3D12GraphicsCommandList::OMSetRenderTargets");
        l->Close();
    } else {
        ok = false;
        LogF("vrcam: no ReShade - no command list could be made on the game's device: the scene's depth is not seen");
    }
    if (l) l->Release();
    if (a) a->Release();
    LogF("vrcam: no ReShade - the game's device %p hooked %s: depth-stencil views, render targets, barriers, viewports%s", (void*)dev, when,
         ok ? "" : " - NOT all (above)");
}
HRESULT WINAPI HookCreateDevice(IUnknown* adapter, D3D_FEATURE_LEVEL level, REFIID riid, void** out) {
    const HRESULT hr = g_origCreateDevice(adapter, level, riid, out);
    if (SUCCEEDED(hr) && out && *out) {
        ID3D12Device* d = nullptr;
        if (SUCCEEDED(((IUnknown*)*out)->QueryInterface(__uuidof(ID3D12Device), (void**)&d)) && d) {
            HookDevice(d, "as it was made");
            d->Release();
        }
    }
    return hr;
}

// ---- the swap chain
using CreateSwapChainFn = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
using CreateForHwndFn = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*,
                                                    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);
using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using Present1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
using ResizeFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using Resize1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT, const UINT*, IUnknown* const*);
CreateSwapChainFn g_origCreateSwapChain = nullptr;
CreateForHwndFn g_origCreateForHwnd = nullptr;
PresentFn g_origPresent = nullptr;
Present1Fn g_origPresent1 = nullptr;
ResizeFn g_origResize = nullptr;
Resize1Fn g_origResize1 = nullptr;
// The game's swap chain and its queue: raw pointers, no reference of ours (a swap chain we held would keep its window from the game's next
// one). Only compared, and used inside its own Present, while the game holds it.
std::atomic<IDXGISwapChain*> g_sc{nullptr};
std::atomic<ID3D12CommandQueue*> g_queue{nullptr};

void BeforePresent(IDXGISwapChain* sc, UINT flags) {
    if (t_present || (flags & DXGI_PRESENT_TEST) || sc != g_sc.load(std::memory_order_relaxed)) return;
    g_presents.fetch_add(1, std::memory_order_relaxed);
    if (!g_sinks.present) return;
    IDXGISwapChain3* sc3 = nullptr;
    if (FAILED(sc->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&sc3)) || !sc3) return;
    ++t_present;
    g_sinks.present(sc3, g_queue.load());
    --t_present;
    sc3->Release();
}
HRESULT STDMETHODCALLTYPE HookPresent(IDXGISwapChain* sc, UINT sync, UINT flags) {
    BeforePresent(sc, flags);
    ++t_present;
    const HRESULT hr = g_origPresent(sc, sync, flags);
    --t_present;
    return hr;
}
HRESULT STDMETHODCALLTYPE HookPresent1(IDXGISwapChain1* sc, UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS* p) {
    BeforePresent(sc, flags);
    ++t_present;
    const HRESULT hr = g_origPresent1(sc, sync, flags, p);
    --t_present;
    return hr;
}
void BeforeResize(IDXGISwapChain* sc) {
    if (sc == g_sc.load() && g_sinks.swapChainChanged) g_sinks.swapChainChanged();
}
HRESULT STDMETHODCALLTYPE HookResize(IDXGISwapChain* sc, UINT n, UINT w, UINT h, DXGI_FORMAT f, UINT flags) {
    BeforeResize(sc);
    return g_origResize(sc, n, w, h, f, flags);
}
HRESULT STDMETHODCALLTYPE HookResize1(IDXGISwapChain3* sc, UINT n, UINT w, UINT h, DXGI_FORMAT f, UINT flags, const UINT* mask, IUnknown* const* qs) {
    BeforeResize(sc);
    return g_origResize1(sc, n, w, h, f, flags, mask, qs);
}

// A swap chain made on a D3D12 queue (pDevice is the queue under D3D12): the game's - its functions hooked, it is the one told.
void Took(IUnknown* device, IDXGISwapChain* sc) {
    ID3D12CommandQueue* q = nullptr;
    if (!device || FAILED(device->QueryInterface(__uuidof(ID3D12CommandQueue), (void**)&q)) || !q) return;   // not D3D12
    void** vt = *(void***)sc;
    // IDXGISwapChain: IUnknown 0-2, IDXGIObject 3-6, GetDevice 7, Present 8, GetBuffer 9, ... ResizeBuffers 13;
    // IDXGISwapChain1: Present1 22; IDXGISwapChain3: ResizeBuffers1 39
    bool ok = HookOne(vt[8], (void*)&HookPresent, (void**)&g_origPresent, "IDXGISwapChain::Present");
    ok &= HookOne(vt[13], (void*)&HookResize, (void**)&g_origResize, "IDXGISwapChain::ResizeBuffers");
    IDXGISwapChain3* sc3 = nullptr;
    if (SUCCEEDED(sc->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&sc3)) && sc3) {
        void** v3 = *(void***)sc3;
        ok &= HookOne(v3[22], (void*)&HookPresent1, (void**)&g_origPresent1, "IDXGISwapChain1::Present1");
        ok &= HookOne(v3[39], (void*)&HookResize1, (void**)&g_origResize1, "IDXGISwapChain3::ResizeBuffers1");
        sc3->Release();
    }
    if (!g_deviceHooked.load()) {   // the device was made before the plugin's hook: its lists' functions from now on, its older views unknown
        ID3D12Device* dev = nullptr;
        if (SUCCEEDED(q->GetDevice(__uuidof(ID3D12Device), (void**)&dev)) && dev) {
            HookDevice(dev, "late, at its swap chain (the depth-stencil views made before are not known)");
            dev->Release();
        }
    }
    g_queue.store(q);
    q->Release();   // (the swap chain holds it)
    g_sc.store(sc);
    g_swapSeen.store(true);
    DXGI_SWAP_CHAIN_DESC d{};
    sc->GetDesc(&d);
    LogF("vrcam: no ReShade - the game's swap chain %p (%ux%u, %u buffers) on queue %p: its presents go to the headset from here%s", (void*)sc,
         d.BufferDesc.Width, d.BufferDesc.Height, d.BufferCount, (void*)q, ok ? "" : " - NOT all its hooks went in (above)");
    if (g_sinks.swapChainChanged) g_sinks.swapChainChanged();
}
HRESULT STDMETHODCALLTYPE HookCreateSwapChain(IDXGIFactory* f, IUnknown* dev, DXGI_SWAP_CHAIN_DESC* d, IDXGISwapChain** out) {
    ++t_create;
    const HRESULT hr = g_origCreateSwapChain(f, dev, d, out);
    --t_create;
    if (!t_create && SUCCEEDED(hr) && out && *out) Took(dev, *out);
    return hr;
}
HRESULT STDMETHODCALLTYPE HookCreateForHwnd(IDXGIFactory2* f, IUnknown* dev, HWND w, const DXGI_SWAP_CHAIN_DESC1* d,
                                            const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fs, IDXGIOutput* o, IDXGISwapChain1** out) {
    ++t_create;
    const HRESULT hr = g_origCreateForHwnd(f, dev, w, d, fs, o, out);
    --t_create;
    if (!t_create && SUCCEEDED(hr) && out && *out) Took(dev, *out);
    return hr;
}

// ---- our list on the game's queue
std::atomic<uint32_t> g_lists{0}, g_listWaits{0};   // TakeStats
std::atomic<uint64_t> g_listWaitUs{0};
// Our lists' allocators, each used again only when the fence has passed its last list. Deep: a present runs up to four lists of ours (the depth's
// copy, the effects, the overlays on each eye) - with three, the fourth waited for this present's copy, which runs after the game's whole frame
// on the queue: the CPU stood until the graphics card had drawn the frame, every present (PC2, 2026-10-26: native effects 1.4 ms on the card
// took 72 fps to 58, the frame's slack gone). Sixteen: four presents back.
struct Ring {
    static constexpr int kN = 16;
    ID3D12Device* dev = nullptr;
    ID3D12CommandAllocator* a[kN] = {};
    UINT64 done[kN] = {};
    ID3D12GraphicsCommandList* list = nullptr;
    ID3D12Fence* fence = nullptr;
    UINT64 v = 0;
    HANDLE ev = nullptr;
    int at = 0, open = -1;
    bool failed = false;
};
Ring g_ring;

}  // namespace

bool Install(const Sinks& sinks, LogFn log) {
    g_sinks = sinks;
    g_log = log;
    wchar_t sys[MAX_PATH];
    const UINT n = GetSystemDirectoryW(sys, MAX_PATH);
    if (!n || n >= MAX_PATH - 16) return false;
    // (the system's own DLLs by their full path: a dxgi.dll beside the game - another ReShade - is not this mode's, vrcam stopped before)
    const HMODULE d3d12 = LoadLibraryW((std::wstring(sys) + L"\\d3d12.dll").c_str());
    const HMODULE dxgi = LoadLibraryW((std::wstring(sys) + L"\\dxgi.dll").c_str());
    bool ok = true;
    void* create = d3d12 ? (void*)GetProcAddress(d3d12, "D3D12CreateDevice") : nullptr;
    ok &= HookOne(create, (void*)&HookCreateDevice, (void**)&g_origCreateDevice, "D3D12CreateDevice");
    using CreateFactory1Fn = HRESULT(WINAPI*)(REFIID, void**);
    const auto createFactory = dxgi ? (CreateFactory1Fn)GetProcAddress(dxgi, "CreateDXGIFactory1") : nullptr;
    IDXGIFactory2* f = nullptr;
    if (createFactory && SUCCEEDED(createFactory(__uuidof(IDXGIFactory2), (void**)&f)) && f) {
        void** vt = *(void***)f;
        // IDXGIFactory: IUnknown 0-2, IDXGIObject 3-6, EnumAdapters 7, MakeWindowAssociation 8, GetWindowAssociation 9, CreateSwapChain 10;
        // IDXGIFactory1 12-13; IDXGIFactory2: IsWindowedStereoEnabled 14, CreateSwapChainForHwnd 15
        ok &= HookOne(vt[10], (void*)&HookCreateSwapChain, (void**)&g_origCreateSwapChain, "IDXGIFactory::CreateSwapChain");
        ok &= HookOne(vt[15], (void*)&HookCreateForHwnd, (void**)&g_origCreateForHwnd, "IDXGIFactory2::CreateSwapChainForHwnd");
        f->Release();
    } else {
        ok = false;
        LogF("vrcam: no ReShade - no DXGI factory to hook: the game's presents are not seen");
    }
    LogF("vrcam: no ReShade - D3D12 and DXGI hooked by the plugin itself (D3D12CreateDevice, the swap chain's creation)%s", ok ? "" : " - NOT all (above)");
    return ok;
}

ID3D12Resource* RtvResource(uint64_t handle) {
    AcquireSRWLockShared(&g_rtvLock);
    const auto it = g_rtvs.find((SIZE_T)handle);
    ID3D12Resource* r = it != g_rtvs.end() ? it->second : nullptr;
    ReleaseSRWLockShared(&g_rtvLock);
    return r;
}
unsigned RtvIncrement() { return g_rtvInc.load(); }
void TrackSceneViews(bool on) { g_trackSrv.store(on); }
ID3D12Resource* SrvResource(uint64_t handle) { return SrvAt((SIZE_T)handle); }
unsigned SrvIncrement() { return g_srvInc.load(); }
ID3D12CommandQueue* GameQueue() { return g_queue.load(); }
void SetOurs(bool ours) { t_ours = ours; }
bool Ours() { return t_ours; }

void WatchIndexCount(uint32_t indexCount) { g_watchCount.store(indexCount, std::memory_order_relaxed); }
void TakeWatched(uint32_t* draws, uint32_t* instances) {
    *draws = g_watchDraws.exchange(0, std::memory_order_relaxed);
    *instances = g_watchInstances.exchange(0, std::memory_order_relaxed);
}

Stats TakeStats() {
    Stats s;
    s.presents = g_presents.exchange(0);
    s.depthBinds = g_depthBinds.exchange(0);
    s.unknownViews = g_unknownViews.exchange(0);
    AcquireSRWLockShared(&g_viewLock);
    s.views = (uint32_t)g_views.size();
    ReleaseSRWLockShared(&g_viewLock);
    s.device = g_deviceHooked.load();
    s.swapChain = g_swapSeen.load();
    s.lists = g_lists.exchange(0);
    s.listWaits = g_listWaits.exchange(0);
    s.listWaitMs = g_listWaitUs.exchange(0) / 1000.0;
    return s;
}

ID3D12GraphicsCommandList* OpenList(ID3D12Device* dev) {
    Ring& r = g_ring;
    if (!dev || r.failed) return nullptr;
    if (!r.dev) {
        r.dev = dev;
        bool ok = SUCCEEDED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&r.fence));
        for (int i = 0; ok && i < Ring::kN; ++i)
            ok = SUCCEEDED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&r.a[i]));
        ok = ok && SUCCEEDED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, r.a[0], nullptr, __uuidof(ID3D12GraphicsCommandList),
                                                    (void**)&r.list));
        r.ev = ok ? CreateEventW(nullptr, FALSE, FALSE, nullptr) : nullptr;
        if (!ok || !r.ev) {
            r.failed = true;
            LogF("vrcam: no ReShade - our command list on the game's queue could not be made: no depth for the eye made");
            return nullptr;
        }
        r.list->Close();
        r.list->SetName(L"d2rvr no-reshade depth copy list");   // (the names DRED prints for what the GPU was running)
        r.fence->SetName(L"d2rvr no-reshade fence");
    }
    if (dev != r.dev) return nullptr;   // (a second device: the game made it again - not this mode's case)
    const int i = r.at;
    if (r.done[i] && r.fence->GetCompletedValue() < r.done[i]) {
        LARGE_INTEGER t0, t1, hz;
        QueryPerformanceCounter(&t0);
        r.fence->SetEventOnCompletion(r.done[i], r.ev);
        const DWORD w = WaitForSingleObject(r.ev, 100);
        QueryPerformanceCounter(&t1);
        QueryPerformanceFrequency(&hz);
        g_listWaits.fetch_add(1, std::memory_order_relaxed);
        g_listWaitUs.fetch_add((uint64_t)((t1.QuadPart - t0.QuadPart) * 1000000 / hz.QuadPart), std::memory_order_relaxed);
        if (w != WAIT_OBJECT_0) return nullptr;   // the queue stands: the stall guard's, not ours
    }
    if (FAILED(r.a[i]->Reset()) || FAILED(r.list->Reset(r.a[i], nullptr))) return nullptr;
    r.open = i;
    t_ours = true;
    return r.list;
}

void Submit(ID3D12CommandQueue* q) {
    Ring& r = g_ring;
    t_ours = false;
    if (r.open < 0) return;
    const int i = r.open;
    r.open = -1;
    if (FAILED(r.list->Close()) || !q) return;
    ID3D12CommandList* l = r.list;
    q->ExecuteCommandLists(1, &l);
    q->Signal(r.fence, ++r.v);
    r.done[i] = r.v;
    g_lists.fetch_add(1, std::memory_order_relaxed);
    r.at = (i + 1) % Ring::kN;
}

}  // namespace natived3d
