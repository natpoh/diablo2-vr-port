// Synthetic checks of vr/client_verdict.h: hit by touch's verdict from the client's own units, for a TCP/IP client whose game
// server runs on the host. What it guards: a pet (the hireling, a summon) is never hostile, nothing in town is, a wall stops the
// press, and a layout that is not understood says nothing instead of guessing. No game, no headset.
// Build: the `client_verdict_test` target; the exit code is 0 when everything passes.
#include "../../vr/client_verdict.h"

#include <cstdio>
#include <cstdlib>
#include <initializer_list>

using namespace clientverdict;

namespace {
void Check(bool ok, const char* why) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", why);
        std::exit(1);
    }
}

const uint32_t kPlayers[] = {1, 7};   // the hero (1) and another player in the game (7)

Facts Wild() {
    Facts f;
    f.unitRead = true;
    f.mode = 1;
    f.owner = kNoOwner;
    f.heroLevel = 2;   // Blood Moor
    f.line = 0;
    return f;
}
}  // namespace

int main() {
    // The read in the game (2026-10-22): a zombie, the hero's Rogue, another player's summon.
    Verdict v = Decide(Wild(), kPlayers, 2);
    Check(v.known && v.hostile && v.clear, "a wild monster in the open is hostile and clear");

    Facts f = Wild();
    f.owner = 1;
    v = Decide(f, kPlayers, 2);
    Check(v.known && !v.hostile && !v.clear, "the hero's own hireling is never hostile");
    f.owner = 7;
    v = Decide(f, kPlayers, 2);
    Check(v.known && !v.hostile, "another player's pet is not hostile either");

    // Towns: nobody there is hostile, the NPCs included.
    for (int town : {1, 40, 75, 103, 109}) {
        f = Wild();
        f.heroLevel = town;
        v = Decide(f, kPlayers, 2);
        Check(v.known && !v.hostile && !v.clear, "nothing in town is hostile");
    }
    Check(!IsTown(2) && !IsTown(39) && !IsTown(108) && !IsTown(0), "only the five towns are towns");

    // Walls: the game's line test.
    f = Wild();
    f.line = 1;
    v = Decide(f, kPlayers, 2);
    Check(v.known && v.hostile && !v.clear, "a wall between keeps the touch from pressing");
    f.line = 0x804;
    Check(!Decide(f, kPlayers, 2).clear, "any non-zero line answer is a wall");
    f.line = -1;
    v = Decide(f, kPlayers, 2);
    Check(v.known && v.hostile && v.clear, "a line test that did not answer leaves an arm's-length touch free");

    // Nothing known -> no answer (the touch presses nothing, as with no server before).
    f = Wild();
    f.unitRead = false;
    Check(!Decide(f, kPlayers, 2).known, "an unread unit gets no answer");
    f = Wild();
    f.mode = 0;
    Check(!Decide(f, kPlayers, 2).known, "a dying monster gets no answer");
    f.mode = 12;
    Check(!Decide(f, kPlayers, 2).known, "a dead monster gets no answer");
    f = Wild();
    f.heroLevel = 0;
    Check(!Decide(f, kPlayers, 2).known, "with the hero's area unknown there is no answer");

    // The owner field read as something else (a game update moved it): no guess.
    f = Wild();
    f.owner = 0x12345678;
    v = Decide(f, kPlayers, 2);
    Check(!v.known && !v.hostile, "an owner that is no player the client knows: not understood, no answer");
    Check(ClassifyOwner(kNoOwner, kPlayers, 2) == Owner::None, "0xFFFFFFFF is no owner");
    Check(ClassifyOwner(7, kPlayers, 2) == Owner::Player, "a known player id is an owner");
    Check(ClassifyOwner(3, kPlayers, 2) == Owner::NotUnderstood, "an unknown id is not understood");
    Check(ClassifyOwner(1, nullptr, 0) == Owner::NotUnderstood, "with no players known no owner is understood");

    std::printf("client_verdict_test: all checks passed\n");
    return 0;
}
