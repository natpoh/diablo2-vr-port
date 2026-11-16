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

// [gameplay] melee_touch_interact_attack: a command on A (himbrat, 0.218.3, a Warlock: A held 357 Interact - the touches ran Interact, 0 hit rolls)
void Commands() {
    Check(IsCommand(357) && IsCommand(358) && IsCommand(361) && IsCommand(364) && IsCommand(370), "Interact, Loot, Swap Weapons, Run, Cube Loot are commands");
    Check(!IsCommand(0) && !IsCommand(-1) && !IsCommand(356) && !IsCommand(365) && !IsCommand(373) && !IsCommand(398) && !IsCommand(36),
          "Attack, empty, skills (a Warlock's 373 .. 402 included) are not");
    // the player's case: Interact on A, the touch's press young, at the cast's read -> the plain Attack
    Check(AnswerTouch(357, 0, true, true, true, true) == kAttack, "Interact on A at the touch's press: the plain Attack");
    Check(AnswerTouch(358, 0, true, true, true, true) == kAttack, "Loot on A at the touch's press: the plain Attack");
    Check(AnswerTouch(370, 0, true, false, true, true) == kAttack, "Cube Loot on A, the empty-slot option off: still the plain Attack (its own key)");
    // the way back and every other case: as the game has it
    Check(AnswerTouch(357, 0, true, true, false, true) == 357, "option off: Interact pressed as before");
    Check(AnswerTouch(357, 0, true, true, true, false) == 357, "the player's own A (a pick-up), not the touch's: Interact");
    Check(AnswerTouch(357, 0, false, true, true, true) == 357, "the interface drawing the slot: Interact");
    Check(AnswerTouch(357, 5, true, true, true, true) == 357, "Interact on another slot: as the game has it");
    Check(AnswerTouch(398, 0, true, true, true, true) == 398, "a Warlock skill on A: still that skill");
    Check(AnswerTouch(0, 0, true, true, true, true) == 0, "Attack on A: Attack");
    Check(AnswerTouch(-1, 0, true, false, true, true) == -1, "an empty A with only the command option on: as before (nothing cast)");
    Check(AnswerTouch(-1, 0, true, true, true, true) == kAttack, "an empty A with both on: the plain Attack");
    Check(Answer(357, 0, true, true, true) == 357, "the old Answer (empty slot only) leaves a command alone");
    // Stand Still's view of the press
    Check(StillSkill(357, true, true) == kAttack, "the touch's press on Interact: held still as an attack");
    Check(StillSkill(357, false, true) == 357, "the player's own A on Interact: a pick-up, not held");
    Check(StillSkill(357, true, false) == 357, "option off: a pick-up, as before");
    Check(StillSkill(132, true, true) == 132, "a Leap on A: still Leap (it moves the hero)");
    Check(StillSkill(-1, true, true) == -1, "an empty A: as it was (Stand Still holds for it already)");
}

void TellingCommands() {
    Teller t;
    Check(t.Read(357, true), "Interact answered as Attack: told");
    for (int i = 0; i < 20; ++i) Check(!t.Read(357, true), "the next touches on Interact: not told again");
    Check(!t.Read(357, false), "the player's own pick-up on Interact: nothing told");
    Check(!t.Read(357, true), "... and the touch after it is still not told again");
    Check(t.Read(-1, true), "the slot emptied (another binding fell back): told");
    Check(t.Read(357, true), "Interact again: told");
    Check(!t.Read(398, false), "a skill on A: nothing told");
    Check(t.Read(357, true), "Interact after a skill: told again");
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
    Commands();
    Telling();
    TellingCommands();
    if (g_fail) {
        std::printf("touch_empty_slot_test: %d FAILED\n", g_fail);
        return 1;
    }
    std::printf("touch_empty_slot_test: all passed\n");
    return 0;
}
