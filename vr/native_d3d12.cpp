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

#include "sky_atlas.h"   // [sky] game_sky: the domes' picture as the game loads it (WatchDomeAtlas)
#include <vector>

#include "MinHook.h"
#include "native_d3d12.h"
#include "render/stereo/replay_list_gen.h"   // [stereo] native_replay: each list's resets, for the replay's check before it submits a list again

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

// The views' lookups on the lists' calls without a lock (docs/notes/vr_cpu_overhead.md): each recording thread keeps the last views it looked up,
// thrown away whenever any view of that type is made or copied over (the generation moves - the maps' writers bump it after their write). A
// lookup that misses takes the map's shared lock as before. Before this, one OMSetRenderTargets took g_viewLock up to five times (one per sink).
std::atomic<uint32_t> g_dsvGen{1}, g_rtvGen{1};
struct ViewCache {
    static constexpr int kN = 32;
    uint32_t gen = 0;
    SIZE_T key[kN] = {};
    ID3D12Resource* res[kN] = {};
    uint8_t state[kN] = {};   // 0 empty, 1 in the map (res: nullptr = the plugin's own), 2 not in the map
};
thread_local ViewCache t_dsvCache, t_rtvCache;
inline int CacheSlot(SIZE_T h) { return (int)(((uint64_t)h * 0x9E3779B97F4A7C15ull) >> 59); }   // (32 slots)
template <class Map>
ID3D12Resource* CachedView(ViewCache& c, const std::atomic<uint32_t>& gen, SRWLOCK* lock, const Map& map, SIZE_T h, bool* known) {
    const uint32_t g = gen.load(std::memory_order_acquire);
    if (c.gen != g) {
        c.gen = g;
        memset(c.state, 0, sizeof c.state);
    }
    const int i = CacheSlot(h);
    if (c.state[i] && c.key[i] == h) {
        if (known) *known = c.state[i] == 1;
        return c.res[i];
    }
    AcquireSRWLockShared(lock);
    const auto it = map.find(h);
    const bool in = it != map.end();
    ID3D12Resource* r = in ? it->second : nullptr;
    ReleaseSRWLockShared(lock);
    c.key[i] = h;
    c.res[i] = r;
    c.state[i] = in ? 1 : 2;
    if (known) *known = in;
    return r;
}
// A depth-stencil view's resource: nullptr = not known (`*known` false) or the plugin's own (true).
ID3D12Resource* DsvResource(SIZE_T h, bool* known) { return CachedView(t_dsvCache, g_dsvGen, &g_viewLock, g_views, h, known); }

// [debug] call_census (natived3d::SetCensus): the game's calls through these hooks, counted per recording thread - each thread its own slot
// (no shared counter on the way: a plain increment of its own), summed by the 10 s line. Off: one relaxed load a call.
std::atomic<bool> g_census{false};
#pragma warning(push)
#pragma warning(disable : 4324)   // (padded to its cache line on purpose: no two threads' slots share one)
struct alignas(64) CensusSlot {
    std::atomic<DWORD> tid{0};
    std::atomic<uint32_t> n[kCensusKinds] = {};
};
#pragma warning(pop)
constexpr int kCensusSlots = 64;
CensusSlot g_censusSlot[kCensusSlots];
std::atomic<int> g_censusUsed{0};
thread_local CensusSlot* t_census = nullptr;
thread_local bool t_censusFull = false;
inline void Count(int kind, uint32_t n = 1) {
    if (!g_census.load(std::memory_order_relaxed)) return;
    CensusSlot* s = t_census;
    if (!s) {
        if (t_censusFull) return;
        const int i = g_censusUsed.fetch_add(1);
        if (i >= kCensusSlots) { t_censusFull = true; return; }
        s = &g_censusSlot[i];
        s->tid.store(GetCurrentThreadId());
        t_census = s;
    }
    s->n[kind].store(s->n[kind].load(std::memory_order_relaxed) + n, std::memory_order_relaxed);   // (only this thread writes it)
}

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
    g_rtvGen.fetch_add(1, std::memory_order_release);   // (every thread's cached RTV lookups thrown away)
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
// [sky] game_sky: the game's views of the domes' picture (WatchDomeAtlas) - each texture's first view told, and how many it got. Serg on
// 0.192.8 (92.webp): our effect recoloured the dome after the game had blended the trees' edges into it - bright outlines round every tree
// at night; the game should draw the dome in its right colours itself. A view of the picture made by the game is where a texture of ours,
// baked by the time of day, could stand in. (Read only: the view is the game's.)
std::atomic<bool> g_watchAtlas{false};
std::atomic<AtlasStandInFn> g_atlasStandIn{nullptr};   // [sky] dome_in_game_colour: ours made in the game's view's place (SetAtlasStandIn)
bool IsAtlas(ID3D12Resource* r) {
    const D3D12_RESOURCE_DESC rd = r->GetDesc();
    return rd.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && skyatlas::IsAtlasTexture(rd.Width, rd.Height, (uint32_t)rd.Format);
}
void NoteAtlasView(ID3D12Resource* r, const D3D12_SHADER_RESOURCE_VIEW_DESC* desc, D3D12_CPU_DESCRIPTOR_HANDLE h) {
    const D3D12_RESOURCE_DESC rd = r->GetDesc();
    static std::mutex m;
    static ID3D12Resource* seen[8] = {};
    static unsigned views[8] = {};
    std::lock_guard<std::mutex> lock(m);
    int i = 0;
    while (i < 8 && seen[i] && seen[i] != r) ++i;
    if (i == 8) return;
    const bool first = !seen[i];
    seen[i] = r;
    ++views[i];
    if (first || views[i] == 2 || views[i] == 10)
        LogF("vrcam: game sky - the game made %s view of the domes' picture: texture %p %llux%u, format %u, %u mips, heap %s; the view %s, "
             "format %u, mips %u from %u, in slot %zx%s", first ? "a" : views[i] == 2 ? "a second" : "a tenth", (void*)r,
             (unsigned long long)rd.Width, rd.Height, (unsigned)rd.Format, (unsigned)rd.MipLevels,
             i ? "(another texture of it: loaded again)" : "", desc ? "described" : "with no description (the texture's own)",
             desc ? (unsigned)desc->Format : (unsigned)rd.Format, desc && desc->ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2D ? desc->Texture2D.MipLevels : 0u,
             desc && desc->ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2D ? desc->Texture2D.MostDetailedMip : 0u, (size_t)h.ptr,
             first ? " - where a texture of ours could stand in for it ([sky] game_sky, the dome's own colours by the time of day)" : "");
}
void STDMETHODCALLTYPE HookCreateSrv(ID3D12Device* d, ID3D12Resource* r, const D3D12_SHADER_RESOURCE_VIEW_DESC* desc, D3D12_CPU_DESCRIPTOR_HANDLE h) {
    const bool atlas = r && !t_ours && g_watchAtlas.load(std::memory_order_relaxed) && IsAtlas(r);
    if (atlas) NoteAtlasView(r, desc, h);
    const AtlasStandInFn standIn = atlas ? g_atlasStandIn.load() : nullptr;
    if (!standIn || !standIn(d, r, desc, h, g_origCreateSrv)) g_origCreateSrv(d, r, desc, h);
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
    (rtv ? g_rtvGen : g_dsvGen).fetch_add(1, std::memory_order_release);
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
    if (SUCCEEDED(hr) && out && *out) {   // [stereo] replay_reset_guard: a list made where a freed one was (D3D12 gives the address again) is a new recording too
        ID3D12GraphicsCommandList* l = nullptr;
        if (SUCCEEDED(((IUnknown*)*out)->QueryInterface(__uuidof(ID3D12GraphicsCommandList), (void**)&l)) && l) {
            listgen::Lists().NoteReset(l);
            l->Release();
        }
    }
    if (SUCCEEDED(hr) && out && *out && !t_ours && (g_sinks.listReset || g_sinks.fxReset || g_sinks.vrsReset) && type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
        ID3D12GraphicsCommandList* l = nullptr;
        if (SUCCEEDED(((IUnknown*)*out)->QueryInterface(__uuidof(ID3D12GraphicsCommandList), (void**)&l)) && l) {
            if (g_sinks.vrsReset) g_sinks.vrsReset(l);
            if (g_sinks.listReset) g_sinks.listReset(l);
            if (g_sinks.fxReset) g_sinks.fxReset(l);
            l->Release();
        }
    }
    if (SUCCEEDED(hr) && out && *out && !t_ours && g_sinks.soReset) {   // [render] shadows_once_a_pair: every type (a copy list may copy into the maps)
        ID3D12GraphicsCommandList* l = nullptr;
        if (SUCCEEDED(((IUnknown*)*out)->QueryInterface(__uuidof(ID3D12GraphicsCommandList), (void**)&l)) && l) {
            g_sinks.soReset(l);
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
    g_dsvGen.fetch_add(1, std::memory_order_release);
}

// ---- [stereo] native_replay: the game's CPU-visible buffers (Sinks::uploadMade / uploadGone)
// Every way a resource is made that can land in an upload heap: CreateCommittedResource (27) and CreatePlacedResource (29); Device4's
// CreateCommittedResource1 (53); Device8's CreateCommittedResource2 (69) and CreatePlacedResource1 (70); Device10's CreateCommittedResource3 (76)
// and CreatePlacedResource2 (77) - the indices counted in d3d12.h's own vtables (each with the #if twins of GetResourceAllocationInfo,
// GetCustomHeapProperties and GetAdapterLuid taken once). Only the outermost of nested ones tells (the runtime may make one through another, the
// debug layer forwards to the runtime's). The descriptions are not read (only the made resource's own GetDesc / GetHeapProperties): the
// later versions' parameters stay untyped here.
std::atomic<uint32_t> g_uploadsMade{0}, g_uploadsGone{0};
thread_local int t_making = 0;
// Let go by D3D12 when the resource is destroyed (SetPrivateDataInterface keeps a reference): the resource's pointer told gone.
class GoneWatch final : public IUnknown {
public:
    explicit GoneWatch(ID3D12Resource* r) : r_(r) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        if (riid == __uuidof(IUnknown)) { *out = this; AddRef(); return S_OK; }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const long n = --refs_;
        if (n == 0) {
            g_uploadsGone.fetch_add(1, std::memory_order_relaxed);
            if (g_sinks.uploadGone) g_sinks.uploadGone(r_);
            delete this;
        }
        return (ULONG)n;
    }
private:
    std::atomic<long> refs_{1};
    ID3D12Resource* r_;
};
const GUID kGoneWatch = {0x7d1c3e52, 0x5b0a, 0x4f6e, {0x9a, 0x41, 0x2d, 0x8e, 0x63, 0xb7, 0x0c, 0x19}};   // d2rvr: upload buffer watched
void MadeResource(REFIID riid, void** out) {
    if (!g_sinks.uploadMade || !out || !*out) return;
    ID3D12Resource* r = nullptr;
    if (FAILED(((IUnknown*)*out)->QueryInterface(__uuidof(ID3D12Resource), (void**)&r)) || !r) return;
    (void)riid;
    const D3D12_RESOURCE_DESC d = r->GetDesc();
    D3D12_HEAP_PROPERTIES hp{};
    D3D12_HEAP_FLAGS hf{};
    bool cpu = false;
    if (d.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER && SUCCEEDED(r->GetHeapProperties(&hp, &hf)))
        cpu = hp.Type == D3D12_HEAP_TYPE_UPLOAD ||
              (hp.Type == D3D12_HEAP_TYPE_CUSTOM && (hp.CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE ||
                                                      hp.CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_WRITE_BACK));
    uint8_t* p = nullptr;
    const D3D12_RANGE none{0, 0};
    if (cpu && SUCCEEDED(r->Map(0, &none, (void**)&p)) && p) {
        GoneWatch* w = new GoneWatch(r);
        if (SUCCEEDED(r->SetPrivateDataInterface(kGoneWatch, w))) {   // (it holds its own reference now)
            g_uploadsMade.fetch_add(1, std::memory_order_relaxed);
            g_sinks.uploadMade(r, r->GetGPUVirtualAddress(), d.Width, p);
        }
        w->Release();
    }
    r->Release();
}
using CreateCommittedFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*, const D3D12_HEAP_PROPERTIES*, D3D12_HEAP_FLAGS, const D3D12_RESOURCE_DESC*,
                                                      D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*, REFIID, void**);
using CreatePlacedFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*, ID3D12Heap*, UINT64, const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES,
                                                   const D3D12_CLEAR_VALUE*, REFIID, void**);
// (the later versions, untyped: heap properties / heap, flags or offset, description, state or layout, clear value, then their own extras)
using CreateCommitted1Fn = HRESULT(STDMETHODCALLTYPE*)(void*, const void*, UINT, const void*, UINT, const void*, void*, REFIID, void**);
using CreatePlaced1Fn = HRESULT(STDMETHODCALLTYPE*)(void*, void*, UINT64, const void*, UINT, const void*, REFIID, void**);
using CreateCommitted3Fn = HRESULT(STDMETHODCALLTYPE*)(void*, const void*, UINT, const void*, UINT, const void*, void*, UINT32, const void*, REFIID, void**);
using CreatePlaced2Fn = HRESULT(STDMETHODCALLTYPE*)(void*, void*, UINT64, const void*, UINT, const void*, UINT32, const void*, REFIID, void**);
CreateCommittedFn g_origCommitted = nullptr;
CreatePlacedFn g_origPlaced = nullptr;
CreateCommitted1Fn g_origCommitted1 = nullptr, g_origCommitted2 = nullptr;
CreatePlaced1Fn g_origPlaced1 = nullptr;
CreateCommitted3Fn g_origCommitted3 = nullptr;
CreatePlaced2Fn g_origPlaced2 = nullptr;
struct Making { Making() { ++t_making; } ~Making() { --t_making; } bool Outer() const { return t_making == 1; } };
HRESULT STDMETHODCALLTYPE HookCommitted(ID3D12Device* d, const D3D12_HEAP_PROPERTIES* hp, D3D12_HEAP_FLAGS f, const D3D12_RESOURCE_DESC* desc,
                                        D3D12_RESOURCE_STATES s, const D3D12_CLEAR_VALUE* c, REFIID riid, void** out) {
    const Making m;
    const HRESULT hr = g_origCommitted(d, hp, f, desc, s, c, riid, out);
    if (SUCCEEDED(hr) && m.Outer()) MadeResource(riid, out);
    return hr;
}
HRESULT STDMETHODCALLTYPE HookPlaced(ID3D12Device* d, ID3D12Heap* h, UINT64 at, const D3D12_RESOURCE_DESC* desc, D3D12_RESOURCE_STATES s,
                                     const D3D12_CLEAR_VALUE* c, REFIID riid, void** out) {
    const Making m;
    const HRESULT hr = g_origPlaced(d, h, at, desc, s, c, riid, out);
    if (SUCCEEDED(hr) && m.Outer()) MadeResource(riid, out);
    return hr;
}
HRESULT STDMETHODCALLTYPE HookCommitted1(void* d, const void* hp, UINT f, const void* desc, UINT s, const void* c, void* ps, REFIID riid, void** out) {
    const Making m;
    const HRESULT hr = g_origCommitted1(d, hp, f, desc, s, c, ps, riid, out);
    if (SUCCEEDED(hr) && m.Outer()) MadeResource(riid, out);
    return hr;
}
HRESULT STDMETHODCALLTYPE HookCommitted2(void* d, const void* hp, UINT f, const void* desc, UINT s, const void* c, void* ps, REFIID riid, void** out) {
    const Making m;
    const HRESULT hr = g_origCommitted2(d, hp, f, desc, s, c, ps, riid, out);
    if (SUCCEEDED(hr) && m.Outer()) MadeResource(riid, out);
    return hr;
}
HRESULT STDMETHODCALLTYPE HookPlaced1(void* d, void* h, UINT64 at, const void* desc, UINT s, const void* c, REFIID riid, void** out) {
    const Making m;
    const HRESULT hr = g_origPlaced1(d, h, at, desc, s, c, riid, out);
    if (SUCCEEDED(hr) && m.Outer()) MadeResource(riid, out);
    return hr;
}
HRESULT STDMETHODCALLTYPE HookCommitted3(void* d, const void* hp, UINT f, const void* desc, UINT layout, const void* c, void* ps, UINT32 nf,
                                         const void* fmts, REFIID riid, void** out) {
    const Making m;
    const HRESULT hr = g_origCommitted3(d, hp, f, desc, layout, c, ps, nf, fmts, riid, out);
    if (SUCCEEDED(hr) && m.Outer()) MadeResource(riid, out);
    return hr;
}
HRESULT STDMETHODCALLTYPE HookPlaced2(void* d, void* h, UINT64 at, const void* desc, UINT layout, const void* c, UINT32 nf, const void* fmts,
                                      REFIID riid, void** out) {
    const Making m;
    const HRESULT hr = g_origPlaced2(d, h, at, desc, layout, c, nf, fmts, riid, out);
    if (SUCCEEDED(hr) && m.Outer()) MadeResource(riid, out);
    return hr;
}
// The device's resource creation, all versions it has (once, with the device's other hooks)
bool HookResourceCreation(ID3D12Device* dev) {
    void** vt = *(void***)dev;
    bool ok = HookOne(vt[27], (void*)&HookCommitted, (void**)&g_origCommitted, "ID3D12Device::CreateCommittedResource");
    ok &= HookOne(vt[29], (void*)&HookPlaced, (void**)&g_origPlaced, "ID3D12Device::CreatePlacedResource");
    int versions = 1;
    ID3D12Device4* d4 = nullptr;
    if (SUCCEEDED(dev->QueryInterface(__uuidof(ID3D12Device4), (void**)&d4)) && d4) {
        ok &= HookOne((*(void***)d4)[53], (void*)&HookCommitted1, (void**)&g_origCommitted1, "ID3D12Device4::CreateCommittedResource1");
        d4->Release();
        versions = 4;
    }
    ID3D12Device8* d8 = nullptr;
    if (SUCCEEDED(dev->QueryInterface(__uuidof(ID3D12Device8), (void**)&d8)) && d8) {
        ok &= HookOne((*(void***)d8)[69], (void*)&HookCommitted2, (void**)&g_origCommitted2, "ID3D12Device8::CreateCommittedResource2");
        ok &= HookOne((*(void***)d8)[70], (void*)&HookPlaced1, (void**)&g_origPlaced1, "ID3D12Device8::CreatePlacedResource1");
        d8->Release();
        versions = 8;
    }
    ID3D12Device10* d10 = nullptr;
    if (SUCCEEDED(dev->QueryInterface(__uuidof(ID3D12Device10), (void**)&d10)) && d10) {
        ok &= HookOne((*(void***)d10)[76], (void*)&HookCommitted3, (void**)&g_origCommitted3, "ID3D12Device10::CreateCommittedResource3");
        ok &= HookOne((*(void***)d10)[77], (void*)&HookPlaced2, (void**)&g_origPlaced2, "ID3D12Device10::CreatePlacedResource2");
        d10->Release();
        versions = 10;
    }
    LogF("vrcam: native replay - the game's CPU-visible buffers followed (resource creation hooked up to ID3D12Device%d)%s", versions,
         ok ? "" : " - NOT all (above)");
    return ok;
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

// [stereo] native_replay (vr/render/stereo/replay_list_gen.h, crash batch 2026-11-09): the compute and copy lists' Reset, counted for the replay.
// D3D12 gives each list type its own vtable; where the compute or copy list's Reset is another function than the direct one's, it gets this hook.
ResetFn g_origResetC = nullptr, g_origResetY = nullptr;
HRESULT STDMETHODCALLTYPE HookResetC(ID3D12GraphicsCommandList* cl, ID3D12CommandAllocator* a, ID3D12PipelineState* ps) {
    const HRESULT hr = g_origResetC(cl, a, ps);
    if (SUCCEEDED(hr)) listgen::Lists().NoteReset(cl);
    return hr;
}
HRESULT STDMETHODCALLTYPE HookResetY(ID3D12GraphicsCommandList* cl, ID3D12CommandAllocator* a, ID3D12PipelineState* ps) {
    const HRESULT hr = g_origResetY(cl, a, ps);
    if (SUCCEEDED(hr)) listgen::Lists().NoteReset(cl);
    return hr;
}
HRESULT STDMETHODCALLTYPE HookReset(ID3D12GraphicsCommandList* cl, ID3D12CommandAllocator* a, ID3D12PipelineState* ps) {
    const InList in;
    const HRESULT hr = g_origReset(cl, a, ps);
    if (SUCCEEDED(hr)) listgen::Lists().NoteReset(cl);   // (every Reset, outer or not: the replay compares counts, never adds them up)
    if (in.outer && !t_ours) Count(kCensusReset);
    if (in.outer && !t_ours && g_sinks.huntBound && SUCCEEDED(hr)) g_sinks.huntBound(cl, ps);   // (a list starts with its Reset's pipeline)
    if (in.outer && !t_ours && g_sinks.vrsReset && SUCCEEDED(hr)) g_sinks.vrsReset(cl);   // [render] foveated: a new recording, no rate image
    if (in.outer && !t_ours && g_sinks.listReset && SUCCEEDED(hr)) g_sinks.listReset(cl);
    if (in.outer && !t_ours && g_sinks.fxReset && SUCCEEDED(hr)) g_sinks.fxReset(cl);
    if (in.outer && !t_ours && g_sinks.soReset && SUCCEEDED(hr)) g_sinks.soReset(cl);
    return hr;
}
void STDMETHODCALLTYPE HookScissors(ID3D12GraphicsCommandList* cl, UINT n, const D3D12_RECT* r) {
    const InList in;
    if (in.outer && !t_ours) Count(kCensusScissors);
    if (in.outer && !t_ours && g_sinks.scissors && n && r) g_sinks.scissors(cl, n, r);
    g_origScissors(cl, n, r);
}
// (before the clear, as ReShade's event: the interface's layer is learnt from its frame-start clear)
void STDMETHODCALLTYPE HookClearRtv(ID3D12GraphicsCommandList* cl, D3D12_CPU_DESCRIPTOR_HANDLE h, const FLOAT* c, UINT n, const D3D12_RECT* r) {
    const InList in;
    if (in.outer && !t_ours) Count(kCensusClearRtv);
    if (in.outer && !t_ours && g_sinks.clearRtv && c) g_sinks.clearRtv(cl, &h, c, n, r);
    if (in.outer && !t_ours && g_sinks.soWrite) g_sinks.soWrite(cl, RtvResource((uint64_t)h.ptr));
    g_origClearRtv(cl, h, c, n, r);
}

// One hook, two users: [render] shadows_once_a_pair is told the write BEFORE the clear (soWrite: into what), [render] lens_mask AFTER it (the sink
// records the hidden area's clear right behind the game's)
using ClearDsvFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_CLEAR_FLAGS, FLOAT, UINT8, UINT, const D3D12_RECT*);
ClearDsvFn g_origClearDsv = nullptr;
void STDMETHODCALLTYPE HookClearDsv(ID3D12GraphicsCommandList* cl, D3D12_CPU_DESCRIPTOR_HANDLE h, D3D12_CLEAR_FLAGS f, FLOAT depth, UINT8 stencil, UINT n,
                                    const D3D12_RECT* r) {
    const InList in;
    const bool game = in.outer && !t_ours;
    bool known = false;
    ID3D12Resource* ds = game && (g_sinks.soWrite || g_sinks.clearDsv) ? DsvResource(h.ptr, &known) : nullptr;   // (one lookup for both)
    if (game) Count(kCensusClearDsv);
    if (game && g_sinks.soWrite) g_sinks.soWrite(cl, ds);
    g_origClearDsv(cl, h, f, depth, stencil, n, r);
    if (!game || !g_sinks.clearDsv) return;
    if (known && !ds) return;   // the plugin's own depth-stencil
    g_sinks.clearDsv(cl, (uint64_t)h.ptr, ds, (unsigned)f, depth, stencil, n, r);
}

