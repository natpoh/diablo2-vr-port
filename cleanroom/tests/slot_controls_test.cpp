// [openxr] skill_rt_a (vr/slot_controls.h): the right trigger as the game's RT slot and the right A as its A slot, the left trigger held as the second layer.
// Which slot each control presses, when a control is the slot's and when it stays the layout's (a plain slot, a held grip, someone else's take, a press
// begun in a menu), the left trigger's lead, the shortest press, and the slot-to-index mapping against the game's table (the fake game of the wheel's bank).
// Pure: controls and the slots' contents in, pad buttons and takes out. No game, no headset. Run: build/Release/slot_controls_test.exe (exit code 0 = all passed)

#include "../../vr/slot_controls.h"

#include <cstdio>
#include <cstring>

#include "../../vr/direct_input.h"
#include "../../vr/wheel_bank.h"

using namespace wheel;

static int g_failures = 0;
static int g_checks = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        ++g_checks;                                                      \
        if (!(cond)) {                                                   \
            ++g_failures;                                                \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);  \
        }                                                                \
    } while (0)

constexpr double kDt = 1.0 / 90.0;

struct Rig {
    SlotControls sc;
    double t = 100.0;
    uint16_t mask = 0;
    bool allowed = true, triggerFree = true, aFree = true, grip = false;
    SlotControls::Out last;
    SlotControls::Out Step(float trigger, bool a, float lt = 0.0f) {
        SlotControls::In in;
        in.time = t += kDt;
        in.allowed = allowed;
        in.triggerFree = triggerFree;
        in.aFree = aFree;
        in.trigger = trigger;
        in.leftTrigger = lt;
        in.a = a;
        in.gripHeld = grip;
        in.skillMask = mask;
        return last = sc.Step(in);
    }
    // frames of the same state for `s` seconds; returns the last frame's output
    SlotControls::Out Hold(float trigger, bool a, float lt, double s) {
        SlotControls::Out o;
        for (int n = int(s / kDt) + 1; n > 0; --n) o = Step(trigger, a, lt);
        return o;
    }
};
static uint16_t Bit(int slot) { return uint16_t(1u << slot); }

static void TestSlotMapping() {
    // the control -> slot: A 8 (LT + A 10), the trigger 9 (LT + RT 11), and they are the slots the game's table calls so
    CHECK(SlotOfControl(Control::A, false) == 8 && SlotOfControl(Control::A, true) == 10);
    CHECK(SlotOfControl(Control::Trigger, false) == 9 && SlotOfControl(Control::Trigger, true) == 11);
    CHECK(std::strcmp(kSlotDefs[SlotOfControl(Control::A, false)].label, "A") == 0 && std::strcmp(kSlotDefs[SlotOfControl(Control::A, true)].label, "LT + A") == 0);
    CHECK(std::strcmp(kSlotDefs[SlotOfControl(Control::Trigger, false)].label, "RT") == 0 && std::strcmp(kSlotDefs[SlotOfControl(Control::Trigger, true)].label, "LT + RT") == 0);
    CHECK(kSlotDefs[8].button == kPadA && kSlotDefs[10].button == kPadA && kSlotDefs[8].alt == false && kSlotDefs[10].alt == true);
    CHECK(kSlotDefs[9].button == 0 && kSlotDefs[11].button == 0 && kSlotDefs[11].alt);   // (RT has no pad button bit: it is the pad's trigger value)
    CHECK(kPadA == directinput::pad::kA);
}

static Skill Sk(int32_t id, bool available = true, bool known = true) {
    Skill k;
    k.id = id;
    k.known = known;
    k.available = available;
    return k;
}

