// held_units_test - vr/engine/held_units.h: a blz::shared_ptr let go as the game does (uses, then the object, weaks, then the block), an
// instance's entity list brought down to the registry's live entities, and the leave / rejoin bookkeeping (no placing from the world's
// teardown until a menu, then the next game places again).
// Run: build/Release/held_units_test.exe (exit code 0 = passed)

#include <cstdio>
#include <cstring>
#include <vector>

#include "../vr/engine/held_units.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

// A control block as the game lays it out: {vtable, uses, weaks}; its vtable's +0x10 and +0x18 record their calls
struct FakeCtrl { void** vtbl; long uses; long weaks; };
static int g_destroyed = 0, g_freed = 0;
static void* g_lastCtrl = nullptr;
static void __fastcall Slot0(void*) {}
static void __fastcall Slot1(void*) {}
static void __fastcall Destroy(void* c) { ++g_destroyed; g_lastCtrl = c; }
static void __fastcall Free(void* c) { ++g_freed; g_lastCtrl = c; }
static void* g_vtbl[4] = {(void*)&Slot0, (void*)&Slot1, (void*)&Destroy, (void*)&Free};

int main() {
    using namespace heldunits;
    static_assert(offsetof(FakeCtrl, uses) == 8 && offsetof(FakeCtrl, weaks) == 0xC, "the game's control block");

    // ---- letting go of one reference
    {
        CHECK(!ReleaseCtrl(nullptr));   // an empty shared_ptr: nothing
        FakeCtrl c{g_vtbl, 2, 1};
        g_destroyed = g_freed = 0;
        CHECK(ReleaseCtrl(&c));
        CHECK(c.uses == 1 && c.weaks == 1 && g_destroyed == 0 && g_freed == 0);   // another holder keeps it: nothing destroyed
        CHECK(ReleaseCtrl(&c));
        CHECK(c.uses == 0 && g_destroyed == 1 && g_lastCtrl == &c);               // the last use: the object
        CHECK(c.weaks == 0 && g_freed == 1);                                      // and with no weak one left, the block
    }
    {
        FakeCtrl c{g_vtbl, 1, 2};   // a weak reference elsewhere (the definition's own): the object goes, the block stays
        g_destroyed = g_freed = 0;
        CHECK(ReleaseCtrl(&c));
        CHECK(g_destroyed == 1 && g_freed == 0 && c.weaks == 1);
    }

    // ---- an instance's entities: only the registry's own are left for the game's removal
    {
        // the registry's entities by index: index 3 holds version 1 of it now (0x00100003), index 5 is free (its next version)
        std::vector<uint32_t> reg(8);
        for (uint32_t i = 0; i < reg.size(); ++i) reg[i] = i;
        reg[3] = 0x00100003u;
        reg[5] = 0x00200005u;
        CHECK(Live(reg.data(), reg.size(), 2));
        CHECK(!Live(reg.data(), reg.size(), 3));            // the slot was reused: version 0 is gone
        CHECK(Live(reg.data(), reg.size(), 0x00100003u));
        CHECK(!Live(reg.data(), reg.size(), 5));
        CHECK(!Live(reg.data(), reg.size(), 9));             // past the registry
        CHECK(!Live(nullptr, 0, 0));
        uint32_t list[5] = {2, 3, 4, 5, 6};                 // an instance placed before an area change took 3 and 5
        const size_t k = KeepLive(list, 5, [&](uint32_t e) { return Live(reg.data(), reg.size(), e); });
        CHECK(k == 3 && list[0] == 2 && list[1] == 4 && list[2] == 6);
        uint32_t gone[2] = {3, 5};
        CHECK(KeepLive(gone, 2, [&](uint32_t e) { return Live(reg.data(), reg.size(), e); }) == 0);   // all taken: the game removes nothing
        uint32_t whole[3] = {0, 1, 2};
        CHECK(KeepLive(whole, 3, [&](uint32_t e) { return Live(reg.data(), reg.size(), e); }) == 3);  // untouched: all removed by the game
    }

    // ---- leave and rejoin: from the teardown no placing until a menu has been seen, then the next game places
    {
        Life life;
        CHECK(life.MayPlace(true) && !life.MayPlace(false));
        life.Teardown();                       // the client leaves: our units let go
        CHECK(!life.MayPlace(true));           // the frames the game still draws while it leaves: nothing placed again
        life.World(true);                      // still the old world's name: still nothing
        CHECK(!life.MayPlace(true));
        life.World(false);                     // the menu
        CHECK(!life.MayPlace(false));
        life.World(true);                      // the next game
        CHECK(life.MayPlace(true));
        life.Teardown(); life.Teardown();      // twice (a teardown with nothing held): the same
        CHECK(!life.MayPlace(true));
        life.World(false);
        CHECK(life.MayPlace(true));            // quit from the menu after it: nothing in the way
    }

    // ---- the log line
    {
        Tally t;
        t.instances = 6; t.stale = 2; t.definitions = 3;
        const std::string s = Line("game left", t);
        CHECK(s.find("game left: 6 placed instances removed (2 with entities an area change had taken), 3 definitions let go") != std::string::npos);
        CHECK(s.find("KEPT") == std::string::npos);
        t = {}; t.instances = 1; t.definitions = 1; t.kept = 4;
        const std::string k = Line("game left", t);
        CHECK(k.find("1 placed instance removed") != std::string::npos && k.find("1 definition let go") != std::string::npos);
        CHECK(k.find("KEPT") != std::string::npos && k.find("(4 kept)") != std::string::npos);
    }

    if (g_failures) { std::printf("%d of %d check(s) FAILED\n", g_failures, g_checks); return 1; }
    std::printf("held_units_test: all %d checks passed\n", g_checks);
    return 0;
}
