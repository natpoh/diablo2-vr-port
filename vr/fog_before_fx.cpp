// fog_before_fx.cpp - see fog_before_fx.h.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "fog_before_fx.h"

#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "fog_height.h"           // StreamPixelShader
#include "fog_in_frame_math.h"    // IsResolve: the game's ps_transparent_resolve by its size and hash
#include "native_d3d12.h"

namespace fogfx {
namespace {

using natived3d::RootCall;

LogFn g_log = nullptr;
void Line(const char* fmt, ...) {
    if (!g_log) return;
    char b[400];
    va_list va;
    va_start(va, fmt);
    vsnprintf(b, sizeof b, fmt, va);
    va_end(va);
    g_log(b);
}
// One line per reason while it lasts; 0 = drawing
std::atomic<int> g_toldWhy{-1};
void Why(int code, const char* text) {
    if (g_toldWhy.exchange(code) != code) Line("vrcam: fog before the effects - %s", text);
}

SRWLOCK g_paramLock = SRWLOCK_INIT;
Params g_params;
std::atomic<bool> g_on{false};
std::atomic<ULONGLONG> g_lastDraw{0};
std::atomic<uint32_t> g_draws{0};
// what the 10 s line says when none is drawn: the resolve's pipeline bound on a followed list / on one not followed (made or reset while
// the step was off), its draws seen, and of them on lists not followed
std::atomic<uint32_t> g_boundSeen{0}, g_boundUntracked{0}, g_drawSeen{0}, g_drawUntracked{0};

// ---- the game's resolve pipelines
constexpr int kResolves = 16;
std::atomic<ID3D12PipelineState*> g_resolve[kResolves] = {};
std::atomic<int> g_resolveN{0};
void AddResolve(ID3D12PipelineState* p) {
    for (auto& r : g_resolve) {
        ID3D12PipelineState* none = nullptr;
        if (r.compare_exchange_strong(none, p)) {
            const int n = g_resolveN.fetch_add(1) + 1;
            Line("vrcam: fog before the effects - the game's transparent resolve pipeline %d found (%p)", n, (void*)p);
            return;
        }
    }
}
bool IsResolvePso(ID3D12PipelineState* p) {
    if (!p) return false;
    for (const auto& r : g_resolve)
        if (r.load(std::memory_order_relaxed) == p) return true;
    return false;
}

// ---- what each list of the game's has bound
struct CParam {
    RootCall::Kind kind = RootCall::kComputeTable;
    uint64_t value = 0;
    uint64_t mask = 0;   // constants: which of c[] are set
    uint32_t c[64] = {};
};
struct Known {   // a resource's last barrier on a list: its subresource 0's state, and whether all its subresources are in it
    ID3D12Resource* r;
    D3D12_RESOURCE_STATES s;
    bool whole;
    uint32_t frame;   // (any list's record: the present count when it was recorded)
};
std::atomic<uint32_t> g_frame{0};   // the game's presents (Frame)
struct List {
    bool tracked = false;   // reset (or made) while the step was wanted: everything bound since is known
    ID3D12DescriptorHeap* heaps[2] = {};
    unsigned nHeaps = 0;
    uint64_t cRoot = 0;
    uint32_t cSet = 0;      // compute parameters set (bits)
    CParam cp[32];
    uint64_t gRoot = 0;
    uint32_t gSet = 0;      // graphics tables set (bits)
    uint64_t gTables[32] = {};
    ID3D12PipelineState* pso = nullptr;
    ID3D12Resource* ds = nullptr;
    ID3D12Resource* rt0 = nullptr;
    ID3D12Resource* scene = nullptr;     // the last R11G11B10 target bound with a depth-stencil
    ID3D12Resource* sceneDs = nullptr;
    std::vector<Known> states;   // the list's last barrier on each
    void Clear() {
        tracked = true;
        nHeaps = 0;
        cRoot = gRoot = 0;
        cSet = gSet = 0;
        pso = nullptr;
        ds = rt0 = scene = sceneDs = nullptr;
        states.clear();
    }
};
SRWLOCK g_listLock = SRWLOCK_INIT;
std::unordered_map<ID3D12GraphicsCommandList*, List*> g_lists;   // (never erased: a list's address keeps its record; Reset clears it)
thread_local ID3D12GraphicsCommandList* t_cl = nullptr;
thread_local List* t_list = nullptr;
List* Of(ID3D12GraphicsCommandList* cl) {
    if (cl == t_cl && t_list) return t_list;
    AcquireSRWLockExclusive(&g_listLock);
    List*& l = g_lists[cl];
    if (!l) l = new List();
    List* got = l;
    ReleaseSRWLockExclusive(&g_listLock);
    t_cl = cl;
    t_list = got;
    return got;
}
// The state THIS list's own last barrier left a resource in (false: none on this list) - certain at the resolve's draw. Another list's are
// recorded in no order with this one's (g_states, below): taken only where the resolve's own draw vouches for them (Insert). PC1
// 2026-10-28 16:52, 0be5454f: DEVICE_HUNG in our CopyResource back into the scene (DRED: "Late Post Processing", [58] >>CopyResource<<) -
// its states guessed, and the scene, it turned out at 17:16 (5db6a047's log), a texture with more than one mip, which a copy of our one-mip
// texture over all of it does not fit.
bool OwnState(const List& l, ID3D12Resource* r, Known* k) {
    for (auto it = l.states.rbegin(); it != l.states.rend(); ++it)
        if (it->r == r) { *k = *it; return true; }
    return false;
}
// any list's last barrier on a resource, in the order they were recorded (not run)
SRWLOCK g_stateLock = SRWLOCK_INIT;
std::unordered_map<ID3D12Resource*, Known> g_states;
bool AnyState(ID3D12Resource* r, Known* k) {
    AcquireSRWLockShared(&g_stateLock);
    const auto it = g_states.find(r);
    const bool got = it != g_states.end();
    if (got) *k = it->second;
    ReleaseSRWLockShared(&g_stateLock);
    return got;
}
// One barrier into a record: subresource 0's state (all the step touches of the scene: its first mip), and whether every subresource is in
// it (the depth's planes: our barriers on it cover them all)
void Note(Known& k, const D3D12_RESOURCE_TRANSITION_BARRIER& t) {
    if (t.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) { k.s = t.StateAfter; k.whole = true; return; }
    if (t.Subresource == 0) k.s = t.StateAfter;
    k.whole = false;
}

// ---- the sinks (the game's calls, before the real ones)
bool Wanted() { return g_on.load(std::memory_order_relaxed); }
void OnReset(ID3D12GraphicsCommandList* cl) {
    List* l = Of(cl);
    if (Wanted()) l->Clear();
    else l->tracked = false;
}
// The scene's target by its depth-stencil, from ANY list: the game draws its opaque scene on another list than the resolve's (PC1
// 2026-10-28: the resolve's draw seen 897 times in 10 s, "no R11G11B10 scene target bound with the resolve's depth before it" - looked
// for on the resolve's own list only). The last few pairs, each target held (a reference of ours: no destroy event here), the oldest let go.
struct ScenePair { ID3D12Resource* ds; ID3D12Resource* scene; };
constexpr int kScenePairs = 8;   // (two eyes' scene and resolve targets, a menu's size besides)
ScenePair g_scenes[kScenePairs] = {};
int g_sceneNext = 0;
SRWLOCK g_sceneLock = SRWLOCK_INIT;
void NoteScene(ID3D12Resource* ds, ID3D12Resource* scene) {
    AcquireSRWLockExclusive(&g_sceneLock);
    for (ScenePair& p : g_scenes)
        if (p.ds == ds && p.scene == scene) { ReleaseSRWLockExclusive(&g_sceneLock); return; }
    ScenePair& slot = g_scenes[g_sceneNext];
    g_sceneNext = (g_sceneNext + 1) % kScenePairs;
    ID3D12Resource* old[2] = {slot.ds, slot.scene};
    ds->AddRef();
    scene->AddRef();
    slot = {ds, scene};
    ReleaseSRWLockExclusive(&g_sceneLock);
    for (ID3D12Resource* r : old)
        if (r) r->Release();
}
// The latest scene target bound with this depth-stencil, on any list - not `resolveTarget`: the frame's target the resolve draws into is
// R11G11B10 too and bound with the same depth (PC1 2026-10-28 15:45: "its target ... format 26", and the newest pair noted for that depth was
// the resolve's own target - the scene looked for was always that, and refused)
// ... and of the depth's own size: the game's half-size target of its additive effects is R11G11B10 as well, bound with the full-size depth
// after the scene (PC1 2026-10-28, flat and VR: "the scene's target 1158x868 found for the resolve's depth 2316x1737" - that layer was
// fogged, not the scene, and the game's pass left its fog's colour out: no fog at all).
ID3D12Resource* SceneOf(ID3D12Resource* ds, ID3D12Resource* resolveTarget) {
    ID3D12Resource* found = nullptr;
    const D3D12_RESOURCE_DESC dd = ds->GetDesc();
    AcquireSRWLockShared(&g_sceneLock);
    for (int i = 1; i <= kScenePairs && !found; ++i) {
        const ScenePair& p = g_scenes[(g_sceneNext - i + kScenePairs) % kScenePairs];
        if (p.ds != ds || p.scene == resolveTarget) continue;
        const D3D12_RESOURCE_DESC sd = p.scene->GetDesc();
        if (sd.Width == dd.Width && sd.Height == dd.Height) found = p.scene;
    }
    ReleaseSRWLockShared(&g_sceneLock);
    return found;
}

// The scene the resolve itself reads (its t10): of the targets bound with its depth (held here, alive), the one a shader-resource view in the
// resolve's own descriptor tables names (natived3d tracks the game's R11G11B10 views). nullptr: none does - the views not seen made, or none
// of those targets in the tables; `elsewhere` then counts R11G11B10 views in them of targets never bound with the depth (only compared).
// PC1 2026-10-28 17:43: the step ran every frame (0.09 ms) on the target last bound with the depth, and no fog reached the picture.
constexpr unsigned kTableLook = 16;   // the descriptors looked at from each table's start (the resolve reads t10 - t13)
ID3D12Resource* ReadScene(const List& l, ID3D12Resource* depth, int* elsewhere) {
    *elsewhere = 0;
    const unsigned inc = natived3d::SrvIncrement();
    if (!inc || !l.gSet || !l.nHeaps) return nullptr;
    ID3D12Resource* cand[kScenePairs];
    int nc = 0;
    AcquireSRWLockShared(&g_sceneLock);
    for (const ScenePair& p : g_scenes)
        if (p.ds == depth && p.scene && p.scene != l.rt0) cand[nc++] = p.scene;
    ReleaseSRWLockShared(&g_sceneLock);
    for (unsigned h = 0; h < l.nHeaps; ++h) {
        ID3D12DescriptorHeap* heap = l.heaps[h];
        if (!heap) continue;
        const D3D12_DESCRIPTOR_HEAP_DESC hd = heap->GetDesc();
        if (hd.Type != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV || !(hd.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)) continue;
        const UINT64 g0 = heap->GetGPUDescriptorHandleForHeapStart().ptr;
        const SIZE_T c0 = heap->GetCPUDescriptorHandleForHeapStart().ptr;
        for (uint32_t i = 0; i < 32; ++i) {
            if (!(l.gSet & (1u << i))) continue;
            const uint64_t gpu = l.gTables[i];
            if (gpu < g0 || gpu >= g0 + (UINT64)hd.NumDescriptors * inc) continue;
            const UINT first = (UINT)((gpu - g0) / inc);
            for (UINT j = 0; j < kTableLook && first + j < hd.NumDescriptors; ++j) {
                ID3D12Resource* r = natived3d::SrvResource(c0 + (SIZE_T)(first + j) * inc);
                if (!r) continue;
                for (int c = 0; c < nc; ++c)
                    if (cand[c] == r) return r;
                ++*elsewhere;
            }
        }
    }
    return nullptr;
}

void OnTargets(ID3D12GraphicsCommandList* cl, unsigned n, const D3D12_CPU_DESCRIPTOR_HANDLE* rts, bool, ID3D12Resource* ds) {
    if (!Wanted()) return;
    List* l = Of(cl);
    l->ds = ds;
    l->rt0 = n && rts ? natived3d::RtvResource(rts[0].ptr) : nullptr;
    if (l->rt0 && ds) {
        const D3D12_RESOURCE_DESC d = l->rt0->GetDesc();
        if (d.Format == DXGI_FORMAT_R11G11B10_FLOAT && d.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
            l->scene = l->rt0;
            l->sceneDs = ds;
            NoteScene(ds, l->rt0);
        }
    }
}
void OnBarrier(ID3D12GraphicsCommandList* cl, unsigned n, const D3D12_RESOURCE_BARRIER* b) {
    if (!Wanted()) return;
    List* l = Of(cl);
    for (unsigned i = 0; i < n; ++i) {
        if (b[i].Type != D3D12_RESOURCE_BARRIER_TYPE_TRANSITION || !b[i].Transition.pResource) continue;
        ID3D12Resource* r = b[i].Transition.pResource;
        const D3D12_RESOURCE_TRANSITION_BARRIER& t = b[i].Transition;
        // (a first barrier on one subresource other than 0: subresource 0's state not known from it)
        const bool firstKnows = t.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES || t.Subresource == 0;
        Known* e = nullptr;
        for (auto& x : l->states)
            if (x.r == r) { e = &x; break; }
        if (!e && firstKnows) { l->states.push_back({r, t.StateAfter, false, 0}); e = &l->states.back(); }
        if (e) Note(*e, t);
        const uint32_t frame = g_frame.load(std::memory_order_relaxed);
        AcquireSRWLockExclusive(&g_stateLock);
        if (const auto it = g_states.find(r); it != g_states.end()) { Note(it->second, t); it->second.frame = frame; }
        else if (firstKnows) { Known k{r, t.StateAfter, false, frame}; Note(k, t); g_states[r] = k; }
        ReleaseSRWLockExclusive(&g_stateLock);
    }
}
void OnBound(ID3D12GraphicsCommandList* cl, ID3D12PipelineState* pso) {
    if (!Wanted()) return;
    List* l = Of(cl);
    l->pso = pso;
    if (IsResolvePso(pso)) (l->tracked ? g_boundSeen : g_boundUntracked).fetch_add(1, std::memory_order_relaxed);
}
void OnHeaps(ID3D12GraphicsCommandList* cl, unsigned n, ID3D12DescriptorHeap* const* h) {
    if (!Wanted()) return;
    List* l = Of(cl);
    l->nHeaps = std::min(n, 2u);
    for (unsigned i = 0; i < l->nHeaps; ++i) l->heaps[i] = h[i];
}
void OnRoot(ID3D12GraphicsCommandList* cl, const RootCall& c) {
    if (!Wanted()) return;
    List* l = Of(cl);
    switch (c.kind) {
    case RootCall::kGraphicsRootSignature:
        if (c.value != l->gRoot) l->gSet = 0;
        l->gRoot = c.value;
        return;
    case RootCall::kGraphicsTable:
        if (c.index < 32) { l->gTables[c.index] = c.value; l->gSet |= 1u << c.index; }
        return;
    case RootCall::kComputeRootSignature:
        if (c.value != l->cRoot) l->cSet = 0;
        l->cRoot = c.value;
        return;
    default:
        break;
    }
    if (c.index >= 32) return;
    CParam& p = l->cp[c.index];
    const bool consts = c.kind == RootCall::kComputeConstant || c.kind == RootCall::kComputeConstants;
    if (!(l->cSet & (1u << c.index)) || (p.kind == RootCall::kComputeConstant) != consts) p.mask = 0;
    p.kind = consts ? RootCall::kComputeConstant : c.kind;
    if (c.kind == RootCall::kComputeConstant) {
        if (c.offset < 64) { p.c[c.offset] = (uint32_t)c.value; p.mask |= 1ull << c.offset; }
    } else if (c.kind == RootCall::kComputeConstants) {
        for (unsigned i = 0; i < c.n && c.offset + i < 64; ++i) {
            p.c[c.offset + i] = ((const uint32_t*)c.data)[i];
            p.mask |= 1ull << (c.offset + i);
        }
    } else {
        p.value = c.value;
    }
    l->cSet |= 1u << c.index;
}
void OnDraw(ID3D12GraphicsCommandList* cl, unsigned, unsigned);

// ---- our step: its shader, root signature, pipeline, heap, texture, timestamps (one device)
const char kCs[] = R"(
cbuffer K : register(b0) {
    float plane, density, nearPlane, heightOn;
    float3 fogColour; float top;
    float3 up; float soft;
    float sx, sy, offX, curveOn;
    uint width, height; float cStart, cEnd;
    float cCurve, cStrength, pad0, pad1;
};
Texture2D<float> Depth : register(t0);
Texture2D<float3> Scene : register(t1);
RWTexture2D<float3> Out : register(u0);
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= width || id.y >= height) return;
    const float3 c = Scene[id.xy];
    const float d = Depth[id.xy];
    if (d <= 1e-7) { Out[id.xy] = c; return; }   // the void: left as it is
    const float z = nearPlane / d;
    const float2 ndc = float2((id.x + 0.5) / width * 2.0 - 1.0, 1.0 - (id.y + 0.5) / height * 2.0);
    const float3 v = float3((ndc.x - offX) / sx * z, ndc.y / sy * z, z);
    const float dist = sqrt(dot(v, v) + 1.0);   // (the game's: its view position's w is 1)
    float t = curveOn > 0.5 ? 1.0 - pow(saturate((dist - cStart) / max(cEnd - cStart, 1.0)), cCurve) * cStrength   // ([fog] game_fog_our_curve)
                            : min(1.0, exp(-density * (dist / plane - 1.0)));
    if (heightOn > 0.5) t = 1.0 - (1.0 - t) * saturate((top - dot(v, up)) / soft);
    Out[id.xy] = lerp(fogColour, c, t);
}
)";
struct Consts {
    float plane, density, nearPlane, heightOn;
    float fog[3], top;
    float up[3], soft;
    float sx, sy, offX, curveOn;
    uint32_t width, height;
    float cStart, cEnd;
    float cCurve, cStrength, pad0, pad1;
};
static_assert(sizeof(Consts) == 24 * 4, "twenty-four root constants");

