// [hands] double_swing_by_hands (vr/double_swing.h): the second hand's touch on the monster the first hand just struck is the dual skill - the timing, the same monster, the other hand, the skill on a pad slot,
// the level, the mana. Pure maths on made-up clocks.
// Run: build/Release/double_swing_test.exe   (exit code 0 = all passed)

#include "../../vr/double_swing.h"

#include <cmath>
#include <cstdio>
#include <cstdint>

using namespace doubleswing;

static int g_failures = 0;
static void Expect(bool ok, const char* what, double value = 0.0) {
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s (%g)\n", what, value);
    }
}

// The hands: 0 right, 1 left.
constexpr int R = 0, L = 1;
constexpr uint32_t kMon = 4711, kOther = 4712;

// A hero with Double Swing on pad slot 2 at level 5 and 20 mana: the usual case.
static Facts Usual() {
    Facts f;
    f.skill[0] = {2, 5};
    f.mana = 20.0f;
    return f;
}

static void CostTests() {
    // the rows of Skills.txt: Double Swing mana 8, lvlmana -1, shift 5, min 0; Frenzy mana 3, lvlmana 0, shift 7, min 1
    Expect(std::fabs(CostPoints(0, 1) - 1.0f) < 1e-6f, "Double Swing at level 1: 1.0 mana", CostPoints(0, 1));
    Expect(std::fabs(CostPoints(0, 2) - 0.875f) < 1e-6f, "Double Swing at level 2: 0.875", CostPoints(0, 2));
    Expect(std::fabs(CostPoints(0, 8) - 0.125f) < 1e-6f, "Double Swing at level 8: 0.125", CostPoints(0, 8));
    Expect(CostPoints(0, 9) == 0.0f && CostPoints(0, 20) == 0.0f, "Double Swing from level 9: nothing (the row's minimum is 0)");
    Expect(std::fabs(CostPoints(1, 1) - 1.5f) < 1e-6f && std::fabs(CostPoints(1, 20) - 1.5f) < 1e-6f, "Frenzy: 1.5 mana at every level", CostPoints(1, 1));
    Expect(CostPoints(0, 0) < 0.0f && CostPoints(1, -3) < 0.0f && CostPoints(2, 5) < 0.0f && CostPoints(-1, 5) < 0.0f, "no level, no skill: no cost");
    // short: the arithmetic against the mana, in the game's 1/256 points
    Expect(Short(0, 1, 0.99f) && !Short(0, 1, 1.0f) && !Short(0, 1, 5.0f), "Double Swing level 1 needs 1.0");
    Expect(!Short(0, 9, 0.0f), "Double Swing from level 9 is never short (not even with no mana)");
    Expect(Short(1, 3, 1.49f) && !Short(1, 3, 1.5f), "Frenzy needs 1.5");
    Expect(!Short(0, 1, -1.0f) && !Short(0, 1, NAN) && !Short(0, 0, 0.0f), "mana not read / level not known: not judged (the game decides)");
    Expect(ClampWindowMs(700.0) == 700.0 && ClampWindowMs(10.0) == kMinWindowMs && ClampWindowMs(99999.0) == kMaxWindowMs && ClampWindowMs(NAN) == kDefaultWindowMs, "the window is clamped to 200 .. 2000 ms");
}

