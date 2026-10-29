// The corner map for the hand kept in a corner (vr/minimap_side.h): the setter's globals from its own bytes, the mode from them,
// and when the setter is asked for - never for a map already in a corner.
#include "minimap_side.h"

#include <cstdio>

namespace {
int g_fails = 0;
void Check(bool ok, const char* what) {
    if (!ok) { ++g_fails; std::printf("FAIL: %s\n", what); }
}
}   // namespace

int main() {
    using namespace minimapside;
    // The globals as bearstonem/d2r-pcvr reads them on 3.3.93787 (0x2a2add0 mini, 0x2a2add5 left), here from the setter's operands.
    const Globals g = MapGlobals(kSetterRva, kSetterSig);
    Check(g.mini == 0x2a2add0, "mini: the mov's target");
    Check(g.left == 0x2a2add5, "left: the sete's target");
    Check(sizeof kSetterSig == 35, "the setter's checked bytes");

    Check(Mode(0, 0) == 0 && Mode(0, 1) == 0, "not mini: the whole screen (the middle)");
    Check(Mode(1, 1) == 1, "mini, left");
    Check(Mode(1, 0) == 2, "mini, right");
    Check(Mode(2, 0) == -1 && Mode(-1, 0) == -1 && Mode(1, 7) == -1, "nonsense reads as unknown");

    Check(CornerFor(0) == 2 && CornerFor(2) == 2 && CornerFor(1) == 1, "map_corner: either/right -> right, left -> left");

    {   // a map on the right (or left) is never touched, whatever else
        Keeper k;
        for (uint64_t t = 1; t < 10000; t += 100) {
            Check(k.Step(true, 2, 0, t) == -1, "right stays right");
            Check(k.Step(true, 1, 0, t) == -1, "left stays left");
            Check(k.Step(true, -1, 0, t) == -1, "unknown is left alone");
        }
    }
    {   // the middle: put in the corner once, again only after a second
        Keeper k;
        Check(k.Step(true, 0, 0, 1000) == 2, "middle -> right");
        Check(k.Step(true, 0, 0, 1500) == -1, "not again within a second");
        Check(k.Step(true, 0, 0, 2000) == 2, "again after a second");
        Check(k.Step(true, 2, 0, 2100) == -1, "now on the right: nothing");
        Check(k.Step(true, 0, 0, 2200) == 2, "back in the middle (the options): at once");
        Check(k.Step(true, 0, 1, 5000) == 1, "map_corner left -> left");
    }
    {   // not wanted (flat, [hud] map=0, no hero, a menu): nothing
        Keeper k;
        Check(k.Step(false, 0, 0, 1000) == -1, "not wanted: nothing");
        Check(k.Step(true, 0, 0, 1001) == 2, "wanted again: at once");
    }
    std::printf(g_fails ? "minimap_side_test: %d failed\n" : "minimap_side_test: all passed\n", g_fails);
    return g_fails ? 1 : 0;
}
