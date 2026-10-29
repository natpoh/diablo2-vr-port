// flat_ground.h - [input] flat_aim_ground: in flat mode with our camera, the game's mouse point is the cell of the ground DRAWN under the pointer,
// not the cell its own mouse raycast gives. Pure; the test is cleanroom/tests/flat_ground_test.cpp, the wiring is vr/vrcam.cpp (HookMouseGround).
//
// Players on flat (2026-10-24): "if there is a height difference in the game, the aiming goes away"; earlier "monsters cannot be targeted after a
// while" (docs/notes/flat_targeting.md). The cause, read in the game's code (docs/notes/flat_targeting.md, section 5):
//
//   - The game's world is a 2D grid; height is only drawn, and drawn SHEARED: what stands on ground y high is put at render (2*gx + y*k, y, 2*gz + y*k),
//     k = sqrt(3/2) - along the game's own view (its "floor under a cell" query, 0x6B0350, casts along (0.612, 0.5, 0.612) through (2*gx, 0, 2*gz)).
//     So in the game's camera a raised floor sits on screen exactly where its cell would on flat ground (as the legacy 2D picture has it).
//   - The mouse's point (0x7E7CE0) is the game's raycast of physics world 1 along the pointer's ray, then (hit.x / 2, hit.z / 2): no height taken
//     off. For that to be the cell in the game's own view, world 1 must answer at the base height (y 0) - the iso line through a raised floor's
//     drawn point comes down to y 0 exactly over its cell. Along any other view it does not.
//   - From our first-person camera the ray through a raised floor goes on, shallow, down to the base: the cell comes out off along the look by
//     y / tan(down), less or more the shear by the heading. A floor 1 m up (3.1 units): 2-4 cells off 3 cells out, 3-7 at 8, 10-14 at 20
//     (flat_ground_test -v). On base ground it is right. Walking onto a ledge, a stair, a raised street, the aim goes; on base ground it is back.
//
// What is done instead (only the point; the game's own hover, click and skills do the rest): the same pointer ray is cast by the game into its
// floor world (world 0 with the filter of its "floor under a cell" query - where it stands units), and the cell under that point is the point
// with its own height taken off the shear. Along the game's own view it is the game's own answer.
#pragma once

#include <cmath>

namespace flatground {

constexpr float kShear = 1.22474487139f;    // render x, z = 2 * cell + y * kShear (= bowpitch::kShear, meleetouch::kGroundShear)
constexpr float kRenderPerCell = 2.0f;      // render units per grid cell (the game's (2, 0, 2) and hit * 0.5)

inline bool Finite3(const float p[3]) { return std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]); }

// The cell of a point drawn ON the ground (a floor's point): its height's shear taken off.
inline void Cell(const float p[3], float* gx, float* gz) {
    *gx = (p[0] - p[1] * kShear) / kRenderPerCell;
    *gz = (p[2] - p[1] * kShear) / kRenderPerCell;
}

// The game's own cell for its mouse hit (0x7E7CE0: hit.x * 0.5, hit.z * 0.5).
inline void GameCell(const float hit[3], float* gx, float* gz) {
    *gx = hit[0] / kRenderPerCell;
    *gz = hit[2] / kRenderPerCell;
}

// A cell on ground y high, as drawn (the game's 0x6B0350 for a cell whose floor is y).
inline void Drawn(float gx, float gz, float y, float out[3]) {
    out[0] = gx * kRenderPerCell + y * kShear;
    out[1] = y;
    out[2] = gz * kRenderPerCell + y * kShear;
}

// The ray o + d*t against the plane y = planeY: the point, false for a ray that never comes down to it.
inline bool PlaneHit(const float o[3], const float d[3], float planeY, float out[3]) {
    if (!(d[1] < -1e-6f) || !Finite3(o)) return false;
    const float t = (planeY - o[1]) / d[1];
    if (!(t > 0.0f)) return false;
    for (int i = 0; i < 3; ++i) out[i] = o[i] + d[i] * t;
    return Finite3(out);
}

// What goes to the game as its mouse point. ours: the floor's cell; else the game's own, unchanged.
struct Pick {
    bool ok = false;        // a point at all (the game's function returns this)
    bool ours = false;      // the floor's cell, not the game's
    float gx = 0.0f, gz = 0.0f;
    bool gameOk = false;
    float gameGx = 0.0f, gameGz = 0.0f;
    float offCells = 0.0f;  // how far the game's cell was from ours (both known)
};
// gameOk / gameHit: the game's own raycast (world 1); floorOk / floorHit: the pointer's ray into the floor world (world 0).
inline Pick Choose(bool gameOk, const float gameHit[3], bool floorOk, const float floorHit[3]) {
    Pick p;
    p.gameOk = gameOk && Finite3(gameHit);
    if (p.gameOk) GameCell(gameHit, &p.gameGx, &p.gameGz);
    if (floorOk && Finite3(floorHit)) {
        Cell(floorHit, &p.gx, &p.gz);
        p.ok = p.ours = std::isfinite(p.gx) && std::isfinite(p.gz);
        if (p.ours && p.gameOk) p.offCells = std::hypot(p.gameGx - p.gx, p.gameGz - p.gz);
    }
    if (!p.ours && p.gameOk) {
        p.ok = true;
        p.gx = p.gameGx;
        p.gz = p.gameGz;
    }
    return p;
}

}  // namespace flatground
