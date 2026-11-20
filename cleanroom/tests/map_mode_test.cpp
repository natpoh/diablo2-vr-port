// shared/map_mode.h: the map's five modes over the map keys - each mode's keys, the mode read back from them, what a mode leaves alone,
// Custom for keys no mode makes, the selector's step round the five.
#include "map_mode.h"

#include <cstdio>
#include <string_view>

namespace {
int g_fails = 0;
void Check(bool ok, const char* what) {
    if (!ok) { ++g_fails; std::printf("FAIL: %s\n", what); }
}
}   // namespace

int main() {
    using namespace mapmode;
    {   // the shipped ini (map=1, two sizes, the orb by default): the orb in the hand
        Keys k; k.map = 1;
        Check(FromKeys(k) == kOrb, "shipped keys: Orb in the hand");
        Keys none;   // a file without the keys: the plugin's defaults (map=0)
        Check(FromKeys(none) == kInPicture, "no keys at all: the map in the picture (map=0, the plugin's default)");
    }
    {   // each mode written, then read back
        for (int m = 0; m < kCount; ++m) {
            Keys k; k.map = 1;
            ToKeys(m, &k);
            Check(FromKeys(k) == m, "a mode written reads back as itself");
            for (int n = 0; n < kCount; ++n) {   // from every mode to every other
                Keys j = k;
                ToKeys(n, &j);
                Check(FromKeys(j) == n, "from any mode to any other: it reads back");
            }
        }
    }
    {   // the keys each mode writes
        Keys k; k.map = 1;
        ToKeys(kFlat, &k);
        Check(k.map == 1 && k.orb == 0 && k.twoSizes == 1 && k.handFull == 0, "Flat: map 1, orb 0, two sizes");
        ToKeys(kBigOnHand, &k);
        Check(k.map == 1 && k.clickBig == 1 && k.twoSizes == 0 && k.onHand == 1 && k.keep == 1 && k.handFull == 0,
              "Big map on the left hand: click 1, two sizes 0, on hand 1, keep 1");
        Check(k.orb == 0, "Big map on the left hand: the small map's look (flat) left as it was");
        ToKeys(kWholeOnHand, &k);
        Check(k.map == 1 && k.handFull == 1, "Whole map on the left hand: map 1, map_hand_full 1");
        ToKeys(kInPicture, &k);
        Check(k.map == 0 && k.handFull == 0, "In the picture: map 0, map_hand_full 0");
        ToKeys(kOrb, &k);
        Check(k.map == 1 && k.orb == 1 && k.twoSizes == 1 && k.handFull == 0, "Orb: map 1, orb 1, two sizes");
        Check(k.onHand == 1 && k.keep == 1, "Orb: map_big_on_hand / map_big_keep left as they were (two sizes overrides the hand)");
    }
    {   // what a mode leaves alone: the place (but a hand mode never leaves the map "not shown")
        Keys k; k.map = 1; k.place = 2;
        ToKeys(kFlat, &k);
        Check(k.place == 2, "a hand mode keeps the map in the right hand when it was there");
        Keys n; n.map = 1; n.place = 0;
        ToKeys(kOrb, &n);
        Check(n.place == 1, "a hand mode with the map not shown: on the left hand");
        Keys p; p.map = 1; p.place = 0;
        ToKeys(kInPicture, &p);
        Check(p.place == 0, "In the picture leaves map_place alone");
        Keys bad; bad.map = 1;
        const Keys was = bad;
        ToKeys(7, &bad); ToKeys(-1, &bad); ToKeys(kOrb, nullptr);
        Check(bad == was, "no mode / no keys: nothing written");
    }
    {   // keys no mode makes: Custom
        Keys k; k.map = 1; k.twoSizes = 0; k.onHand = 1; k.keep = 0;
        Check(FromKeys(k) == kCustom, "the old three steps without the keep: Custom");
        k.keep = 1; k.clickBig = 0;
        Check(FromKeys(k) == kCustom, "no laser click: Custom");
        k.handFull = 1;
        Check(FromKeys(k) == kWholeOnHand, "map_hand_full wins over the rest");
        k.map = 0;
        Check(FromKeys(k) == kInPicture, "map=0 wins over everything");
    }
    {   // the step: round the five, Custom to an end
        Check(Step(kOrb, 1) == kFlat && Step(kInPicture, 1) == kOrb && Step(kOrb, -1) == kInPicture, "round the five, both ways");
        Check(Step(kCustom, 1) == kOrb && Step(kCustom, -1) == kInPicture, "Custom: to the first / the last");
        Check(Step(kFlat, 0) == kBigOnHand, "a click (no direction): forward");
        Check(std::string_view(Name(kWholeOnHand)) == "Whole map on the left hand" && std::string_view(Name(kCustom)) == "Custom", "the names");
    }
    std::printf(g_fails ? "map_mode_test: %d failed\n" : "map_mode_test: all passed\n", g_fails);
    return g_fails ? 1 : 0;
}
