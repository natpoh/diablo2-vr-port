// unit_scan.h - the monsters near a point, read from the client's unit hash. Pure: the memory reads and the MonStats rows are
// handed in, so the walk is tested against a made-up game memory (cleanroom/tests/unit_scan_test.cpp).
//
// ONE walk for everything that looks at the monsters round the hero - hit by touch (melee_touch.cpp ScanUnits) and the shield bash
// (shield_smite.cpp) - so the two cannot see different worlds. The hash is the game's own: a type's row is table + type * 0x400,
// 128 chain heads, and its FindClientUnit (RVA 0x9A5D0 in 3.3.93787, read 2026-10-17) takes id & 0x7F as the head and follows
// unit + 0x158 from one unit to the next.
//
// Why the shield bash does not walk the hero's rooms any more (room + 0xA8, the units of a room, "next" at +0x158 as the cave
// ceiling's torches do): in the test-54 headset log (2026-10-17, a Paladin with Smite on slot 1) the room walk named a monster
// once, and in the 1062 lines of the debug line after it "no monster in the scan" stood while the melee boxes line, which reads
// this hash, listed a monster 0.9 m from the hero. The game's own lookup follows +0x158 along the HASH CHAIN, so from a room's
// first unit that link does not stay in the room; the hash is the walk that has always found what hit by touch hits.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <unordered_map>

#include "melee_bodies.h"

namespace unitscan {

constexpr size_t kUnitBytes = 0x1C0;        // the front of a unit that is read: its bank byte (+0x1BD) is the last field used
constexpr uintptr_t kRowBytes = 0x400;      // one type's row of the hash: 128 chain heads
constexpr unsigned kBuckets = 128, kChainMax = 64, kVisitMax = 2048;   // a chain or a row that runs on is cut here
constexpr size_t kOffType = 0, kOffClass = 4, kOffId = 8, kOffMode = 0xC, kOffPath = 0x38, kOffNext = 0x158, kOffBank = 0x1BD;
constexpr int kPlayer = 0, kMonster = 1;    // the hash's rows

struct Unit { uint8_t b[kUnitBytes]; };
inline uint32_t Field32(const Unit& u, size_t o) { uint32_t v; std::memcpy(&v, u.b + o, 4); return v; }
inline uintptr_t Field64(const Unit& u, size_t o) { uintptr_t v; std::memcpy(&v, u.b + o, 8); return v; }

// The units of one type in the hash, one after the other: calls fn(unit address, unit) until it says stop. read(dst, src, n) -> bool.
template <class Read, class Fn> void EachUnit(Read&& read, uintptr_t table, int type, Fn&& fn) {
    if (!table) return;
    uintptr_t heads[kBuckets];
    if (!read(heads, table + static_cast<uintptr_t>(type) * kRowBytes, sizeof heads)) return;
    unsigned visited = 0;
    for (const uintptr_t head : heads) {
        uintptr_t at = head;
        for (unsigned chain = 0; at && chain < kChainMax && visited < kVisitMax; ++chain, ++visited) {
            Unit u;
            if (!read(u.b, at, sizeof u.b)) break;
            if (Field32(u, kOffType) != static_cast<uint32_t>(type)) break;   // not a unit of this row: stop this chain
            if (!fn(at, u)) return;
            at = Field64(u, kOffNext);
        }
    }
}

// A unit's place on the grid in game WORLD units (twice the cell): the 16.16 coordinates of its path.
template <class Read> bool Place(Read&& read, const Unit& u, float* x, float* z) {
    uint16_t p[4];
    if (!read(p, Field64(u, kOffPath), sizeof p)) return false;
    *x = 2.0f * (p[1] + p[0] / 65536.0f);
    *z = 2.0f * (p[3] + p[2] / 65536.0f);
    return true;
}

struct Near {
    uintptr_t unit = 0;
    uint32_t id = 0, cls = 0;
    uint32_t mode = 0;          // its unit's mode now (MonMode: 1 neutral, 4 and 5 its attacks, ...; [weight] parry_*)
    float x = 0.0f, z = 0.0f;   // its place on the grid, game WORLD units
    float d2 = 0.0f;            // squared distance to the point asked from, game WORLD units
    stroke::Body body;          // its capsule, render metres (melee_bodies.h)
};

// The live monsters within `reach` game WORLD units of (px, pz), each with its capsule: the nearest `cap` when more are there.
// rows(bank, class, &MonsterRow) -> bool is the game's MonStats row (false: the row does not read, the monster is left out).
// floorM / upm: the ground height and the units per metre at the hero's size (melee_bodies.h BodyFor).
template <class Read, class Rows>
size_t Monsters(Read&& read, uintptr_t table, float px, float pz, float reach, float upm, float floorM, Rows&& rows, Near* out, size_t cap) {
    if (!table || !out || !cap || !std::isfinite(px + pz + reach) || reach < 0.0f) return 0;
    size_t n = 0;
    const float reach2 = reach * reach;
    EachUnit(read, table, kMonster, [&](uintptr_t at, const Unit& u) {
        const uint32_t mode = Field32(u, kOffMode);
        if (mode == 0 || mode == 12) return true;   // dying, dead
        float x, z;
        if (!Place(read, u, &x, &z)) return true;
        const float d2 = (x - px) * (x - px) + (z - pz) * (z - pz);
        if (d2 > reach2) return true;
        Near c;
        c.unit = at;
        c.id = Field32(u, kOffId);
        c.cls = Field32(u, kOffClass);
        c.mode = mode;
        c.x = x;
        c.z = z;
        c.d2 = d2;
        meleetouch::MonsterRow row;
        if (!rows(u.b[kOffBank], c.cls, &row)) return true;
        if (!meleetouch::BodyFor(row, x, z, floorM, upm, c.body)) return true;
        if (n < cap) {
            out[n++] = c;
            return true;
        }
        size_t farthest = 0;   // full: the farthest goes
        for (size_t i = 1; i < n; ++i) if (out[i].d2 > out[farthest].d2) farthest = i;
        if (c.d2 < out[farthest].d2) out[farthest] = c;
        return true;
    });
    return n;
}

// The MonStats rows, asked of the game once per class. A row that read stays for good; one that did not (the table was not ready, a
// call that faulted) is asked again after kRetryMs - never remembered as "no row" for the rest of the session, which would leave that
// kind of monster out of every scan until the game restarted. ask(bank, class, &MonsterRow) -> bool is the game's getter.
class RowCache {
public:
    static constexpr uint64_t kRetryMs = 5000;
    template <class Ask> bool Get(uint8_t bank, uint32_t cls, uint64_t nowMs, Ask&& ask, meleetouch::MonsterRow* out) {
        const uint32_t key = (static_cast<uint32_t>(bank) << 16) | cls;
        const auto it = map_.find(key);
        if (it != map_.end() && (it->second.ok || nowMs - it->second.at < kRetryMs)) {
            if (it->second.ok) *out = it->second.row;
            return it->second.ok;
        }
        Entry e;
        e.at = nowMs;
        e.ok = ask(bank, cls, &e.row);
        map_[key] = e;
        if (e.ok) *out = e.row;
        return e.ok;
    }
    size_t Size() const { return map_.size(); }

private:
    struct Entry { meleetouch::MonsterRow row; bool ok = false; uint64_t at = 0; };
    std::unordered_map<uint32_t, Entry> map_;
};

}  // namespace unitscan