static void TestHoldsSkill() {
    // a slot casts a skill only when it holds one: nothing, the basic attack and the pick-up commands are the plain action (the layout's)
    CHECK(!CastsSkill(Skill{}));                       // (not read)
    CHECK(!CastsSkill(Sk(-1)));                        // read, empty
    CHECK(!CastsSkill(Sk(kSkillAttack)) && !CastsSkill(Sk(kSkillInteract)) && !CastsSkill(Sk(kSkillLoot)) && !CastsSkill(Sk(kSkillCubeLoot)));
    CHECK(!CastsSkill(Sk(36, false)));                 // a skill the player no longer has
    CHECK(!CastsSkill(Sk(36, true, false)));
    CHECK(CastsSkill(Sk(36)) && CastsSkill(Sk(kSkillTownPortal)) && CastsSkill(Sk(1)));   // Fire Bolt, the portal, Kick
    CHECK(IsPlainAction(-1) && IsPlainAction(0) && IsPlainAction(357) && IsPlainAction(358) && IsPlainAction(370));
    CHECK(!IsPlainAction(1) && !IsPlainAction(36) && !IsPlainAction(359) && !IsPlainAction(356) && !IsPlainAction(371));
    Snapshot s;
    s.session = 7;
    s.ready = true;
    s.at = 5000;
    s.slot[8] = Sk(36);
    s.slot[9] = Sk(kSkillAttack);
    s.slot[10] = Sk(-1);
    s.slot[11] = Sk(40);
    s.slot[3] = Sk(41);                                // (a slot of the wheel's eight is none of this)
    CHECK(SkillMask(s, 5100, 7) == (Bit(8) | Bit(11)));
    CHECK(HoldsSkill(s, 8) && !HoldsSkill(s, 9) && !HoldsSkill(s, 10) && HoldsSkill(s, 11) && !HoldsSkill(s, -1) && !HoldsSkill(s, 12));
    CHECK(SkillMask(s, 5100, 8) == 0);                 // another session
    CHECK(SkillMask(s, 5000 + 3001, 7) == 0);          // older than three seconds: the controls are the layout's
    CHECK(SkillMask(s, 5000 + 3000, 7) == (Bit(8) | Bit(11)));
    CHECK(SkillMask(s, 4000, 7) == 0);                 // a clock that went back
    s.ready = false;
    CHECK(SkillMask(s, 5100, 7) == 0);
}

static void TestTrigger() {
    Rig r;
    r.mask = Bit(9);                                   // RT holds a skill
    SlotControls::Out o = r.Hold(0, false, 0, 0.1);    // (the trigger up first: armed)
    CHECK(!o.casting && !o.takeTrigger);
    o = r.Step(1.0f, false);                           // the pull
    CHECK(o.casting && o.started[1] == 9 && o.started[0] == -1);
    CHECK(o.takeTrigger && o.takeLeftTrigger && !o.takeA);
    CHECK(o.rt == 255 && o.buttons == 0 && o.lt == 0);
    o = r.Hold(1.0f, false, 0, 0.3);                   // held: still the game's RT, no new start
    CHECK(o.casting && o.rt == 255 && o.started[1] == -1 && o.takeTrigger);
    o = r.Step(0.0f, false);                           // let go after the shortest press: over
    CHECK(!o.casting && !o.takeTrigger && o.rt == 0 && !r.sc.Active(Control::Trigger));
    // a tap shorter than a game poll is held to the shortest press
    o = r.Step(1.0f, false);
    CHECK(o.casting && o.started[1] == 9);
    o = r.Step(0.0f, false);
    CHECK(o.casting && o.rt == 255 && o.takeTrigger);                                    // (still the press: the trigger is up already)
    o = r.Hold(0.0f, false, 0, kMinPressS);
    CHECK(!o.casting && o.rt == 0);
    // the hysteresis: a trigger between the two thresholds neither starts nor ends a press
    o = r.Step(0.4f, false);
    CHECK(!o.casting);
    o = r.Step(0.7f, false);
    CHECK(o.casting && o.started[1] == 9);
    o = r.Hold(0.3f, false, 0, 0.3);
    CHECK(o.casting && o.rt == 255);                                                      // (0.3 is above the release threshold: still held)
    o = r.Hold(0.1f, false, 0, 0.2);
    CHECK(!o.casting);
}

static void TestA() {
    Rig r;
    r.mask = Bit(8);                                   // A holds a skill
    r.Hold(0, false, 0, 0.1);
    SlotControls::Out o = r.Step(0.0f, true);
    CHECK(o.casting && o.started[0] == 8 && o.takeA && o.takeLeftTrigger && !o.takeTrigger);
    CHECK(o.buttons == kPadA && o.rt == 0 && o.lt == 0);
    o = r.Hold(0.0f, true, 0, 0.3);
    CHECK(o.casting && o.buttons == kPadA && o.started[0] == -1);
    o = r.Hold(0.0f, false, 0, 0.2);
    CHECK(!o.casting && o.buttons == 0 && !o.takeA);
}

