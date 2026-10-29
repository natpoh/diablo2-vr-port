// [gameplay] melee_touch_only (vr/melee_trigger.h): with hit by touch on, the trigger does not attack - every weapon the touch swings (melee, bare hands, throwing), a bow and a crossbow keep it. The verdict
// for every state of the facts, the log's gate, and what the layout does with the verdict's mask (the real vr/direct_layout.h). Pure maths, no game, no headset.
// Run: build/Release/melee_trigger_test.exe   (exit code 0 = all passed)

#include "../../vr/melee_trigger.h"

#include <cstdio>
#include <string>
#include <cstdint>

#include "../../vr/direct_input.h"
#include "../../vr/direct_layout.h"

using namespace meleetrigger;

static int g_failures = 0;
static void Expect(bool ok, const char* what, double value = 0.0) {
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s (%g)\n", what, value);
    }
}

static Facts Live(uint32_t type = D2RVR_TYPE_SWORD) {
    Facts f;
    f.on = true;
    f.touchLive = true;
    f.weaponType = type;
    return f;
}

static void KindTests() {
    // only the shot weapons keep the trigger
    Expect(KeepsTrigger(D2RVR_TYPE_BOW) && KeepsTrigger(D2RVR_TYPE_CROSSBOW), "a bow and a crossbow keep the trigger");
    for (uint32_t t = D2RVR_TYPE_UNARMED; t < D2RVR_TYPE_COUNT; ++t) {
        const bool shot = t == D2RVR_TYPE_BOW || t == D2RVR_TYPE_CROSSBOW;
        const Verdict v = Decide(Live(t));
        Expect(KeepsTrigger(t) == shot, "KeepsTrigger is the bow and the crossbow only", t);
        Expect(v.drop == !shot, "every other kind the touch swings: the trigger's attack is withheld", t);
        Expect(v.why == (shot ? Why::Shot : Why::Dropped), "the reason", t);
    }
    // by name: the player's list - melee, bare hands, throwing
    for (const uint32_t t : {D2RVR_TYPE_SWORD, D2RVR_TYPE_AXE, D2RVR_TYPE_MACE, D2RVR_TYPE_SCEPTER, D2RVR_TYPE_DAGGER, D2RVR_TYPE_SPEAR, D2RVR_TYPE_POLEARM, D2RVR_TYPE_CLAW, D2RVR_TYPE_WAND,
                             D2RVR_TYPE_STAFF, D2RVR_TYPE_ORB, D2RVR_TYPE_UNARMED, D2RVR_TYPE_THROWING, D2RVR_TYPE_JAVELIN})
        Expect(Decide(Live(t)).drop, "melee, bare hands, throwing: withheld", t);
}

