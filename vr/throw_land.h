// throw_land.h - [hands] potion_throw_mode: WHEN the game's own potion throw is pressed. Pure (no game, no clock of its own); unit test
// cleanroom/tests/throw_land_test.cpp.
//
// A thrown potion is two things in this mod: the potion in the hand (the weapon-weight model's model, vr/weight.h: it leaves the hand at the release and
// flies the worked-out trajectory) and the game's own throw (the Throw skill: a missile that leaves the hero's body at the action frame of his throw
// animation, flies straight to the cast point at 1 cell a frame - 25 cells a second - and explodes there, the game's own explosion, damage and gas).
//   mode 0 (kNow, the default): the game's throw is pressed at the release; the model is taken by the game a few frames later and vanishes, the game's
//          missile is what flies and explodes.
//   mode 1 (kAtLanding): the game's throw is pressed when the model LANDS (the weight model's flight time after the release), aimed at its landing:
//          the model flies the trajectory that was thrown, and the game's own throw - and so its explosion - follows it to the spot.
// Mode 1 needs the weight model's account of the throw (its flight time and landing: vr/throw_source.h); without it the throw is mode 0.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace throwland {

enum Mode : int { kNow = 0, kAtLanding = 1 };

inline constexpr double kMinFlightS = 0.15;   // a flight shorter than this is not waited for: the press goes at the release
inline constexpr double kMaxFlightS = 4.0;    // a flight longer than this (a slow lob) is pressed at this
inline constexpr double kMaxWaitS = 5.0;      // a pending press older than this is dropped (a stale one must never fire)

struct Plan {
    bool deferred = false;      // the press waits
    double delayS = 0.0;        // ... this long after the release
    const char* why = "";       // the log's word for what decided
};

// What to do with a throw released now: mode (0 / 1, anything else is 0), a potion, the weight model has the throw (its flight time in seconds).
inline Plan Decide(int mode, bool potion, bool haveWeight, double flightS) {
    Plan p;
    if (mode != kAtLanding) { p.why = "potion_throw_mode 0: pressed at the release"; return p; }
    if (!potion) { p.why = "not a potion: pressed at the release"; return p; }
    if (!haveWeight || !std::isfinite(flightS)) { p.why = "the weight model has no account of this throw (flight time unknown): pressed at the release"; return p; }
    if (flightS < kMinFlightS) { p.why = "the model lands within 0.15 s: pressed at the release"; return p; }
    p.deferred = true;
    p.delayS = std::min(flightS, kMaxFlightS);
    p.why = "pressed when the model lands";
    return p;
}

// -- what the game's own throw costs, from the game's code (3.3.93787, the server's potion throw 0x432F90 and missile creation 0x5371A0) -------------
// The potion's missile (missiles.txt: Vel 16, Range 30, AlwaysExplode) is made with the "distance" flag: its life in frames is the OCTAGONAL distance
// from the hero to the cast point - (2 * longer + shorter) / 2 of the two axes, in the game's cells - at a speed of Vel / 16 = 1 cell a frame, 25 frames
// a second (the game's tick). It explodes where its life ends.
inline constexpr double kGameTickHz = 25.0;
inline constexpr double kPotionCellsPerFrame = 1.0;   // Vel 16

inline int OctFrames(double dxCells, double dyCells) {
    const long ax = (long)std::fabs(dxCells), ay = (long)std::fabs(dyCells);
    const long hi = std::max(ax, ay), lo = std::min(ax, ay);
    return (int)((2 * hi + lo) / 2);
}
// How long the game's potion flies to a point (dx, dy) cells away, after it has left the hero.
inline double GameFlightS(double dxCells, double dyCells) { return OctFrames(dxCells, dyCells) / (kGameTickHz * kPotionCellsPerFrame); }
// The distance along a straight line of `cells` cells (the cast point's distance as the ray goes: an angle changes the octagon only a little):
// the octagonal distance is between 1.0 and 1.12 of it.
inline double GameFlightStraightS(double cells) { return cells / (kGameTickHz * kPotionCellsPerFrame); }

// -- the pending press ---------------------------------------------------------------------------------------------------------------------------
class Pending {
public:
    enum class Act { None, Fire, Dropped };
    bool Active() const { return active_; }
    unsigned Seq() const { return seq_; }
    double FireAt() const { return at_; }
    void Arm(double releaseTime, double delayS, unsigned seq) {
        active_ = true;
        at_ = releaseTime + delayS;
        armed_ = releaseTime;
        seq_ = seq;
    }
    void Cancel() { active_ = false; }
    // Every frame: `ok` false cancels it (the play stopped, the weapon changed, the view left); Fire once the time has come.
    Act Step(double now, bool ok) {
        if (!active_) return Act::None;
        if (!ok || now - armed_ > kMaxWaitS) { active_ = false; return Act::Dropped; }
        if (now < at_) return Act::None;
        active_ = false;
        return Act::Fire;
    }

private:
    bool active_ = false;
    double at_ = 0.0, armed_ = 0.0;
    unsigned seq_ = 0;
};

}  // namespace throwland
