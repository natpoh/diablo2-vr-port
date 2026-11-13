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
#include <unordered_set>
#include <vector>

#include "fog_height.h"           // StreamPixelShader
#include "render_bench.h"   // the hair / foliage pipelines by their pixel shader (the bench's stages)
#include "fog_in_frame_math.h"    // IsResolve: the game's ps_transparent_resolve by its size and hash
#include "native_d3d12.h"
#include "pso_stream.h"           // the effects' pipelines made from a stream
#include "../shared/unleashed_fog.h"   // [fog] ease_in: UnleashedFogEase (MPL 2.0, D2R VR Unleashed), put in front of our shader

namespace fogfx {
namespace {

using natived3d::RootCall;

LogFn g_log = nullptr;
// The step's lines rate-limited per format: a burst of kLineBurst, then one a second each (a line costs ~1.2 ms here, 29 ms on a player's
// PC). Serg 2026-11-13 16:37 - 16:39 on 0.215.6: the scene flipping between two targets at every resolve told ~6 lines a frame, 30,000 in
// 2.5 minutes - the game stuttered. A line held back is counted and the next one of its kind says how many were not written.
constexpr int kLineKinds = 64;
constexpr double kLineBurst = 5.0;
struct LineKind { const char* fmt = nullptr; double tokens = kLineBurst; ULONGLONG at = 0; uint32_t held = 0; };
LineKind g_lineKinds[kLineKinds];
SRWLOCK g_lineLock = SRWLOCK_INIT;
std::atomic<uint32_t> g_linesHeld{0};   // held back since the 10 s line
// true: write it (`held` = how many of its kind were not written since the last one)
bool LineAllowed(const char* fmt, uint32_t* held) {
    const ULONGLONG now = GetTickCount64();
    AcquireSRWLockExclusive(&g_lineLock);
    LineKind* k = nullptr;
    for (LineKind& e : g_lineKinds)
        if (e.fmt == fmt || !e.fmt) { k = &e; break; }
    bool ok = true;
    if (k) {
        if (!k->fmt) { k->fmt = fmt; k->at = now; }
        k->tokens = (std::min)(kLineBurst, k->tokens + (double)(now - k->at) / 1000.0);
        k->at = now;
        if (k->tokens >= 1.0) {
            k->tokens -= 1.0;
            *held = k->held;
            k->held = 0;
        } else {
            ++k->held;
            ok = false;
        }
    }
    ReleaseSRWLockExclusive(&g_lineLock);
    if (!ok) g_linesHeld.fetch_add(1, std::memory_order_relaxed);
    return ok;
}
void Line(const char* fmt, ...) {
    if (!g_log) return;
    uint32_t held = 0;
    if (!LineAllowed(fmt, &held)) return;
    char b[480];
    va_list va;
    va_start(va, fmt);
    int n = vsnprintf(b, sizeof b, fmt, va);
    va_end(va);
    if (held && n > 0 && n < (int)sizeof b) snprintf(b + n, sizeof b - n, " [%u more of these not written: at most one a second]", held);
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
// The sinks' calls (tens of thousands a frame): one in kSinkSample timed, counted kSinkSample times - the clock read twice a call was itself a
// good part of what it measured (Serg 2026-11-09 on 0.209.16: 0.42 - 0.53 ms a present "in the followed lists' calls")
constexpr uint32_t kSinkSample = 16;
thread_local uint32_t t_sinkTick = 0;
struct SinkTime {
    std::atomic<uint64_t>& to;
    LARGE_INTEGER t0{};
    bool on;
    explicit SinkTime(std::atomic<uint64_t>& a) : to(a), on((++t_sinkTick % kSinkSample) == 0) { if (on) QueryPerformanceCounter(&t0); }
    ~SinkTime() {
        if (!on) return;
        LARGE_INTEGER t1;
        QueryPerformanceCounter(&t1);
        to.fetch_add((uint64_t)(t1.QuadPart - t0.QuadPart) * kSinkSample, std::memory_order_relaxed);
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

// Which picture the step fogs (SceneCheck, each present; Serg 2026-11-09 on 0.209.11 - the probe: the scene the resolve's tables name read
// black at every point, the lit world in the resolve's own target before it drew). 0 the tables' scene; 1 the resolve's own target - the
// tables' scene black where that target is lit, and the resolve blends into its target (weighted OIT: the target times the revealage plus
// the effects - fogging the target is fogging under the effects); 2 neither: nothing of ours written, the game's pass keeps its own fog.
// kWrongRuns probes in a row decide, either way.
// Starts at 2 (Serg 2026-11-09 on 0.209.13, flat with DLSS: the tables' scene AND the resolve's own target read black at the probe's points,
// the resolve writes its target whole - the lit world is laid in after it, and what we wrote ended up over the flame): nothing is written
// until the probe has PROVEN a lit picture, 20 probes in a row
std::atomic<int> g_sceneMode{2};
constexpr int kWrongRuns = 20;
uint8_t g_slotMode[64] = {};             // (kSlots) 1: that step slot fogged the resolve's own target (the probe's two reads swapped)
std::atomic<int> g_lastResolveBlends{-1};   // the last resolve drawn: 1 it blends into its target, 0 it writes it whole, -1 not known
std::atomic<bool> g_lastWrote{false};       // the last step wrote its fog into the picture (Drawn)
std::atomic<int> g_planesDrawnInto{0};
// Watching (Serg 2026-11-09: "такое ощущение что это все в реальном времени пересчитывается и ищется"): once the step has drawn kWatchAfter
// times in a row with the same scene, depth and resolve's target, the sinks stop looking for them - the barriers of other resources are not
// followed, a target binding is not classified (no descriptions, no notes) - only the found ones are watched. Re-armed (the full search
// again) the moment the step meets anything else: another depth or scene at the resolve (a resize, a device change, a menu), a state not
// known, the submission order in doubt.
constexpr uint32_t kWatchAfter = 120;
std::atomic<bool> g_watching{false};
std::atomic<ID3D12Resource*> g_watchRes[4] = {};   // the scene, the depth, the effects' weighted colour, the effects' additive target
std::atomic<uint32_t> g_watchRun{0};
bool Watched(ID3D12Resource* r) {
    for (const auto& w : g_watchRes)
        if (w.load(std::memory_order_relaxed) == r) return true;
    return false;
}
void ReArm(const char* why) {
    if (!g_watching.exchange(false)) return;
    g_watchRun.store(0);
    for (auto& w : g_watchRes) w.store(nullptr);
    Line("vrcam: fog before the effects - the full search again (%s)", why);
}      // [fog] game_fog_planes, the last step: 0 nothing written, 1 the tables' scene, 2 the resolve's own target

// ---- the hair and foliage pipelines (Serg 2026-11-09 on 0.209.17: "затененный забор перекрывает огонь" - 13201776 fogged the effects' whole
// weighted colour by the depth behind it, the flames' weighted part too: a bonfire's lower flame took the fogged palisade's colour). The
// effects pass draws particles (popcorn - left as drawn) and, into the same weighted targets, the see-through parts of models: hair strands,
// leaf and branch cards (their pixel shaders the bench's lit and water / glass / hair stages). Those draws are sent into a weighted colour
// target of ours (the game's revealage target kept), and at the step ours is fogged by the depth behind it and added into the game's.
SRWLOCK g_fbLock = SRWLOCK_INIT;
std::unordered_set<ID3D12PipelineState*> g_fogBehind;
std::unordered_set<ID3D12PipelineState*> g_fogBehindOit;   // ... of them, those whose pixel shader writes the game's weight (HasOitWeight)
std::atomic<uint32_t> g_fogBehindN{0}, g_fogBehindOitN{0}, g_redirected{0}, g_notOit{0};
std::atomic<bool> g_hairStrict{true}, g_ownBehind{true};   // [fog] game_fog_hair_strict / game_fog_own_distance_behind (Params)
std::atomic<bool> g_sceneOneSize{true};    // [fog] game_fog_scene_one_size (Params::sceneOneSize; ReadScene)
std::atomic<bool> g_sceneByName{true};     // [fog] game_fog_scene_by_name (Params::sceneByName; NamedScene)
std::atomic<uint32_t> g_byName{0}, g_bySearch{0};   // resolves whose scene the game's name gave / the search gave (the key on), since the 10 s line
std::atomic<int> g_lastResolveStep{-1};    // [fog] game_fog_miss_keeps_fog: the last resolve anywhere - 1 the step wrote, 0 not, -1 none yet
std::atomic<uint32_t> g_pickOwn{0}, g_pickLast{0}, g_pickNone{0};   // StepDrewAt's answers by: the list's own resolve, the last anywhere, none
thread_local bool t_stepped = false;       // Insert on this thread recorded a step (OnDraw)
uint64_t Fnv64(const void* p, size_t n) {   // (vr/pipe_hunt.cpp's - the bench's hashes)
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) { h ^= ((const uint8_t*)p)[i]; h *= 1099511628211ull; }
    return h;
}
void NoteFogBehind(ID3D12PipelineState* pso, bool oit) {
    AcquireSRWLockExclusive(&g_fbLock);
    const bool added = g_fogBehind.insert(pso).second;
    const bool addedOit = oit && g_fogBehindOit.insert(pso).second;
    ReleaseSRWLockExclusive(&g_fbLock);
    if (added) g_fogBehindN.fetch_add(1, std::memory_order_relaxed);
    if (addedOit) g_fogBehindOitN.fetch_add(1, std::memory_order_relaxed);
}
// Every pipeline's pixel shader hash (the 10 s line below names the ones drawing into the effects' weighted colour that are not sent - the
// particles, and any foliage / grass the bench's stages do not know: Serg 2026-11-10 on 0.210.4, far crowns, bushes and small grass on
// Medium through the fog)
std::unordered_map<ID3D12PipelineState*, uint64_t> g_psHashOf;   // (under g_fbLock)
SRWLOCK g_notSentLock = SRWLOCK_INIT;
std::unordered_map<ID3D12PipelineState*, uint32_t> g_notSent;     // draws into the weighted colour not sent, by pipeline (10 s)
void NoteIfFogBehind(ID3D12PipelineState* pso, const void* ps, size_t n) {
    if (!pso || !ps || !n) return;
    const uint64_t h = Fnv64(ps, n);
    AcquireSRWLockExclusive(&g_fbLock);
    g_psHashOf[pso] = h;
    ReleaseSRWLockExclusive(&g_fbLock);
    const uint8_t st = renderbench::StageOf(h);
    if (st == renderbench::kLit || st == renderbench::kWaterGlassHair) NoteFogBehind(pso, HasOitWeight(ps, n));
}
// A hair / foliage pipeline (the bench's lit or hair stage); *oit: its pixel shader writes the game's weight
bool IsFogBehind(ID3D12PipelineState* pso, bool* oit = nullptr) {
    if (oit) *oit = false;
    if (!pso) return false;
    AcquireSRWLockShared(&g_fbLock);
    const bool is = g_fogBehind.count(pso) != 0;
    if (is && oit) *oit = g_fogBehindOit.count(pso) != 0;
    ReleaseSRWLockShared(&g_fbLock);
    return is;
}
std::atomic<bool> g_foldOk{false};   // the last step folded ours into the game's weighted colour: the hair / foliage draws may be sent there
std::atomic<bool> g_foldReveal{false};   // ... and our revealage into the game's: their revealage may be sent to ours too (their own distance)
std::atomic<bool> g_sentSinceFold{false};   // a draw was sent to ours since the last step that read it (folded or cleared)
std::atomic<bool> g_revealSentSinceFold{false};   // ... its revealage to ours too
std::atomic<uint32_t> g_unfolded{0}, g_revealDropped{0};   // (10 s) resolves that went on without folding what was sent; revealage cleared unfolded

// ---- the game's resolve pipelines
constexpr int kResolves = 16;
std::atomic<ID3D12PipelineState*> g_resolve[kResolves] = {};
std::atomic<int> g_resolveBlend[kResolves] = {};   // 1 its target blended, 0 written whole, -1 not known (as g_resolve)
std::atomic<int> g_resolveN{0};
// (blend: its first target's blend as made - Serg 2026-11-09 on 0.209.11, the probe: the scene the resolve's tables name read black at every
// point while the picture had the lit world; whether the resolve blends into its own target - the lit scene there - says where the fog goes)
void AddResolve(ID3D12PipelineState* p, const D3D12_RENDER_TARGET_BLEND_DESC* blend = nullptr, int blends = -1) {
    for (int i = 0; i < kResolves; ++i)   // (one known already: its blend as told now)
        if (g_resolve[i].load() == p) {
            g_resolveBlend[i].store(blend ? (blend->BlendEnable ? 1 : 0) : blends);
            return;
        }
    for (int i = 0; i < kResolves; ++i) {
        auto& r = g_resolve[i];
        ID3D12PipelineState* none = nullptr;
        if (r.compare_exchange_strong(none, p)) {
            g_resolveBlend[i].store(blend ? (blend->BlendEnable ? 1 : 0) : blends);
            const int n = g_resolveN.fetch_add(1) + 1;
            if (blend)
                Line("vrcam: fog before the effects - the game's transparent resolve pipeline %d found (%p); its target's blend: %s (colour %d x src "
                     "+ %d x dst, op %d; alpha %d / %d, op %d; write mask %u)", n, (void*)p, blend->BlendEnable ? "ON - it blends into its target" : "off - "
                     "it writes its target whole", (int)blend->SrcBlend, (int)blend->DestBlend, (int)blend->BlendOp, (int)blend->SrcBlendAlpha,
                     (int)blend->DestBlendAlpha, (int)blend->BlendOpAlpha, (unsigned)blend->RenderTargetWriteMask);
            else Line("vrcam: fog before the effects - the game's transparent resolve pipeline %d found (%p)", n, (void*)p);
            return;
        }
    }
}
int ResolveBlends(ID3D12PipelineState* p) {
    if (!p) return -1;
    for (int i = 0; i < kResolves; ++i)
        if (g_resolve[i].load(std::memory_order_relaxed) == p) return g_resolveBlend[i].load(std::memory_order_relaxed);
    return -1;
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
    D3D12_RESOURCE_STATES first = D3D12_RESOURCE_STATE_COMMON;   // (a list's record: the state its first barrier on it came from - the chain)
    bool firstWhole = false;                                       // ... that first barrier was on all its subresources
    // Subresource 0 - all the step touches of any target (its views one mip, its barriers and copies subresource 0): whether a barrier on it
    // (or on all) was seen, so s is its state, and the state the first such barrier on this list came from (the chain). Serg's 0.210.4 log:
    // the effects' weighted colour "set on the resolve's list (0x80)" and still refused after e93a2020 - more than one subresource, moved
    // by barriers on subresource 0
    bool has0 = false;
    D3D12_RESOURCE_STATES first0 = D3D12_RESOURCE_STATE_COMMON;
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
    // The step at this list's resolve (the submission order's proof, OnExecute): the scene it took, whether this list set its state before
    // the draw, and - when the step drew on the chain's state - the state it took
    bool redirected = false;   // our weighted colour target bound in the game's (a hair / foliage draw) - given back before the next other draw
    int8_t resolveStep = -1;   // [fog] game_fog_miss_keeps_fog: this list's last resolve since its reset - 1 the step wrote, 0 it did not, -1 none
    ID3D12Resource* stepRes[4] = {};   // (0 the scene, 1 the effects' weighted colour, 2 their revealage, 3 their additive light - the chains)
    bool stepOwn[4] = {};
    bool stepOnChain[4] = {};
    D3D12_RESOURCE_STATES stepState[4] = {};
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
        for (int i = 0; i < 4; ++i) {
            stepRes[i] = nullptr;
            stepOwn[i] = stepOnChain[i] = false;
        }
        redirected = false;
        resolveStep = -1;
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
// ---- The scene's state in the order the GPU runs the lists (Serg 2026-11-09 on 0.209.15, flat with DLSS: the scene the resolve reads is
// never given a barrier on the resolve's own list; the last one recorded anywhere this frame puts it in RENDER_TARGET - the game records its
// lists on several threads, so "the last recorded" is not the order they run in, and a state guessed hung the GPU once, 0be5454f). At every
// ExecuteCommandLists of the game's direct queue (OnExecute, before the real call): the lists in their order, each one's first barrier on the
// scene must come from the state the chain holds (else a transition we do not see - another queue, a list not followed, an enhanced barrier:
// the chain is lost until the next whole one), its last becomes the chain's. At the resolve's list the chain's state there (its own barriers
// before the draw: none) is noted; kChainRuns submissions in a row with the same one and nothing lost = proven: the step may draw from that
// state (Insert). Each submission checks a step drawn on it against the chain - any doubt and nothing is drawn on it again until proven anew.
// Two followed: 0 the scene, 1 the effects' weighted colour target (fogged by the depth behind it - Serg 2026-11-09 on 0.209.16: branches,
// bushes and hair escaped the fog; the game's effects pass draws them, and its colour target is read by the resolve as the scene is).
constexpr int kChainRuns = 60;
constexpr int kChains = 4;
SRWLOCK g_chainLock = SRWLOCK_INIT;
struct Chain {
    ID3D12Resource* res = nullptr;                              // followed (the step's latest)
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    bool known = false;
    D3D12_RESOURCE_STATES at = D3D12_RESOURCE_STATE_COMMON;    // the chain's state at the resolve's list, the last time
    int runs = 0;                                              // ... the same this many submissions in a row
};
Chain g_chain[kChains];
std::atomic<uint32_t> g_chainLost{0}, g_chainDoubts{0};
void ChainWatch(int i, ID3D12Resource* r) {   // (Insert: what the step took - a new one is followed from the start)
    AcquireSRWLockExclusive(&g_chainLock);
    Chain& c = g_chain[i];
    if (c.res != r) {
        c.res = r;
        c.known = false;
        c.runs = 0;
    }
    ReleaseSRWLockExclusive(&g_chainLock);
}
bool ChainProven(int i, ID3D12Resource* r, D3D12_RESOURCE_STATES* s) {
    AcquireSRWLockShared(&g_chainLock);
    const Chain& c = g_chain[i];
    const bool ok = c.res == r && c.runs >= kChainRuns;
    if (ok) *s = c.at;
    ReleaseSRWLockShared(&g_chainLock);
    return ok;
}
List* Find(ID3D12CommandList* cl) {
    AcquireSRWLockShared(&g_listLock);
    const auto it = g_lists.find((ID3D12GraphicsCommandList*)cl);
    List* l = it != g_lists.end() ? it->second : nullptr;
    ReleaseSRWLockShared(&g_listLock);
    return l;
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
    if (t.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES || t.Subresource == 0) {
        if (!k.has0) k.first0 = t.StateBefore;
        k.has0 = true;
    }
    if (t.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) { k.s = t.StateAfter; k.whole = true; return; }
    if (t.Subresource == 0) k.s = t.StateAfter;
    k.whole = false;
}

// ---- the sinks (the game's calls, before the real ones)
bool Wanted() { return g_on.load(std::memory_order_relaxed); }
void OnReset(ID3D12GraphicsCommandList* cl) {
    const SinkTime cpuTime(g_cpuSinks);
    List* l = Of(cl);
    if (Wanted()) l->Clear();
    else l->tracked = false;
}
// The scene's target by its depth-stencil, from ANY list: the game draws its opaque scene on another list than the resolve's (PC1
// 2026-10-28: the resolve's draw seen 897 times in 10 s, "no R11G11B10 scene target bound with the resolve's depth before it" - looked
// for on the resolve's own list only). The last few pairs, each target held (a reference of ours: no destroy event here), the oldest let go.
// The game's additive effects target: R11G11B10 of the scene's size in the flat game with DLSS (0.209.14's Ctrl+F10 trace, Serg 2026-11-09:
// R11G11B10F @D46288A0 bound with the effects' RGBA16F pair - the weighted colour and the revealage - and with the depth for the blended
// draws into it), so it passed for the scene: the step fogged it - the resolve adds it over everything (the fog laid over the flame) and the
// flame's own glow in it went down by the fog's share. In VR it was half the scene's size and the size check left it out. A R11G11B10 target
// bound in one call with a RGBA16F one is that target: never the scene. The last few kept (held: no destroy event here).
constexpr int kFxR11 = 8;
ID3D12Resource* g_fxR11[kFxR11] = {};
int g_fxR11Next = 0;
SRWLOCK g_fxR11Lock = SRWLOCK_INIT;
bool IsFxR11(ID3D12Resource* r) {
    if (!r) return false;
    AcquireSRWLockShared(&g_fxR11Lock);
    bool is = false;
    for (ID3D12Resource* x : g_fxR11) is |= x == r;
    ReleaseSRWLockShared(&g_fxR11Lock);
    return is;
}
void NoteFxR11(ID3D12Resource* r) {
    if (!r || IsFxR11(r)) return;
    r->AddRef();
    AcquireSRWLockExclusive(&g_fxR11Lock);
    ID3D12Resource* old = g_fxR11[g_fxR11Next];
    g_fxR11[g_fxR11Next] = r;
    g_fxR11Next = (g_fxR11Next + 1) % kFxR11;
    ReleaseSRWLockExclusive(&g_fxR11Lock);
    if (old) old->Release();
    const D3D12_RESOURCE_DESC d = r->GetDesc();
    Line("vrcam: fog before the effects - the effects' additive target %p (R11G11B10 %llux%u, bound with the effects' RGBA16F targets): never "
         "taken for the scene", (void*)r, (unsigned long long)d.Width, d.Height);
}
// ... and the effects' weighted colour target bound in that call: the first RGBA16F one (the revealage the second - the particles write colour x
// alpha x weight into SV_Target0, the revealage into 1; the resolve takes rgb / a). The latest held.
std::atomic<ID3D12Resource*> g_fxAccum{nullptr};
std::atomic<uint32_t> g_accumBarriers{0};   // the game's barriers seen on it (the "not known" line: none ever = no barrier of ours to follow)
std::atomic<ID3D12Resource*> g_fxReveal{nullptr};   // ... and its revealage target (the pair's second RGBA16F), held
void NoteFxReveal(ID3D12Resource* r) {
    if (!r || g_fxReveal.load() == r) return;
    r->AddRef();
    if (ID3D12Resource* old = g_fxReveal.exchange(r)) old->Release();
}
void NoteFxAccum(ID3D12Resource* r) {
    if (!r || g_fxAccum.load() == r) return;
    r->AddRef();
    if (ID3D12Resource* old = g_fxAccum.exchange(r)) old->Release();
    const D3D12_RESOURCE_DESC d = r->GetDesc();
    Line("vrcam: fog before the effects - the effects' weighted colour target %p (RGBA16F %llux%u): its hair and foliage draws (the lit and hair "
         "pipelines, %u known) drawn into a target of ours, fogged by the depth behind them and added in; the particles (flames, smoke) as drawn",
         (void*)r, (unsigned long long)d.Width, d.Height, g_fogBehindN.load());
}
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
        if (p.ds != ds || p.scene == resolveTarget || IsFxR11(p.scene)) continue;
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
ID3D12Resource* ReadScene(const List& l, ID3D12Resource* depth, int* elsewhere, int* sized) {
    *elsewhere = 0;
    *sized = 0;
    const unsigned inc = natived3d::SrvIncrement();
    if (!inc || !l.gSet || !l.nHeaps) return nullptr;
    ID3D12Resource* cand[kScenePairs];
    ID3D12Resource* other[kScenePairs];   // (bound with the depth but not its size: neither a candidate nor counted as elsewhere)
    int nc = 0, no = 0;
    // [fog] game_fog_scene_one_size: only a target of the depth's size is a candidate. Serg 2026-11-13 16:37 on 0.215.6 - the resolve's
    // tables also read a 1024 x 1024 two-mip R11G11B10 target once bound with the 2048 depth (the game's half-size effects layer); it was
    // taken as the scene, the step then refused it for its size and the frame had no fog (thousands of frames over two minutes)
    const bool oneSize = g_sceneOneSize.load(std::memory_order_relaxed);
    const D3D12_RESOURCE_DESC dd = depth->GetDesc();
    AcquireSRWLockShared(&g_sceneLock);
    for (const ScenePair& p : g_scenes)
        if (p.ds == depth && p.scene && p.scene != l.rt0 && !IsFxR11(p.scene)) {
            if (oneSize) {
                const D3D12_RESOURCE_DESC sd = p.scene->GetDesc();
                if (sd.Width != dd.Width || sd.Height != dd.Height) { other[no++] = p.scene; continue; }
            }
            cand[nc++] = p.scene;
        }
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
                bool skip = false;
                for (int c = 0; c < no && !skip; ++c) skip = other[c] == r;
                if (!skip) ++*elsewhere;
                else ++*sized;
            }
        }
    }
    return nullptr;
}

// [fog] game_fog_scene_by_name (2026-11-13, Serg: "find a faster method without searching the scene's targets every frame"): the game's
// renderer makes its targets with names (D2R image: "Main Color 0", "Main Color 1", "Main Color Copy/Mips", "Revealage", "Accumulation",
// "Main Depth Mip %d" - one function at RVA 0xE1C700 - 0xE1CA41) and its D3D12 back end names what it makes through
// SetPrivateData(WKPDID_D3DDebugObjectName) (26 references at 0x10B0000 - 0x10C9000). So the scene the resolve reads is the view in its
// own tables named "Main Color 0" or "Main Color 1" (not "Main Color Copy...", not its own target), of the depth's size: known, not guessed
// from what was bound with the depth. nullptr: no such view (names not set in this build, the views not seen) - the search as before.
// `names`: the tables' named views ("name WxH"), for the line that tells them once a change.
bool MainColourName(const char* s) {
    return std::strncmp(s, "Main Color ", 11) == 0 && (s[11] == '0' || s[11] == '1') && s[12] == 0;
}
ID3D12Resource* NamedScene(const List& l, ID3D12Resource* depth, char* names, size_t namesN) {
    if (names && namesN) names[0] = 0;
    const unsigned inc = natived3d::SrvIncrement();
    if (!inc || !l.gSet || !l.nHeaps || !depth) return nullptr;
    const D3D12_RESOURCE_DESC dd = depth->GetDesc();
    ID3D12Resource* found = nullptr;
    size_t len = 0;
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
                char nm[64];
                if (!natived3d::ResourceName(r, nm, sizeof nm)) continue;
                const D3D12_RESOURCE_DESC rd = r->GetDesc();
                if (names && len + 1 < namesN) {
                    const int w = snprintf(names + len, namesN - len, "%s'%s' %llux%u", len ? ", " : "", nm, (unsigned long long)rd.Width, rd.Height);
                    if (w > 0) len = (std::min)(namesN - 1, len + (size_t)w);
                }
                if (!found && r != l.rt0 && MainColourName(nm) && rd.Width == dd.Width && rd.Height == dd.Height) found = r;
            }
        }
    }
    return found;
}

