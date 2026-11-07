// Offline checks of vr/drawn_hand.h ([hands] left_slot_drawn_left): for every combination of what the two hand slots hold, which attach bone the game draws the weapon on
// and whether ours carries it to the slot's pose; whether a right-slot shield goes to the right forearm; and the carried matrix (place, axes, the drawn bone's size and
// handedness). Run: drawn_hand_test.exe (exit 0 = all passed).
#include <cmath>
#include <cstdio>
#include <initializer_list>

#include "drawn_hand.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

using namespace drawnhand;

enum Item { None, Sword, Axe, Dagger, Javelin, Claw, Orb, Bow, Crossbow, Shield, Quiver, TwoHandSword };

static Slots Make(Item right, Item left) {
    Slots s;
    const Item items[2] = {right, left};
    for (int side = 0; side < 2; ++side) {
        switch (items[side]) {
        case Sword: s.kind[side] = D2RVR_TYPE_SWORD; break;
        case Axe: s.kind[side] = D2RVR_TYPE_AXE; break;
        case Dagger: s.kind[side] = D2RVR_TYPE_DAGGER; break;
        case Javelin: s.kind[side] = D2RVR_TYPE_JAVELIN; break;
        case Claw: s.kind[side] = D2RVR_TYPE_CLAW; break;
        case Orb: s.kind[side] = D2RVR_TYPE_ORB; break;
        case Bow: s.kind[side] = D2RVR_TYPE_BOW; break;
        case Crossbow: s.kind[side] = D2RVR_TYPE_CROSSBOW; break;
        case TwoHandSword: s.kind[side] = D2RVR_TYPE_SWORD; s.two |= D2RVR_TWO_HANDED | D2RVR_TWO_HANDED_KNOWN; break;
        case Shield: s.shield[side] = true; break;
        case Quiver: case None: break;
        }
    }
    // gamestate: two-handed "in both hands" when the other slot is empty
    if ((s.two & D2RVR_TWO_HANDED) && ((items[0] == TwoHandSword && items[1] == None) || (items[1] == TwoHandSword && items[0] == None))) s.two |= D2RVR_TWO_HANDS_ON;
    return s;
}

static void Slots_() {
    // the bug: a sword alone in the left slot, a shield in the right - drawn on the right bone, carried to the left; the shield to the right forearm
    {
        const Plan p = Decide(Make(Shield, Sword));
        CHECK(p.carry && p.slot == kLeft && p.drawn == kRight && p.shieldToRight && p.why == Why::Carry);
    }
    // a sword alone in the left slot, nothing in the right: carried, no shield to move
    {
        const Plan p = Decide(Make(None, Sword));
        CHECK(p.carry && p.slot == kLeft && p.drawn == kRight && !p.shieldToRight);
    }
    // any one-hand weapon alone in the left slot is the right hand's part: an axe, a dagger, a javelin, a claw, an orb (the pose's Shown check confirms each)
    for (Item it : {Axe, Dagger, Javelin, Claw, Orb}) {
        const Plan p = Decide(Make(None, it));
        CHECK(p.carry && p.drawn == kRight);
        const Plan q = Decide(Make(Shield, it));
        CHECK(q.carry && q.shieldToRight);
    }
    // the usual: a weapon in the right slot, a shield (or nothing) in the left - nothing carried, the shield stays the game's
    for (Item left : {None, Shield, Quiver}) {
        const Plan p = Decide(Make(Sword, left));
        CHECK(!p.carry && p.slot == kRight && p.drawn == kRight && !p.shieldToRight && p.why == Why::NoLeftWeapon);
    }
    // dual wield: a weapon in each hand - each on its own bone (both ways round, mixed kinds too)
    for (Item r : {Sword, Axe, Dagger, Javelin, Claw})
        for (Item l : {Sword, Axe, Dagger, Javelin, Claw}) {
            const Plan p = Decide(Make(r, l));
            CHECK(!p.carry && p.drawn == kLeft && p.slot == kLeft && p.why == Why::Dual);
        }
    // a bow in the left with a quiver in the right, a crossbow: on its own wrist
    CHECK(!Decide(Make(Quiver, Bow)).carry && Decide(Make(Quiver, Bow)).why == Why::Ranged);
    CHECK(!Decide(Make(None, Crossbow)).carry);
    CHECK(!Decide(Make(Bow, Quiver)).carry);
    // a two-handed sword alone in the left slot (in both hands): both_attach lays it between the bones - not carried into one point
    {
        const Plan p = Decide(Make(None, TwoHandSword));
        CHECK(!p.carry && p.why == Why::BothHands);
        // a barbarian's two-handed sword in the left beside a shield is held in one hand: carried
        const Plan q = Decide(Make(Shield, TwoHandSword));
        CHECK(q.carry && q.shieldToRight);
    }
    // empty hands, a shield alone: nothing
    CHECK(!Decide(Make(None, None)).carry);
    CHECK(!Decide(Make(Shield, None)).carry);
    CHECK(!Decide(Make(None, Shield)).carry);
    // every why has words for the log
    for (Why w : {Why::Carry, Why::NoLeftWeapon, Why::Dual, Why::Ranged, Why::BothHands}) CHECK(WhyText(w)[0] != '?');
}

