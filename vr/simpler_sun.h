// simpler_sun.h - [render] sun_shadow_radius_m / sun_shadow_every: the game's own sun shadow map filled for less, in VR first person (F4). Serg
// 2026-11-08: "может быть сделать своё солнце более простое" - the game's map, its light and its lighting are kept; only what goes INTO the map
// costs less. Measured that morning (render bench, test 0.207.6): the sun's map is the biggest single block of the game's draws (~1,816 a present,
// more than the lit colour pass), and Serg's frame rate is bound by the processor's cost of the game's draws.
//
// The keys (sun_shadows 2 / sun_shadow_every 1 = as before):
//   0. sun_shadows: 0 = the sun without shadows - none of the sun's models is collected (no caster draw at all: the processor's saving) and right
//      after the game clears its sun map, the map is cleared again to kLitDepth, the value the game's lighting reads as "nothing in the way":
//      the sun lights everything, nothing casts a sun shadow. 1 = near casters only (sun_shadow_radius_m). 2 = as the game.
//      The game's depth is REVERSED (ps_terrain / ps_grass_lighting: a texel is a blocker when the receiver's depth is less than the map's, the
//      receiver's depth clamped to 1e-5..1; the PCF compare agrees), so "nothing in the way" is 0. The 10 s line says what the game clears its
//      map to (expected 0 already - then the clear of ours is not needed and not made).
//   1. sun_shadow_radius_m (with sun_shadows 1): only models within N metres of the hero (on the ground, from their origin) are collected into the
//      sun's map. The hero himself and what hangs on him (armour parts, weapons: their transform's parent is the hero) are never left out.
//   2. sun_shadow_every: the sun's map is drawn every Nth game frame and kept in between - only while the sun's light camera stands still (it is
//      fitted from the game's own top-down camera, which follows the hero: walking, every frame is drawn; standing - a fight, the camp - the map
//      is drawn every Nth). A kept frame's map is the last drawn one copied back in (a copy of ours, made right after it was drawn), so nothing
//      depends on the game keeping its map's memory from frame to frame.
//
// Where (the game's code, D2R 3.3.93787, docs/notes/simpler_sun.md): the shadow collectors in shadow_pass.cpp (0xF414D0, 0xF41640, 0xF417C0,
// 0xF41940, 0xF41A30) call ModelDrawCollect 0xEDB450 for each model with pass 1; its 5th argument is the model's TransformComponent (world
// matrix at +0x40, row-major, the translation at +0x70; parent entity at +0xC0) and its 9th the light's view-projection the model's draws are
// made with (row vectors: clip = [x y z 1] * M). The sun's light is the directional one: its projection is orthographic (the w column 0,0,0,1);
// spot and point lights' are perspective - they are never touched. A model left out is left out the game's own way: ModelDrawCollect returns at
// once without a draw for it (as it does for a model faded to nothing), so neither its collection nor its draw calls cost anything.
//
// Rendering only: nothing of the game's state, its units or its fight is touched (vr/head_shadow.cpp's hook on ModelDrawCollect, our own copy
// on the game's command list). F4 only; elsewhere every model is collected and every frame drawn.
//
// This header is the pure part (tested by cleanroom/tests/simpler_sun_test.cpp); vr/simpler_sun.cpp holds the hooks' side.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

struct ID3D12GraphicsCommandList;
struct ID3D12Resource;
struct D3D12_RESOURCE_BARRIER;
struct tagRECT;

