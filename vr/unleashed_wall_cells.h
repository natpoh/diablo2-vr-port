// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed commit b0f8eb1dfab71ec1cbc7e9535d24d72f11be5d6b ("Weapons hit walls: held weapons and shields stop at walls, closed
// doors and the floor"): its steamvr/dropped_bottles.h (the cell patch, CellAt, the barrier bits), steamvr/world_coordinates.h (the ground's
// shear) and steamvr/held_world.h (AddWalls, AddFloor: each wall cell a box standing on the floor that pushes out only through its sides whose
// neighbour is open, the floor a box that pushes only up), taken into D2R VR Authentic for [weight] wall_contact_mode 2 (Bear's). Changed: the namespace and the
// types (plain floats, render metres); the boxes go to our own weight model's walls (cleanroom/heft/walls.h, its Wall::open), not to Unleashed's
// WorldSolver; the window is the cells within reach of the eyes' column; the patch is marked by Mark (pure, for the test).
// The rest of D2R VR Authentic is MIT; this file stays MPL 2.0.
//
// The world the held weapon meets, as the game itself has it:
// - Walls: the game's own collision cells around the hero (vr/unleashed_wall_cells.cpp reads them on the game thread: the missile-barrier bits
//   0x804 that arrows stop at), each 2 world units (about 46 cm) square, standing straight up from the floor - they have no height of their own.
//   Left out: an object's footprint (0x400: a barrel, an urn, a chest - no 6 m pillar over a barrel) and a cell with no room loaded (a doorway
//   into the next area). A closed door (0x800) stays: the sword stops at it.
// - The floor: the ground the hero stands on (his feet), flat around him. Stairs and slopes within reach are not followed.
// Every wall runs along the cell grid, so a wall's face is one plane: a cell pushes out only through a side whose neighbour is not a wall - the
// cells of one wall are one face (no catching at the seams), an inner corner pushes out of both walls, nothing is pushed out through the far side.
//
// Coordinates: the renderer draws raised ground sheared along both horizontal axes, render x, z = 2 * cell + height * sqrt(3/2) (world units,
// y up); the cells lie on the unsheared grid. Render metres = render units / units per metre. Pure: no game, no Windows, no clock.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace unleashedwalls {

constexpr float kShear = 1.22474487139f;            // the renderer's shear of raised ground: cot(30 deg) / sqrt(2) = sqrt(3/2)
constexpr uint16_t kBarrierBits = 0x804;            // a wall (0x4) or a closed door (0x800): what arrows and dropped bottles stop at
constexpr uint16_t kObjectBit = 0x400, kDoorBit = 0x800;
constexpr int kPatchCells = 32;                     // the square of cells sampled around the hero
constexpr float kWallHeightM = 6.0f;                // m: a wall cell stands this high above the floor
constexpr float kWallBelowM = 0.5f;                 // m: ... and reaches this far under it
constexpr float kFloorHalfM = 4.0f;                 // m: the floor's box around the eyes' column, each way
constexpr float kFloorThickM = 1.0f;                // m: the floor's box under its top
constexpr int kWindowCells = 16;                    // the most cells across looked at around the eyes

