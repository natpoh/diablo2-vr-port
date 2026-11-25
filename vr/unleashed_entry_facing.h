// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// Written by Piotr Kożuch (ptkk73) for D2R VR Unleashed, PR #602 "Character rotation on entering new areas/TP to town", 2026-11-21:
//   https://github.com/bearstonem/d2r-pcvr/commit/f5ecd55098f05c8c498a36efba8465fb67ee07af  always back to entrance after entering dungeons
//   https://github.com/bearstonem/d2r-pcvr/commit/1f36dc59c0943bb1ef3830cc44f2b083d70d1072  turn delay glitch, caves
//   https://github.com/bearstonem/d2r-pcvr/commit/29adeb2b0ef8660f70e505fba16f24b6a8bd9d32  improved detection, edge cases
//   https://github.com/bearstonem/d2r-pcvr/commit/b62c239aea56320ba32ff9e3af511efd137461b8  jail fix
//   https://github.com/bearstonem/d2r-pcvr/commit/a5bb284d75077fc4646338e649789718460d573d  town arrival faces the same NPC every time
//   https://github.com/bearstonem/d2r-pcvr/commit/18b04ed25bbd84935f68e9312e5161206b5044eb  build fix
//   https://github.com/bearstonem/d2r-pcvr/commit/1b223a03b90d694f464034b9b98dd2c84cd06a37  double rotation fix
//   https://github.com/bearstonem/d2r-pcvr/commit/b57ed57c5458d87fb12f04e832faadc715a022b2  town portal offsets, Kurast sewers exit
// Its steamvr/entry_facing.h (and the unit walk of steamvr/loot_aim.h it calls, Walk / ItemCell), taken into D2R VR Authentic for
// [camera] entry_facing. Thank you, Piotr. Kept as they are: Town, TownTargets (the act's NPC ids and the Harrogath perpendicular), TownArrival,
// WalkTown, NativeExitHeading, State, Direction, TurnDelta. Changed: the namespace; NearUnits is loot_aim's Walk under its own name (the
// sewer stairs' walk, vr/sewer_interaction.h); NativeTurnDelta is not taken (our native view gives TurnDelta the yaw it was built with, head
// included: vr/view/camera.cpp); Window added (ours): an arrival is resolved within kWindowMs of its first view - the game's wall cells are
// read on the game thread (vr/unleashed_wall_cells.cpp) and come a few frames after the level does. Not taken: vr/portal_placement.* (11d6f01,
// it moves where the server makes the portal - a game rule).
// The rest of D2R VR Authentic is MIT; this file stays MPL 2.0.
//
// Entry facing (VR F4, view only): on arriving in a town from outside it (a town portal, a waypoint) the view turns to the same spot of that
// act every time - toward Deckard Cain in the Rogue Encampment, Fara in Lut Gholein, Cain in Kurast Docks, between Halbu and Jamella in the
// Pandemonium Fortress, and in Harrogath across the town with Malah on the right. On entering any other area by its entrance (stairs, a cave
// mouth, a doorway) the view turns away from that entrance, into the area. Only the camera's body turn ([turn] / the right stick's) changes,
// once per arrival; the hero, his facing, his path and every game state stay the game's.
//
// Pure: no game, no Windows, no clock - `read` (a guarded memory read), the room's level and `walkable` are the caller's, so it runs on fakes.
// Headings in degrees, as the camera's: atan2(-forward.x, -forward.z), the game's grid x / y being the world's x / z (vr/aim_point.h).
#pragma once
#include <cmath>
#include <cstdint>

