// melee_arm_test - [gameplay] melee_skill_armed (vr/melee_arm.h): which skills are armed, the press that arms and disarms, the pad buttons the arming takes from the game, the hit's skill or the
// plain attack by the game's own cost arithmetic, and the press in flight. No game, no windows.h.
#include "../../vr/melee_arm.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>

namespace {
int g_fail = 0;
void Check(bool ok, const char* what) {
    if (!ok) {
        ++g_fail;
        std::printf("FAIL: %s\n", what);
    }
}
bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

using namespace meleearm;

void Skills() {
    // the list: every class's melee attacks, ticked by default; the movement attacks in it unticked; never the bow's or a spell
    const Allowed def = Allowed::Defaults();
    for (int id : {126, 133, 139, 144, 147, 152, 96, 97, 106, 111, 116, 10, 14, 19, 24, 30, 34, 73, 232, 233, 238, 239, 242, 248, 254, 255, 259, 260, 265, 269, 270, 274, 280, 391})
        Check(IsMelee(id) && def.Has(id) && !IsMover(id), "a melee attack skill: in the list, ticked by default");
    for (int id : {107, 143, 151, 275})
        Check(IsMelee(id) && !def.Has(id) && IsMover(id), "Charge, Leap Attack, Whirlwind, Dragon Flight: in the list, unticked by default, movers");
    for (int id : {0, 1, 2, 392, 131, 142, 150, 132, 54, 6, 7, 15, 36, 49, 112, 155, 149, -1, 9999})
        Check(!IsMelee(id) && !def.Has(id), "Attack, Kick, Throw, Mirrored Blades, the corpse skills, Leap, Teleport, bow / javelin skills and spells are not");
    Check(def.Count() == 34, "34 ticked by default");
    bool unique = true;
    for (size_t i = 0; i < sizeof kRows / sizeof kRows[0]; ++i)
        for (size_t j = i + 1; j < sizeof kRows / sizeof kRows[0]; ++j)
            if (kRows[i].row.id == kRows[j].row.id) unique = false;
    Check(unique, "each skill once");
    Check(kCount == 38, "38 skills in the list");
    for (const Skill& k : kRows) Check(k.cls && k.cls[0] && k.name && k.name[0], "each has a name and a class (Settings groups by it)");
    // the cost, as the game shows it: Bash 2, Zeal 2, Frenzy 3, Berserk 4, Vengeance 4 at level 1 and 7.75 at 16, Jab 2 / 2.25, Double Swing 1 at 1 and none from 9, Sacrifice none, Fury 4, Tiger Strike 1
    auto cost = [](int id, int level) { return (float)bowmana::RowCostRaw(&Find(id)->row, level) / 256.0f; };
    Check(Near(cost(126, 1), 2.0f) && Near(cost(126, 20), 2.0f), "Bash: 2");
    Check(Near(cost(106, 1), 2.0f), "Zeal: 2");
    Check(Near(cost(147, 5), 1.5f), "Frenzy: 1.5 (3 << 7)");
    Check(Near(cost(152, 1), 4.0f), "Berserk: 4");
    Check(Near(cost(111, 1), 4.0f) && Near(cost(111, 16), 7.75f), "Vengeance: 4 at level 1, 7.75 at 16");
    Check(Near(cost(10, 1), 2.0f) && Near(cost(10, 2), 2.25f), "Jab: 2, then +0.25 a level");
    Check(Near(cost(133, 1), 1.0f) && Near(cost(133, 9), 0.0f) && Near(cost(133, 30), 0.0f), "Double Swing: 1 at level 1, none from level 9");
    Check(Near(cost(96, 1), 0.0f), "Sacrifice: no mana");
    Check(Near(cost(248, 1), 4.0f), "Fury: 4");
    Check(Near(cost(254, 1), 1.0f), "Tiger Strike: 1");
    Check(Name(106)[0] == 'Z' && Name(0)[0] == 'A', "names");
    // the movers' rows (Skills.txt): Whirlwind (25 + lvl) << 7 - 12.5 at level 1, 15 at 6; Leap Attack 10, Charge 9, Dragon Flight 15
    Check(Near(cost(151, 1), 12.5f) && Near(cost(151, 6), 15.0f), "Whirlwind: 12.5 at level 1, +0.5 a level");
    Check(Near(cost(143, 1), 10.0f) && Near(cost(107, 1), 9.0f) && Near(cost(275, 1), 15.0f), "Leap Attack 10, Charge 9, Dragon Flight 15");
}

void Press() {
    Arm a;
    Check(a.Press(106) == Outcome::Armed && a.Armed() == 106, "Zeal's button arms Zeal");
    Check(a.Press(106) == Outcome::Disarmed && a.Armed() == -1, "the same button again disarms");
    Check(a.Press(126) == Outcome::Armed, "Bash arms");
    Check(a.Press(139) == Outcome::Switched && a.Armed() == 139, "Stun's button arms Stun instead");
    Check(a.Press(36) == Outcome::Pass && a.Armed() == 139, "a spell's button is the game's; the armed skill stays");
    Check(a.Press(-1) == Outcome::Pass && a.Armed() == 139, "an empty slot is the game's");
    Check(a.Press(kAttack) == Outcome::Disarmed && a.Armed() == -1, "the plain attack's button disarms");
    Check(a.Press(kAttack) == Outcome::Pass, "... and with nothing armed it is the game's");
    Check(a.Press(151) == Outcome::Pass, "Whirlwind, unticked by default, stays a cast");
    a.Press(106);
    a.Clear();
    Check(a.Armed() == -1, "cleared");
}

void List() {
    Allowed al = Allowed::Defaults();
    Check(al.Set(151, 1) && al.Has(151), "Whirlwind ticked ([melee_armed] s151=1)");
    Check(al.Set(106, 0) && !al.Has(106), "Zeal unticked (s106=0)");
    Check(!al.Set(49, 1) && !al.Has(49), "a skill not in the list (Lightning) cannot be ticked");
    Check(!al.Set(-5, 1) && !al.Set(100000, 1), "nonsense ids are ignored");
    Check(Allowed::FromBits(al.Bits()) == al, "the bits round-trip");
    Check(Allowed::FromBits(~0ull).Count() == kCount, "bits past the list are dropped");

    Arm a;
    a.SetAllowed(al);
    Check(a.Press(151) == Outcome::Armed && a.Armed() == 151, "ticked Whirlwind: its button arms it");
    Check(a.Press(106) == Outcome::Pass && a.Armed() == 151, "unticked Zeal: its button casts as ever, Whirlwind stays armed");
    Check(a.Press(126) == Outcome::Switched && a.Armed() == 126, "Bash (ticked) re-arms");
    Allowed noBash = al;
    noBash.Set(126, 0);
    Check(a.SetAllowed(noBash) && a.Armed() == -1, "Bash unticked while armed: disarmed");
    Check(!a.SetAllowed(noBash) && a.Armed() == -1, "... once");
    a.Press(151);
    Check(!a.SetAllowed(noBash) && a.Armed() == 151, "a ticked armed skill stays through a new list");
    Check(a.SetAllowed(Allowed::Defaults()) && a.Armed() == -1, "back to the defaults: Whirlwind unticked, disarmed");

    // the pad: an unticked skill's button is the game's
    PadFilter f;
    int slots[8] = {151, 106, -1, kAttack, -1, -1, -1, -1};
    auto skillOf = [&](int s) { return s >= 0 && s < 8 ? slots[s] : -1; };
    a.SetAllowed(Allowed::Defaults());
    Step s = f.Poll(kPadB, 0, true, a, skillOf);
    Check(!s.event && s.strip == 0, "Whirlwind unticked: B goes to the game");
    f.Poll(0, 0, true, a, skillOf);
    a.SetAllowed(al);
    s = f.Poll(kPadB, 0, true, a, skillOf);
    Check(s.event && s.strip == kPadB && a.Armed() == 151, "Whirlwind ticked: B arms it, taken from the game");
    // the hit: the game's arithmetic on Whirlwind's row
    Hit h = Decide(151, 1, 20.0f);
    Check(h.skill == 151 && Near(h.cost, 12.5f), "armed Whirlwind with 20 mana: the touch is Whirlwind");
    h = Decide(151, 1, 12.0f);
    Check(h.skill == kAttack && h.why == Why::NoMana, "with 12: the plain attack");
}

void Pad() {
    Arm a;
    PadFilter f;
    int slots[8] = {106, 36, -1, kAttack, 126, 151, 147, 0};   // B Zeal, X Fire Bolt, Y nothing, RB Attack; LT: B Bash, X Whirlwind, Y Frenzy, RB Attack
    auto skillOf = [&](int s) { return s >= 0 && s < 8 ? slots[s] : -1; };
    Step s = f.Poll(kPadB, 0, true, a, skillOf);
    Check(s.event && s.strip == kPadB && s.outcome == Outcome::Armed && s.slot == 0 && a.Armed() == 106, "B down: Zeal armed, B taken from the game");
    s = f.Poll(kPadB, 0, true, a, skillOf);
    Check(!s.event && s.strip == kPadB && a.Armed() == 106, "B held: still taken, nothing new");
    s = f.Poll(kPadB, 0, false, a, skillOf);
    Check(s.strip == kPadB, "B held while no longer armable: still taken until let go (the game never sees half a press)");
    s = f.Poll(0, 0, true, a, skillOf);
    Check(!s.event && s.strip == 0, "B up");
    s = f.Poll(kPadX, 0, true, a, skillOf);
    Check(!s.event && s.strip == 0 && a.Armed() == 106, "X: Fire Bolt is the game's");
    s = f.Poll(kPadX | kPadB, 0, true, a, skillOf);
    Check(s.event && s.strip == kPadB && s.outcome == Outcome::Disarmed && a.Armed() == -1, "B again (X still held): disarmed, only B taken");
    f.Poll(0, 0, true, a, skillOf);
    s = f.Poll(kPadB, 255, true, a, skillOf);
    Check(s.event && s.slot == 4 && a.Armed() == 126, "LT + B: the second bank's Bash");
    f.Poll(0, 0, true, a, skillOf);
    s = f.Poll(kPadX, 255, true, a, skillOf);
    Check(!s.event && s.strip == 0 && a.Armed() == 126, "LT + X: Whirlwind is the game's");
    f.Poll(0, 0, true, a, skillOf);
    s = f.Poll(kPadRB, 0, true, a, skillOf);
    Check(s.event && s.outcome == Outcome::Disarmed && s.strip == kPadRB, "RB, the plain attack: disarms");
    f.Poll(0, 0, true, a, skillOf);
    s = f.Poll(kPadB, 0, false, a, skillOf);
    Check(!s.event && s.strip == 0 && a.Armed() == -1, "not armable (another weapon, view, the touch off): B is the game's");
    s = f.Poll(kPadB, 0, true, a, skillOf);
    Check(!s.event && s.strip == 0, "a button already down when it became armable is left to the game");
    f.Poll(0, 0, true, a, skillOf);
    s = f.Poll(0x1000, 0, true, a, skillOf);
    Check(!s.event && s.strip == 0, "A (the touch's own attack) is never taken");
    s = f.Poll(kPadB | kPadY, 0, true, a, skillOf);
    Check(s.event && s.strip == kPadB && a.Armed() == 106, "B and an empty Y together: only B");
}

void Hits() {
    Hit h = Decide(-1, 5, 50.0f);
    Check(h.skill == kAttack && h.why == Why::NotArmed, "nothing armed: the plain attack");
    h = Decide(106, 10, 50.0f);
    Check(h.skill == 106 && h.why == Why::Skill && Near(h.cost, 2.0f), "Zeal with mana: Zeal");
    h = Decide(106, 10, 1.99f);
    Check(h.skill == kAttack && h.why == Why::NoMana, "Zeal with 1.99 mana: the plain attack");
    h = Decide(106, 10, 2.0f);
    Check(h.skill == 106, "exactly enough: the skill");
    h = Decide(106, 0, 50.0f);
    Check(h.skill == kAttack && h.why == Why::NotLearned, "no level in it: the plain attack");
    h = Decide(111, -1, 4.5f);
    Check(h.skill == 111 && h.levelGuessed && Near(h.cost, 4.0f), "level not read: the first level's cost stands in");
    h = Decide(126, 5, -1.0f);
    Check(h.skill == 126 && h.why == Why::ManaUnknown, "mana not read: the skill, the game decides");
    h = Decide(126, 5, NAN);
    Check(h.skill == 126 && h.why == Why::ManaUnknown, "mana not a number: the same");
    h = Decide(96, 1, 0.0f);
    Check(h.skill == 96, "Sacrifice with no mana: still Sacrifice (it costs life)");
    h = Decide(36, 5, 50.0f);
    Check(h.skill == kAttack && h.why == Why::NotArmed, "a skill not in the list is never answered");
}

void Flight() {
    InFlight f;
    Check(f.Answer(1000) == kAttack, "nothing in flight");
    f.Set(106, 1000);
    Check(f.Answer(1000) == 106 && f.Answer(1350) == 106, "Zeal while the press is young");
    Check(f.Answer(1351) == kAttack, "after the window: as the game has it");
    Check(f.Answer(999) == kAttack, "a clock that went back: as the game has it");
    f.Set(kAttack, 2000);
    Check(f.Answer(2000) == kAttack, "a plain hit: Attack");
    f.Set(106, 3000);
    f.Clear();
    Check(f.Answer(3000) == kAttack, "cleared");
}
}  // namespace

int main() {
    Skills();
    Press();
    List();
    Pad();
    Hits();
    Flight();
    if (g_fail) {
        std::printf("melee_arm_test: %d FAILED\n", g_fail);
        return 1;
    }
    std::printf("melee_arm_test: all passed\n");
    return 0;
}
