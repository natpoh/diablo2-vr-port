// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed commit d4168617160f1759b6be6c7c612361fdd8dcd527 (Cal), its steamvr/room_stitch_test.cpp and
// steamvr/ceiling_top_test.cpp, for vr/unleashed_room_stitch.h. Taken as they are; assert() became CHECK (a Release build keeps it) and a
// few checks of ours were added at the end (marked). This file stays MPL 2.0.
#include "unleashed_room_stitch.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

static int g_fail = 0, g_pass = 0;
#define CHECK(c) do { if (c) ++g_pass; else { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

using namespace unleashed::roomstitch;
using namespace unleashed::ceilingtop;

// A fake game memory: byte buffers at made-up addresses.
struct Memory {
    std::map<uintptr_t, std::vector<uint8_t>> blocks;
    uintptr_t next = 0x100000;
    uintptr_t Alloc(size_t n) {
        const uintptr_t a = next;
        blocks[a].assign(n, 0);
        next += (n + 0xfff) & ~size_t(0xfff);
        next += 0x1000;
        return a;
    }
    template <class T> void Put(uintptr_t a, T v) {
        for (auto& [b, bytes] : blocks)
            if (a >= b && a + sizeof v <= b + bytes.size()) { std::memcpy(&bytes[a - b], &v, sizeof v); return; }
        CHECK(false);
    }
    template <class T> bool Get(uintptr_t a, T& v) const {
        for (auto& [b, bytes] : blocks)
            if (a >= b && a + sizeof v <= b + bytes.size()) { std::memcpy(&v, &bytes[a - b], sizeof v); return true; }
        return false;
    }
};
struct Room { uintptr_t at = 0; int x = 0, y = 0; uint64_t key = 0; };

static void StitchTest() {
    Memory m;
    const auto read = [&](uintptr_t a, auto& v) { return m.Get(a, v); };
    // Five rooms in a row and one above the middle: W(0,0) H(10,0) M(20,0) E(30,0), N(20,-10) S(20,10).
    auto mk = [&](int x, int y, uint64_t key) {
        Room r;
        r.at = m.Alloc(0x200);
        r.x = x; r.y = y; r.key = key;
        m.Put(r.at + PosX, int32_t(x)); m.Put(r.at + PosY, int32_t(y)); m.Put(r.at + Key, key);
        return r;
    };
    const Room w = mk(0, 0, 50), h = mk(10, 0, 20), mid = mk(20, 0, 90), e = mk(30, 0, 70), n = mk(20, -10, 10), s = mk(20, 10, 40);
    // The middle room's near list: everyone (order shuffled), plus a second room to the west further away.
    const Room farW = mk(-10, 0, 99);
    const uintptr_t list = m.Alloc(8 * 8);
    const uintptr_t order[] = {s.at, farW.at, e.at, n.at, h.at, w.at};
    for (int i = 0; i < 6; ++i) m.Put(list + i * 8, order[i]);
    m.Put(mid.at + NearData, list); m.Put(mid.at + NearCount, uint64_t(6));
    // The layer's table: entries sorted by key. Built: h (key 20), n (10), s (40), e (70); not w, farW, mid.
    const uintptr_t layer = m.Alloc(0x400);
    struct E { uint64_t key; float edges[4]; float height; };
    std::vector<E> entries = {{10, {0, 0, 0, 2}, 5}, {20, {0, 0, 1, 0}, 0}, {40, {0, 3, 0, 0}, -6.5f}, {70, {4, 0, 0, 0}, 12}};
    const uintptr_t table = m.Alloc(entries.size() * Stride);
    for (size_t i = 0; i < entries.size(); ++i) {
        const uintptr_t at = table + i * Stride;
        m.Put(at, entries[i].key);
        for (int k = 0; k < 4; ++k) m.Put(at + 0x28 + k * 4, entries[i].edges[k]);
        m.Put(at + Height, entries[i].height);
    }
    m.Put(layer + TableData, table); m.Put(layer + TableCount, uint64_t(entries.size()));
    // Find: every built key, none of the others.
    for (size_t i = 0; i < entries.size(); ++i) CHECK(Find(layer, entries[i].key, read) == table + i * Stride);
    for (uint64_t k : {0ull, 15ull, 50ull, 90ull, 1000ull}) CHECK(!Find(layer, k, read));
    // The middle room's edges (W N E S) and its candidates: neighbour h + facing edge - own edge.
    const float edges[4] = {1, 2, 3, 4};
    Candidate c[4];
    // West: the first room to the west in the list is farW (not built) - the game stops there, so no west.
    CHECK(Candidates(layer, mid.at, edges, read, c) == 3);
    CHECK(!c[West].have);
    CHECK(c[North].have && c[North].room == n.at && c[North].h == 5 + 2 - 2);        // n's south edge 2, our north 2
    CHECK(c[East].have && c[East].room == e.at && c[East].h == 12 + 4 - 3);          // e's west edge 4, our east 3
    CHECK(c[South].have && c[South].room == s.at && c[South].h == -6.5f + 3 - 4);   // s's north edge 3, our south 4
    // The game: the hero's room outright, else the largest in size (13 east).
    CHECK(GameChoice(c, 0) == East);
    CHECK(GameChoice(c, s.at) == South);
    // Ours: the built neighbour nearest the hero's room.
    CHECK(Choice(c, 0, true, 20, 20) == South);   // hero south of us: the south room (distance 10)
    CHECK(Choice(c, 0, true, 20, -30) == North);
    CHECK(Choice(c, 0, true, 40, 0) == East);
    CHECK(Choice(c, n.at, true, 20, -10) == North);   // his own room first
    CHECK(Choice(c, 0, false, 0, 0) == East);         // hero unknown: the game's pick
    // A tie keeps the game's pick: hero at (25,-5) is 10 from n and 10 from e.
    CHECK(Choice(c, 0, true, 25, -5) == East);
    CHECK(HeightOf(c, -1) == 0);
    // With the hero's room to the west built, it wins (game and ours).
    entries.insert(entries.begin() + 3, E{50, {0, 0, 6, 0}, 1});   // w (key 50) built
    const uintptr_t order2[] = {s.at, w.at, e.at, n.at, h.at, farW.at};   // w now before farW in the near list
    for (int i = 0; i < 6; ++i) m.Put(list + i * 8, order2[i]);
    const uintptr_t table2 = m.Alloc(entries.size() * Stride);
    for (size_t i = 0; i < entries.size(); ++i) {
        const uintptr_t at = table2 + i * Stride;
        m.Put(at, entries[i].key);
        for (int k = 0; k < 4; ++k) m.Put(at + 0x28 + k * 4, entries[i].edges[k]);
        m.Put(at + Height, entries[i].height);
    }
    m.Put(layer + TableData, table2); m.Put(layer + TableCount, uint64_t(entries.size()));
    CHECK(Candidates(layer, mid.at, edges, read, c) == 4 && c[West].room == w.at && c[West].h == 1 + 6 - 1);
    CHECK(GameChoice(c, w.at) == West && Choice(c, w.at, true, 0, 0) == West);
    // Unreadable or absurd lists give nothing.
    m.Put(mid.at + NearCount, uint64_t(1000));
    CHECK(Candidates(layer, mid.at, edges, read, c) == 0 && GameChoice(c, 0) == -1 && Choice(c, 0, true, 0, 0) == -1);
    const float nan = NAN;
    const float bad[4] = {nan, nan, nan, nan};
    m.Put(mid.at + NearCount, uint64_t(6));
    CHECK(Candidates(layer, mid.at, bad, read, c) == 0);
    // (ours) the layer's table unreadable: nothing found, so the game's own answer stands in the wiring
    m.Put(layer + TableData, uintptr_t(0));
    CHECK(Candidates(layer, mid.at, edges, read, c) == 0);
}

