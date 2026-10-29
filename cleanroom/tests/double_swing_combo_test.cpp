// double_swing_combo_test - [hands] double_swing_combo (vr/double_swing.h Tracker, combo true): the first touch on a monster is the normal attack, every following touch on it (either hand, within the
// window of the last one) the dual skill; another monster or a longer pause starts a new combo; a touch that cannot be the dual skill keeps the combo going. And the pairs (combo false) as they were.
// No game, no windows.h.
#include "../../vr/double_swing.h"

#include <cstdio>

namespace {
int g_fail = 0;
void Check(bool ok, const char* what) {
    if (!ok) {
        ++g_fail;
        std::printf("FAIL: %s\n", what);
    }
}

using namespace doubleswing;

Facts Ready() {
    Facts f;
    f.skill[0].slot = 4;   // Double Swing on LT + B
    f.skill[0].level = 5;
    f.attackSkill = 0;
    f.mana = 50.0f;
    return f;
}

void Combo() {
    Tracker t;
    const Facts f = Ready();
    Verdict v = t.Strike(0, 7, 1000, true, 700, f, true);
    Check(!v.dual && v.why == Why::First, "the first blow: a normal attack");
    v = t.Strike(1, 7, 1400, true, 700, f, true);
    Check(v.dual && v.skill == 0 && v.slot == 4, "the next (other hand): Double Swing");
    v = t.Strike(0, 7, 1900, true, 700, f, true);
    Check(v.dual, "the next again: Double Swing - the combo is not spent");
    v = t.Strike(0, 7, 2400, true, 700, f, true);
    Check(v.dual, "the same hand twice: still the combo (either hand)");
    v = t.Strike(1, 7, 3000, true, 700, f, true);
    Check(v.dual, "600 ms after the last (2.0 s after the first): the window runs from the last blow");
    v = t.Strike(1, 7, 3800, true, 700, f, true);
    Check(!v.dual && v.why == Why::TooLate, "a pause over the window: a new combo, a normal attack");
    v = t.Strike(0, 7, 4000, true, 700, f, true);
    Check(v.dual, "... and the next is the dual skill again");
    v = t.Strike(0, 9, 4200, true, 700, f, true);
    Check(!v.dual && v.why == Why::OtherMonster, "another monster: its first blow is a normal attack");
    v = t.Strike(1, 9, 4400, true, 700, f, true);
    Check(v.dual, "... then its combo");

    Facts poor = f;
    poor.mana = 0.4f;   // Double Swing at level 5 costs (8 - 4) << 5 = 0.5
    v = t.Strike(0, 9, 4600, true, 700, poor, true);
    Check(!v.dual && v.why == Why::NoMana, "no mana: a normal attack ...");
    v = t.Strike(1, 9, 4800, true, 700, f, true);
    Check(v.dual, "... and the combo goes on when the mana is there");

    v = t.Strike(1, 9, 4900, false, 700, f, true);
    Check(!v.dual && v.why == Why::NotDual, "not a weapon in each hand: nothing, the combo forgotten");
    v = t.Strike(1, 9, 5000, true, 700, f, true);
    Check(!v.dual && v.why == Why::First, "... the next is a first again");

    t.Reset();
    t.Record(0, 11, 6000);   // two blades on one monster in one pass: the first noted ...
    v = t.Strike(1, 11, 6000, true, 700, f, true);
    Check(v.dual, "... the second's strike is a following blow (gap 0)");
}

void Pairs() {
    Tracker t;
    const Facts f = Ready();
    Verdict v = t.Strike(0, 7, 1000, true, 700, f);
    Check(!v.dual && v.why == Why::First, "pairs: the first hand - a normal attack");
    v = t.Strike(1, 7, 1400, true, 700, f);
    Check(v.dual, "pairs: the other hand - Double Swing");
    v = t.Strike(0, 7, 1800, true, 700, f);
    Check(!v.dual && v.why == Why::First, "pairs: the pair is spent - the next is a first (as before)");
    v = t.Strike(0, 7, 2000, true, 700, f);
    Check(!v.dual, "pairs: the same hand again is not a pair");
    v = t.Strike(1, 7, 2200, true, 700, f);
    Check(v.dual, "pairs: the other hand - Double Swing");
}

}  // namespace

int main() {
    Combo();
    Pairs();
    if (g_fail) {
        std::printf("double_swing_combo_test: %d FAILED\n", g_fail);
        return 1;
    }
    std::printf("double_swing_combo_test: all passed\n");
    return 0;
}
