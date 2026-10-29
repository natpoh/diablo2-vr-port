// Offline checks of vr/attack_view.h: in which views the aiming hand may direct the hero's attack.
// Run: build/Release/attack_view_test.exe (exit code 0 = passed)

#include <cstdio>

#include "../../vr/attack_view.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main() {
    using attackview::HandMayAim;
    // VR: F2 (third person) and F4 (first person) look where the player looks, as they did
    CHECK(HandMayAim(1, 2));
    CHECK(HandMayAim(1, 4));
    // VR: F1 (from above) and F3 (the game on the floor) are top-down: the hero hits where HE faces - nothing of the hand or the head
    CHECK(!HandMayAim(1, 1));
    CHECK(!HandMayAim(1, 3));
    // the flat game is left exactly as it was, whatever its view number is
    for (int v = 0; v <= 6; ++v) CHECK(HandMayAim(0, v));
    // a view number nobody has (a stale or unset value) is not a look view in VR: the game's own aim
    CHECK(!HandMayAim(1, 0));
    CHECK(!HandMayAim(1, 5));
    CHECK(!HandMayAim(1, -1));
    int allowed = 0;
    for (int v = 1; v <= 4; ++v) if (HandMayAim(1, v)) ++allowed;
    CHECK(allowed == 2);
    if (g_failures) { std::printf("%d check(s) FAILED\n", g_failures); return 1; }
    std::printf("attack_view_test: all %d checks passed\n", g_checks);
    return 0;
}