// A square of the game's collision cells sampled around a point. Bit x of rows[z] is cell (x0 + x, z0 + z): a wall, a closed door, an object's
// footprint or no room loaded (the barrier as arrows see it); of those, objects / doors / unknown say which.
struct Patch {
    uint64_t session = 0, serial = 0;
    uint64_t at = 0;   // GetTickCount64 when sampled
    int x0 = 0, z0 = 0;
    std::array<uint32_t, kPatchCells> rows{}, objects{}, doors{}, unknown{};
    bool Covers(int x, int z) const { return x >= x0 && z >= z0 && x < x0 + kPatchCells && z < z0 + kPatchCells; }
    bool Blocked(int x, int z) const { return Covers(x, z) && ((rows[z - z0] >> (x - x0)) & 1u); }
    bool Bit(const std::array<uint32_t, kPatchCells>& of, int x, int z) const { return Covers(x, z) && ((of[z - z0] >> (x - x0)) & 1u); }
    // What the held weapon stops at: a wall or a closed door - not an object's footprint, not an unloaded room.
    bool Wall(int x, int z) const { return Blocked(x, z) && !Bit(objects, x, z) && !Bit(unknown, x, z); }
    // One sampled cell (x, z: the patch's own 0..kPatchCells-1): off the map, no room loaded there, or the bits the game gave it.
    void Mark(int x, int z, bool offMap, bool roomKnown, uint16_t bits) {
        if (x < 0 || z < 0 || x >= kPatchCells || z >= kPatchCells) return;
        const bool unknownCell = offMap || !roomKnown;
        const bool wall = unknownCell || (bits & kBarrierBits) != 0;
        if (!wall) return;
        const uint32_t b = 1u << x;
        rows[z] |= b;
        if (unknownCell) unknown[z] |= b;
        else if (bits & kDoorBit) doors[z] |= b;
        else if (bits & kObjectBit) objects[z] |= b;
    }
};

struct Cell {
    int x = 0, z = 0;
};
// The cell under a render point (world units) on ground at floorUnits: only the floor's shear is removed - walls stand vertically in render space.
inline Cell CellAt(float xUnits, float zUnits, float floorUnits) {
    return {(int)std::floor((xUnits - floorUnits * kShear) * 0.5f), (int)std::floor((zUnits - floorUnits * kShear) * 0.5f)};
}

enum class Kind : uint8_t { Floor, Wall, Door };
inline const char* KindName(Kind k) { return k == Kind::Floor ? "floor" : k == Kind::Door ? "door" : "wall"; }
// A face of an axis box: k 0 x, 1 y, 2 z; side +1 or -1 - heft::walls' numbering (face = 2 k, +1 for the minus side).
inline uint8_t FaceBit(int k, int side) { return (uint8_t)(1u << (k * 2 + (side < 0 ? 1 : 0))); }

// One thing to stay out of: an axis box in render metres and the faces it pushes out through.
struct Box {
    float lo[3] = {}, hi[3] = {};
    uint8_t open = 0;
    Kind kind = Kind::Wall;
    int x = 0, z = 0;   // its cell (the floor: INT_MIN-like, unused)
};
// The window's cells, for the log: walls kept (of them closed doors, and those wholly inside a wall), and those left out.
struct Counts {
    unsigned walls = 0, doors = 0, inside = 0, objects = 0, unknown = 0, uncovered = 0, dropped = 0;
};

