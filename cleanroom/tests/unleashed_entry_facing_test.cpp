// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed's steamvr/entry_facing_test.cpp by Piotr Kożuch (ptkk73), PR #602, at
// https://github.com/bearstonem/d2r-pcvr/commit/b57ed57c5458d87fb12f04e832faadc715a022b2 (and the commits before it named in
// vr/unleashed_entry_facing.h). Not taken: the native quaternion check of NativeTurnDelta (not taken either). Added (ours): TurnDelta against
// the view's own heading formula, NearUnits / StaticCell on a fake room, Window.
// The rest of D2R VR Authentic is MIT; this file stays MPL 2.0.
//
// Offline checks of vr/unleashed_entry_facing.h ([camera] entry_facing). Run: unleashed_entry_facing_test.exe
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>   // the Windows macros the real wiring's translation unit sees
#endif
#include "unleashed_entry_facing.h"

#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <unordered_map>
#include <utility>

using namespace unleashed::entryfacing;

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; }
}

int main() {
    State s;
    float h = 0;
    // Every act its own NPC, other NPCs ignored.
    for (auto pair : {std::pair{1, 146}, std::pair{1, 265}, std::pair{40, 178}, std::pair{75, 245}, std::pair{103, 257}}) {
        TownTargets targets;
        targets.Add(pair.first, 999, 100, 90);
        Check(!targets.Heading(pair.first, 100, 100, h), "an unrelated NPC gives no heading");
        targets.Add(pair.first, pair.second, 110, 100);
        Check(targets.Heading(pair.first, 100, 100, h) && std::abs(h + 90) < .01f, "the act's NPC to the +x gives heading -90");
    }
    TownTargets traders;
    traders.Add(103, 405, 110, 110);
    traders.Add(103, 257, 110, 90);
    Check(traders.Heading(103, 100, 100, h) && std::abs(h + 90) < .01f, "Act 4: the midpoint of Halbu and Jamella");
    TownTargets jamellaOnly;
    jamellaOnly.Add(103, 405, 100, 110);
    Check(jamellaOnly.Heading(103, 100, 100, h) && std::abs(std::abs(h) - 180) < .01f, "Act 4: Jamella alone when Halbu is not loaded");
    TownTargets malah;
    malah.Add(109, 513, 110, 110);
    Check(malah.Heading(109, 100, 100, h), "Harrogath: Malah gives a heading");
    const float radians = h * .0174532925f;
    const float fx = -std::sin(radians), fy = -std::cos(radians);
    Check(std::abs(fx * 10 + fy * 10) < .001f, "Harrogath: Malah perpendicular to forward");
    Check(-fy * 10 + fx * 10 > 14.f, "Harrogath: Malah on the right, never on the left");
    Check(!malah.Heading(109, 110, 110, h), "standing on the NPC: no heading");
    TownTargets wrongTown;
    wrongTown.Add(40, 146, 110, 100);   // Cain's Act 1 row in Lut Gholein: not this act's
    Check(!wrongTown.Heading(40, 100, 100, h), "another act's NPC id is not taken");
    TownTargets bad;
    bad.Add(1, 146, NAN, 100);
    bad.Add(1, 146, 0, 100);
    Check(!bad.Heading(1, 100, 100, h), "a position not finite or at 0 is not taken");
    for (int town : {1, 40, 75, 103, 109}) {
        Check(TownArrival(8, town), "a dungeon to a town is a town arrival");
        Check(!TownArrival(0, town), "no level before: not an arrival");
        Check(!TownArrival(40, town), "a town to a town: not an arrival");
    }
    Check(!TownArrival(8, 9), "a dungeon to a dungeon: not a town arrival");
    Check(!Town(2) && Town(109), "towns");
    // Observed in Unleashed: the second town portal landed / walked 7.78 cells from its marker. Both returns apply, whatever the marker's
    // distance or whether it was loaded on the first view.
    State portals;
    Check(!portals.Observe(1, 75, 0), "the first level of a session is no arrival");
    for (int i = 0; i < 2; ++i) {
        Check(portals.Observe(1, 83, 100 + i * 100), "town to Travincal is a change");
        const int from = portals.level;
        Check(portals.Observe(1, 75, 150 + i * 100) && TownArrival(from, portals.level), "back to the Docks is a town arrival");
        Check(!portals.Observe(1, 75, 151 + i * 100), "seen once");
    }
    // The loaded room graph: an NPC two rooms away, cycles walked once, other levels skipped, a looping unit chain stopped.
    std::unordered_map<uintptr_t, unsigned char> memory;
    auto put = [&](uintptr_t at, const auto& value) {
        auto p = reinterpret_cast<const unsigned char*>(&value);
        for (size_t i = 0; i < sizeof value; ++i) memory[at + i] = p[i];
    };
    auto read = [&](uintptr_t at, auto& value) {
        auto p = reinterpret_cast<unsigned char*>(&value);
        for (size_t i = 0; i < sizeof value; ++i) {
            auto it = memory.find(at + i);
            if (it == memory.end()) return false;
            p[i] = it->second;
        }
        return true;
    };
    for (uintptr_t r : {(uintptr_t)0x10000u, (uintptr_t)0x20000u, (uintptr_t)0x30000u, (uintptr_t)0x40000u}) {
        put(r, r + 0x1000);
        put(r + 0x40, uint32_t(2));
        put(r + 0xa8, uintptr_t(0));
        put(r + 0x1000, uintptr_t(0x10000));
        put(r + 0x1008, r + 0x10000);
    }
    put(0x30000 + 0xa8, uintptr_t(0x90000));
    uint32_t npc[4]{1, 178, 1, 0};
    put(0x90000, npc);
    put(0x90000 + 0x160, uintptr_t(0x90000));
    put(0x40000 + 0xa8, uintptr_t(0x90000));   // must not be visited: the other level
    int found = 0;
    WalkTown(0x10000, 40, read, [](uintptr_t r) { return r == 0x40000 ? 41 : 40; }, [&](uintptr_t, const uint32_t* header) {
        Check(header[1] == 178, "the NPC found");
        ++found;
    });
    Check(found == 1, "the NPC two rooms away found once, the other level's room skipped");
    // Level changes, sessions.
    Check(!s.Observe(1, 2, 0), "first observation");
    Check(!s.Observe(1, 0, 10), "level 0 ignored");
    Check(s.Observe(1, 8, 100), "a change");
    Check(!s.Observe(1, 8, 100) && !s.Observe(1, 8, 180), "the same level again: nothing");
    Check(s.Observe(1, 2, 300) && !s.Observe(1, 2, 1800), "back: a change once");
    Check(!s.Observe(2, 8, 2000), "a new session starts over without a turn");
    Check(!s.Observe(0, 0, 2100) && s.session == 0, "no session clears");
    // Away from the entrance.
    Check(Direction(100, 100, 98, 100, [](int, int) { return true; }, h) && std::abs(h + 90) < .01f, "the tile to the west: face east");
    // A farther obstacle makes a diagonal longer, but the way ahead from the door is clear: neither side pulls the view.
    for (int side : {-1, 1}) {
        Check(Direction(100, 100, 98, 100, [&](int x, int y) { return x < 104 || (y - 100) * side > 0; }, h) && std::abs(h + 90) < .01f,
              "a farther obstacle does not pull the view sideways");
    }
    // A cave's landing exactly on the warp: west a wall, east the way in.
    Check(Direction(100, 100, 100, 100, [](int x, int y) { return x >= 100 && y >= 99 && y <= 101; }, h) && std::abs(h + 90) < .01f,
          "on the warp: the longest walkable ray");
    // Leaving a cave: the entrance to the east must be behind.
    Check(Direction(100, 100, 102, 100, [](int x, int) { return x <= 100; }, h) && std::abs(h - 90) < .01f, "the cave mouth behind");
    Check(!Direction(100, 100, 100, 100, [](int, int) { return false; }, h), "nothing walkable: no heading");
    Check(!Direction(100, 100, 130, 100, [](int, int) { return true; }, h), "a tile farther than 20 cells is not the entrance");
    // The jail's landings vary sideways and along the stairs: the doorway's way stays +Y even when spawn-to-tile is diagonal.
    for (uint32_t warp : {13u, 14u})
        for (float x : {99.5f, 100.f, 100.5f})
            for (float y : {100.8f, 102.f, 104.f}) {
                float direction = 0;
                Check(NativeExitHeading(warp, direction) || Direction(x, y, 100, 100, [](int, int) { return true; }, direction), "jail heading");
                const float r = direction * .0174532925f;
                Check(std::abs(std::sin(r)) < .001f && -std::cos(r) > .999f, "jail: +Y");
            }
    // Kurast's surface stairs face out into +Y, never toward the hole by a sideways or overlapping landing.
    Check(NativeExitHeading(57, h) && h == 180.f, "warp 57");
    Check(NativeExitHeading(58, h) && h == -90.f, "warp 58");
    Check(NativeExitHeading(59, h) && h == 180.f, "warp 59");
    Check(NativeExitHeading(60, h) && h == -135.f, "warp 60");
    Check(!NativeExitHeading(56, h), "other entrances keep the resolver");
    // TurnDelta brings the view's heading (camera - yaw, vr/view/camera.cpp) to the destination, from any start.
    for (float dest : {-135.f, 0.f, 90.f, 180.f})
        for (float base : {-170.f, 0.f, 135.f})
            for (float yaw : {-180.f, 30.f, 179.f}) {
                const float turn = TurnDelta(base, yaw, dest);
                Check(std::abs(std::remainder(base - (yaw + turn) - dest, 360.f)) < .001f, "TurnDelta reaches the destination");
                Check(std::abs(turn) <= 180.001f, "TurnDelta takes the short way");
            }
    // NearUnits: the hero's room and its near rooms, the nearest tile and its static cell.
    {
        memory.clear();
        const uintptr_t hero = 0x100000, path = 0x110000, room = 0x120000, other = 0x130000, list = 0x140000;
        put(hero + 0x38, path);
        uint32_t xy[2]{uint32_t(50.5f * 65536), uint32_t(60.25f * 65536)};
        put(path, xy);
        put(path + 0x20, room);
        put(room, list);
        put(room + 0x40, uint32_t(2));
        put(list, room);   // itself in its own list: not walked twice
        put(list + 8, other);
        put(other, uintptr_t(0));
        put(other + 0x40, uint32_t(0));
        const uintptr_t tileA = 0x150000, tileB = 0x160000, monster = 0x170000;
        put(room + 0xa8, monster);
        uint32_t mh[4]{1, 999, 7, 1};
        put(monster, mh);
        put(monster + 0x160, tileA);
        uint32_t ah[4]{5, 57, 8, 0};
        put(tileA, ah);
        put(tileA + 0x160, uintptr_t(0));
        put(tileA + 0x38, uintptr_t(0x180000));
        uint32_t ac[2]{55, 60};
        put(0x180000 + 0x10, ac);
        put(other + 0xa8, tileB);
        uint32_t bh[4]{5, 30, 9, 0};
        put(tileB, bh);
        put(tileB + 0x160, tileB);   // a looping chain ends
        put(tileB + 0x38, uintptr_t(0x190000));
        uint32_t bc[2]{51, 61};
        put(0x190000 + 0x10, bc);
        float px = 0, py = 0, best = 20;
        uint32_t warp = 0, units = 0;
        const int visited = NearUnits(hero, read, [&](uintptr_t unit, const uint32_t* hd) {
            ++units;
            uint32_t cell[2]{};
            if (hd[0] != 5 || !StaticCell(unit, read, cell)) return true;
            const float d = std::hypot(float(cell[0]) - px, float(cell[1]) - py);
            if (d < best) { best = d; warp = hd[1]; }
            return true;
        }, px, py);
        Check(visited == 3 && units == 3, "NearUnits: three units over two rooms, the room listed in itself walked once");
        Check(std::abs(px - 50.5f) < 1e-4f && std::abs(py - 60.25f) < 1e-4f, "NearUnits: the hero's cell from 16.16");
        Check(warp == 30 && std::abs(best - std::hypot(0.5f, 0.75f)) < 1e-4f, "the nearest tile is the next room's");
        float qx = 0, qy = 0;
        Check(NearUnits(0x200000, read, [](uintptr_t, const uint32_t*) { return true; }, qx, qy) == 0, "an unreadable hero walks nothing");
        uint32_t cell[2]{};
        put(0x1A0000 + 0x38, uintptr_t(0x1B0000));
        uint32_t zero[2]{0, 5};
        put(0x1B0000 + 0x10, zero);
        Check(!StaticCell(0x1A0000, read, cell), "a cell at 0 is no cell");
    }
    // Window (ours): an arrival lives kWindowMs from its first view; the wall cells are waited for kWallsWaitMs.
    {
        Window w;
        Check(!w.open && !w.Expired(5000), "closed: never expires");
        w.Start(8, 1, 1000);
        Check(w.open && w.from == 8 && w.to == 1, "started");
        Check(!w.WallsLate(1000 + kWallsWaitMs - 1) && w.WallsLate(1000 + kWallsWaitMs), "walls late after kWallsWaitMs");
        Check(!w.Expired(1000 + kWindowMs) && w.Expired(1000 + kWindowMs + 1), "expires after kWindowMs");
        Check(!w.Expired(500) && w.Age(500) == 0, "a clock before the start is age 0");
        w.Close();
        Check(!w.open && !w.Expired(99999), "closed");
    }
    if (g_fail) { std::printf("%d check(s) failed\n", g_fail); return 1; }
    std::puts("entry facing: the five towns' NPCs (Harrogath perpendicular), the bounded room walk, level changes, caves / jail / Kurast stairs, "
              "the turn, the near units and the window - all passed");
    return 0;
}
