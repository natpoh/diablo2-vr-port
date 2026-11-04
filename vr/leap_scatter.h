// leap_scatter.h - [weight] leap_landing_scatter (VR F4): the Barbarian lands from a Leap or a Leap Attack and the humanoids round the spot fly apart -
// drawn by the knock-down library (vr/knock_down.cpp: thrown off their feet, they lie, get up and walk home), their units moved only by the game's own
// knockback (vr/grab_move.cpp, as [weight] knock_down_moves_unit; single player / host). Pure (cleanroom/tests/leap_scatter_test.cpp).
//
// Serg (2026-11-04): «можно чтобы они разлетались при приземлении - сделать». The game's own Leap knocks back the monsters near where it lands (its skill's
// knockback; Leap Attack strikes its target and knocks nothing): those the game puts into its knockback (mode 13) at the landing are its own radius - each is
// thrown wherever it stands within kGameRadiusM. Every other humanoid within kRadiusM is thrown too (Leap Attack; a Leap whose knockback the game did not
// start on it), its unit following the drawn body by the game's knockback where the game allows it. Damage, the hit roll and the stun stay the game's.
//
//   the landing   the hero's model matrix over the camera's ground (vr/under_camera.h Measure's lift): up past kLeapLiftUnits it is a leap (the author's log:
//                 12 - 13.7 units at its top), back under kLandedUnits it has landed - once a leap;
//   the throw     ONE game cell outward (Serg 2026-11-04: «вокруг разлетались на 1 клетку»): the way from the spot rounded to the cell's eight neighbours,
//                 the unit asked the game's knockback onto that neighbour, and the drawn body thrown the same cell that way (its speed across the step's
//                 length over kFallS), so where it lies and where the game has it agree. The nearer it stood, the higher its arc (kUpNearMs at the spot to
//                 kUpEdgeMs at kRadiusM) - never farther. Heavier than kBigKg it only staggers and is not moved (a big one keeps its feet - bosses and the like);
//   landed on it  (Serg 2026-11-04: «если он прыгнул в монстра монстр сдвигается на 1 клетку» - the hero stuck inside Griswold, a goatman, a zombie after a
//                 leap): a body within kOnTopM of the spot is moved its cell whatever it weighs (the game's knockback refuses what it never moves: it stays),
//                 the way from the spot - or, dead centre, the leap's own way (from its take-off to the landing); none known: a way of its own from its id.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace leapscatter {

constexpr float kLeapLiftUnits = 6.0f;   // world units: the model this high over the camera's ground is a leap
constexpr float kLandedUnits = 0.5f;     // ... and back under this, it has landed
constexpr float kRadiusM = 3.0f;         // the bodies thrown round the spot (render metres across the ground)
constexpr float kGameRadiusM = 6.0f;     // ... and one the game itself knocked back at the landing, this far at most
constexpr uint64_t kWindowMs = 400;      // the game's own knockback seen this long after the landing counts as the leap's
constexpr float kUnitsPerCell = 2.0f;    // the game's world units in a cell (grabmove::kUnitsPerCell)
constexpr float kFallS = 0.7f;           // s a body thrown off its feet takes to come down: its speed across = the cell's length over it
constexpr float kUpNearMs = 1.6f, kUpEdgeMs = 0.4f;   // m/s up: at the spot, at kRadiusM (the arc only)
constexpr float kBigKg = 110.0f;         // heavier: a stagger, not a fall
constexpr float kOnSpotM = 0.05f;        // dead centre: no way from the spot - the leap's way
constexpr float kOnTopM = 0.6f;          // the hero landed on it: moved its cell whatever it weighs

