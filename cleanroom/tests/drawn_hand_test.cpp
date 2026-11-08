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

// ---- MirrorGrip: the game's right-hand grip mirrored onto the left hand, on a synthetic mirrored rig ----
// Row-vector 4x4 helpers (skeletons.cpp's convention: rows the axes, the place in 12..14).
struct M4 { float m[16]; };
static M4 MulM(const M4& a, const M4& b) {
    M4 r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float v = 0;
            for (int k = 0; k < 4; ++k) v += a.m[i * 4 + k] * b.m[k * 4 + j];
            r.m[i * 4 + j] = v;
        }
    return r;
}
static M4 InvRigid(const M4& a) {   // rows orthogonal with any lengths and signs (a mirror too): the inverse of the 3x3 is its transpose over the squared lengths
    M4 r{};
    for (int i = 0; i < 3; ++i) {
        const float l2 = a.m[i * 4] * a.m[i * 4] + a.m[i * 4 + 1] * a.m[i * 4 + 1] + a.m[i * 4 + 2] * a.m[i * 4 + 2];
        for (int j = 0; j < 3; ++j) r.m[j * 4 + i] = a.m[i * 4 + j] / l2;
    }
    for (int j = 0; j < 3; ++j) r.m[12 + j] = -(a.m[12] * r.m[j] + a.m[13] * r.m[4 + j] + a.m[14] * r.m[8 + j]);
    r.m[15] = 1;
    return r;
}
static M4 Rot(int axis, float deg, float tx = 0, float ty = 0, float tz = 0, float scale = 1) {
    M4 r{};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1;
    const float t = deg * 0.01745329f, c = std::cos(t), sn = std::sin(t);
    const int i = (axis + 1) % 3, j = (axis + 2) % 3;
    r.m[i * 4 + i] = c; r.m[i * 4 + j] = sn; r.m[j * 4 + i] = -sn; r.m[j * 4 + j] = c;
    for (int k = 0; k < 12; ++k) r.m[k] *= scale;
    r.m[12] = tx; r.m[13] = ty; r.m[14] = tz;
    return r;
}
static M4 KMat(const weaponmirror::K& k) {   // the signed permutation as a 4x4 acting in a bone's own frame (pre-multiplied)
    M4 r{};
    for (int i = 0; i < 3; ++i) r.m[i * 4 + k.col[i]] = (float)k.sign[i];
    r.m[15] = 1;
    return r;
}
static M4 ReflectX(const M4& a) {   // the world reflected in x (post-multiplied: X)
    M4 r = a;
    for (int i = 0; i < 4; ++i) r.m[i * 4] = -r.m[i * 4];
    return r;
}
static bool Near(const M4& a, const M4& b, float eps = 2e-4f) {
    for (int i = 0; i < 16; ++i)
        if (std::fabs(a.m[i] - b.m[i]) > eps) return false;
    return true;
}

static void MirrorGrip_() {
    // the rig's correspondences, as the Amazon's: her L_weapon_attach has its x the other way, y (the shaft) and z the same; the wrists another
    weaponmirror::K kw, kh;
    kw.sign[0] = -1;
    kh.col[0] = 0; kh.col[1] = 2; kh.col[2] = 1; kh.sign[1] = -1;
    // the bind pose: the right wrist somewhere, its attach bone in it; the left ones their mirror images (left = K * right * X)
    const M4 handR = MulM(Rot(1, 25.0f, 0, 0, 0, 1.3f), Rot(2, -40.0f, -0.6f, 3.1f, 0.4f));
    const M4 attachRbind = Rot(0, 8.0f, 0.02f, 0.05f, -0.01f, 1.0f);   // against the wrist
    const M4 weaponR = MulM(attachRbind, handR);
    const M4 handL = MulM(KMat(kh), ReflectX(handR)), weaponL = MulM(KMat(kw), ReflectX(weaponR));
    // the rig read back: the correspondences FromMirrorPair finds are the ones built in
    weaponmirror::K kh2, kw2;
    CHECK(weaponmirror::FromMirrorPair(handR.m, handL.m, &kh2) && weaponmirror::FromMirrorPair(weaponR.m, weaponL.m, &kw2));
    CHECK(kh2.col[0] == kh.col[0] && kh2.col[1] == kh.col[1] && kh2.col[2] == kh.col[2] && kh2.sign[0] == kh.sign[0] && kh2.sign[1] == kh.sign[1] && kh2.sign[2] == kh.sign[2]);
    CHECK(kw2.col[0] == 0 && kw2.col[1] == 1 && kw2.col[2] == 2 && kw2.sign[0] == -1 && kw2.sign[1] == 1 && kw2.sign[2] == 1);
    // at the bind pose the mirrored right grip IS the left attach bone against the left wrist
    M4 out{};
    CHECK(MirrorGrip(attachRbind.m, kw2, kh2, out.m));
    CHECK(Near(out, MulM(weaponL, InvRigid(handL))));
    CHECK(GripTurnDeg(out.m, MulM(weaponL, InvRigid(handL)).m) < 0.05 && GripApart(out.m, MulM(weaponL, InvRigid(handL)).m) < 1e-4);
    // the game's animated grip of a sword (rolled 30 deg about the shaft, slid along it, at the attach bone's own scale 1.0): mirrored, the left weapon is the right
    // weapon's mirror image, wherever the hands are
    for (float roll : {-30.0f, 12.0f, 30.0f, 47.0f}) {
        const M4 gripR = MulM(Rot(1, roll, 0.0f, 0.12f, 0.0f, 1.0f), attachRbind);
        const M4 swordR = MulM(gripR, handR);
        const M4 swordMirror = MulM(KMat(kw2), ReflectX(swordR));   // the right sword reflected, in the left attach bone's axes
        CHECK(MirrorGrip(gripR.m, kw2, kh2, out.m));
        CHECK(Near(MulM(out, handL), swordMirror));
        // the left fist's attach bone in a right-hand animation (the bind's, holding nothing): the log's measure tells the roll it lacked
        const M4 idle = MulM(weaponL, InvRigid(handL));
        CHECK(std::fabs(GripTurnDeg(out.m, idle.m) - std::fabs(roll)) < 0.1);
        // the size the game draws at stays; the mirror's handedness: the left grip's as the bind's left attach bone has it
        CHECK(std::fabs(RowLen(out.m, 1) - RowLen(gripR.m, 1)) < 1e-5f);
        CHECK((Det3(out.m) < 0.0) == (Det3(idle.m) < 0.0));
    }
    // a scaled grip (the hero drawn bigger): the size kept
    const M4 big = MulM(Rot(2, 10.0f, 0.1f, 0.2f, 0.3f, 1.7f), attachRbind);
    CHECK(MirrorGrip(big.m, kw2, kh2, out.m) && std::fabs(RowLen(out.m, 0) - RowLen(big.m, 0)) < 1e-4f);
    // not finite: out untouched
    M4 bad = attachRbind;
    bad.m[5] = NAN;
    M4 keep{};
    keep.m[0] = 7.0f;
    CHECK(!MirrorGrip(bad.m, kw2, kh2, keep.m) && keep.m[0] == 7.0f);
}

int main() {
    MirrorGrip_();
    Slots_();
    Shown_();
    ShieldArm_();
    Carried_();
    if (g_failures) { std::printf("drawn_hand_test: %d of %d FAILED\n", g_failures, g_checks); return 1; }
    std::printf("drawn_hand_test: all %d checks passed\n", g_checks);
    return 0;
}
