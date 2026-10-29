// Offline checks of vr/interact_press.h ([input] interact_one_press): one target per press of "pick up / interact".
// Run: interact_press_test.exe (exit 0 = all passed).
#include <cstdio>

#include "interact_press.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

using interactpress::Press;

int main() {
    const uintptr_t chest = 0x1000, loot = 0x2000, gold = 0x3000;
    {   // a chest opened by a press does not also give up its loot
        Press p;
        CHECK(!p.Poll(false, chest, true, 1, 0).down);
        CHECK(p.Poll(true, chest, true, 1, 0).down);           // pressed on the chest: A down
        CHECK(p.Poll(true, chest, true, 1, 0).down);           // held, still the chest
        const auto r = p.Poll(true, loot, true, 1, 0);         // the chest opened, its loot is under the hand now
        CHECK(!r.down && r.lost);
        CHECK(!p.Poll(true, loot, true, 1, 0).down);           // held on: nothing
        CHECK(!p.Poll(false, loot, true, 1, 0).down);          // let go
        CHECK(p.Poll(true, loot, true, 1, 0).down);            // a new press takes the loot
    }
    {   // a hold over a pile takes one item
        Press p;
        CHECK(p.Poll(true, gold, true, 1, 0).down);
        CHECK(!p.Poll(true, loot, true, 1, 0).down);           // the gold taken, the next item under the hand
        CHECK(!p.Poll(true, chest, true, 1, 0).down);
    }
    {   // a press on nothing is spent: moving onto something while held does not take it
        Press p;
        const auto r = p.Poll(true, 0, true, 1, 0);
        CHECK(!r.down && r.miss);
        CHECK(!p.Poll(true, loot, true, 1, 0).down);
        CHECK(!p.Poll(false, loot, true, 1, 0).down);
        CHECK(p.Poll(true, loot, true, 1, 0).down);
    }
    {   // without the target hook (another game build): a plain A, as before
        Press p;
        const auto r = p.Poll(true, 0, false, 1, 0);
        CHECK(r.down && !r.miss);
        CHECK(p.Poll(true, 0, false, 1, 0).down);
        CHECK(!p.Poll(false, 0, false, 1, 0).down);
    }
    {   // a press held across a menu opening or closing counts for nothing until let go
        Press p;
        CHECK(p.Poll(true, chest, true, 1, 0).down);
        CHECK(!p.Poll(true, chest, true, 1, 1).down);          // a menu opened (epoch 1)
        CHECK(!p.Poll(true, chest, true, 1, 2).down);          // and closed again, the action still held
        CHECK(!p.Poll(false, chest, true, 1, 2).down);
        CHECK(p.Poll(true, chest, true, 1, 2).down);           // a fresh press works at once
    }
    {   // ... and across another game (another local player)
        Press p;
        CHECK(p.Poll(true, chest, true, 1, 0).down);
        CHECK(!p.Poll(true, chest, true, 2, 0).down);
        CHECK(!p.Poll(false, 0, true, 2, 0).down);
        CHECK(p.Poll(true, chest, true, 2, 0).down);
    }
    {   // a context change with the action not held: the next press is a normal one
        Press p;
        CHECK(!p.Poll(false, 0, true, 1, 0).down);
        CHECK(!p.Poll(false, 0, true, 1, 3).down);
        CHECK(p.Poll(true, chest, true, 1, 3).down);
    }
    std::printf("interact_press_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
