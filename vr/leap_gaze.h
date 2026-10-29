// leap_gaze.h - [gameplay] leap_by_gaze: the Barbarian's Leap and Leap Attack go where the head looks (VR F4). Pure maths, no game:
// cleanroom/tests/leap_gaze_test.cpp. The game's side is in vr/vrcam.cpp (LeapAimFor, by the spell resolver's hooks).
//
// What is decided here, in the game's grid cells round the hero (1 cell = 2 world units; the render world's units are the same across and up):
//   - where the head's gaze ray, from the eyes, meets the hero's own ground: its distance out along the gaze's heading on the ground;
//   - for Leap Attack, the monster the gaze is on: the nearest one whose body - an upright cylinder on the hero's ground at its cell - the ray
//     goes through;
//   - and the range: the gaze's point held to the skill's own range (the distance the game's own pad cast asks of its aim point, when it gives
//     one), and a gaze that meets no ground within it - level, above the horizon, too far - is the full range along its heading.
// Nothing of the leap is decided here: the cast is the game's own (its key, its slot), its range, cooldown, mana and arc are the game's. Only the
// point the game's aim gives the cast (or, for Leap Attack, the monster the game's own target lookups give it) is the gaze's.
//
// The same rule could serve the other movement skills (Charge, Whirlwind's path, Teleport): IsLeap() is the one place that says which skills.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace leapgaze {

constexpr int kLeap = 132, kLeapAttack = 143;   // skills.txt rows (Barbarian)
inline bool IsLeap(int skill) { return skill == kLeap || skill == kLeapAttack; }

constexpr float kMinCells = 1.0f;        // nearer than this the point is held out to it (a look at the feet: a short leap, not none)
constexpr float kMaxCells = 40.0f;       // = aimpoint::kMaxCells: never farther (the game refuses a point 50 cells off on either axis)
constexpr float kMinDown = 0.02f;        // the gaze's down per unit across the ground under this: level - it meets no ground
constexpr float kUnitsPerCell = 2.0f;    // game world units in one grid cell

// The skill's range in cells: the distance of the game's own aim point for the cast (`gameCells`, what its pad cast asks of AttackPoint), when that
// is one a leap can be (more than kMinCells + a margin: the game's ~3-cell default for an attack is not a leap's range); else kMaxCells - the game's own
// leap then holds the point to its range itself. Always within [kMinCells, kMaxCells].
inline float RangeCells(float gameCells) {
    if (!std::isfinite(gameCells) || !(gameCells > 3.5f)) return kMaxCells;
    return std::clamp(gameCells, kMinCells, kMaxCells);
}

// The gaze's point on the ground, as a distance out along its heading. eyeHCells: the eyes above the hero's ground (cells); downPerAcross: how
// far the gaze drops for each unit across the ground (-fwd.y / |fwd.xz|; + looks down); rangeCells: the skill's range (RangeCells), held to
// [kMinCells, hardMax] (kMaxCells for a leap; a point skill of [gameplay] teleport_circle passes its own, up to 48: vr/point_cast.h kHardCapCells).
struct Ground {
    bool met = false;      // the gaze meets the ground within the range
    float cells = 0.0f;    // the point's distance out (met: where it meets, held to [kMinCells, range]; else the full range)
};
inline Ground GroundCells(float eyeHCells, float downPerAcross, float rangeCells, float hardMax = kMaxCells) {
    Ground g;
    const float hi = std::isfinite(hardMax) ? std::max(kMinCells, hardMax) : kMaxCells;
    const float range = std::clamp(std::isfinite(rangeCells) ? rangeCells : hi, kMinCells, hi);
    g.cells = range;
    if (!(eyeHCells > 0.0f) || !std::isfinite(eyeHCells) || !(downPerAcross > kMinDown) || !std::isfinite(downPerAcross)) return g;
    const float s = eyeHCells / downPerAcross;
    if (!(s <= range)) return g;   // beyond the range: the full range along the heading
    g.met = true;
    g.cells = std::max(s, kMinCells);
    return g;
}

