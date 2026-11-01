// vr/scan_reach.h: the bodies drawn away keep their units in the scans - the reach grown to the farthest one's place (a margin, a cap), and a skeleton
// at a kept place taken whatever the distance. The cases of Serg's 0.196.0 log, in game units (3.2 a metre).
#include <cmath>
#include <cstdio>

#include "../../vr/scan_reach.h"

static int g_fail = 0, g_checks = 0;
static void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++g_fail; }
}
static bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

int main() {
    using namespace scanreach;
    const float upm = 3.2f;
    const float base = 24.0f;   // the monsters' scan: 24 game units (7.5 m)

    Check(Near(Reach(base, 0, 0, nullptr, 0, upm), base), "nothing drawn away: the reach as ever");
    {   // a body held by the hero, its unit at his feet: the reach as ever
        const float at[1][2] = {{3.0f, 2.0f}};
        Check(Near(Reach(base, 0, 0, at, 1, upm), base), "a body near: as ever");
    }
    {   // Kashya, carried 13 cells (26 units) from where the game keeps her: the scan reaches her place and a margin past it
        const float at[1][2] = {{10014.0f + 26.0f, 12387.0f}};
        const float r = Reach(base, 10014.0f, 12387.0f, at, 1, upm);
        Check(Near(r, 26.0f + kMarginM * upm), "a body 26 units off: reached, the margin past it");
        Check(r > 26.0f, "... her unit inside it");
    }
    {   // the farthest of several counts; a place not a number is passed over
        const float at[3][2] = {{10.0f, 0.0f}, {0.0f, -40.0f}, {NAN, 5.0f}};
        Check(Near(Reach(base, 0, 0, at, 3, upm), 40.0f + kMarginM * upm), "the farthest of them");
    }
    {   // one 300 units off: held to the cap (that far it is let go of, as before)
        const float at[1][2] = {{300.0f, 0.0f}};
        Check(Near(Reach(base, 0, 0, at, 1, upm), kCapM * upm), "held to the cap");
    }
    Check(Near(Reach(base, NAN, 0, nullptr, 0, upm), base) && Near(Reach(base, 0, 0, nullptr, 0, 0.0f), base), "bad input: as ever");

    // -- a skeleton at a kept place: its unit moved a little since (walking, a knockback's step) still counts; another monster a few metres off does not
    {
        const float at[2][2] = {{100.0f, 200.0f}, {-50.0f, 10.0f}};
        Check(Kept(100.0f, 200.0f, at, 2, upm), "at the place: kept");
        Check(Kept(102.5f, 198.0f, at, 2, upm), "moved under a metre: kept");
        Check(!Kept(110.0f, 200.0f, at, 2, upm), "three metres off: not that body");
        Check(Kept(-50.5f, 9.5f, at, 2, upm), "the second place too");
        Check(!Kept(0, 0, nullptr, 0, upm) && !Kept(100.0f, 200.0f, at, 2, 0.0f), "nothing kept / no scale: none");
    }

    std::printf("scan_reach_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
