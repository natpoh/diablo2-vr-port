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
#include <string>
#include <unordered_map>
#include <vector>

#include "fog_height.h"           // StreamPixelShader
#include "fog_in_frame_math.h"    // IsResolve: the game's ps_transparent_resolve by its size and hash
#include "native_d3d12.h"
#include "pso_stream.h"           // the effects' pipelines made from a stream
#include "../shared/unleashed_fog.h"   // [fog] ease_in: UnleashedFogEase (MPL 2.0, D2R VR Unleashed), put in front of our shader

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
std::atomic<bool> g_ours{false};   // Params::ours, on
std::atomic<bool> g_fxDepth{false};   // Params::fxDepth, on
std::atomic<ULONGLONG> g_lastDraw{0};
std::atomic<uint32_t> g_draws{0};
// the effects' depth, since the 10 s line: draws given a depth, of them by a twin made now; a twin pipeline's draws not given one (their
// targets not the effects', a twin not made, no depth bound); steps that fogged the effects by it, steps with effects drawn but not fogged
std::atomic<uint32_t> g_fxTwinDraws{0}, g_fxTwinsMade{0}, g_fxOther{0}, g_fxFogged{0}, g_fxNotFogged{0};
// ... the effects' draws not drawn again (no step took their depth for a while: g_fxIdle), and the steps in a row that took none - the
// twins then wait, trying again for a quarter second every 5 s (PC1 2026-10-31 16:03: 1250 draws a pair drawn again, no step took them)
std::atomic<uint32_t> g_fxSkipped{0}, g_fxIdle{0};
constexpr uint32_t kFxIdleSteps = 120;
// What it costs (PC1 2026-10-31 16:08: [fog] enabled=0 took 0.192.21 from 38 to 68-70 frames/s, a pair 25 -> 14 ms): the CPU in the
// sinks (every bind, barrier and draw of the followed lists), in the twins and in the step, by the performance counter; the twins' GPU
// time sampled - one present a second, each twin between two timestamps (TwinTimes, Frame)
std::atomic<uint64_t> g_cpuSinks{0}, g_cpuTwins{0}, g_cpuStep{0};   // counter ticks since the 10 s line
struct CpuTime {
    std::atomic<uint64_t>& to;
    LARGE_INTEGER t0;
    explicit CpuTime(std::atomic<uint64_t>& a) : to(a) { QueryPerformanceCounter(&t0); }
    ~CpuTime() {
        LARGE_INTEGER t1;
        QueryPerformanceCounter(&t1);
        to.fetch_add((uint64_t)(t1.QuadPart - t0.QuadPart), std::memory_order_relaxed);
    }
};
constexpr uint32_t kTwinStamps = 4096;   // twins timed in a sampled present (pairs)
struct TwinTimes {
    ID3D12QueryHeap* heap = nullptr;
    ID3D12Resource* readback = nullptr;
    const uint64_t* data = nullptr;
    ID3D12CommandAllocator* alloc = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    ID3D12Fence* fence = nullptr;
    UINT64 fenceAt = 0;
    std::atomic<bool> sampling{false};     // the twins recorded now are timed
    std::atomic<uint32_t> used{0};         // ... so many of them
    int stage = 0;                         // the present's: 0 idle, 1 sampling, 2 waiting to resolve, 3 waiting to read
    uint32_t pairs = 0, waited = 0;
    ULONGLONG sampledAt = 0;
    bool failed = false;
    double sumMs = 0.0;                    // since the 10 s line: the sampled presents' twins, in all
    uint32_t samples = 0, timed = 0;
};
TwinTimes g_twinTimes;
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
struct FxDepth;
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
    // the targets as bound (bound again after an effect's twin): how many, the handles (`single`: one, n in a row), the depth-stencil view's
    unsigned nRt = 0;
    bool single = false, hasDsv = false;
    D3D12_CPU_DESCRIPTOR_HANDLE rts[8] = {}, dsv = {};
    // the effects drawn on this list since its reset or its last step: the game's weighted-blended colour target and its additive one, and
    // the depths of ours their twins drew into (each cleared before its first)
    ID3D12Resource* accum = nullptr;
    ID3D12Resource* glow = nullptr;
    FxDepth* accumZ = nullptr;
    FxDepth* glowZ = nullptr;
    void FxDone() { accum = glow = nullptr; accumZ = glowZ = nullptr; }
    void Clear() {
        tracked = true;
        nHeaps = 0;
        cRoot = gRoot = 0;
        cSet = gSet = 0;
        pso = nullptr;
        ds = rt0 = scene = sceneDs = nullptr;
        states.clear();
        nRt = 0;
        single = hasDsv = false;
        FxDone();
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
    const CpuTime cpuTime(g_cpuSinks);
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

void OnTargets(ID3D12GraphicsCommandList* cl, unsigned n, const D3D12_CPU_DESCRIPTOR_HANDLE* rts, bool single, ID3D12Resource* ds,
               const D3D12_CPU_DESCRIPTOR_HANDLE* dsv) {
    if (!Wanted()) return;
    const CpuTime cpuTime(g_cpuSinks);
    List* l = Of(cl);
    l->ds = ds;
    l->rt0 = n && rts ? natived3d::RtvResource(rts[0].ptr) : nullptr;
    l->nRt = rts ? std::min(n, 8u) : 0;
    l->single = single;
    for (unsigned i = 0; i < (single ? std::min(l->nRt, 1u) : l->nRt); ++i) l->rts[i] = rts[i];
    l->hasDsv = dsv != nullptr;
    if (dsv) l->dsv = *dsv;
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
    const CpuTime cpuTime(g_cpuSinks);
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
    const CpuTime cpuTime(g_cpuSinks);
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
    const CpuTime cpuTime(g_cpuSinks);
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
void OnDraw(ID3D12GraphicsCommandList* cl, bool indexed, unsigned n, unsigned inst, unsigned first, int base, unsigned firstInst);

// ---- our step: its shader, root signature, pipeline, heap, texture, timestamps (one device)
const char kCs[] = R"(
cbuffer K : register(b0) {
    float plane, density, nearPlane, heightOn;
    float3 fogColour; float top;
    float3 up; float soft;
    float sx, sy, offX, curveOn;
    uint width, height; float cStart, cEnd;
    float cCurve, cStrength, easeOn, byDepth;
    uint glowW, glowH; float holes, skyDepth;
};
Texture2D<float> Depth : register(t0);
Texture2D<float3> Scene : register(t1);
Texture2D<float> AccumDepth : register(t2);   // [fog] game_fog_fx_depth: the nearest effect drawn into the weighted-blended targets
Texture2D<float4> Accum : register(t3);       // ... their colour target (rgb weighted, a the weights' sum)
Texture2D<float> GlowDepth : register(t4);    // ... the nearest drawn into the half-size additive target
Texture2D<float3> Glow : register(t5);
RWTexture2D<float3> Out : register(u0);
RWTexture2D<float4> OutAccum : register(u1);
RWTexture2D<float3> OutGlow : register(u2);
// What the fog leaves of a point at the depth d (reverse-Z, > 0) seen at uv in the frame
float Keep(float2 uv, float d) {
    const float z = nearPlane / d;
    const float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    const float3 v = float3((ndc.x - offX) / sx * z, ndc.y / sy * z, z);
    // (the game's: its view position's w is 1; our effect's fog, Params::ours: the view's depth, as D2R_DepthFog.fx FogAmount)
    const float dist = byDepth > 0.5 ? z : sqrt(dot(v, v) + 1.0);
    float t = curveOn > 0.5 ? 1.0 - pow(saturate((dist - cStart) / max(cEnd - cStart, 1.0)), cCurve) * cStrength * UnleashedFogEase(dist, cStart, easeOn)   // ([fog] game_fog_our_curve, ease_in)
                            : min(1.0, exp(-density * (dist / plane - 1.0)));
    if (heightOn > 0.5) t = 1.0 - (1.0 - t) * saturate((top - dot(v, up)) / soft);
    return t;
}
// The void, or the game's dome ([sky] game_sky, Params::skyDepth) at p: the depth is the view's (near / the depth along the view), so off the
// view's middle the dome's is near / (radius x cos) - the pixel's view ray at depth 1 (its length 1 / cos) brings it back, as D2R_DepthFog.fx
// DomeAt. By the depth alone only the dome within 11.5 degrees of the view's middle was the sky (cos 0.98) and the rest of it was fogged as
// ground: Serg on 0.192.39 (126.webp, "туман ужасен") - a ten-sided disc straight ahead in the dome's own colours, the sky round it fogged.
// The game's fog only: our effect's fog in the step (byDepth) keeps the depth alone, as it was (Serg: the old fog not touched)
bool SkyAt(uint2 p, float d) {
    const float g = max(skyDepth, 1e-7);
    if (skyDepth <= 0.0 || sx <= 0.0 || sy <= 0.0 || byDepth > 0.5) return d <= g;
    const float2 uv = (float2(p) + 0.5) / float2(width, height);
    const float2 t = float2((uv.x * 2.0 - 1.0 - offX) / sx, (1.0 - uv.y * 2.0) / sy);
    return d <= g * sqrt(1.0 + dot(t, t));
}
// The depth a pixel is fogged by: its own. With Params::holes a pixel of none beside drawn ground - a hole the far trees' dithered edges
// leave in it, or the one-pixel rim round anything against the void, where the game lays its blended edge (Serg 2026-10-31: the far
// trees a band of dark specks; every plank of the far palisade traced in a dark line, 85.png) - takes the farthest of its drawn
// neighbours: fogged as what it borders. The void proper (no neighbour drawn): none (0)
float GroundDepth(uint2 p) {
    const float d = Depth[p];
    if (d > 1e-7 || holes < 0.5) return d;
    // (a neighbour counts when it is ground: not the void, not the game's dome - a hole beside the dome is the sky's)
    const uint2 m = uint2(width - 1, height - 1);
    const float n0 = Depth[uint2(min(p.x + 1, m.x), p.y)], n1 = Depth[uint2(p.x > 0 ? p.x - 1 : 0, p.y)];
    const float n2 = Depth[uint2(p.x, min(p.y + 1, m.y))], n3 = Depth[uint2(p.x, p.y > 0 ? p.y - 1 : 0)];
    const float farthest = min(min(SkyAt(p, n0) ? 1e9 : n0, SkyAt(p, n1) ? 1e9 : n1), min(SkyAt(p, n2) ? 1e9 : n2, SkyAt(p, n3) ? 1e9 : n3));
    return farthest < 1e8 ? farthest : 0.0;
}
// What the fog leaves of a pixel at the depth d: the sky (the void, the game's dome) all of it
float KeepAt(uint2 p, float2 uv, float d) { return SkyAt(p, d) ? 1.0 : Keep(uv, d); }
// The share of `o` in c, a blend of `own` and `o` (where c lies along the line between them); `none` when the two are alike
float ShareOf(float3 c, float3 own, float3 o, float none) {
    const float3 e = o - own;
    const float l = dot(e, e);
    return l > 1e-6 ? saturate(dot(c - own, e) / l) : none;
}
// The game's fog (Params::ours off): a pixel on an edge between two fogs. The pixels along a silhouette hold the object and what is behind
// it blended (the game's soft edges, leaves and planks drawn over the dome) but only one depth: fogged whole by the object's, the sky or the
// far ground in it took the object's fog (a light line against the sky); left whole as the sky, the object in it stayed unfogged (a dark
// line round the fogged object) - Serg on 0.192.34 - 0.192.40 (119, 121, 126.webp: "чтобы туман игры был без полос вокруг объектов"). Where
// the 3 x 3 round a pixel holds two fogs (what the fog leaves differing by over 0.05), its neighbours on the other side give that side's
// colour O and fog kO, those on its own side its own colour; the pixel's share s of O is fogged by kO, the rest by its own depth - so it
// lies between its two neighbours as fogged. A pixel of no depth beside drawn ground and no sky (a hole the far trees' dithered edges leave,
// a band of dark specks; with the game's dome shown the sky has its depth, so no depth is never the sky there): fogged as the farthest
// ground beside it, as before (Params::holes)
bool SkyProper(uint2 p, float d) { return skyDepth > 0.0 && d > 1e-7 && SkyAt(p, d); }   // (the game's dome: the sky has a depth)
float3 GameFogPixel(uint2 p, float3 c) {
    const float2 uv = (float2(p) + 0.5) / float2(width, height);
    const int2 m = int2(width - 1, height - 1);
    const float d0 = Depth[p];
    // (alike depths round it - the most of the picture: its own fog, no more reads)
    float dn[8];
    float dLo = d0, dHi = d0;
    [unroll] for (int i = 0; i < 8; ++i) {
        const int j = i < 4 ? i : i + 1;
        dn[i] = Depth[uint2(clamp(int2(p) + int2(j % 3 - 1, j / 3 - 1), int2(0, 0), m))];
        dLo = min(dLo, dn[i]);
        dHi = max(dHi, dn[i]);
    }
    const float k0 = KeepAt(p, uv, d0);
    if (dHi - dLo <= 0.02 * dHi) return lerp(fogColour, c, k0);
    if (d0 <= 1e-7 && holes > 0.5) {   // (a hole: no sky proper beside it, drawn ground beside it)
        float far = 1e9;
        bool sky = false;
        [unroll] for (int i = 0; i < 8; ++i) {
            if (SkyProper(p, dn[i])) sky = true;
            else if (!SkyAt(p, dn[i])) far = min(far, dn[i]);
        }
        if (!sky && far < 1e8) return lerp(fogColour, c, Keep(uv, far));
    }
    float kn[8];
    float kLo = k0, kHi = k0;
    [unroll] for (int i = 0; i < 8; ++i) {
        kn[i] = KeepAt(p, uv, dn[i]);
        kLo = min(kLo, kn[i]);
        kHi = max(kHi, kn[i]);
    }
    if (kHi - kLo <= 0.05) return lerp(fogColour, c, k0);
    const float mid = 0.5 * (kLo + kHi);
    const bool ownHigh = k0 > mid;
    float3 own = 0.0, o = 0.0;
    float nOwn = 0.0, nO = 0.0, kO = 0.0;
    [unroll] for (int i = 0; i < 8; ++i) {
        if (skyDepth > 0.0 && dn[i] <= 1e-7) continue;   // (no depth under the dome: a blend itself, no side's colour)
        const int j = i < 4 ? i : i + 1;
        const float3 q = Scene[uint2(clamp(int2(p) + int2(j % 3 - 1, j / 3 - 1), int2(0, 0), m))];
        if ((kn[i] > mid) == ownHigh) { own += q; nOwn += 1.0; }
        else { o += q; nO += 1.0; kO += kn[i]; }
    }
    if (nO < 0.5) return lerp(fogColour, c, k0);
    own = nOwn > 0.5 ? own / nOwn : c;
    o /= nO;
    kO /= nO;
    return lerp(fogColour, c, k0) + ShareOf(c, own, o, 0.0) * (kO - k0) * (o - fogColour);
}
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= width || id.y >= height) return;
    const float3 c = Scene[id.xy];
    if (byDepth < 0.5) { Out[id.xy] = GameFogPixel(id.xy, c); return; }   // (the game's fog; our effect's below, as it was)
    const float d = GroundDepth(id.xy);
    // the void, and the game's dome ([sky] game_sky, Params::skyDepth: it stands past the game's linear depth's far end, and the game's
    // pass leaves it - Serg 2026-10-31 on 0.192.19, "обводка вокруг объектов": its low part right behind the trees fogged to the fog's
    // colour, a light band round every tree against the dark sky): left as they are
    if (SkyAt(id.xy, d)) { Out[id.xy] = c; return; }
    Out[id.xy] = lerp(fogColour, c, Keep((id.xy + 0.5) / float2(width, height), d));
}
// The effects at their own depth, never nearer than the scene behind them (an effect hidden there adds nothing); none drawn here (our
// depth 0): as they are. The weighted-blended colour toward the fog's colour (its weight kept: the resolve divides by it), the additive
// light down. Over the void (the sky goes there after) the colour fades out rather than into the fog's colour: a far fire's halo above
// the palisade it stands on fades with its lower half (Serg's 0.192.1 shot: an orange disc on the horizon)
[numthreads(8, 8, 1)]
void fxAccum(uint3 id : SV_DispatchThreadID) {
    if (id.x >= width || id.y >= height) return;
    const float4 a = Accum[id.xy];
    const float e = AccumDepth[id.xy], o = GroundDepth(id.xy);   // (a blended edge on the rim against the void: as the ground it borders)
    const float t = a.a > 0.0 && e > 1e-7 ? Keep((id.xy + 0.5) / float2(width, height), max(e, o)) : 1.0;
    OutAccum[id.xy] = float4(a.rgb * t + (!SkyAt(id.xy, o) ? fogColour * (a.a * (1.0 - t)) : 0.0), a.a);   // (over the sky: fades out)
}
[numthreads(8, 8, 1)]
void fxGlow(uint3 id : SV_DispatchThreadID) {
    if (id.x >= glowW || id.y >= glowH) return;
    const float3 g = Glow[id.xy];
    const float e = GlowDepth[id.xy];
    // (the scene's depth under it: the nearest of the full-size pixels it covers)
    const uint2 f0 = min(uint2(float2(id.xy) * float2(width, height) / float2(glowW, glowH)), uint2(width - 1, height - 1));
    const uint2 f1 = min(f0 + 1, uint2(width - 1, height - 1));
    const float o = max(max(Depth[f0], Depth[uint2(f1.x, f0.y)]), max(Depth[uint2(f0.x, f1.y)], Depth[f1]));
    const float t = e > 1e-7 ? Keep((id.xy + 0.5) / float2(glowW, glowH), max(e, o)) : 1.0;
    OutGlow[id.xy] = g * t;
}
)";
struct Consts {
    float plane, density, nearPlane, heightOn;
    float fog[3], top;
    float up[3], soft;
    float sx, sy, offX, curveOn;
    uint32_t width, height;
    float cStart, cEnd;
    float cCurve, cStrength, easeOn, byDepth;
    uint32_t glowW, glowH;
    float holes, skyDepth;
};
static_assert(sizeof(Consts) == 28 * 4, "twenty-eight root constants");