static void Shown_() {
    CHECK(Shown(1.0f) && !Hidden(1.0f));
    CHECK(!Shown(0.001f) && Hidden(0.001f));   // the game's point
    CHECK(!Shown(0.5f) && !Hidden(0.5f));      // neither: not trusted
    CHECK(!Shown(NAN) && Hidden(NAN));
}

static void ShieldArm_() {
    CHECK(ShieldArm(false, true) == kRight);   // the weapon in the left hand (the slot's): the shield on the right forearm
    CHECK(ShieldArm(true, true) == kRight);    // both hands on it: stays
    CHECK(ShieldArm(false, false) == kRight);  // let go: stays
    CHECK(ShieldArm(true, false) == kLeft);    // passed into the right hand alone: back to the game's arm
}

static void Ident(float m[16], float s = 1.0f) {
    for (int i = 0; i < 16; ++i) m[i] = 0.0f;
    m[0] = m[5] = m[10] = s;
    m[15] = 1.0f;
}

static void Carried_() {
    // the slot's (left) bone as posed: a mirror (det < 0), size 1.3, turned 90 deg about z, at (1, 2, 3)
    float slot[16];
    Ident(slot);
    slot[0] = 0.0f; slot[1] = -1.3f; slot[2] = 0.0f;   // x row (mirrored: -y)
    slot[4] = -1.3f; slot[5] = 0.0f; slot[6] = 0.0f;   // y row
    slot[8] = 0.0f; slot[9] = 0.0f; slot[10] = 1.3f;   // z row
    slot[12] = 1.0f; slot[13] = 2.0f; slot[14] = 3.0f;
    CHECK(Det3(slot) < 0.0);
    // the right bone: proper (det > 0), the game draws at size 0.9
    float drawnNow[16], drawnGame[16], out[16];
    Ident(drawnNow, 0.9f);
    drawnNow[12] = -5.0f;
    Ident(drawnGame, 1.0f);
    CHECK(Carried(slot, drawnNow, drawnGame, out));
    // at the slot bone's place
    CHECK(out[12] == 1.0f && out[13] == 2.0f && out[14] == 3.0f);
    // the drawn bone's size, its own handedness
    for (int r = 0; r < 3; ++r) CHECK(std::fabs(RowLen(out, r) - 0.9f) < 1e-5f);
    CHECK(Det3(out) > 0.0);
    // the blade's line (y) and z the slot's; only x turned over
    CHECK(std::fabs(out[4] + 0.9f) < 1e-5f && std::fabs(out[5]) < 1e-6f);
    CHECK(std::fabs(out[10] - 0.9f) < 1e-5f);
    CHECK(std::fabs(out[1] - 0.9f) < 1e-5f);
    // the same handedness: nothing turned over
    {
        float slotR[16], o2[16];
        Ident(slotR, 2.0f);
        CHECK(Carried(slotR, drawnNow, drawnGame, o2) && o2[0] > 0.0f && Det3(o2) > 0.0);
    }
    // a drawn bone the game shrank (thrown, hidden): the carried one is a point too
    {
        float tiny[16], o3[16];
        Ident(tiny, 0.001f);
        CHECK(Carried(slot, tiny, drawnGame, o3) && RowLen(o3, 1) < 0.01f);
    }
    // a slot pose that is a point gives no axes: refused, out untouched
    {
        float point[16], o4[16];
        Ident(point, 0.0f);
        for (float& v : o4) v = 7.0f;
        CHECK(!Carried(point, drawnNow, drawnGame, o4) && o4[0] == 7.0f);
        float bad[16];
        Ident(bad);
        bad[13] = NAN;
        CHECK(!Carried(bad, drawnNow, drawnGame, o4));
    }
}

int main() {
    Slots_();
    Shown_();
    ShieldArm_();
    Carried_();
    if (g_failures) { std::printf("drawn_hand_test: %d of %d FAILED\n", g_failures, g_checks); return 1; }
    std::printf("drawn_hand_test: all %d checks passed\n", g_checks);
    return 0;
}
