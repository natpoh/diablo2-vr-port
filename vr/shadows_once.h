// shadows_once.h - [render] shadows_once_a_pair: in the native one-pass replay ([stereo] native_replay=1) the command lists that only draw the
// shadow maps and the grass bending map are not submitted again for the right eye - the right eye samples the maps the left pass drew.
//
// Why it is the same picture (docs/notes/frame_cost_study.md sections 2 and 4, cut #1): the sun's shadow map is fitted from the game's own top-down
// camera (DirectLightShadows::UpdateMatrices, cleanroom/RECON.md - camfix never moves the light cameras), the other shadow maps (hair, 2048, the
// point lights' 512 copies) and the grass bending map (world space round the hero) do not depend on the eye at all. In the replay the right eye
// is the left pass's own lists again - the same light cameras, the same lights picked, the same hero pose (one CPU pass) - so drawing them a
// second time draws the same texels. ~31 % of an eye's draws (1,003 for the 4096 sun map in a list of its own, clf381 in the 2026-10-13 trace).
//
// How a list is known (by what it WRITES, from the plugin's own D3D12 hooks, vr/native_d3d12.h so* sinks; each recording since its Reset or
// creation is judged on its own):
//   - every target it binds is a shadow depth map (a depth format made for depth-stencil, square, a power of two from 256 to 8192) or the grass map (R8, square, a
//     power of two up to 1024); at least one bind;
//   - every clear, copy and resolve lands in one of those;
//   - its transition barriers touch only those, none split, and leave each of them in the state it found it (the right eye's lists then find
//     the maps as they found them in the left pass: the sequence without the skipped lists stays as the game made it);
//   - no indirect call, bundle, render pass, meta command, ray or mesh dispatch, atomic copy, immediate buffer write, enhanced barrier;
//   - a compute dispatch only after the grass map was bound in that list (the grass map's own update: the trace has its 5 dispatches there);
//   - a side that is the eye's own render size (the swap chain's, the scene depth's) or a half, quarter or eighth of it never counts as a map:
//     at render sizes 512 / 1024 / 2048 / 4096 the maps of that size are replayed as before (safe, not cut).
// A list that mixes the maps with eye work (any write outside them) is replayed whole. A list whose recording was not followed from its start
// (the tick switched on mid-recording, a list made before the hooks) is replayed whole.
//
// Rendering only, never the game's state: what is left out is our own second submission of the game's lists. F4 only (the replay is F4 only);
// the replay's pause paths (area change, long frame, resize: two passes) have nothing to leave out. Default 0 = every list replayed, as before.
// Two passes (native_replay=0): not cut - see docs/notes/shadows_once_a_pair.md (the right pass's cost is its CPU recording, which dropping its
// lists at submission would not save, and its point-light picks are its own).
//
// This header is the pure part (the classes, a list's judgement); vr/shadows_once.cpp holds the sinks and the lists' states; the test is
// tools/native_replay_test.cpp (section 6, WARP: a shadow-only list left out of the replay while the eye lists are replayed).
#pragma once

#include <cstdint>
#include <vector>

struct ID3D12GraphicsCommandList;
struct ID3D12CommandList;
namespace natived3d { struct Sinks; }

namespace shadowsonce {

// What a resource is to the judgement.
enum class Kind { kOther, kShadow, kGrass };

// A texture's description, as D3D12_RESOURCE_DESC has it (plain numbers: the header stays free of d3d12.h)
struct Tex {
    int dimension = 0;      // D3D12_RESOURCE_DIMENSION: 3 = TEXTURE2D
    unsigned format = 0;    // DXGI_FORMAT
    uint64_t width = 0;
    unsigned height = 0;
    unsigned samples = 1;
    unsigned flags = 0;     // D3D12_RESOURCE_FLAGS: 2 = ALLOW_DEPTH_STENCIL
};
inline constexpr unsigned kAllowDepthStencil = 0x2;

// DXGI_FORMAT numbers used here
inline constexpr unsigned kR32G8X24Typeless = 19, kD32FloatS8X24 = 20, kR32FloatX8X24Typeless = 21, kR32Typeless = 39, kD32Float = 40,
                          kR24G8Typeless = 44, kD24UnormS8 = 45, kR16Typeless = 53, kD16Unorm = 55, kR8Typeless = 60, kR8Unorm = 61,
                          kR8Uint = 62, kR8Snorm = 63, kR8Sint = 64;
inline bool DepthFormat(unsigned f) {
    return f == kR32G8X24Typeless || f == kD32FloatS8X24 || f == kR32Typeless || f == kD32Float || f == kR24G8Typeless || f == kD24UnormS8 ||
           f == kR16Typeless || f == kD16Unorm;
}
inline bool R8Format(unsigned f) { return f >= kR8Typeless && f <= kR8Sint; }
inline bool Pow2(uint64_t v) { return v && !(v & (v - 1)); }

// The eye's render sizes known (the swap chain's back buffer, the scene depth's): up to 4 sides
struct Eyes {
    unsigned side[4] = {};
    int n = 0;
    void Add(unsigned s) { if (s && n < 4) side[n++] = s; }
    // a side that is the eye's, or a half / quarter / eighth of it
    bool Near(uint64_t s) const {
        for (int i = 0; i < n; ++i)
            for (int k = 0; k <= 3; ++k)
                if ((side[i] >> k) == s && side[i] >> k) return true;
        return false;
    }
};

inline Kind Classify(const Tex& t, const Eyes& eyes) {
    if (t.dimension != 3 || t.samples > 1 || t.width != t.height || !Pow2(t.width) || eyes.Near(t.width)) return Kind::kOther;
    if (DepthFormat(t.format) && (t.flags & kAllowDepthStencil) && t.width >= 256 && t.width <= 8192) return Kind::kShadow;
    if (R8Format(t.format) && t.width >= 64 && t.width <= 1024) return Kind::kGrass;
    return Kind::kOther;
}

// Why a list is replayed (kSkip: it is not - only the maps)
enum class Why { kSkip, kNotFollowed, kNoTargets, kEyeTarget, kUnknownTarget, kWrite, kDispatch, kIndirect, kBarrier, kUnbalanced, kOther, kCount };
inline const char* WhyText(Why w) {
    switch (w) {
        case Why::kSkip: return "only the maps";
        case Why::kNotFollowed: return "not followed from its start";
        case Why::kNoTargets: return "no target bound";
        case Why::kEyeTarget: return "an eye target";
        case Why::kUnknownTarget: return "a target view not seen made";
        case Why::kWrite: return "a clear / copy outside the maps";
        case Why::kDispatch: return "a dispatch without the grass map";
        case Why::kIndirect: return "an indirect call";
        case Why::kBarrier: return "a barrier outside the maps / split";
        case Why::kUnbalanced: return "a map left in another state";
        case Why::kOther: return "another kind of write";
        case Why::kCount: break;
    }
    return "?";
}

// One recording of one list
struct List {
    struct Track { const void* res; unsigned sub; unsigned first; unsigned last; };
    bool followed = false;   // since its Reset or creation, with the tick on
    Why why = Why::kSkip;    // the first reason it must be replayed (kSkip: none yet)
    uint32_t binds = 0, draws = 0, dispatches = 0;
    bool grass = false;      // the grass map bound in this recording
    std::vector<Track> tracks;