constexpr int kSlots = 64;   // descriptor sets and timestamp pairs used in turn (the GPU is never that many steps behind)
constexpr UINT kViews = 9;   // a step's views: t0 - t5, u0 - u2
struct Gpu {
    ID3D12Device* dev = nullptr;
    ID3D12RootSignature* root = nullptr;
    ID3D12PipelineState* cs = nullptr;
    ID3D12PipelineState* csAccum = nullptr;   // the effects' (fxAccum, fxGlow)
    ID3D12PipelineState* csGlow = nullptr;
    ID3D12DescriptorHeap* heap = nullptr;
    UINT inc = 0;
    ID3D12Resource* tex = nullptr;
    UINT64 w = 0;
    UINT h = 0;
    // the effects' weighted-blended colour and additive light as fogged (Params::fxDepth), as big as the game's and of their formats
    ID3D12Resource* accumTex = nullptr;
    UINT64 accumW = 0;
    UINT accumH = 0;
    DXGI_FORMAT accumF = DXGI_FORMAT_UNKNOWN;
    ID3D12Resource* glowTex = nullptr;
    UINT64 glowW = 0;
    UINT glowH = 0;
    DXGI_FORMAT glowF = DXGI_FORMAT_UNKNOWN;
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
    ID3DBlob *code = nullptr, *codeAccum = nullptr, *codeGlow = nullptr, *err = nullptr;
    const std::string source = std::string(unleashed::fog::kEaseHlsl) + kCs;
    const char* entries[3] = {"main", "fxAccum", "fxGlow"};
    ID3DBlob** codes[3] = {&code, &codeAccum, &codeGlow};
    for (int i = 0; i < 3; ++i) {
        if (!compile || FAILED(compile(source.data(), source.size(), "vrcam-fog-before-fx", nullptr, nullptr, entries[i], "cs_5_0",
                                       D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, codes[i], &err))) {
            Line("vrcam: fog before the effects - our shader did not compile: %s", err ? (const char*)err->GetBufferPointer() : "no d3dcompiler_47.dll");
            if (err) err->Release();
            for (ID3DBlob** c : codes)
                if (*c) (*c)->Release();
            if (lib) FreeLibrary(lib);
            g.failed = true;
            return false;
        }
    }
    D3D12_DESCRIPTOR_RANGE ranges[2] = {{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 6, 0, 0, 0}, {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 3, 0, 0, 0}};
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
    pd.CS = {codeAccum->GetBufferPointer(), codeAccum->GetBufferSize()};
    ok = ok && SUCCEEDED(dev->CreateComputePipelineState(&pd, __uuidof(ID3D12PipelineState), (void**)&g.csAccum));
    pd.CS = {codeGlow->GetBufferPointer(), codeGlow->GetBufferSize()};
    ok = ok && SUCCEEDED(dev->CreateComputePipelineState(&pd, __uuidof(ID3D12PipelineState), (void**)&g.csGlow));
    natived3d::SetOurs(false);
    code->Release();
    codeAccum->Release();
    codeGlow->Release();
    // Only now: the blobs are d3dcompiler_47's objects - their code lives in it. Freed before them (until 0.192.29) on a PC where nothing else
    // held the compiler (PC2: FlatVR, no ReShade), it was unloaded and the first GetBufferPointer read its gone vtable: the game crashed at the
    // first stereo pair with fog before the effects on.
    if (lib) FreeLibrary(lib);
    const D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kViews * kSlots, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
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

// ---- [fog] game_fog_fx_depth: the effects' own depth
// A pipeline of the game's that may draw an effect - blended into its first target (a float colour target with alpha: the weighted-blended
// colour; or R11G11B10: the additive light), no depth written, one sample, no mesh shaders - kept as made: its shaders before the pixel's
// (copies), its input layout, root signature (held), rasterizer and topology. Its twin, made at its first draw into the effects' targets,
// draws only the depth (the nearest: reverse-Z, greater-equal) into a buffer of ours.
struct Twin {
    std::vector<uint8_t> vs, gs, hs, ds;
    std::vector<D3D12_INPUT_ELEMENT_DESC> layout;
    std::vector<std::string> names;   // (the layout's semantic names: copies)
    ID3D12RootSignature* root = nullptr;
    D3D12_RASTERIZER_DESC raster{};
    D3D12_PRIMITIVE_TOPOLOGY_TYPE topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    D3D12_INDEX_BUFFER_STRIP_CUT_VALUE cut = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
    UINT nodeMask = 0;
    std::atomic<ID3D12PipelineState*> made{nullptr};
    bool failed = false;   // (under g_twinLock)
};
SRWLOCK g_twinLock = SRWLOCK_INIT;
std::unordered_map<ID3D12PipelineState*, Twin*> g_twins;   // (the game's pipeline held: its address never another's)
std::atomic<uint32_t> g_twinsKept{0};

bool EffectTarget(DXGI_FORMAT f) {
    return f == DXGI_FORMAT_R16G16B16A16_FLOAT || f == DXGI_FORMAT_R32G32B32A32_FLOAT || f == DXGI_FORMAT_R11G11B10_FLOAT;
}
// A target's resource may be typeless (its views typed): the float format it is drawn and read as
DXGI_FORMAT Typed(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R32G32B32A32_TYPELESS: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    default: return f;
    }
}
bool MayDrawEffect(const D3D12_BLEND_DESC& b, const D3D12_DEPTH_STENCIL_DESC& d, UINT nRt, DXGI_FORMAT rt0, DXGI_SAMPLE_DESC s) {
    return nRt >= 1 && EffectTarget(rt0) && b.RenderTarget[0].BlendEnable && s.Count == 1 &&
           (!d.DepthEnable || d.DepthWriteMask == D3D12_DEPTH_WRITE_MASK_ZERO);
}
std::vector<uint8_t> Copy(const D3D12_SHADER_BYTECODE& c) {
    return c.pShaderBytecode && c.BytecodeLength ? std::vector<uint8_t>((const uint8_t*)c.pShaderBytecode, (const uint8_t*)c.pShaderBytecode + c.BytecodeLength)
                                                 : std::vector<uint8_t>();
}
void Keep(ID3D12PipelineState* pso, Twin* t, const D3D12_INPUT_LAYOUT_DESC& il) {
    t->names.reserve(il.NumElements);
    for (UINT i = 0; i < il.NumElements && il.pInputElementDescs; ++i) {
        t->layout.push_back(il.pInputElementDescs[i]);
        t->names.emplace_back(il.pInputElementDescs[i].SemanticName ? il.pInputElementDescs[i].SemanticName : "");
    }
    for (size_t i = 0; i < t->layout.size(); ++i) t->layout[i].SemanticName = t->names[i].c_str();
    if (t->root) t->root->AddRef();
    pso->AddRef();
    AcquireSRWLockExclusive(&g_twinLock);
    Twin*& slot = g_twins[pso];
    Twin* old = slot;
    slot = t;
    ReleaseSRWLockExclusive(&g_twinLock);
    if (old) { pso->Release(); delete old; }   // (the same pipeline told again: one hold)
    else g_twinsKept.fetch_add(1, std::memory_order_relaxed);
}
Twin* TwinOf(ID3D12PipelineState* pso) {
    if (!pso) return nullptr;
    AcquireSRWLockShared(&g_twinLock);
    const auto it = g_twins.find(pso);
    Twin* t = it != g_twins.end() ? it->second : nullptr;
    ReleaseSRWLockShared(&g_twinLock);
    return t;
}
// The twin itself (made once, under g_twinLock); nullptr when the device would not make it
ID3D12PipelineState* MadeTwin(ID3D12Device* dev, Twin* t) {
    if (ID3D12PipelineState* m = t->made.load(std::memory_order_acquire)) return m;
    AcquireSRWLockExclusive(&g_twinLock);
    if (!t->made.load(std::memory_order_relaxed) && !t->failed) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
        d.pRootSignature = t->root;
        d.VS = {t->vs.data(), t->vs.size()};
        if (!t->gs.empty()) d.GS = {t->gs.data(), t->gs.size()};
        if (!t->hs.empty()) d.HS = {t->hs.data(), t->hs.size()};
        if (!t->ds.empty()) d.DS = {t->ds.data(), t->ds.size()};
        d.SampleMask = ~0u;
        d.RasterizerState = t->raster;
        d.RasterizerState.ForcedSampleCount = 0;
        d.DepthStencilState.DepthEnable = TRUE;
        d.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        d.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        d.InputLayout = {t->layout.data(), (UINT)t->layout.size()};
        d.IBStripCutValue = t->cut;
        d.PrimitiveTopologyType = t->topology;
        d.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        d.SampleDesc.Count = 1;
        d.NodeMask = t->nodeMask;
        ID3D12PipelineState* made = nullptr;
        natived3d::SetOurs(true);
        const HRESULT hr = dev->CreateGraphicsPipelineState(&d, __uuidof(ID3D12PipelineState), (void**)&made);
        natived3d::SetOurs(false);
        if (FAILED(hr) || !made) {
            t->failed = true;
            static std::atomic<int> told{0};
            if (told.fetch_add(1) < 3) Line("vrcam: fog before the effects - an effect's depth pipeline could not be made (0x%08X): that effect not fogged", (unsigned)hr);
        } else {
            made->SetName(L"d2rvr fog before fx effect depth");
            t->made.store(made, std::memory_order_release);
            g_fxTwinsMade.fetch_add(1, std::memory_order_relaxed);
        }
    }
    ID3D12PipelineState* got = t->made.load(std::memory_order_relaxed);
    ReleaseSRWLockExclusive(&g_twinLock);
    return got;
}