void STDMETHODCALLTYPE HookOmSet(ID3D12GraphicsCommandList* cl, UINT n, const D3D12_CPU_DESCRIPTOR_HANDLE* rts, BOOL single,
                                 const D3D12_CPU_DESCRIPTOR_HANDLE* ds) {
    const InList in;
    if (!in.outer || t_ours) { g_origOmSet(cl, n, rts, single, ds); return; }
    Count(kCensusTargets);
    // the depth-stencil's resource looked up once for every sink (nullptr: none bound, a view not seen made, or the plugin's own)
    bool dsKnown = false;
    ID3D12Resource* d = ds ? DsvResource(ds->ptr, &dsKnown) : nullptr;
    if (g_sinks.vrsTargets)   // [render] foveated: first - the rate image taken off before anything else records here
        g_sinks.vrsTargets(cl, n && rts ? RtvResource((uint64_t)rts[0].ptr) : nullptr, d, n);
    if (g_sinks.fxTargets) g_sinks.fxTargets(cl, n, rts, single != FALSE, d, ds);
    if (g_sinks.bindDepth) {
        bool tell = true;
        if (ds) {
            if (dsKnown && !d) tell = false;   // the plugin's own depth-stencil
            else if (!dsKnown) g_unknownViews.fetch_add(1, std::memory_order_relaxed);
            else g_depthBinds.fetch_add(1, std::memory_order_relaxed);
        }
        if (tell) g_sinks.bindDepth(cl, d, n);
    }
    if (g_sinks.targets) g_sinks.targets(cl, n, rts, single != FALSE);
    if (g_sinks.soTargets) {   // [render] shadows_once_a_pair: each colour target's resource and the depth-stencil's
        ID3D12Resource* rs[8] = {};
        bool unknown = n > 8;
        const unsigned inc = g_rtvInc.load(std::memory_order_relaxed);
        for (unsigned i = 0; i < n && i < 8; ++i) {
            const uint64_t h = rts ? (single ? (uint64_t)rts[0].ptr + (uint64_t)i * inc : (uint64_t)rts[i].ptr) : 0;
            rs[i] = h ? RtvResource(h) : nullptr;
            if (!rs[i]) unknown = true;
        }
        if (ds && !d) unknown = true;   // (not seen made, or the plugin's own)
        g_sinks.soTargets(cl, n < 8 ? n : 8, rs, d, unknown);
    }
    if (g_sinks.huntTargets)   // [debug] patch_hunt / capture: the pipe trace's columns (the sink looks only while it records)
        g_sinks.huntTargets(cl, n, rts, single != FALSE, d);
    g_origOmSet(cl, n, rts, single, ds);
}
void STDMETHODCALLTYPE HookBarrier(ID3D12GraphicsCommandList* cl, UINT n, const D3D12_RESOURCE_BARRIER* b) {
    const InList in;
    if (in.outer && !t_ours) { Count(kCensusBarrierCalls); Count(kCensusBarriers, n); }
    if (in.outer && !t_ours && g_sinks.captureBarrier && n && b) g_sinks.captureBarrier(cl, n, b);   // (the capture's targets: copied before the game's barrier)
    if (in.outer && !t_ours && g_sinks.barrier && n && b) g_sinks.barrier(cl, n, b);
    if (in.outer && !t_ours && g_sinks.fxBarrier && n && b) g_sinks.fxBarrier(cl, n, b);
    if (in.outer && !t_ours && g_sinks.soBarrier && n && b) g_sinks.soBarrier(cl, n, b);
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
        if (in.outer && !t_ours) Count(kCensusDrawIndexed);
        if (in.outer && !t_ours && g_sinks.huntSkip && g_sinks.huntSkip(cl, 0)) return;   // [debug] skip_pipeline / patch_hunt 8
        if (in.outer && !t_ours && g_sinks.vrsDraw) g_sinks.vrsDraw(cl);   // [render] foveated (before the fog step: the resolve's own rate)
        if (in.outer && !t_ours && g_sinks.fxDraw) g_sinks.fxDraw(cl, true, n, inst, first, base, firstInst);
        if (in.outer && !t_ours && g_sinks.soWork) g_sinks.soWork(cl, 0);
    }
    g_origDrawIndexed(cl, n, inst, first, base, firstInst);
}

