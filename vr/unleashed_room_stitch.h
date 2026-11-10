// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed commit d4168617160f1759b6be6c7c612361fdd8dcd527 (Cal, 2026-11-08, "Dungeon rooms join up, and the ceiling stands
// over the level's highest floor"; merged in PR #468, main 4e0edb2), its steamvr/room_stitch.h, taken into D2R VR Authentic for
// [render] room_stitch: the four candidates, the game's own pick and the stitch's pick - kept as they are; only the namespace changed
// (unleashed::roomstitch). The wiring (the hook, the hero's room, its key; every view) is ours: vr/render/render_switches.cpp. (The same
// commit's ceiling top, steamvr/ceiling_top.h, was here as [ceiling] top_from_level from 0.209.8 and is removed: it lifted our ceilings
// over their own heights - Serg 2026-11-10.) The rest of D2R VR Authentic is MIT; this file stays MPL 2.0.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

// Dungeon rooms stitched toward the hero (Unleashed's owner, 2026-11-07: "There are sections of dungeons throughout
// the game that exist at higher and lower points than their adjacent tileset. This must be rectified so
// the player isn't seeing the next room high above them or below them, with a black void inbetween").
//
// D2R 3.3.93787 gives each room of a level a height when the 3D layer first builds it (0x6A2A40, from its
// four callers in the level translation layer, 0x6A1527 the room's own model among them). It takes the
// room's four edge heights (the art's floor at its west, north, east and south borders) and, for each
// neighbour already built (the DRLG room's near list, +0x10 data / +0x18 count, positions +0x60 / +0x64),
// the height that makes the shared border meet:
//     neighbour.height + neighbour's facing edge - this room's edge
// (the layer's room table, +0x2B0 data / +0x2B8 count, 0x2B0-byte entries sorted by the DRLG room's
// +0x1B8 key; an entry's edges +0x28 W, +0x2C N, +0x30 E, +0x34 S and its height +0x38). The hero's own
// room wins outright; otherwise the candidate largest in size wins. From the game's camera the choice is
// invisible: a room's height slides it along the line of sight. In first person every border the choice
// did not meet is a step of floors with void between - and with more room rings built, the hero's room is
// never a neighbour, so the choice was nearly always the arbitrary one.
//
// The rule here: the candidate of the built neighbour nearest the hero's room. Rooms then meet along the
// way the hero comes; what a loop of the level cannot close is left on the borders away from him. Units,
// missiles and items follow by themselves (the game finds their ground with a ray along the same line).
namespace unleashed::roomstitch {
constexpr uint64_t HeightRva = 0x6A2A40;
// mov rax,rsp; push rbx; push rsi; push rdi; push r13; sub rsp,0x58; movaps [rax-0x48],xmm7
constexpr uint8_t HeightSig[] = {0x48, 0x8b, 0xc4, 0x53, 0x56, 0x57, 0x41, 0x55, 0x48, 0x83, 0xec, 0x58, 0x0f, 0x29, 0x78, 0xb8};
// The table lookup the game uses (0x6B7450: a binary search on the entries' first qword), checked so
// the layout this file reads is the one the game reads.
constexpr uint64_t FindRva = 0x6B7450;
constexpr uint8_t FindSig[] = {0x40, 0x53, 0x4c, 0x69, 0x51, 0x08, 0xb0, 0x02, 0x00, 0x00, 0x4c, 0x8b, 0x01, 0x48, 0x8b, 0xda};

constexpr size_t NearData = 0x10, NearCount = 0x18, PosX = 0x60, PosY = 0x64, Key = 0x1B8;
constexpr size_t TableData = 0x2B0, TableCount = 0x2B8, Stride = 0x2B0, Height = 0x38;
enum Dir { West, North, East, South };
// The neighbour's edge facing this room, for a neighbour to the W, N, E, S.
constexpr size_t FacingEdge[4] = {0x30, 0x34, 0x28, 0x2C};
constexpr int MaxNear = 64;

struct Candidate {
    bool have = false;
    uintptr_t room = 0;
    int x = 0, y = 0;
    float h = 0;
};

// The table entry for a key, as 0x6B7450 finds it (0 when not built). read(address, value&) -> bool.
template <class Read>
uintptr_t Find(uintptr_t layer, uint64_t key, Read read) {
    uintptr_t data = 0;
    uint64_t count = 0;
    if (!read(layer + TableData, data) || !read(layer + TableCount, count) || !data || count > 100000) return 0;
    uint64_t lo = 0, hi = count;
    while (lo < hi) {
        const uint64_t mid = lo + (hi - lo) / 2;
        uint64_t k = 0;
        if (!read(data + mid * Stride, k)) return 0;
        if (k < key) lo = mid + 1;
        else hi = mid;
    }
    uint64_t k = 0;
    if (lo >= count || !read(data + lo * Stride, k) || k != key) return 0;
    return data + lo * Stride;
}

// The four candidates as the game finds them: per direction the first neighbour in the near list there
// (W: x less, y equal; N: x equal, y less; E: x more, y equal; S: x equal, y more), if built.
template <class Read>
int Candidates(uintptr_t layer, uintptr_t room, const float edges[4], Read read, Candidate out[4]) {
    for (int d = 0; d < 4; ++d) out[d] = {};
    uintptr_t data = 0;
    uint64_t count = 0;
    int32_t x = 0, y = 0;
    if (!read(room + NearData, data) || !read(room + NearCount, count) || !read(room + PosX, x) || !read(room + PosY, y)) return 0;
    if (!data || count > MaxNear) return 0;
    int found = 0;
    for (int d = 0; d < 4; ++d) {
        for (uint64_t i = 0; i < count; ++i) {
            uintptr_t nb = 0;
            int32_t nx = 0, ny = 0;
            if (!read(data + i * 8, nb) || !nb || !read(nb + PosX, nx) || !read(nb + PosY, ny)) continue;
            const bool there = d == West ? (nx < x && ny == y) : d == North ? (nx == x && ny < y) : d == East ? (nx > x && ny == y) : (nx == x && ny > y);
            if (!there) continue;
            uint64_t key = 0;
            if (!read(nb + Key, key)) break;
            const uintptr_t entry = Find(layer, key, read);
            float edge = 0, h = 0;
            if (entry && read(entry + FacingEdge[d], edge) && read(entry + Height, h) && std::isfinite(edge) && std::isfinite(h) &&
                std::isfinite(edges[d])) {
                out[d] = {true, nb, nx, ny, edge - edges[d] + h};
                ++found;
            }
            break;   // the game looks at the first neighbour in each direction only
        }
    }
    return found;
}

// The game's own pick (index, -1 = none: height 0): the hero's room outright, else the largest in size.
inline int GameChoice(const Candidate c[4], uintptr_t heroRoom) {
    int pick = -1;
    float best = 0;
    for (int d = 0; d < 4; ++d) {
        if (!c[d].have) continue;
        if (c[d].room == heroRoom) return d;
        if (std::fabs(c[d].h) > std::fabs(best)) { best = c[d].h; pick = d; }
    }
    return pick;
}
// Ours: the built neighbour nearest the hero's room (its origin, the rooms' own units); the hero's room
// itself first; a tie keeps the game's pick when it is among them. heroKnown false: the game's pick.
inline int Choice(const Candidate c[4], uintptr_t heroRoom, bool heroKnown, int hx, int hy) {
    const int game = GameChoice(c, heroRoom);
    if (!heroKnown) return game;
    int pick = -1;
    long best = 0;
    for (int d = 0; d < 4; ++d) {
        if (!c[d].have) continue;
        const long dist = c[d].room == heroRoom ? -1 : std::labs(long(c[d].x) - hx) + std::labs(long(c[d].y) - hy);
        if (pick < 0 || dist < best || (dist == best && d == game)) { pick = d; best = dist; }
    }
    return pick;
}
inline float HeightOf(const Candidate c[4], int pick) { return pick < 0 ? 0.0f : c[pick].h; }
inline const char* DirName(int d) { return d == West ? "west" : d == North ? "north" : d == East ? "east" : d == South ? "south" : "none"; }
}  // namespace unleashed::roomstitch