// The leap watched pose by pose: lift (world units, + up) at now, pos the hero's place (world units; null: not known). True once, on the pose it lands.
class Landing {
public:
    bool Feed(float lift, uint64_t nowMs, const float* pos = nullptr) {
        if (!std::isfinite(lift)) return false;
        const bool posOk = pos && std::isfinite(pos[0] + pos[1] + pos[2]);
        if (lift < kLandedUnits && !up_ && posOk) { ground_[0] = pos[0]; ground_[2] = pos[2]; groundOk_ = true; }   // (on the ground: where it takes off from)
        if (lift > kLeapLiftUnits) {
            if (!up_) { sinceMs_ = nowMs; from_[0] = ground_[0]; from_[2] = ground_[2]; fromOk_ = groundOk_; }
            up_ = true;
            peak_ = std::max(peak_, lift);
            return false;
        }
        if (!up_ || lift >= kLandedUnits) return false;
        up_ = false;
        lastPeak_ = peak_;
        lastMs_ = nowMs >= sinceMs_ ? nowMs - sinceMs_ : 0;
        peak_ = 0.0f;
        way_[0] = way_[1] = 0.0f;
        if (fromOk_ && posOk) {
            way_[0] = pos[0] - from_[0];
            way_[1] = pos[2] - from_[2];
        }
        groundOk_ = false;
        return true;
    }
    float LastPeak() const { return lastPeak_; }   // the leap's top (world units)
    uint64_t LastMs() const { return lastMs_; }    // from leaving the ground's band to landing
    bool Up() const { return up_; }
    // The last leap's way across the ground, take-off to landing (world x, z; 0, 0: not known).
    const float* Way() const { return way_; }

private:
    bool up_ = false, groundOk_ = false, fromOk_ = false;
    float peak_ = 0.0f, lastPeak_ = 0.0f;
    float ground_[3] = {}, from_[3] = {}, way_[2] = {};
    uint64_t sinceMs_ = 0, lastMs_ = 0;
};

struct Throw {
    bool ok = false;        // thrown
    bool fall = false;      // off its feet (else a stagger: a big one)
    bool move = false;      // its unit asked the game's knockback onto `cell` (a fall; or landed on, whatever it weighs)
    int cell[2] = {};       // the neighbour cell it goes to (x, y: -1, 0, 1 each; render x -> cell x, render z -> cell y)
    float impulse[3] = {};  // N s (render world, y up): across, the cell's way and length over kFallS; up, the arc
    float speedMs = 0.0f;   // the speed across given to the body
    float upMs = 0.0f;      // ... and up
    float distM = 0.0f;     // its pelvis from the spot across the ground
};

// The neighbour cell the way (dx, dz) points to - one of the eight, rounded by its angle (a 45-degree sector each).
inline void CellOf(float dx, float dz, int cell[2]) {
    const float a = std::atan2(dz, dx);
    const int k = static_cast<int>(std::lround(a / 0.785398163f)) & 7;   // 0 east, 1 north-east, ... in the render x / z plane
    static const int kWay[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
    cell[0] = kWay[k][0];
    cell[1] = kWay[k][1];
}

// land, pelvis: render metres (y up); upm: render units a metre (a cell is kUnitsPerCell / upm metres). gameKnocked: the game put this unit into its
// knockback within kWindowMs of the landing. leapWay: the leap's way across the ground (render x, z; null or 0, 0: not known).
inline Throw Scatter(const float land[3], const float pelvis[3], float massKg, uint32_t id, bool gameKnocked, float upm, const float* leapWay = nullptr) {
    Throw t;
    if (!land || !pelvis || !std::isfinite(land[0] + land[1] + land[2] + pelvis[0] + pelvis[1] + pelvis[2] + massKg + upm) || !(massKg > 0.5f) ||
        !(upm > 0.1f))
        return t;
    float dx = pelvis[0] - land[0], dz = pelvis[2] - land[2];
    const float d = std::sqrt(dx * dx + dz * dz);
    if (d > (gameKnocked ? kGameRadiusM : kRadiusM)) return t;
    if (d < kOnSpotM) {   // dead centre: the leap's own way, else a way of its own
        const float wl = leapWay && std::isfinite(leapWay[0] + leapWay[1]) ? std::sqrt(leapWay[0] * leapWay[0] + leapWay[1] * leapWay[1]) : 0.0f;
        if (wl > 1e-3f) {
            dx = leapWay[0];
            dz = leapWay[1];
        } else {
            const float a = static_cast<float>(id % 360u) * 2.39996323f;   // (the golden angle: neighbours' ids far apart)
            dx = std::cos(a);
            dz = std::sin(a);
        }
    }
    CellOf(dx, dz, t.cell);
    const float cellM = kUnitsPerCell / upm;
    const float wx = static_cast<float>(t.cell[0]), wz = static_cast<float>(t.cell[1]), wl = std::sqrt(wx * wx + wz * wz);   // (a diagonal: sqrt 2 cells)
    t.speedMs = wl * cellM / kFallS;
    const float s = std::clamp(d / kRadiusM, 0.0f, 1.0f);
    t.upMs = kUpNearMs + (kUpEdgeMs - kUpNearMs) * s;
    t.distM = d;
    t.impulse[0] = massKg * t.speedMs * wx / wl;
    t.impulse[1] = massKg * t.upMs;
    t.impulse[2] = massKg * t.speedMs * wz / wl;
    t.fall = massKg <= kBigKg;
    t.move = t.fall || d < kOnTopM;
    t.ok = true;
    return t;
}

}  // namespace leapscatter
