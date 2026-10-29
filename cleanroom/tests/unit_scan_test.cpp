// Synthetic checks of vr/unit_scan.h: the monsters round the hero, read from a made-up copy of the client's unit hash. No game, no headset.
// What it guards: the shield bash once saw NO monster (a walk of the hero's room lists) while hit by touch, reading the unit hash, listed
// one 0.9 m away (test-54 headset log). Both now go through unitscan::Monsters; the checks below are the world they must agree on - a
// monster at the far end of a long chain, a row that fails once and reads later, a full list, dead and far units.
// Build: the `unit_scan_test` target; the exit code is 0 when everything passes.
#include "../../vr/unit_scan.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using unitscan::Near;

namespace {
void Check(bool ok, const char* why) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", why);
        std::exit(1);
    }
}
bool Near1(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

// A game memory: a flat block at a fake address, the hash table at its start, the units and their paths after it.
struct Game {
    static constexpr uintptr_t kBase = 0x20000000;
    static constexpr uintptr_t kTable = kBase + 0x1000;     // 6 rows of 128 heads
    static constexpr uintptr_t kUnits = kBase + 0x10000;    // one 0x200 block per unit
    static constexpr uintptr_t kPaths = kBase + 0x100000;   // one 0x10 block per unit
    std::vector<uint8_t> mem = std::vector<uint8_t>(0x120000, 0);
    int count = 0;
    bool broken = false;   // every read fails

    bool operator()(void* dst, uintptr_t src, size_t n) const {
        if (broken || src < kBase || src + n > kBase + mem.size()) return false;
        std::memcpy(dst, &mem[src - kBase], n);
        return true;
    }
    template <class T> void Put(uintptr_t at, T v) { std::memcpy(&mem[at - kBase], &v, sizeof v); }
    template <class T> T Get(uintptr_t at) const { T v; std::memcpy(&v, &mem[at - kBase], sizeof v); return v; }

    // A unit of `type` at cell (cx, cz), put at the TAIL of the chain its id hashes to (id & 0x7F), as the game's own would be found.
    uintptr_t Add(uint32_t type, uint32_t id, uint32_t cls, uint32_t mode, float cx, float cz, uint8_t bank = 0) {
        const uintptr_t u = kUnits + uintptr_t(count) * 0x200, path = kPaths + uintptr_t(count) * 0x10;
        ++count;
        Put<uint32_t>(u + unitscan::kOffType, type);
        Put<uint32_t>(u + unitscan::kOffClass, cls);
        Put<uint32_t>(u + unitscan::kOffId, id);
        Put<uint32_t>(u + unitscan::kOffMode, mode);
        Put<uint64_t>(u + unitscan::kOffPath, path);
        Put<uint8_t>(u + unitscan::kOffBank, bank);
        Put<uint64_t>(u + unitscan::kOffNext, 0);
        const float fx = cx - std::floor(cx), fz = cz - std::floor(cz);   // 16.16: fraction (u16) then whole cells (u16)
        Put<uint16_t>(path + 0, uint16_t(fx * 65536.0f));
        Put<uint16_t>(path + 2, uint16_t(std::floor(cx)));
        Put<uint16_t>(path + 4, uint16_t(fz * 65536.0f));
        Put<uint16_t>(path + 6, uint16_t(std::floor(cz)));
        const uintptr_t head = kTable + uintptr_t(type) * unitscan::kRowBytes + uintptr_t(id & 0x7F) * 8;
        uintptr_t at = Get<uint64_t>(head);
        if (!at) {
            Put<uint64_t>(head, u);
        } else {
            while (Get<uint64_t>(at + unitscan::kOffNext)) at = Get<uint64_t>(at + unitscan::kOffNext);
            Put<uint64_t>(at + unitscan::kOffNext, u);
        }
        return u;
    }
};

constexpr float kUpm = 3.7f;      // game world units per metre at the hero's size
constexpr float kFloor = 0.0f;

// The MonStats rows of the made-up game: class 5 and 63 read; 99 does not (yet).
struct Rows {
    bool ready99 = false;
    int asked = 0;
    bool operator()(uint8_t, uint32_t cls, meleetouch::MonsterRow* row) {
        ++asked;
        if (cls == 99 && !ready99) return false;
        *row = {2, 2, 72};
        return true;
    }
};

size_t Scan(const Game& g, float heroCellX, float heroCellZ, float reachUnits, Rows& rows, Near* out, size_t cap) {
    return unitscan::Monsters(g, Game::kTable, heroCellX * 2.0f, heroCellZ * 2.0f, reachUnits, kUpm, kFloor, rows, out, cap);
}
const Near* Find(const Near* a, size_t n, uint32_t id) {
    for (size_t i = 0; i < n; ++i) if (a[i].id == id) return &a[i];
    return nullptr;
}

void OneMonster() {
    Game g;
    g.Add(unitscan::kMonster, 26, 5, 1, 102.5f, 200.0f);
    Rows rows;
    Near out[8];
    const size_t n = Scan(g, 100.0f, 200.0f, 3.0f * kUpm, rows, out, 8);
    Check(n == 1, "one monster 2.5 cells away is found");
    Check(out[0].id == 26 && out[0].cls == 5, "its id and class are read");
    Check(Near1(out[0].x, 205.0f) && Near1(out[0].z, 400.0f), "its place is the path's 16.16 coordinates, twice the cell");
    Check(Near1(out[0].d2, 25.0f), "its squared distance is in game world units");
    Check(out[0].unit == Game::kUnits, "the unit's address is handed back (the cast looks it up again by id)");
    Check(out[0].body.radius > 0.1f && out[0].body.top.y > out[0].body.bottom.y - 1e-4f, "a capsule is built from its row");
}

void AtTheEndOfALongChain() {
    // The failure the headset log showed: the monster is not the first unit of anything. 40 monsters of the level far off share the hash
    // with it - ids 1, 129, 257, ... are one chain - and it is the LAST of that chain.
    Game g;
    for (uint32_t k = 0; k < 40; ++k) g.Add(unitscan::kMonster, 1 + 128 * k, 63, 1, 500.0f + float(k), 900.0f);
    g.Add(unitscan::kMonster, 1 + 128 * 40, 5, 1, 101.0f, 200.0f);   // 41st in the chain, 1 cell from the hero
    Rows rows;
    Near out[8];
    // chains are cut at kChainMax (64): 41 is inside, and the walk reaches it
    const size_t n = Scan(g, 100.0f, 200.0f, 3.0f * kUpm, rows, out, 8);
    Check(n == 1 && out[0].id == 1 + 128 * 40, "the monster at the far end of a chain of 41 is found");
}

void WhatIsLeftOut() {
    Game g;
    g.Add(unitscan::kMonster, 1, 5, 1, 101.0f, 200.0f);      // alive, near: counted
    g.Add(unitscan::kMonster, 2, 5, 0, 101.0f, 201.0f);      // dying
    g.Add(unitscan::kMonster, 3, 5, 12, 101.0f, 199.0f);     // dead
    g.Add(unitscan::kMonster, 4, 5, 1, 160.0f, 200.0f);      // alive, 60 cells away
    Rows rows;
    Near out[8];
    const size_t n = Scan(g, 100.0f, 200.0f, 3.0f * kUpm, rows, out, 8);
    Check(n == 1 && out[0].id == 1, "only the live monster in reach is listed (not dying, not dead, not far)");
}

void OtherRowsAreNotMonsters() {
    Game g;
    g.Add(unitscan::kPlayer, 7, 1, 1, 100.5f, 200.0f);       // the hero himself: the player row
    g.Add(2, 8, 5, 1, 100.5f, 200.5f);                        // an object
    g.Add(3, 9, 5, 1, 100.5f, 199.5f);                        // a missile
    Rows rows;
    Near out[8];
    Check(Scan(g, 100.0f, 200.0f, 3.0f * kUpm, rows, out, 8) == 0, "the player, an object and a missile are not monsters");
}

void AFailedRowIsAskedAgain() {
    Game g;
    g.Add(unitscan::kMonster, 40, 99, 1, 101.0f, 200.0f);   // class 99: the game's table does not give its row yet
    g.Add(unitscan::kMonster, 41, 5, 1, 102.0f, 200.0f);
    Rows rows;
    unitscan::RowCache cache;
    auto ask = [&](uint8_t bank, uint32_t cls, meleetouch::MonsterRow* row) { return rows(bank, cls, row); };
    uint64_t now = 1000;
    auto rowsAt = [&](uint8_t bank, uint32_t cls, meleetouch::MonsterRow* row) { return cache.Get(bank, cls, now, ask, row); };
    Near out[8];
    size_t n = unitscan::Monsters(g, Game::kTable, 200.0f, 400.0f, 3.0f * kUpm, kUpm, kFloor, rowsAt, out, 8);
    Check(n == 1 && out[0].id == 41, "a monster whose row does not read is left out for now, the others are not");
    const int askedBefore = rows.asked;
    now += 4000;
    n = unitscan::Monsters(g, Game::kTable, 200.0f, 400.0f, 3.0f * kUpm, kUpm, kFloor, rowsAt, out, 8);
    Check(n == 1 && rows.asked == askedBefore, "a failed row is not asked again within 5 s");
    rows.ready99 = true;
    now += 1500;   // 5.5 s after the failure
    n = unitscan::Monsters(g, Game::kTable, 200.0f, 400.0f, 3.0f * kUpm, kUpm, kFloor, rowsAt, out, 8);
    Check(n == 2 && Find(out, n, 40), "after 5 s the row is asked again, and now the monster is in the list (a failure is not for the whole session)");
    const int askedAfter = rows.asked;
    now += 600000;
    n = unitscan::Monsters(g, Game::kTable, 200.0f, 400.0f, 3.0f * kUpm, kUpm, kFloor, rowsAt, out, 8);
    Check(n == 2 && rows.asked == askedAfter, "a row that read is never asked again");
}

void TheNearestAreKept() {
    Game g;
    for (uint32_t k = 0; k < 10; ++k) g.Add(unitscan::kMonster, 100 + k, 5, 1, 110.0f - float(k), 200.0f);   // the nearest is added LAST (id 109, 1 cell)
    Rows rows;
    Near out[4];
    const size_t n = Scan(g, 100.0f, 200.0f, 40.0f, rows, out, 4);
    Check(n == 4, "a full list stays full");
    for (uint32_t id = 106; id <= 109; ++id) Check(Find(out, n, id) != nullptr, "the four nearest of ten are kept, whatever the order they were met in");
}

void BrokenMemory() {
    Game g;
    g.Add(unitscan::kMonster, 1, 5, 1, 101.0f, 200.0f);
    Rows rows;
    Near out[4];
    g.broken = true;
    Check(Scan(g, 100.0f, 200.0f, 11.0f, rows, out, 4) == 0, "unreadable memory is no monsters, not a crash");
    g.broken = false;
    Check(unitscan::Monsters(g, 0, 200.0f, 400.0f, 11.0f, kUpm, kFloor, rows, out, 4) == 0, "no table address is no monsters");
    Check(unitscan::Monsters(g, Game::kTable, 200.0f, 400.0f, 11.0f, kUpm, kFloor, rows, out, 0) == 0, "a capacity of 0 is no monsters");
    Check(unitscan::Monsters(g, Game::kTable, NAN, 400.0f, 11.0f, kUpm, kFloor, rows, out, 4) == 0, "a point that is not a number is no monsters");
}

void ALoopingChainEnds() {
    Game g;
    const uintptr_t u = g.Add(unitscan::kMonster, 1, 5, 1, 101.0f, 200.0f);
    g.Put<uint64_t>(u + unitscan::kOffNext, u);   // a corrupted chain: the unit points at itself
    Rows rows;
    Near out[100];
    const size_t n = Scan(g, 100.0f, 200.0f, 11.0f, rows, out, 100);
    Check(n <= unitscan::kChainMax, "a chain that loops back on itself is cut, not walked for ever");
}

void ShieldAndTouchSeeOneWorld() {
    // The regression itself. The world: hero at cell (100, 200); monsters at 2 cells (a shield-bash target, in the shield's 3 m reach),
    // 4 cells (in touch's 24 world units, out of the shield's reach) and 30 cells (out of both), all in long shared chains.
    Game g;
    for (uint32_t k = 0; k < 30; ++k) g.Add(unitscan::kMonster, 3 + 128 * k, 63, 1, 600.0f, 900.0f + float(k));   // filler of the same chain (bucket 3)
    g.Add(unitscan::kMonster, 3 + 128 * 30, 5, 1, 102.0f, 200.0f);     // 2 cells = 4 world units
    g.Add(unitscan::kMonster, 4, 5, 1, 106.0f, 200.0f);                // 6 cells = 12 world units
    g.Add(unitscan::kMonster, 5, 5, 1, 130.0f, 200.0f);                // 30 cells = 60 world units
    Rows rows;
    Near touch[48], shield[48];
    const size_t nt = Scan(g, 100.0f, 200.0f, 24.0f, rows, touch, 48);           // melee_touch.cpp ScanUnits: 24 game units
    const size_t ns = Scan(g, 100.0f, 200.0f, 3.0f * kUpm, rows, shield, 48);    // shield_smite.cpp Gather: 3 m
    Check(nt == 2, "hit by touch sees the monsters within 24 game units");
    Check(ns == 1 && shield[0].id == 3 + 128 * 30, "the shield sees the monster in its 3 m, at the far end of its chain");
    for (size_t i = 0; i < ns; ++i) {
        const Near* t = Find(touch, nt, shield[i].id);
        Check(t != nullptr, "every monster the shield sees, hit by touch sees too");
        Check(t->cls == shield[i].cls && t->unit == shield[i].unit && Near1(t->d2, shield[i].d2) && Near1(t->body.radius, shield[i].body.radius) &&
                  Near1(t->body.bottom.x, shield[i].body.bottom.x) && Near1(t->body.top.y, shield[i].body.top.y),
              "and it is the same monster with the same capsule");
    }
}
}  // namespace

int main() {
    OneMonster();
    AtTheEndOfALongChain();
    WhatIsLeftOut();
    OtherRowsAreNotMonsters();
    AFailedRowIsAskedAgain();
    TheNearestAreKept();
    BrokenMemory();
    ALoopingChainEnds();
    ShieldAndTouchSeeOneWorld();
    std::puts("unit_scan_test: all checks passed");
    return 0;
}