void OnTargets(ID3D12GraphicsCommandList* cl, unsigned n, const D3D12_CPU_DESCRIPTOR_HANDLE* rts, bool single, ID3D12Resource* ds,
               const D3D12_CPU_DESCRIPTOR_HANDLE* dsv) {
    if (!Wanted()) return;
    const SinkTime cpuTime(g_cpuSinks);
    List* l = Of(cl);
    l->ds = ds;
    l->rt0 = n && rts ? natived3d::RtvResource(rts[0].ptr) : nullptr;
    l->nRt = rts ? std::min(n, 8u) : 0;
    l->single = single;
    for (unsigned i = 0; i < (single ? std::min(l->nRt, 1u) : l->nRt); ++i) l->rts[i] = rts[i];
    l->hasDsv = dsv != nullptr;
    if (dsv) l->dsv = *dsv;
    l->redirected = false;   // (the game's own targets bound now)
    if (g_watching.load(std::memory_order_relaxed)) {   // (watching: the scene known by its pointer - no descriptions, no notes)
        if (l->rt0 && ds && l->rt0 == g_watchRes[0].load(std::memory_order_relaxed) && ds == g_watchRes[1].load(std::memory_order_relaxed)) {
            l->scene = l->rt0;
            l->sceneDs = ds;
        }
        return;
    }
    {   // the effects' additive target: a R11G11B10 one bound with a RGBA16F one (the weighted colour / revealage) - never the scene
        ID3D12Resource* rs[8] = {};
        bool half = false;
        const unsigned inc = natived3d::RtvIncrement();
        for (unsigned i = 0; i < l->nRt && rts; ++i) {
            rs[i] = natived3d::RtvResource(single ? rts[0].ptr + (uint64_t)i * inc : rts[i].ptr);
            if (rs[i] && rs[i]->GetDesc().Format == DXGI_FORMAT_R16G16B16A16_FLOAT) half = true;
        }
        if (half) {
            bool r11 = false;
            for (unsigned i = 0; i < l->nRt; ++i)
                if (rs[i] && rs[i]->GetDesc().Format == DXGI_FORMAT_R11G11B10_FLOAT) { NoteFxR11(rs[i]); r11 = true; }
            int pairAt = 0;   // (with the additive one: the effects' pair - its first RGBA16F the weighted colour, the second the revealage)
            for (unsigned i = 0; r11 && i < l->nRt; ++i)
                if (rs[i] && rs[i]->GetDesc().Format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
                    if (pairAt++ == 0) NoteFxAccum(rs[i]);
                    else { NoteFxReveal(rs[i]); break; }
                }
        }
    }
    if (l->rt0 && ds && !IsFxR11(l->rt0)) {
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
    const SinkTime cpuTime(g_cpuSinks);
    List* l = Of(cl);
    const bool watching = g_watching.load(std::memory_order_relaxed);
    for (unsigned i = 0; i < n; ++i) {
        if (b[i].Type != D3D12_RESOURCE_BARRIER_TYPE_TRANSITION || !b[i].Transition.pResource) continue;
        ID3D12Resource* r = b[i].Transition.pResource;
        if (r == g_fxAccum.load(std::memory_order_relaxed)) g_accumBarriers.fetch_add(1, std::memory_order_relaxed);
        if (watching && !Watched(r)) continue;   // (watching: only the found ones' states)
        const D3D12_RESOURCE_TRANSITION_BARRIER& t = b[i].Transition;
        // (a first barrier on one subresource other than 0: subresource 0's state not known from it)
        const bool firstKnows = t.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES || t.Subresource == 0;
        Known* e = nullptr;
        for (auto& x : l->states)
            if (x.r == r) { e = &x; break; }
        if (!e && firstKnows) {
            l->states.push_back({r, t.StateAfter, false, 0, t.StateBefore, t.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES});
            e = &l->states.back();
        } else if (!e) {   // (a first barrier on another subresource: the chain cannot follow this list's resource - noted as not whole)
            l->states.push_back({r, t.StateAfter, false, 0, t.StateBefore, false});
            e = &l->states.back();
        }
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
    const SinkTime cpuTime(g_cpuSinks);
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
    const SinkTime cpuTime(g_cpuSinks);
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
    float planeN, pStart, pEnd, pStrength;
    float pSoft, accumBehind, revealOn, coverAt;
    float partX, partY, ownBehind, layersInOrder;
};
// The share of the target the scene is drawn in (Params::part): 1 x 1, or an upscaler's part at the top left (DLSS at 0.667: the scene in
// the top-left 2/3, the camera's whole view over it). A pixel's place in the view - its ray, so its distance and the dome test - from that
// part, not from the whole target: with DLSS on (Serg 2026-11-12, test 0.214.5, 244.webp) the dome's test took the whole target, and only a
// circle of the dome round 0.3, 0.3 of the picture (where that ray came out shorter than the true one) was the sky - the rest of it ground past
// [fog] game_fog_cover's 200, the fog's colour whole; every fog distance off the middle was off by the same factor.
float2 UvOf(float2 p) { return (p + 0.5) / (float2(width, height) * float2(partX, partY)); }
Texture2D<float> Depth : register(t0);
Texture2D<float3> Scene : register(t1);
Texture2D<float> AccumDepth : register(t2);   // [fog] game_fog_fx_depth: the nearest effect drawn into the weighted-blended targets
Texture2D<float4> Accum : register(t3);       // ... their colour target (rgb weighted, a the weights' sum)
Texture2D<float> GlowDepth : register(t4);    // ... the nearest drawn into the half-size additive target
Texture2D<float3> Glow : register(t5);
Texture2D<float3> Target : register(t6);      // the probe: the resolve's own target before it draws (a black view when not readable)
Texture2D<float4> Hair : register(t7);        // the hair / foliage draws' weighted colour (ours): fogged by their distance, added in
Texture2D<float4> HairReveal : register(t8);  // ... their revealage (ours): their opacity, so their distance from the weight; multiplied back
Texture2D<float4> Reveal : register(t9);      // the game's revealage (the particles')
RWTexture2D<float3> Out : register(u0);
RWTexture2D<float4> OutAccum : register(u1);
RWTexture2D<float3> OutGlow : register(u2);
RWTexture2D<float4> OutReveal : register(u4);   // the game's revealage with ours multiplied in
// The probe (Serg 2026-11-09: "туман просто слегка затеняет" with density 33 - full fog by 60 units, yet the house at 100+ readable): at five
// points of the picture what the step itself read and made - the depth, the view depth and distance it decoded, what the fog leaves, the
// sky test, the scene's colour before and after. Read back with the timestamps (the 10 s line), so the decode is proven on the GPU.
RWByteAddressBuffer Probe : register(u3);
static const float2 kProbe[5] = {float2(0.5, 0.5), float2(0.15, 0.45), float2(0.35, 0.45), float2(0.65, 0.45), float2(0.85, 0.45)};
// [fog] game_fog_planes: what the camera-facing planes in front of a point at view depth z leave of it - plane i at pStart + (pEnd - pStart)
// (i + 1) / N covers it when z lies behind it (the depth test); soft (pSoft x the spacing): it fades in over that much in front of itself, so
// the fog starts at pStart and is whole at pEnd either way (soft particles, no line where a plane cuts the ground; 0: hard planes),
// each adding its share so the fog after plane i is pStrength (i + 1) / N (vr/fog_before_fx.h PlaneAlpha, PlaneAt)
float PlanesKeep(float z) {
    const int n = (int)(planeN + 0.5);
    const float s = saturate(pStrength), gap = max((pEnd - pStart) / max(planeN, 1.0), 0.01), fade = gap * saturate(pSoft);
    float keep = 1.0;
    [loop] for (int i = 0; i < n; ++i) {
        const float at = pStart + (pEnd - pStart) * (float)(i + 1) / (float)n;
        if (z <= at - fade) break;   // (the planes farther on are behind the point)
        const float before = s * (float)i / (float)n, after = s * (float)(i + 1) / (float)n;
        const float a = 1.0 - (1.0 - after) / max(1.0 - before, 1e-4);
        const float cover = fade > 1e-3 ? saturate((z - at) / fade + 1.0) : (z > at ? 1.0 : 0.0);
        keep *= 1.0 - a * cover;
    }
    return keep;
}
// What the fog leaves of a point at the depth d (reverse-Z, > 0) seen at uv in the frame
float KeepZ(float2 uv, float z);
float Keep(float2 uv, float d) { return KeepZ(uv, nearPlane / d); }
// ... of a point at the view depth z
float KeepZ(float2 uv, float z) {
    const float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    const float3 v = float3((ndc.x - offX) / sx * z, ndc.y / sy * z, z);
    // (the game's: its view position's w is 1; our effect's fog, Params::ours: the view's depth, as D2R_DepthFog.fx FogAmount)
    const float dist = byDepth > 0.5 ? z : sqrt(dot(v, v) + 1.0);
    float t = planeN > 0.5 ? PlanesKeep(z)   // ([fog] game_fog_planes: the planes in front of the point's view depth)
            : curveOn > 0.5 ? 1.0 - pow(saturate((dist - cStart) / max(cEnd - cStart, 1.0)), cCurve) * cStrength * UnleashedFogEase(dist, cStart, easeOn)   // ([fog] game_fog_our_curve, ease_in)
                            : min(1.0, exp(-density * (dist / plane - 1.0)));
    if (heightOn > 0.5) t = 1.0 - (1.0 - t) * saturate((top - dot(v, up)) / soft);
    if (coverAt > 0.5 && byDepth < 0.5 && dist >= coverAt) t = 0.0;   // ([fog] game_fog_cover: the fog's colour whole from there on)
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
    const float2 uv = UvOf(float2(p));
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
)"
R"(// The game's fog (Params::ours off): a pixel on an edge between two fogs. The pixels along a silhouette hold the object and what is behind
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
    const float2 uv = UvOf(float2(p));
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
    // The game's dome beside ground the fog nearly covers: fogged as that ground. Far tree crowns are thinned to a pixel pattern by the game
    // (its draw-distance fade); between their leaves - fogged by their depth - the dome showed through unfogged: dark specks over every
    // far crown (Serg 2026-11-09 on 0.209.21 / 0.210 rc, VR F4; with the dome at 200, in front of them, "все выглядит отлично"). Only beside
    // ground fogged half or more: near trees' gaps keep the sky as it is
    if (SkyProper(p, d0)) {
        float kGround = 2.0;
        [unroll] for (int i = 0; i < 8; ++i)
            if (dn[i] > 1e-7 && !SkyAt(p, dn[i])) kGround = min(kGround, Keep(uv, dn[i]));
        if (kGround <= 0.5) return lerp(fogColour, c, kGround);
    }
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
void ProbeAt(uint2 p, float3 c, float3 o) {
    [unroll] for (int i = 0; i < 5; ++i) {
        const uint2 q = min(uint2(kProbe[i] * float2(width, height) * float2(partX, partY)), uint2(width - 1, height - 1));   // (in the drawn part)
        if (q.x != p.x || q.y != p.y) continue;
        const float d = Depth[p];
        const float2 uv = UvOf(float2(p));
        const float z = d > 1e-7 ? nearPlane / d : 0.0;
        const float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
        const float3 v = float3((ndc.x - offX) / sx * z, ndc.y / sy * z, z);
        const float dist = byDepth > 0.5 ? z : sqrt(dot(v, v) + 1.0);
        Probe.Store4(i * 64, asuint(float4(d, z, dist, KeepAt(p, uv, d))));
        Probe.Store4(i * 64 + 16, asuint(float4(c, SkyAt(p, d) ? 1.0 : 0.0)));
        Probe.Store4(i * 64 + 32, asuint(float4(o, 1.0)));
        Probe.Store4(i * 64 + 48, asuint(float4(Target[p], 0.0)));
        Probe.Store4(320 + i * 32, asuint(float4(0.0, 0.0, 0.0, 0.0)));   // (the hair / foliage probe: fxAccum writes it when it runs)
        Probe.Store4(320 + i * 32 + 16, asuint(float4(0.0, 0.0, 0.0, 0.0)));
    }
}
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= width || id.y >= height) return;
    const float3 c = Scene[id.xy];
    if (byDepth < 0.5) {   // (the game's fog; our effect's below, as it was)
        const float3 o = GameFogPixel(id.xy, c);
        Out[id.xy] = o;
        ProbeAt(id.xy, c, o);
        return;
    }
    const float d = GroundDepth(id.xy);
    // the void, and the game's dome ([sky] game_sky, Params::skyDepth: it stands past the game's linear depth's far end, and the game's
    // pass leaves it - Serg 2026-10-31 on 0.192.19, "обводка вокруг объектов": its low part right behind the trees fogged to the fog's
    // colour, a light band round every tree against the dark sky): left as they are
    const float3 o = SkyAt(id.xy, d) ? c : lerp(fogColour, c, Keep(UvOf(float2(id.xy)), d));
    Out[id.xy] = o;
    ProbeAt(id.xy, c, o);
}
// The effects at their own depth, never nearer than the scene behind them (an effect hidden there adds nothing); none drawn here (our
// depth 0): as they are. The weighted-blended colour toward the fog's colour (its weight kept: the resolve divides by it), the additive
// light down. Over the void (the sky goes there after) the colour fades out rather than into the fog's colour: a far fire's halo above
// the palisade it stands on fades with its lower half (Serg's 0.192.1 shot: an orange disc on the horizon)
// The view depth of the opaque scene behind a see-through draw at the depth d (reverse-Z): what its own distance never passes ([fog]
// game_fog_own_distance_behind - it passed the depth test there); none (0) for the void and the game's dome, or with the key off
float BehindZ(uint2 p, float d) { return ownBehind > 0.5 && d > 1e-7 && !SkyAt(p, d) ? nearPlane / d : 0.0; }
// A view depth read from the game's weight w = 1e6 / (1e5 + z^3), never past zMax (0: no limit); 0 when unreadable
float ZOfWeight(float w, float zMax) {
    const float z = w > 1e-6 ? pow(max(1e6 / w - 1e5, 0.0), 1.0 / 3.0) : 0.0;
    return zMax > 0.0 && z > zMax ? zMax : z;
}
// What the fog leaves of the particles at full-size pixel p (their weighted colour a): their view depth read from the game's weight
// (1e6 / (1e5 + z^3)) over their opacity (1 - the game's revealage), never past zMax; none there or unreadable: 1
float ParticleKeep(uint2 p, float2 uv, float4 a, float zMax) {
    const float op = 1.0 - saturate(Reveal[p].a);
    const float w = op > 1e-4 && a.a > 0.0 ? a.a / op : 0.0;
    const float z = ZOfWeight(w, zMax);
    return z > 0.0 ? KeepZ(uv, z) : 1.0;
}
// The probe's hair / foliage half (fxAccum, after the main entry's ProbeAt zeroed it): at the same five points their weighted alpha, their
// opacity, the view depth read from the weight and the one used, the scene's behind, what the fog leaves of them and of the particles, and
// what ran (1 hair by their own distance, 2 by the depth behind, + 4 the limit on)
void HairProbeAt(uint2 p, float ha, float op, float zRead, float zUsed, float zBehind, float th, float tp, float how) {
    [unroll] for (int i = 0; i < 5; ++i) {
        const uint2 q = min(uint2(kProbe[i] * float2(width, height) * float2(partX, partY)), uint2(width - 1, height - 1));
        if (q.x != p.x || q.y != p.y) continue;
        Probe.Store4(320 + i * 32, asuint(float4(ha, op, zRead, zUsed)));
        Probe.Store4(320 + i * 32 + 16, asuint(float4(zBehind, th, tp, how + (ownBehind > 0.5 ? 4.0 : 0.0))));
    }
}
[numthreads(8, 8, 1)]
void fxAccum(uint3 id : SV_DispatchThreadID) {
    if (id.x >= width || id.y >= height) return;
    const float4 a = Accum[id.xy];
    const float e = AccumDepth[id.xy], o = GroundDepth(id.xy);   // (a blended edge on the rim against the void: as the ground it borders)
    const float2 uv = UvOf(float2(id.xy));
    if (accumBehind > 0.5) {   // the hair / foliage (ours) fogged, added in; the particles as drawn
        const float4 h = Hair[id.xy];
        const float zb = BehindZ(id.xy, o);
        if (revealOn > 0.5) {   // by their own distance: the opacity from our revealage, the weight 1e6 / (1e5 + z^3) from the weighted colour
            const float4 hr = HairReveal[id.xy];
            const float op = 1.0 - saturate(hr.a);
            const float w = op > 1e-4 ? h.a / op : 0.0;
            const float z = ZOfWeight(w, zb);
            const float th = h.a > 0.0 ? (z > 0.0 ? KeepZ(uv, z) : KeepAt(id.xy, uv, o)) : 1.0;
            // the particles (the game's own weighted colour) by their own distance as well - the same reading of the game's weight, their
            // opacity from the game's revealage (theirs alone: the hair / foliage revealage is ours): a near flame as drawn, a far fire fading
            // into the fog with the land round it (Serg 2026-11-09: "far fires show as red / orange blobs with halos through the fog")
            const float opP = 1.0 - saturate(Reveal[id.xy].a);
            const float zP = ZOfWeight(opP > 1e-4 && a.a > 0.0 ? a.a / opP : 0.0, zb);
            const float tp = zP > 0.0 ? KeepZ(uv, zP) : 1.0;   // (ParticleKeep's, its distance kept for the order below)
            const float4 accP = float4(a.rgb * tp + fogColour * (a.a * (1.0 - tp)), a.a);
            const float4 accH = float4(h.rgb * th + fogColour * (h.a * (1.0 - th)), h.a);
            // [fog] game_fog_layers_in_order: the two groups - the particles (the game's targets) and the hair / foliage (ours) - laid near
            // over far, not averaged by the game's weight. That weight, 1e6 / (1e5 + z^3), is even across its camera's 550 - 1060 units; from
            // our eyes a haze particle 10 units off weighs 80 times a bush 120 off, so a bush behind the rain's haze took the haze's pale
            // colour over its whole, nearly opaque silhouette (Serg 2026-11-13 on 0.215.8, night rain, Blood Moor). Each group's coverage from
            // its revealage, its distance from its weight; the resolve then gives R scene + (1 - R) c with R their joint revealage
            if (layersInOrder > 0.5 && a.a > 0.0 && h.a > 0.0 && opP > 1e-4 && op > 1e-4 && zP > 0.0 && z > 0.0) {
                const bool hNear = z <= zP;
                const float3 cP = accP.rgb / accP.a, cH = accH.rgb / accH.a;
                const float3 cN = hNear ? cH : cP, cF = hNear ? cP : cH;
                const float oN = hNear ? op : opP, oF = hNear ? opP : op;
                const float cover = 1.0 - (1.0 - oN) * (1.0 - oF);
                OutAccum[id.xy] = float4((cF * (oF * (1.0 - oN)) + cN * oN) / max(cover, 1e-4), 1.0) * (accP.a + accH.a);
            } else OutAccum[id.xy] = accP + accH;
            OutReveal[id.xy] = Reveal[id.xy] * hr;
            HairProbeAt(id.xy, h.a, op, ZOfWeight(w, 0.0), z, zb, th, tp, 1.0);
            return;
        }
        const float th = h.a > 0.0 ? KeepAt(id.xy, uv, o) : 1.0;   // (their revealage not ours: by the opaque depth behind them)
        OutAccum[id.xy] = a + float4(h.rgb * th + (!SkyAt(id.xy, o) ? fogColour * (h.a * (1.0 - th)) : 0.0), h.a);
        HairProbeAt(id.xy, h.a, 0.0, 0.0, 0.0, zb, th, 1.0, 2.0);
        return;
    }
    const float t = a.a > 0.0 && e > 1e-7 ? Keep(uv, max(e, o)) : 1.0;
    OutAccum[id.xy] = float4(a.rgb * t + (!SkyAt(id.xy, o) ? fogColour * (a.a * (1.0 - t)) : 0.0), a.a);   // (over the sky: fades out)
}
[numthreads(8, 8, 1)]
void fxGlow(uint3 id : SV_DispatchThreadID) {
    if (id.x >= glowW || id.y >= glowH) return;
    const float3 g = Glow[id.xy];
    if (accumBehind > 0.5 && revealOn > 0.5) {   // the additive light (no depth of its own): by the particles' distance there, else the depth behind
        const uint2 fp = min(uint2((float2(id.xy) + 0.5) * float2(width, height) / float2(glowW, glowH)), uint2(width - 1, height - 1));
        const float2 fuv = UvOf(float2(fp));
        const float4 a = Accum[fp];
        const float tp = a.a > 0.0 ? ParticleKeep(fp, fuv, a, BehindZ(fp, Depth[fp])) : KeepAt(fp, fuv, Depth[fp]);
        OutGlow[id.xy] = g * tp;   // (light fades out, it does not turn into the fog's colour)
        return;
    }
    const float e = GlowDepth[id.xy];
    // (the scene's depth under it: the nearest of the full-size pixels it covers)
    const uint2 f0 = min(uint2(float2(id.xy) * float2(width, height) / float2(glowW, glowH)), uint2(width - 1, height - 1));
    const uint2 f1 = min(f0 + 1, uint2(width - 1, height - 1));
    const float o = max(max(Depth[f0], Depth[uint2(f1.x, f0.y)]), max(Depth[uint2(f0.x, f1.y)], Depth[f1]));
    const float t = e > 1e-7 ? Keep((id.xy + 0.5) / float2(glowW, glowH) / float2(partX, partY), max(e, o)) : 1.0;
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
    float planeN, pStart, pEnd, pStrength;
    float pSoft, accumBehind, revealOn, coverAt;
    float partX, partY, ownBehind, layersInOrder;
};
static_assert(sizeof(Consts) == 40 * 4, "forty root constants");

