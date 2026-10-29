// The weapon-neutral part of the quiver (vr/quiver_input.h) on synthetic numbers, without the game and without a headset: the controls' names. (The reach behind a shoulder that
// was tested here is gone with the feature.)
//
//   quiver_input_test

#include "../../vr/quiver_input.h"

#include <cmath>
#include <cstdio>

using namespace quiverin;

namespace {

int g_fails = 0;
#define EXPECT(c, ...)                                         \
    do {                                                       \
        if (!(c)) {                                            \
            ++g_fails;                                         \
            std::printf("  FAIL line %d: %s - ", __LINE__, #c); \
            std::printf(__VA_ARGS__);                          \
            std::printf("\n");                                 \
        }                                                      \
    } while (0)

void TestControls() {
    std::puts("the controls' names");
    Control c;
    EXPECT(ParseControl("right_b", &c) && c.Set() && c.rel == -1 && c.hand == 1 && c.mask == 2u, "right_b");
    EXPECT(ParseControl("LEFT_X", &c) && c.hand == 0 && c.mask == 1u, "case does not matter");
    EXPECT(ParseControl("  left_stick", &c) && c.hand == 0 && c.mask == 4u, "leading blanks");
    EXPECT(ParseControl("right_menu", &c) && c.hand == 1 && c.mask == 8u, "right_menu");
    EXPECT(ParseControl("string_secondary", &c) && c.Set() && c.rel == 1 && c.mask == 2u, "string_secondary");
    EXPECT(c.Hand(0) == 1 && c.Hand(1) == 0, "the string hand is the other one than the weapon's");
    EXPECT(ParseControl("bow_primary", &c) && c.rel == 0 && c.mask == 1u && c.Hand(0) == 0 && c.Hand(1) == 1, "bow_primary follows the weapon");
    EXPECT(ParseControl("right_a", &c) && c.Hand(0) == 1 && c.Hand(1) == 1, "a fixed hand stays");
    EXPECT(ParseControl("none", &c) && !c.Set(), "none: off");
    EXPECT(ParseControl("", &c) && !c.Set() && ParseControl(nullptr, &c) && !c.Set(), "empty: off");
    EXPECT(!ParseControl("left_trigger", &c) && !c.Set(), "a trigger is no button here");
    EXPECT(!ParseControl("right_bb", &c) && !ParseControl("right_", &c) && !ParseControl("rights", &c), "no half names");
    EXPECT(!ParseControl("right_b", nullptr), "no output");
    // every name: the hand it means and the Button bit (direct_input.h: primary 1, secondary 2, stick 4, menu 8)
    struct Row { const char* name; int rel, hand; uint32_t mask; };
    const Row rows[] = {{"left_x", -1, 0, 1u}, {"left_y", -1, 0, 2u}, {"left_stick", -1, 0, 4u}, {"left_menu", -1, 0, 8u},
                        {"right_a", -1, 1, 1u}, {"right_b", -1, 1, 2u}, {"right_stick", -1, 1, 4u}, {"right_menu", -1, 1, 8u},
                        {"string_primary", 1, -1, 1u}, {"string_secondary", 1, -1, 2u}, {"string_stick", 1, -1, 4u},
                        {"bow_primary", 0, -1, 1u}, {"bow_secondary", 0, -1, 2u}, {"bow_stick", 0, -1, 4u}};
    for (const Row& w : rows) {
        EXPECT(ParseControl(w.name, &c) && c.rel == w.rel && c.hand == w.hand && c.mask == w.mask && c.Set(), "%s", w.name);
        if (w.rel < 0) EXPECT(c.Hand(0) == w.hand && c.Hand(1) == w.hand, "%s: a fixed hand", w.name);
        else EXPECT(c.Hand(0) == (w.rel == 0 ? 0 : 1) && c.Hand(1) == (w.rel == 0 ? 1 : 0), "%s: follows the weapon", w.name);
    }
}

}  // namespace

int main() {
    TestControls();
    std::printf(g_fails ? "\n%d FAILED\n" : "\nall passed\n", g_fails);
    return g_fails ? 1 : 0;
}