static void PairTests() {
    const double W = 700.0;
    {   // the first hand's touch is a normal attack; the other hand within the window on the same monster is the dual skill
        Tracker t;
        Verdict v = t.Strike(R, kMon, 1000, true, W, Usual());
        Expect(!v.dual && v.why == Why::First, "the first hand: a normal attack");
        v = t.Strike(L, kMon, 1400, true, W, Usual());
        Expect(v.dual && v.why == Why::Dual && v.skill == 0 && v.slot == 2 && v.level == 5 && !v.levelGuessed, "the other hand, 400 ms later, the same monster: Double Swing on slot 2");
        Expect(std::fabs(v.gapMs - 400.0) < 1e-9 && std::fabs(v.cost - CostPoints(0, 5)) < 1e-6f && v.mana == 20.0f, "the verdict says the gap, the cost and the mana", v.gapMs);
    }
    {   // the window's edge: at the window is in, a millisecond past is out
        Tracker t;
        t.Strike(R, kMon, 1000, true, W, Usual());
        Expect(t.Strike(L, kMon, 1700, true, W, Usual()).dual, "exactly the window later: still a pair");
        Tracker u;
        u.Strike(R, kMon, 1000, true, W, Usual());
        const Verdict late = u.Strike(L, kMon, 1701, true, W, Usual());
        Expect(!late.dual && late.why == Why::TooLate && std::fabs(late.gapMs - 701.0) < 1e-9, "a millisecond past the window: a normal attack, too late", late.gapMs);
        // ... and that late strike is the first of a new pair: the right hand within the window of IT is a dual
        const Verdict again = u.Strike(R, kMon, 2000, true, W, Usual());
        Expect(again.dual, "the late strike starts a new pair: the other hand within the window of it is a dual skill");
    }
    {   // the same hand never pairs with itself
        Tracker t;
        t.Strike(R, kMon, 1000, true, W, Usual());
        const Verdict v = t.Strike(R, kMon, 1200, true, W, Usual());
        Expect(!v.dual && v.why == Why::First, "the same hand twice: a normal attack, not a pair", (double)(int)v.why);
        Expect(t.Strike(L, kMon, 1500, true, W, Usual()).dual, "... and the other hand, within the window of the SECOND strike, is the pair");
    }
    {   // another monster: not a pair; the strike is a first, and the pair then forms on that monster
        Tracker t;
        t.Strike(R, kMon, 1000, true, W, Usual());
        Verdict v = t.Strike(L, kOther, 1200, true, W, Usual());
        Expect(!v.dual && v.why == Why::OtherMonster, "the other hand on another monster: a normal attack");
        v = t.Strike(R, kOther, 1500, true, W, Usual());
        Expect(v.dual, "the right hand on the monster the left hand struck: a pair there");
    }
    {   // a spent pair: the next strike starts again, alternate swings give normal, dual, normal, dual
        Tracker t;
        Expect(!t.Strike(R, kMon, 1000, true, W, Usual()).dual, "swing 1 (right): normal");
        Expect(t.Strike(L, kMon, 1300, true, W, Usual()).dual, "swing 2 (left): dual");
        Verdict v = t.Strike(R, kMon, 1500, true, W, Usual());
        Expect(!v.dual && v.why == Why::First, "swing 3 (right, 200 ms after the dual): a normal attack - the dual skill spent the pair");
        Expect(t.Strike(L, kMon, 1800, true, W, Usual()).dual, "swing 4 (left): dual again");
    }
    {   // the hands the other way round
        Tracker t;
        t.Strike(L, kMon, 1000, true, W, Usual());
        Expect(t.Strike(R, kMon, 1200, true, W, Usual()).dual, "left first, right second: the same");
    }
    {   // not two weapons: a normal attack, nothing remembered
        Tracker t;
        t.Strike(R, kMon, 1000, true, W, Usual());
        Verdict v = t.Strike(L, kMon, 1100, false, W, Usual());
        Expect(!v.dual && v.why == Why::NotDual, "not a weapon in each hand: normal");
        v = t.Strike(R, kMon, 1200, true, W, Usual());
        Expect(!v.dual && v.why == Why::First, "(the memory went with the weapon): a first");
    }
    {   // Reset forgets
        Tracker t;
        t.Strike(R, kMon, 1000, true, W, Usual());
        t.Reset();
        Expect(!t.Strike(L, kMon, 1100, true, W, Usual()).dual, "after a Reset the first hand is forgotten");
    }
    {   // a clock that goes back: no time passed
        Tracker t;
        t.Strike(R, kMon, 5000, true, W, Usual());
        const Verdict v = t.Strike(L, kMon, 4000, true, W, Usual());
        Expect(v.dual && v.gapMs == 0.0, "a clock that went back counts as gap 0");
    }
    {   // two blades that touched one monster in the same pass: the first is noted, the second is a pair at gap 0
        Tracker t;
        t.Record(R, kMon, 3000);
        const Verdict v = t.Strike(L, kMon, 3000, true, W, Usual());
        Expect(v.dual && v.gapMs == 0.0, "two blades in one pass: the second is the dual skill");
    }
}