static void DecideTests() {
    Verdict v = Decide(Live());
    Expect(v.drop && v.why == Why::Dropped, "on, live, a sword: the trigger's attack is withheld");
    {   // off: the way back
        Facts f = Live();
        f.on = false;
        v = Decide(f);
        Expect(!v.drop && v.why == Why::Off, "the key off: nothing withheld");
    }
    {   // not first person, no focus, a menu or a panel: the trigger is the player's (it is a click in a menu)
        Facts f = Live();
        f.f4 = false;
        Expect(!Decide(f).drop && Decide(f).why == Why::NotF4, "another view: the trigger attacks as bound");
        f = Live();
        f.focused = false;
        Expect(!Decide(f).drop && Decide(f).why == Why::Unfocused, "no focus: not withheld");
        f = Live();
        f.menu = true;
        Expect(!Decide(f).drop && Decide(f).why == Why::MenuOrPanel, "a menu or a panel up: the trigger is a click");
    }
    {   // hit by touch not swinging anything (no tracking, no pose, the weapon let go, a throwing weapon with its touch off, a potion): the trigger works - never a lock-out
        Facts f = Live();
        f.touchLive = false;
        v = Decide(f);
        Expect(!v.drop && v.why == Why::TouchNotLive, "hit by touch not live: the trigger works");
        f.weaponType = D2RVR_TYPE_JAVELIN;
        Expect(!Decide(f).drop && Decide(f).why == Why::TouchNotLive, "a javelin with its touch off (nothing else throws it): the trigger works");
        // ... the throw by gesture throws it and the touch swings it: withheld also while the touch is not live (thrown, put down, the hand empty)
        f.thrownHeld = true;
        f.thrownKind = D2RVR_TYPE_JAVELIN;
        Expect(Decide(f).drop && Decide(f).why == Why::Dropped, "a javelin the gesture throws, between throws: the trigger is withheld");
        f.rtSkill = true;
        Expect(!Decide(f).drop && Decide(f).why == Why::SlotSkill, "... a skill bound to the trigger still casts");
        f.rtSkill = false;
        f.menu = true;
        Expect(!Decide(f).drop, "... a menu up: the trigger is a click");
        f.menu = false;
        f.thrownHeld = false;
        f.weaponType = D2RVR_TYPE_BOW;
        Expect(!Decide(f).drop, "a bow, the touch not live: the trigger shoots");
    }
    {   // the default of a fresh Facts is OFF in the struct (the wire's default is on): the verdict never withholds by accident
        Facts f;
        Expect(!Decide(f).drop && Decide(f).why == Why::Off, "an empty Facts withholds nothing");
    }
}

static void GateTests() {
    Gate g;
    const float up[2] = {0.0f, 0.0f}, right[2] = {0.0f, 1.0f}, left[2] = {1.0f, 0.0f};
    Facts f = Live();
    // the first frame says what it is
    Gate::Out o = g.Step(f, up, 10.0);
    Expect(o.changed && o.v.drop && !o.withheld, "the first frame: the verdict is told, no press yet");
    o = g.Step(f, up, 10.1);
    Expect(!o.changed, "the same verdict again: not told again");
    // a press while withheld: counted and told
    o = g.Step(f, right, 10.2);
    Expect(o.withheld && o.withheldCount == 1, "a trigger press while withheld is told");
    o = g.Step(f, right, 10.3);
    Expect(!o.withheld && o.withheldCount == 1, "held, not a new press");
    g.Step(f, up, 10.4);
    o = g.Step(f, left, 10.5);
    Expect(o.withheld && o.withheldCount == 2, "the left trigger's press counts too");
    // a press while NOT withheld is not counted
    Gate h;
    Facts bow = Live(D2RVR_TYPE_BOW);
    h.Step(bow, up, 0.0);
    o = h.Step(bow, right, 0.1);
    Expect(!o.withheld && o.withheldCount == 0 && !o.v.drop, "a bow's shot is not 'withheld'");
    // a change of verdict is told, but not oftener than kQuietS
    Gate k;
    k.Step(f, up, 0.0);
    Facts menu = Live();
    menu.menu = true;
    o = k.Step(menu, up, 0.5);
    Expect(!o.changed && !o.v.drop, "a change inside kQuietS is not told (the blade going in and out of reach does not fill the log)");
    o = k.Step(menu, up, 2.1);
    Expect(o.changed && o.v.why == Why::MenuOrPanel, "... until kQuietS has passed: then told");
    o = k.Step(f, up, 2.2);
    Expect(!o.changed, "back to withheld soon after: waits");
    o = k.Step(f, up, 4.2);
    Expect(o.changed && o.v.drop, "... and is told after kQuietS");
    // only the first presses are told
    Gate c;
    unsigned told = 0;
    for (int i = 0; i < 30; ++i) {
        c.Step(f, up, i * 0.2);
        const Gate::Out p = c.Step(f, right, i * 0.2 + 0.1);
        if (p.withheld) ++told;
    }
    Expect(told == kToldPresses, "the first presses are told, then only counted", told);
    // a clock that goes back does not stall the log
    Gate b;
    b.Step(f, up, 100.0);
    o = b.Step(menu, up, 50.0);
    Expect(o.changed, "a clock that went back: the change is told");
    // Reset forgets
    Gate r;
    r.Step(f, right, 0.0);
    r.Reset();
    o = r.Step(f, up, 0.1);
    Expect(o.changed && o.withheldCount == 0, "after a Reset: told afresh, the count starts again");
}

