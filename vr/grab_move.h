// grab_move.h - [weight] grab_moves_unit: a body the hands carry or throw ([weight] grab_throw - the knock-down library's) is moved by the GAME: its own
// knockback puts the unit on the cell under the drawn body, so it fights from there, comes back to there, and every player in the game sees it.
// Pure (cleanroom/tests/grab_move_test.cpp); the game's side is vr/grab_move.cpp, the drawn body vr/knock_down.cpp.
//
// Serg (2026-11-01, «Yes, via knockback»): today the grab and the throw are only drawn - the game keeps the unit on its cell, it fights from there and the
// drawn body walks back to it. Moving it must go through the game's own mechanism, never a written position or a made-up packet: the server's knockback,
// the one its damage code starts for a Knockback hit (D2R 3.3.93787 0x44AB31: the monster's mode event for mode 13 - MonModeEvent 0x4471E0 - with a point
// at +0x18 / +0x1C, then MonModeSet 0x4475C0). That knockback's path goes to the point it is given (path type 8, 0x3AA870: with no unit to fly from, the
// target point itself), clipped by the game's own collision (0x3C5610) and refused past 100 cells; the mode's start (0x446310) refuses a monster its
// MonStats row does not let be knocked back - the game's own answer stands. The server moves the unit and tells every client, as for any knockback.
// Only where the server runs in this process - single player, or the host of a game; a joined player's game stays drawn only.
// [weight] knock_down_moves_unit (Serg 2026-11-04, zbiromir's report: a monster knocked down by a blow stayed on its old cell - it fought from there, the hits on
// the body lying elsewhere did nothing, and up again it walked back): a body a weapon's blow knocked down is moved the same way, its own tick.
//
// Here: from the drawn body and the game's to the cells apart, when a knockback is asked, and the cell it is sent to.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace grabmove {

constexpr float kShear = 1.22474487139f;   // render = grid + height * kShear on x and on z (meleetouch::kGroundShear, bowpitch::kShear)
constexpr float kUnitsPerCell = 2.0f;      // the game's world units in one cell (pointcast::kUnitsPerCell): cell x = world x / 2, cell y = world z / 2
constexpr float kStepCells = 2.0f;         // the drawn body this many cells off the unit's (either axis) asks for a knockback
constexpr float kSettleCells = 1.0f;       // ... and once it lies, gets up or walks: this many (a unit left short of it - a step of 1.9 cells never asked, the game's
                                           // collision - was walked back to: Serg 2026-11-04, «иногда пытается вернуться на старое место»)
constexpr float kMaxStepCells = 24.0f;     // one knockback is never sent farther (the game's own path refuses past 100 cells; a long throw takes several)
constexpr uint64_t kGapMs = 250;           // not asked again for the same unit sooner (its knockback takes a moment to run)
constexpr int kKnockbackMode = 13;         // the monster's mode KB

// The knock-down library's phases (shared/d2r_vr_fall.h D2RF_*) in which the drawn body is away from the unit by the hands: held, flying and falling after the
// throw, lying, getting up. Walking home, staggering, back: not moved (the walk home is the drawn body going back to wherever the unit now is).
inline bool PhaseMoves(int phase) { return phase == 1 || phase == 2 || phase == 3 || phase == 5; }
// The cells off that ask a knockback in this phase: held or flying kStepCells (it moves on; every step would be a knockback); at rest - lying, getting up,
// walking home (7) - kSettleCells, so the unit ends on the body's own cell and the walk home is no walk back.
inline float StepFor(int phase) { return phase == 2 || phase == 3 || phase == 7 ? kSettleCells : kStepCells; }
// [weight] knock_down_moves_unit: a body the library has after a weapon's blow is moved too - knocked down (any kind but the stagger: a step back and its
// balance caught is no move) or carried on a spear (impale_drag); a hand's take always ([weight] grab_moves_unit).
inline bool KindMoves(bool grabbed, int kind) { return grabbed || kind != 3; }   // (3: D2RF_STAGGER)
// ... and a blow's at most this many knockbacks (where it lands, where it settles): a hand holds a body on and on, a blow does not - asked again and again
// while the game walks the unit off, the knockback would pin a monster the game has moving (it gets up and goes after it instead, as before).
constexpr int kKnockMoves = 3;

// The cells (x, y) the drawn point stands off the game's point: both render metres (the same joint - the pelvis), `upm` render units a metre. Each point's
// cell is the ground under it, its height's push taken off (a body lifted in the hands or lying on the ground is on the cell below it).
inline bool CellsApart(const float drawn[3], const float game[3], float upm, float* dx, float* dy) {
    if (!drawn || !game || !dx || !dy || !(upm > 0.0f) || !std::isfinite(upm)) return false;
    const float gx = (drawn[0] - drawn[1] * kShear) - (game[0] - game[1] * kShear);
    const float gz = (drawn[2] - drawn[1] * kShear) - (game[2] - game[1] * kShear);
    const float x = gx * upm / kUnitsPerCell, y = gz * upm / kUnitsPerCell;
    if (!std::isfinite(x) || !std::isfinite(y)) return false;
    *dx = x;
    *dy = y;
    return true;
}

