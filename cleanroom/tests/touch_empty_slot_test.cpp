// touch_empty_slot_test - [gameplay] melee_touch_empty_attack (vr/touch_empty_slot.h): which read of the attack slot the hook answers with the plain Attack,
// and that the log line comes once per change of the slot, not per hit. No game, no windows.h.
#include "../../vr/touch_empty_slot.h"

#include <cstdio>

namespace {
int g_fail = 0;
void Check(bool ok, const char* what) {
    if (!ok) {
        ++g_fail;
        std::printf("FAIL: %s\n", what);
    }
}

using namespace touchemptyslot;

void Answers() {
    // the player's case (rewhymo, 0.215.0): A empty (-1), the touch's press young, at the cast's read -> the plain Attack
    Check(Answer(-1, 0, true, true, true) == kAttack, "an empty A at the touch's press: the plain Attack");
    Check(Answer(-2, 0, true, true, true) == kAttack, "any negative binding (nothing usable) counts as empty");
    // the way back and every other case: as the game has it
    Check(Answer(-1, 0, true, false, true) == -1, "option off: as before (nothing cast)");
    Check(Answer(-1, 0, true, true, false) == -1, "not the touch's press (the player's own A, the window over, not F4): as the game has it");
    Check(Answer(-1, 0, false, true, true) == -1, "another reader of the slot (the interface drawing it): as the game has it");
    Check(Answer(-1, 5, true, true, true) == -1, "another slot (RT, a skill button): as the game has it");
    Check(Answer(-1, -1, true, true, true) == -1, "the setter's own slot index (-1): as the game has it");
    Check(Answer(0, 0, true, true, true) == 0, "Attack on A: Attack");
    Check(Answer(36, 0, true, true, true) == 36, "a skill on A (a spell): still that skill");
    Check(Answer(221, 0, true, true, true) == 221, "a Druid form skill on A: still that skill");
}

void Telling() {
    Teller t;
    Check(!t.Read(-1, false), "an empty slot not answered (option off): nothing told");
    Check(t.Read(-1, true), "the first fallback: told");
    for (int i = 0; i < 20; ++i) Check(!t.Read(-1, true), "the next hits on the same empty slot: not told again");
    Check(!t.Read(36, false), "the slot holds a skill: nothing told");
    Check(t.Read(-1, true), "empty again after holding a skill: told again");
    Check(!t.Read(-1, true), "... once");
    Check(!t.Read(0, false), "Attack on A: nothing told");
    Check(t.Read(-1, true), "empty again after Attack: told again");
}
}  // namespace

int main() {
    Answers();
    Telling();
    if (g_fail) {
        std::printf("touch_empty_slot_test: %d FAILED\n", g_fail);
        return 1;
    }
    std::printf("touch_empty_slot_test: all passed\n");
    return 0;
}