static void TestPlainSlotsAreTheLayouts() {
    // nothing on RT and A, or the plain action: not a slot's press - no take, no pad button, the layout acts as it always did
    Rig r;
    r.mask = 0;
    r.Hold(0, false, 0, 0.1);
    SlotControls::Out o = r.Step(1.0f, true);
    CHECK(!o.casting && !o.takeTrigger && !o.takeA && !o.takeLeftTrigger && o.buttons == 0 && o.rt == 0 && o.started[0] == -1 && o.started[1] == -1);
    o = r.Hold(1.0f, true, 0, 0.5);
    CHECK(!o.casting && !o.takeTrigger && !o.takeA);
    // only RT holds a skill: A stays the layout's, and the other way round
    Rig t;
    t.mask = Bit(9);
    t.Hold(0, false, 0, 0.1);
    o = t.Step(0.0f, true);
    CHECK(!o.casting && !o.takeA);
    o = t.Step(1.0f, true);
    CHECK(o.casting && o.takeTrigger && !o.takeA && o.buttons == 0 && o.rt == 255);
    Rig a;
    a.mask = Bit(8);
    a.Hold(0, false, 0, 0.1);
    o = a.Step(1.0f, false);
    CHECK(!o.casting && !o.takeTrigger);
    // the slot's skill is picked back to the plain action while a press is on: that press stays the slot's (sticky), the next one is the layout's
    Rig s;
    s.mask = Bit(9);
    s.Hold(0, false, 0, 0.1);
    s.Step(1.0f, false);
    s.mask = 0;
    o = s.Hold(1.0f, false, 0, 0.2);
    CHECK(o.casting && o.rt == 255 && o.takeTrigger);
    s.Hold(0, false, 0, 0.2);
    o = s.Step(1.0f, false);
    CHECK(!o.casting && !o.takeTrigger);
}

static void TestSecondLayer() {
    // the left trigger held: LT + RT (11) and LT + A (10); the left trigger is the game's from the first frame of the press, taken from the layout
    Rig r;
    r.mask = Bit(11) | Bit(10);
    r.Hold(0, false, 1.0f, 0.5);                       // LT down long ago: no wait
    SlotControls::Out o = r.Step(1.0f, false, 1.0f);
    CHECK(o.casting && o.started[1] == 11 && o.lt == 255 && o.rt == 255 && o.takeLeftTrigger && o.takeTrigger);
    r.Hold(0, false, 1.0f, 0.3);
    o = r.Step(0.0f, true, 1.0f);
    CHECK(o.casting && o.started[0] == 10 && o.lt == 255 && o.buttons == kPadA && o.takeA);
    // the layer is read when the press begins: LT up -> slot 9 (not castable here: plain), LT down -> 11
    Rig u;
    u.mask = Bit(9) | Bit(11);
    u.Hold(0, false, 0.0f, 0.1);
    o = u.Step(1.0f, false, 0.0f);
    CHECK(o.started[1] == 9 && o.lt == 0);
    u.Hold(0, false, 0.0f, 0.3);
    u.Hold(0, false, 1.0f, 0.3);
    o = u.Step(1.0f, false, 1.0f);
    CHECK(o.started[1] == 11 && o.lt == 255);
    // only the base slot holds a skill, LT held: the second layer is its own slot - plain -> the layout's
    Rig v;
    v.mask = Bit(9);
    v.Hold(0, false, 1.0f, 0.5);
    o = v.Step(1.0f, false, 1.0f);
    CHECK(!o.casting && !o.takeTrigger && o.started[1] == -1);
    // the lead: the left trigger alone first, as the game reads the modifier before the button - when it went down just before the press
    Rig w;
    w.mask = Bit(11);
    w.Hold(0, false, 0.0f, 0.2);
    w.Step(0.0f, false, 1.0f);                         // LT goes down now
    o = w.Step(1.0f, false, 1.0f);                     // and the trigger one frame later
    CHECK(o.casting && o.started[1] == 11 && o.lt == 255 && o.rt == 0);       // LT alone: no RT yet
    o = w.Hold(1.0f, false, 1.0f, kLeadS * 0.5);
    CHECK(o.lt == 255 && o.rt == 0);
    o = w.Hold(1.0f, false, 1.0f, kLeadS);
    CHECK(o.lt == 255 && o.rt == 255);                 // then the button
    // a tap in that window still lasts lead + the shortest press: the game's poll cannot miss it
    Rig x;
    x.mask = Bit(10);
    x.Hold(0, false, 0.0f, 0.2);
    x.Step(0.0f, false, 1.0f);
    o = x.Step(0.0f, true, 1.0f);
    CHECK(o.casting && o.started[0] == 10 && o.buttons == 0);
    o = x.Step(0.0f, false, 1.0f);                     // let go at once
    CHECK(o.casting);
    bool sawButton = false;
    for (int n = 0; n < 60; ++n) {
        o = x.Step(0.0f, false, 1.0f);
        if (o.buttons == kPadA) sawButton = true;
    }
    CHECK(sawButton && !o.casting);
    // the first layer's press takes the left trigger too (a pulled LT between the thresholds cannot turn it into the second layer)
    Rig y;
    y.mask = Bit(9);
    y.Hold(0, false, 0.1f, 0.1);
    o = y.Step(1.0f, false, 0.1f);
    CHECK(o.casting && o.started[1] == 9 && o.takeLeftTrigger && o.lt == 0);
}