// [fog] game_fog_height: the game's pipelines as they are made (the fog pass's is told to the sink) and bound (the sink may give another)
using CreateGpsoFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*, const D3D12_GRAPHICS_PIPELINE_STATE_DESC*, REFIID, void**);
using CreatePsoFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device2*, const D3D12_PIPELINE_STATE_STREAM_DESC*, REFIID, void**);
using SetPsoFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12PipelineState*);
using CreateCpsoFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*, const D3D12_COMPUTE_PIPELINE_STATE_DESC*, REFIID, void**);
CreateGpsoFn g_origCreateGpso = nullptr;
CreateCpsoFn g_origCreateCpso = nullptr;
CreatePsoFn g_origCreatePso = nullptr;
SetPsoFn g_origSetPso = nullptr;
HRESULT STDMETHODCALLTYPE HookCreateGpso(ID3D12Device* dev, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* d, REFIID riid, void** out) {
    const HRESULT hr = g_origCreateGpso(dev, d, riid, out);
    if (SUCCEEDED(hr) && out && *out && d && !t_ours && g_sinks.graphicsPso && riid == __uuidof(ID3D12PipelineState))
        g_sinks.graphicsPso(dev, d, (ID3D12PipelineState*)*out);
    if (SUCCEEDED(hr) && out && *out && d && !t_ours && g_sinks.vrsGraphicsPso && riid == __uuidof(ID3D12PipelineState))
        g_sinks.vrsGraphicsPso(d, (ID3D12PipelineState*)*out);
    return hr;
}
HRESULT STDMETHODCALLTYPE HookCreateCpso(ID3D12Device* dev, const D3D12_COMPUTE_PIPELINE_STATE_DESC* d, REFIID riid, void** out) {
    const HRESULT hr = g_origCreateCpso(dev, d, riid, out);
    if (SUCCEEDED(hr) && out && *out && d && !t_ours && g_sinks.computePso && riid == __uuidof(ID3D12PipelineState))
        g_sinks.computePso(dev, d, (ID3D12PipelineState*)*out);
    return hr;
}
HRESULT STDMETHODCALLTYPE HookCreatePso(ID3D12Device2* dev, const D3D12_PIPELINE_STATE_STREAM_DESC* d, REFIID riid, void** out) {
    const HRESULT hr = g_origCreatePso(dev, d, riid, out);
    if (SUCCEEDED(hr) && out && *out && d && !t_ours && g_sinks.streamPso && riid == __uuidof(ID3D12PipelineState))
        g_sinks.streamPso(dev, d, (ID3D12PipelineState*)*out);
    if (SUCCEEDED(hr) && out && *out && d && !t_ours && g_sinks.vrsStreamPso && riid == __uuidof(ID3D12PipelineState))
        g_sinks.vrsStreamPso(d, (ID3D12PipelineState*)*out);
    return hr;
}
void STDMETHODCALLTYPE HookSetPso(ID3D12GraphicsCommandList* cl, ID3D12PipelineState* pso) {
    const InList in;
    if (in.outer && !t_ours) Count(kCensusPso);
    if (in.outer && !t_ours && g_sinks.huntBound) g_sinks.huntBound(cl, pso);   // (the game's own, before a swap of ours)
    if (in.outer && !t_ours && g_sinks.vrsBound) g_sinks.vrsBound(cl, pso);     // [render] foveated: the game's own too
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
// camfix's handlers on these functions (SetRootChain; nullptr: none) - before the fog step's sink, on every call as camfix's own hooks were
std::atomic<void (*)(ID3D12GraphicsCommandList*, ID3D12RootSignature*, int)> g_chainRs{nullptr};
std::atomic<uint64_t (*)(ID3D12GraphicsCommandList*, unsigned, uint64_t, int)> g_chainCbv{nullptr};
void Root(ID3D12GraphicsCommandList* cl, const InList& in, RootCall c) {
    if (in.outer && !t_ours && g_sinks.fxRoot) g_sinks.fxRoot(cl, c);
}
void STDMETHODCALLTYPE HookHeaps(ID3D12GraphicsCommandList* cl, UINT n, ID3D12DescriptorHeap* const* h) {
    const InList in;
    if (in.outer && !t_ours && g_sinks.fxHeaps) g_sinks.fxHeaps(cl, n, h);
    g_origHeaps(cl, n, h);
}
void STDMETHODCALLTYPE HookCRootSig(ID3D12GraphicsCommandList* cl, ID3D12RootSignature* r) {
    if (const auto f = g_chainRs.load(std::memory_order_relaxed)) f(cl, r, 1);
    const InList in;
    Root(cl, in, {RootCall::kComputeRootSignature, 0, (uint64_t)(uintptr_t)r});
    g_origCRootSig(cl, r);
}
void STDMETHODCALLTYPE HookGRootSig(ID3D12GraphicsCommandList* cl, ID3D12RootSignature* r) {
    if (const auto f = g_chainRs.load(std::memory_order_relaxed)) f(cl, r, 0);
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
    if (const auto f = g_chainCbv.load(std::memory_order_relaxed))
        if (const uint64_t to = f(cl, i, a, 1)) a = to;
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
        if (in.outer && !t_ours) Count(kCensusDraw);
        if (in.outer && !t_ours && g_sinks.huntSkip && g_sinks.huntSkip(cl, 0)) return;   // [debug] skip_pipeline / patch_hunt 8
        if (in.outer && !t_ours && g_sinks.vrsDraw) g_sinks.vrsDraw(cl);   // [render] foveated
        if (in.outer && !t_ours && g_sinks.fxDraw) g_sinks.fxDraw(cl, false, n, inst, first, 0, firstInst);
        if (in.outer && !t_ours && g_sinks.soWork) g_sinks.soWork(cl, 0);
    }
    g_origDraw(cl, n, inst, first, firstInst);
}
// [debug] skip_pipeline / patch_hunt 8 (vr/pipe_hunt.h): the game's dispatches and indirect calls, left out while their pipeline is skipped
using DispatchFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, UINT, UINT);
using IndirectFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12CommandSignature*, UINT, ID3D12Resource*, UINT64, ID3D12Resource*, UINT64);
DispatchFn g_origDispatch = nullptr;
IndirectFn g_origIndirect = nullptr;
void STDMETHODCALLTYPE HookDispatch(ID3D12GraphicsCommandList* cl, UINT x, UINT y, UINT z) {
    {
        const InList in;
        if (in.outer && !t_ours) Count(kCensusDispatch);
        if (in.outer && !t_ours && g_sinks.huntSkip && g_sinks.huntSkip(cl, 1)) return;
        if (in.outer && !t_ours && g_sinks.soWork) g_sinks.soWork(cl, 1);
    }
    g_origDispatch(cl, x, y, z);
}
void STDMETHODCALLTYPE HookIndirect(ID3D12GraphicsCommandList* cl, ID3D12CommandSignature* sig, UINT n, ID3D12Resource* args, UINT64 argsAt,
                                    ID3D12Resource* count, UINT64 countAt) {
    {
        const InList in;
        if (in.outer && !t_ours) Count(kCensusIndirect);
        if (in.outer && !t_ours && g_sinks.huntSkip && g_sinks.huntSkip(cl, 2)) return;
        if (in.outer && !t_ours && g_sinks.vrsIndirect) g_sinks.vrsIndirect(cl);   // [render] foveated: an indirect call is not known - whole
        if (in.outer && !t_ours && g_sinks.fxIndirect) g_sinks.fxIndirect(cl);     // [fog] game_fog_before_fx: the hair / foliage targets
        if (in.outer && !t_ours && g_sinks.soWork) g_sinks.soWork(cl, 2);
    }
    g_origIndirect(cl, sig, n, args, argsAt, count, countAt);
}