// The patch's walls within reachM (metres, square) of the eyes' column (cxM, czM: render metres), standing on floorUnits (the hero's ground,
// render units), as boxes in render metres: each from kWallBelowM under the floor to kWallHeightM over it, open only on its sides whose
// neighbour is not a wall. Cells with no open side (inside a wall) are left out. Returns how many were written (up to max).
inline int Walls(const Patch& p, float floorUnits, float upm, float cxM, float czM, float reachM, Box* out, int max, Counts* counts = nullptr) {
    Counts c;
    int n = 0;
    if (!(upm >= 1.0f && upm <= 12.0f) || !std::isfinite(floorUnits) || !std::isfinite(cxM) || !std::isfinite(czM) || !(reachM > 0.0f) || !out || max <= 0) {
        if (counts) *counts = c;
        return 0;
    }
    const float shear = floorUnits * kShear, u = 1.0f / upm, floorY = floorUnits * u;
    auto cell = [&](float metres) { return (int)std::floor((metres * upm - shear) * 0.5f); };
    int x0 = cell(cxM - reachM), x1 = cell(cxM + reachM), z0 = cell(czM - reachM), z1 = cell(czM + reachM);
    if (x1 - x0 + 1 > kWindowCells) { const int mid = (x0 + x1) / 2; x0 = mid - kWindowCells / 2; x1 = x0 + kWindowCells - 1; }
    if (z1 - z0 + 1 > kWindowCells) { const int mid = (z0 + z1) / 2; z0 = mid - kWindowCells / 2; z1 = z0 + kWindowCells - 1; }
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x) {
            if (!p.Covers(x, z)) { ++c.uncovered; continue; }
            if (!p.Blocked(x, z)) continue;
            if (p.Bit(p.unknown, x, z)) { ++c.unknown; continue; }
            if (p.Bit(p.objects, x, z)) { ++c.objects; continue; }
            ++c.walls;
            const bool door = p.Bit(p.doors, x, z);
            if (door) ++c.doors;
            const uint8_t open = (uint8_t)((p.Wall(x + 1, z) ? 0 : FaceBit(0, 1)) | (p.Wall(x - 1, z) ? 0 : FaceBit(0, -1)) |
                                           (p.Wall(x, z + 1) ? 0 : FaceBit(2, 1)) | (p.Wall(x, z - 1) ? 0 : FaceBit(2, -1)));
            if (!open) { ++c.inside; continue; }   // its neighbours hold the weapon out of it
            if (n >= max) { ++c.dropped; continue; }
            Box& b = out[n++];
            b.kind = door ? Kind::Door : Kind::Wall;
            b.open = open;
            b.x = x;
            b.z = z;
            b.lo[0] = (2.0f * x + shear) * u;
            b.hi[0] = (2.0f * x + 2.0f + shear) * u;
            b.lo[1] = floorY - kWallBelowM;
            b.hi[1] = floorY + kWallHeightM;
            b.lo[2] = (2.0f * z + shear) * u;
            b.hi[2] = (2.0f * z + 2.0f + shear) * u;
        }
    if (counts) *counts = c;
    return n;
}

// The floor (render metres) under the eyes' column: its top at the hero's ground, pushing only up.
inline bool Floor(float floorUnits, float upm, float cxM, float czM, Box* out) {
    if (!out || !(upm >= 1.0f && upm <= 12.0f) || !std::isfinite(floorUnits) || !std::isfinite(cxM) || !std::isfinite(czM)) return false;
    const float y = floorUnits / upm;
    Box b;
    b.kind = Kind::Floor;
    b.open = FaceBit(1, 1);
    b.lo[0] = cxM - kFloorHalfM; b.hi[0] = cxM + kFloorHalfM;
    b.lo[1] = y - kFloorThickM;  b.hi[1] = y;
    b.lo[2] = czM - kFloorHalfM; b.hi[2] = czM + kFloorHalfM;
    *out = b;
    return true;
}

// When to sample again (Unleashed's wall_cells.cpp Ask): the hero moved more than kRecentre cells from the last patch's middle, or the last
// patch is older than kIntervalMs (doors open and close); never more often than kMinGapMs, and a queued sample is waited for up to kQueuedMs.
constexpr uint64_t kIntervalMs = 500, kMinGapMs = 50, kQueuedMs = 2000;
constexpr int kRecentre = 6;
inline bool Nearby(const Patch& last, bool have, uint64_t session, Cell c) {
    return have && last.session == session && std::abs(c.x - (last.x0 + kPatchCells / 2)) <= kRecentre && std::abs(c.z - (last.z0 + kPatchCells / 2)) <= kRecentre;
}
inline bool SampleDue(const Patch& last, bool have, uint64_t sampledAt, uint64_t session, Cell c, uint64_t now) {
    const bool isNear = Nearby(last, have, session, c);
    const uint64_t age = now >= sampledAt ? now - sampledAt : 0;
    if (isNear && age < kIntervalMs) return false;
    if (!isNear && have && age < kMinGapMs) return false;   // at most one game tick apart
    return true;
}

}  // namespace unleashedwalls
