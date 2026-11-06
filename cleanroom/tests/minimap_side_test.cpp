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
    {   // [hud] map_rotate_flat: the overlay put in a corner while the turn is wanted, put back once it is not
        FlatLease l;
        Check(l.Step(false, 0, 0, 1000) == -1 && !l.Moved(), "flat: not wanted, overlay: nothing");
        Check(l.Step(true, 0, 0, 1000) == 2 && l.Moved(), "flat: wanted, overlay -> right");
        Check(l.Step(true, 0, 0, 1500) == -1, "flat: not again within a second");
        Check(l.Step(true, 2, 0, 1600) == -1, "flat: in the corner: nothing");
        Check(l.Step(false, 2, 0, 1700) == 0 && !l.Moved(), "flat: not wanted any more: back to the whole screen");
        Check(l.Step(false, 0, 0, 1800) == -1, "flat: back: nothing more");
    }
    {   // a corner map of the player's is never touched, and never put to the whole screen
        FlatLease l;
        Check(l.Step(true, 1, 0, 1000) == -1 && l.Step(false, 1, 0, 2000) == -1, "flat: the player's left corner kept, on and off");
        Check(l.Step(true, 2, 1, 3000) == -1 && l.Step(false, 2, 1, 4000) == -1, "flat: the player's right corner kept, on and off");
    }
    {   // the player moves the map while the lease holds it: theirs from then on
        FlatLease l;
        Check(l.Step(true, 0, 0, 1000) == 2, "flat: moved to the right");
        Check(l.Step(true, 1, 0, 1100) == -1 && !l.Moved(), "flat: the player put it left: not ours any more");
        Check(l.Step(false, 1, 0, 1200) == -1, "flat: off: the player's left corner stays");
    }
    {   // the map mode not read: nothing either way
        FlatLease l;
        Check(l.Step(true, -1, 0, 1000) == -1, "flat: unknown mode: nothing");
        Check(l.Step(true, 0, 1, 2000) == 1, "flat: map_corner left -> left");
        Check(l.Step(false, -1, 1, 3000) == -1 && l.Moved(), "flat: unknown while going off: wait");
        Check(l.Step(false, 1, 1, 3100) == 0, "flat: then back");
    }
    // [hud] flat_map_place: left / middle / right, whatever the turn
    Check(PlaceMode(0) == -1 && PlaceMode(4) == -1 && PlaceMode(-1) == -1, "flat_map_place 0 (or nonsense): the game's own");
    Check(PlaceMode(1) == 1 && PlaceMode(2) == 0 && PlaceMode(3) == 2, "flat_map_place: left 1, middle 0 (the whole screen), right 2");
    {
        PlaceKeeper k;
        Check(k.Step(0, 2, 1000) == 0, "the middle wanted, the map on the right: the whole screen");
        Check(k.Step(0, 2, 1500) == -1, "not again within a second");
        Check(k.Step(0, 0, 1600) == -1, "in the middle now: nothing");
        Check(k.Step(0, 1, 1700) == 0, "the options put it left: back at once");
        Check(k.Step(1, 0, 5000) == 1, "left wanted, the map in the middle: left");
        Check(k.Step(2, 1, 6100) == 2, "right wanted: right");
        Check(k.Step(2, -1, 7000) == -1, "the mode not read: nothing");
        Check(k.Step(-1, 0, 8000) == -1, "not wanted (the game's own, a menu, VR): nothing");
    }
    std::printf(g_fails ? "minimap_side_test: %d failed\n" : "minimap_side_test: all passed\n", g_fails);
    return g_fails ? 1 : 0;
}
