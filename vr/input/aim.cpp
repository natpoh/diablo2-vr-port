// vr/input/aim.cpp - cut from vr/vrcam.cpp (docs/plan_modules.md M10); what it gives: vr/input/input.h. It sees vrcam.cpp's headers.
#include "../vrcam_headers.h"

using namespace D2RL;
using namespace vrc;   // as in vrcam.cpp

namespace vrc {

// Bow aim: while a skill button is held, the left stick is the bow hand's
// pointing, so the game aims (and turns the hero) that way instead of where
// his feet face. The hand's direction is in the head's turn frame, which the
// camera turns with, so it goes in as a stick "relative to the view" and the
// usual camera-yaw turn below makes it a direction on the game's screen.
// Pointing = the grip's -Z (a controller held like a gun) plus its -Y (a fist
// round a bow's grip): flattened on the ground, one of the two always remains.
bool BowStick(XINPUT_STATE* s) {
    if (g_set.bowMode.load() != 1) return false;
    const XINPUT_GAMEPAD& g = s->Gamepad;
    const WORD skills = XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_Y | XINPUT_GAMEPAD_RIGHT_SHOULDER;
    if (!(g.wButtons & skills) && g.bRightTrigger < 128) return false;
    const D2RVR_Shared* sh = Shared();
    const int hand = g_set.bowHand.load();
    if (!sh || sh->version != D2RVR_SHARED_VERSION || !sh->headValid || !(sh->handsValid & (1u << hand))) return false;
    const float* q = hand == 0 ? sh->rightRot : sh->leftRot;
    auto rot = [&](float vx, float vy, float vz, float* o) {   // q v q*
        const float tx = 2 * (q[1]*vz - q[2]*vy), ty = 2 * (q[2]*vx - q[0]*vz), tz = 2 * (q[0]*vy - q[1]*vx);
        o[0] = vx + q[3]*tx + (q[1]*tz - q[2]*ty);
        o[1] = vy + q[3]*ty + (q[2]*tx - q[0]*tz);
        o[2] = vz + q[3]*tz + (q[0]*ty - q[1]*tx);
    };
    float a[3], b[3];
    rot(0, 0, -1, a); rot(0, -1, 0, b);
    const float x = a[0] + b[0], fwd = -(a[2] + b[2]);   // head frame: x right, z back
    const float l = sqrtf(x*x + fwd*fwd);
    if (!(l > 0.05f)) return false;
    s->Gamepad.sThumbLX = (SHORT)std::clamp(x / l * 32767.0f, -32768.0f, 32767.0f);
    s->Gamepad.sThumbLY = (SHORT)std::clamp(fwd / l * 32767.0f, -32768.0f, 32767.0f);
    return true;
}

// Attack along the hand (bow mode 2). On the pad the game attacks where the
// hero's feet face and never reads the stick for it (2026-10-02 log: ten shots,
// stick turned each time, facing stayed 82.5). With the mouse it attacks the
// point under the cursor, standing still while Shift is held - and the point
// under the cursor is found through the world camera's ray, which RayBuild
// hands us anyway. So while A is held: the ray starts at the aiming hand and
// runs where it points, A itself is kept from the game, and Shift + left
// button go in instead. Shooting and striking both go that way, running
// backwards included.

// The aiming hand per weapon set, as the player sets it: a bow in set I aimed
// with the left hand, a sword in set II with the right - or both sets right,
// or both left; the weapon itself does not decide.
// Bow and crossbow: the left hand ([bow] ranged_left). Otherwise the set's hand; if only the other one is tracked (a controller asleep or out
// of view), that one - an untracked hand left every arrow flying straight ahead.
// The hand a bow or crossbow is held in without [hands] shoulder_draw_ranged_swap: the settings' (ranged_left, else the weapon set's aim hand).
int RangedHomeHand() { return g_set.rangedLeft.load() ? 1 : gamestate::WeaponSet() == 2 ? g_set.bowHand2.load() : g_set.bowHand.load(); }

int AimHand() {
    const uint32_t wc = gamestate::WeaponClass();
    const bool ranged = wc == 3 || wc == 4;   // D2RVR_WEAPON_BOW, D2RVR_WEAPON_CROSSBOW
    const uint32_t type = gamestate::WeaponType();
    const bool thrown = type == D2RVR_TYPE_JAVELIN || type == D2RVR_TYPE_THROWING;   // thrown from the right hand
    int hand = thrown ? 0 : ranged && g_set.rangedLeft.load() ? 1 : gamestate::WeaponSet() == 2 ? g_set.bowHand2.load() : g_set.bowHand.load();
    // [hands] shoulder_draw_ranged_swap: a bow drawn into the other hand is held, drawn on, aimed and fed from THAT hand (read every frame by the draw, the quiver, the skeleton)
    if (wc == 3 && g_set.shoulderDraw.load() && g_set.rangedHolster.load() && g_set.rangedSwap.load()) {
        const int live = rangedwire::Side();
        if (live == 0 || live == 1) hand = live;
    }
    // [hands] shoulder_draw: a melee weapon the weight model carries in the OTHER hand than the game's slot (drawn from the shoulder with the other hand, or passed) is aimed
    // along the hand that carries it; in the slot's own hand the set's configured hand stays what aims, exactly as before. (The game's slots do not change - only the aim.)
    if (g_set.shoulderDraw.load() && !ranged && !thrown && weight::Placed()) {
        const int carrying = weight::CarryingHand();
        if ((carrying == 0 || carrying == 1) && carrying != (int)(gamestate::WeaponHand() & 1u)) hand = carrying;
    }
    const D2RVR_Shared* sh = Shared();
    if (sh && sh->version == D2RVR_SHARED_VERSION && !(sh->handsValid & (1u << hand)) && (sh->handsValid & (1u << (hand ^ 1))))
        return hand ^ 1;
    return hand;
}

// A staff is held in both hands, the right one ahead: it points from the
// left hand to the right one, whichever way the wrists are turned. With
// [hands] staff_free_left the right hand alone holds it: it points along the
// shaft as the skeleton hook last posed it, the left hand on it or not.
// A crossbow is held the same way ([bow] xbow_two_hands): the right hand alone,
// the left taking it with the grip - the right hand behind, the left ahead.
bool XbowLikeStaff() { return g_set.xbowTwoHands.load() && gamestate::WeaponType() == D2RVR_TYPE_CROSSBOW; }
// A melee weapon held in both hands the staff's way, without its aim (they do not shoot):
// 1 a spear or a polearm (the left slides along the shaft), 2 a two-handed sword with the
// other hand empty (the left on the hilt); 0 none - one-handed, or a barbarian's two-handed
// sword beside another weapon.
// A staff held by the general rule (staff_hand wrist, shaft = bone axis): every hero's, the sorceress's too
// ("make hers like everyone's", 2026-10-06 - her left wrist was a guess from before the rule).
bool StaffByRule() { return gamestate::WeaponType() == D2RVR_TYPE_STAFF; }
int HeldLikeStaff() {
    const uint32_t type = gamestate::WeaponType(), two = gamestate::TwoHanded();
    if (!(two & D2RVR_TWO_HANDS_ON)) return 0;
    // a two-handed axe or mace (maul) has a haft like a polearm's: the left slides along it too
    if ((type == D2RVR_TYPE_SPEAR || type == D2RVR_TYPE_POLEARM || type == D2RVR_TYPE_AXE || type == D2RVR_TYPE_MACE ||
         type == D2RVR_TYPE_OTHER) && g_set.polearmTwoHands.load()) return 1;
    if (type == D2RVR_TYPE_SWORD && g_set.swordTwoHands.load()) return 2;
    return 0;
}
// Where a staff's aim comes from (vr/aim_source.h): the controllers - never the weapon as the weight model draws it. Only with the body (F4): there the
// controllers hold the weapon and the line between them is its line. Anywhere else the hands are just held, and that line points sideways - a crossbow in
// third person shot to the left (2026-10-06); there it aims with one hand, like the bow. Along the staff in the right hand (staff_free_left) too, not only
// aimed from hand to hand ([bow] staff_two_hands).
aimsource::Out StaffAimChoice() {
    aimsource::In in;
    in.fullBody = FullBody();
    const D2RVR_Shared* sh = Shared();
    in.sharedOk = sh && sh->version == D2RVR_SHARED_VERSION;
    in.handsValid = in.sharedOk ? sh->handsValid : 0u;
    in.staffWeapon = gamestate::WeaponType() == D2RVR_TYPE_STAFF;
    in.xbowLike = XbowLikeStaff();
    in.staffTwoHands = g_set.staffTwoHands.load();
    in.staffFreeLeft = g_set.staffFreeLeft.load();
#if D2RVR_FIRST_PERSON
    in.armsOnControllers = ArmsMode() == 2;
    if (in.staffFreeLeft && in.armsOnControllers) {
        // [weight] on: the model holds the staff and the skeleton hook does not pose it the staff_free_left way, so the hook's axis is not made (it
        // went stale and the staff's aim was lost: the spells went where the plain hand points, 2026-10-14). The model says which hands hold it.
        weight::AimInfo wi;
        weight::Aim(&wi);
        in.weightHas = weight::Placed() && in.staffWeapon;
        in.holds[0] = wi.holds[0];
        in.holds[1] = wi.holds[1];
        in.carrying = wi.hand;
        in.drawnFresh = !in.weightHas && skel::StaffAxis(nullptr);
    }
#endif
    return aimsource::Choose(in);
}
bool StaffAim() { return StaffAimChoice().aims; }

// What the aim goes along, told whenever it changes (the log says what a cast went along; "aim:" lines).
void NoteAimSource(const aimsource::Out& o, int hand) {
    static std::atomic<int> told{-1};
    const int key = (o.aims ? 1 : 0) | ((int)o.src << 1) | (hand << 4) | (o.leftOn ? 64 : 0);
    if (told.exchange(key) == key) return;
    const char* hn = hand ? "left" : "right";
    if (!o.aims) LogF("aim: the %s controller's own direction (%s)", hn, o.why);
    else if (o.src == aimsource::Src::LeftToRight) LogF("aim: left->right controllers (%s)", o.why);
    else if (o.src == aimsource::Src::Shaft) LogF("aim: the %s controller, along the staff as it grips it (%s)", hn, o.why);
    else LogF("aim: the drawn staff's axis (%s)", o.why);
}

#if D2RVR_FIRST_PERSON
bool BowInHand();
bool ThrownInHand();
// [hands] spell_aim_model: wanted now - VR first person (F4) with the arms on the controllers, and no bow, crossbow, javelin or throwing weapon in hand
// (those keep their own lines: the draw, the barrel, the throw).
bool SpellModelWanted() {
    return g_set.spellAimModel.load() && viewgate::F4() && FullBody() && ArmsMode() == 2 && !BowInHand() && !ThrownInHand();
}
bool CasterInHand() { const uint32_t t = gamestate::WeaponType(); return t == D2RVR_TYPE_STAFF || t == D2RVR_TYPE_WAND || t == D2RVR_TYPE_ORB; }
// The aim from the hero as drawn (vr/spell_aim_model.h), in the head's turn-only frame (origin from the eye, metres; dir unit); src None: the old aim.
// A staff, wand or orb: the skeleton hook's weapon bone - unless [weight] has the weapon: then the shaft as the carrying hand grips it (the drawn one
// lags and sags; lying or let go, no weapon aims), as vr/aim_source.h says. Else the aiming hand (AimHand) as drawn; with the weapon, the hand that holds it.
spellmodel::Out SpellModelAim() {
    spellmodel::In in;
    in.caster = CasterInHand();
    skel::CastPose cp;
    skel::CastPoseNow(&cp);
    int hand = AimHand();
    if (in.caster) {
        if (weight::Placed()) {
            weight::AimInfo wi;
            const D2RVR_Shared* sh = Shared();
            if (weight::Aim(&wi) && (wi.hand == 0 || wi.hand == 1) && sh && sh->version == D2RVR_SHARED_VERSION && (sh->handsValid & (1u << wi.hand))) {
                in.weightCarries = true;
                hand = wi.hand;
                aimsource::TurnBy(wi.hand == 0 ? sh->rightRot : sh->leftRot, wi.shaft, in.gripDir);
                memcpy(in.gripFrom, wi.hand == 0 ? sh->rightHand : sh->leftHand, sizeof in.gripFrom);
                // both hands on it, both tracked: the staff as the weight model draws it between the two grips (butt -> tip), not the first hand's
                // grip alone, nor the line between the controllers (each grip sits a fist's offset from its controller: 0.170, "the ray goes off to the side")
                if (wi.holds[0] && wi.holds[1] && (sh->handsValid & 3u) == 3u) {
                    in.bothHands = true;
                    in.shaftOk = weight::ShaftEnds(in.shaftButt, in.shaftTip);
                }
            }
        } else if (cp.weapon) {
            in.weaponOk = true;
            hand = cp.weaponSide & 1;
            memcpy(in.weaponOrigin, cp.weaponOrigin, sizeof in.weaponOrigin);
            memcpy(in.weaponTip, cp.weaponTip, sizeof in.weaponTip);
        }
    }
    in.handOk = cp.hand[hand];
    memcpy(in.palm, cp.palm[hand], sizeof in.palm);
    memcpy(in.fingers, cp.fingers[hand], sizeof in.fingers);
    memcpy(in.face, cp.face[hand], sizeof in.face);
    // [hands] staff_upright_arm: a staff held upright aims along the arm that holds it - its drawn shoulder (skel::ArmReach, the same head frame) to its
    // drawn palm - blended with the staff's line as it tilts (vr/staff_upright.h). After d2r-pcvr 787f820 (bearstonem, MIT).
    in.staff = gamestate::WeaponType() == D2RVR_TYPE_STAFF;
    if (in.staff && g_set.staffUprightArm.load()) {
        in.uprightArm = true;
        in.uprightFullDeg = g_set.staffUprightFullDeg.load();
        in.uprightNoneDeg = g_set.staffUprightNoneDeg.load();
        float len = 0.0f;
        in.shoulderOk = skel::ArmReach(hand, in.shoulder, &len);
    }
    return spellmodel::Choose(in);
}
// What the aim from the model goes along, told whenever it changes ("spell aim (model):" lines).
void NoteSpellModel(const spellmodel::Out& m) {
    static std::atomic<int> told{-1};
    const int key = (int)m.src | (CasterInHand() ? 16 : 0) | (m.armW > 0.0f ? 32 : 0);   // (the arm taking part in an upright staff's line: told once as it starts and stops)
    if (told.exchange(key) == key) return;
    if (m.src == spellmodel::Src::None) LogF("spell aim (model): the controller's own line, the old aim and its corrections - %s", m.why);
    else LogF("spell aim (model): %s - %s; no hand-set correction ([hands] spell_aim_model)", spellmodel::Name(m.src), m.why);
}
#endif

bool ShooterActive();
// dRaw (optional): the same ray as the pointing hand gives it, before [bow] aim_yaw turns it and before it is tipped toward the ground - what [bow] aim_distance_by_arm measures the arm along.
bool HandRay(float* o, float* d, float* dRaw = nullptr) {
    // Mouse look and W A S D (flat first person): no hands - the blow or the
    // shot goes where the camera looks, a little down so it meets the ground.
    if (ShooterActive() && g_eyeOk.load()) {
        AcquireSRWLockShared(&g_stickLock); const StickFrame f = g_stickFrame; ReleaseSRWLockShared(&g_stickLock);
        const float fl = sqrtf(f.camF[0] * f.camF[0] + f.camF[2] * f.camF[2]);
        if (!f.valid || fl < 1e-3f) return false;
        const float dw[3] = {f.camF[0] / fl, -0.08f, f.camF[2] / fl};
        const float l = sqrtf(1.0f + 0.08f * 0.08f);
        for (int i = 0; i < 3; ++i) { d[i] = dw[i] / l; o[i] = g_eyeWorld[i].load(); if (dRaw) dRaw[i] = d[i]; }
        return true;
    }
#if D2RVR_FIRST_PERSON
    // [hands] spell_aim_model: the line from the hero as drawn - the staff, wand or orb, else the hand - with no correction on top. Into the world as
    // below (the head frame along the camera's own right and forward), tipped to meet the ground as below. No fresh pose: the old aim below.
    if (SpellModelWanted()) {
        const spellmodel::Out m = SpellModelAim();
        NoteSpellModel(m);
        AcquireSRWLockShared(&g_stickLock); const StickFrame f = g_stickFrame; ReleaseSRWLockShared(&g_stickLock);
        if (m.src != spellmodel::Src::None && f.valid && g_eyeOk.load()) {
            float dw[3];
            for (int i = 0; i < 3; ++i) dw[i] = m.dir[0] * f.camR[i] - m.dir[2] * f.camF[i];
            dw[1] += m.dir[1];
            const float horiz = sqrtf(dw[0] * dw[0] + dw[2] * dw[2]);
            if (horiz >= 1e-3f) {
                if (dw[1] > -0.08f * horiz) dw[1] = -0.08f * horiz;
                const float l = sqrtf(dw[0] * dw[0] + dw[1] * dw[1] + dw[2] * dw[2]);
                const float upm = std::max(1.0f, ViewHeight()) / UserEyeHeightM();
                for (int i = 0; i < 3; ++i) {
                    d[i] = dw[i] / l;
                    o[i] = g_eyeWorld[i].load() + upm * (m.origin[0] * f.camR[i] - m.origin[2] * f.camF[i]);
                }
                o[1] += upm * m.origin[1];
                return true;
            }
        }
    }
#endif
    const D2RVR_Shared* sh = Shared();
    const aimsource::Out source = StaffAimChoice();
    const bool staff = source.aims;
    const int hand = staff ? source.hand : AimHand();
    if (!sh || sh->version != D2RVR_SHARED_VERSION || !sh->headValid || !(sh->handsValid & (1u << hand)) || !g_eyeOk.load()) return false;
    AcquireSRWLockShared(&g_stickLock); const StickFrame f = g_stickFrame; ReleaseSRWLockShared(&g_stickLock);
    if (!f.valid) return false;
    const float* q = hand == 0 ? sh->rightRot : sh->leftRot;
    const float* p = hand == 0 ? sh->rightHand : sh->leftHand;
    float staffDir[3] = {sh->rightHand[0] - sh->leftHand[0], sh->rightHand[1] - sh->leftHand[1], sh->rightHand[2] - sh->leftHand[2]};
#if D2RVR_FIRST_PERSON
    if (staff && source.src == aimsource::Src::Drawn) skel::StaffAxis(staffDir);
    else if (staff && source.src == aimsource::Src::Shaft) {   // [weight] on, one hand on the staff: the shaft as that controller grips it (fixed by the grip, not by the drawn weapon)
        weight::AimInfo wi;
        if (weight::Aim(&wi) && wi.hand == hand) aimsource::TurnBy(q, wi.shaft, staffDir);
    }
#endif
    NoteAimSource(source, hand);
    // Both lines run from the left hand toward the right one - a staff's way, the
    // right hand ahead. A crossbow has the left ahead and the right on the stock.
    if (staff && XbowLikeStaff()) for (float& v : staffDir) v = -v;
    auto rot = [&](float vx, float vy, float vz, float* r) {   // q v q*
        const float tx = 2 * (q[1]*vz - q[2]*vy), ty = 2 * (q[2]*vx - q[0]*vz), tz = 2 * (q[0]*vy - q[1]*vx);
        r[0] = vx + q[3]*tx + (q[1]*tz - q[2]*ty);
        r[1] = vy + q[3]*ty + (q[2]*tx - q[0]*tz);
        r[2] = vz + q[3]*tz + (q[0]*ty - q[1]*tx);
    };
    float a[3], b[3];
    rot(0, 0, -1, a); rot(0, -1, 0, b);
    // The knuckles (-Y) first: round a bow's grip they point at the target however the
    // bow is turned about the arrow, upright or flat. -Z (a controller held like a gun)
    // only when the knuckles point up or down - it ran along the fist of a flat bow.
    // A javelin or a throwing weapon lies along the fist, as the rod through a
    // closed hand: it flies along +Z (its point is on that side - -Z threw it
    // backwards), never where the knuckles point.
    const uint32_t type = gamestate::WeaponType();
    const bool thrown = type == D2RVR_TYPE_JAVELIN || type == D2RVR_TYPE_THROWING;
    const float rod[3] = {-a[0], -a[1], -a[2]};
#if D2RVR_FIRST_PERSON
    // A crossbow held as the staff shoots along its stock as the model lies (skeletons.cpp
    // GunFrame), like the staff: the model, the shot and the left hand's line are one.
#endif
    const float* h = staff ? staffDir : thrown ? rod : fabsf(b[1]) < 0.7f ? b : a;
    // head frame (x right, y up, z back) -> world: right, up, forward
    float dw[3];
    for (int i = 0; i < 3; ++i) dw[i] = h[0] * f.camR[i] - h[2] * f.camF[i];
    dw[1] += h[1];
    if (dRaw) {
        const float rl = sqrtf(dw[0]*dw[0] + dw[1]*dw[1] + dw[2]*dw[2]);
        for (int i = 0; i < 3; ++i) dRaw[i] = rl > 1e-6f ? dw[i] / rl : 0.0f;
    }
    // [bow] aim_yaw: the shots land a little to one side of where the hand points
    // (the controller's axes are not quite the arrow's), so the aim is turned
    // about the vertical by a set angle. On the ground plane the camera spans,
    // so + is to the right as seen in the headset, whatever the grid does.
    // A staff has its own ([bow] staff_aim_yaw): it aims along the shaft, not the knuckles;
    // and a crossbow ([bow] xbow_aim_yaw): held like a gun, not round a bow's grip.
    const float yawDeg = type == D2RVR_TYPE_CROSSBOW ? g_set.xbowAimYawDeg.load() : staff ? g_set.staffAimYawDeg.load() : g_set.aimYawDeg.load();
    if (yawDeg != 0.0f) {
        float F[3] = {f.camF[0], 0.0f, f.camF[2]};
        const float fl = sqrtf(F[0]*F[0] + F[2]*F[2]);
        float R[3] = {f.camR[0], 0.0f, f.camR[2]};
        const float rl = sqrtf(R[0]*R[0] + R[2]*R[2]);
        if (fl > 1e-3f && rl > 1e-3f) {
            for (int i = 0; i < 3; i += 2) { F[i] /= fl; R[i] /= rl; }
            const float vf = dw[0]*F[0] + dw[2]*F[2], vr = dw[0]*R[0] + dw[2]*R[2];
            const float a = yawDeg * 3.14159265f / 180.0f, c = cosf(a), sn = sinf(a);
            const float nf = vf * c - vr * sn, nr = vf * sn + vr * c;
            dw[0] = nf * F[0] + nr * R[0];
            dw[2] = nf * F[2] + nr * R[2];
        }
    }
    // a ray that never comes down never finds the ground: tip it a little
    const float horiz = sqrtf(dw[0]*dw[0] + dw[2]*dw[2]);
    if (horiz < 1e-3f) return false;
    if (dw[1] > -0.08f * horiz) dw[1] = -0.08f * horiz;
    const float l = sqrtf(dw[0]*dw[0] + dw[1]*dw[1] + dw[2]*dw[2]);
    const float headM = UserEyeHeightM();   // standing, seated or not (see TrueScale)
    const float upm = std::max(1.0f, ViewHeight()) / headM;
    for (int i = 0; i < 3; ++i) {
        d[i] = dw[i] / l;
        o[i] = g_eyeWorld[i].load() + upm * (p[0] * f.camR[i] - p[2] * f.camF[i]);
    }
    o[1] += upm * p[1];
    return true;
}

void AimByMouse(XINPUT_STATE* s) {
    if (g_set.bowMode.load() != 2) {
        if (g_aimDown) { SendKeysMouse(false); g_aimDown = false; }
        return;
    }
    // In a menu A is the menu's own button: trade, inventory, dialogue.
    const bool held = (s->Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0 && !gamestate::MenuOpen();
    float o[3], d[3];
    if (held && HandRay(o, d)) {
        for (int i = 0; i < 3; ++i) { g_aimO[i].store(o[i]); g_aimD[i].store(d[i]); }
        g_aimRay.store(true);
        g_aimRayUntil.store(GetTickCount64() + 200);   // the click is read a little after it is sent
        s->Gamepad.wButtons &= ~XINPUT_GAMEPAD_A;
        if (g_aimDown && g_aimStage == 1 && GetTickCount64() - g_aimStageAt >= 20) { SendLeftButton(true); g_aimStage = 2; }
        if (!g_aimDown && GameFocused()) {
            SendKeysMouse(true);
            g_aimDown = true;
            LogF("aim: attack along the %s hand, ray %.2f %.2f %.2f from %.1f %.1f %.1f",
                 AimHand() ? "left" : "right", d[0], d[1], d[2], o[0], o[1], o[2]);
        }
    } else if (g_aimDown) {
        SendKeysMouse(false);
        g_aimDown = false;
    }
    static bool toldWhy = false;
    const bool pressed = (s->Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0;
    if (pressed && !g_aimDown && !toldWhy) {
        toldWhy = true;
        LogF("aim: A left to the game - %s", gamestate::MenuOpen() ? "a menu is open" : "no hand ray (hand not tracked, or no camera yet)");
    } else if (!pressed) toldWhy = false;
}

// Attack along the hand, pad only (bow mode 3). While A is held the game
// attacks where the hero faces and never reads the stick; without A the stick
// turns him at once. So a press of A is held back for turn_ms while the stick
// points along the aiming hand (he turns, maybe half a step), then the stick
// goes quiet and A goes through: the shot or blow leaves the way he now faces.
// Held A keeps attacking that way; when the hand swings more than 25 degrees
// off, A is held back again for another short turn. No mouse or keyboard, so
// the game's UI stays in controller mode (mode 2 flipped it to mouse).
//
// The hand's pointing, flattened, as a stick relative to the view: the grip's
// -Z held like a gun, -Y (wrist to knuckles) round a bow's upright grip.
bool HandStick(float* sx, float* sy) {
    const D2RVR_Shared* sh = Shared();
    const int hand = AimHand();
    if (!sh || sh->version != D2RVR_SHARED_VERSION || !sh->headValid || !(sh->handsValid & (1u << hand))) return false;
    const float* q = hand == 0 ? sh->rightRot : sh->leftRot;
    auto rot = [&](float vx, float vy, float vz, float* r) {   // q v q*
        const float tx = 2 * (q[1]*vz - q[2]*vy), ty = 2 * (q[2]*vx - q[0]*vz), tz = 2 * (q[0]*vy - q[1]*vx);
        r[0] = vx + q[3]*tx + (q[1]*tz - q[2]*ty);
        r[1] = vy + q[3]*ty + (q[2]*tx - q[0]*tz);
        r[2] = vz + q[3]*tz + (q[0]*ty - q[1]*tx);
    };
    float a[3], b[3];
    rot(0, 0, -1, a); rot(0, -1, 0, b);
    // The knuckles (-Y) first: round a bow's grip they point at the target however the
    // bow is turned about the arrow, upright or flat. -Z (a controller held like a gun)
    // only when the knuckles point up or down - it ran along the fist of a flat bow.
    const float* h = fabsf(b[1]) < 0.7f ? b : a;
    const float x = h[0], fwd = -h[2];   // head frame: x right, z back
    const float l = sqrtf(x*x + fwd*fwd);
    if (!(l > 0.05f)) return false;
    *sx = x / l; *sy = fwd / l;
    return true;
}

struct PreTurn { int stage = 0; ULONGLONG at = 0; float sx = 0, sy = 0; } g_pre;   // stage 0 idle, 1 turning, 2 attacking

void AimByTurn(XINPUT_STATE* s) {
    if (g_set.bowMode.load() != 3) { g_pre.stage = 0; return; }
    XINPUT_GAMEPAD& g = s->Gamepad;
    const bool held = (g.wButtons & XINPUT_GAMEPAD_A) != 0 && !gamestate::MenuOpen();
    if (!held) { g_pre.stage = 0; return; }
    float sx, sy;
    const bool hand = HandStick(&sx, &sy);
    const ULONGLONG now = GetTickCount64();
    if (g_pre.stage == 0 || (g_pre.stage == 2 && hand && sx * g_pre.sx + sy * g_pre.sy < 0.906f)) {   // cos 25 deg
        if (!hand) { g_pre.stage = 2; return; }   // no hand to aim with: A as it is
        g_pre = {1, now, sx, sy};
        LogF("aim: turning to the %s hand (stick %.2f %.2f) before the attack", AimHand() ? "left" : "right", sx, sy);
    }
    if (g_pre.stage == 1) {
        if (now - g_pre.at < (ULONGLONG)g_set.turnMs.load()) {
            g.wButtons &= ~XINPUT_GAMEPAD_A;
            g.sThumbLX = (SHORT)std::clamp(g_pre.sx * 32767.0f, -32768.0f, 32767.0f);
            g.sThumbLY = (SHORT)std::clamp(g_pre.sy * 32767.0f, -32768.0f, 32767.0f);
            return;
        }
        g_pre.stage = 2;
    }
    // attacking: the stick stays quiet so he does not walk off the aim
    g.sThumbLX = 0; g.sThumbLY = 0;
}

// The controller's target vector (bow mode 4, and its log in every mode).
//
// ControllerInputHandler builds the point an attack goes to in one function
// (RVA 0x143960, D2RLoader layout): from a start - the hero, or the current
// free-target point - it steps along a direction vector, float x/y in GAME
// (grid) coordinates, up to 11 cells, checking the ground, and writes the
// point to two ints. The vector is the controller manager's slot field +0x195C
// (the left stick turned onto the grid); its callers keep the point at
// manager+0x39BD4. Found 2026-10-02 from the controllersettings.json fields
// freeTargetMovementPerMS / freeTargetStickyPullFactor (manager +0x1F98/+0x1F9C).
//
// Mode 4 hands it the aiming hand's direction instead while A is held. Game
// and world axes differ by a fixed turn (and maybe a mirror), learned while
// the hero walks: the vector the stick gives against the way he then moves.
const uint8_t kSigAttackTarget[16] = {0x48,0x8B,0xC4,0x48,0x89,0x58,0x10,0x48,0x89,0x70,0x18,0x4C,0x89,0x70,0x20,0x41};
AttackTargetFn OrigAttackTarget = nullptr;

std::atomic<bool> g_aHeld{false};            // A is down (and no menu), from the pad
std::atomic<float> g_walkTheta{0.0f};        // the hero's walking direction in the world, atan2(dz, dx)
std::atomic<bool> g_walkSteady{false};
SRWLOCK g_calLock = SRWLOCK_INIT;
Calib g_cal;

// [bow] pitch_aim (vr/bow_pitch_wire.h): a line of the hands' frame (metres from the eyes, the head's turn: s a point, d a direction) in the render world, metres -
// where the drawn hands are put (monsterbodies::HeadToRender: the eye, the camera's right and forward on the ground, up as it is).
bool HandsLineToRender(const StickFrame& f, const float s[3], const float d[3], float o[3], float w[3]) {
    if (!f.valid || !g_eyeOk.load()) return false;
    monsterbodies::View v;
    for (int i = 0; i < 3; ++i) { v.eye[i] = g_eyeWorld[i].load(); v.camR[i] = f.camR[i]; v.camF[i] = f.camF[i]; }
    v.upm = std::max(1.0f, ViewHeight()) / UserEyeHeightM();
    if (!monsterbodies::HeadToRender(v, s, o)) return false;
    for (int i = 0; i < 3; ++i) w[i] = d[0] * f.camR[i] - d[2] * f.camF[i] + (i == 1 ? d[1] : 0.0f);
    return std::isfinite(w[0] + w[1] + w[2]);
}

extern float g_heroPosNow[3];   // (defined with the hero's facing, below)
extern std::atomic<bool> g_skillHeld;   // (below)

// [bow] arrow_flight (vr/arrow_flight_wire.h): the shot's line (render metres), the heading the game is given and what pitch_aim met - our arrow flies it
void NoteArrow(unsigned shot, const float o[3], const float w[3], float theta, const bowpitch::Result* met) {
    if (!arrowflightwire::On()) return;
    const float hero[3] = {g_heroPosNow[0], g_heroPosNow[1], g_heroPosNow[2]};
    arrowflightwire::NoteShot(shot, o, w, theta, met, hero, std::max(1.0f, ViewHeight()) / UserEyeHeightM());
}

// [hands] bow_draw: the line of a released draw (vr/bow_draw_wire.cpp), in the head's turn frame, on the ground in world
// terms - the same turn into the world as a hand's (HandRay: x along the camera's right, z back along its forward).
bool DrawWorldTheta(float* theta) {
    float h[3];
    if (!bowdrawwire::AimDir(h)) return false;
    AcquireSRWLockShared(&g_stickLock); const StickFrame f = g_stickFrame; ReleaseSRWLockShared(&g_stickLock);
    if (!f.valid) return false;
    float s[3], d[3], o[3], w[3];
    const bool line = (bowpitchwire::On() || arrowflightwire::On()) && bowdrawwire::AimLine(s, d) && HandsLineToRender(f, s, d, o, w);
    if (line && bowpitchwire::On()) {   // [bow] pitch_aim: the whole line, from the arrow's release point - the heading of the cell of what it meets (the stairs)
        float t;
        bowpitch::Result met;
        if (bowpitchwire::Theta(o, w, bowdrawwire::ShotCount(), &t, &met)) {
            *theta = t;
            NoteArrow(bowdrawwire::ShotCount(), o, w, t, &met);   // [bow] arrow_flight: ours flies to what the line met
            return true;
        }
    }
    const float dx = h[0] * f.camR[0] - h[2] * f.camF[0], dz = h[0] * f.camR[2] - h[2] * f.camF[2];
    if (!std::isfinite(dx) || !std::isfinite(dz) || dx * dx + dz * dz < 1e-6f) return false;
    *theta = atan2f(dz, dx);
    if (line) NoteArrow(bowdrawwire::ShotCount(), o, w, *theta, nullptr);   // [bow] arrow_flight: ours leaves the bow and joins the game's path
    return true;
}

// The aiming hand's direction on the ground, in world terms: atan2(z, x). None for a ray
// straight up or down (no direction on the ground) or not finite: a NaN from the bridge passes
// HandRay's test (it is not below it) and would go on to the game's free target (2026-10-13).
bool GestureTheta(float* theta);   // [hands] throw_gesture, below
bool HandWorldTheta(float* theta) {
    if (GestureTheta(theta)) return true;   // a throw by gesture is on its way: the hand's motion, not its pointing
    float o[3], d[3], dRaw[3] = {NAN, NAN, NAN};   // (HandRay's spell-model branch leaves it unset: then no pitch below)
    if (DrawWorldTheta(theta)) return true;   // [hands] bow_draw: a released draw's line, not the hand's
    if (!HandRay(o, d, dRaw) || !std::isfinite(d[0]) || !std::isfinite(d[2]) || d[0] * d[0] + d[2] * d[2] < 1e-6f) return false;
    *theta = atan2f(d[2], d[0]);
    const uint32_t wc = gamestate::WeaponClass();
    if ((bowpitchwire::On() || arrowflightwire::On()) && (wc == D2RVR_WEAPON_BOW || wc == D2RVR_WEAPON_CROSSBOW) && std::isfinite(dRaw[0] + dRaw[1] + dRaw[2])) {
        // [bow] pitch_aim: a crossbow, or a bow aimed by the hand (no draw) - its line as the hand points it ([bow] aim_yaw's heading, the hand's own pitch:
        // not the tip toward the ground HandRay adds) to the cell of what it meets
        const float upm = std::max(1.0f, ViewHeight()) / UserEyeHeightM();
        const float hd = sqrtf(d[0] * d[0] + d[2] * d[2]), hr = sqrtf(dRaw[0] * dRaw[0] + dRaw[2] * dRaw[2]);
        const float ro[3] = {o[0] / upm, o[1] / upm, o[2] / upm}, rw[3] = {d[0] / hd * hr, dRaw[1], d[2] / hd * hr};
        float t;
        bowpitch::Result met;
        const bool turned = bowpitchwire::On() && bowpitchwire::Theta(ro, rw, 0, &t, &met);
        if (turned) *theta = t;
        // [bow] arrow_flight: the shot of a press (A or a skill held: the game is reading its aim) - the arrow leaves the hand along this line
        if (g_aHeld.load() || g_skillHeld.load()) NoteArrow(0, ro, rw, *theta, turned ? &met : nullptr);
    }
    return true;
}

// The aiming hand as a unit vector on the game's grid. The grid's axes are the
// world's x and z (measured 2026-10-02: 0 deg, not mirrored, agreement 0.98);
// what the walk teaches overrides that once it is sure - not in native OpenXR, where
// the walk-learned turn is only logged (vr/aim_point.h, GridDir, says why).
bool HandGridDir(float* gx, float* gy) {
    float theta;
    if (!HandWorldTheta(&theta)) return false;
    AcquireSRWLockShared(&g_calLock); const Calib c = g_cal; ReleaseSRWLockShared(&g_calLock);
    aimpoint::GridDir(theta, xr::Running(), c.ok, c.mirror != 0, c.angle, gx, gy);
    return true;
}

// A skill slot's control is down (A, B, X, Y, RB or the right trigger: vr/pad_skill.h), no menu: the attack or spell it casts goes along the hand.
std::atomic<bool> g_skillHeld{false};
// [hands] throw_gesture (vr/throw_gesture_wire.cpp): a javelin thrown by the hand's motion presses the game's own attack once
// at the release; for the ticks the game takes to read it, the point of that attack is out along the motion. The heading
// comes in the head's own frame (right, forward) and is made the world's with the camera's axes, as HandRay does the hand's.
bool GestureAimLive() {
    float r, f;
    return g_set.throwGesture.load() && throwwire::Aim(&r, &f);
}
bool GestureTheta(float* theta) {
    float right, fwd;
    if (!g_set.throwGesture.load() || !throwwire::Aim(&right, &fwd)) return false;
    AcquireSRWLockShared(&g_stickLock); const StickFrame f = g_stickFrame; ReleaseSRWLockShared(&g_stickLock);
    if (!f.valid) return false;
    const float x = right * f.camR[0] + fwd * f.camF[0], z = right * f.camR[2] + fwd * f.camF[2];
    if (!std::isfinite(x) || !std::isfinite(z) || x * x + z * z < 1e-6f) return false;
    *theta = atan2f(z, x);
    throwwire::NoteRead();   // (the wire logs when the game never asks for the aim of a throw's press)
    return true;
}
bool GestureCanAim() {
    AcquireSRWLockShared(&g_stickLock); const bool ok = g_stickFrame.valid; ReleaseSRWLockShared(&g_stickLock);
    return ok;
}
// [hands] bow_draw: the 0.4 s after a released draw go along its line whatever [bow] mode says.
// VR F1 and F3 (top-down) never (vr/attack_view.h): there the hero hits where HE faces - the game's own aim, the hand and the head turn nothing (the log of
// 2026-10-15 had "attack vector 0.62 0.79 -> the hand -0.79 -0.62" and 53 "TURNED to the hand" in F1 and F3). VR F2 and F4 look where the player looks, as
// they did; the flat game is as it was.
bool AimByVector() {
    return attackview::HandMayAim(g_set.platform.load(), g_set.vrView.load()) && g_enabled.load() &&
           ((g_set.bowMode.load() == 4 && (g_aHeld.load() || g_skillHeld.load() || GestureAimLive())) || bowdrawwire::AimLive());
}

// The other half of the controller's aim. With the stick at rest the aim
// function (0x1446C0) takes the attack's direction from the hero's own facing:
// UnitFacingVector (RVA 0x349EF0, (unit, float out[2])), called at 0x14481F.
// For that call only, and only while A is held in mode 4, the answer is the
// aiming hand - the real vector, so turning the hand while firing turns the
// shots, with no step. Everyone else (the other caller, 0x18BD6D) gets the facing.
const uint8_t kSigUnitFacing[16] = {0x48,0x89,0x5C,0x24,0x10,0x57,0x48,0x83,0xEC,0x40,0x0F,0x29,0x74,0x24,0x30,0x48};
UnitFacingFn OrigUnitFacing = nullptr;

// The hero's facing as the game's logic has it (UnitFacingVector's answer for
// the hero, the unit the aim works on), with when it was asked - for the frame
// log: is it this frame's on the first pass of a pair, where the render matrix
// is a frame old, and does it say where the model is drawn facing?
std::atomic<void*> g_heroUnit{nullptr};
// The hero's mode now (the client's player unit the aim function last worked on: +0xC, as every unit has it): the game's answer to a throw's press.
bool GestureHeroMode(uint32_t* mode) {
    const uint8_t* u = (const uint8_t*)g_heroUnit.load();
    if (!u || (uintptr_t)u < 0x10000) return false;
    __try {
        if (*(const uint32_t*)u != 0) return false;   // not a player
        *mode = *(const uint32_t*)(u + 0xC);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
// The hero's client unit id (potion hide: whose missile is the hero's): the player unit the aim function last worked on. False when it is not known.
bool GestureHeroId(uint32_t* id) {
    const uint8_t* u = (const uint8_t*)g_heroUnit.load();
    if (!u || (uintptr_t)u < 0x10000) return false;
    __try {
        if (*(const uint32_t*)u != 0) return false;   // not a player
        *id = *(const uint32_t*)(u + 8);
        return *id != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
// The hero unit found without an attack, for the cave ceiling's torches (worldobj): while
// it is wanted, every unit the game asks the facing of is tried - a player (type 0)
// whose place on its path, times 2, is the look-at (2026-10-07: "on entering the cave").
std::atomic<void*> g_heroSeen{nullptr};
std::atomic<bool> g_heroWanted{false};

bool IsHeroUnit(const void* unit) {
    float L[3];
    AcquireSRWLockShared(&g_lookLock); memcpy(L, g_lookAt, sizeof L); ReleaseSRWLockShared(&g_lookLock);
    __try {
        const uint8_t* u = (const uint8_t*)unit;
        if (*(const uint32_t*)u != 0) return false;
        const uint8_t* path = *(const uint8_t* const*)(u + 0x38);
        if ((uintptr_t)path < 0x10000) return false;
        const float x = 2.0f * *(const uint16_t*)(path + 2), z = 2.0f * *(const uint16_t*)(path + 6);
        return fabsf(x - L[0]) < 3.0f && fabsf(z - L[2]) < 3.0f;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
std::atomic<float> g_logicFacingDeg{0.0f};
std::atomic<LONGLONG> g_logicFacingAt{0};

// The hero's facing asked of the game right now (UnitFacingVector on the hero
// unit), guarded: the unit pointer is the one the aim last worked on, and a
// stale one after an area change must not take the game down. false = none.
bool AskHeroFacingNow(float* deg) noexcept {
    void* unit = g_heroUnit.load();
    if (!unit || !OrigUnitFacing) return false;
    float out[2] = {0.0f, 0.0f};
    __try { OrigUnitFacing(unit, out); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    if (!std::isfinite(out[0]) || !std::isfinite(out[1]) || (out[0] == 0.0f && out[1] == 0.0f)) return false;
    *deg = atan2f(out[1], out[0]) * 57.2957795f;
    return true;
}

LONGLONG QpcUs() {
    static LARGE_INTEGER f{};
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return c.QuadPart * 1000000 / f.QuadPart;
}

void HookUnitFacing(void* unit, float* out) {
    frametrace::Scope trace(frametrace::kAimHooks);   // [debug] frame_trace: ours, the game's call left out
    trace.Pause();
    OrigUnitFacing(unit, out);
    trace.Resume();
    if (unit && g_heroWanted.load() && !g_heroSeen.load() && IsHeroUnit(unit)) g_heroSeen.store(unit);   // until one is found
    if (out && unit && unit == g_heroUnit.load()) {
        g_logicFacingDeg.store(atan2f(out[1], out[0]) * 57.2957795f);
        g_logicFacingAt.store(QpcUs());
    }
    if (!out || (uintptr_t)_ReturnAddress() != d2rsig::Addr(RVA_AIM_FACING_RET) || !AimByVector()) return;
    float gx, gy;
    if (!HandGridDir(&gx, &gy)) return;
    static ULONGLONG told = 0;
    if (GetTickCount64() - told > 250) {
        told = GetTickCount64();
        LogF("target: attack vector %.2f %.2f -> the hand %.2f %.2f", out[0], out[1], gx, gy);
    }
    out[0] = gx; out[1] = gy;
}

// [hands] spell_aim: a spell that flies (Fire Bolt, Charged Bolt, Teeth...) is never turned to a
// monster, in mode 4 while its button is held. The controller's cast resolver (0x190440,
// (manager, unit, assist, float* x, float* y)) sends a cast to the unit the controller has
// picked for the skill (0x18DDE0, called at 0x19057A) or, failing that, the one under its
// cursor (HoverUnit, at 0x190583) - only with neither to AttackPoint along the aim (at 0x1906E2);
// then, with `assist`, it may still bend the point toward a monster near it. For a skill the
// game's Skills table says flies (gamestate::SkillAimed), and that one call only: those two
// lookups find nothing and the assist is off - the cast takes AttackPoint along the aim the
// attack along the hand already gives every attack (HookUnitFacing, HookAttackPoint). bow_aim and
// throw_aim also put the point aimpoint::kMaxCells (40) out along the hand (whole cells, so far out the
// steps are fine). Mana, cast rate, the missile: the game's own.
// From bearstonem/d2r-pcvr (MIT, a fork of 0.152), which found the resolver and its call sites, and that the
// game's check of a skill's point refuses one more than 50 cells from the hero on either axis (vr/aim_point.h):
// a point 60 out was refused along every heading within 34 degrees of a cardinal one, and the aim stayed stale.
// directed: the target lookups find nothing, the assist is off. point: the point too is ours, aimpoint::kMaxCells out
// (bow_aim, throw_aim); a spell keeps the game's aim, turned to the hand as every attack in mode 4 is (the user,
// 2026-10-13: "the old aim worked, just keep it from the monsters").
// leap: [gameplay] leap_by_gaze - a Leap or Leap Attack whose point is the gaze's (LeapPointFor, by the spell resolver).
thread_local SpellCast g_spellCast;   // the resolution under way on this thread
uintptr_t g_spellPointRet = 0;        // where the resolver's AttackPoint call returns (set before its hook goes in)
bool LeapPointFor(const float from[2], const float game[2], float* px, float* py);   // [gameplay] leap_by_gaze, by the spell resolver

// The point the attack goes to: AttackPoint (RVA 0x18BAD0, (manager, float
// out[2], unit, float2 from, float2 dir)) = from + dir * range, with range the
// default attack distance (~3 cells) or the skill's. The command that carries
// it rounds to whole cells, and at 3 cells that is a 20-degree staircase - the
// shots went in steps, not where the bow pointed (0.22 test). With a bow or
// crossbow in hand, while A is held in mode 4, the point goes [bow] range
// cells out along the hand instead: steps of about 3 degrees, and an arrow
// flies on past its point anyway. A melee weapon keeps the game's point, so
// the hero does not walk off towards a far one.
const uint8_t kSigAttackPoint[16] = {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57};
AttackPointFn OrigAttackPoint = nullptr;

bool ThrownInHand();   // (below, by the spell resolver)

// [hands] throw_gesture / throw_by_grip: the point the game's attack takes for a throw released a moment ago (vr/throw_aim.h). A javelin or a
// throwing weapon: throwaim::kMaxCells (48) out along the line the hand was going - or, with [hands] throw_autoaim_deg above 0, at the hostile
// monster nearest that line inside that half-angle (the monsters the melee touch reads, the server's own hostility and wall check). A throwing
// potion: a ground location - the weapon-weight model's landing when it has the throw (vr/throw_source.h; already scaled by [weight]
// throw_reach_pct, NOT scaled again here), else where a real throw of the hand's peak velocity would land (its height, [hands]
// throw_potion_reach_pct) - held to [3, 48] cells; the monster snap counts only those within +-25% of that distance. The HEADING is the weight
// model's too when it has the throw (throwwire::Info / Aim). 0: no throw is live - the old paths go on. 1: *px, *py is the point. 2: a throw is
// live but no point can be made (no room before the map's edge, or nearer than aimpoint's 3 cells): the game's own point stays, and the old
// 40-cell paths must NOT go on (a throw is not to fly to the far end of its line because it could not be placed near).
// The point goes to the game's own attack through the aim hooks; its range, line of sight and relation checks stay the game's.
int GesturePoint(const float from[2], float gx, float gy, float* px, float* py) {
    throwwire::ThrowInfo ti;
    if (!g_set.throwGesture.load() || !ThrownInHand() || !throwwire::Info(&ti)) return 0;
    const bool potion = gamestate::HandIsThrowPotion((int)gamestate::WeaponHand());
    const float eye = UserEyeHeightM();
    const float upm = std::max(1.0f, ViewHeight()) / eye;
    float cells = throwaim::kMaxCells;
    throwaim::Reach reach;
    float handM = 0.0f;
    if (potion) {
        handM = std::max(0.05f, eye - ti.handBelowHeadM);
        // the weight model's landing is final (its reach scale applied, its cap held); the hand's own estimate is scaled by throw_potion_reach_pct here
        reach = throwaim::ThrowReach(ti.fromWeight ? ti.landingM : 0.0f, ti.horizSpeed, ti.vertSpeed, handM, g_set.throwPotionReachPct.load(), upm);
        cells = reach.cells;
    }
    throwaim::Pick pick;
    size_t seen = 0;
    bool polled = false;
    const float cone = g_set.throwAutoaimDeg.load();
    if (cone > 0.0f) {
        meleetouch::ThrowMonster mons[48];
        throwaim::Monster cm[48];
        seen = meleetouch::ThrowTargets(mons, 48, &polled);
        for (size_t i = 0; i < seen; ++i) cm[i] = {mons[i].id, mons[i].x * 0.5f, mons[i].z * 0.5f};   // world units -> cells
        float lo = 1.0f, hi = throwaim::kMaxCells;
        if (potion) { throwaim::PotionWindow(cells, &lo, &hi); hi = std::min(hi, throwaim::kMaxCells); }
        pick = throwaim::PickInCone(from[0], from[1], gx, gy, cone, lo, hi, cm, seen);
    }
    bool ok = true;
    if (pick.found) { *px = pick.x; *py = pick.y; }
    else ok = aimpoint::Point(from[0], from[1], gx, gy, cells, px, py, throwaim::kMaxCells);
    static unsigned toldSeq = 0;
    static bool toldWeight = false;
    if (ti.seq != toldSeq || ti.fromWeight != toldWeight) {   // once per throw (and again if the weight model's account came in after the first read): what decided
        toldSeq = ti.seq;
        toldWeight = ti.fromWeight;
        char src[200];
        if (ti.fromWeight)
            snprintf(src, sizeof src, "SOURCE weight model (%.1f m/s, flight %.2f s, %+.0f deg up, lands %.1f m%s%s)", ti.weightSpeed, ti.flightS, ti.elevationDeg, ti.landingM,
                     ti.weightCapped ? ", its cell cap held" : "", ti.headingFromVelocity ? ", heading from its velocity" : "");
        else
            snprintf(src, sizeof src, "SOURCE hand estimate (the weight model had no valid outcome for this throw, or [weight] is off)");
        const float finalCells = pick.found ? pick.cells : cells;
        char how[200];
        if (pick.found) snprintf(how, sizeof how, "snapped to monster %u, %.1f deg off the line, %.1f cells out", pick.id, pick.angleDeg, pick.cells);
        else if (cone <= 0.0f) snprintf(how, sizeof how, "along the hand (auto-aim off)");
        else snprintf(how, sizeof how, "along the hand (%u hostile monster(s) read%s, none in the %.0f deg cone%s)", (unsigned)seen,
                      polled ? "" : " - no poll lately", cone, potion ? " and distance window" : "");
        const char* refused = ok ? "" : " - the point was refused (the map's edge, or nearer than 3 cells): the game's own point stays";
        if (potion && ti.fromWeight)
            LogF("throw gesture: potion - %s: the landing is final (not scaled again) = %.1f m at %.1f units/m = %.1f cells%s; %s; FINAL distance %.1f cells (%.1f m)%s",
                 src, reach.metres, upm, cells, reach.clamped ? " (clamped to 3..48)" : "", how, finalCells, finalCells * 2.0f / upm, refused);
        else if (potion)
            LogF("throw gesture: potion - %s: hand peak %.1f m/s (%.1f ground, %+.1f up; %.1f at the release), %.2f m high: lands %.1f m, x%.0f%% at %.1f units/m = %.1f cells%s; %s; FINAL distance %.1f cells (%.1f m)%s",
                 src, ti.speed, ti.horizSpeed, ti.vertSpeed, ti.releaseSpeed, handM, reach.metres, g_set.throwPotionReachPct.load(), upm, cells,
                 reach.clamped ? " (clamped to 3..48)" : "", how, finalCells, finalCells * 2.0f / upm, refused);
        else
            LogF("throw gesture: %s: %s; FINAL distance %.1f cells (%.1f m)%s", src, how, finalCells, finalCells * 2.0f / upm, refused);
    }
    return ok ? 1 : 2;
}

// The distance of the plain aim point (the attack's own point along the hand, mode 4) for the weapon in hand: [bow] range, or a staff's / crossbow's by the hands on it
// (`twoHands`, when asked: both hands are on it).
float AimRangeCells(uint32_t type, bool* twoHands = nullptr) {
    float range = g_set.aimRange.load();
    if (twoHands) *twoHands = false;
    if (StaffAim()) {
        const bool xbow = type == D2RVR_TYPE_CROSSBOW;
        bool leftOn = true;   // both controllers, the staff between them: always both
#if D2RVR_FIRST_PERSON
        if (g_set.staffFreeLeft.load() && ArmsMode() == 2) {
            const aimsource::Out as = StaffAimChoice();   // [weight] on: the model's hands on the staff; off: the skeleton hook's answer
            if (as.src == aimsource::Src::Drawn) skel::StaffAxis(nullptr, nullptr, &leftOn);
            else leftOn = as.leftOn;
        }
#endif
        range = leftOn ? (xbow ? g_set.xbowRangeTwo : g_set.staffRangeTwo).load() : (xbow ? g_set.xbowRangeOne : g_set.staffRangeOne).load();
        if (twoHands) *twoHands = leftOn;
    }
    return range;
}

#if D2RVR_FIRST_PERSON
bool BowInHandNow() { const uint32_t w = gamestate::WeaponClass(); return w == D2RVR_WEAPON_BOW || w == D2RVR_WEAPON_CROSSBOW; }
// [bow] aim_distance_by_arm (vr/arm_reach.h): how far the aiming arm is stretched along the aim, 0..1, from the aiming hand's ray (HandRay: the grip and the way it points) and the
// drawn arm's own shoulder and length (skel::ArmReach, in the head's turn-only frame, carried to the world as HandRay does the grip). The hand HandRay uses: the aiming hand; a
// staff held in one hand aimed along its shaft is the right one's. False in anything but VR first person, or with no fresh pose of the arms.
bool ArmExtensionNow(float* e) {
    if (!viewgate::F4() || !g_eyeOk.load()) return false;
    float o[3], d[3], dRaw[3];
    if (!HandRay(o, d, dRaw)) return false;   // the arm is measured along the hand's own pointing: [bow] aim_yaw turns the shot, not the arm
    float S[3], len = 0.0f;
    if (!skel::ArmReach(StaffAim() ? 0 : AimHand(), S, &len)) return false;
    AcquireSRWLockShared(&g_stickLock); const StickFrame f = g_stickFrame; ReleaseSRWLockShared(&g_stickLock);
    if (!f.valid) return false;
    const float upm = std::max(1.0f, ViewHeight()) / UserEyeHeightM();
    float shoulder[3];
    for (int i = 0; i < 3; ++i) shoulder[i] = g_eyeWorld[i].load() + upm * (S[0] * f.camR[i] - S[2] * f.camF[i]);
    shoulder[1] += upm * S[1];
    return armreach::Extension(o, shoulder, dRaw, len * upm, e);
}
SRWLOCK g_armLock = SRWLOCK_INIT;
armreach::Smoother g_armSmooth;
std::atomic<float> g_armLiveExt{-1.0f}, g_armLiveCells{0.0f};   // the last value the point had (for the log and the debug rays); ext < 0: never
std::atomic<ULONGLONG> g_armLiveAt{0};

// The aim point's distance of a SPELL (a wand, an orb, a staff with one hand on it, bare hands - never a bow, a crossbow or a staff held in both hands: the callers ask only for these).
// `baseCells`: what the weapon asks today. With [bow] aim_distance_by_arm, in VR first person, with a fresh pose of the arms: between [bow] aim_near_cells (the arm at the body) and
// `farCells` (the arm stretched along the aim; for a staff the two-hand distance, staff_range_two, the most it ever aims), by the smoothed extension; else baseCells, as before.
float ArmAimCells(float baseCells, float farCells) {
    if (!g_set.aimByArm.load()) return baseCells;
    float e;
    if (!ArmExtensionNow(&e)) return baseCells;
    AcquireSRWLockExclusive(&g_armLock);
    const float smooth = g_armSmooth.Step(e, QpcUs() * 1e-6);
    ReleaseSRWLockExclusive(&g_armLock);
    const float cells = armreach::Cells(smooth, g_set.aimNearCells.load(), farCells);
    g_armLiveExt.store(smooth);
    g_armLiveCells.store(cells);
    g_armLiveAt.store(GetTickCount64());
    static ULONGLONG told = 0;
    if (GetTickCount64() - told > 1000) {
        told = GetTickCount64();
        LogF("aim by arm: the arm is %.0f%% stretched along the aim (%.0f%% now) -> the spell's aim point %.1f cells out (between %.1f at the body and %.1f, the weapon's own)", smooth * 100.0f, e * 100.0f, cells,
             std::min(g_set.aimNearCells.load(), farCells), farCells);
    }
    return cells;
}
// The far end of a staff under the arm: the two-hand distance, the most it ever aims (a staff in both hands always aims there), never nearer than what it asks with one.
float StaffFarCells(float oneHandCells) { return std::max(oneHandCells, g_set.staffRangeTwo.load()); }
// For the debug ray of a spell: the distances the arm decides between, for the weapon in hand - a wand, an orb, bare hands or a staff not aimed as one (HookAttackPoint's second branch) and
// a staff aimed along its shaft with one hand on it (its third). False for everything else - a bow, a crossbow, a staff in both hands, a thrown weapon.
bool SpellArmCells(float* base, float* farCells) {
    const uint32_t t = gamestate::WeaponType();
    if (BowInHandNow() || t == D2RVR_TYPE_CROSSBOW || t == D2RVR_TYPE_JAVELIN || t == D2RVR_TYPE_THROWING) return false;
    if (SpellModelWanted() && !StaffAim()) {
        const spellmodel::Hold hold = t == D2RVR_TYPE_STAFF ? spellmodel::Hold::Staff : t == D2RVR_TYPE_WAND || t == D2RVR_TYPE_ORB ? spellmodel::Hold::OneHandCaster : spellmodel::Hold::Hand;
        *base = *farCells = spellmodel::Cells(hold, g_set.handRange.load(), g_set.casterRange.load(), g_set.staffRangeOne.load());
        if (hold == spellmodel::Hold::Staff) *farCells = StaffFarCells(*base);
        return true;
    }
    bool two = false;
    const float range = AimRangeCells(t, &two);
    if (!StaffAim() || t != D2RVR_TYPE_STAFF || two) return false;
    *base = range;
    *farCells = StaffFarCells(range);
    return true;
}
// For the debug rays: the end of the ray in metres on the ground along its direction - the live aim distance, or 0 when the feature is off or the arm is not read now.
float ArmRayMetres(float baseCells, float farCells) {
    if (!g_set.aimByArm.load() || !viewgate::F4() || !(farCells > 0.0f)) return 0.0f;
    const float cells = ArmAimCells(baseCells, farCells);
    if (GetTickCount64() - g_armLiveAt.load() > 300) return 0.0f;
    return armreach::CellsToMetres(cells, std::max(1.0f, ViewHeight()) / UserEyeHeightM());
}
#else
float ArmAimCells(float baseCells, float) { return baseCells; }
float StaffFarCells(float oneHandCells) { return oneHandCells; }
float ArmRayMetres(float, float) { return 0.0f; }
bool SpellArmCells(float*, float*) { return false; }
#endif

float* HookAttackPoint(void* mgr, float* out, void* unit, uint64_t from, uint64_t dir) {
    frametrace::Scope trace(frametrace::kAimHooks);   // [debug] frame_trace: ours, the game's call left out
    trace.Pause();
    float* r = OrigAttackPoint(mgr, out, unit, from, dir);
    trace.Resume();
    if (out && g_spellCast.leap && (uintptr_t)_ReturnAddress() == g_spellPointRet) {   // [gameplay] leap_by_gaze: the leap's point is where the head looks
        float f[2]; memcpy(f, &from, sizeof f);
        float px, py;   // none: the game's own point stays
        if (LeapPointFor(f, out, &px, &py)) { out[0] = px; out[1] = py; }
        return r;
    }
    if (out && g_spellCast.directed && g_spellCast.point && (uintptr_t)_ReturnAddress() == g_spellPointRet) {   // bow_aim's, throw_aim's shot
        float f[2]; memcpy(f, &from, sizeof f);
        float px, py;   // none (no room before the edge of the coordinates): the game's own point stays
        if (const int g = GesturePoint(f, g_spellCast.gx, g_spellCast.gy, &px, &py)) {   // a throw by gesture
            if (g == 1) { out[0] = px; out[1] = py; }
            return r;
        }
        if (aimpoint::Point(f[0], f[1], g_spellCast.gx, g_spellCast.gy, aimpoint::kMaxCells, &px, &py)) { out[0] = px; out[1] = py; }   // (a bow's, a javelin's: the fixed distance; [bow] aim_distance_by_arm is for spells)
        return r;
    }
#if D2RVR_FIRST_PERSON
    // [hands] spell_aim_model: a spell that goes along the hand (spell_aim's directed cast, the game's aim turned to the hand) gets its point as far out as
    // what the hero holds asks (vr/spell_aim_model.h Cells): a wand or orb [bow] caster_range, bare hands [bow] hand_range - the game's ~3 cells were a
    // 20-degree staircase. A staff aimed as one (StaffAim) keeps staff_range_one / _two below; one that is not, staff_range_one. Too near or no room: the game's.
    if (out && g_spellCast.directed && !g_spellCast.point && (uintptr_t)_ReturnAddress() == g_spellPointRet && SpellModelWanted() && !StaffAim()) {
        const uint32_t t = gamestate::WeaponType();
        const spellmodel::Hold hold = t == D2RVR_TYPE_STAFF ? spellmodel::Hold::Staff : t == D2RVR_TYPE_WAND || t == D2RVR_TYPE_ORB ? spellmodel::Hold::OneHandCaster : spellmodel::Hold::Hand;
        const float asked = spellmodel::Cells(hold, g_set.handRange.load(), g_set.casterRange.load(), g_set.staffRangeOne.load());
        const float cells = ArmAimCells(asked, hold == spellmodel::Hold::Staff ? StaffFarCells(asked) : asked);   // [bow] aim_distance_by_arm: a spell's point follows the arm
        float f[2]; memcpy(f, &from, sizeof f);
        float px, py;
        if (aimpoint::Point(f[0], f[1], g_spellCast.gx, g_spellCast.gy, cells, &px, &py)) { out[0] = px; out[1] = py; }
        static ULONGLONG told = 0;
        if (GetTickCount64() - told > 2000) {
            told = GetTickCount64();
            LogF("spell aim (model): the spell's point %.1f cells out along the hand (%s)", cells,
                 hold == spellmodel::Hold::Staff ? "a staff, [bow] staff_range_one" : hold == spellmodel::Hold::OneHandCaster ? "a wand or orb, [bow] caster_range" : "bare hands, [bow] hand_range");
        }
        return r;
    }
#endif
    const uint32_t w = gamestate::WeaponClass();
    float gx, gy;
    const uint32_t type = gamestate::WeaponType();
    const bool thrown = type == D2RVR_TYPE_JAVELIN || type == D2RVR_TYPE_THROWING;
    if (!out || !AimByVector() || (w != 3 && w != 4 && !thrown && !StaffAim()) || !HandGridDir(&gx, &gy)) return r;
    float f[2]; memcpy(f, &from, sizeof f);
    if (thrown && GestureAimLive()) {   // a throw by gesture: its own point (vr/throw_aim.h), as in the resolver's branch above
        float tx, ty;
        if (const int g = GesturePoint(f, gx, gy, &tx, &ty)) {
            if (g == 1) { out[0] = tx; out[1] = ty; }
            return r;
        }
    }
    // A crossbow or a staff aims finer held in both hands: the point farther out.
    bool twoHands = false;
    float range = AimRangeCells(type, &twoHands);
    if (StaffAim() && type == D2RVR_TYPE_STAFF && !twoHands) range = ArmAimCells(range, StaffFarCells(range));   // [bow] aim_distance_by_arm: a staff with one hand on it is a spell's, up to staff_range_two; a bow, a crossbow, two hands on a staff keep theirs
    static ULONGLONG told = 0;
    if (GetTickCount64() - told > 250) {
        told = GetTickCount64();
        LogF("target: attack point %.1f %.1f (%.1f cells out) -> %.1f %.1f along the hand", out[0], out[1],
             sqrtf((out[0]-f[0])*(out[0]-f[0]) + (out[1]-f[1])*(out[1]-f[1])), f[0] + gx * range, f[1] + gy * range);
    }
    out[0] = f[0] + gx * range;
    out[1] = f[1] + gy * range;
    return r;
}

uint64_t HookAttackTarget(void* self, void* unit, float* dir, int* outX, int* outY) {
    frametrace::Scope trace(frametrace::kAimHooks);   // [debug] frame_trace: ours, the game's call left out
    if (unit) g_heroUnit.store(unit);
    const uintptr_t caller = (uintptr_t)_ReturnAddress() - g_base;
    float use[2];
    float* d = dir;
    const float gx = dir ? dir[0] : 0.0f, gy = dir ? dir[1] : 0.0f, mag = sqrtf(gx*gx + gy*gy);
    // learn the axes: the stick's grid vector against the way the hero walks
    if (dir && mag > 0.5f && g_walkSteady.load()) {
        const float tg = atan2f(gy, gx), tw = g_walkTheta.load();
        AcquireSRWLockExclusive(&g_calLock);
        Calib& c = g_cal;
        c.c1 += cos(tg - tw); c.s1 += sin(tg - tw); c.c2 += cos(tg + tw); c.s2 += sin(tg + tw); c.n++;
        if (c.n >= 15 && c.n % 15 == 0) {
            const double r1 = sqrt(c.c1*c.c1 + c.s1*c.s1) / c.n, r2 = sqrt(c.c2*c.c2 + c.s2*c.s2) / c.n;
            c.mirror = r2 > r1 ? 1 : 0;
            c.angle = (float)(c.mirror ? atan2(c.s2, c.c2) : atan2(c.s1, c.c1));
            const bool was = c.ok;
            c.ok = std::max(r1, r2) > 0.8;
            if (!was || c.n % 150 == 0)
                LogF("target: grid vs world axes %s, %.1f deg %s (agreement %.2f / %.2f over %d steps)%s", c.ok ? "LEARNED" : "unclear",
                     c.angle * 57.2957795f, c.mirror ? "mirrored" : "turned", r1, r2, c.n,
                     xr::Running() ? " - not used: native OpenXR aims on the world's axes" : "");
        }
        ReleaseSRWLockExclusive(&g_calLock);
    }
    // mode 4: while A is held, the vector points along the aiming hand
    bool turned = false;
    float theta = 0.0f;
    // only the aim function's own call (0x144845, returns to 0x14484A): the
    // walking calls keep the stick, or the hero would walk after the hand
    if (dir && caller == d2rsig::Rva(RVA_ATTACK_POINT_RET) && AimByVector() && HandGridDir(&use[0], &use[1])) {
        d = use;
        turned = true;
    }
    trace.Pause();
    const uint64_t r = OrigAttackTarget(self, unit, d, outX, outY);
    trace.Resume();
    // the log: which caller, what vector, what point - a few times a second per caller
    static uintptr_t lastCaller[4] = {};
    static ULONGLONG lastAt[4] = {};
    int slot = 0;
    for (; slot < 4 && lastCaller[slot] && lastCaller[slot] != caller; ++slot) {}
    if (slot < 4) {
        lastCaller[slot] = caller;
        const ULONGLONG now = GetTickCount64();
        if (now - lastAt[slot] >= (g_aHeld.load() ? 150u : 1000u)) {
            lastAt[slot] = now;
            LogF("target: caller 0x%llX unit %p vector %.2f %.2f%s -> %d %d (A %d, hand %.0f deg)", (unsigned long long)caller, unit, gx, gy,
                 turned ? " TURNED to the hand" : "", outX ? *outX : -1, outY ? *outY : -1, g_aHeld.load() ? 1 : 0,
                 HandWorldTheta(&theta) ? theta * 57.2957795f : -999.0f);
        }
    }
    return r;
}

}  // namespace vrc