// Depths of ours the twins draw into (D32, cleared to 0: nothing), one for each size the effects' targets have - kept (a few: both eyes'
// targets are one size, the half-size layer another, a menu's besides), never let go while the GPU may use them
struct FxDepth {
    ID3D12Resource* tex = nullptr;
    UINT64 w = 0;
    UINT h = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
};
constexpr int kFxDepths = 8;
FxDepth g_fxDepths[kFxDepths];
int g_fxDepthN = 0;
ID3D12DescriptorHeap* g_dsvHeap = nullptr;
std::mutex g_fxDepthMx;
FxDepth* DepthFor(ID3D12Device* dev, UINT64 w, UINT h) {
    std::lock_guard<std::mutex> one(g_fxDepthMx);
    for (int i = 0; i < g_fxDepthN; ++i)
        if (g_fxDepths[i].w == w && g_fxDepths[i].h == h) return &g_fxDepths[i];
    if (g_fxDepthN == kFxDepths) return nullptr;
    if (!g_dsvHeap) {
        const D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_DSV, kFxDepths, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
        if (FAILED(dev->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void**)&g_dsvHeap))) { g_dsvHeap = nullptr; return nullptr; }
    }
    FxDepth& z = g_fxDepths[g_fxDepthN];
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = w;
    d.Height = h;
    d.DepthOrArraySize = d.MipLevels = 1;
    d.Format = DXGI_FORMAT_R32_TYPELESS;
    d.SampleDesc.Count = 1;
    d.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE cv{DXGI_FORMAT_D32_FLOAT};
    cv.DepthStencil = {0.0f, 0};
    // (its home state DEPTH_WRITE: the step takes it to a shader resource and back within itself)
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_DEPTH_WRITE, &cv, __uuidof(ID3D12Resource),
                                            (void**)&z.tex)))
        return nullptr;
    z.tex->SetName(L"d2rvr fog before fx effects depth");
    z.w = w;
    z.h = h;
    z.dsv.ptr = g_dsvHeap->GetCPUDescriptorHandleForHeapStart().ptr +
                (SIZE_T)g_fxDepthN * dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    D3D12_DEPTH_STENCIL_VIEW_DESC dv{DXGI_FORMAT_D32_FLOAT, D3D12_DSV_DIMENSION_TEXTURE2D};
    dev->CreateDepthStencilView(z.tex, &dv, z.dsv);
    ++g_fxDepthN;
    Line("vrcam: fog before the effects - an effects' depth of ours made, %llux%u", (unsigned long long)w, h);
    return &z;
}

