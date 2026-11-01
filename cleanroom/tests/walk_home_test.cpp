// vr/walk_home.h: a body the knock-down library draws walking home goes back to the game's own animation once it is home and the game has it act, or
// once it has stood home a while - the cases of Serg's 0.196.1 log (Rec 0104: "26 walking home 5.2 - 11.2 s, 0.03 - 0.09 m from home, weight 1.00").
#include <cmath>
#include <cstdio>

#include "../../vr/walk_home.h"

static int g_fail = 0, g_checks = 0;
static void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++g_fail; }
}

int main() {
    using namespace walkhome;
    // the log's: up again, 5 - 9 cm from where the game has it - released at once (it was held 6 s more)
    Check(BackNow(kWalking, 0.05f) && BackNow(kWalking, 0.09f), "up again, the game has it where it is drawn: released");
    // the game has it elsewhere (its knockback short or refused): not released - it would walk over; the unit is brought under it, or it walks home
    Check(!BackNow(kWalking, 1.58f) && !BackNow(kWalking, 0.77f) && !BackNow(kWalking, kHomeM), "the game has it 0.3 - 1.6 m off: not yet");
    // not up yet: lying, getting up, held, back - not this rule's
    Check(!BackNow(2, 0.05f) && !BackNow(3, 0.0f) && !BackNow(5, 0.0f) && !BackNow(4, 0.0f), "lying, getting up, held, back: not this rule's");
    Check(!BackNow(kWalking, NAN), "no distance: nothing");

    std::printf("walk_home_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
