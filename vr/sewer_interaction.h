// sewer_interaction.h - the Kurast sewer stairs taken with A while standing (VR F4).
//
// Kurast Bazaar (80) and Upper Kurast (81) go down to Sewers Level 1 by warp 57 (a tile unit: type 5, class 57). Its native selection
// rectangle is found by the controller's target only when the eyes are low and turned to it - the player had to crouch and look right,
// and the A press found nothing. When the game's own Interact query (skill 357) answers none, vrcam asks Find for the real warp nearby
// that the head faces (its height ignored); the game's own reach and line-of-sight test (0x348650) must still accept it. Travel, quest
// rules and the one-target-per-press guard stay the game's. Only that warp, only those two areas.
//
// From bearstonem/d2r-pcvr (MIT, v0.3.20: steamvr/sewer_interaction.h, docs/KURAST_SEWER_ENTRANCES.md), the same logic on the same
// 3.3.93787 layout. Pure: `read` and `allowed` are the caller's (guarded memory reads, the game's range test), so it runs on fakes.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace sewerinteraction {

constexpr int kBazaar = 80, kUpperKurast = 81;
constexpr uint32_t kTileType = 5, kSewerWarp = 57;
constexpr float kNearCells = 8.0f;     // a warp farther than this is not looked at
constexpr float kUnderCells = 0.75f;   // standing on it: any facing
constexpr float kFacingCos = 0.5f;     // within 60 degrees of the head's heading

inline bool Area(int level) { return level == kBazaar || level == kUpperKurast; }

struct Result {
    uintptr_t unit = 0;
    uint32_t id = 0, visited = 0, tiles = 0, firstTileClass = UINT32_MAX, warps = 0, facing = 0;
    float distance = 0;
};

// player: the client's local hero unit. forward: the head's heading on the game's grid (any length).
// The unit layout of 3.3.93787: +0x00 type, +0x04 class, +0x08 id, +0x0C mode, +0x38 path; a player's path +0x00 x, +0x04 y (16.16)
// and +0x20 its room; a room +0x00 its neighbours, +0x40 their count, +0xA8 its first unit; a unit +0x160 the next in the room
// (NOT the hash-list link +0x158); a tile's static path +0x00 its room, +0x10 x, +0x14 y (cells).
template <class Read, class Allowed>
Result Find(uintptr_t player, int level, float forwardX, float forwardY, Read read, Allowed allowed) {
    Result result;
    const float length = std::hypot(forwardX, forwardY);
    if (!Area(level) || !std::isfinite(length) || length < 0.05f) return result;
    forwardX /= length;
    forwardY /= length;
    uint32_t header[4]{}, xy[2]{};
    uintptr_t path = 0, room = 0;
    if (!read(player, header) || header[0] != 0 || (header[3] != 1 && header[3] != 2 && header[3] != 3) ||   // standing, walking, running
        !read(player + 0x38, path) || !read(path, xy) || !read(path + 0x20, room) || room < 0x10000)
        return result;
    const float x = xy[0] / 65536.0f, y = xy[1] / 65536.0f;
    if (x <= 0 || y <= 0) return result;
    std::array<uintptr_t, 65> rooms{};
    unsigned count = 1;
    rooms[0] = room;
    uintptr_t nearby = 0;
    uint32_t nearCount = 0;
    if (!read(room, nearby) || !read(room + 0x40, nearCount) || nearCount > 64 || (!nearby && nearCount)) return result;
    for (unsigned i = 0; i < nearCount; ++i) {
        uintptr_t next = 0;
        if (!read(nearby + i * 8, next)) return result;
        if (next >= 0x10000 && std::find(rooms.begin(), rooms.begin() + count, next) == rooms.begin() + count) rooms[count++] = next;
    }
    float best = kNearCells;
    for (unsigned r = 0; r < count; ++r) {
        uintptr_t unit = 0;
        if (!read(rooms[r] + 0xA8, unit)) continue;
        for (unsigned chain = 0; unit && chain < 256 && result.visited < 1024; ++chain) {
            ++result.visited;
            uintptr_t next = 0;
            uint32_t h[3]{};
            if (!read(unit + 0x160, next) || !read(unit, h)) break;
            if (h[0] == kTileType) {
                ++result.tiles;
                if (result.firstTileClass == UINT32_MAX) result.firstTileClass = h[1];
            }
            if (h[0] == kTileType && h[1] == kSewerWarp) {
                uintptr_t staticPath = 0, ownerRoom = 0;
                uint32_t position[2]{};
                if (read(unit + 0x38, staticPath) && read(staticPath, ownerRoom) && ownerRoom == rooms[r] && read(staticPath + 0x10, position) &&
                    position[0] > 0 && position[1] > 0 && position[0] <= 65535 && position[1] <= 65535) {
                    const float dx = float(position[0]) - x, dy = float(position[1]) - y;
                    const float distance = std::hypot(dx, dy);
                    if (distance <= kNearCells) {
                        ++result.warps;
                        // The head's height is not asked: standing, the stairs are taken without crouching or looking at the floor.
                        if (distance < kUnderCells || (dx * forwardX + dy * forwardY) / distance >= kFacingCos) {
                            ++result.facing;
                            if (distance < best && allowed(player, unit)) {
                                best = distance;
                                result.unit = unit;
                                result.id = h[2];
                                result.distance = distance;
                            }
                        }
                    }
                }
            }
            if (next == unit) break;
            unit = next;
        }
    }
    return result;
}

}  // namespace sewerinteraction