// The effects drawn this frame, on any list: the game records them on another list than its resolve's (PC1 2026-10-31 16:03, 0.192.21:
// 489 091 draws drawn again in 10 s, "0 steps fogged the effects by it" - the step looked on its own list only). Each kind's target and
// our depth of it, and the present it was drawn in; the step takes them while this present's or the last one's.
struct FxSeen {
    ID3D12Resource* game = nullptr;
    FxDepth* z = nullptr;
    uint32_t frame = 0;
};
SRWLOCK g_fxSeenLock = SRWLOCK_INIT;
FxSeen g_fxSeen[2];   // [0] the weighted-blended colour, [1] the additive light

// An effect's draw (before the game's, its arguments): drawn again by its twin into our depth of its target's size - the weighted-blended
// colour target (a float one with alpha, the depth's size), or the additive light's (R11G11B10, smaller than the depth); any other target
// (the scene itself: a flame drawn into it is fogged with it) is left alone. The game's pipeline and targets bound again after it; its root
// signature, arguments, viewports, vertex and index buffers never changed.
void TwinDraw(ID3D12GraphicsCommandList* cl, List& l, Twin* t, bool indexed, unsigned n, unsigned inst, unsigned first, int base, unsigned firstInst) {
    const CpuTime cpuTime(g_cpuTwins);
    if (!l.rt0 || !l.ds || !l.nRt) { g_fxOther.fetch_add(1, std::memory_order_relaxed); return; }
    const D3D12_RESOURCE_DESC rd = l.rt0->GetDesc(), dd = l.ds->GetDesc();
    const DXGI_FORMAT rf = Typed(rd.Format);
    const bool accum = rf != DXGI_FORMAT_R11G11B10_FLOAT && rd.Width == dd.Width && rd.Height == dd.Height;
    const bool glow = rf == DXGI_FORMAT_R11G11B10_FLOAT && l.nRt == 1 && rd.Width < dd.Width && rd.Height < dd.Height;
    if ((!accum && !glow) || rd.SampleDesc.Count != 1 || !EffectTarget(rf)) { g_fxOther.fetch_add(1, std::memory_order_relaxed); return; }
    ID3D12Device* dev = nullptr;
    if (FAILED(cl->GetDevice(__uuidof(ID3D12Device), (void**)&dev)) || !dev) return;
    dev->Release();   // (the game's device, held by the game)
    ID3D12PipelineState* twin = MadeTwin(dev, t);
    FxDepth* z = twin ? DepthFor(dev, rd.Width, rd.Height) : nullptr;
    if (!z) { g_fxOther.fetch_add(1, std::memory_order_relaxed); return; }
    {   // the targets each kind was found in, once each
        static ID3D12Resource* told[2] = {};
        if (told[glow] != l.rt0) {
            told[glow] = l.rt0;
            Line("vrcam: fog before the effects - the effects' %s target %p, format %d, %llux%u (%u target(s) bound; the depth %llux%u): their depth drawn",
                 glow ? "additive light" : "weighted-blended colour", (void*)l.rt0, (int)rd.Format, (unsigned long long)rd.Width, rd.Height, l.nRt,
                 (unsigned long long)dd.Width, dd.Height);
        }
    }
    FxDepth*& mine = glow ? l.glowZ : l.accumZ;
    natived3d::SetOurs(true);
    if (mine != z) cl->ClearDepthStencilView(z->dsv, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);   // (its first on this list)
    cl->SetPipelineState(twin);
    cl->OMSetRenderTargets(0, nullptr, FALSE, &z->dsv);
    TwinTimes& tt = g_twinTimes;
    const uint32_t stamp = tt.sampling.load(std::memory_order_relaxed) ? tt.used.fetch_add(1, std::memory_order_relaxed) : kTwinStamps;
    if (stamp < kTwinStamps) cl->EndQuery(tt.heap, D3D12_QUERY_TYPE_TIMESTAMP, 2 * stamp);
    if (indexed) cl->DrawIndexedInstanced(n, inst, first, base, firstInst);
    else cl->DrawInstanced(n, inst, first, firstInst);
    if (stamp < kTwinStamps) cl->EndQuery(tt.heap, D3D12_QUERY_TYPE_TIMESTAMP, 2 * stamp + 1);
    cl->OMSetRenderTargets(l.nRt, l.rts, l.single ? TRUE : FALSE, l.hasDsv ? &l.dsv : nullptr);
    cl->SetPipelineState(l.pso);
    natived3d::SetOurs(false);
    mine = z;
    (glow ? l.glow : l.accum) = l.rt0;
    AcquireSRWLockExclusive(&g_fxSeenLock);
    g_fxSeen[glow] = {l.rt0, z, g_frame.load(std::memory_order_relaxed)};
    ReleaseSRWLockExclusive(&g_fxSeenLock);
    g_fxTwinDraws.fetch_add(1, std::memory_order_relaxed);
}