constexpr int kSlots = 64;   // descriptor sets and timestamp pairs used in turn (the GPU is never that many steps behind)
struct Gpu {
    ID3D12Device* dev = nullptr;
    ID3D12RootSignature* root = nullptr;
    ID3D12PipelineState* cs = nullptr;
    ID3D12DescriptorHeap* heap = nullptr;
    UINT inc = 0;
    ID3D12Resource* tex = nullptr;
    UINT64 w = 0;
    UINT h = 0;
    ID3D12QueryHeap* queries = nullptr;
    ID3D12Resource* readback = nullptr;
    const uint64_t* stamps = nullptr;
    bool failed = false;
};
Gpu g_gpu;
std::mutex g_gpuMx;
uint32_t g_slot = 0;          // under g_gpuMx
uint32_t g_readUpTo = 0;      // timestamps read up to this step

bool MakeGpu(ID3D12Device* dev) {
    Gpu& g = g_gpu;
    if (g.dev == dev && g.cs) return true;
    if (g.failed || (g.dev && g.dev != dev)) return false;
    g.dev = dev;
    HMODULE lib = LoadLibraryExW(L"d3dcompiler_47.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto compile = lib ? reinterpret_cast<pD3DCompile>(GetProcAddress(lib, "D3DCompile")) : nullptr;
    ID3DBlob *code = nullptr, *err = nullptr;
    if (!compile || FAILED(compile(kCs, sizeof kCs - 1, "vrcam-fog-before-fx", nullptr, nullptr, "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code,
                                   &err))) {
        Line("vrcam: fog before the effects - our shader did not compile: %s", err ? (const char*)err->GetBufferPointer() : "no d3dcompiler_47.dll");
        if (err) err->Release();
        if (lib) FreeLibrary(lib);
        g.failed = true;
        return false;
    }
    if (lib) FreeLibrary(lib);
    D3D12_DESCRIPTOR_RANGE ranges[2] = {{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, 0}, {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0}};
    D3D12_ROOT_PARAMETER params[3] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants = {0, 0, sizeof(Consts) / 4};
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable = {1, &ranges[0]};
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable = {1, &ranges[1]};
    const D3D12_ROOT_SIGNATURE_DESC rd{3, params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ID3DBlob* sig = nullptr;
    using SerializeFn = HRESULT(WINAPI*)(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);
    const HMODULE d3d12 = GetModuleHandleW(L"d3d12.dll");   // (the game's, loaded: the plugin links no d3d12.lib)
    const auto serialize = d3d12 ? reinterpret_cast<SerializeFn>(GetProcAddress(d3d12, "D3D12SerializeRootSignature")) : nullptr;
    bool ok = serialize && SUCCEEDED(serialize(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, nullptr)) &&
              SUCCEEDED(dev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), __uuidof(ID3D12RootSignature), (void**)&g.root));
    if (sig) sig->Release();
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = g.root;
    pd.CS = {code->GetBufferPointer(), code->GetBufferSize()};
    natived3d::SetOurs(true);
    ok = ok && SUCCEEDED(dev->CreateComputePipelineState(&pd, __uuidof(ID3D12PipelineState), (void**)&g.cs));
    natived3d::SetOurs(false);
    code->Release();
    const D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 3 * kSlots, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    ok = ok && SUCCEEDED(dev->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void**)&g.heap));
    g.inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const D3D12_QUERY_HEAP_DESC qd{D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 2 * kSlots, 0};
    ok = ok && SUCCEEDED(dev->CreateQueryHeap(&qd, __uuidof(ID3D12QueryHeap), (void**)&g.queries));
    D3D12_HEAP_PROPERTIES rb{D3D12_HEAP_TYPE_READBACK};
    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = 2 * kSlots * sizeof(uint64_t);
    bd.Height = bd.DepthOrArraySize = bd.MipLevels = 1;
    bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ok = ok && SUCCEEDED(dev->CreateCommittedResource(&rb, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource),
                                                      (void**)&g.readback));
    void* mapped = nullptr;
    ok = ok && SUCCEEDED(g.readback->Map(0, nullptr, &mapped));
    g.stamps = (const uint64_t*)mapped;
    D3D12_FEATURE_DATA_FORMAT_SUPPORT fs{DXGI_FORMAT_R11G11B10_FLOAT};
    if (ok && (FAILED(dev->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &fs, sizeof fs)) ||
               !(fs.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE))) {
        Line("vrcam: fog before the effects - this graphics card cannot write R11G11B10 from a compute shader: the game's fog as before");
        ok = false;
    }
    if (g.heap) g.heap->SetName(L"d2rvr fog before fx heap");
    if (!ok) {
        g.failed = true;
        Line("vrcam: fog before the effects - our step could not be made on the game's device: the game's fog as before");
        return false;
    }
    return true;
}
bool MakeTexture(UINT64 w, UINT h) {   // the fogged scene, as big as the game's
    Gpu& g = g_gpu;
    if (g.tex && g.w == w && g.h == h) return true;
    if (g.tex) {   // (a new size: the old one's last step long done - a resize waits for the GPU)
        g.tex->Release();
        g.tex = nullptr;
    }
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = w;
    d.Height = h;
    d.DepthOrArraySize = d.MipLevels = 1;
    d.Format = DXGI_FORMAT_R11G11B10_FLOAT;
    d.SampleDesc.Count = 1;
    d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (FAILED(g.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, __uuidof(ID3D12Resource),
                                              (void**)&g.tex)))
        return false;
    g.tex->SetName(L"d2rvr fog before fx scene");
    g.w = w;
    g.h = h;
    return true;
}
DXGI_FORMAT DepthSrvFormat(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R24G8_TYPELESS: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case DXGI_FORMAT_R32G8X24_TYPELESS: return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    case DXGI_FORMAT_R32_TYPELESS: return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_R16_TYPELESS: return DXGI_FORMAT_R16_UNORM;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}
D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to,
                                  UINT sub = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {r, sub, from, to};
    return b;
}