namespace unleashed::entryfacing {

inline bool Town(int level) {
    return level == 1 || level == 40 || level == 75 || level == 103 || level == 109;
}

// MonStats rows of the stock game (the unit's class, +0x04): 146 cain1 / 265 cain5 (Rogue Encampment), 178 fara (Lut Gholein), 245 cain3
// (Kurast Docks), 257 halbu + 405 jamella (Pandemonium Fortress), 513 malah (Harrogath). The two traders are kept apart so their midpoint
// does not depend on the order the rooms and units are walked in.
struct TownTargets {
    struct Point { float x = 0, y = 0; bool valid = false; };
    Point first, second;
    void Add(int level, uint32_t id, float x, float y) {
        if (!std::isfinite(x + y) || x <= 0 || y <= 0) return;
        if ((level == 1 && (id == 146 || id == 265)) || (level == 40 && id == 178) || (level == 75 && id == 245) || (level == 103 && id == 257) ||
            (level == 109 && id == 513))
            first = {x, y, true};
        if (level == 103 && id == 405) second = {x, y, true};
    }
    bool Heading(int level, float px, float py, float& heading) const {
        if (!Town(level) || (!first.valid && !second.valid)) return false;
        Point target = first.valid ? first : second;
        if (first.valid && second.valid) target = {(first.x + second.x) * .5f, (first.y + second.y) * .5f, true};
        float dx = target.x - px, dy = target.y - py;
        if (!std::isfinite(dx + dy) || std::hypot(dx, dy) < .5f) return false;
        // Harrogath: perpendicular to Malah, she on the right, looking into the town instead of up at her raised shop.
        if (level == 109) { const float x = dx; dx = dy; dy = -x; }
        heading = std::atan2(-dx, -dy) * 57.2957795f;
        return true;
    }
};

// An arrival is a change of level, not nearness to a portal: the landing (and the walk off it) can already be beyond four cells when the
// first view is drawn. Waypoint returns take the same town facing.
inline bool TownArrival(int from, int to) {
    return from > 0 && !Town(from) && Town(to);
}

// The NPC may be beyond the rooms next to the hero: the loaded rooms of the same level, walked once at the arrival, bounded and without
// repeats; no room is loaded, no game pointer kept across frames. Room +0x00 its near rooms, +0x40 their count, +0xA8 its first unit; a unit
// +0x160 the next in the room; visit(unit, header[4]: type, class, id, mode).
template <class Read, class Level, class Visit>
void WalkTown(uintptr_t room, int level, Read read, Level levelOf, Visit visit) {
    uintptr_t rooms[128]{room};
    int count = 1, visited = 0;
    for (int r = 0; r < count && visited < 2048; ++r) {
        if (rooms[r] < 0x10000 || levelOf(rooms[r]) != level) continue;
        uintptr_t nearby = 0, unit = 0;
        uint32_t n = 0;
        if (read(rooms[r], nearby) && read(rooms[r] + 0x40, n) && n <= 64 && nearby >= 0x10000)
            for (uint32_t i = 0; i < n && count < 128; ++i) {
                uintptr_t next = 0;
                if (!read(nearby + i * 8, next)) break;
                bool seen = false;
                for (int j = 0; j < count; ++j)
                    if (rooms[j] == next) { seen = true; break; }
                if (next >= 0x10000 && !seen) rooms[count++] = next;
            }
        if (!read(rooms[r] + 0xa8, unit)) continue;
        for (int chain = 0; unit >= 0x10000 && chain < 256 && visited < 2048; ++chain) {
            ++visited;
            uintptr_t next = 0;
            uint32_t header[4]{};
            if (!read(unit, header) || !read(unit + 0x160, next)) break;
            visit(unit, header);
            if (next == unit) break;
            unit = next;
        }
    }
}

// The units near the hero (Unleashed's loot_aim Walk, the sewer stairs' walk): the hero's path +0x38 -> its room +0x20 -> that room and its
// near rooms -> their units. heroX / heroY: the hero's cell (16.16 on his dynamic path +0x00). visit(unit, header) returns false to stop.
// Returns the units visited (0: the hero's path or room not readable).
template <class Read, class Visit>
int NearUnits(uintptr_t hero, Read read, Visit visit, float& heroX, float& heroY) {
    uintptr_t path = 0, room = 0, nearby = 0;
    uint32_t xy[2]{}, nearCount = 0;
    if (!read(hero + 0x38, path) || !read(path, xy) || !read(path + 0x20, room) || room < 0x10000) return 0;
    if (!read(room, nearby) || !read(room + 0x40, nearCount) || nearCount > 64 || (!nearby && nearCount)) return 0;
    heroX = xy[0] / 65536.f;
    heroY = xy[1] / 65536.f;
    uintptr_t rooms[65]{room};
    int roomCount = 1;
    for (uint32_t i = 0; i < nearCount; ++i) {
        uintptr_t next = 0;
        if (!read(nearby + i * 8, next)) break;
        bool seen = false;
        for (int j = 0; j < roomCount; ++j)
            if (rooms[j] == next) { seen = true; break; }
        if (next >= 0x10000 && !seen) rooms[roomCount++] = next;
    }
    int visited = 0;
    for (int r = 0; r < roomCount && visited < 1024; ++r) {
        uintptr_t unit = 0;
        if (!read(rooms[r] + 0xa8, unit)) continue;
        for (int chain = 0; unit && chain < 256 && visited < 1024; ++chain) {
            ++visited;
            uintptr_t next = 0;
            uint32_t header[4]{};
            if (!read(unit + 0x160, next) || !read(unit, header)) break;
            if (!visit(unit, header)) return visited;
            if (next == unit) break;
            unit = next;
        }
    }
    return visited;
}

// A tile's (or any static unit's) cell on its static path (+0x38 -> +0x10 / +0x14).
template <class Read>
bool StaticCell(uintptr_t unit, Read read, uint32_t (&cell)[2]) {
    uintptr_t staticPath = 0;
    return read(unit + 0x38, staticPath) && read(staticPath + 0x10, cell) && cell[0] && cell[1] && cell[0] <= 65535 && cell[1] <= 65535;
}

// The stock LvlWarp's ExitWalkX/Y gives the way off the stairs, whatever the landing's fraction: the Act 1 jail (13, 14) and the Kurast
// sewers - the surface exit (57) faces +Y, its two stair turns underground (58 / 59) +X / +Y.
inline bool NativeExitHeading(uint32_t warp, float& heading) {
    switch (warp) {
    case 13: case 14: case 57: case 59: heading = 180.f; return true;
    case 58: heading = -90.f; return true;
    case 60: heading = -135.f; return true;
    default: return false;
    }
}

// A live change of level, seen once: a new session (game, hero) starts over without a turn; level 0 (none yet) is ignored.
struct State {
    uint64_t session = 0;
    int level = 0;
    bool Observe(uint64_t s, int l, uint64_t) {
        if (!s) { *this = {}; return false; }
        if (s != session) { session = s; level = l; return false; }
        if (!l) return false;
        const int previous = level;
        level = l;
        return previous && l != previous;
    }
};

// Away from the return tile. When the hero stands on the tile, the longest walkable ray finds the room's inside instead of dividing by zero.
template <class Walkable>
bool Direction(float px, float py, float tx, float ty, Walkable walkable, float& heading) {
    if (!std::isfinite(px + py + tx + ty)) return false;
    const float dx = px - tx, dy = py - ty, d = std::hypot(dx, dy);
    if (d > 20) return false;
    constexpr float pi = 3.14159265f;
    const float away = d >= .5f ? std::atan2(dy, dx) : 0.f;
    float best = -1, bestAngle = 0;
    for (int i = 0; i < 17; ++i) {
        const float angle = i == 0 ? away : (i - 1) * pi / 8;
        const float fx = std::cos(angle), fy = std::sin(angle);
        const float alignment = d >= .5f ? (fx * dx + fy * dy) / d : 0;
        if (d >= .5f && alignment < .25f) continue;
        float reach = 0;
        for (int step = 1; step <= 12; ++step) {
            const float distance = step * .5f;
            if (!walkable(int(std::lround(px + fx * distance)), int(std::lround(py + fy * distance)))) break;
            reach = distance;
        }
        if (reach < 1.5f) continue;
        // Facing is not route planning: a longer diagonal must not win over a clear way away from the entrance (farther furniture and walls
        // pulled the view sideways by where the hero landed).
        if (i == 0 && d >= .5f) {
            heading = std::atan2(-fx, -fy) * 180 / pi;
            return true;
        }
        const float score = reach + alignment * 2.f;
        if (score > best) { best = score; bestAngle = angle; }
    }
    if (best < 0) return false;
    heading = std::atan2(-std::cos(bestAngle), -std::sin(bestAngle)) * 180 / pi;
    return true;
}

// The body turn to add so a view at cameraHeading (the game camera's) turned by currentYaw (+ right, as the view's yaw) faces destination.
inline float TurnDelta(float cameraHeading, float currentYaw, float destinationHeading) {
    return std::remainder(cameraHeading - currentYaw - destinationHeading, 360.f);
}

// Ours: an arrival waits for what it needs (the wall cells, an NPC's room) at most kWindowMs from the first view of the new level, then is
// dropped - never a turn long after the player has looked round. Without the wall cells, after kWallsWaitMs, an entrance not under the hero
// is turned away from as it is (every way walkable); one under him is left as it is.
constexpr uint64_t kWindowMs = 600, kWallsWaitMs = 250;
struct Window {
    int from = 0, to = 0;
    uint64_t at = 0;
    bool open = false;
    void Start(int fromLevel, int toLevel, uint64_t now) { from = fromLevel; to = toLevel; at = now; open = true; }
    uint64_t Age(uint64_t now) const { return now >= at ? now - at : 0; }
    bool Expired(uint64_t now) const { return open && Age(now) > kWindowMs; }
    bool WallsLate(uint64_t now) const { return Age(now) >= kWallsWaitMs; }
    void Close() { open = false; }
};

}  // namespace unleashed::entryfacing
