// flat_pick.h - [debug] flat_pick: the pure part of the flat mouse look's targeting log (docs/notes/flat_targeting.md). Pure; the test is
// cleanroom/tests/flat_pick_test.cpp, the wiring is vr/vrcam.cpp (FlatPickClick in the map click's hook, FlatPickWatch on the timer).
//
// A player (flat first person, 2026-10): "after a fashion of normal gameplay the targeting of monsters becomes very out of whack... monsters impossible
// to actively target, sometimes in 3rd person too, totally fine in the normal camera". The crosshair is the window's middle: every mouse message the
// game gets carries it (GameWndProc) and the game's mouse code asks d2rcam's ScreenToRay for the ray through its pixel. These say, from what the log
// gathers, whether the game's pixel is still the middle, and where the ray through it meets the hero's ground - nothing here changes anything.
#pragma once

#include <cmath>

namespace flatpick {

// How far the game's pixel is from the middle of its viewport, as a share of the half size on the worse axis: 0 = the middle, 1 = an edge.
// -1 when the viewport is not known.
inline float OffCentre(float px, float py, float vpW, float vpH) {
    if (!(vpW > 0.0f) || !(vpH > 0.0f)) return -1.0f;
    const float x = std::fabs(2.0f * px / vpW - 1.0f), y = std::fabs(2.0f * py / vpH - 1.0f);
    return x > y ? x : y;
}

// The game's pixel counts as the crosshair's while within this share of the half size (a pixel or two of rounding on any window).
constexpr float kCentreTol = 0.02f;
inline bool AtCentre(float px, float py, float vpW, float vpH) {
    const float off = OffCentre(px, py, vpW, vpH);
    return off >= 0.0f && off <= kCentreTol;
}

// A ray (origin o, direction d, world y up) against the ground plane y = groundY: how far along the ground from the origin it lands, and how
// steeply it looks down (degrees below the horizon; negative = above). hit false: the ray never comes down to that ground (looks up or level,
// or starts below it).
struct Ahead {
    bool hit = false;
    float along = 0.0f;   // world units, along the ground from under the origin
    float downDeg = 0.0f;
};
inline Ahead GroundAhead(const float o[3], const float d[3], float groundY) {
    Ahead a;
    const float l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (!(l > 1e-6f)) return a;
    const float dy = d[1] / l;
    a.downDeg = std::asin(dy < -1.0f ? 1.0f : dy > 1.0f ? -1.0f : -dy) * 57.2957795f;
    if (!(dy < -1e-4f)) return a;
    const float t = (groundY - o[1]) / d[1];
    if (!(t > 0.0f)) return a;
    a.along = std::sqrt(d[0] * d[0] + d[2] * d[2]) * t;
    a.hit = std::isfinite(a.along);
    return a;
}

// How much farther along the ground the ray lands per world unit the ground is off (the plane the game meets the ray with one unit higher or
// lower than the one the picture shows): 1 / tan(down). Straight down 0, the iso camera's ~45 degrees 1, a first-person look 10 degrees down ~5.7.
// 0 when the ray does not come down.
inline float SlipPerUnit(float downDeg) {
    if (!(downDeg > 0.05f)) return 0.0f;
    return 1.0f / std::tan(downDeg / 57.2957795f);
}

}  // namespace flatpick
