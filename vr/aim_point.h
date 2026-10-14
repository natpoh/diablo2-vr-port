#pragma once
// Where a cast along the hand aims, and the hand's direction on the game's grid: pure math
// (no game calls, no globals); tools/aim_point_test.cpp runs it over all headings.
//
// The game's check of a skill's target point is the function at RVA 0x4FE1F0 (called from 0x4FE076 with
// the limit 0x32 in r9d): the point is refused when its x differs from the hero's by more than 50 cells OR
// its y does - each axis alone, so the allowed area is a square round the hero, not a circle. Along a
// cardinal heading 50 cells is the end of it, along a diagonal 70. A refused point leaves the aim where it
// was. bearstonem/d2r-pcvr (MIT, steamvr/spell_aim.h) found this and puts its point 40 cells out.
#include <algorithm>
#include <cmath>

namespace aimpoint {

// The game's limit on either axis, for the tests' model of its check.
inline constexpr int kRefuseBeyondCells = 50;

// How far out along the hand we put a point at most: the limit less 10 cells for the hero's step between
// our point and the game's check of it. Every point this header makes is inside it.
inline constexpr float kMaxCells = 40.0f;

// Nearer than this a point is no better than the game's own (about 3 cells); Point gives up instead.
inline constexpr float kMinCells = 3.0f;

// The farthest Point may be asked for with its maxCells: the limit less 2 cells (the hero's step between our point and the game's check of it).
// A throw by gesture (vr/throw_aim.h) goes that far - a living-room throw of 30 m is 48 cells at 3.2 units per metre; every other cast keeps kMaxCells.
inline constexpr float kLongMaxCells = 48.0f;

// The point `cells` out from (fx, fy) along (gx, gy), in the game's grid cells, for the aim point of a cast.
// `cells` is held to maxCells (kMaxCells; a caller may ask for more, up to kLongMaxCells), and to the map: the command's x and y are unsigned words, so a hero near the
// edge of the coordinates gets a nearer point on the same line instead of one that wraps round.
// False (the outputs untouched) with no hero position, no direction, or less than kMinCells of room.
inline bool Point(float fx, float fy, float gx, float gy, float cells, float* px, float* py, float maxCells = kMaxCells) {
    if (!px || !py || !std::isfinite(fx) || !std::isfinite(fy) || !std::isfinite(gx) || !std::isfinite(gy) || !(cells > 0.0f)) return false;
    const float n = std::hypot(gx, gy);
    if (!(n > 0.5f)) return false;
    gx /= n; gy /= n;
    cells = std::min(cells, std::clamp(std::isfinite(maxCells) ? maxCells : kMaxCells, kMinCells, kLongMaxCells));
    constexpr float lo = 1.0f, hi = 65534.0f;
    if (gx < 0.0f) cells = std::min(cells, (fx - lo) / -gx); else if (gx > 0.0f) cells = std::min(cells, (hi - fx) / gx);
    if (gy < 0.0f) cells = std::min(cells, (fy - lo) / -gy); else if (gy > 0.0f) cells = std::min(cells, (hi - fy) / gy);
    if (!(cells >= kMinCells)) return false;
    *px = std::clamp(fx + gx * cells, lo, hi);   // the clamp only takes off what float rounding left past an edge
    *py = std::clamp(fy + gy * cells, lo, hi);
    return true;
}

// The aiming hand's direction on the ground (theta = atan2(z, x) in the world) as a unit vector on the game's
// grid. The grid's axes are the world's x and z, not mirrored (measured 2026-10-02; the log of a native OpenXR
// session, 2026-10-13, has the walk-learned turn at -0.2 to -1.1 degrees after a few thousand steps). `calOk`,
// `mirror` and `angle` are what the walk has taught (vrcam's g_cal) and override that when it is sure - except
// in native OpenXR (`native`), where they are only logged. The walk teaches by pairing the game's stick vector
// with the motion of the camera's look-at, which trails the hero on a spring, so right after a change of
// direction (a step back) the pairs can run against each other: that same log had the angle at -3.7 degrees
// after 15 steps, -10.2 after 150, -6.8 after 300, and a wrong angle turns every cast. The calibration is
// the world's against the grid, which no pose enters, so it has nothing to add in native OpenXR; the hand
// comes to the world's axes by the camera's own, as in every mode. (bearstonem/d2r-pcvr bypasses it there
// too: backpedalling flipped a cast.)
inline void GridDir(float theta, bool native, bool calOk, bool mirror, float angle, float* gx, float* gy) {
    const float tg = native || !calOk ? theta : mirror ? angle - theta : angle + theta;
    *gx = std::cos(tg);
    *gy = std::sin(tg);
}

}  // namespace aimpoint