static void SkillTests() {
    const double W = 700.0;
    const auto second = [&](const Facts& f) {
        Tracker t;
        t.Strike(R, kMon, 1000, true, W, f);
        return t.Strike(L, kMon, 1300, true, W, f);
    };
    {   // no skill on any slot
        Facts f = Usual();
        f.skill[0] = {};
        Verdict v = second(f);
        Expect(!v.dual && v.why == Why::NoSlot, "neither skill on a pad slot: a normal attack");
    }
    {   // on a slot but no level (not learned)
        Facts f = Usual();
        f.skill[0].level = 0;
        const Verdict v = second(f);
        Expect(!v.dual && v.why == Why::NotLearned, "a skill on a slot the hero has no level in: a normal attack");
    }
    {   // the level not read: the first level's cost stands in, the verdict says so
        Facts f = Usual();
        f.skill[0].level = -1;
        const Verdict v = second(f);
        Expect(v.dual && v.levelGuessed && v.level == 1 && std::fabs(v.cost - 1.0f) < 1e-6f, "the level not read: Double Swing is pressed, worked out at level 1");
        f.mana = 0.5f;
        const Verdict s = second(f);
        Expect(!s.dual && s.why == Why::NoMana && s.levelGuessed, "... and with 0.5 mana the guess says it is short");
    }
    {   // mana
        Facts f = Usual();
        f.skill[0].level = 1;   // 1.0 mana
        f.mana = 0.9f;
        Verdict v = second(f);
        Expect(!v.dual && v.why == Why::NoMana && v.skill == 0 && std::fabs(v.cost - 1.0f) < 1e-6f && v.mana == 0.9f, "0.9 mana for a 1.0 cost: a normal attack, no mana");
        f.mana = 1.0f;
        Expect(second(f).dual, "exactly the cost: pressed");
        f.skill[0].level = 9;   // free from level 9
        f.mana = 0.0f;
        Expect(second(f).dual, "level 9: free, pressed with no mana at all");
        f.skill[0].level = 1;
        f.mana = -1.0f;
        v = second(f);
        Expect(v.dual && v.mana < 0.0f, "the mana not read: pressed - the game decides (AttackNoMana turns it into the plain attack)");
    }
    {   // Frenzy
        Facts f = Usual();
        f.skill[0] = {};
        f.skill[1] = {5, 3};
        Verdict v = second(f);
        Expect(v.dual && v.skill == 1 && v.slot == 5 && std::fabs(v.cost - 1.5f) < 1e-6f, "only Frenzy on a slot: Frenzy");
        f.mana = 1.4f;
        Expect(!second(f).dual && second(f).why == Why::NoMana, "Frenzy with 1.4 mana: no mana");
        // both on slots: the player's order
        Facts both = Usual();
        both.skill[1] = {6, 3};
        v = second(both);
        Expect(v.dual && v.skill == 0 && v.slot == 2, "both on slots: Double Swing first");
        both.prefer = 1;
        v = second(both);
        Expect(v.dual && v.skill == 1 && v.slot == 6, "Frenzy preferred: Frenzy first");
        // Double Swing too dear (level 1, 1.0) while Frenzy is dearer still (1.5): both short
        both.prefer = 0;
        both.skill[0].level = 1;
        both.mana = 1.2f;
        v = second(both);
        Expect(v.dual && v.skill == 0, "1.2 mana: Double Swing (1.0) is affordable, Frenzy (1.5) is not");
        both.mana = 0.4f;
        v = second(both);
        Expect(!v.dual && v.why == Why::NoMana, "0.4 mana: neither");
        // the preferred one is short but the other is not: the other
        Facts mix = Usual();
        mix.skill[0].level = 1;   // 1.0
        mix.skill[1] = {6, 3};     // 1.5
        mix.prefer = 1;            // Frenzy first
        mix.mana = 1.2f;           // Frenzy short, Double Swing not
        v = second(mix);
        Expect(v.dual && v.skill == 0, "Frenzy preferred but short: Double Swing is pressed instead");
    }
    {   // the attack button already carries one of them: nothing to add
        Facts f = Usual();
        f.attackSkill = kDoubleSwing;
        Verdict v = second(f);
        Expect(!v.dual && v.why == Why::AlreadyOnAttack, "Double Swing on the attack button: every touch casts it already");
        f.attackSkill = kFrenzy;
        v = second(f);
        Expect(!v.dual && v.why == Why::AlreadyOnAttack && v.skill == 1, "Frenzy on the attack button: the same");
        f.attackSkill = 0;   // the plain attack
        Expect(second(f).dual, "the plain attack on the button: the second hand's touch is the dual skill");
    }
    {   // a first strike is judged by nothing: no skill, no mana needed for a normal attack
        Tracker t;
        Facts none;
        const Verdict v = t.Strike(R, kMon, 100, true, W, none);
        Expect(!v.dual && v.why == Why::First, "the first hand needs no skill and no mana");
    }
}

int main() {
    CostTests();
    PairTests();
    SkillTests();
    if (g_failures == 0) std::printf("double_swing_test: all passed\n");
    return g_failures == 0 ? 0 : 1;
}
