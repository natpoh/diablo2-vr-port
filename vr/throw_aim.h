// throw_aim.h - where a throw by gesture goes: the pure maths (no game calls, no globals), unit test
// cleanroom/tests/throw_aim_test.cpp.
//
// A javelin or a throwing weapon flies along the line the hand was going. [hands] throw_autoaim_deg is a gentle
// help INSIDE a narrow cone around that line: a hostile monster within the cone's half-angle of the line, ahead of the
// hero, snaps the aim point to itself - the one nearest the line (smallest angle, then the nearest); nothing in the cone
// is exactly along the hand, no help at all. The game's own attack takes the point (its range, line of sight and relation
// checks stay the game's).
//
// A throwing POTION goes to a ground location. With the weapon-weight model on, that is the landing it works out itself (vr/throw_source.h: it
// has the mass, the lag and the real floor, and has already scaled the reach by [weight] throw_reach_pct - not scaled again here). Without it: where
// a real object thrown that way would land (the ballistics of the hand's PEAK velocity before the release and its height above the ground),
// scaled by [hands] throw_potion_reach_pct (a living room's gentle swing is far from a game screen's distances: default 800%, the weight tab's own
// reach). Either way it is held to [3, 48] cells (the game's own command check refuses a point more than 50 cells off on an axis: 48 keeps
// 2 cells for the hero's step; 3 is aim_point.h's, 48 its kLongMaxCells - every other cast keeps 40).
// Its auto-aim counts only the monsters within +-25% of that landing distance: a short lob never snaps to a far monster.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "aim_point.h"

namespace throwaim {

inline constexpr float kGravity = 9.81f;   // m/s^2
inline constexpr float kMinCells = aimpoint::kMinCells;   // a lob lands at least this far out (3: nearer, aimpoint::Point gives up and the game's own point stays)
inline constexpr float kMaxCells = aimpoint::kLongMaxCells;   // 48: the game refuses a point more than 50 cells off (a throw goes farther than a bow's or a spell's 40)
inline constexpr float kMaxConeDeg = 45.0f;
inline constexpr float kDefaultReachPct = 800.0f;   // [hands] throw_potion_reach_pct (the weight tab's [weight] throw_reach_pct is 800 too)
inline constexpr float kMinReachPct = 50.0f, kMaxReachPct = 1500.0f;
inline constexpr float kPotionWindow = 0.25f;   // the monsters a potion's auto-aim counts: its landing distance +- this share

// A monster on the game's grid, in cells (the game's world units are twice the cell).
struct Monster {
    uint32_t id = 0;
    float x = 0.0f, y = 0.0f;
};

struct Pick {
    bool found = false;
    uint32_t id = 0;
    float x = 0.0f, y = 0.0f;   // its place, cells
    float angleDeg = 0.0f;      // off the throw's line
    float cells = 0.0f;         // from the hero
};

// The monster to snap to: within coneDeg (half-angle, clamped to 0..45; 0 = none) of the line from the hero (hx, hy)
// along (dx, dy), ahead of the hero (angle under 90), at a distance of minCells..maxCells. The smallest angle wins;
// angles within half a degree of each other are one, and then the nearest does. Cheap: one pass.
inline Pick PickInCone(float hx, float hy, float dx, float dy, float coneDeg, float minCells, float maxCells, const Monster* m, size_t n) {
    Pick best;
    const float dl = std::sqrt(dx * dx + dy * dy);
    if (!m || n == 0 || !(coneDeg > 0.0f) || !(dl > 1e-6f) || !std::isfinite(coneDeg + dl + hx + hy)) return best;
    dx /= dl;
    dy /= dl;
    const float cone = std::min(coneDeg, kMaxConeDeg);
    const float cosCone = std::cos(cone * 0.017453292519943295f);
    for (size_t i = 0; i < n; ++i) {
        const float vx = m[i].x - hx, vy = m[i].y - hy;
        const float dist = std::sqrt(vx * vx + vy * vy);
        if (!std::isfinite(dist) || dist < minCells || dist > maxCells || !(dist > 1e-4f)) continue;
        const float c = (vx * dx + vy * dy) / dist;
        if (!(c > 0.0f) || c < cosCone) continue;   // behind the hero, or outside the cone
        const float angle = std::acos(std::min(1.0f, c)) * 57.29577951308232f;
        const bool better = !best.found || angle < best.angleDeg - 0.5f || (angle <= best.angleDeg + 0.5f && dist < best.cells);
        if (!better) continue;
        best.found = true;
        best.id = m[i].id;
        best.x = m[i].x;
        best.y = m[i].y;
        best.angleDeg = angle;
        best.cells = dist;
    }
    return best;
}

// Where a thrown object lands, metres from the hand along the throw's horizontal heading: it leaves at height h with the
// horizontal speed vh and the vertical speed vy (up positive), under gravity: t = (vy + sqrt(vy^2 + 2 g h)) / g, d = vh t.
// Nothing for a hand at no speed or no heading; a hand lower than the ground (h < 0) is the ground.
inline float LandingDistanceM(float vh, float vy, float h) {
    if (!std::isfinite(vh + vy + h) || vh <= 0.0f) return 0.0f;
    h = std::max(0.0f, h);
    const float t = (vy + std::sqrt(vy * vy + 2.0f * kGravity * h)) / kGravity;
    return t > 0.0f ? vh * t : 0.0f;
}

struct Reach {
    float metres = 0.0f;     // the real throw's distance
    float cells = 0.0f;      // scaled and clamped, what the point is placed at
    bool clamped = false;    // the clamp changed it
};

// Metres on the ground (already the distance the throw is to cover in the game) in cells with the world's scale (upm: game units per metre at
// the hero's size; a cell is two units), held to [kMinCells, kMaxCells]. No scaling by a percentage: that is ReachFor's, for the hand's own estimate.
inline Reach CellsFor(float metres, float upm) {
    Reach r;
    r.metres = std::isfinite(metres) && metres > 0.0f ? metres : 0.0f;
    const float u = std::clamp(std::isfinite(upm) ? upm : 4.0f, 1.0f, 12.0f);
    const float raw = r.metres * u * 0.5f;
    r.cells = std::clamp(raw, kMinCells, kMaxCells);
    r.clamped = raw < kMinCells || raw > kMaxCells;
    return r;
}

// The hand's own estimate of the throw's distance in cells: the metres of LandingDistanceM, scaled by reachPct (50..1500), then CellsFor.
inline Reach ReachFor(float vh, float vy, float h, float reachPct, float upm) {
    const float pct = std::clamp(std::isfinite(reachPct) ? reachPct : kDefaultReachPct, kMinReachPct, kMaxReachPct);
    Reach r = CellsFor(LandingDistanceM(vh, vy, h) * (pct * 0.01f), upm);
    r.metres = LandingDistanceM(vh, vy, h);   // (the real throw's metres, as it always was; cells are the scaled ones)
    return r;
}

// Where a throw's distance comes from, and the Reach it gives: the weight model's landing (weightLandingM > 0: its metres are final, the percentage
// is NOT applied again) or the hand's own estimate (ReachFor).
inline Reach ThrowReach(float weightLandingM, float vh, float vy, float h, float reachPct, float upm) {
    return weightLandingM > 0.0f ? CellsFor(weightLandingM, upm) : ReachFor(vh, vy, h, reachPct, upm);
}

// The monsters a potion's auto-aim counts: landing distance +- 25%.
inline void PotionWindow(float cells, float* lo, float* hi) {
    *lo = cells * (1.0f - kPotionWindow);
    *hi = cells * (1.0f + kPotionWindow);
}

}  // namespace throwaim