// [render] shadows_once_a_pair (vr/shadows_once.h): the other ways a list writes - each told to soWrite (into what) or soWork (3: another kind),
// before the real call (inside its InList: the D3D12 debug layer's forward to the runtime's list is the same call, not told again)
using ClearUavUFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, D3D12_GPU_DESCRIPTOR_HANDLE, D3D12_CPU_DESCRIPTOR_HANDLE, ID3D12Resource*, const UINT*, UINT,
                                             const D3D12_RECT*);
using ClearUavFFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, D3D12_GPU_DESCRIPTOR_HANDLE, D3D12_CPU_DESCRIPTOR_HANDLE, ID3D12Resource*, const FLOAT*, UINT,
                                             const D3D12_RECT*);
using DiscardFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12Resource*, const D3D12_DISCARD_REGION*);
using CopyBufFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12Resource*, UINT64, ID3D12Resource*, UINT64, UINT64);
using CopyTexFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, const D3D12_TEXTURE_COPY_LOCATION*, UINT, UINT, UINT, const D3D12_TEXTURE_COPY_LOCATION*,
                                           const D3D12_BOX*);
using CopyResFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12Resource*, ID3D12Resource*);
using CopyTilesFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12Resource*, const D3D12_TILED_RESOURCE_COORDINATE*, const D3D12_TILE_REGION_SIZE*,
                                             ID3D12Resource*, UINT64, D3D12_TILE_COPY_FLAGS);
using ResolveFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12Resource*, UINT, ID3D12Resource*, UINT, DXGI_FORMAT);
using BundleFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12GraphicsCommandList*);
using AtomicCopyFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12Resource*, UINT64, ID3D12Resource*, UINT64, UINT, ID3D12Resource* const*,
                                              const D3D12_SUBRESOURCE_RANGE_UINT64*);
using ResolveRegionFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12Resource*, UINT, UINT, UINT, ID3D12Resource*, UINT, D3D12_RECT*, DXGI_FORMAT,
                                                 D3D12_RESOLVE_MODE);
using WriteImmFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER*, const D3D12_WRITEBUFFERIMMEDIATE_MODE*);
using RenderPassFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, const D3D12_RENDER_PASS_RENDER_TARGET_DESC*, const D3D12_RENDER_PASS_DEPTH_STENCIL_DESC*,
                                              D3D12_RENDER_PASS_FLAGS);
using MetaFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12MetaCommand*, const void*, SIZE_T);
using BuildAsFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC*, UINT,
                                           const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC*);
using CopyAsFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, D3D12_GPU_VIRTUAL_ADDRESS, D3D12_GPU_VIRTUAL_ADDRESS, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE);
using RaysFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, const D3D12_DISPATCH_RAYS_DESC*);
using MeshFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, UINT, UINT);
using Barrier7Fn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT32, const D3D12_BARRIER_GROUP*);
ClearUavUFn g_origClearUavU = nullptr;
ClearUavFFn g_origClearUavF = nullptr;
DiscardFn g_origDiscard = nullptr;
CopyBufFn g_origCopyBuf = nullptr;
CopyTexFn g_origCopyTex = nullptr;
CopyResFn g_origCopyRes = nullptr;
CopyTilesFn g_origCopyTiles = nullptr;
ResolveFn g_origResolve = nullptr;
BundleFn g_origBundle = nullptr;
AtomicCopyFn g_origAtomic32 = nullptr, g_origAtomic64 = nullptr;
ResolveRegionFn g_origResolveRegion = nullptr;
WriteImmFn g_origWriteImm = nullptr;
RenderPassFn g_origRenderPass = nullptr;
MetaFn g_origMeta = nullptr;
BuildAsFn g_origBuildAs = nullptr;
CopyAsFn g_origCopyAs = nullptr;
RaysFn g_origRays = nullptr;
MeshFn g_origMesh = nullptr;
Barrier7Fn g_origBarrier7 = nullptr;
void SoWrite(const InList& in, ID3D12GraphicsCommandList* cl, ID3D12Resource* dst) {
    if (in.outer && !t_ours && g_sinks.soWrite) g_sinks.soWrite(cl, dst);
}
void SoOther(const InList& in, ID3D12GraphicsCommandList* cl) {
    if (in.outer && !t_ours && g_sinks.soWork) g_sinks.soWork(cl, 3);
}
// (ClearDepthStencilView: the one HookClearDsv above, lens_mask's too)
void STDMETHODCALLTYPE HookClearUavU(ID3D12GraphicsCommandList* cl, D3D12_GPU_DESCRIPTOR_HANDLE g, D3D12_CPU_DESCRIPTOR_HANDLE c, ID3D12Resource* r, const UINT* v,
                                     UINT n, const D3D12_RECT* rc) {
    const InList in;
    SoWrite(in, cl, r);
    g_origClearUavU(cl, g, c, r, v, n, rc);
}
void STDMETHODCALLTYPE HookClearUavF(ID3D12GraphicsCommandList* cl, D3D12_GPU_DESCRIPTOR_HANDLE g, D3D12_CPU_DESCRIPTOR_HANDLE c, ID3D12Resource* r, const FLOAT* v,
                                     UINT n, const D3D12_RECT* rc) {
    const InList in;
    SoWrite(in, cl, r);
    g_origClearUavF(cl, g, c, r, v, n, rc);
}
void STDMETHODCALLTYPE HookDiscard(ID3D12GraphicsCommandList* cl, ID3D12Resource* r, const D3D12_DISCARD_REGION* reg) {
    const InList in;
    SoWrite(in, cl, r);
    g_origDiscard(cl, r, reg);
}
void STDMETHODCALLTYPE HookCopyBuf(ID3D12GraphicsCommandList* cl, ID3D12Resource* dst, UINT64 at, ID3D12Resource* src, UINT64 from, UINT64 n) {
    const InList in;
    SoWrite(in, cl, dst);
    g_origCopyBuf(cl, dst, at, src, from, n);
}
void STDMETHODCALLTYPE HookCopyTex(ID3D12GraphicsCommandList* cl, const D3D12_TEXTURE_COPY_LOCATION* dst, UINT x, UINT y, UINT z, const D3D12_TEXTURE_COPY_LOCATION* src,
                                   const D3D12_BOX* box) {
    const InList in;
    SoWrite(in, cl, dst ? dst->pResource : nullptr);
    g_origCopyTex(cl, dst, x, y, z, src, box);
}
void STDMETHODCALLTYPE HookCopyRes(ID3D12GraphicsCommandList* cl, ID3D12Resource* dst, ID3D12Resource* src) {
    const InList in;
    SoWrite(in, cl, dst);
    g_origCopyRes(cl, dst, src);
}
void STDMETHODCALLTYPE HookCopyTiles(ID3D12GraphicsCommandList* cl, ID3D12Resource* t, const D3D12_TILED_RESOURCE_COORDINATE* c, const D3D12_TILE_REGION_SIZE* s,
                                     ID3D12Resource* b, UINT64 at, D3D12_TILE_COPY_FLAGS f) {
    const InList in;
    SoOther(in, cl);
    g_origCopyTiles(cl, t, c, s, b, at, f);
}
void STDMETHODCALLTYPE HookResolve(ID3D12GraphicsCommandList* cl, ID3D12Resource* dst, UINT ds, ID3D12Resource* src, UINT ss, DXGI_FORMAT f) {
    const InList in;
    SoWrite(in, cl, dst);
    g_origResolve(cl, dst, ds, src, ss, f);
}
void STDMETHODCALLTYPE HookBundle(ID3D12GraphicsCommandList* cl, ID3D12GraphicsCommandList* b) {
    const InList in;
    SoOther(in, cl);
    g_origBundle(cl, b);
}
void STDMETHODCALLTYPE HookAtomic32(ID3D12GraphicsCommandList* cl, ID3D12Resource* d, UINT64 at, ID3D12Resource* s, UINT64 from, UINT n, ID3D12Resource* const* deps,
                                    const D3D12_SUBRESOURCE_RANGE_UINT64* r) {
    const InList in;
    SoOther(in, cl);
    g_origAtomic32(cl, d, at, s, from, n, deps, r);
}
void STDMETHODCALLTYPE HookAtomic64(ID3D12GraphicsCommandList* cl, ID3D12Resource* d, UINT64 at, ID3D12Resource* s, UINT64 from, UINT n, ID3D12Resource* const* deps,
                                    const D3D12_SUBRESOURCE_RANGE_UINT64* r) {
    const InList in;
    SoOther(in, cl);
    g_origAtomic64(cl, d, at, s, from, n, deps, r);
}
void STDMETHODCALLTYPE HookResolveRegion(ID3D12GraphicsCommandList* cl, ID3D12Resource* dst, UINT ds, UINT x, UINT y, ID3D12Resource* src, UINT ss, D3D12_RECT* r,
                                         DXGI_FORMAT f, D3D12_RESOLVE_MODE m) {
    const InList in;
    SoWrite(in, cl, dst);
    g_origResolveRegion(cl, dst, ds, x, y, src, ss, r, f, m);
}
void STDMETHODCALLTYPE HookWriteImm(ID3D12GraphicsCommandList* cl, UINT n, const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER* p, const D3D12_WRITEBUFFERIMMEDIATE_MODE* m) {
    const InList in;
    SoOther(in, cl);
    g_origWriteImm(cl, n, p, m);
}
void STDMETHODCALLTYPE HookRenderPass(ID3D12GraphicsCommandList* cl, UINT n, const D3D12_RENDER_PASS_RENDER_TARGET_DESC* rt, const D3D12_RENDER_PASS_DEPTH_STENCIL_DESC* ds,
                                      D3D12_RENDER_PASS_FLAGS f) {
    const InList in;
    SoOther(in, cl);
    g_origRenderPass(cl, n, rt, ds, f);
}
void STDMETHODCALLTYPE HookMeta(ID3D12GraphicsCommandList* cl, ID3D12MetaCommand* m, const void* p, SIZE_T n) {
    const InList in;
    SoOther(in, cl);
    g_origMeta(cl, m, p, n);
}
void STDMETHODCALLTYPE HookBuildAs(ID3D12GraphicsCommandList* cl, const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC* d, UINT n,
                                   const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC* p) {
    const InList in;
    SoOther(in, cl);
    g_origBuildAs(cl, d, n, p);
}
void STDMETHODCALLTYPE HookCopyAs(ID3D12GraphicsCommandList* cl, D3D12_GPU_VIRTUAL_ADDRESS d, D3D12_GPU_VIRTUAL_ADDRESS s, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE m) {
    const InList in;
    SoOther(in, cl);
    g_origCopyAs(cl, d, s, m);
}
void STDMETHODCALLTYPE HookRays(ID3D12GraphicsCommandList* cl, const D3D12_DISPATCH_RAYS_DESC* d) {
    const InList in;
    SoOther(in, cl);
    g_origRays(cl, d);
}
void STDMETHODCALLTYPE HookMesh(ID3D12GraphicsCommandList* cl, UINT x, UINT y, UINT z) {
    const InList in;
    SoOther(in, cl);
    g_origMesh(cl, x, y, z);
}
void STDMETHODCALLTYPE HookBarrier7(ID3D12GraphicsCommandList* cl, UINT32 n, const D3D12_BARRIER_GROUP* g) {
    const InList in;
    SoOther(in, cl);
    g_origBarrier7(cl, n, g);
}
// The hooks above on a list of ours (every list's functions are its type's; the later interfaces' only where the runtime has them)
bool HookShadowsOnce(ID3D12GraphicsCommandList* l, void** lt) {
    // ID3D12GraphicsCommandList: DrawInstanced 12, Dispatch 14, CopyBufferRegion 15, CopyTextureRegion 16, CopyResource 17, CopyTiles 18,
    // ResolveSubresource 19, ExecuteBundle 27, ClearDepthStencilView 47, ClearUnorderedAccessViewUint 49, ...Float 50, DiscardResource 51, ExecuteIndirect 59
    bool ok = HookOne(lt[12], (void*)&HookDraw, (void**)&g_origDraw, "ID3D12GraphicsCommandList::DrawInstanced");
    ok &= HookOne(lt[14], (void*)&HookDispatch, (void**)&g_origDispatch, "ID3D12GraphicsCommandList::Dispatch");
    ok &= HookOne(lt[59], (void*)&HookIndirect, (void**)&g_origIndirect, "ID3D12GraphicsCommandList::ExecuteIndirect");
    ok &= HookOne(lt[15], (void*)&HookCopyBuf, (void**)&g_origCopyBuf, "ID3D12GraphicsCommandList::CopyBufferRegion");
    ok &= HookOne(lt[16], (void*)&HookCopyTex, (void**)&g_origCopyTex, "ID3D12GraphicsCommandList::CopyTextureRegion");
    ok &= HookOne(lt[17], (void*)&HookCopyRes, (void**)&g_origCopyRes, "ID3D12GraphicsCommandList::CopyResource");
    ok &= HookOne(lt[18], (void*)&HookCopyTiles, (void**)&g_origCopyTiles, "ID3D12GraphicsCommandList::CopyTiles");
    ok &= HookOne(lt[19], (void*)&HookResolve, (void**)&g_origResolve, "ID3D12GraphicsCommandList::ResolveSubresource");
    ok &= HookOne(lt[27], (void*)&HookBundle, (void**)&g_origBundle, "ID3D12GraphicsCommandList::ExecuteBundle");
    ok &= HookOne(lt[47], (void*)&HookClearDsv, (void**)&g_origClearDsv, "ID3D12GraphicsCommandList::ClearDepthStencilView");
    ok &= HookOne(lt[49], (void*)&HookClearUavU, (void**)&g_origClearUavU, "ID3D12GraphicsCommandList::ClearUnorderedAccessViewUint");
    ok &= HookOne(lt[50], (void*)&HookClearUavF, (void**)&g_origClearUavF, "ID3D12GraphicsCommandList::ClearUnorderedAccessViewFloat");
    ok &= HookOne(lt[51], (void*)&HookDiscard, (void**)&g_origDiscard, "ID3D12GraphicsCommandList::DiscardResource");
    // List1: AtomicCopyBufferUINT 60, ...UINT64 61, ResolveSubresourceRegion 64; List2: WriteBufferImmediate 66; List4: BeginRenderPass 68,
    // ExecuteMetaCommand 71, BuildRaytracingAccelerationStructure 72, CopyRaytracingAccelerationStructure 74, DispatchRays 76; List6: DispatchMesh 79;
    // List7: Barrier 80
    auto has = [l](REFIID iid) {
        IUnknown* u = nullptr;
        if (FAILED(l->QueryInterface(iid, (void**)&u)) || !u) return false;
        u->Release();
        return true;
    };
    if (has(__uuidof(ID3D12GraphicsCommandList1))) {
        ok &= HookOne(lt[60], (void*)&HookAtomic32, (void**)&g_origAtomic32, "ID3D12GraphicsCommandList1::AtomicCopyBufferUINT");
        ok &= HookOne(lt[61], (void*)&HookAtomic64, (void**)&g_origAtomic64, "ID3D12GraphicsCommandList1::AtomicCopyBufferUINT64");
        ok &= HookOne(lt[64], (void*)&HookResolveRegion, (void**)&g_origResolveRegion, "ID3D12GraphicsCommandList1::ResolveSubresourceRegion");
    }
    if (has(__uuidof(ID3D12GraphicsCommandList2))) ok &= HookOne(lt[66], (void*)&HookWriteImm, (void**)&g_origWriteImm, "ID3D12GraphicsCommandList2::WriteBufferImmediate");
    if (has(__uuidof(ID3D12GraphicsCommandList4))) {
        ok &= HookOne(lt[68], (void*)&HookRenderPass, (void**)&g_origRenderPass, "ID3D12GraphicsCommandList4::BeginRenderPass");
        ok &= HookOne(lt[71], (void*)&HookMeta, (void**)&g_origMeta, "ID3D12GraphicsCommandList4::ExecuteMetaCommand");
        ok &= HookOne(lt[72], (void*)&HookBuildAs, (void**)&g_origBuildAs, "ID3D12GraphicsCommandList4::BuildRaytracingAccelerationStructure");
        ok &= HookOne(lt[74], (void*)&HookCopyAs, (void**)&g_origCopyAs, "ID3D12GraphicsCommandList4::CopyRaytracingAccelerationStructure");
        ok &= HookOne(lt[76], (void*)&HookRays, (void**)&g_origRays, "ID3D12GraphicsCommandList4::DispatchRays");
    }
    if (has(__uuidof(ID3D12GraphicsCommandList6))) ok &= HookOne(lt[79], (void*)&HookMesh, (void**)&g_origMesh, "ID3D12GraphicsCommandList6::DispatchMesh");
    if (has(__uuidof(ID3D12GraphicsCommandList7))) ok &= HookOne(lt[80], (void*)&HookBarrier7, (void**)&g_origBarrier7, "ID3D12GraphicsCommandList7::Barrier");
    return ok;
}

