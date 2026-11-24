// dome_backing_test - vr/sky/dome_backing.h ([sky] dome_atlas_tiled): how the dome's baked texture is backed.
//   1. the key off (the default): committed and whole, on every card - tier 2 or not; the log says so and names the key;
//   2. the key on: reserved only where the card has tiled resources tier 2; without it committed, and the log says why;
//   3. the reserved one failing: committed, the log says it could not be made;
//   4. ours kept only for the same size and the same key: a key change remakes it (at the game's next view of its picture).
#include "sky/dome_backing.h"

#include <cstdio>
#include <cstring>
#include <initializer_list>

static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

static bool Has(const char* s, const char* part) { return s && std::strstr(s, part) != nullptr; }

int main() {
    using namespace domebacking;
    {   // 1. the default
        for (bool tier2 : {false, true}) {
            const Choice c = Choose(false, tier2);
            CHECK(!c.reserved);
            CHECK(Has(c.why, "all of it backed") && Has(c.why, "dome_atlas_tiled=0") && Has(c.why, "no tile mappings"));
        }
    }
    {   // 2. the key on
        const Choice on = Choose(true, true);
        CHECK(on.reserved);
        CHECK(Has(on.why, "reserved") && Has(on.why, "tier 2") && Has(on.why, "dome_atlas_tiled=1"));
        const Choice no = Choose(true, false);
        CHECK(!no.reserved);
        CHECK(Has(no.why, "all of it backed") && Has(no.why, "no tiled resources tier 2"));
    }
    {   // 3. the reserved one failing
        const Choice f = Fallback();
        CHECK(!f.reserved && Has(f.why, "all of it backed") && Has(f.why, "could not be made"));
    }
    {   // 4. kept or made again
        CHECK(!Keep(false, true, false, false));   // nothing made yet
        CHECK(Keep(true, true, false, false) && Keep(true, true, true, true));
        CHECK(!Keep(true, false, false, false));   // another size
        CHECK(!Keep(true, true, false, true) && !Keep(true, true, true, false));   // the key changed
    }
    std::printf("dome_backing_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
