// shot_release.h - the shot at release (docs/notes/shot_at_release.md): the server's create of the hero's arrow (CreateMissile 0x5371A0, vr/throw_born.cpp's
// hook) is where the arrow leaves the bow - the attack's action frame, half a second and more after the click that started the attack. The aim the click gave
// is turned there to the crosshair of THAT moment: the player may have turned the camera during the animation (Serg, 2026-11-06: "the target must be taken at
// the release"). Flat mouse look only, for now ([input] flat_shot_at_crosshair; vr/input/flat_click.cpp asks this).
//
// The turn: every arrow of the shot is turned by the same angle - the release's heading less the press's - so a Multiple Shot's fan turns as a whole; its
// target goes kFarCells out along its new heading (the command and the create carry whole cells: far out a cell is about a degree). With the press's heading
// not known, the arrow's own heading stands for it (the shot's own arrow goes to the release's heading). Strafe picks its own targets and Guided Arrow homes:
// left as the game made them; so is an arrow aimed at a unit (the game's own attack on a unit, never ours here) and one with no target of its own.
// Only the target cell changes: the start, the speed, the range, the collision and the damage stay the game's.
//
// Pure: no game, no Windows; the test cleanroom/tests/shot_release_test.cpp.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "arrow_flight.h"

namespace shotrelease {

constexpr float kFarCells = 40.0f;   // as flatarrow::kFarCells and aimpoint::kMaxCells
constexpr float kPi = 3.14159265358979f;

// The arrows the release turns: arrow_flight's classes less Strafe's (they pick their own targets) and Guided Arrow (it homes).
inline bool Turns(int32_t cls) {
    if (cls < 0 || !arrowflight::IsArrowClass((uint32_t)cls) || arrowflight::Homes((uint32_t)cls)) return false;
    return cls != (int32_t)arrowflight::kStrafeArrow && cls != (int32_t)arrowflight::kStrafeBolt;
}

inline float Wrap(float a) {
    if (!std::isfinite(a)) return a;
    a = std::fmod(a + kPi, 2.0f * kPi);
    if (a < 0.0f) a += 2.0f * kPi;
    return a - kPi;
}

struct In {
    int32_t cls = -1;
    int sx = 0, sy = 0;          // the start cell the create takes
    int tx = 0, ty = 0;          // the target cell it takes (absolute)
    bool hasTarget = false;      // the block gives a target of its own (absolute 0x20 or relative 0x2)
    bool targetUnit = false;     // ... aimed at a unit
    bool pressKnown = false;     // the click that started the attack noted its heading
    float pressTheta = 0.0f;     // ... atan2(y, x) on the grid
    float releaseTheta = 0.0f;   // the crosshair's heading now (the same frame)
    float farCells = kFarCells;
};

struct Out {
    bool write = false;
    int tx = 0, ty = 0;              // the new target cell (absolute)
    float fromTheta = 0.0f, toTheta = 0.0f;
    const char* why = "";
};

inline Out Decide(const In& in) {
    Out o;
    if (!Turns(in.cls)) { o.why = "not an arrow the release turns (Strafe, Guided Arrow, not an arrow)"; return o; }
    if (in.targetUnit) { o.why = "aimed at a unit"; return o; }
    if (!in.hasTarget) { o.why = "no target cell of its own"; return o; }
    if (!std::isfinite(in.releaseTheta) || (in.pressKnown && !std::isfinite(in.pressTheta))) { o.why = "no heading"; return o; }
    const int dx = in.tx - in.sx, dy = in.ty - in.sy;
    if (dx == 0 && dy == 0) { o.why = "its target is its start"; return o; }
    o.fromTheta = std::atan2((float)dy, (float)dx);
    const float turn = in.pressKnown ? Wrap(in.releaseTheta - in.pressTheta) : Wrap(in.releaseTheta - o.fromTheta);
    o.toTheta = Wrap(o.fromTheta + turn);
    const float cells = in.farCells > 1.0f && std::isfinite(in.farCells) ? in.farCells : kFarCells;
    const float fx = (float)in.sx + std::cos(o.toTheta) * cells, fy = (float)in.sy + std::sin(o.toTheta) * cells;
    constexpr float lo = 1.0f, hi = 65534.0f;   // the cells are unsigned words (aimpoint::Point's rule)
    if (!(fx >= lo && fx <= hi && fy >= lo && fy <= hi)) { o.why = "the point would leave the map's coordinates"; return o; }
    o.tx = (int)std::lround(fx);
    o.ty = (int)std::lround(fy);
    if (o.tx == in.sx && o.ty == in.sy) { o.why = "no room"; return o; }
    o.write = true;
    o.why = in.pressKnown ? "turned by the crosshair's turn since the press" : "turned to the crosshair's heading";
    return o;
}

}  // namespace shotrelease