namespace simplersun {

// ---- the light

// A light's view-projection is orthographic (the sun's) when its w column is 0, 0, 0, 1 (row vectors: m[3], m[7], m[11], m[15]); a spot or
// point light's projection puts the depth into w (m[11] = +-1, m[15] = 0).
inline bool Orthographic(const float m[16]) {
    for (int i = 0; i < 16; ++i)
        if (!std::isfinite(m[i])) return false;
    return std::fabs(m[3]) < 1e-5f && std::fabs(m[7]) < 1e-5f && std::fabs(m[11]) < 1e-5f && std::fabs(m[15] - 1.0f) < 1e-4f;
}

// A world point through a light's matrix (row vectors), divided by w
inline void Project(const float m[16], const float p[3], float out[3]) {
    const float x = p[0] * m[0] + p[1] * m[4] + p[2] * m[8] + m[12];
    const float y = p[0] * m[1] + p[1] * m[5] + p[2] * m[9] + m[13];
    const float z = p[0] * m[2] + p[1] * m[6] + p[2] * m[10] + m[14];
    float w = p[0] * m[3] + p[1] * m[7] + p[2] * m[11] + m[15];
    if (std::fabs(w) < 1e-8f) w = 1e-8f;
    out[0] = x / w;
    out[1] = y / w;
    out[2] = z / w;
}

// How far the map's picture of the ground round `center` moves from light matrix `a` to `b`: the most any of nine points (the centre and eight
// round it, `reach` world units out, at the centre's height and `reach` above) moves, in the map's texels (side: the map's width) across, and in
// its depth (0..1). Kept-frame rule: a map drawn with `a` may stand for `b` while both are under a fraction of a texel / a hair of depth.
struct Shift { float texels = 0.0f, depth = 0.0f; };
inline Shift LightShift(const float a[16], const float b[16], const float center[3], float reach, float side) {
    Shift s;
    const float dx[9] = {0, 1, -1, 0, 0, 1, -1, 1, -1}, dz[9] = {0, 0, 0, 1, -1, 1, 1, -1, -1};
    for (int up = 0; up < 2; ++up)
        for (int i = 0; i < 9; ++i) {
            const float p[3] = {center[0] + dx[i] * reach, center[1] + (up ? reach : 0.0f), center[2] + dz[i] * reach};
            float pa[3], pb[3];
            Project(a, p, pa);
            Project(b, p, pb);
            const float t = std::max(std::fabs(pa[0] - pb[0]), std::fabs(pa[1] - pb[1])) * 0.5f * side;   // (clip -1..1 across the map's side)
            s.texels = std::max(s.texels, t);
            s.depth = std::max(s.depth, std::fabs(pa[2] - pb[2]));
        }
    return s;
}
inline constexpr float kStillTexels = 0.35f;   // a kept map shifts less than this (the eye cannot tell a third of a texel of a 4096 map)
inline constexpr float kStillDepth = 2e-5f;    // ... and its depth less than this (the game's shadow bias is far larger)
inline bool Still(const float a[16], const float b[16], const float center[3], float reach, float side) {
    if (!Orthographic(a) || !Orthographic(b)) return false;
    const Shift s = LightShift(a, b, center, reach, side);
    return std::isfinite(s.texels) && std::isfinite(s.depth) && s.texels < kStillTexels && s.depth < kStillDepth;
}

// ---- 1. which models cast into the sun's map

inline constexpr float kMinRadiusM = 4.0f;   // a radius set lower is taken as this: the hero's own parts and what he holds always in it
inline constexpr uint32_t kNoEntity = 0xFFFFFu;

struct Hero {
    bool ok = false;          // the hero's place is known this frame
    float at[3] = {};         // where he stands (world units: the game camera's look-at, the point the sun's map is fitted round)
    float upm = 0.0f;         // world units a metre
    uint32_t entity = kNoEntity;   // his model's entity (the low 20 bits; kNoEntity: not known)
};

enum class Cast { kKeep, kCull, kHero, kOff, kNoHero, kNoShadow, kLearn };
// A model's say: kKeep / kCull by its distance; kHero: the hero or hung on him (kept); kOff: no radius set (kept); kNoHero: the hero's place not
// known (kept). `entity` and `parent`: their low 20 bits matter (the game's entity index); `pos`: the model's world translation.
inline Cast Select(float radiusM, const Hero& h, uint32_t entity, uint32_t parent, const float pos[3]) {
    if (!(radiusM > 0.0f)) return Cast::kOff;
    const uint32_t e = entity & kNoEntity, p = parent & kNoEntity, he = h.entity & kNoEntity;
    if (he != kNoEntity && (e == he || p == he)) return Cast::kHero;
    if (!h.ok || !(h.upm > 0.0f) || !std::isfinite(h.at[0] + h.at[2])) return Cast::kNoHero;
    if (!std::isfinite(pos[0] + pos[2])) return Cast::kKeep;
    const float r = std::max(radiusM, kMinRadiusM) * h.upm;
    const float dx = pos[0] - h.at[0], dz = pos[2] - h.at[2];
    return dx * dx + dz * dz <= r * r ? Cast::kKeep : Cast::kCull;
}
inline bool Culled(Cast c) { return c == Cast::kCull || c == Cast::kNoShadow; }

// [render] sun_shadows
enum Mode : int { kModeNone = 0, kModeNear = 1, kModeGame = 2 };
inline constexpr float kNearDefaultM = 15.0f;   // sun_shadows 1 with sun_shadow_radius_m 0
inline constexpr float kLitDepth = 0.0f;        // the far plane of the game's reversed depth: no caster in the way
// A sun model's say by the mode: none - left out (kNoShadow), but kept while the sun's map is still to be found (`learning`: its draws are what
// tell which depth target is the sun's); near - by the radius (kNearDefaultM when 0); as the game - kept (kOff).
inline Cast SelectMode(int mode, bool learning, float radiusM, const Hero& h, uint32_t entity, uint32_t parent, const float pos[3]) {
    if (mode == kModeNone) return learning ? Cast::kLearn : Cast::kNoShadow;
    if (mode == kModeNear) return Select(radiusM > 0.0f ? radiusM : kNearDefaultM, h, entity, parent, pos);
    return Cast::kOff;
}
// Our clear right after the game's clear of a depth target: only of the sun's map, with the sun without shadows, not while it is being found,
// only a depth clear, and only when the game's own value is not already the lit one.
inline bool LitClearNeeded(int mode, bool learning, bool sunMap, unsigned flags, float gameDepth) {
    return mode == kModeNone && !learning && sunMap && (flags & 0x1u) && gameDepth != kLitDepth;
}

// ---- 2. the sun's map drawn every Nth frame

// What a frame does with the sun's map
enum class Plan { kDraw, kHold };
// Why a frame draws (kHeld: it does not)
enum class Why { kHeld, kOff, kTurn, kNoKeep, kMoved, kNoLight, kNotF4, kParts, kCount };
inline const char* WhyText(Why w) {
    switch (w) {
        case Why::kHeld: return "kept";
        case Why::kOff: return "every frame asked";
        case Why::kTurn: return "its turn";
        case Why::kNoKeep: return "no copy kept yet";
        case Why::kMoved: return "the sun's camera moved";
        case Why::kNoLight: return "decided before the sun's models";
        case Why::kNotF4: return "not F4";
        case Why::kParts: return "the map drawn again after its end (in parts): no keeping";
        case Why::kCount: break;
    }
    return "?";
}

// The decision for one frame. every: the key (1 = every frame); drawnAgo: frames since the map was last drawn (1 = the frame before);
// keep: a copy of the last drawn map is kept, of the same map; still: the sun's camera stands where it stood when it was drawn (unknown = false);
// parts: the game was seen to draw into the map again after the barrier that ended it, in the same frame (Reopens) - a kept frame would leave
// that part empty, so nothing is kept any more (until the keys change).
struct Decision { Plan plan = Plan::kDraw; Why why = Why::kOff; };
inline Decision Decide(bool f4, int every, int drawnAgo, bool keep, bool lightKnown, bool still, bool parts = false) {
    if (!f4) return {Plan::kDraw, Why::kNotF4};
    if (every <= 1) return {Plan::kDraw, Why::kOff};
    if (parts) return {Plan::kDraw, Why::kParts};
    if (drawnAgo >= every) return {Plan::kDraw, Why::kTurn};
    if (!keep) return {Plan::kDraw, Why::kNoKeep};
    if (!lightKnown) return {Plan::kDraw, Why::kNoLight};
    if (!still) return {Plan::kDraw, Why::kMoved};
    return {Plan::kHold, Why::kHeld};
}

// One frame's decision taken once (the first of the render workers' collects, or the barrier that ends the map), by the frame's number: the
// hooks on any thread ask it; the first asks decides. Not thread-safe by itself (vr/simpler_sun.cpp holds a lock round it).
struct Frames {
    uint64_t frame = 0;        // the game frame now (the presents counted)
    uint64_t decided = 0;      // the frame the decision below is for (0 none)
    Decision now;
    uint64_t lastDrawn = 0;    // the last frame that drew the map
    bool keepValid = false;    // a copy of a drawn map was recorded
    // the present: the next frame begins
    void Next() { ++frame; }
    int DrawnAgo() const { return lastDrawn ? (int)std::min<uint64_t>(frame - lastDrawn, 1000000u) : 1000000; }
    bool Has() const { return decided == frame && frame; }
    const Decision& Take(const Decision& d) {
        decided = frame;
        now = d;
        if (d.plan == Plan::kDraw) lastDrawn = frame;
        return now;
    }
    void Reset() { decided = 0; lastDrawn = 0; keepValid = false; }
};

// ---- the barrier that ends the map's drawing

// D3D12_RESOURCE_STATES used here
inline constexpr unsigned kDepthWrite = 0x10, kCopyDest = 0x400, kCopySource = 0x800, kAllSubresources = 0xFFFFFFFFu;
// A transition barrier as the game records it (plain numbers: no d3d12.h here). type 0 = transition; flags 0 = not split.
struct Transition { int type = 0; unsigned flags = 0; const void* res = nullptr; unsigned sub = 0; unsigned before = 0, after = 0; };
// In a batch of the game's barriers, the one that takes `map` out of depth-write (its drawing done): its index, -1 none. *whole: it covers every
// subresource (ALL, or the map has one); a barrier on one slice of a map of several is found but not whole - the keep is not made then.
inline int MapDone(const Transition* b, unsigned n, const void* map, unsigned subresources, bool* whole) {
    if (whole) *whole = false;
    if (!map || !b) return -1;
    for (unsigned i = 0; i < n; ++i) {
        const Transition& t = b[i];
        if (t.type != 0 || t.res != map || t.flags != 0) continue;
        if (!(t.before & kDepthWrite) || (t.after & kDepthWrite)) continue;
        if (whole) *whole = t.sub == kAllSubresources || subresources <= 1;
        return (int)i;
    }
    return -1;
}

// After the barrier that ended the map this frame (our copy made there): does this batch take the map back INTO depth-write (a second part
// drawn - another slice, a clear again)? Then a kept frame would leave that part empty: the caller stops keeping.
inline bool Reopens(const Transition* b, unsigned n, const void* map) {
    if (!map || !b) return false;
    for (unsigned i = 0; i < n; ++i) {
        const Transition& t = b[i];
        if (t.type == 0 && t.res == map && !(t.before & kDepthWrite) && (t.after & kDepthWrite)) return true;
    }
    return false;
}

// ---- the log

// The 10 s line's numbers (vr/simpler_sun.cpp keeps them; Line() prints them beside "N pairs/s")
struct Counts {
    uint64_t presents = 0, f4Presents = 0;
    uint64_t sunKept = 0, sunCulled = 0, heroKept = 0, otherLights = 0, notRead = 0;   // models offered to the sun's map, other lights' models
    uint64_t heldModels = 0;                                                          // models not collected on a kept frame
    uint64_t framesDrawn = 0, framesHeld = 0;
    uint64_t why[(int)Why::kCount] = {};
    uint64_t copiesOut = 0, copiesIn = 0, notWhole = 0, keepMade = 0, keepFailed = 0, reopened = 0;
    uint64_t noShadow = 0, learning = 0, litClears = 0, gameClears = 0;
};

// ---- the runtime (vr/simpler_sun.cpp)
void SetLog(void (*log)(const char*));
// The keys, live (load_settings): the mode (sun_shadows: 0 none, 1 near, 2 as the game), radius in metres (mode 1; 0 = kNearDefaultM), every Nth
// frame (1 = every frame; modes 1 and 2). Anything but 2 / 1: the model draw collector's hook is asked for (vr/head_shadow.cpp) and vr/pipe_hunt's
// shadow tally runs.
void Configure(int mode, float radiusM, int every);
// The game cleared a depth-stencil (the plugin's clear sink, AFTER the real call - effects.cpp chains it behind [render] lens_mask's): `dsv` its
// view as given, `ds` its resource. With the sun without shadows, the sun's map is cleared again here to kLitDepth (ours, unseen by the sinks).
void OnClearDsv(ID3D12GraphicsCommandList* cl, uint64_t dsv, ID3D12Resource* ds, unsigned flags, float depth, uint8_t stencil, unsigned n,
                const tagRECT* rects);
// The hero, from vrcam's frame (vr/body/arms.cpp): the camera's look-at (world units), world units a metre, his model's entity (kNoEntity: not known).
// WantsHero: either key is on (the entity's lookup is skipped otherwise).
bool WantsHero();
void SetHero(const float at[3], float upm, uint32_t entity);
// The game's present (vr/render/effects/effects.cpp, after pipe_hunt's): the frame ends.
void OnPresent();
// The game's barriers, before the real call (effects.cpp's barrier sink): at the one that ends the sun map's drawing our copy is recorded on `cl`.
void OnBarrier(ID3D12GraphicsCommandList* cl, unsigned n, const D3D12_RESOURCE_BARRIER* b);
// The 10 s line (vr/frame/frame_order.cpp, under the render bench's): empty while off and already told so.
std::string Line();

}  // namespace simplersun
