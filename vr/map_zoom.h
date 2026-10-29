#pragma once
// map_zoom.h - [hud] map_stick_zoom: with the right laser on the map (in the hand, or the big map), the right stick up zooms in and down out
// (Serg, 2026-10-29: "Bear added a good thing: the map zooms in and out"). Pure: no game, no XR (cleanroom/tests/map_zoom_test.cpp).
//
// The idea is bearstonem/d2r-pcvr's (D2R VR Unleashed, MIT before 2026-10-29: steamvr/map_zoom.h 966dbf8 / a93757b, steamvr/wrist_full_map.h
// 3bb57af, vr/gamestate.cpp fullmapzoom); written here afresh for our laser and our maps. What each map can do:
//   - the map in the hand is the game's corner map, and the game draws nothing past its box: zooming in magnifies it about the hero's
//     marker (1x - 4x, kHandMax), 1x being the whole box as before;
//   - the big map is the game's whole-screen map, and that one the game itself draws at a zoom it is given per call - its scale function
//     (game_sigs.h AutomapScale) takes max(round(z * 8) / 8, 0.125), 0.5 its own: while the big map is up that zoom is the stick's
//     (gamestate.cpp, the map panel's own call only). The game draws whole 1/8 steps; the rest, under one step, is magnified (Remainder),
//     so the zoom glides instead of jumping.
// The stick: a mostly vertical push past kDead while the laser is on a map is the zoom's; from then both its axes are the zoom's (no turn,
// no walk / run toggle) until it comes back to the middle - also when the laser leaves the map meanwhile. A push that began elsewhere stays
// the game's to its end. kDoublings a second at full push. Not saved; each opening of the big map starts at the game's own zoom.

#include <algorithm>
#include <cmath>

namespace mapzoom {

constexpr float kHandMin = 1.0f, kHandMax = 4.0f;                      // the map in the hand: magnified
constexpr float kGameMin = 0.125f, kGameOwn = 0.5f, kGameMax = 2.0f;   // the big map: the game's own zoom (its floor, its own, as far as we go)
constexpr float kDoublings = 1.5f;   // per second at full push: 1x to 4x in 1.3 s
constexpr float kDead = 0.3f, kBack = 0.15f;   // a push counts past kDead; back under kBack each way is the middle

// The game's rounding (AutomapScale: max(round(z * 8) / 8, 0.125)) and the whole step at or below a zoom wanted.
inline float GameRounded(float z) { return std::isfinite(z) && z > 0.0f ? std::max(std::round(z * 8.0f) / 8.0f, kGameMin) : 0.0f; }
inline float GameStep(float wanted) {
    return std::isfinite(wanted) && wanted > 0.0f ? std::max(std::floor(wanted * 8.0f + 1e-3f) / 8.0f, kGameMin) : kGameMin;
}
// What the picture still magnifies for a zoom wanted when the game drew `drawn` (1 = none; never under 1).
inline float Remainder(float wanted, float drawn) {
    if (!(wanted > 0.0f) || !(drawn > 0.0f) || !std::isfinite(wanted) || !std::isfinite(drawn)) return 1.0f;
    return std::max(1.0f, wanted / drawn);
}
// The argument the scale call is given: the game's own unless asked (wanted > 0) and the game's own is sane; then the step for it.
inline float GameArg(float game, float wanted) {
    if (!(wanted > 0.0f) || !std::isfinite(wanted) || !(game >= 0.05f && game <= 4.0f)) return game;
    return GameStep(std::clamp(wanted, kGameMin, kGameMax));
}

// One frame of the stick (Stick::Step). take: x and y are the zoom's this frame (hidden from the game); factor: the zoom times this.
struct Frame { bool take = false; float factor = 1.0f; };
class Stick {
    bool held_ = false, foreign_ = false;   // foreign: a push that began off the map, the game's until the stick is back in the middle
public:
    bool Held() const { return held_; }
    // onMap: the laser on a map now; x, y: the right stick (+ right, + up); dt: seconds since the last frame.
    Frame Step(bool onMap, float x, float y, float dt) {
        Frame f;
        if (!std::isfinite(x) || !std::isfinite(y)) { x = 0.0f; y = 0.0f; }
        const bool middle = std::fabs(x) < kBack && std::fabs(y) < kBack;
        if (middle) { held_ = false; foreign_ = false; }
        if (!held_ && !middle && !onMap) foreign_ = true;
        if (!held_ && !foreign_ && onMap && std::fabs(y) > kDead && std::fabs(y) >= std::fabs(x)) held_ = true;
        f.take = held_;
        if (held_ && onMap && std::fabs(y) > kDead && dt > 0.0f && std::isfinite(dt)) {
            const float push = (std::fabs(y) - kDead) / (1.0f - kDead);
            f.factor = std::exp2((y > 0.0f ? 1.0f : -1.0f) * push * kDoublings * std::min(dt, 0.1f));
        }
        return f;
    }
    void Reset() { held_ = foreign_ = false; }
};

inline float Clamp(float z, float lo, float hi) { return std::isfinite(z) ? std::clamp(z, lo, hi) : lo; }

}  // namespace mapzoom