// The state a target the resolve reads is in: certain when this list set it; else a read state set on another list this frame (see Insert)
bool ReadState(const List& l, ID3D12Resource* r, D3D12_RESOURCE_STATES* s) {
    const D3D12_RESOURCE_STATES readable = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    Known k{};
    const bool own = OwnState(l, r, &k);
    if (!own && !(AnyState(r, &k) && k.frame == g_frame.load(std::memory_order_relaxed))) return false;
    *s = k.s;
    return (k.s & D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) && !(k.s & ~readable);
}
// One of ours as big as a target of the game's and of its format, written by a compute shader (a new size or format: made again - the old
// one's last step long done, as the scene's)
bool MakeOwn(ID3D12Resource*& tex, UINT64& w, UINT& h, DXGI_FORMAT& f, UINT64 nw, UINT nh, DXGI_FORMAT nf, const wchar_t* name) {
    if (tex && w == nw && h == nh && f == nf) return true;
    if (tex) {
        tex->Release();
        tex = nullptr;
    }
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = nw;
    d.Height = nh;
    d.DepthOrArraySize = d.MipLevels = 1;
    d.Format = nf;
    d.SampleDesc.Count = 1;
    d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (FAILED(g_gpu.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, __uuidof(ID3D12Resource),
                                                  (void**)&tex))) {
        tex = nullptr;
        return false;
    }
    tex->SetName(name);
    w = nw;
    h = nh;
    f = nf;
    return true;
}
// An effects' target to fog at the step: the game's, its state, ours, our depth of it
struct FxPart {
    ID3D12Resource* game = nullptr;
    D3D12_RESOURCE_STATES s{}, r{};
    ID3D12Resource* ours = nullptr;
    FxDepth* z = nullptr;
    D3D12_RESOURCE_DESC d{};
};

