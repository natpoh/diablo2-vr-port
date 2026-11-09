// flat_arrow.h - [bow] arrow_flight_flat: in the flat 3D views (our camera, mouse look) the shot arrow is drawn into the crosshair (docs/notes/shot_at_release.md,
// item 5). The shot click (vr/input/flat_click.cpp) notes the shot's line for vr/arrow_flight_wire.h: from the release point (first person: just below the eye;
// third person: the hero's chest) to what the crosshair's ray met - a body (flat_body_pick: a monster, an NPC, an object), else the game's floor (known ground:
// the arrow sticks there) - the heading the game is given (the hero's cell to the cell met) and the distance between the two cells. The game's missile, its
// flight, its hit are the game's; arrow_flight puts only where its model is drawn. The aim is read at the click for now (the shot at release comes later).
// Pure: no game, no Windows; the test cleanroom/tests/flat_arrow_test.cpp.
#pragma once

#include <cmath>

namespace flatarrow {

// -- the shot's line ---------------------------------------------------------------------------------------------------------------------------------------

constexpr float kShear = 1.22474487139f;   // render x, z = 2 * cell + y * kShear (flatground::kShear)
constexpr float kPerCell = 2.0f;           // world units a cell
constexpr float kBelowEyeM = 0.25f;        // first person: the arrow leaves this far below the eye
constexpr float kChestM = 1.3f;            // third person: ... this high over the hero's feet

struct LineIn {
    float o[3] = {}, d[3] = {};   // the crosshair's ray (world units; d a unit vector)
    bool body = false;            // it met a body first (flat_body_pick): at `bodyAt`, the cell (bodyCellX, bodyCellZ)
    bool bodyMonster = false;     // ... a monster's or an NPC's (not an object's)
    float bodyAt[3] = {}, bodyCellX = 0.0f, bodyCellZ = 0.0f;
    bool floor = false;           // it met the game's floor (world 0) at `floorAt`
    float floorAt[3] = {};
    float feet[3] = {};           // the hero's feet as drawn (the look-at)
    bool third = false;           // third person (the release from the chest, not below the eye)
    float upm = 1.0f;             // world units a metre
};

struct Line {
    bool ok = false;
    float start[3] = {};          // the release point (world units)
    float dir[3] = {};            // the line, a unit vector
    float theta = 0.0f;           // the heading the game is given, atan2(z, x) on the grid
    bool target = false;          // the ray met something
    bool monster = false;         // ... a body
    bool stick = false;           // ... the game's floor (the ground is known there: the arrow stays in it)
    float at[3] = {};             // the point met
    float gridDist = 0.0f;        // the cell met from the hero's cell (world units)
};

inline bool Finite3(const float v[3]) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }

// The cell under a point drawn on raised ground (its height's shear taken off), in world units (2 a cell).
inline void GridOf(const float p[3], float* x, float* z) {
    *x = p[0] - p[1] * kShear;
    *z = p[2] - p[1] * kShear;
}

inline Line MakeLine(const LineIn& in) {
    Line l;
    const float upm = in.upm > 1e-3f && std::isfinite(in.upm) ? in.upm : 1.0f;
    if (!Finite3(in.o) || !Finite3(in.d) || !Finite3(in.feet)) return l;
    if (in.third) {
        l.start[0] = in.feet[0]; l.start[1] = in.feet[1] + kChestM * upm; l.start[2] = in.feet[2];
    } else {
        l.start[0] = in.o[0]; l.start[1] = in.o[1] - kBelowEyeM * upm; l.start[2] = in.o[2];
    }
    float hx, hz;
    GridOf(in.feet, &hx, &hz);
    float tx = 0.0f, tz = 0.0f;
    if (in.body && Finite3(in.bodyAt)) {
        l.target = true;
        l.monster = in.bodyMonster;
        for (int i = 0; i < 3; ++i) l.at[i] = in.bodyAt[i];
        // the heading goes to where the crosshair met the body, not to the middle of its cell (that was an aim at the monster): flat_body_pick's
        // capsules stand on the grid (twice the cell), so the point met is already on it
        tx = in.bodyAt[0];
        tz = in.bodyAt[2];
    } else if (in.floor && Finite3(in.floorAt)) {
        l.target = true;
        l.stick = true;
        for (int i = 0; i < 3; ++i) l.at[i] = in.floorAt[i];
        GridOf(in.floorAt, &tx, &tz);
    }
    float w[3];
    if (l.target) {
        for (int i = 0; i < 3; ++i) w[i] = l.at[i] - l.start[i];
        l.gridDist = std::sqrt((tx - hx) * (tx - hx) + (tz - hz) * (tz - hz));
    } else {
        for (int i = 0; i < 3; ++i) w[i] = in.d[i];
    }
    const float len = std::sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
    if (!(len > 1e-4f) || !std::isfinite(len)) return l;
    for (int i = 0; i < 3; ++i) l.dir[i] = w[i] / len;
    // the heading: the hero's cell to the cell met (what the game's point is); the ray's own on the ground when nothing was met or the cells are one
    if (l.target && l.gridDist > 0.5f) l.theta = std::atan2(tz - hz, tx - hx);
    else if (in.d[0] * in.d[0] + in.d[2] * in.d[2] > 1e-6f) l.theta = std::atan2(in.d[2], in.d[0]);
    else return l;
    l.ok = std::isfinite(l.theta);
    return l;
}

// The point the game's shot is given: kFarCells out from the hero's cell along the line's heading (aimpoint::Point, the caller's). The command carries whole
// cells, so a point near the hero is a staircase of headings (3 cells out: up to 13 degrees off - arrow_flight then took the game's arrow for a side one and
// bent ours onto its path, and the shot went beside the crosshair); this far out a cell is about a degree.
constexpr float kFarCells = 40.0f;
// The hero's cell (the look-at less its height's push), in cells.
inline bool HeroCell(const float feet[3], float* cx, float* cz) {
    if (!Finite3(feet)) return false;
    GridOf(feet, cx, cz);
    *cx /= kPerCell;
    *cz /= kPerCell;
    return true;
}

}  // namespace flatarrow