static void CeilingTopTest() {
    // Built rooms' border floors, per level.
    LevelFloors f;
    const float flat[4] = {0, 0, 0, 0}, stair[4] = {0, 0, -6.5f, 0}, nan4[4] = {NAN, NAN, NAN, NAN};
    float hi = 0;
    CHECK(!f.Highest(0xA, hi));
    f.Add(0xA, 0.03f, flat); f.Add(0xA, 6.53f, stair); f.Add(0xB, -2.96f, flat);
    CHECK(f.Highest(0xA, hi) && hi == 6.53f);
    CHECK(f.Highest(0xB, hi) && hi == -2.96f);
    f.Add(0xA, 3.0f, nan4); CHECK(f.Highest(0xA, hi) && hi == 6.53f);   // no edges: the height itself
    f.Add(0, 99, flat); CHECK(!f.Highest(0, hi));
    // Old levels drop out once more than Slots are in use; the most recent stay.
    for (uintptr_t l = 0x100; l < 0x100 + LevelFloors::Slots; ++l) f.Add(l, 1, flat);
    CHECK(!f.Highest(0xB, hi) && f.Highest(0x100 + LevelFloors::Slots - 1, hi));

    // The tomb: he enters at 0.03 with the level's rooms built up to 6.53: the ceiling's top is 6.53 at once.
    {
        Top t;
        CHECK(t.Step(0, 1, 0xA, 0.03f, true, 6.53f) == 6.53f);
        // up the stairs and down again: it never moves
        for (double s = 0.1; s < 5; s += 0.1) CHECK(t.Step(s, 1, 0xA, 0.03f + float(s), true, 6.53f) == std::max(6.53f, 0.03f + float(s)));
    }
    {
        Top t;
        t.Step(0, 1, 0xA, 0.03f, true, 6.53f);
        // a higher room built later (after the settle time): eased up, never down
        float v = t.Step(3, 1, 0xA, 0.03f, true, 13.0f);
        CHECK(v > 6.53f && v < 13.0f);
        for (double s = 3.1; s < 6; s += 0.05) v = t.Step(s, 1, 0xA, 0.03f, true, 13.0f);
        CHECK(v == 13.0f);
        CHECK(t.Step(6.1, 1, 0xA, 0.03f, true, 13.0f) == 13.0f);
        // within the settle time a new level is taken whole, higher or lower
        CHECK(t.Step(7, 2, 0xB, -2.96f, true, -2.96f) == -2.96f);
        CHECK(t.Step(7.5, 2, 0xB, -2.96f, true, 3.03f) == 3.03f);
        // the hero's level unknown: his own floor
        Top u;
        CHECK(u.Step(0, 1, 0, 1.5f, false, 0) == 1.5f);
        CHECK(u.Step(0.1, 1, 0xC, 1.5f, true, 4.0f) == 4.0f);   // the level now known: a fresh start, taken whole
        CHECK(std::isfinite(u.Step(0.2, 1, 0xC, NAN, true, 4.0f)));
    }
}

int main() {
    StitchTest();
    CeilingTopTest();
    std::printf("unleashed_room_stitch_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