// A monster round the hero: its id and its cell against the hero's, on the game's grid (cells).
struct Monster {
    uint32_t id = 0;
    float dx = 0.0f, dy = 0.0f;
};
struct Body {
    float radiusCells = 0.6f;   // its cylinder's radius
    float topCells = 1.6f;      // its height above the ground
};
struct Pick {
    bool found = false;
    int index = -1;
    uint32_t id = 0;
    float along = 0.0f;    // its cell's distance out along the gaze's heading (cells)
    float across = 0.0f;   // ... and off it (+ to the left of the heading)
};
// The monster the gaze is on, for Leap Attack: the nearest whose body the ray goes through, not farther out than rangeCells. (gx, gy): the gaze's
// heading on the grid (need not be unit). The ray starts at the eyes, eyeHCells over the hero's cell, and drops downPerAcross a cell across;
// a body is an upright cylinder of `body`'s size standing on the hero's ground at the monster's cell (the leap's world is the hero's floor).
inline Pick PickMonster(float gx, float gy, float eyeHCells, float downPerAcross, float rangeCells, const Monster* m, int n, const Body& body = Body{}) {
    Pick p;
    const float len = std::hypot(gx, gy);
    if (!m || n <= 0 || !(len > 1e-4f) || !std::isfinite(len) || !std::isfinite(eyeHCells) || !std::isfinite(downPerAcross)) return p;
    gx /= len; gy /= len;
    const float range = std::clamp(std::isfinite(rangeCells) ? rangeCells : kMaxCells, kMinCells, kMaxCells);
    const float r = std::max(0.05f, body.radiusCells);
    float best = 1e30f;
    for (int i = 0; i < n; ++i) {
        const float along = m[i].dx * gx + m[i].dy * gy, across = m[i].dy * gx - m[i].dx * gy;
        if (!std::isfinite(along) || !std::isfinite(across) || !(along > 0.0f) || along - r > range || std::fabs(across) > r) continue;
        // the ray's stretch inside the cylinder (seen from above, a chord through the circle) - its height there must cross [0, top]
        const float half = std::sqrt(std::max(0.0f, r * r - across * across));
        const float a0 = std::max(0.0f, along - half), a1 = along + half;
        const float h0 = eyeHCells - a0 * downPerAcross, h1 = eyeHCells - a1 * downPerAcross;   // the ray's height entering and leaving it
        const float lo = std::min(h0, h1), hi = std::max(h0, h1);
        if (hi < 0.0f || lo > body.topCells) continue;   // over its head, or into the ground before it
        if (along < best) {
            best = along;
            p.found = true;
            p.index = i;
            p.id = m[i].id;
            p.along = along;
            p.across = across;
        }
    }
    return p;
}

// [gameplay] leap_arc: the monster at a point (px, py: cells against the hero's, on the grid) - the nearest whose cell is within radiusCells of it. along: its
// distance from the hero.
inline Pick PickNear(float px, float py, float radiusCells, const Monster* m, int n) {
    Pick p;
    if (!m || n <= 0 || !std::isfinite(px) || !std::isfinite(py) || !(radiusCells > 0.0f)) return p;
    float best = radiusCells * radiusCells;
    for (int i = 0; i < n; ++i) {
        const float dx = m[i].dx - px, dy = m[i].dy - py, d2 = dx * dx + dy * dy;
        if (!std::isfinite(d2) || d2 > best) continue;
        best = d2;
        p.found = true;
        p.index = i;
        p.id = m[i].id;
        p.along = std::hypot(m[i].dx, m[i].dy);
        p.across = std::sqrt(d2);
    }
    return p;
}

// The leap's point `cells` out from the hero's cell (fx, fy) along (gx, gy), held to [kMinCells, maxCells] (maxCells itself to hardMax) and to the map
// (the command's x and y are unsigned words: near the edge of the coordinates a nearer point on the same line, never one that wraps). False (outputs
// untouched) with no hero position, no heading, or less than kMinCells of room.
inline bool Point(float fx, float fy, float gx, float gy, float cells, float maxCells, float* px, float* py, float hardMax = kMaxCells) {
    if (!px || !py || !std::isfinite(fx) || !std::isfinite(fy) || !std::isfinite(gx) || !std::isfinite(gy) || !std::isfinite(cells)) return false;
    const float n = std::hypot(gx, gy);
    if (!(n > 1e-4f)) return false;
    gx /= n; gy /= n;
    const float top = std::isfinite(hardMax) ? std::max(kMinCells, hardMax) : kMaxCells;
    cells = std::clamp(cells, kMinCells, std::clamp(std::isfinite(maxCells) ? maxCells : top, kMinCells, top));
    constexpr float lo = 1.0f, hi = 65534.0f;
    if (gx < 0.0f) cells = std::min(cells, (fx - lo) / -gx); else if (gx > 0.0f) cells = std::min(cells, (hi - fx) / gx);
    if (gy < 0.0f) cells = std::min(cells, (fy - lo) / -gy); else if (gy > 0.0f) cells = std::min(cells, (hi - fy) / gy);
    if (!(cells >= kMinCells)) return false;
    *px = std::clamp(fx + gx * cells, lo, hi);
    *py = std::clamp(fy + gy * cells, lo, hi);
    return true;
}

// The gaze from the view: its down per unit across the ground (+ down), false for a gaze straight up or down or not finite (no heading on the ground).
inline bool DownPerAcross(float fx, float fy, float fz, float* down) {
    const float h = std::hypot(fx, fz);
    if (!(h > 1e-3f) || !std::isfinite(h) || !std::isfinite(fy)) return false;
    *down = -fy / h;
    return true;
}

}  // namespace leapgaze