// The step, before the resolve's draw on `cl` (the game's state given back after it)
void Insert(ID3D12GraphicsCommandList* cl, List& l) {
    const CpuTime cpuTime(g_cpuStep);
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
    // [fog] game_fog_fx_depth: the effects drawn on this list (their twins drew their depth): their targets as the scene's - one slice, one
    // sample, the weighted-blended colour the depth's size, the additive light smaller; each in a read state this list set or set elsewhere
    // this frame. Anything else: those effects as they are (a line, once each way)
    FxPart fa, fg;
    ID3D12Resource* accum = l.accum;
    ID3D12Resource* glow = l.glow;
    FxDepth* accumZ = l.accumZ;
    FxDepth* glowZ = l.glowZ;
    bool elsewhere = false;   // (taken from another list's twins)
    if (p.fxDepth && (!accum || !glow)) {
        const uint32_t now = g_frame.load(std::memory_order_relaxed);
        AcquireSRWLockShared(&g_fxSeenLock);
        const FxSeen sa = g_fxSeen[0], sg = g_fxSeen[1];
        ReleaseSRWLockShared(&g_fxSeenLock);
        if (!accum && sa.game && now - sa.frame <= 1) { accum = sa.game; accumZ = sa.z; elsewhere = true; }
        if (!glow && sg.game && now - sg.frame <= 1) { glow = sg.game; glowZ = sg.z; elsewhere = true; }
    }
    if (p.fxDepth && (accum || glow)) {
        int why = 0;   // 0 fogged; else the first refusal
        auto take = [&](FxPart& f, ID3D12Resource* game, FxDepth* z, bool glow) {
            if (!game || !z) return;
            const D3D12_RESOURCE_DESC d = game->GetDesc();
            D3D12_RESOURCE_STATES s{};
            if (d.SampleDesc.Count != 1 || d.DepthOrArraySize != 1 || d.Width != z->w || d.Height != z->h ||
                (glow ? d.Width >= sd.Width : d.Width != sd.Width || d.Height != sd.Height)) { why = why ? why : glow ? 2 : 1; return; }
            if (!ReadState(l, game, &s)) { why = why ? why : glow ? 4 : 3; return; }
            ID3D12Resource*& tex = glow ? g.glowTex : g.accumTex;
            const DXGI_FORMAT tf = Typed(d.Format);   // (a copy between a typeless resource and its typed format is allowed)
            const bool made = glow ? MakeOwn(tex, g.glowW, g.glowH, g.glowF, d.Width, d.Height, tf, L"d2rvr fog before fx glow")
                                   : MakeOwn(tex, g.accumW, g.accumH, g.accumF, d.Width, d.Height, tf, L"d2rvr fog before fx accum");
            if (!made) { why = why ? why : 5; return; }
            f.game = game;
            f.s = s;
            f.r = s | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            f.ours = tex;
            f.z = z;
            f.d = d;
        };
        take(fa, accum, accumZ, false);
        take(fg, glow, glowZ, true);
        static int toldWhy = -1;
        static bool toldElsewhere = false;
        if (why != toldWhy || elsewhere != toldElsewhere) {
            toldWhy = why;
            toldElsewhere = elsewhere;
            static const char* const kWhy[] = {"fogged by their own distance", "the weighted-blended target not of the scene's size (or arrays, samples): as they are",
                                               "the additive target not smaller than the scene (or arrays, samples): as it is",
                                               "the weighted-blended target's state not known as the resolve reads it: as they are",
                                               "the additive target's state not known as the resolve reads it: as it is",
                                               "a texture of ours could not be made: as they are"};
            Line("vrcam: fog before the effects - the effects drawn %s: %s (colour %p, additive %p)",
                 elsewhere ? "on another list this frame" : "on the resolve's list", kWhy[why], (void*)fa.game, (void*)fg.game);
        }
        (fa.game || fg.game ? g_fxFogged : g_fxNotFogged).fetch_add(1, std::memory_order_relaxed);
    }
    if (p.fxDepth) {   // (no step taking the effects' depth for long: the twins wait - OnDraw)
        if (fa.game || fg.game) g_fxIdle.store(0, std::memory_order_relaxed);
        else g_fxIdle.fetch_add(1, std::memory_order_relaxed);
    }
    const uint32_t step = g_slot++;
    const uint32_t k = step % kSlots;
    // this step's views: t0 the depth, t1 the scene, t2 - t5 the effects' depths and targets, u0 - u2 ours (the effects' none: empty views)
    const D3D12_CPU_DESCRIPTOR_HANDLE cpu{g.heap->GetCPUDescriptorHandleForHeapStart().ptr + (SIZE_T)k * kViews * g.inc};
    const D3D12_GPU_DESCRIPTOR_HANDLE gpu{g.heap->GetGPUDescriptorHandleForHeapStart().ptr + (UINT64)k * kViews * g.inc};
    auto at = [&](UINT i) { return D3D12_CPU_DESCRIPTOR_HANDLE{cpu.ptr + (SIZE_T)i * g.inc}; };
    D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    sv.Format = dfmt;
    dev->CreateShaderResourceView(depth, &sv, at(0));
    sv.Format = DXGI_FORMAT_R11G11B10_FLOAT;
    dev->CreateShaderResourceView(scene, &sv, at(1));
    sv.Format = DXGI_FORMAT_R32_FLOAT;
    dev->CreateShaderResourceView(fa.game ? fa.z->tex : nullptr, &sv, at(2));
    sv.Format = fa.game ? Typed(fa.d.Format) : DXGI_FORMAT_R16G16B16A16_FLOAT;
    dev->CreateShaderResourceView(fa.game, &sv, at(3));
    sv.Format = DXGI_FORMAT_R32_FLOAT;
    dev->CreateShaderResourceView(fg.game ? fg.z->tex : nullptr, &sv, at(4));
    sv.Format = DXGI_FORMAT_R11G11B10_FLOAT;
    dev->CreateShaderResourceView(fg.game, &sv, at(5));
    D3D12_UNORDERED_ACCESS_VIEW_DESC uv{};
    uv.Format = DXGI_FORMAT_R11G11B10_FLOAT;
    uv.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    dev->CreateUnorderedAccessView(g.tex, nullptr, &uv, at(6));
    uv.Format = fa.game ? Typed(fa.d.Format) : DXGI_FORMAT_R16G16B16A16_FLOAT;
    dev->CreateUnorderedAccessView(fa.ours, nullptr, &uv, at(7));
    uv.Format = DXGI_FORMAT_R11G11B10_FLOAT;
    dev->CreateUnorderedAccessView(fg.ours, nullptr, &uv, at(8));

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
    c.easeOn = p.ease ? 1.0f : 0.0f;
    c.byDepth = p.ours ? 1.0f : 0.0f;
    c.glowW = fg.game ? (uint32_t)fg.d.Width : 0;
    c.glowH = fg.game ? fg.d.Height : 0;
    // (the holes and rims: a few reads in the step itself, so with the step - not with game_fog_fx_depth, off as shipped for its cost;
    // our effect's fog: the post pass fogs such a pixel itself - FrameVoidFog)
    c.holes = p.ours ? 0.0f : 1.0f;
    c.skyDepth = std::max(p.skyDepth, 0.0f);

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
    cl->SetComputeRootDescriptorTable(2, D3D12_GPU_DESCRIPTOR_HANDLE{gpu.ptr + 6 * g.inc});
    cl->Dispatch((UINT)((sd.Width + 7) / 8), (sd.Height + 7) / 8, 1);
    FxPart* parts[2] = {&fa, &fg};
    if (fa.game || fg.game) {   // the effects': their targets and our depths read
        D3D12_RESOURCE_BARRIER fb[4];
        UINT nf = 0;
        for (FxPart* f : parts) {
            if (!f->game) continue;
            if (f->r != f->s) fb[nf++] = Transition(f->game, f->s, f->r, 0);
            fb[nf++] = Transition(f->z->tex, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        if (nf) cl->ResourceBarrier(nf, fb);
        if (fa.game) {
            cl->SetPipelineState(g.csAccum);
            cl->Dispatch((UINT)((sd.Width + 7) / 8), (sd.Height + 7) / 8, 1);
        }
        if (fg.game) {
            cl->SetPipelineState(g.csGlow);
            cl->Dispatch((UINT)((fg.d.Width + 7) / 8), (fg.d.Height + 7) / 8, 1);
        }
    }
    D3D12_RESOURCE_BARRIER bx[3 + 6];
    nb = 0;
    bx[nb++] = Transition(g.tex, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    bx[nb++] = Transition(scene, rScene, D3D12_RESOURCE_STATE_COPY_DEST, 0);
    if (rDepth != sDepth) bx[nb++] = Transition(depth, rDepth, sDepth);
    for (FxPart* f : parts) {
        if (!f->game) continue;
        bx[nb++] = Transition(f->ours, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        bx[nb++] = Transition(f->game, f->r, D3D12_RESOURCE_STATE_COPY_DEST, 0);
        bx[nb++] = Transition(f->z->tex, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    }
    cl->ResourceBarrier(nb, bx);
    {   // into the scene's first mip (CopyResource wants the same mips on both sides); the effects' targets the same way
        D3D12_TEXTURE_COPY_LOCATION dst{scene, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX}, src{g.tex, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
        dst.SubresourceIndex = 0;
        src.SubresourceIndex = 0;
        cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        for (FxPart* f : parts) {
            if (!f->game) continue;
            dst.pResource = f->game;
            src.pResource = f->ours;
            cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        }
    }
    nb = 0;
    bx[nb++] = Transition(scene, D3D12_RESOURCE_STATE_COPY_DEST, sScene, 0);
    bx[nb++] = Transition(g.tex, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    for (FxPart* f : parts) {
        if (!f->game) continue;
        bx[nb++] = Transition(f->game, D3D12_RESOURCE_STATE_COPY_DEST, f->s, 0);
        bx[nb++] = Transition(f->ours, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    cl->ResourceBarrier(nb, bx);
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
    static int toldOurs = -1;   // (again when the fog drawn changes: the game's, or our effect's)
    if (g_toldWhy.exchange(0) != 0 || toldOurs != (int)p.ours) {
        toldOurs = (int)p.ours;
        if (p.ours)
            Line("vrcam: fog before the effects - drawn: the scene fogged by our effect's fog (start %.0f, end %.0f, curve %.2f, strength %.2f, colour "
                 "%.3f %.3f %.3f in the scene's units) before the game's transparent resolve (%llux%u); the post pass leaves a drawn pixel's fog to it",
                 c.cStart, c.cEnd, c.cCurve, c.cStrength, c.fog[0], c.fog[1], c.fog[2], (unsigned long long)sd.Width, sd.Height);
        else
            Line("vrcam: fog before the effects - drawn: the scene fogged (plane %.0f, density %.3f, colour %.3f %.3f %.3f) before the game's transparent "
                 "resolve (%llux%u); the game's fog pass leaves the fog's colour out",
                 c.plane, c.density, c.fog[0], c.fog[1], c.fog[2], (unsigned long long)sd.Width, sd.Height);
    }
}
void OnDraw(ID3D12GraphicsCommandList* cl, bool indexed, unsigned n, unsigned inst, unsigned first, int base, unsigned firstInst) {
    if (!Wanted()) return;
    List* l = Of(cl);
    if (!IsResolvePso(l->pso)) {
        const CpuTime cpuTime(g_cpuSinks);   // (the twin's own in g_cpuTwins as well)
        if (!g_fxDepth.load(std::memory_order_relaxed) || !l->tracked) return;
        if (Twin* t = TwinOf(l->pso)) {
            // no step took the effects' depth for kFxIdleSteps in a row: not drawn again, but for a quarter second every 5 s (to see
            // whether a step takes it again - a target found, a state known)
            if (g_fxIdle.load(std::memory_order_relaxed) >= kFxIdleSteps && (GetTickCount64() / 250) % 20 != 0) {
                g_fxSkipped.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            TwinDraw(cl, *l, t, indexed, n, inst, first, base, firstInst);
        }
        return;
    }
    g_drawSeen.fetch_add(1, std::memory_order_relaxed);
    if (!l->tracked) { g_drawUntracked.fetch_add(1, std::memory_order_relaxed); return; }
    Insert(cl, *l);
    l->FxDone();   // (the next effects on this list clear our depths again)
}

}  // namespace

void SetLog(LogFn log) { g_log = log; }

std::atomic<bool> g_hooked{false};
bool Hooked() { return g_hooked.load(std::memory_order_relaxed); }
void FillSinks(natived3d::Sinks& k) {
    g_hooked.store(true);
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
    else if (d && pso && Hooked() && d->VS.pShaderBytecode &&
             MayDrawEffect(d->BlendState, d->DepthStencilState, d->NumRenderTargets, d->RTVFormats[0], d->SampleDesc)) {
        Twin* t = new Twin();
        t->vs = Copy(d->VS);
        t->gs = Copy(d->GS);
        t->hs = Copy(d->HS);
        t->ds = Copy(d->DS);
        t->root = d->pRootSignature;
        t->raster = d->RasterizerState;
        t->topology = d->PrimitiveTopologyType;
        t->cut = d->IBStripCutValue;
        t->nodeMask = d->NodeMask;
        Keep(pso, t, d->InputLayout);
    }
}
void OnStreamPso(ID3D12Device*, const D3D12_PIPELINE_STATE_STREAM_DESC* d, ID3D12PipelineState* pso) {
    const void* code = nullptr;
    size_t size = 0;
    if (pso && fogheight::StreamPixelShader(d, &code, &size) && fogframe::IsResolve(code, size)) { AddResolve(pso); return; }
    if (!pso || !d || !Hooked() || !d->pPipelineStateSubobjectStream || !d->SizeInBytes || d->SizeInBytes > 65536) return;
    // a stream's subobjects, each kind's defaults where it has none (no blend, the depth written: not an effect's)
    const uint8_t* s = (const uint8_t*)d->pPipelineStateSubobjectStream;
    D3D12_SHADER_BYTECODE vs{}, gs{}, hs{}, dsh{};
    ID3D12RootSignature* root = nullptr;
    D3D12_BLEND_DESC blend{};
    D3D12_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable = TRUE;
    depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    D3D12_RASTERIZER_DESC raster{};
    raster.FillMode = D3D12_FILL_MODE_SOLID;
    raster.CullMode = D3D12_CULL_MODE_BACK;
    raster.DepthClipEnable = TRUE;
    D3D12_INPUT_LAYOUT_DESC il{};
    D3D12_INDEX_BUFFER_STRIP_CUT_VALUE cut = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
    D3D12_PRIMITIVE_TOPOLOGY_TYPE topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED;
    D3D12_RT_FORMAT_ARRAY rts{};
    DXGI_SAMPLE_DESC sample{1, 0};
    UINT nodeMask = 0;
    for (size_t at = 0; at + 4 <= d->SizeInBytes;) {
        uint32_t type = 0;
        std::memcpy(&type, s + at, 4);
        size_t vo = 0, sz = 0;
        if (!psostream::Subobject(type, &vo, &sz) || at + sz > d->SizeInBytes) return;   // (a kind not read: left alone)
        const uint8_t* v = s + at + vo;
        switch (type) {
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE: std::memcpy(&root, v, sizeof root); break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS: std::memcpy(&vs, v, sizeof vs); break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS: std::memcpy(&gs, v, sizeof gs); break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS: std::memcpy(&hs, v, sizeof hs); break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS: std::memcpy(&dsh, v, sizeof dsh); break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS: case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS: {   // (mesh shaders, stream output, views: not twinned)
            D3D12_SHADER_BYTECODE b{};
            std::memcpy(&b, v, sizeof b);
            if (b.pShaderBytecode) return;
            break;
        }
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT: {
            D3D12_STREAM_OUTPUT_DESC so{};
            std::memcpy(&so, v, sizeof so);
            if (so.NumEntries) return;
            break;
        }
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING: {
            D3D12_VIEW_INSTANCING_DESC vi{};
            std::memcpy(&vi, v, sizeof vi);
            if (vi.ViewInstanceCount) return;
            break;
        }
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND: std::memcpy(&blend, v, sizeof blend); break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER: std::memcpy(&raster, v, sizeof raster); break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL: std::memcpy(&depth, v, sizeof depth); break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1: {
            D3D12_DEPTH_STENCIL_DESC1 d1{};
            std::memcpy(&d1, v, sizeof d1);
            depth.DepthEnable = d1.DepthEnable;
            depth.DepthWriteMask = d1.DepthWriteMask;
            depth.DepthFunc = d1.DepthFunc;
            break;
        }
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT: std::memcpy(&il, v, sizeof il); break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE: std::memcpy(&cut, v, sizeof cut); break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY: std::memcpy(&topology, v, sizeof topology); break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS: std::memcpy(&rts, v, sizeof rts); break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC: std::memcpy(&sample, v, sizeof sample); break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK: std::memcpy(&nodeMask, v, sizeof nodeMask); break;
        default: break;
        }
        at += sz;
    }
    if (!vs.pShaderBytecode || topology == D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED ||
        !MayDrawEffect(blend, depth, rts.NumRenderTargets, rts.RTFormats[0], sample))
        return;
    Twin* t = new Twin();
    t->vs = Copy(vs);
    t->gs = Copy(gs);
    t->hs = Copy(hs);
    t->ds = Copy(dsh);
    t->root = root;
    t->raster = raster;
    t->topology = topology;
    t->cut = cut;
    t->nodeMask = nodeMask;
    Keep(pso, t, il);
}
void MarkResolve(ID3D12PipelineState* pso) { AddResolve(pso); }

void Configure(const Params& p) {
    AcquireSRWLockExclusive(&g_paramLock);
    g_params = p;
    ReleaseSRWLockExclusive(&g_paramLock);
    g_ours.store(p.on && p.ours, std::memory_order_relaxed);
    g_fxDepth.store(p.on && p.fxDepth, std::memory_order_relaxed);
    g_on.store(p.on, std::memory_order_relaxed);
}

bool Drawn() { return g_on.load(std::memory_order_relaxed) && GetTickCount64() - g_lastDraw.load(std::memory_order_relaxed) < 250; }
bool OursDrawn() { return g_ours.load(std::memory_order_relaxed) && Drawn(); }

// The twins' GPU time, one present a second (the present's thread): 1 its twins get timestamps; 2 two presents on (its lists surely
// submitted) the timestamps resolved into a readback by a list of ours on the game's queue; 3 eight presents on, read
void TwinTimesTick() {
    TwinTimes& tt = g_twinTimes;
    const ULONGLONG now = GetTickCount64();
    if (tt.failed) return;
    switch (tt.stage) {
    case 0: {
        if (!g_fxDepth.load(std::memory_order_relaxed) || now - tt.sampledAt < 1000) return;
        ID3D12Device* dev = nullptr;
        {
            std::lock_guard<std::mutex> one(g_gpuMx);
            dev = g_gpu.dev;
        }
        if (!dev) return;
        if (!tt.heap) {
            const D3D12_QUERY_HEAP_DESC qd{D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 2 * kTwinStamps, 0};
            D3D12_HEAP_PROPERTIES rb{D3D12_HEAP_TYPE_READBACK};
            D3D12_RESOURCE_DESC bd{};
            bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            bd.Width = 2 * kTwinStamps * sizeof(uint64_t);
            bd.Height = bd.DepthOrArraySize = bd.MipLevels = 1;
            bd.SampleDesc.Count = 1;
            bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            void* mapped = nullptr;
            natived3d::SetOurs(true);
            const bool ok = SUCCEEDED(dev->CreateQueryHeap(&qd, __uuidof(ID3D12QueryHeap), (void**)&tt.heap)) &&
                            SUCCEEDED(dev->CreateCommittedResource(&rb, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                                   __uuidof(ID3D12Resource), (void**)&tt.readback)) &&
                            SUCCEEDED(tt.readback->Map(0, nullptr, &mapped)) &&
                            SUCCEEDED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&tt.alloc)) &&
                            SUCCEEDED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, tt.alloc, nullptr, __uuidof(ID3D12GraphicsCommandList),
                                                             (void**)&tt.list)) &&
                            SUCCEEDED(tt.list->Close()) &&
                            SUCCEEDED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&tt.fence));
            natived3d::SetOurs(false);
            if (!ok) {
                tt.failed = true;
                Line("vrcam: fog before the effects - the effects' depth's GPU time cannot be sampled (a query heap or list of ours not made)");
                return;
            }
            tt.data = (const uint64_t*)mapped;
        }
        if (tt.fence->GetCompletedValue() < tt.fenceAt) return;   // (the last resolve still running: later)
        tt.used.store(0, std::memory_order_relaxed);
        tt.sampling.store(true, std::memory_order_relaxed);
        tt.sampledAt = now;
        tt.stage = 1;
        return;
    }
    case 1:
        tt.sampling.store(false, std::memory_order_relaxed);
        tt.pairs = std::min(tt.used.load(std::memory_order_relaxed), kTwinStamps);
        tt.waited = 0;
        tt.stage = tt.pairs ? 2 : 0;
        return;
    case 2: {
        if (++tt.waited < 2) return;
        ID3D12CommandQueue* q = natived3d::GameQueue();
        if (!q || FAILED(tt.alloc->Reset()) || FAILED(tt.list->Reset(tt.alloc, nullptr))) { tt.stage = 0; return; }
        natived3d::SetOurs(true);
        tt.list->ResolveQueryData(tt.heap, D3D12_QUERY_TYPE_TIMESTAMP, 0, 2 * tt.pairs, tt.readback, 0);
        tt.list->Close();
        natived3d::SetOurs(false);
        ID3D12CommandList* lists[1] = {tt.list};
        q->ExecuteCommandLists(1, lists);
        q->Signal(tt.fence, ++tt.fenceAt);
        tt.waited = 0;
        tt.stage = 3;
        return;
    }
    default: {
        if (++tt.waited < 8 && tt.fence->GetCompletedValue() < tt.fenceAt) return;
        if (tt.fence->GetCompletedValue() < tt.fenceAt) return;
        ID3D12CommandQueue* q = natived3d::GameQueue();
        UINT64 hz = 0;
        if (q && SUCCEEDED(q->GetTimestampFrequency(&hz)) && hz) {
            double ms = 0.0;
            uint32_t timed = 0;
            for (uint32_t i = 0; i < tt.pairs; ++i) {
                const uint64_t a = tt.data[2 * i], b = tt.data[2 * i + 1];
                if (b >= a && b - a < hz / 10) { ms += (double)(b - a) * 1000.0 / (double)hz; ++timed; }
            }
            tt.sumMs += ms;
            tt.timed += timed;
            ++tt.samples;
        }
        tt.stage = 0;
        return;
    }
    }
}

void Frame() {
    g_frame.fetch_add(1, std::memory_order_relaxed);
    TwinTimesTick();
    static ULONGLONG toldAt = 0;
    static uint32_t frameAtTold = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - toldAt < 10000) return;
    toldAt = now;
    {   // the cost, a present: the CPU in the sinks, the twins and the step; the twins' GPU time in the sampled presents
        const uint32_t f = g_frame.load(std::memory_order_relaxed), presents = f - frameAtTold;
        frameAtTold = f;
        LARGE_INTEGER hz;
        QueryPerformanceFrequency(&hz);
        const double per = presents ? 1000.0 / (double)hz.QuadPart / presents : 0.0;
        const uint64_t sinks = g_cpuSinks.exchange(0), twins = g_cpuTwins.exchange(0), stepCpu = g_cpuStep.exchange(0);
        TwinTimes& tt = g_twinTimes;
        if (g_on.load() || sinks)
            Line("vrcam: fog before the effects, 10 s, its cost a present (%u presents): on the CPU %.3f ms in the followed lists' calls, of them "
                 "%.3f ms drawing the effects' depth again and %.3f ms recording the step; on the graphics card the effects' depth %.3f ms a "
                 "present (%u presents sampled, %u draws timed)", presents, sinks * per, twins * per, stepCpu * per,
                 tt.samples ? tt.sumMs / tt.samples : 0.0, tt.samples, tt.timed);
        tt.sumMs = 0.0;
        tt.samples = tt.timed = 0;
    }
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
    const bool ours = g_ours.load();
    const uint32_t twinDraws = g_fxTwinDraws.exchange(0), twinsMade = g_fxTwinsMade.exchange(0), other = g_fxOther.exchange(0),
                   fogged = g_fxFogged.exchange(0), notFogged = g_fxNotFogged.exchange(0), skipped = g_fxSkipped.exchange(0);
    if (g_fxDepth.load() || twinDraws)
        Line("vrcam: fog before the effects, 10 s: the effects' own depth - %u draws drawn again for it (%u new depth pipelines, %u of the "
             "game's kept), %u draws of those pipelines into other targets, %u not drawn again (no step took it for %u steps in a row); %u "
             "steps fogged the effects by it, %u with effects not fogged",
             twinDraws, twinsMade, g_twinsKept.load(), other, skipped, kFxIdleSteps, fogged, notFogged);
    Line("vrcam: fog before the effects, 10 s: %u steps before the game's transparent resolve (%s)%s; on the graphics card %.3f ms a step (%d "
         "measured)%s (its pipeline bound %u times on followed lists, %u on others; its draws %u, %u of them on lists not followed)",
         draws, ours ? "our effect's fog" : "the game's fog", g_resolveN.load() ? "" : " - the game's resolve pipeline NOT found (another build?)",
         n ? ms / n : 0.0, n, g_on.load() && !draws ? (ours ? " - NONE drawn: our post pass's fog as before" : " - NONE drawn: the game's fog as before") : "",
         bound, boundU, drawn, drawnU);
}

}  // namespace fogfx