// What the real layout makes of the verdict: the default table (right trigger = action:attack, A = interact, left trigger = pad LT), the mask a drop takes.
static void LayoutTests() {
    using namespace directinput;
    using namespace directinput::layout;
    const uint32_t takeDrop[2] = {kNoShotTrigger, kNoShotTrigger};   // both triggers, as the wire takes them
    const uint32_t takeNone[2] = {0, 0};
    const auto step = [&](Layout& L, std::initializer_list<std::pair<Ctl, float>> list, const uint32_t take[2], uint64_t t) {
        Controls c;
        for (const auto& p : list) c.v[p.first] = p.second;
        return L.Step(Mode::World, c, t, false, NoShotMask(take));
    };
    {   // as bound: the right trigger attacks
        Layout L;
        Outputs o = step(L, {{kRTrigger, 1.0f}}, takeNone, 0);
        Expect((o.actions & act::kAttack) != 0, "no mask: the right trigger is the game's attack");
    }
    {   // withheld: no attack, however long it is held
        Layout L;
        Outputs o = step(L, {{kRTrigger, 1.0f}}, takeDrop, 0);
        Expect((o.actions & act::kAttack) == 0 && (o.pad.buttons & pad::kA) == 0, "the mask: the right trigger gives no attack");
        o = step(L, {{kRTrigger, 1.0f}}, takeDrop, 500);
        Expect((o.actions & act::kAttack) == 0, "... held for half a second, still none");
        o = step(L, {}, takeDrop, 600);
        Expect((o.actions & act::kAttack) == 0, "... nor at the release");
    }
    {   // everything else works as bound: A is interact, the left trigger is the pad's LT (the second bank), a stick is a stick
        Layout L;
        Outputs o = step(L, {{kRA, 1.0f}, {kLTrigger, 1.0f}}, takeDrop, 0);
        Expect((o.actions & act::kInteract) != 0, "the mask leaves A (interact) alone");
        Expect(o.pad.lt >= 250, "the mask leaves the left trigger (the pad's LT, the second bank) alone", o.pad.lt);
    }
    {   // a press begun with the mask lifted attacks; the mask coming later stops it (the verdict changes under a held trigger)
        Layout L;
        Outputs o = step(L, {{kRTrigger, 1.0f}}, takeNone, 0);
        Expect((o.actions & act::kAttack) != 0, "a press without the mask attacks");
        o = step(L, {{kRTrigger, 1.0f}}, takeDrop, 10);
        Expect((o.actions & act::kAttack) == 0, "the mask arriving under the held trigger stops the attack at once");
        o = step(L, {{kRTrigger, 1.0f}}, takeNone, 20);
        Expect((o.actions & act::kAttack) != 0, "... and lifting it gives it back while the trigger is still down");
    }
    {   // a trigger bound to a skill slot (pad B) is not a shot: it works with the mask
        Table t = DefaultTable();
        t.plain[kRTrigger] = Bind(OutPadBtn(pad::kB));
        Layout L;
        L.SetWorld(t);
        Outputs o = step(L, {{kRTrigger, 1.0f}}, takeDrop, 0);
        Expect((o.pad.buttons & pad::kB) != 0, "a trigger bound to a skill slot (pad B) still casts it");
    }
    {   // a trigger bound to both: the shot goes, the other output stays
        Table t = DefaultTable();
        t.plain[kRTrigger] = Bind(OutAction(act::kAttack), OutPadBtn(pad::kX));
        Layout L;
        L.SetWorld(t);
        Outputs o = step(L, {{kRTrigger, 1.0f}}, takeDrop, 0);
        Expect((o.actions & act::kAttack) == 0 && (o.pad.buttons & pad::kX) != 0, "attack + pad X on the trigger: the attack goes, pad X works");
    }
    {   // the face buttons and the bumper are the spells' and skills' places: the mask never touches them
        Layout L;
        Outputs o = step(L, {{kRB, 1.0f}, {kLX, 1.0f}, {kLY, 1.0f}}, takeDrop, 0);
        Expect((o.pad.buttons & pad::kB) != 0 && (o.pad.buttons & pad::kX) != 0 && (o.pad.buttons & pad::kY) != 0, "B, X, Y (the skill slots) work with the mask");
    }
}