static void TestOneLayerAtATime() {
    // A pressed in the first layer, then LT and the trigger: the trigger's press would be in the other layer - it is left to the layout
    Rig r;
    r.mask = Bit(8) | Bit(11) | Bit(9);
    r.Hold(0, false, 0.0f, 0.1);
    SlotControls::Out o = r.Step(0.0f, true, 0.0f);
    CHECK(o.casting && o.started[0] == 8);
    r.Hold(0.0f, true, 1.0f, 0.2);
    o = r.Step(1.0f, true, 1.0f);
    CHECK(o.started[1] == -1 && !o.takeTrigger && o.rt == 0 && o.lt == 0);
    // both in the same layer: both are the game's
    Rig s;
    s.mask = Bit(8) | Bit(9);
    s.Hold(0, false, 0.0f, 0.1);
    s.Step(0.0f, true);
    o = s.Step(1.0f, true);
    CHECK(o.casting && o.buttons == kPadA && o.rt == 255 && o.takeA && o.takeTrigger);
}

static void TestArmingAndGates() {
    // a control already held when the slot controls become allowed starts nothing; let go and pulled again it does
    Rig r;
    r.mask = Bit(9) | Bit(8);
    r.allowed = false;
    r.Hold(1.0f, true, 0, 0.2);
    r.allowed = true;
    SlotControls::Out o = r.Hold(1.0f, true, 0, 0.2);
    CHECK(!o.casting && !o.takeTrigger && !o.takeA);
    r.Hold(0.0f, false, 0, 0.05);
    o = r.Step(1.0f, true);
    CHECK(o.casting && o.started[0] == 8 && o.started[1] == 9);
    // a menu (or a panel, or another view) comes up under the press: it ends, and the held control does not start again behind it
    r.allowed = false;
    o = r.Step(1.0f, true);
    CHECK(!o.casting && !o.takeTrigger && !o.takeA && o.rt == 0 && o.buttons == 0);
    r.allowed = true;
    o = r.Hold(1.0f, true, 0, 0.3);
    CHECK(!o.casting);
    // the right grip held: the layout's chord (grip + trigger, grip + A), not a slot's; the grip taken mid-press does not end it
    Rig g;
    g.mask = Bit(9) | Bit(8);
    g.Hold(0, false, 0, 0.1);
    g.grip = true;
    o = g.Step(1.0f, true);
    CHECK(!o.casting && o.started[0] == -1 && o.started[1] == -1);
    g.grip = false;
    g.Hold(0, false, 0, 0.1);
    g.Step(1.0f, false);
    g.grip = true;
    o = g.Hold(1.0f, false, 0, 0.2);
    CHECK(o.casting && o.rt == 255);
    // the trigger is somebody else's (a picked slot, the bow's string, a bottle): not started, and A is not affected
    Rig t;
    t.mask = Bit(9) | Bit(8);
    t.triggerFree = false;
    t.Hold(0, false, 0, 0.1);
    o = t.Step(1.0f, true);
    CHECK(o.started[1] == -1 && !o.takeTrigger && o.rt == 0 && o.started[0] == 8 && o.takeA);
    // it comes free while held: that pull still starts nothing; the next one does
    t.triggerFree = true;
    o = t.Hold(1.0f, true, 0, 0.2);
    CHECK(o.started[1] == -1 && !o.takeTrigger);
    t.Hold(0.0f, false, 0, 0.3);
    o = t.Step(1.0f, false);
    CHECK(o.started[1] == 9 && o.takeTrigger);
    // A is a gesture's: only the trigger is the slots'
    Rig b;
    b.mask = Bit(9) | Bit(8);
    b.aFree = false;
    b.Hold(0, false, 0, 0.1);
    o = b.Step(1.0f, true);
    CHECK(o.started[0] == -1 && !o.takeA && o.buttons == 0 && o.started[1] == 9);
    // a press ends the moment the control becomes someone else's too
    b.triggerFree = false;
    o = b.Step(1.0f, true);
    CHECK(!o.casting && o.rt == 0);
}