// The step, before the resolve's draw on `cl` (the game's state given back after it)
void Insert(ID3D12GraphicsCommandList* cl, List& l) {
    Params p;
    AcquireSRWLockShared(&g_paramLock);
    p = g_params;
    ReleaseSRWLockShared(&g_paramLock);
    if (!p.on) return;
    ID3D12Resource* scene = l.scene;
    ID3D12Resource* depth = l.ds;
    if (!depth) { Why(1, "the resolve's draw has no depth-stencil bound: its draw goes on alone"); return; }
    // (the scene drawn on another list, or the list's own note a smaller target bound with the same depth: by the depth they share and its size)
    if (scene && (l.sceneDs != depth || scene == l.rt0 || scene->GetDesc().Width != depth->GetDesc().Width || scene->GetDesc().Height != depth->GetDesc().Height))
        scene = nullptr;
    if (!scene) scene = SceneOf(depth, l.rt0);
    {   // the scene the resolve's own tables name, when they name one: it wins
        int elsewhere = 0;
        ID3D12Resource* read = ReadScene(l, depth, &elsewhere);
        static int toldHow = -1;   // 0 the tables name the target taken, 1 they name another of the depth's, 2 one not bound with it, 3 none seen
        const int how = read ? (read == scene ? 0 : 1) : elsewhere ? 2 : 3;
        if (how != toldHow) {
            toldHow = how;
            if (how == 0) Line("vrcam: fog before the effects - the resolve's own tables read the scene %p: the target taken", (void*)read);
            else if (how == 1)
                Line("vrcam: fog before the effects - the resolve's own tables read %p, not %p (the target last bound with its depth): that one taken",
                     (void*)read, (void*)scene);
            else if (how == 2)
                Line("vrcam: fog before the effects - the resolve's own tables read %d R11G11B10 view(s) of target(s) never bound with its depth: "
                     "the scene may be made apart from the depth (not followed); %p (the last bound with it) taken", elsewhere, (void*)scene);
            else Line("vrcam: fog before the effects - no R11G11B10 view seen in the resolve's own tables: %p (the last bound with its depth) taken",
                      (void*)scene);
        }
        if (read) scene = read;
    }
    if (!scene || scene == l.rt0) {
        static ID3D12Resource* toldRt = nullptr;   // what IS bound at the resolve: again whenever it changes (a menu's size, the render size)
        static ID3D12Resource* toldDs = nullptr;
        if (toldRt != l.rt0 || toldDs != depth) {
            toldRt = l.rt0;
            toldDs = depth;
            const D3D12_RESOURCE_DESC rd = l.rt0 ? l.rt0->GetDesc() : D3D12_RESOURCE_DESC{}, dd0 = depth->GetDesc();
            int pairs = 0;
            AcquireSRWLockShared(&g_sceneLock);
            for (const ScenePair& p : g_scenes) pairs += p.ds != nullptr;
            ReleaseSRWLockShared(&g_sceneLock);
            Line("vrcam: fog before the effects - at the resolve: its target %p format %d %llux%u, its depth %p format %d %llux%u; R11G11B10 "
                 "targets seen bound with a depth: %d pair(s), none but its own target with this depth", (void*)l.rt0, (int)rd.Format,
                 (unsigned long long)rd.Width, rd.Height, (void*)depth, (int)dd0.Format, (unsigned long long)dd0.Width, dd0.Height, pairs);
        }
        Why(2, "no R11G11B10 scene target bound with the resolve's depth (on any list): its draw goes on alone");
        return;
    }
    const D3D12_RESOURCE_DESC sd = scene->GetDesc(), dd = depth->GetDesc();
    {   // each scene target found, once (a menu's size, the render size, the other eye's)
        static ID3D12Resource* toldScene = nullptr;
        if (toldScene != scene) {
            toldScene = scene;
            Line("vrcam: fog before the effects - the scene's target %p (%llux%u) found for the resolve's depth %p (%llux%u; its own target %p)",
                 (void*)scene, (unsigned long long)sd.Width, sd.Height, (void*)depth, (unsigned long long)dd.Width, dd.Height, (void*)l.rt0);
        }
    }
    const DXGI_FORMAT dfmt = DepthSrvFormat(dd.Format);
    // (one size: a smaller target bound with the depth is the game's half-size effects layer, not the scene - see SceneOf. The scene may
    // have mips - PC1's has, 2026-10-28 17:16 - its first is read and written, the rest left as they are; one slice. The depth one mip, one
    // slice: our barriers on it cover all its subresources)
    if (sd.Width != dd.Width || sd.Height != dd.Height || sd.SampleDesc.Count != 1 || dd.SampleDesc.Count != 1 || sd.DepthOrArraySize != 1 ||
        dd.MipLevels != 1 || dd.DepthOrArraySize != 1 || sd.Format != DXGI_FORMAT_R11G11B10_FLOAT) {
        static ID3D12Resource* toldFor = nullptr;
        if (toldFor != scene) {
            toldFor = scene;
            Line("vrcam: fog before the effects - not drawn with the scene %p: %llux%u, %u mip(s), %u slice(s), %u sample(s), format %d; its depth "
                 "%llux%u, %u mip(s), %u slice(s), %u sample(s)", (void*)scene, (unsigned long long)sd.Width, sd.Height, (unsigned)sd.MipLevels,
                 (unsigned)sd.DepthOrArraySize, sd.SampleDesc.Count, (int)sd.Format, (unsigned long long)dd.Width, dd.Height,
                 (unsigned)dd.MipLevels, (unsigned)dd.DepthOrArraySize, dd.SampleDesc.Count);
        }
        Why(3, "the scene and the depth are not one size (or multisampled, arrays, the depth mipped): the resolve's draw goes on alone");
        return;
    }
    if (dfmt == DXGI_FORMAT_UNKNOWN || (dd.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)) {
        Why(4, "the depth cannot be read by a shader (its format or flags): the resolve's draw goes on alone");
        return;
    }
    // Fail safe: nothing of ours on a target whose state is a guess.
    //  - The resolve's own list's barriers: certain.
    //  - Another list's (the game records the scene's and the depth's barriers on earlier lists - PC1 2026-10-28 17:16: none on the
    //    resolve's): only a read state, recorded since the last present. The resolve's draw on this list reads the scene and has the depth
    //    bound without writing it - in any state but a read one the game's own draw would be wrong; so a read state recorded elsewhere this
    //    frame is the one it runs in. A write state recorded elsewhere (the next frame's, recorded early) is not taken, nor anything older
    //    than this frame (a barrier back to a write state made where it was not seen - a list not followed - would leave a read state stale).
    //  The scene: subresource 0 (its first mip, all we touch) a shader resource; the depth: every subresource in one read state of depth.
    D3D12_RESOURCE_STATES sScene{}, sDepth{};
    {
        const D3D12_RESOURCE_STATES readable = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        const D3D12_RESOURCE_STATES depthOk = D3D12_RESOURCE_STATE_DEPTH_READ | readable;
        Known ks{}, kd{};
        const bool ownScene = OwnState(l, scene, &ks), ownDepth = OwnState(l, depth, &kd);
        const uint32_t frame = g_frame.load(std::memory_order_relaxed);
        const bool anyScene = ownScene || (AnyState(scene, &ks) && ks.frame == frame);
        const bool anyDepth = ownDepth || (AnyState(depth, &kd) && kd.frame == frame);
        sScene = anyScene ? ks.s : D3D12_RESOURCE_STATES{};
        sDepth = anyDepth ? kd.s : D3D12_RESOURCE_STATES{};
        const bool sceneRead = (sScene & D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) && !(sScene & ~readable);
        const bool depthRead = (sDepth & D3D12_RESOURCE_STATE_DEPTH_READ) && !(sDepth & ~depthOk);
        const bool sceneOk = anyScene && sceneRead;
        const bool depthGood = anyDepth && kd.whole && (depthRead || (ownDepth && sDepth == D3D12_RESOURCE_STATE_DEPTH_WRITE));
        static uint64_t toldWays = 0;   // (each way the states were found or refused, once: a line costs a millisecond)
        const int which = (sceneOk ? 0 : 1) | (depthGood ? 0 : 2) | (ownScene ? 4 : 0) | (ownDepth ? 8 : 0) | (anyScene ? 16 : 0) | (anyDepth ? 32 : 0);
        if (!(toldWays & (1ull << which))) {
            toldWays |= 1ull << which;
            Line("vrcam: fog before the effects - the scene's state 0x%X (%s), the depth's 0x%X (%s%s): %s", (unsigned)sScene,
                 ownScene ? "set on the resolve's list" : anyScene ? "set on another list this frame" : "NOT known this frame", (unsigned)sDepth,
                 ownDepth ? "set on the resolve's list" : anyDepth ? "set on another list this frame" : "NOT known this frame",
                 anyDepth && !kd.whole ? ", not all its subresources" : "",
                 sceneOk && depthGood ? "drawn from them" : "not drawn - only states the resolve's list set, or read states set elsewhere, are taken");
        }
        if (!sceneOk || !depthGood) {
            Why(5, "the scene's or the depth's state not known as the resolve needs it: its draw goes on alone");
            return;
        }
    }
    std::lock_guard<std::mutex> one(g_gpuMx);
    ID3D12Device* dev = nullptr;
    if (FAILED(cl->GetDevice(__uuidof(ID3D12Device), (void**)&dev)) || !dev) return;
    dev->Release();   // (the game's device, held by the game)
    if (!MakeGpu(dev) || !MakeTexture(sd.Width, sd.Height)) return;
    Gpu& g = g_gpu;
    const uint32_t step = g_slot++;
    const uint32_t k = step % kSlots;
    // this step's views: t0 the depth, t1 the scene, u0 ours
    const D3D12_CPU_DESCRIPTOR_HANDLE cpu{g.heap->GetCPUDescriptorHandleForHeapStart().ptr + (SIZE_T)k * 3 * g.inc};
    const D3D12_GPU_DESCRIPTOR_HANDLE gpu{g.heap->GetGPUDescriptorHandleForHeapStart().ptr + (UINT64)k * 3 * g.inc};
    D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    sv.Format = dfmt;
    dev->CreateShaderResourceView(depth, &sv, cpu);
    sv.Format = DXGI_FORMAT_R11G11B10_FLOAT;
    dev->CreateShaderResourceView(scene, &sv, D3D12_CPU_DESCRIPTOR_HANDLE{cpu.ptr + g.inc});
    D3D12_UNORDERED_ACCESS_VIEW_DESC uv{};
    uv.Format = DXGI_FORMAT_R11G11B10_FLOAT;
    uv.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    dev->CreateUnorderedAccessView(g.tex, nullptr, &uv, D3D12_CPU_DESCRIPTOR_HANDLE{cpu.ptr + 2 * g.inc});

    const D3D12_RESOURCE_STATES readOnly = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    const D3D12_RESOURCE_STATES rScene =
        (sScene & ~readOnly) == 0 && sScene != D3D12_RESOURCE_STATE_COMMON ? (sScene | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
                                                                             : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    const D3D12_RESOURCE_STATES rDepth = D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                                         (sDepth & D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    Consts c{};
    c.plane = std::max(p.plane, 0.01f);
    c.density = p.density;
    c.nearPlane = p.nearPlane;
    c.heightOn = p.height ? 1.0f : 0.0f;
    std::memcpy(c.fog, p.colour, sizeof c.fog);
    c.top = p.top;
    std::memcpy(c.up, p.up, sizeof c.up);
    c.soft = std::max(p.soft, 0.01f);
    c.sx = p.sx != 0.0f ? p.sx : 1.0f;
    c.sy = p.sy != 0.0f ? p.sy : 1.0f;
    c.offX = p.offX;
    c.width = (uint32_t)sd.Width;
    c.height = sd.Height;
    c.curveOn = p.ourCurve ? 1.0f : 0.0f;
    c.cStart = p.start;
    c.cEnd = p.end;
    c.cCurve = std::max(p.curve, 0.05f);
    c.cStrength = p.strength;

    natived3d::SetOurs(true);
    cl->EndQuery(g.queries, D3D12_QUERY_TYPE_TIMESTAMP, 2 * k);
    D3D12_RESOURCE_BARRIER b[3];
    UINT nb = 0;
    if (rScene != sScene) b[nb++] = Transition(scene, sScene, rScene, 0);
    if (rDepth != sDepth) b[nb++] = Transition(depth, sDepth, rDepth);
    if (nb) cl->ResourceBarrier(nb, b);
    ID3D12DescriptorHeap* heaps[1] = {g.heap};
    cl->SetDescriptorHeaps(1, heaps);
    cl->SetComputeRootSignature(g.root);
    cl->SetPipelineState(g.cs);
    cl->SetComputeRoot32BitConstants(0, sizeof(Consts) / 4, &c, 0);
    cl->SetComputeRootDescriptorTable(1, gpu);
    cl->SetComputeRootDescriptorTable(2, D3D12_GPU_DESCRIPTOR_HANDLE{gpu.ptr + 2 * g.inc});
    cl->Dispatch((UINT)((sd.Width + 7) / 8), (sd.Height + 7) / 8, 1);
    nb = 0;
    b[nb++] = Transition(g.tex, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    b[nb++] = Transition(scene, rScene, D3D12_RESOURCE_STATE_COPY_DEST, 0);
    if (rDepth != sDepth) b[nb++] = Transition(depth, rDepth, sDepth);
    cl->ResourceBarrier(nb, b);
    {   // into the scene's first mip (CopyResource wants the same mips on both sides)
        D3D12_TEXTURE_COPY_LOCATION dst{scene, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX}, src{g.tex, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
        dst.SubresourceIndex = 0;
        src.SubresourceIndex = 0;
        cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    }
    b[0] = Transition(scene, D3D12_RESOURCE_STATE_COPY_DEST, sScene, 0);
    b[1] = Transition(g.tex, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cl->ResourceBarrier(2, b);
    cl->EndQuery(g.queries, D3D12_QUERY_TYPE_TIMESTAMP, 2 * k + 1);
    cl->ResolveQueryData(g.queries, D3D12_QUERY_TYPE_TIMESTAMP, 2 * k, 2, g.readback, 2 * k * sizeof(uint64_t));
    // the game's state again: its heaps, its graphics tables (set again under them), its compute root signature and arguments, its pipeline
    if (l.nHeaps) cl->SetDescriptorHeaps(l.nHeaps, l.heaps);
    for (uint32_t i = 0; i < 32; ++i)
        if (l.gSet & (1u << i)) cl->SetGraphicsRootDescriptorTable(i, D3D12_GPU_DESCRIPTOR_HANDLE{l.gTables[i]});
    if (l.cRoot) {
        cl->SetComputeRootSignature((ID3D12RootSignature*)(uintptr_t)l.cRoot);
        for (uint32_t i = 0; i < 32; ++i) {
            if (!(l.cSet & (1u << i))) continue;
            const CParam& q = l.cp[i];
            switch (q.kind) {
            case RootCall::kComputeTable: cl->SetComputeRootDescriptorTable(i, D3D12_GPU_DESCRIPTOR_HANDLE{q.value}); break;
            case RootCall::kComputeCbv: cl->SetComputeRootConstantBufferView(i, q.value); break;
            case RootCall::kComputeSrv: cl->SetComputeRootShaderResourceView(i, q.value); break;
            case RootCall::kComputeUav: cl->SetComputeRootUnorderedAccessView(i, q.value); break;
            case RootCall::kComputeConstant:
                for (uint32_t j = 0; j < 64; ++j)
                    if (q.mask & (1ull << j)) cl->SetComputeRoot32BitConstant(i, q.c[j], j);
                break;
            default: break;
            }
        }
    }
    cl->SetPipelineState(l.pso);
    natived3d::SetOurs(false);
    g_draws.fetch_add(1, std::memory_order_relaxed);
    g_lastDraw.store(GetTickCount64(), std::memory_order_relaxed);
    if (g_toldWhy.exchange(0) != 0)
        Line("vrcam: fog before the effects - drawn: the scene fogged (plane %.0f, density %.3f, colour %.3f %.3f %.3f) before the game's transparent "
             "resolve (%llux%u); the game's fog pass leaves the fog's colour out",
             c.plane, c.density, c.fog[0], c.fog[1], c.fog[2], (unsigned long long)sd.Width, sd.Height);
}
void OnDraw(ID3D12GraphicsCommandList* cl, unsigned, unsigned) {
    if (!Wanted()) return;
    List* l = Of(cl);
    if (!IsResolvePso(l->pso)) return;
    g_drawSeen.fetch_add(1, std::memory_order_relaxed);
    if (!l->tracked) { g_drawUntracked.fetch_add(1, std::memory_order_relaxed); return; }
    Insert(cl, *l);
}

}  // namespace

void SetLog(LogFn log) { g_log = log; }

void FillSinks(natived3d::Sinks& k) {
    natived3d::TrackSceneViews(true);   // (the scene the resolve reads: ReadScene)
    k.fxReset = &OnReset;
    k.fxTargets = &OnTargets;
    k.fxBarrier = &OnBarrier;
    k.fxBound = &OnBound;
    k.fxHeaps = &OnHeaps;
    k.fxRoot = &OnRoot;
    k.fxDraw = &OnDraw;
}

void OnGraphicsPso(ID3D12Device*, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* d, ID3D12PipelineState* pso) {
    if (d && pso && fogframe::IsResolve(d->PS.pShaderBytecode, d->PS.BytecodeLength)) AddResolve(pso);
}
void OnStreamPso(ID3D12Device*, const D3D12_PIPELINE_STATE_STREAM_DESC* d, ID3D12PipelineState* pso) {
    const void* code = nullptr;
    size_t size = 0;
    if (pso && fogheight::StreamPixelShader(d, &code, &size) && fogframe::IsResolve(code, size)) AddResolve(pso);
}
void MarkResolve(ID3D12PipelineState* pso) { AddResolve(pso); }

void Configure(const Params& p) {
    AcquireSRWLockExclusive(&g_paramLock);
    g_params = p;
    ReleaseSRWLockExclusive(&g_paramLock);
    g_on.store(p.on, std::memory_order_relaxed);
}

bool Drawn() { return g_on.load(std::memory_order_relaxed) && GetTickCount64() - g_lastDraw.load(std::memory_order_relaxed) < 250; }

void Frame() {
    g_frame.fetch_add(1, std::memory_order_relaxed);
    static ULONGLONG toldAt = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - toldAt < 10000) return;
    toldAt = now;
    const uint32_t draws = g_draws.exchange(0);
    if (!g_on.load() && !draws) return;
    double ms = 0.0;
    int n = 0;
    {
        std::lock_guard<std::mutex> one(g_gpuMx);
        ID3D12CommandQueue* q = natived3d::GameQueue();
        UINT64 hz = 0;
        if (g_gpu.stamps && q && SUCCEEDED(q->GetTimestampFrequency(&hz)) && hz) {
            // the steps at least 8 back are surely done; at most the last kSlots - 8 of them
            const uint32_t upTo = g_slot > 8 ? g_slot - 8 : 0;
            const uint32_t from = std::max(g_readUpTo, upTo > (uint32_t)kSlots - 8 ? upTo - (kSlots - 8) : 0u);
            for (uint32_t s = from; s < upTo; ++s) {
                const uint32_t k = s % kSlots;
                const uint64_t a = g_gpu.stamps[2 * k], b = g_gpu.stamps[2 * k + 1];
                if (b > a && b - a < hz) { ms += (double)(b - a) * 1000.0 / (double)hz; ++n; }
            }
            g_readUpTo = upTo;
        }
    }
    const uint32_t bound = g_boundSeen.exchange(0), boundU = g_boundUntracked.exchange(0), drawn = g_drawSeen.exchange(0), drawnU = g_drawUntracked.exchange(0);
    Line("vrcam: fog before the effects, 10 s: %u steps before the game's transparent resolve%s; on the graphics card %.3f ms a step (%d measured)%s "
         "(its pipeline bound %u times on followed lists, %u on others; its draws %u, %u of them on lists not followed)",
         draws, g_resolveN.load() ? "" : " - the game's resolve pipeline NOT found (another build?)", n ? ms / n : 0.0, n,
         g_on.load() && !draws ? " - NONE drawn: the game's fog as before" : "", bound, boundU, drawn, drawnU);
}

}  // namespace fogfx