    void Begin(bool on) {
        followed = on;
        why = Why::kSkip;
        binds = draws = dispatches = 0;
        grass = false;
        tracks.clear();
    }
    void Mark(Why w) { if (why == Why::kSkip) why = w; }
    // a bind's targets: `kinds` of each colour target and the depth-stencil (n of them, any order); `unknown`: a view not seen made
    void Bind(const Kind* kinds, int n, bool unknown) {
        ++binds;
        if (unknown) Mark(Why::kUnknownTarget);
        for (int i = 0; i < n; ++i) {
            if (kinds[i] == Kind::kOther) Mark(Why::kEyeTarget);
            if (kinds[i] == Kind::kGrass) grass = true;
        }
    }
    void Write(Kind dst) { if (dst == Kind::kOther) Mark(Why::kWrite); }   // a clear, copy, resolve into dst
    void Draw() { ++draws; }
    void Dispatch() {
        ++dispatches;
        if (!grass) Mark(Why::kDispatch);
    }
    void Indirect() { Mark(Why::kIndirect); }
    void Other() { Mark(Why::kOther); }
    // a transition barrier (D3D12_RESOURCE_STATES before / after; `split`: BEGIN_ONLY or END_ONLY)
    void Transition(const void* res, Kind k, unsigned sub, unsigned before, unsigned after, bool split) {
        if (k == Kind::kOther || split) { Mark(Why::kBarrier); return; }
        for (Track& t : tracks)
            if (t.res == res && t.sub == sub) { t.last = after; return; }
        tracks.push_back({res, sub, before, after});
    }
    void Uav(Kind k, bool any) { if (!any && k == Kind::kOther) Mark(Why::kBarrier); }   // a UAV barrier (any: on every UAV - harmless)
    void Aliasing() { Mark(Why::kBarrier); }

    Why Verdict() const {
        if (!followed) return Why::kNotFollowed;
        if (why != Why::kSkip) return why;
        if (!binds) return Why::kNoTargets;
        for (const Track& t : tracks)
            if (t.first != t.last) return Why::kUnbalanced;
        return Why::kSkip;
    }
};

// What the replay leaves out of a pair: the captured lists' verdicts in, the kept ones' indices out (in order). `on`: the tick, `mode1`: the
// replay draws with the right eye's camera (replay_right 1 / native). Off: every list kept.
template <class Sub, class VerdictFn>
inline void Filter(std::vector<Sub>& subs, bool on, VerdictFn verdict, uint32_t* skipped) {
    if (!on) return;
    size_t w = 0;
    for (size_t i = 0; i < subs.size(); ++i) {
        if (verdict(subs[i])) { if (skipped) ++*skipped; continue; }
        subs[w++] = subs[i];
    }
    subs.resize(w);
}

// ---- the runtime (vr/shadows_once.cpp)
using LogFn = void (*)(const char*);
// At the plugin's load, before natived3d::Install: the so* sinks filled (the hooks they need go in with the device). Called when the replay's
// hooks go in at the start and [render] shadows_once_a_pair is on then.
void FillSinks(natived3d::Sinks& k, LogFn log);
bool Hooked();
// [render] shadows_once_a_pair now (live once hooked: lists recorded from their next Reset on are followed)
void SetOn(bool on);
bool On();
// The eye's render sizes (the replay, once a pair: the back buffer's, the scene depth's)
void SetEyes(unsigned a, unsigned b, unsigned c, unsigned d);
// At the capture (the list closed, before the game's ExecuteCommandLists): this recording only draws the maps (not to be replayed). Counted.
bool MapsOnly(ID3D12CommandList* cl);
// The replay of a pair: `lists` replayed, `skipped` left out (their draws counted from their recordings)
void NotePair(uint32_t lists, uint32_t skipped);
// The 10 s line (the replay calls it with its own): lists and draws left out a pair, why the others were kept
void Report(double seconds);

}  // namespace shadowsonce