static void TestReset() {
    Rig r;
    r.mask = Bit(9);
    r.Hold(0, false, 0, 0.1);
    r.Step(1.0f, false);
    CHECK(r.sc.Active(Control::Trigger));
    r.sc.Reset();
    CHECK(!r.sc.Active(Control::Trigger) && !r.sc.LeftDown());
    // after a reset the held trigger is not armed (the session changed under it)
    SlotControls::Out o = r.Hold(1.0f, false, 0, 0.2);
    CHECK(!o.casting);
}

// -- the slot mapping against the game's table (the wheel's bank logic with a fake game) ----------------------------------------------------------------

namespace {
struct FakeGame : Game {
    int slotOf[2][6] = {{2, 1, 3, 4, 0, 5}, {8, 7, 9, 10, 6, 11}};   // index [alt][B X Y RB A -]: the live log's table
    Binding table[32];
    bool offer = false;
    std::vector<Learned> learned;
    bool controller = true;
    int assigned = -1;
    bool assignedLeft = false;
    FakeGame() { for (Binding& b : table) b = Binding{}; }
    bool ControllerLayout() override { return controller; }
    int Bank() override { return 0; }
    int ButtonSlot(uint16_t button, bool alt) override {
        const int col = button == kPadB ? 0 : button == kPadX ? 1 : button == kPadY ? 2 : button == kPadRb ? 3 : button == kPadA ? 4 : 5;
        return slotOf[alt ? 1 : 0][col];
    }
    bool ReadBinding(int index, Binding* out) override {
        if (index < 0 || index > 31) return false;
        *out = table[index];
        return true;
    }
    bool HasSkill(int32_t id, int32_t, int32_t* level) override {
        for (const Learned& l : learned) if (l.id == id) { *level = l.level; return true; }
        return false;
    }
    bool LeftFlag(int32_t, int32_t, bool* left) override { *left = true; return true; }
    bool LearnedSkills(std::vector<Learned>* out) override { *out = learned; return true; }
    bool Name(int32_t id, char* out, int n) override {
        std::snprintf(out, (size_t)n, id == 0 ? "Attack" : id == 357 ? "Interact" : id == 77 ? "Aaa first" : "Skill %d", (int)id);
        return true;
    }
    void DoAssign(int index, bool left, int32_t skill, int32_t owner) override {
        table[index].skill = skill;
        table[index].owner = owner;
        assigned = index;
        assignedLeft = left;
    }
    void DoClear(int index) override { table[index] = Binding{}; }
    bool OfferAttack() override { return offer; }
};
}  // namespace