// [openxr] skill_rt_a: a skill the player put on the RT slot casts with the trigger - only the plain attack on it stays withheld.
static void SlotSkillTests() {
    {   // a sword in hand, the trigger is the RT slot's and RT holds a skill: not withheld, and the reason says so
        Facts f = Live();
        f.rtSkill = true;
        const Verdict v = Decide(f);
        Expect(!v.drop && v.why == Why::SlotSkill, "a skill on RT: the trigger is not withheld");
        Expect(std::string(WhyText(Why::SlotSkill)).find("RT") != std::string::npos, "the reason names the RT slot");
    }
    {   // every weapon the touch swings: the same (the rule is the slot's, not the weapon's)
        for (uint32_t t = D2RVR_TYPE_UNARMED; t < D2RVR_TYPE_COUNT; ++t) {
            Facts f = Live(t);
            f.rtSkill = true;
            Expect(!Decide(f).drop, "a skill on RT: no kind of weapon withholds it", t);
        }
    }
    {   // the plain attack (rtSkill false: nothing, Attack, Interact on RT, or the slot controls not in charge of the trigger): withheld as before
        Facts f = Live();
        f.rtSkill = false;
        Expect(Decide(f).drop && Decide(f).why == Why::Dropped, "the plain attack on the trigger: still withheld");
    }
    {   // the other reasons come first and are unchanged: a bow shoots, hit by touch not live, a menu, the key off
        Facts f = Live(D2RVR_TYPE_BOW);
        f.rtSkill = true;
        Expect(Decide(f).why == Why::Shot, "a bow with a skill on RT: shot, as before");
        f = Live();
        f.rtSkill = true;
        f.touchLive = false;
        Expect(Decide(f).why == Why::TouchNotLive, "a skill on RT, hit by touch not live: the same reason as before");
        f = Live();
        f.rtSkill = true;
        f.menu = true;
        Expect(Decide(f).why == Why::MenuOrPanel, "a skill on RT, a menu up: the same reason as before");
        f = Live();
        f.rtSkill = true;
        f.on = false;
        Expect(Decide(f).why == Why::Off, "a skill on RT, the key off: the same reason as before");
    }
    {   // the gate: a press with a skill on RT is not a withheld press (no count, no log line); the same press with the plain attack is
        Gate g;
        Facts f = Live();
        f.rtSkill = true;
        const float down[2] = {0.0f, 1.0f}, up[2] = {0.0f, 0.0f};
        Gate::Out o = g.Step(f, up, 1.0);
        o = g.Step(f, down, 1.1);
        Expect(!o.withheld && o.withheldCount == 0 && !o.v.drop, "a press with a skill on RT is not counted as withheld");
        g.Step(f, up, 1.2);
        f.rtSkill = false;
        o = g.Step(f, down, 1.3);
        Expect(o.withheld && o.withheldCount == 1 && o.v.drop, "the same press with the plain attack on RT is withheld and counted");
        Expect(!o.changed, "the reason changed from SlotSkill to Dropped 0.3 s after it was told: not told again within the quiet time");
        o = g.Step(f, up, 4.0);
        Expect(o.v.why == Why::Dropped && o.changed, "and told once the quiet time has passed");
    }
}

int main() {
    KindTests();
    DecideTests();
    GateTests();
    LayoutTests();
    SlotSkillTests();
    if (g_failures == 0) std::printf("melee_trigger_test: all passed\n");
    return g_failures == 0 ? 0 : 1;
}