constexpr int kSlots = 64;   // descriptor sets and timestamp pairs used in turn (the GPU is never that many steps behind)
constexpr UINT kViews = 15;   // a step's views: t0 - t9, u0 - u4
constexpr UINT kProbeMain = 5 * 64;   // the probe's five points (the shader's ProbeAt)
constexpr UINT kProbeBytes = kProbeMain + 5 * 32;   // ... and their hair / foliage half (HairProbeAt; the buffer holds 512)
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
    ID3D12Resource* hairTex = nullptr;   // the hair / foliage draws' weighted colour (RGBA16F, the game's size; a render target but at the step)
    UINT64 hairW = 0;
    UINT hairH = 0;
    ID3D12DescriptorHeap* rtvHeap = nullptr;   // its render-target view (0), the revealage's (1)
    ID3D12Resource* hairReveal = nullptr;   // the hair / foliage draws' revealage (RGBA16F, 1 where none drawn; a render target but at the step)
    bool hairFresh = false;                 // just made: both cleared at the next step before they are read
    UINT rtvInc = 0;
    ID3D12Resource* revealTex = nullptr;    // the game's revealage with ours multiplied in (copied back), as accumTex
    UINT64 revealW = 0;
    UINT revealH = 0;
    DXGI_FORMAT revealF = DXGI_FORMAT_UNKNOWN;
    ID3D12Resource* probe = nullptr;     // the probe's points (u3), copied after each step into its slot of probeRb
    ID3D12Resource* probeRb = nullptr;
    const float* probes = nullptr;
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
    D3D12_DESCRIPTOR_RANGE ranges[2] = {{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 10, 0, 0, 0}, {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 5, 0, 0, 0}};
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
    {   // the probe: a buffer the step writes, its readback a slot a step
        D3D12_HEAP_PROPERTIES dh{D3D12_HEAP_TYPE_DEFAULT};
        D3D12_RESOURCE_DESC pb = bd;
        pb.Width = 512;
        pb.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        ok = ok && SUCCEEDED(dev->CreateCommittedResource(&dh, D3D12_HEAP_FLAG_NONE, &pb, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                          __uuidof(ID3D12Resource), (void**)&g.probe));
        D3D12_RESOURCE_DESC rbd = bd;
        rbd.Width = (UINT64)kSlots * kProbeBytes;
        ok = ok && SUCCEEDED(dev->CreateCommittedResource(&rb, D3D12_HEAP_FLAG_NONE, &rbd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                          __uuidof(ID3D12Resource), (void**)&g.probeRb));
        void* pm = nullptr;
        ok = ok && SUCCEEDED(g.probeRb->Map(0, nullptr, &pm));
        g.probes = (const float*)pm;
        if (g.probe) g.probe->SetName(L"d2rvr fog before fx probe");
    }
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
// The hair / foliage target, as big as the game's weighted colour (made zeroed, in the render-target state; under g_gpuMx)
bool MakeHair(ID3D12Device* dev, UINT64 w, UINT h) {
    Gpu& g = g_gpu;
    if (g.hairTex && g.hairW == w && g.hairH == h) return true;
    if (g.hairTex) { g.hairTex->Release(); g.hairTex = nullptr; }   // (a new size: the old one's last use long done - a resize waits)
    if (g.hairReveal) { g.hairReveal->Release(); g.hairReveal = nullptr; }
    if (!g.rtvHeap) {
        const D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
        if (FAILED(dev->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void**)&g.rtvHeap))) return false;
        g.rtvInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    }
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = w;
    d.Height = h;
    d.DepthOrArraySize = d.MipLevels = 1;
    d.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    d.SampleDesc.Count = 1;
    d.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE cv{DXGI_FORMAT_R16G16B16A16_FLOAT};
    natived3d::SetOurs(true);
    const bool ok = SUCCEEDED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_RENDER_TARGET, &cv,
                                                           __uuidof(ID3D12Resource), (void**)&g.hairTex));
    if (ok) dev->CreateRenderTargetView(g.hairTex, nullptr, g.rtvHeap->GetCPUDescriptorHandleForHeapStart());
    D3D12_CLEAR_VALUE one{DXGI_FORMAT_R16G16B16A16_FLOAT, {1.0f, 1.0f, 1.0f, 1.0f}};
    const bool okR = ok && SUCCEEDED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_RENDER_TARGET, &one,
                                                                  __uuidof(ID3D12Resource), (void**)&g.hairReveal));
    if (okR) dev->CreateRenderTargetView(g.hairReveal, nullptr, D3D12_CPU_DESCRIPTOR_HANDLE{g.rtvHeap->GetCPUDescriptorHandleForHeapStart().ptr + g.rtvInc});
    natived3d::SetOurs(false);
    if (!okR) return false;
    g.hairReveal->SetName(L"d2rvr fog before fx hair foliage revealage");
    g.hairFresh = true;
    g.hairTex->SetName(L"d2rvr fog before fx hair foliage");
    g.hairW = w;
    g.hairH = h;
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
// Subresource 0's state is what counts (Known::has0): the game moves the effects' targets by barriers on subresource 0 (Serg's 0.210.3 log:
// refused as "part of it"; 0.210.4: still refused with one-subresource targets counted whole - so they have more), and the step touches
// only it - its views one mip, its barriers and copies subresource 0
bool ReadState(const List& l, ID3D12Resource* r, D3D12_RESOURCE_STATES* s) {
    const D3D12_RESOURCE_STATES readable = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    Known k{};
    const bool own = OwnState(l, r, &k);
    if (!own && !(AnyState(r, &k) && k.frame == g_frame.load(std::memory_order_relaxed))) return false;
    *s = k.s;
    return k.has0 && (k.s & D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) && !(k.s & ~readable);
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
    // (watching: another depth at the resolve - a resize, a device change, a menu - the new scene is noted only by the full search)
    if (g_watching.load(std::memory_order_relaxed) && depth != g_watchRes[1].load(std::memory_order_relaxed)) ReArm("another depth at the resolve");
    // (the scene drawn on another list, or the list's own note a smaller target bound with the same depth: by the depth they share and its size)
    if (scene && (l.sceneDs != depth || scene == l.rt0 || IsFxR11(scene) || scene->GetDesc().Width != depth->GetDesc().Width || scene->GetDesc().Height != depth->GetDesc().Height))
        scene = nullptr;
    if (!scene) scene = SceneOf(depth, l.rt0);
    {   // the scene the resolve's own tables name, when they name one: it wins
        int elsewhere = 0, sized = 0;
        ID3D12Resource* read = ReadScene(l, depth, &elsewhere, &sized);
        static int toldHow = -1;   // 0 the tables name the target taken, 1 they name another of the depth's, 2 one not bound with it, 3 none seen
        const int how = read ? (read == scene ? 0 : 1) : elsewhere ? 2 : 3;
        // (only targets left out for their size named - [fog] game_fog_scene_one_size: nothing new to tell, the note stays as it was)
        if (how != toldHow && !(how == 3 && sized)) {
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
    if (g_sceneByName.load(std::memory_order_relaxed)) {   // [fog] game_fog_scene_by_name: the game's own name wins over the search
        char names[320];
        ID3D12Resource* named = NamedScene(l, depth, names, sizeof names);
        static char toldNames[320] = "-";
        static ID3D12Resource* toldNamed = nullptr;
        if (std::strcmp(names, toldNames) != 0 || named != toldNamed) {
            std::memcpy(toldNames, names, sizeof toldNames);
            toldNamed = named;
            if (named)
                Line("vrcam: fog before the effects - the scene known by the game's own name: %p ([fog] game_fog_scene_by_name); the resolve's "
                     "tables name: %s", (void*)named, names);
            else
                Line("vrcam: fog before the effects - no view named \"Main Color 0/1\" of the depth's size in the resolve's tables (%s): the scene "
                     "found by the search as before", names[0] ? names : "no named views");
        }
        if (named) {
            scene = named;
            g_byName.fetch_add(1, std::memory_order_relaxed);
        } else g_bySearch.fetch_add(1, std::memory_order_relaxed);
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
        if (g_watching.load(std::memory_order_relaxed)) ReArm("no scene found");
        return;
    }
    if (g_watching.load(std::memory_order_relaxed) &&
        (scene != g_watchRes[0].load(std::memory_order_relaxed) || depth != g_watchRes[1].load(std::memory_order_relaxed)))
        ReArm("another scene or depth at the resolve");
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
        // (the resolve's list sets none: the scene's state as the submission order proved it at this draw - OnExecute)
        l.stepRes[0] = scene;
        l.stepOwn[0] = ownScene;
        l.stepOnChain[0] = false;
        ChainWatch(0, scene);
        D3D12_RESOURCE_STATES chained{};
        const bool onChain = !ownScene && ChainProven(0, scene, &chained);
        if (onChain) {
            sScene = chained;
            l.stepOnChain[0] = true;
            l.stepState[0] = chained;
        }
        const bool sceneOk = onChain || (anyScene && sceneRead);
        const bool depthGood = anyDepth && kd.whole && (depthRead || (ownDepth && sDepth == D3D12_RESOURCE_STATE_DEPTH_WRITE));
        static uint64_t toldWays = 0;   // (each way the states were found or refused, once: a line costs a millisecond)
        const int which = (sceneOk ? 0 : 1) | (depthGood ? 0 : 2) | (ownScene ? 4 : 0) | (ownDepth ? 8 : 0) | (anyScene ? 16 : 0) | (anyDepth ? 32 : 0) |
                          (onChain ? 64 : 0);
        if (!(toldWays & (1ull << which))) {
            toldWays |= 1ull << which;
            Line("vrcam: fog before the effects - the scene's state 0x%X (%s), the depth's 0x%X (%s%s): %s", (unsigned)sScene,
                 ownScene ? "set on the resolve's list" : onChain ? "proven by the submission order" : anyScene ? "set on another list this frame" : "NOT known this frame", (unsigned)sDepth,
                 ownDepth ? "set on the resolve's list" : anyDepth ? "set on another list this frame" : "NOT known this frame",
                 anyDepth && !kd.whole ? ", not all its subresources" : "",
                 sceneOk && depthGood ? "drawn from them" : "not drawn - only states the resolve's list set, or read states set elsewhere, are taken");
        }
        if (!sceneOk || !depthGood) {
            Why(5, "the scene's or the depth's state not known as the resolve needs it: its draw goes on alone");
            if (g_watching.load(std::memory_order_relaxed)) ReArm("the scene's or the depth's state not known");
            g_watchRun.store(0, std::memory_order_relaxed);
            return;
        }
    }
    // The resolve's own target (R11G11B10 of the scene's size, not the scene; bound as its render target at this draw): the probe's second
    // read, and in mode 1 the picture fogged - the tables' scene is then the probe's second read
    ID3D12Resource* target = l.rt0;
    if (target) {
        const D3D12_RESOURCE_DESC td = target->GetDesc();
        if (target == scene || td.Format != DXGI_FORMAT_R11G11B10_FLOAT || td.Width != sd.Width || td.Height != sd.Height || td.MipLevels < 1 ||
            td.DepthOrArraySize != 1 || td.SampleDesc.Count != 1)
            target = nullptr;
    }
    const int blends = ResolveBlends(l.pso);
    g_lastResolveBlends.store(blends, std::memory_order_relaxed);
    const int mode = g_sceneMode.load(std::memory_order_relaxed);
    const bool onTarget = mode == 1 && target && blends == 1;
    ID3D12Resource* other = target;
    D3D12_RESOURCE_STATES sOther = D3D12_RESOURCE_STATE_RENDER_TARGET;
    if (onTarget) {
        other = scene;
        sOther = sScene;
        scene = target;
        sScene = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }
    // mode 2, or mode 1 without its target: the step runs for the probe, nothing of it goes into the picture, the game's pass keeps its fog
    const bool write = mode == 0 || onTarget;
    g_lastWrote.store(write, std::memory_order_relaxed);
    {
        static int toldOn = -1;
        if (toldOn != (int)onTarget) {
            toldOn = (int)onTarget;
            if (onTarget)
                Line("vrcam: fog before the effects - fogging the resolve's own target %p (the lit picture; the resolve blends its effects over it), "
                     "not the tables' scene %p", (void*)scene, (void*)other);
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
    FxPart fa, fg, fr;   // (fr: the effects' revealage - our revealage multiplied in, copied back)
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
    // Without the effects' own depth: their weighted colour target fogged by the depth behind it (the game's fog pass laid it over them so;
    // the step makes it leave its colour out) - the additive light stays. Only while the step writes, with its state known.
    if (!p.fxDepth && write && !p.ours && !fa.game) {
        ID3D12Resource* acc = g_fxAccum.load();
        const D3D12_RESOURCE_DESC ad = acc ? acc->GetDesc() : D3D12_RESOURCE_DESC{};
        if (acc && ad.Width == sd.Width && ad.Height == sd.Height && ad.SampleDesc.Count == 1 && ad.DepthOrArraySize == 1 &&
            ad.Format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
            const D3D12_RESOURCE_STATES readable = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            Known k{};
            const bool own = OwnState(l, acc, &k);
            l.stepRes[1] = acc;
            l.stepOwn[1] = own;
            l.stepOnChain[1] = false;
            ChainWatch(1, acc);
            D3D12_RESOURCE_STATES s{};
            bool ok = false;
            const char* how = "";
            if (own) { s = k.s; ok = k.has0 && (s & D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) && !(s & ~readable); how = "set on the resolve's list"; }
            else if (ChainProven(1, acc, &s)) { ok = true; l.stepOnChain[1] = true; l.stepState[1] = s; how = "proven by the submission order"; }
            else if (ReadState(l, acc, &s)) { ok = true; how = "a read state set on another list this frame"; }
            static int toldOk = -1;
            if (toldOk != (int)ok) {
                toldOk = (int)ok;
                Line("vrcam: fog before the effects - the effects' weighted colour: %s", ok ? how : "its state not known yet - not fogged (the additive and the scene are)");
            }
            // Why not known (Serg 2026-11-09, 0.210.2: in every session so far "not known yet" and never known - the hair / foliage / particle
            // fog never ran in the game; the submission order never lost it either, so it never saw a barrier on it): its flags, this list's and
            // any list's last barrier on it, the chain's view - once for each target, again every 30 s while not known
            if (!ok) {
                static ID3D12Resource* toldFor = nullptr;
                static ULONGLONG toldAt = 0;
                const ULONGLONG now = GetTickCount64();
                if (toldFor != acc || now - toldAt > 30000) {
                    toldFor = acc;
                    toldAt = now;
                    Known ka{};
                    const bool anyK = AnyState(acc, &ka);
                    AcquireSRWLockShared(&g_chainLock);
                    const Chain ch = g_chain[1];
                    ReleaseSRWLockShared(&g_chainLock);
                    Line("vrcam: fog before the effects - the weighted colour %p not known: flags 0x%X (simultaneous access %d), %u mips, %u slices; on "
                         "the resolve's list %s (0x%X, subresource 0 %s, all %s); last recorded anywhere %s (0x%X, frame %u, now %u); the submission "
                         "order: %s, state 0x%X, %d runs; barriers seen on it %u", (void*)acc, (unsigned)ad.Flags,
                         (ad.Flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS) ? 1 : 0, (unsigned)ad.MipLevels, (unsigned)ad.DepthOrArraySize,
                         own ? "set" : "none", own ? (unsigned)k.s : 0u, own && k.has0 ? "moved" : "not moved", own && k.whole ? "yes" : "no", anyK ? "yes" : "never", anyK ? (unsigned)ka.s : 0u, anyK ? ka.frame : 0u,
                         g_frame.load(), ch.res == acc ? (ch.known ? "known" : "no barrier seen") : "another target followed", (unsigned)ch.state, ch.runs,
                         g_accumBarriers.load());
                }
            }
            if (ok && !MakeHair(dev, ad.Width, ad.Height)) ok = false;   // (g_gpuMx held here)
            ID3D12Resource* rev = ok ? g_fxReveal.load() : nullptr;   // the revealage: its state as the weighted colour's, else by the depth behind
            const D3D12_RESOURCE_DESC rd2 = rev ? rev->GetDesc() : D3D12_RESOURCE_DESC{};
            if (rev && rd2.Width == ad.Width && rd2.Height == ad.Height && rd2.Format == DXGI_FORMAT_R16G16B16A16_FLOAT && rd2.SampleDesc.Count == 1) {
                Known kr{};
                const bool ownR = OwnState(l, rev, &kr);
                l.stepRes[2] = rev;
                l.stepOwn[2] = ownR;
                l.stepOnChain[2] = false;
                ChainWatch(2, rev);
                D3D12_RESOURCE_STATES sr{};
                bool okR = false;
                if (ownR) okR = kr.has0 && (kr.s & D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) && !(kr.s & ~readable), sr = kr.s;
                else if (ChainProven(2, rev, &sr)) { okR = true; l.stepOnChain[2] = true; l.stepState[2] = sr; }
                else if (ReadState(l, rev, &sr)) okR = true;
                if (okR && MakeOwn(g.revealTex, g.revealW, g.revealH, g.revealF, rd2.Width, rd2.Height, rd2.Format, L"d2rvr fog before fx reveal")) {
                    fr.game = rev;
                    fr.s = sr;
                    const D3D12_RESOURCE_STATES ro2 = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                    fr.r = (sr & ~ro2) == 0 && sr != D3D12_RESOURCE_STATE_COMMON ? (sr | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
                                                                                : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                    fr.ours = g.revealTex;
                    fr.z = nullptr;
                    fr.d = rd2;
                }
                static int toldR = -1;
                if (toldR != (int)okR) {
                    toldR = (int)okR;
                    Line("vrcam: fog before the effects - the effects' revealage %p: %s", (void*)rev, okR ? "its state known - the hair and foliage fogged by their own distance"
                                                                                                    : "its state not known - the hair and foliage fogged by the depth behind them");
                }
            }
            if (ok && MakeOwn(g.accumTex, g.accumW, g.accumH, g.accumF, ad.Width, ad.Height, ad.Format, L"d2rvr fog before fx accum")) {
                fa.game = acc;
                fa.s = s;
                const D3D12_RESOURCE_STATES ro = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                fa.r = (s & ~ro) == 0 && s != D3D12_RESOURCE_STATE_COMMON ? (s | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
                                                                          : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                fa.ours = g.accumTex;
                fa.z = nullptr;
                fa.d = ad;
            }
        }
    }
    // The additive light target (the one noted with the pair) faded by the particles' distance - only with the revealage known (the distance)
    if (!p.fxDepth && fa.game && !fa.z && fr.game && !fg.game) {
        ID3D12Resource* add = nullptr;
        AcquireSRWLockShared(&g_fxR11Lock);
        add = g_fxR11[(g_fxR11Next + kFxR11 - 1) % kFxR11];
        ReleaseSRWLockShared(&g_fxR11Lock);
        const D3D12_RESOURCE_DESC gd = add ? add->GetDesc() : D3D12_RESOURCE_DESC{};
        if (add && gd.Format == DXGI_FORMAT_R11G11B10_FLOAT && gd.SampleDesc.Count == 1 && gd.DepthOrArraySize == 1 && gd.Width <= sd.Width &&
            gd.Height <= sd.Height) {
            const D3D12_RESOURCE_STATES readable = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            Known kg{};
            const bool ownG = OwnState(l, add, &kg);
            l.stepRes[3] = add;
            l.stepOwn[3] = ownG;
            l.stepOnChain[3] = false;
            ChainWatch(3, add);
            D3D12_RESOURCE_STATES sg{};
            bool okG = false;
            if (ownG) { okG = kg.has0 && (kg.s & D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) && !(kg.s & ~readable); sg = kg.s; }
            else if (ChainProven(3, add, &sg)) { okG = true; l.stepOnChain[3] = true; l.stepState[3] = sg; }
            else if (ReadState(l, add, &sg)) okG = true;
            if (okG && MakeOwn(g.glowTex, g.glowW, g.glowH, g.glowF, gd.Width, gd.Height, gd.Format, L"d2rvr fog before fx glow")) {
                fg.game = add;
                fg.s = sg;
                fg.r = (sg & ~readable) == 0 && sg != D3D12_RESOURCE_STATE_COMMON ? (sg | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
                                                                                 : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                fg.ours = g.glowTex;
                fg.z = nullptr;
                fg.d = gd;
            }
            static int toldG = -1;
            if (toldG != (int)okG) {
                toldG = (int)okG;
                Line("vrcam: fog before the effects - the effects' additive light %p (%llux%u): %s", (void*)add, (unsigned long long)gd.Width, gd.Height,
                     okG ? "faded by the particles' own distance (else the depth behind)" : "its state not known - as drawn");
            }
        }
    }
    if (p.fxDepth) {   // (no step taking the effects' depth for long: the twins wait - OnDraw)
        if (fa.game || fg.game) g_fxIdle.store(0, std::memory_order_relaxed);
        else g_fxIdle.fetch_add(1, std::memory_order_relaxed);
    }
    const uint32_t step = g_slot++;
    t_stepped = true;
    const uint32_t k = step % kSlots;
    g_slotMode[k] = onTarget ? 1 : 0;
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
    dev->CreateShaderResourceView(fa.game && fa.z ? fa.z->tex : nullptr, &sv, at(2));
    sv.Format = fa.game ? Typed(fa.d.Format) : DXGI_FORMAT_R16G16B16A16_FLOAT;
    dev->CreateShaderResourceView(fa.game, &sv, at(3));
    sv.Format = DXGI_FORMAT_R32_FLOAT;
    dev->CreateShaderResourceView(fg.game && fg.z ? fg.z->tex : nullptr, &sv, at(4));
    sv.Format = DXGI_FORMAT_R11G11B10_FLOAT;
    dev->CreateShaderResourceView(fg.game, &sv, at(5));
    D3D12_UNORDERED_ACCESS_VIEW_DESC uv{};
    uv.Format = DXGI_FORMAT_R11G11B10_FLOAT;
    uv.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    // t6: the probe's second read (the resolve's own target, or in mode 1 the tables' scene); none: a view of nothing (reads black)
    sv.Format = DXGI_FORMAT_R11G11B10_FLOAT;
    dev->CreateShaderResourceView(other, &sv, at(6));
    const bool hair = fa.game && !fa.z && g.hairTex;   // (the hair / foliage folded in: the weighted colour taken without the effects' depth)
    sv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    dev->CreateShaderResourceView(hair ? g.hairTex : nullptr, &sv, at(7));
    const bool revealOn = hair && fr.game && g.hairReveal;   // (their revealage ours too: their own distance)
    dev->CreateShaderResourceView(revealOn ? g.hairReveal : nullptr, &sv, at(8));
    sv.Format = revealOn ? Typed(fr.d.Format) : DXGI_FORMAT_R16G16B16A16_FLOAT;
    dev->CreateShaderResourceView(revealOn ? fr.game : nullptr, &sv, at(9));
    dev->CreateUnorderedAccessView(g.tex, nullptr, &uv, at(10));
    uv.Format = fa.game ? Typed(fa.d.Format) : DXGI_FORMAT_R16G16B16A16_FLOAT;
    dev->CreateUnorderedAccessView(fa.ours, nullptr, &uv, at(11));
    uv.Format = DXGI_FORMAT_R11G11B10_FLOAT;
    dev->CreateUnorderedAccessView(fg.ours, nullptr, &uv, at(12));
    D3D12_UNORDERED_ACCESS_VIEW_DESC pv{};
    pv.Format = DXGI_FORMAT_R32_TYPELESS;
    pv.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    pv.Buffer.NumElements = 128;
    pv.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    dev->CreateUnorderedAccessView(g.probe, nullptr, &pv, at(13));
    uv.Format = revealOn ? Typed(fr.d.Format) : DXGI_FORMAT_R16G16B16A16_FLOAT;
    dev->CreateUnorderedAccessView(revealOn ? fr.ours : nullptr, nullptr, &uv, at(14));

    const D3D12_RESOURCE_STATES readOnly = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    const D3D12_RESOURCE_STATES rScene =
        (sScene & ~readOnly) == 0 && sScene != D3D12_RESOURCE_STATE_COMMON ? (sScene | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
                                                                             : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    const D3D12_RESOURCE_STATES rOther =
        (sOther & ~readOnly) == 0 && sOther != D3D12_RESOURCE_STATE_COMMON ? (sOther | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
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
    c.planeN = p.ours ? 0.0f : (float)std::clamp(p.planes, 0, 64);   // ([fog] game_fog_planes: the game's fog only)
    c.pStart = p.planesStart;
    c.pEnd = std::max(p.planesEnd, p.planesStart + 1.0f);
    c.pStrength = std::clamp(p.planesStrength, 0.0f, 1.0f);
    c.pSoft = std::clamp(p.planesSoft, 0.0f, 1.0f);
    if (c.planeN > 0.0f) c.curveOn = 0.0f;   // (the planes in place of either curve)
    c.accumBehind = hair ? 1.0f : 0.0f;   // (the hair / foliage folded in: no effects' depth)
    c.revealOn = revealOn ? 1.0f : 0.0f;
    c.coverAt = p.ours ? 0.0f : std::max(p.cover, 0.0f);
    c.partX = std::clamp(p.part[0], 0.25f, 1.0f);   // (an upscaler's share of the target: the pixels' rays from that part)
    c.partY = std::clamp(p.part[1], 0.25f, 1.0f);
    c.ownBehind = p.ownBehind ? 1.0f : 0.0f;   // ([fog] game_fog_own_distance_behind)
    c.layersInOrder = p.layersInOrder ? 1.0f : 0.0f;   // ([fog] game_fog_layers_in_order)
    g_planesDrawnInto.store(write ? (onTarget ? 2 : 1) : 0, std::memory_order_relaxed);

    natived3d::SetOurs(true);
    cl->EndQuery(g.queries, D3D12_QUERY_TYPE_TIMESTAMP, 2 * k);
    D3D12_RESOURCE_BARRIER b[6];
    UINT nb = 0;
    if (rScene != sScene) b[nb++] = Transition(scene, sScene, rScene, 0);
    if (rDepth != sDepth) b[nb++] = Transition(depth, sDepth, rDepth);
    if (other && rOther != sOther) b[nb++] = Transition(other, sOther, rOther, 0);
    if (hair && g.hairFresh) {   // (just made: zeroed - the revealage must start at 1)
        const float zero[4] = {0, 0, 0, 0}, ones[4] = {1, 1, 1, 1};
        cl->ClearRenderTargetView(g.rtvHeap->GetCPUDescriptorHandleForHeapStart(), zero, 0, nullptr);
        cl->ClearRenderTargetView(D3D12_CPU_DESCRIPTOR_HANDLE{g.rtvHeap->GetCPUDescriptorHandleForHeapStart().ptr + g.rtvInc}, ones, 0, nullptr);
        g.hairFresh = false;
    }
    if (hair) b[nb++] = Transition(g.hairTex, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (revealOn) b[nb++] = Transition(g.hairReveal, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (nb) cl->ResourceBarrier(nb, b);
    ID3D12DescriptorHeap* heaps[1] = {g.heap};
    cl->SetDescriptorHeaps(1, heaps);
    cl->SetComputeRootSignature(g.root);
    cl->SetPipelineState(g.cs);
    cl->SetComputeRoot32BitConstants(0, sizeof(Consts) / 4, &c, 0);
    cl->SetComputeRootDescriptorTable(1, gpu);
    cl->SetComputeRootDescriptorTable(2, D3D12_GPU_DESCRIPTOR_HANDLE{gpu.ptr + 10 * g.inc});
    cl->Dispatch((UINT)((sd.Width + 7) / 8), (sd.Height + 7) / 8, 1);
    FxPart* parts[3] = {&fa, &fg, &fr};
    if (fa.game || fg.game) {   // the effects': their targets and our depths read
        D3D12_RESOURCE_BARRIER fb[7];
        UINT nf = 0;
        for (FxPart* f : parts) {
            if (!f->game) continue;
            if (f->r != f->s) fb[nf++] = Transition(f->game, f->s, f->r, 0);
            if (f->z) fb[nf++] = Transition(f->z->tex, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        if (fa.game) {   // (the probe's hair half after the main entry zeroed it: in that order)
            D3D12_RESOURCE_BARRIER ub{};
            ub.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            ub.UAV.pResource = g.probe;
            fb[nf++] = ub;
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
    D3D12_RESOURCE_BARRIER bx[5 + 9];
    nb = 0;
    bx[nb++] = Transition(g.tex, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    bx[nb++] = Transition(g.probe, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    if (write) bx[nb++] = Transition(scene, rScene, D3D12_RESOURCE_STATE_COPY_DEST, 0);
    else if (rScene != sScene) bx[nb++] = Transition(scene, rScene, sScene, 0);
    if (other && rOther != sOther) bx[nb++] = Transition(other, rOther, sOther, 0);
    if (rDepth != sDepth) bx[nb++] = Transition(depth, rDepth, sDepth);
    for (FxPart* f : parts) {
        if (!f->game) continue;
        bx[nb++] = Transition(f->ours, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        bx[nb++] = Transition(f->game, f->r, D3D12_RESOURCE_STATE_COPY_DEST, 0);
        if (f->z) bx[nb++] = Transition(f->z->tex, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    }
    cl->ResourceBarrier(nb, bx);
    {   // into the scene's first mip (CopyResource wants the same mips on both sides); the effects' targets the same way
        D3D12_TEXTURE_COPY_LOCATION dst{scene, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX}, src{g.tex, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
        dst.SubresourceIndex = 0;
        src.SubresourceIndex = 0;
        if (write) cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        cl->CopyBufferRegion(g.probeRb, (UINT64)k * kProbeBytes, g.probe, 0, kProbeBytes);
        for (FxPart* f : parts) {
            if (!f->game) continue;
            dst.pResource = f->game;
            src.pResource = f->ours;
            cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        }
    }
    nb = 0;
    if (write) bx[nb++] = Transition(scene, D3D12_RESOURCE_STATE_COPY_DEST, sScene, 0);
    bx[nb++] = Transition(g.tex, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    bx[nb++] = Transition(g.probe, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    for (FxPart* f : parts) {
        if (!f->game) continue;
        bx[nb++] = Transition(f->game, D3D12_RESOURCE_STATE_COPY_DEST, f->s, 0);
        bx[nb++] = Transition(f->ours, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    cl->ResourceBarrier(nb, bx);
    cl->EndQuery(g.queries, D3D12_QUERY_TYPE_TIMESTAMP, 2 * k + 1);
    if (hair) {   // ours render targets again, cleared for the next frame's hair and foliage (the revealage to 1)
        D3D12_RESOURCE_BARRIER hb[2] = {Transition(g.hairTex, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
                                        Transition(g.hairReveal, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)};
        cl->ResourceBarrier(revealOn ? 2 : 1, hb);
        const float zero[4] = {0, 0, 0, 0}, ones[4] = {1, 1, 1, 1};
        cl->ClearRenderTargetView(g.rtvHeap->GetCPUDescriptorHandleForHeapStart(), zero, 0, nullptr);
        // ([fog] game_fog_hair_strict: their revealage sent to ours but the game's not taken this step - not multiplied in, so cleared too,
        // never carried into a later frame's: it piled up there, the grass's opacity of frames before, and their distance read too far)
        const bool dropReveal = !revealOn && g.hairReveal && g_hairStrict.load(std::memory_order_relaxed) && g_revealSentSinceFold.load(std::memory_order_relaxed);
        if (revealOn || dropReveal) cl->ClearRenderTargetView(D3D12_CPU_DESCRIPTOR_HANDLE{g.rtvHeap->GetCPUDescriptorHandleForHeapStart().ptr + g.rtvInc}, ones, 0, nullptr);
        if (dropReveal) g_revealDropped.fetch_add(1, std::memory_order_relaxed);
        g_sentSinceFold.store(false, std::memory_order_relaxed);
        g_revealSentSinceFold.store(false, std::memory_order_relaxed);
    } else if (g_hairStrict.load(std::memory_order_relaxed) && g_sentSinceFold.exchange(false, std::memory_order_relaxed)) {
        // ([fog] game_fog_hair_strict: sent to ours, but this step took no weighted colour - cleared before a step reads ours again)
        g.hairFresh = true;
        g_revealSentSinceFold.store(false, std::memory_order_relaxed);
        g_unfolded.fetch_add(1, std::memory_order_relaxed);
    }
    g_foldOk.store(hair, std::memory_order_relaxed);   // (the next frame's hair / foliage may be sent there - folded at its step)
    g_foldReveal.store(revealOn, std::memory_order_relaxed);
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
    // a step drawn with the same scene and depth: after kWatchAfter in a row, only they (and the effects' two targets) are watched
    if (write && !g_watching.load(std::memory_order_relaxed)) {
        static ID3D12Resource* runScene = nullptr;
        static ID3D12Resource* runDepth = nullptr;
        if (scene != runScene || depth != runDepth) { runScene = scene; runDepth = depth; g_watchRun.store(0); }
        if (g_watchRun.fetch_add(1) + 1 >= kWatchAfter) {
            g_watchRes[0].store(scene);
            g_watchRes[1].store(depth);
            g_watchRes[2].store(g_fxAccum.load());
            ID3D12Resource* add = nullptr;
            AcquireSRWLockShared(&g_fxR11Lock);
            add = g_fxR11[(g_fxR11Next + kFxR11 - 1) % kFxR11];
            ReleaseSRWLockShared(&g_fxR11Lock);
            g_watchRes[3].store(add);
            g_watching.store(true);
            Line("vrcam: fog before the effects - watching only the scene %p, its depth %p and the effects' targets now (%u steps in a row): the "
                 "other barriers and bindings are no longer looked at; the full search again on anything else", (void*)scene, (void*)depth, kWatchAfter);
        }
    }
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
void Redirect(ID3D12GraphicsCommandList* cl, List& l) {
    ID3D12Resource* acc = g_fxAccum.load(std::memory_order_relaxed);
    bool oit = false;
    bool want = acc && l.rt0 == acc && g_foldOk.load(std::memory_order_relaxed) && g_sceneMode.load(std::memory_order_relaxed) != 2 &&
                g_lastWrote.load(std::memory_order_relaxed) && l.nRt >= 1 && IsFogBehind(l.pso, &oit);
    // [fog] game_fog_hair_strict: an opaque lit draw into the pooled texture (another pass's use of it) is never taken from its pass
    if (want && !oit && g_hairStrict.load(std::memory_order_relaxed)) {
        want = false;
        g_notOit.fetch_add(1, std::memory_order_relaxed);
    }
    if (want) g_sentSinceFold.store(true, std::memory_order_relaxed);   // (each draw sent: a step must fold or clear ours)
    if (acc && l.rt0 == acc && !want && l.pso) {   // (a draw into the weighted colour left as drawn: counted by its pipeline)
        AcquireSRWLockExclusive(&g_notSentLock);
        if (g_notSent.size() < 4096 || g_notSent.count(l.pso)) ++g_notSent[l.pso];
        ReleaseSRWLockExclusive(&g_notSentLock);
    }
    if (want == l.redirected) return;
    natived3d::SetOurs(true);
    if (want) {
        D3D12_CPU_DESCRIPTOR_HANDLE rts[8];
        const unsigned inc = natived3d::RtvIncrement();
        {
            std::lock_guard<std::mutex> one(g_gpuMx);
            const D3D12_RESOURCE_DESC ad = acc->GetDesc();
            ID3D12Device* dev = nullptr;
            if (!g_gpu.hairTex || g_gpu.hairW != ad.Width || g_gpu.hairH != ad.Height) {   // (made at the step; a size not matching: not sent)
                natived3d::SetOurs(false);
                return;
            }
            (void)dev;
            rts[0] = g_gpu.rtvHeap->GetCPUDescriptorHandleForHeapStart();
        }
        for (unsigned i = 1; i < l.nRt; ++i) rts[i] = l.single ? D3D12_CPU_DESCRIPTOR_HANDLE{l.rts[0].ptr + (SIZE_T)i * inc} : l.rts[i];
        // their revealage to ours as well (folded back at the step): from the two, the step reads each one's own distance (the weight the
        // game's shaders write, 1e6 / (1e5 + d^3), over the opacity) - a branch against the sky fogged by its own distance, not the sky's
        if (l.nRt >= 2 && g_foldReveal.load(std::memory_order_relaxed) && natived3d::RtvResource(rts[1].ptr) == g_fxReveal.load(std::memory_order_relaxed)) {
            std::lock_guard<std::mutex> one(g_gpuMx);
            if (g_gpu.hairReveal) {
                rts[1] = D3D12_CPU_DESCRIPTOR_HANDLE{g_gpu.rtvHeap->GetCPUDescriptorHandleForHeapStart().ptr + g_gpu.rtvInc};
                g_revealSentSinceFold.store(true, std::memory_order_relaxed);
            }
        }
        cl->OMSetRenderTargets(l.nRt, rts, FALSE, l.hasDsv ? &l.dsv : nullptr);
        g_redirected.fetch_add(1, std::memory_order_relaxed);
    } else {
        cl->OMSetRenderTargets(l.nRt, l.rts, l.single ? TRUE : FALSE, l.hasDsv ? &l.dsv : nullptr);
    }
    natived3d::SetOurs(false);
    l.redirected = want;
}
void OnIndirect(ID3D12GraphicsCommandList* cl) {
    if (!Wanted()) return;
    List* l = Of(cl);
    if (l->tracked) Redirect(cl, *l);
}
void OnDraw(ID3D12GraphicsCommandList* cl, bool indexed, unsigned n, unsigned inst, unsigned first, int base, unsigned firstInst) {
    if (!Wanted()) return;
    List* l = Of(cl);
    if (l->tracked && (l->redirected || l->rt0 == g_fxAccum.load(std::memory_order_relaxed))) Redirect(cl, *l);
    if (!IsResolvePso(l->pso)) {
        const SinkTime cpuTime(g_cpuSinks);   // (the twin's own in g_cpuTwins as well)
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
    if (!l->tracked) {
        g_drawUntracked.fetch_add(1, std::memory_order_relaxed);
        l->resolveStep = 0;
        g_lastResolveStep.store(0, std::memory_order_relaxed);
        return;
    }
    const uint32_t stepsBefore = g_slot;
    t_stepped = false;
    Insert(cl, *l);
    {   // [fog] game_fog_miss_keeps_fog: what the game's fog pass after this resolve is to do (StepDrewAt)
        const int8_t drew = t_stepped && g_lastWrote.load(std::memory_order_relaxed) ? 1 : 0;
        l->resolveStep = drew;
        g_lastResolveStep.store(drew, std::memory_order_relaxed);
    }
    (void)stepsBefore;
    if (!t_stepped && g_hairStrict.load(std::memory_order_relaxed) && g_sentSinceFold.exchange(false, std::memory_order_relaxed)) {
        // [fog] game_fog_hair_strict: the resolve went on without the step - the hair / foliage draws sent to ours this frame are not folded:
        // none sent from here on until a step folds again, and what ours holds cleared before the next step reads it (never a later frame's)
        if (g_foldOk.exchange(false, std::memory_order_relaxed)) g_unfolded.fetch_add(1, std::memory_order_relaxed);
        g_foldReveal.store(false, std::memory_order_relaxed);
        std::lock_guard<std::mutex> one(g_gpuMx);
        g_gpu.hairFresh = true;
    }
    l->FxDone();   // (the next effects on this list clear our depths again)
}

}  // namespace

bool Watching() { return g_watching.load(std::memory_order_relaxed); }
bool StepDrewAt(ID3D12GraphicsCommandList* cl) {
    // the list's own resolve first (the fog pass recorded after it on the same list: certain); else the last resolve anywhere; none yet:
    // as before (the colour left out while the step is drawn)
    if (cl) {
        if (List* l = Find(cl)) {
            if (l->tracked && l->resolveStep >= 0) {
                g_pickOwn.fetch_add(1, std::memory_order_relaxed);
                return l->resolveStep == 1;
            }
        }
    }
    const int last = g_lastResolveStep.load(std::memory_order_relaxed);
    if (last >= 0) {
        g_pickLast.fetch_add(1, std::memory_order_relaxed);
        return last == 1;
    }
    g_pickNone.fetch_add(1, std::memory_order_relaxed);
    return true;
}
void SetLog(LogFn log) { g_log = log; }
void OnExecute(ID3D12CommandQueue* q, unsigned n, ID3D12CommandList* const* lists) {
    if (!Wanted() || !q || !lists) return;
    const bool direct = q->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT;
    static uint32_t toldLost = 0;
    const char* lostWhy = nullptr;
    List* found[64];
    const unsigned nf = n < 64 ? n : 64;
    for (unsigned i = 0; i < nf; ++i) found[i] = Find(lists[i]);   // (each list looked up once)
    AcquireSRWLockExclusive(&g_chainLock);
    for (int ci = 0; ci < kChains; ++ci) {
        Chain& ch = g_chain[ci];
        ID3D12Resource* r = ch.res;
        for (unsigned i = 0; r && i < n; ++i) {
            List* l = i < nf ? found[i] : Find(lists[i]);
            if (!direct) {   // (another queue: its lists run beside the direct queue's - one barrier on it there and the order is lost)
                if (l) for (const Known& k : l->states) if (k.r == r) { ch.known = false; ch.runs = 0; lostWhy = "a list of another queue moves it"; }
                continue;
            }
            if (!l || !l->tracked) { ch.known = false; ch.runs = 0; lostWhy = "a list not followed"; continue; }
            if (l->stepRes[ci] == r && !l->stepOwn[ci]) {   // the resolve's list: the chain's state at its draw
                if (l->stepOnChain[ci] && (!ch.known || l->stepState[ci] != ch.state)) {   // (a step drawn on it: the chain must agree)
                    g_chainDoubts.fetch_add(1);
                    ch.runs = 0;
                    lostWhy = "a step drawn on it disagrees";
                } else if (ch.known) {
                    if (ch.runs > 0 && ch.at == ch.state) ++ch.runs;
                    else { ch.at = ch.state; ch.runs = 1; }
                } else ch.runs = 0;
            }
            for (const Known& k : l->states) {
                if (k.r != r) continue;
                if (!k.has0) break;   // (only other subresources moved on this list: subresource 0, all the step touches, as it was)
                if (ch.known && k.first0 != ch.state) { ch.runs = 0; lostWhy = "a transition not seen"; }
                ch.state = k.s;
                ch.known = true;
                break;
            }
        }
    }
    ReleaseSRWLockExclusive(&g_chainLock);
    if (lostWhy) {
        ReArm("the submission order in doubt");
        const uint32_t lost = g_chainLost.fetch_add(1) + 1;
        if (lost == 1 || lost - toldLost >= 1000) {
            toldLost = lost;
            Line("vrcam: fog before the effects - the scene's state in the submission order NOT proven (%s; %u times so far): drawn only from "
                 "states its own list sets", lostWhy, lost);
        }
    }
}
bool LastProbe(float out[80]) {
    std::lock_guard<std::mutex> one(g_gpuMx);
    if (!g_gpu.probes || !g_slot) return false;
    std::memcpy(out, g_gpu.probes + (size_t)((g_slot - 1) % kSlots) * (kProbeBytes / 4), kProbeMain);
    return true;
}

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
    k.fxIndirect = &OnIndirect;
}

void OnGraphicsPso(ID3D12Device*, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* d, ID3D12PipelineState* pso) {
    if (d && pso) NoteIfFogBehind(pso, d->PS.pShaderBytecode, d->PS.BytecodeLength);   // (the hair / foliage ones)
    if (d && pso && fogframe::IsResolve(d->PS.pShaderBytecode, d->PS.BytecodeLength)) AddResolve(pso, &d->BlendState.RenderTarget[0]);
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
    if (pso && fogheight::StreamPixelShader(d, &code, &size)) NoteIfFogBehind(pso, code, size);   // (the hair / foliage ones)
    if (pso && code && fogframe::IsResolve(code, size)) {
        D3D12_BLEND_DESC bd{};   // (none in the stream: D3D12's default, no blend)
        bd.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        size_t at = 0;
        if (d && d->pPipelineStateSubobjectStream &&
            psostream::Find((const uint8_t*)d->pPipelineStateSubobjectStream, d->SizeInBytes, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND, &at))
            std::memcpy(&bd, (const uint8_t*)d->pPipelineStateSubobjectStream + at, sizeof bd);
        AddResolve(pso, &bd.RenderTarget[0]);
        return;
    }
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
void MarkResolve(ID3D12PipelineState* pso, int blends) { AddResolve(pso, nullptr, blends); }
void MarkFogBehind(ID3D12PipelineState* pso, bool oit) { NoteFogBehind(pso, oit); }
bool HasOitWeight(const void* bytecode, size_t n) {
    // (DXBC immediates are whole 32-bit tokens: l(1000000.0) = 0x49742400, l(100000.0) = 0x47C35000 - looked for on token boundaries)
    if (!bytecode || n < 8) return false;
    const uint8_t* b = (const uint8_t*)bytecode;
    bool hasBig = false, hasSmall = false;
    for (size_t i = 0; i + 4 <= n && !(hasBig && hasSmall); i += 4) {
        uint32_t t;
        std::memcpy(&t, b + i, 4);
        hasBig = hasBig || t == 0x49742400u;
        hasSmall = hasSmall || t == 0x47C35000u;
    }
    return hasBig && hasSmall;
}

void Configure(const Params& p) {
    AcquireSRWLockExclusive(&g_paramLock);
    g_params = p;
    ReleaseSRWLockExclusive(&g_paramLock);
    g_ours.store(p.on && p.ours, std::memory_order_relaxed);
    g_fxDepth.store(p.on && p.fxDepth, std::memory_order_relaxed);
    g_on.store(p.on, std::memory_order_relaxed);
    {   // [fog] game_fog_hair_strict / game_fog_own_distance_behind: live, a line each change
        static int told = -1;
        const int now = (p.hairStrict ? 1 : 0) | (p.ownBehind ? 2 : 0);
        g_hairStrict.store(p.hairStrict, std::memory_order_relaxed);
        g_ownBehind.store(p.ownBehind, std::memory_order_relaxed);
        g_sceneOneSize.store(p.sceneOneSize, std::memory_order_relaxed);
        g_sceneByName.store(p.sceneByName, std::memory_order_relaxed);
        if (now != told && p.on) {
            told = now;
            Line("vrcam: fog before the effects - the hair / foliage: %s ([fog] game_fog_hair_strict %d); their own distance %s ([fog] "
                 "game_fog_own_distance_behind %d)", p.hairStrict ? "only draws writing the game's see-through weight sent to ours, nothing "
                 "left over from a step that did not fold it" : "every lit / hair draw into the effects' weighted colour sent (0.215)",
                 p.hairStrict ? 1 : 0, p.ownBehind ? "never past the scene behind them" : "as read from the weight (0.215)", p.ownBehind ? 1 : 0);
        }
    }
    // [fog] game_fog_cover: once each time it changes (Serg 2026-11-09 on 0.210.2: "не работает заглушка тумана" - the log said nothing of it)
    static float toldCover = -1.0f;
    const float cover = p.on && !p.ours ? std::max(p.cover, 0.0f) : 0.0f;
    if (std::fabs(cover - toldCover) > 0.5f) {
        toldCover = cover;
        if (cover > 0.5f)
            Line("vrcam: fog before the effects - the fog covers everything from %.0f units ([fog] game_fog_cover): the scene there the fog's colour "
                 "whole; the hair, foliage and particles too once the effects' weighted colour is taken (its line says)", cover);
        else Line("vrcam: fog before the effects - the fog's own curve all the way ([fog] game_fog_cover 0)");
    }
}

bool Drawn() {
    return g_on.load(std::memory_order_relaxed) && g_lastWrote.load(std::memory_order_relaxed) &&
           GetTickCount64() - g_lastDraw.load(std::memory_order_relaxed) < 250;
}
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
        ID3D12CommandList* lists[1] = {tt.list};
        q->ExecuteCommandLists(1, lists);   // (still ours: [stereo] native_replay's look at the queue leaves it out)
        q->Signal(tt.fence, ++tt.fenceAt);
        natived3d::SetOurs(false);
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

// The probe's verdict on one step (16 floats a point): the scene at the five points vs the resolve's own target there. 1 wrong (the scene
// black, the target lit), 0 right, -1 cannot say (the target dark too, or not read)
// (swapped: the step fogged the resolve's target - its read is then the first, the tables' scene the second)
// The mode the probe proves (16 floats a point; swapped: the step fogged the resolve's target - its read is then the first, the tables'
// scene the second): 0 the tables' scene holds a lit picture (at 3 of the 5 points or more, not the sky: a colour over kLitAt) and the
// resolve's target is not far brighter; 1 the tables' scene black where the resolve's target is lit at 3 points or more; 2 neither proven
// (both dark - 0.209.13's night camp: the lit world is laid in after the resolve; or too few points off the sky)
constexpr float kLitAt = 0.002f;
int ProbeVerdict(const float* r, bool swapped) {
    float scene = 0.0f, target = 0.0f;
    int sceneLit = 0, targetLit = 0, voters = 0;
    for (int i = 0; i < 5; ++i) {
        const float* q = r + i * 16;
        // (the sky does not vote - the void, the game's dome, anything past 2000 units: Serg 2026-11-09 on 0.209.16, a view of the sky - the
        // middle on the dome at 4072 - read "no lit picture" and the step went off for a while)
        // ... nor anything nearer than a unit (0.209.21, VR F4: "depth 1 -> view depth 0.1", black - a hand or an overlay at the near plane
        // voted dark, and with the rest of the view on the sky the step flipped off and on every few seconds)
        if (q[7] > 0.5f || !(q[0] > 1e-7f) || q[1] > 2000.0f || q[1] < 1.0f) continue;
        ++voters;
        const float first = q[4] + q[5] + q[6], second = q[12] + q[13] + q[14];
        const float sc = swapped ? second : first, tg = swapped ? first : second;
        scene += sc;
        target += tg;
        sceneLit += sc > kLitAt;
        targetLit += tg > kLitAt;
    }
    if (voters < 2) return -1;   // (too few points off the sky: no vote - the mode stays)
    const int need = std::max(2, (voters * 3 + 4) / 5);   // (3 of 5; of fewer, as many in proportion, at least 2)
    if (sceneLit >= need && !(scene < 0.1f * target)) return 0;
    if (targetLit >= need && scene < 0.1f * target) return 1;
    return 2;
}
// (the test: the check started again in a mode - 0 a lit scene taken as proven)
void ResetSceneCheck(int mode);
int g_run = 0, g_runWant = -1;   // the same verdict this many probes in a row (SceneCheck)
std::atomic<ID3D12Resource*> g_provenScene{nullptr};   // the scene a lit picture was proven in (mode 0 reached with it)
void ResetSceneCheck(int mode) {
    g_sceneMode.store(mode);
    g_run = 0;
    g_runWant = -1;
    g_provenScene.store(nullptr);
}
void SceneCheck() {
    static uint32_t lastStep = 0;
    float r[kProbeBytes / 4];
    uint32_t step = 0;
    {
        std::lock_guard<std::mutex> one(g_gpuMx);
        if (!g_gpu.probes || g_slot <= 8) return;
        step = g_slot - 8;   // (8 steps back: surely done)
        if (step == lastStep) return;
        std::memcpy(r, g_gpu.probes + (size_t)(step % kSlots) * (kProbeBytes / 4), kProbeBytes);
    }
    lastStep = step;
    int v = ProbeVerdict(r, g_slotMode[step % kSlots] == 1);
    // (a scene proven lit stays so through dark views - night, the sky, a menu over it: "neither lit" is no vote then; a black scene where the
    // resolve's target is lit still is)
    ID3D12Resource* followed = nullptr;
    AcquireSRWLockShared(&g_chainLock);
    followed = g_chain[0].res;
    ReleaseSRWLockShared(&g_chainLock);
    if (v == 2 && followed && g_provenScene.load() == followed) v = -1;
    if (v < 0) return;   // (the sky: no vote)
    const int blends = g_lastResolveBlends.load();
    const int want = v == 0 ? 0 : v == 1 && blends == 1 ? 1 : 2;   // (the target only when the resolve blends into it)
    if (want == g_runWant) ++g_run;
    else { g_runWant = want; g_run = 1; }
    const int mode = g_sceneMode.load();
    if (want == mode || g_run < kWrongRuns) return;
    g_sceneMode.store(want);
    g_run = 0;
    if (want == 0) g_provenScene.store(followed);
    if (want == 0)
        Line("vrcam: fog before the effects - the tables' scene holds a lit picture (%d probes in a row): fogged by the step, the game's pass "
             "leaves the fog's colour out", kWrongRuns);
    else if (want == 1)
        Line("vrcam: fog before the effects - the tables' scene is black where the resolve's own target is lit (%d probes in a row) and the resolve "
             "blends into its target: the step fogs that target now", kWrongRuns);
    else
        Line("vrcam: fog before the effects - no lit picture proven (%d probes in a row: %s; the resolve %s): nothing of ours written, the game's "
             "fog pass keeps its own fog", kWrongRuns, v == 1 ? "the tables' scene black, the resolve's target lit" : "neither the tables' scene "
             "nor the resolve's own target holds a lit picture at the probe's points - the lit world is laid in after the resolve",
             blends == 1 ? "blends into its target" : blends == 0 ? "writes its target whole" : "blend not known");
}
void Frame() {
    SceneCheck();
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
            Line("vrcam: fog before the effects, 10 s, its cost a present (%u presents; %s): on the CPU %.3f ms in the followed lists' calls, of them "
                 "%.3f ms drawing the effects' depth again and %.3f ms recording the step; on the graphics card the effects' depth %.3f ms a "
                 "present (%u presents sampled, %u draws timed)", presents, g_watching.load() ? "watching the found targets only" : "the full search", sinks * per, twins * per, stepCpu * per,
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
        if (g_gpu.probes && g_readUpTo) {
            const float* r = g_gpu.probes + (size_t)((g_readUpTo - 1) % kSlots) * (kProbeBytes / 4);
            static const char* const kAt[5] = {"centre", "15% 45%", "35% 45%", "65% 45%", "85% 45%"};
            char b[2000];
            const bool swapped = g_slotMode[(g_readUpTo - 1) % kSlots] == 1;
            const int modeNow = g_sceneMode.load();
            int len = snprintf(b, sizeof b, "vrcam: fog before the effects - the probe (what the step read and made, step %u; it %s %s; \"scene\" "
                               "is that, \"the other\" %s):", g_readUpTo - 1, modeNow == 2 ? "wrote NOTHING (no lit picture proven), read" : "fogged",
                               swapped ? "the resolve's own target" : "the tables' scene", swapped ? "the tables' scene" : "the resolve's own target");
            for (int i = 0; i < 5 && len > 0 && len < (int)sizeof b; ++i) {
                const float* q = r + i * 16;
                len += snprintf(b + len, sizeof b - len, "%s %s: depth %.3g -> view depth %.1f, distance %.1f, fog %.0f%%%s, scene %.3f %.3f %.3f -> "
                                "%.3f %.3f %.3f, the other %.3f %.3f %.3f", i ? ";" : "", kAt[i], q[0], q[1], q[2], 100.0f * (1.0f - q[3]),
                                q[7] > 0.5f ? " (sky)" : "", q[4], q[5], q[6], q[8], q[9], q[10], q[12], q[13], q[14]);
            }
            if (g_log) g_log(b);   // (longer than Line takes)
            // the hair / foliage half (HairProbeAt): only when the step folded them at one of the points
            bool any = false;
            for (int i = 0; i < 5; ++i) any = any || r[80 + i * 8 + 7] > 0.5f;
            if (any) {
                len = snprintf(b, sizeof b, "vrcam: fog before the effects - the probe's hair / foliage (ours, folded at the step; the limit at the "
                               "scene behind %s):", r[80 + 7] >= 4.0f || r[80 + 15] >= 4.0f ? "on" : "off");
                for (int i = 0; i < 5 && len > 0 && len < (int)sizeof b; ++i) {
                    const float* q = r + 80 + i * 8;
                    const int how = (int)(q[7] + 0.5f) & 3;
                    if (how == 0) len += snprintf(b + len, sizeof b - len, "%s %s: -", i ? ";" : "", kAt[i]);
                    else if (how == 2)
                        len += snprintf(b + len, sizeof b - len, "%s %s: weighted alpha %.4f, by the depth behind (view depth %.1f), fog %.0f%%", i ? ";" : "",
                                        kAt[i], q[0], q[4], 100.0f * (1.0f - q[5]));
                    else
                        len += snprintf(b + len, sizeof b - len, "%s %s: weighted alpha %.4f, opacity %.3f -> view depth read %.1f, used %.1f (the scene "
                                        "behind %.1f), fog %.0f%%; the particles' fog %.0f%%", i ? ";" : "", kAt[i], q[0], q[1], q[2], q[3], q[4],
                                        100.0f * (1.0f - q[5]), 100.0f * (1.0f - q[6]));
                }
                if (g_log) g_log(b);
            }
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
    {   // (the hair / foliage draws sent into ours; [fog] game_fog_hair_strict: the opaque lit draws into the same texture kept in their pass,
        // and what was sent but not folded cleared)
        const uint32_t sent = g_redirected.exchange(0), notOit = g_notOit.exchange(0), unfolded = g_unfolded.exchange(0), dropped = g_revealDropped.exchange(0);
        if (sent || notOit || unfolded || dropped)
            Line("vrcam: fog before the effects, 10 s: %u hair / foliage draws sent into our weighted colour target (fogged by their own "
                 "distance; %u such pipelines known, %u of them writing the game's see-through weight); %u opaque lit draws into the same "
                 "texture left in their own pass ([fog] game_fog_hair_strict %d); %u times sent but not folded (cleared), %u times their "
                 "revealage cleared unfolded; the particles as drawn", sent, g_fogBehindN.load(), g_fogBehindOitN.load(), notOit,
                 g_hairStrict.load() ? 1 : 0, unfolded, dropped);
    }
    {   // the draws into the weighted colour NOT sent, the most drawn pipelines by pixel shader hash and the bench's stage (the particles are
        // "-": fogged by their own distance at the step; a foliage or grass pipeline here is one the hair / foliage table misses)
        std::unordered_map<ID3D12PipelineState*, uint32_t> ns;
        AcquireSRWLockExclusive(&g_notSentLock);
        ns.swap(g_notSent);
        ReleaseSRWLockExclusive(&g_notSentLock);
        if (!ns.empty()) {
            std::vector<std::pair<uint32_t, ID3D12PipelineState*>> v;
            uint64_t total = 0;
            for (const auto& [pso, n] : ns) { v.push_back({n, pso}); total += n; }
            std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
            char b[1400];
            int len = snprintf(b, sizeof b, "vrcam: fog before the effects, 10 s: %llu draws into the effects' weighted colour left as drawn (%s), by %zu "
                               "pipelines; the most (pixel shader hash, stage, draws):", (unsigned long long)total,
                               g_foldOk.load() ? "the particles - fogged by their own distance at the step - and any pipeline not in the hair / foliage table"
                                               : "the fold not running: all of them", v.size());
            AcquireSRWLockShared(&g_fbLock);
            for (size_t i = 0; i < v.size() && i < 12 && len > 0 && len < (int)sizeof b - 80; ++i) {
                const auto it = g_psHashOf.find(v[i].second);
                const uint64_t h = it != g_psHashOf.end() ? it->second : 0;
                len += snprintf(b + len, sizeof b - len, " %016llx %s %u;", (unsigned long long)h,
                                h ? renderbench::StageName(renderbench::StageOf(h)) : "(hash not known)", v[i].first);
            }
            ReleaseSRWLockShared(&g_fbLock);
            Line("%s", b);
        }
    }
    {   // [fog] game_fog_planes: the planes, once each 10 s while they are asked for
        Params pp;
        AcquireSRWLockShared(&g_paramLock);
        pp = g_params;
        ReleaseSRWLockShared(&g_paramLock);
        if (pp.on && pp.planes > 0 && !pp.ours) {
            char b[1200];
            const int np = std::clamp(pp.planes, 1, 64);
            const int into = g_planesDrawnInto.load();
            int len = snprintf(b, sizeof b, "vrcam: fog planes before the player, 10 s: %d planes, soft %.2f, drawn %u times into %s, on the graphics card %.3f "
                               "ms a step; at view depth / alpha:", np, pp.planesSoft, draws,
                               into == 2 ? "the resolve's own target (the lit picture)" : into == 1 ? "the scene the resolve's tables name" : "NOTHING (the scene not the lit picture - the game's fog kept)",
                               n ? ms / n : 0.0);
            for (int i = 0; i < np && len > 0 && len < (int)sizeof b - 32; ++i)
                len += snprintf(b + len, sizeof b - len, " %.0f/%.2f", PlaneAt(i, np, pp.planesStart, pp.planesEnd), PlaneAlpha(i, np, pp.planesStrength));
            if (g_log) g_log(b);
        }
    }
    Line("vrcam: fog before the effects, 10 s: %u steps before the game's transparent resolve (%s)%s; on the graphics card %.3f ms a step (%d "
         "measured)%s (its pipeline bound %u times on followed lists, %u on others; its draws %u, %u of them on lists not followed)",
         draws, ours ? "our effect's fog" : "the game's fog", g_resolveN.load() ? "" : " - the game's resolve pipeline NOT found (another build?)",
         n ? ms / n : 0.0, n, g_on.load() && !draws ? (ours ? " - NONE drawn: our post pass's fog as before" : " - NONE drawn: the game's fog as before") : "",
         bound, boundU, drawn, drawnU);
    {   // [fog] game_fog_miss_keeps_fog: how the game's fog pass knew whether the step drew (vr/fog_height.cpp's line: which variant it took)
        const uint32_t own = g_pickOwn.exchange(0), last = g_pickLast.exchange(0), none = g_pickNone.exchange(0);
        if (own || last || none)
            Line("vrcam: fog before the effects, 10 s: the game's fog pass asked whether the step drew %u times - %u by its own list's resolve, "
                 "%u by the last resolve on another list, %u with none seen yet", own + last + none, own, last, none);
        const uint32_t byName = g_byName.exchange(0), bySearch = g_bySearch.exchange(0);
        if (byName || bySearch)
            Line("vrcam: fog before the effects, 10 s: the scene by the game's own name at %u resolves, by the search at %u ([fog] "
                 "game_fog_scene_by_name)", byName, bySearch);
        if (const uint32_t held = g_linesHeld.exchange(0))
            Line("vrcam: fog before the effects, 10 s: %u of its lines NOT written (each kind at most one a second after a burst of 5) - "
                 "something flips every frame", held);
    }
}

}  // namespace fogfx