// Ask for a knockback now: the body `minCells` off on either axis (StepFor its phase), the last asked for this unit kGapMs ago (lastAt 0: never).
inline bool Wants(float dx, float dy, uint64_t nowMs, uint64_t lastAtMs, float minCells = kStepCells) {
    if (!std::isfinite(dx) || !std::isfinite(dy)) return false;
    if (std::max(std::fabs(dx), std::fabs(dy)) < std::max(kSettleCells, minCells)) return false;
    return lastAtMs == 0 || nowMs < lastAtMs || nowMs - lastAtMs >= kGapMs;
}

// The cell the knockback is sent to: the unit's cell (ux, uy: its server path) and the cells apart - held to kMaxStepCells along the way (the direction kept)
// and to the map's words. False when it would not move it a cell.
inline bool Target(int ux, int uy, float dx, float dy, int* tx, int* ty) {
    if (!tx || !ty || !std::isfinite(dx) || !std::isfinite(dy)) return false;
    const float most = std::max(std::fabs(dx), std::fabs(dy));
    if (most > kMaxStepCells) {
        dx *= kMaxStepCells / most;
        dy *= kMaxStepCells / most;
    }
    const int x = std::clamp(ux + static_cast<int>(std::lround(dx)), 1, 0xFFFE), y = std::clamp(uy + static_cast<int>(std::lround(dy)), 1, 0xFFFE);
    if (x == ux && y == uy) return false;
    *tx = x;
    *ty = y;
    return true;
}

// The hero is in the way. The game's knockback stops at another unit's body (its own collision): a body held in front of the hero and pulled in is drawn
// over the hero's own cells, and a knockback asked there - or across him - ran and moved the unit nothing (0.195.1, Serg's log: monster 28 on 5008,6192 asked
// to 5005,6193 with the hero on 5007,6194 - "moved +0,+0"; every one asked sideways or away from him landed on its cell). So the cell asked is kept
// kHeroClearCells from the hero (out along the hero -> cell line), and a step that would pass within kHeroPassCells of him goes round him instead: to the
// cell kHeroClearCells off him, turned from the unit's side at most 90 degrees toward the asked side (the next steps go on round).
constexpr float kHeroClearCells = 2.5f;   // the hero's body and the unit's (a fallen is one cell across, a zombie two)
constexpr float kHeroPassCells = 1.5f;
// hx, hy: the hero's cell (his server path), ux, uy the unit's, *tx, *ty the cell asked (in: Target's; out: clear of the hero). False: nothing left to move.
inline bool ClearOfHero(float hx, float hy, int ux, int uy, int* tx, int* ty) {
    if (!tx || !ty || !std::isfinite(hx) || !std::isfinite(hy)) return tx && ty && (*tx != ux || *ty != uy);
    const float px = static_cast<float>(*tx) - hx, py = static_cast<float>(*ty) - hy;   // the cell asked, from the hero
    const float vx = static_cast<float>(ux) - hx, vy = static_cast<float>(uy) - hy;     // the unit, from the hero
    const float sx = px - vx, sy = py - vy, l2 = sx * sx + sy * sy;
    const float t = l2 > 1e-6f ? std::clamp(-(vx * sx + vy * sy) / l2, 0.0f, 1.0f) : 0.0f;
    const float pass = std::hypot(vx + sx * t, vy + sy * t), d = std::hypot(px, py), du = std::hypot(vx, vy);
    const bool across = pass < kHeroPassCells && t > 0.0f && pass < du - 0.25f;   // (nearer him on the way than where it starts: a unit held by him may step off)
    if (d >= kHeroClearCells && !across) return *tx != ux || *ty != uy;   // clear: as asked
    float a = d > 1e-3f ? std::atan2(py, px) : std::atan2(vy, vx);   // the side asked (on the hero himself: the unit's side)
    if (across && du > 1e-3f) {   // across him: round him from the unit's side, a quarter turn at most
        const float au = std::atan2(vy, vx);
        float turn = a - au;
        while (turn > 3.14159265f) turn -= 6.28318531f;
        while (turn < -3.14159265f) turn += 6.28318531f;
        a = au + std::clamp(turn, -1.57079633f, 1.57079633f);
    }
    const int x = std::clamp(static_cast<int>(std::lround(hx + kHeroClearCells * std::cos(a))), 1, 0xFFFE);
    const int y = std::clamp(static_cast<int>(std::lround(hy + kHeroClearCells * std::sin(a))), 1, 0xFFFE);
    if (x == ux && y == uy) return false;
    *tx = x;
    *ty = y;
    return true;
}

// What the game did with an asked knockback, from the unit's mode right after the call: 13 - it runs; anything else - the game refused it (a monster that
// cannot be knocked back, one dead or dying by then).
enum class Outcome { Runs, Refused, Dead };
inline Outcome Judge(int modeAfter) {
    if (modeAfter == kKnockbackMode) return Outcome::Runs;
    if (modeAfter == 0 || modeAfter == 12) return Outcome::Dead;
    return Outcome::Refused;
}
// May a knockback be asked of a unit in this mode: not dead or dying (0, 12), not in one already (13: the last one still runs - asked again after it).
inline bool ModeAllows(int mode) { return mode != 0 && mode != 12 && mode != kKnockbackMode; }

}  // namespace grabmove