void STDMETHODCALLTYPE HookViewports(ID3D12GraphicsCommandList* cl, UINT n, const D3D12_VIEWPORT* vp) {
    const InList in;
    if (in.outer && !t_ours) Count(kCensusViewports);
    if (in.outer && !t_ours && g_sinks.vrsViewport && n && vp) g_sinks.vrsViewport(cl, vp);   // [render] foveated
    if (in.outer && !t_ours && g_sinks.viewport && n && vp) g_sinks.viewport(cl, vp[0].Width, vp[0].Height);
    if (in.outer && !t_ours && g_sinks.viewports && n && vp) g_sinks.viewports(cl, n, vp);
    if (in.outer && !t_ours && g_sinks.huntViewport && n && vp) g_sinks.huntViewport(cl, vp[0].Width, vp[0].Height);
    g_origViewports(cl, n, vp);
}

// [stereo] native_replay: a list of each other type made on the game's device, its Reset hooked unless it is the direct list's function (hooked
// already). Told in the log; a hook that does not go in leaves those lists uncounted (the replay then cannot see them reset - as before).
void HookOtherResets(ID3D12Device* dev, void* directReset) {
    const D3D12_COMMAND_LIST_TYPE types[2] = {D3D12_COMMAND_LIST_TYPE_COMPUTE, D3D12_COMMAND_LIST_TYPE_COPY};
    for (int k = 0; k < 2; ++k) {
        ID3D12CommandAllocator* a = nullptr;
        ID3D12GraphicsCommandList* l = nullptr;
        const char* what = k == 0 ? "compute" : "copy";
        if (SUCCEEDED(dev->CreateCommandAllocator(types[k], __uuidof(ID3D12CommandAllocator), (void**)&a)) &&
            SUCCEEDED(dev->CreateCommandList(0, types[k], a, nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&l))) {
            void* fn = (*(void***)l)[10];
            const bool same = fn == directReset;
            const bool ok = same || HookOne(fn, k == 0 ? (void*)&HookResetC : (void*)&HookResetY, k == 0 ? (void**)&g_origResetC : (void**)&g_origResetY,
                                            k == 0 ? "ID3D12GraphicsCommandList::Reset (compute lists)" : "ID3D12GraphicsCommandList::Reset (copy lists)");
            LogF("vrcam: native replay - the %s lists' resets counted for the replay's check (%s)", what,
                 same ? "the direct lists' Reset: one hook" : ok ? "their own Reset hooked" : "NOT: the hook did not go in");
            l->Close();
        } else {
            LogF("vrcam: native replay - no %s list could be made: its lists' resets are not counted", what);
        }
        if (l) l->Release();
        if (a) a->Release();
    }
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
    if (g_sinks.graphicsPso || g_sinks.vrsGraphicsPso) ok &= HookOne(vt[10], (void*)&HookCreateGpso, (void**)&g_origCreateGpso, "ID3D12Device::CreateGraphicsPipelineState");
    // ... CreateComputePipelineState 11 ([render] light_bounds_fix without ReShade: the game's light-bin depth-bounds pipeline)
    if (g_sinks.computePso) ok &= HookOne(vt[11], (void*)&HookCreateCpso, (void**)&g_origCreateCpso, "ID3D12Device::CreateComputePipelineState");
    if (g_sinks.streamPso || g_sinks.vrsStreamPso) {
        ID3D12Device2* d2 = nullptr;
        if (SUCCEEDED(dev->QueryInterface(__uuidof(ID3D12Device2), (void**)&d2)) && d2) {
            ok &= HookOne((*(void***)d2)[47], (void*)&HookCreatePso, (void**)&g_origCreatePso, "ID3D12Device2::CreatePipelineState");
            d2->Release();
        }
    }
    if (g_sinks.uploadMade) ok &= HookResourceCreation(dev);   // [stereo] native_replay: the camera's constants are read from the game's upload buffers
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
        if (g_sinks.psoSwap || g_sinks.fxBound || g_sinks.huntBound || g_sinks.vrsBound) ok &= HookOne(lt[25], (void*)&HookSetPso, (void**)&g_origSetPso, "ID3D12GraphicsCommandList::SetPipelineState");
        // [debug] skip_pipeline / patch_hunt 8: DrawInstanced 12, Dispatch 14, ExecuteIndirect 59 (HookDraw is [fog] game_fog_before_fx's too)
        if (g_sinks.huntSkip) {
            ok &= HookOne(lt[12], (void*)&HookDraw, (void**)&g_origDraw, "ID3D12GraphicsCommandList::DrawInstanced");
            ok &= HookOne(lt[14], (void*)&HookDispatch, (void**)&g_origDispatch, "ID3D12GraphicsCommandList::Dispatch");
            ok &= HookOne(lt[59], (void*)&HookIndirect, (void**)&g_origIndirect, "ID3D12GraphicsCommandList::ExecuteIndirect");
        }
        // [render] foveated: DrawInstanced 12 (each draw may take the rate image), ExecuteIndirect 59 (it is taken off before one)
        if (g_sinks.vrsDraw) ok &= HookOne(lt[12], (void*)&HookDraw, (void**)&g_origDraw, "ID3D12GraphicsCommandList::DrawInstanced");
        if (g_sinks.vrsIndirect) ok &= HookOne(lt[59], (void*)&HookIndirect, (void**)&g_origIndirect, "ID3D12GraphicsCommandList::ExecuteIndirect");
        if (g_sinks.fxIndirect) ok &= HookOne(lt[59], (void*)&HookIndirect, (void**)&g_origIndirect, "ID3D12GraphicsCommandList::ExecuteIndirect");
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
        // [render] lens_mask: ClearDepthStencilView 47 (the hidden area's clear recorded right after the game's scene depth clear)
        if (g_sinks.clearDsv) ok &= HookOne(lt[47], (void*)&HookClearDsv, (void**)&g_origClearDsv, "ID3D12GraphicsCommandList::ClearDepthStencilView");
        ok &= HookOne(lt[26], (void*)&HookBarrier, (void**)&g_origBarrier, "ID3D12GraphicsCommandList::ResourceBarrier");
        ok &= HookOne(lt[46], (void*)&HookOmSet, (void**)&g_origOmSet, "ID3D12GraphicsCommandList::OMSetRenderTargets");
        if (g_sinks.soReset) ok &= HookShadowsOnce(l, lt);   // [render] shadows_once_a_pair: everything else a list writes with
        if (g_sinks.uploadMade) HookOtherResets(dev, lt[10]);   // [stereo] native_replay: the compute and copy lists' resets counted too
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
std::atomic<DWORD> g_presentTid{0};   // the thread of the game's last present (TellResize)

void BeforePresent(IDXGISwapChain* sc, UINT flags) {
    if (t_present || (flags & DXGI_PRESENT_TEST) || sc != g_sc.load(std::memory_order_relaxed)) return;
    g_presents.fetch_add(1, std::memory_order_relaxed);
    g_presentTid.store(GetCurrentThreadId(), std::memory_order_relaxed);
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
// The game's resizes told (rare: a video option, the render size, a mode change): the size and format before and after, and whether the call is on
// the thread that presents. Crash report 292 (2026-11-08) lost its device ~4 ms after the game's HDR Calibration "SaveAndClose" with a page fault
// where a "Swap Chain" was freed; these lines say next time whether a resize came there, and from which thread.
std::atomic<uint32_t> g_resizesTold{0};
void TellResize(IDXGISwapChain* sc, UINT w, UINT h, DXGI_FORMAT f, UINT n) {
    if (g_resizesTold.fetch_add(1) >= 24) return;
    DXGI_SWAP_CHAIN_DESC d{};
    sc->GetDesc(&d);
    const DWORD tid = GetCurrentThreadId(), present = g_presentTid.load();
    LogF("vrcam: no ReShade - the game resizes its swap chain: %ux%u format %u, %u buffers -> %ux%u format %u, %u buffers (0 = kept); on thread %lu, "
         "%s", d.BufferDesc.Width, d.BufferDesc.Height, (unsigned)d.BufferDesc.Format, d.BufferCount, w, h, (unsigned)f, n, (unsigned long)tid,
         !present ? "no present yet" : present == tid ? "the one that presents" : "NOT the one that presents");
}
void BeforeResize(IDXGISwapChain* sc) {
    if (sc != g_sc.load()) return;
    if (g_sinks.beforeResize) g_sinks.beforeResize();
    if (g_sinks.swapChainChanged) g_sinks.swapChainChanged();
}
HRESULT STDMETHODCALLTYPE HookResize(IDXGISwapChain* sc, UINT n, UINT w, UINT h, DXGI_FORMAT f, UINT flags) {
    if (sc == g_sc.load()) TellResize(sc, w, h, f, n);
    BeforeResize(sc);
    return g_origResize(sc, n, w, h, f, flags);
}
HRESULT STDMETHODCALLTYPE HookResize1(IDXGISwapChain3* sc, UINT n, UINT w, UINT h, DXGI_FORMAT f, UINT flags, const UINT* mask, IUnknown* const* qs) {
    if (sc == g_sc.load()) TellResize(sc, w, h, f, n);
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
    // (the system's own DLLs by their full path: a dxgi.dll beside the game is the player's own ReShade - since 0.203 it is left as it is and
    // never joined, and these hooks run beside it: the game loads it first, it hooks the same exports and vtables, and MinHook chains onto its
    // jumps - whichever of the two sits outside, the game's calls pass through both)
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

ID3D12Resource* RtvResource(uint64_t handle) { return CachedView(t_rtvCache, g_rtvGen, &g_rtvLock, g_rtvs, (SIZE_T)handle, nullptr); }
unsigned RtvIncrement() { return g_rtvInc.load(); }
void TrackSceneViews(bool on) { g_trackSrv.store(on); }
void WatchDomeAtlas(bool on) { g_watchAtlas.store(on, std::memory_order_relaxed); }
void SetAtlasStandIn(AtlasStandInFn fn) { g_atlasStandIn.store(fn); }
ID3D12Resource* SrvResource(uint64_t handle) { return SrvAt((SIZE_T)handle); }
unsigned SrvIncrement() { return g_srvInc.load(); }
ID3D12CommandQueue* GameQueue() { return g_queue.load(); }
void SetOurs(bool ours) { t_ours = ours; }
bool HoldsHook(void* fn) {
    std::lock_guard<std::mutex> l(g_hookMx);
    return fn && std::find(g_hooked.begin(), g_hooked.end(), fn) != g_hooked.end();
}
void SetRootChain(const RootChain& c) {
    g_chainRs.store(c.rootSig);
    g_chainCbv.store(c.cbv);
}
bool Ours() { return t_ours; }

void WatchIndexCount(uint32_t indexCount) { g_watchCount.store(indexCount, std::memory_order_relaxed); }
void TakeWatched(uint32_t* draws, uint32_t* instances) {
    *draws = g_watchDraws.exchange(0, std::memory_order_relaxed);
    *instances = g_watchInstances.exchange(0, std::memory_order_relaxed);
}

void UploadCounts(uint32_t* made, uint32_t* gone) {
    *made = g_uploadsMade.load(std::memory_order_relaxed);
    *gone = g_uploadsGone.load(std::memory_order_relaxed);
}

void SetCensus(bool on) { g_census.store(on, std::memory_order_relaxed); }
bool CensusOn() { return g_census.load(std::memory_order_relaxed); }
Census TakeCensus(unsigned long drawThread) {
    // the counters grow for ever (each written by its own thread only): what came since the last call is the difference from what it saw then
    static uint32_t seen[kCensusSlots][kCensusKinds] = {};
    Census c;
    const int used = std::min(g_censusUsed.load(), kCensusSlots);
    for (int i = 0; i < used; ++i) {
        const DWORD tid = g_censusSlot[i].tid.load();
        bool any = false;
        for (int k = 0; k < kCensusKinds; ++k) {
            const uint32_t now = g_censusSlot[i].n[k].load(std::memory_order_relaxed), d = now - seen[i][k];
            seen[i][k] = now;
            if (!d) continue;
            any = true;
            (tid == drawThread ? c.draw : c.other)[k] += d;
        }
        if (any) ++c.threads;
    }
    c.lost = g_censusUsed.load() > kCensusSlots;
    return c;
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
    if (r.open < 0) { t_ours = false; return; }
    const int i = r.open;
    r.open = -1;
    if (FAILED(r.list->Close()) || !q) { t_ours = false; return; }
    ID3D12CommandList* l = r.list;
    q->ExecuteCommandLists(1, &l);   // (Ours() still true: [stereo] native_replay's look at the queue leaves this out)
    q->Signal(r.fence, ++r.v);
    t_ours = false;
    r.done[i] = r.v;
    g_lists.fetch_add(1, std::memory_order_relaxed);
    r.at = (i + 1) % Ring::kN;
}

}  // namespace natived3d