static void TestSlotsAgainstTheTable() {
    FakeGame g;
    g.learned = {{36, -1, 3}, {40, -1, 1}, {77, -1, 1}};   // ("Aaa first" sorts before "Attack": the basic attack must still be second)
    // RT = the one base left (5), A = 0; the live log's indices: B 2 X 1 Y 3 RB 4 | with LT 8 7 9 10 | A 0 RT 5 | LT+A 6 LT+RT 11
    int idx[kSlotCount];
    CHECK(SlotIndices(g, idx));
    CHECK(idx[8] == 0 && idx[9] == 5 && idx[10] == 6 && idx[11] == 11);
    CHECK(idx[SlotOfControl(Control::A, false)] == 0 && idx[SlotOfControl(Control::Trigger, false)] == 5);
    CHECK(idx[SlotOfControl(Control::A, true)] == 6 && idx[SlotOfControl(Control::Trigger, true)] == 11);
    // the slots as the snapshot reads them: a skill on RT and on LT + A
    g.table[5] = Binding{36, -1};
    g.table[6] = Binding{40, -1};
    g.table[0] = Binding{0, -1};                       // A holds the basic attack
    Snapshot s;
    s.session = 3;
    s.at = 1000;
    CHECK(Capture(g, &s) && s.ready);
    CHECK(s.slot[9].id == 36 && s.slot[10].id == 40 && s.slot[8].id == 0 && s.slot[11].id == -1);
    CHECK(SkillMask(s, 1100, 3) == (Bit(9) | Bit(10)));   // Attack on A is plain; empty LT + RT is nothing
    // the list: without the key it is what it was (Interact first, then the learned by name); with it the basic attack is second
    CHECK(s.count == 4 && s.catalog[0].id == kSkillInteract && s.catalog[1].id == 77 && s.catalog[2].id == 36 && s.catalog[3].id == 40);
    g.offer = true;
    CHECK(Capture(g, &s));
    CHECK(s.count == 5 && s.catalog[0].id == kSkillInteract && s.catalog[1].id == kSkillAttack && s.catalog[2].id == 77 && s.catalog[3].id == 36 && s.catalog[4].id == 40);
    CHECK(std::strcmp(s.catalog[1].name, "Attack") == 0 && s.catalog[1].known && s.catalog[1].available && s.catalog[1].owner == -1);
    CHECK(Offered(s, kSkillAttack, -1));
    // A holding the basic attack is available (a command, not a skill the player may lack); with the key off it is as the game says
    CHECK(s.slot[8].id == 0 && s.slot[8].available);
    g.offer = false;
    Snapshot off;
    CHECK(Capture(g, &off) && !Offered(off, kSkillAttack, -1));
}

static void TestAssignAttackBack() {
    FakeGame g;
    g.offer = true;
    g.learned = {{36, -1, 3}};
    g.table[5] = Binding{36, -1};
    Snapshot s;
    s.session = 3;
    s.at = 1000;
    CHECK(Capture(g, &s));
    // RT -> Attack: through the game's setter, read back; the RT slot is then the plain action
    Request r;
    r.op = Op::Assign;
    r.slot = 9;
    r.skill = kSkillAttack;
    r.owner = -1;
    r.prevSkill = 36;
    r.prevOwner = -1;
    r.session = 3;
    r.bank = 0;
    const char* why = nullptr;
    CHECK(Apply(g, r, s, 1100, &why) == Result::Applied);
    CHECK(g.assigned == 5 && g.assignedLeft && g.table[5].skill == kSkillAttack);
    Snapshot after;
    after.session = 3;
    after.at = 1200;
    CHECK(Capture(g, &after));
    CHECK(!CastsSkill(after.slot[9]) && SkillMask(after, 1300, 3) == 0);   // back to the layout's trigger
    // without the key's entry the same request is refused: that skill is not offered
    FakeGame h;
    h.learned = {{36, -1, 3}};
    h.table[5] = Binding{36, -1};
    Snapshot t;
    t.session = 3;
    t.at = 1000;
    CHECK(Capture(h, &t));
    CHECK(Apply(h, r, t, 1100, &why) == Result::Refused);
    // a skill onto A and onto LT + RT
    r.slot = 8;
    r.skill = 36;
    r.prevSkill = 0;
    r.prevOwner = -1;
    CHECK(Apply(g, r, after, 1300, &why) == Result::Refused);        // (the slot held something else than the request says: refused, never forced)
    r.prevSkill = after.slot[8].id;
    r.prevOwner = after.slot[8].owner;
    CHECK(Apply(g, r, after, 1300, &why) == Result::Applied && g.table[0].skill == 36 && g.assigned == 0);
    r.slot = 11;
    r.prevSkill = after.slot[11].id;
    r.prevOwner = after.slot[11].owner;
    CHECK(Apply(g, r, after, 1300, &why) == Result::Applied && g.table[11].skill == 36 && g.assigned == 11);
}

int main() {
    TestSlotMapping();
    TestHoldsSkill();
    TestTrigger();
    TestA();
    TestPlainSlotsAreTheLayouts();
    TestSecondLayer();
    TestOneLayerAtATime();
    TestArmingAndGates();
    TestReset();
    TestSlotsAgainstTheTable();
    TestAssignAttackBack();
    std::printf("slot_controls_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
