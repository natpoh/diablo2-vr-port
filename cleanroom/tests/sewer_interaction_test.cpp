// vr/sewer_interaction.h on a fake room: the Kurast sewer warp nearby is found while the head faces it, and nothing else is.
#include "sewer_interaction.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

static int g_fail = 0;
static void Expect(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; }
}

// A sparse fake memory: bytes at addresses.
struct Memory {
    std::map<uintptr_t, uint8_t> bytes;
    template <class T> void Put(uintptr_t at, const T& v) {
        uint8_t b[sizeof(T)];
        std::memcpy(b, &v, sizeof v);
        for (size_t i = 0; i < sizeof v; ++i) bytes[at + i] = b[i];
    }
    template <class T> bool Get(uintptr_t at, T& v) const {
        uint8_t b[sizeof(T)];
        for (size_t i = 0; i < sizeof v; ++i) {
            const auto it = bytes.find(at + i);
            if (it == bytes.end()) return false;
            b[i] = it->second;
        }
        std::memcpy(&v, b, sizeof v);
        return true;
    }
};

constexpr uintptr_t kPlayer = 0x100000, kPath = 0x110000, kRoom = 0x120000, kNear = 0x130000, kWarp = 0x140000, kStatic = 0x150000, kOther = 0x160000, kOtherStatic = 0x170000;

// The hero at (100, 100) cells, standing; one room; in it a decoy tile (class 12) then the warp at (`wx`, `wy`).
static Memory World(uint32_t wx, uint32_t wy, uint32_t warpClass = sewerinteraction::kSewerWarp) {
    Memory m;
    const uint32_t hero[4] = {0, 1, 7, 1};
    m.Put(kPlayer, hero);
    m.Put(kPlayer + 0x38, kPath);
    const uint32_t xy[2] = {100u << 16, 100u << 16};
    m.Put(kPath, xy);
    m.Put(kPath + 0x20, kRoom);
    m.Put(kRoom, kNear);
    m.Put(kRoom + 0x40, uint32_t(0));
    m.Put(kRoom + 0xA8, kOther);
    const uint32_t decoy[3] = {5, 12, 3};
    m.Put(kOther, decoy);
    m.Put(kOther + 0x160, kWarp);
    m.Put(kOther + 0x38, kOtherStatic);
    m.Put(kOtherStatic, kRoom);
    const uint32_t decoyAt[2] = {101, 100};
    m.Put(kOtherStatic + 0x10, decoyAt);
    const uint32_t warp[3] = {5, warpClass, 42};
    m.Put(kWarp, warp);
    m.Put(kWarp + 0x160, uintptr_t(0));
    m.Put(kWarp + 0x38, kStatic);
    m.Put(kStatic, kRoom);
    const uint32_t at[2] = {wx, wy};
    m.Put(kStatic + 0x10, at);
    return m;
}

static sewerinteraction::Result Run(const Memory& m, int level, float fx, float fy, bool inReach = true) {
    return sewerinteraction::Find(kPlayer, level, fx, fy, [&](uintptr_t at, auto& v) { return m.Get(at, v); },
                                  [&](uintptr_t, uintptr_t) { return inReach; });
}

int main() {
    const Memory ahead = World(104, 100);   // 4 cells along +x
    auto r = Run(ahead, 80, 1, 0);
    Expect(r.unit == kWarp && r.id == 42, "Bazaar, facing the stairs: the warp");
    Expect(r.tiles == 2 && r.firstTileClass == 12, "both tiles seen, the decoy first");
    Expect(Run(ahead, 81, 1, 0.5f).unit == kWarp, "Upper Kurast, 27 degrees off: the warp");
    Expect(Run(ahead, 80, -1, 0).unit == 0, "facing away: none");
    Expect(Run(ahead, 80, 0, 1).unit == 0, "facing 90 degrees off: none");
    Expect(Run(ahead, 75, 1, 0).unit == 0, "Kurast Docks: none");
    Expect(Run(ahead, 92, 1, 0).unit == 0, "in the sewers: none");
    Expect(Run(ahead, 80, 1, 0, false).unit == 0, "the game's reach/line test says no: none");
    Expect(Run(ahead, 80, 0, 0).unit == 0, "no heading: none");
    Expect(Run(World(100, 100), 80, -1, 0).unit == kWarp, "standing on it: any facing");
    Expect(Run(World(110, 100), 80, 1, 0).unit == 0, "10 cells away: none");
    Expect(Run(World(104, 100, 58), 80, 1, 0).unit == 0, "another warp class: none");
    {
        Memory busy = World(104, 100);
        const uint32_t casting[4] = {0, 1, 7, 10};
        busy.Put(kPlayer, casting);
        Expect(Run(busy, 80, 1, 0).unit == 0, "the hero in another mode (casting): none");
    }
    if (g_fail) return 1;
    std::printf("sewer_interaction_test: ok\n");
    return 0;
}
