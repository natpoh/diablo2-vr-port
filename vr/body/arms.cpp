// vr/body/arms.cpp - cut from vr/vrcam.cpp (docs/plan_modules.md M12); what it gives: vr/body/body.h. It sees vrcam.cpp's headers.
#include "../vrcam_headers.h"

using namespace D2RL;
using namespace vrc;   // as in vrcam.cpp

namespace vrc {

// For the skeleton hook, on every pose: the controllers from the block the
// camera reads its head from (during a pair, the pair's own copy).
// The head sample the hands came with and the head in BodyWalk's room (OpenXR stage axes): what puts
// the controllers in the room for a weapon with weight (vr/weight.cpp). No room from BodyWalk: the head
// stands at the room's middle, turned as BodyWalk turned it last.
void HeadInRoom(const D2RVR_Shared* sh, skel::Input* in) {
    in->sample = sh->counter;
    if (sh->roomMagic == D2RVR_ROOM_MAGIC) {
        memcpy(in->headRoom, sh->headRoom, sizeof in->headRoom);
        in->headYawDeg = sh->headYawRoomDeg;
    } else {
        in->headRoom[0] = 0.0f; in->headRoom[1] = sh->headHeightM; in->headRoom[2] = 0.0f;
        in->headYawDeg = sh->headYawDeg;
    }
}

bool FreshHands(skel::Input* in) {
    const D2RVR_Shared* sh = Shared();
    if (!sh || sh->version != D2RVR_SHARED_VERSION || !sh->headValid) return false;
    HeadInRoom(sh, in);
    in->handsValid = sh->handsValid;
    memcpy(in->hand[0], sh->rightHand, sizeof in->hand[0]); memcpy(in->hand[1], sh->leftHand, sizeof in->hand[1]);
    memcpy(in->rot[0], sh->rightRot, sizeof in->rot[0]); memcpy(in->rot[1], sh->leftRot, sizeof in->rot[1]);
    in->userHeadM = UserEyeHeightM();   // standing, seated or not (see TrueScale)
    in->grip[0] = sh->gripMagic == D2RVR_GRIP_MAGIC ? sh->rightGrip : -1.0f;
    in->grip[1] = sh->gripMagic == D2RVR_GRIP_MAGIC ? sh->leftGrip : -1.0f;
    // [gameplay] melee_touch: which hand sample this pose is made from, and the world it stands in
    in->poseStamp = sh->sampleStamp;
    in->havePoseStamp = sh->sampleStampMagic == D2RVR_SAMPLE_STAMP_MAGIC;
    in->worldEpoch = g_gen.load();
    meleetouch::NoteHands(sh, in->worldEpoch);
    // [weight]: the swap gesture's claim of a grip as of THIS pose (the 10 ms copy may be a press old): that grip takes no weapon off the
    // floor (it does take one stowed on the back); and the grips as the controllers have them (a grip on the weapon is taken from the
    // layout, so it reads as let go in in->grip)
    if (in->weight.on) {
        for (int side = 0; side < 2; ++side) in->weight.claimed[side] = shoulderwire::Claims(side);
        float raw[2];
        in->weight.haveRawGrip = weightwire::RawGrips(raw);
        if (in->weight.haveRawGrip) { in->weight.rawGrip[0] = raw[0]; in->weight.rawGrip[1] = raw[1]; }
        float rawTrig[2];
        in->weight.haveRawTrigger = weightwire::RawTriggers(rawTrig);
        if (in->weight.haveRawTrigger) { in->weight.rawTrigger[0] = rawTrig[0]; in->weight.rawTrigger[1] = rawTrig[1]; }
    }
    // [hands] shield_fist: the left hand's fingers follow the grip as the controller has it (not what the layout kept after its takes);
    // no native frame: the shared block's grip is the real one (-1 = skeletons use Input::grip[1])
    in->shieldRaw[0] = in->shieldRaw[1] = -1.0f;
    if (in->weight.on) {   // [hands] shoulder_draw: the game's world under the room, of this pose - a weapon that lies on the floor is carried by how it moved (vr/world_anchor.h)
        AcquireSRWLockShared(&g_stickLock);
        const StickFrame sf = g_stickFrame;
        ReleaseSRWLockShared(&g_stickLock);
        in->weight.worldOk = sf.valid && g_eyeOk.load();
        if (in->weight.worldOk) {
            for (int i = 0; i < 3; ++i) { in->weight.worldEye[i] = g_eyeWorld[i].load(); in->weight.worldCamR[i] = sf.camR[i]; in->weight.worldCamF[i] = sf.camF[i]; }
            in->weight.worldUpm = std::max(1.0f, ViewHeight()) / UserEyeHeightM();
        }
    }
    if (in->grabOn || in->itemsOn) {   // [weight] grab_bodies / grab_items: the render world of this pose, as the weight model's (the bodies and the items are in it)
        AcquireSRWLockShared(&g_stickLock);
        const StickFrame sf = g_stickFrame;
        ReleaseSRWLockShared(&g_stickLock);
        in->grabWorldOk = sf.valid && g_eyeOk.load();
        if (in->grabWorldOk) {
            for (int i = 0; i < 3; ++i) { in->grabEye[i] = g_eyeWorld[i].load(); in->grabCamR[i] = sf.camR[i]; in->grabCamF[i] = sf.camF[i]; }
            in->grabUpm = std::max(1.0f, ViewHeight()) / UserEyeHeightM();
        }
    }
    if (in->shieldFist || (in->shieldHolster && in->shieldStow) || in->rangedHolster) {
        float raw[2];
        if (weightwire::RawGrips(raw)) { in->shieldRaw[0] = raw[0]; in->shieldRaw[1] = raw[1]; }
    }
    return true;
}

void PushArms() {
    // Only in a game area: on the main menu's character screen the hero stood
    // headless, his arms on the controllers (2026-10-05) - there he is the game's.
    const bool on = g_enabled.load() && g_inWorld.load();
    const D2RVR_Shared* sh = Shared();
    skel::Input in;
    if (sh && sh->version == D2RVR_SHARED_VERSION && sh->headValid) {
        HeadInRoom(sh, &in);
        in.handsValid = sh->handsValid;
        memcpy(in.hand[0], sh->rightHand, sizeof in.hand[0]); memcpy(in.hand[1], sh->leftHand, sizeof in.hand[1]);
        memcpy(in.rot[0], sh->rightRot, sizeof in.rot[0]); memcpy(in.rot[1], sh->leftRot, sizeof in.rot[1]);
        in.userHeadM = UserEyeHeightM();   // standing, seated or not (see TrueScale)
        in.grip[0] = sh->gripMagic == D2RVR_GRIP_MAGIC ? sh->rightGrip : -1.0f;
        in.grip[1] = sh->gripMagic == D2RVR_GRIP_MAGIC ? sh->leftGrip : -1.0f;
    }
    float hero[3];
    if (LookAtNow(hero)) {
        skel::SetHeroPos(hero[0], hero[1], hero[2]);
        memcpy(g_heroPosNow, hero, sizeof hero);
    }
    // third person, the game on the floor and F1 in perspective: the whole hero as the game
    // animates him, head included (on the floor he stood headless, 2026-10-05)
    const bool own = on && !ThirdPerson() && !Overhead();
    in.mode = own ? ArmsMode() : 0;
    // From behind the game animates all of him, but he still turns with the
    // camera: else the right stick only swung the camera round a hero who
    // kept facing wherever he last walked.
    // Without the body the hero is turned whole, about the vertical only, and seen from his eyes.
    in.bodyTurn = own ? (FullBody() ? g_set.bodyTurn.load() : 2) : on && ThirdPerson() && !Overhead() && g_set.thirdTurn.load() ? 2 : 0;
#if D2RVR_FIRST_PERSON
    {   // [body] gait_legs_third / gait_legs_f4: our own hero's legs by the gait (the library loaded here, the update thread, never in the pose hook). From behind
        // only while the whole hero turns with the camera ([body] turn_third); in F4 the whole body is turned with the look for it (the pelvis faces where you do).
        const bool third = on && ThirdPerson() && !Overhead() && g_set.gaitThird.load() && in.bodyTurn == 2;
        const bool f4 = own && FullBody() && g_set.gaitF4.load();
        in.gaitLegs = (third || f4) && gaitlib::Ensure();
        if (in.gaitLegs && f4) in.bodyTurn = 2;
    }
#endif
#if D2RVR_FIRST_PERSON
    in.hideHead = own && (g_set.hideHead.load() || !FullBody());
    const int likeStaff = HeldLikeStaff();
    // a staff in the hands: aimed from hand to hand ([bow] staff_two_hands) or fast in the right one with
    // the left free (staff_free_left) - the latter no longer waits for the aim's box (off by default since
    // 2026-10-06, it left the free left hand stiff in the staff's grip, never closing with the grip)
    in.staff = own && (((g_set.staffTwoHands.load() || g_set.staffFreeLeft.load()) && gamestate::WeaponType() == D2RVR_TYPE_STAFF) ||
                       XbowLikeStaff() || likeStaff);
    in.staffHand = gamestate::WeaponType() == D2RVR_TYPE_CROSSBOW ? g_set.xbowHand.load()
                 : likeStaff == 1 ? (gamestate::WeaponType() == D2RVR_TYPE_SPEAR || gamestate::WeaponType() == D2RVR_TYPE_POLEARM
                                     ? g_set.spearHand.load() : g_set.axeHand.load())
                 : likeStaff == 2 ? g_set.swordHand.load()
                 // a staff: [hands] staff_hand, right by default like every other weapon (a barbarian's
                 // battle staff floated off the hand with the old left wrist, 2026-10-06)
                 : StaffByRule() ? g_set.staffHand.load() : 1;
    in.staffHandAuto = likeStaff != 0;   // the staff and the crossbow keep the wrist they were found on
    in.leftHilt = likeStaff == 2;
    in.leftHiltAtM = g_set.swordLeftAtCm.load() * 0.01f;
    in.leftHiltSlideM = g_set.swordSlideCm.load() * 0.01f;
    {
        const uint32_t t = gamestate::WeaponType();
        in.shaftAxis = likeStaff == 2 ? g_set.swordHiltAxis.load()
                     : likeStaff == 1 ? (t == D2RVR_TYPE_SPEAR || t == D2RVR_TYPE_POLEARM ? g_set.spearShaftAxis.load() : g_set.axeShaftAxis.load())
                     : StaffByRule() ? g_set.staffShaftAxis.load()
                     : 0;
    }
    in.stockAxis = g_set.xbowStockAxis.load();
    in.gunFrame = g_set.xbowGunFrame.load();
    for (int i = 0; i < 3; ++i) in.lineTurnDeg[i] = g_set.xbowLine[i].load();
    in.staffHands = g_set.staffHands.load();
    // spears, polearms and two-handed swords have only this way (the old one turned the
    // weapon through both hands as the animation holds them)
    in.staffFreeLeft = g_set.staffFreeLeft.load() || likeStaff;
    // [hands] fist: a hand whose slot holds nothing is free. Bows and crossbows
    // keep the animation's hands (the drawing hand holds the string); a staff
    // sits in the right slot, and the left is free only in staff_free_left
    // (the hook then keeps it from a fist while it is on the shaft).
    in.fist = own && g_set.fist.load();
    in.shieldFist = own && g_set.shieldFist.load() && viewgate::F4() && gamestate::LeftShield();   // [hands] shield_fist: F4 only
    in.forearmTwist = own && g_set.forearmTwist.load() && viewgate::F4() ? g_set.forearmShare.load() : -1.0f;   // [hands] forearm_twist: F4 only
    // [hands] shoulder_draw, the shield: it follows the sword to the forearm opposite the hand that holds it - the strap is how it goes on the right one, so it implies [shield] forearm
    {   // [hands] shoulder_draw_ranged: a bow or crossbow is the weapon - it can be put away and drawn; its own hand (the legacy aim hand, the slot's wrist), the swap, who has the string
        const uint32_t wc = gamestate::WeaponClass();
        const bool ranged = wc == 3 || wc == 4;   // D2RVR_WEAPON_BOW, D2RVR_WEAPON_CROSSBOW
        in.rangedHolster = own && viewgate::F4() && g_set.shoulderDraw.load() && g_set.rangedHolster.load() && ranged;
        in.rangedSwap = g_set.rangedSwap.load();
        in.rangedBow = wc == 3;
        in.rangedHome = RangedHomeHand();
        in.rangedBone = wc == 4 ? g_set.xbowHand.load() : in.rangedHome;   // (the game hangs a crossbow on the left wrist; a bow on its aim hand's)
        for (int side = 0; side < 2; ++side) in.rangedBusy[side] = bowdrawwire::HoldsGrip(side);
    }
    in.shieldFollow = g_set.shieldDraw.load();
    in.shieldStow = g_set.shieldStow.load();
    in.shieldHolster = own && viewgate::F4() && g_set.shoulderDraw.load() && (in.shieldFollow || in.shieldStow) && gamestate::LeftShield();
    {   // why the shield's holster is on or off, told when it changes: with no shield in the weapon set (a two-handed weapon, a quiver in the left slot) there is nothing to put away
        static int toldHolster = -1;
        const int now = in.shieldHolster ? 1 : 0;
        if (now != toldHolster) {
            toldHolster = now;
            if (now) Log("vrcam: shield holster - on: a shield is in the left slot, F4, shoulder_draw on");
            else LogF("vrcam: shield holster - off: %s",
                      !own ? "the arms are not on the controllers" : !viewgate::F4() ? "not the F4 view" : !g_set.shoulderDraw.load() ? "[hands] shoulder_draw is off" :
                      !(in.shieldFollow || in.shieldStow) ? "both shield ticks are off" : "the left slot holds no shield (this weapon set has none: a two-handed weapon, a crossbow with a quiver, a bow)");
        }
    }
    in.shieldTick = g_set.weightTakeHaptic.load() ? g_set.weightTakeHapticPct.load() * 0.01f : 0.0f;
    for (int side = 0; side < 2; ++side) in.shieldBusy[side] = engitem::BeltBusy(side);
    in.shieldForearm = own && (g_set.shieldForearm.load() || in.shieldHolster) && viewgate::F4() && gamestate::LeftShield();         // [shield] forearm: F4 only
    in.headsHang = own && g_set.headsHang.load() && viewgate::F4() && gamestate::HeadSide() >= 0;   // [shield] heads_hang: F4 only, a head in either hand slot
    {   // [shield] heads_hang: once for each change, what the hands hold and whether a head hangs (or why not)
        const int headSide = gamestate::HeadSide();
        const uint32_t codes[2] = {gamestate::HandCode(0), gamestate::HandCode(1)};
        const int state = !g_set.headsHang.load() ? 1 : !viewgate::F4() ? 2 : !own ? 3 : headSide < 0 ? 4 : 0;
        static int toldState = -1;
        static uint32_t toldCodes[2] = {~0u, ~0u};
        if (state != toldState || codes[0] != toldCodes[0] || codes[1] != toldCodes[1]) {
            toldState = state;
            toldCodes[0] = codes[0];
            toldCodes[1] = codes[1];
            char hand[2][5];
            for (int s = 0; s < 2; ++s) {
                for (int i = 0; i < 4; ++i) hand[s][i] = codes[s] ? (char)((codes[s] >> (8 * i)) & 0xFF) : ' ';
                hand[s][4] = 0;
            }
            static const char* const kWhy[5] = {"it hangs from its cord (the head's own skeleton is turned - the 'head hang' lines say how)", "not hung: [shield] heads_hang is off",
                                                "not hung: not the F4 view", "not hung: the arms are not on the controllers", "neither is a Necromancer's head (ItemTypes head) - nothing to hang"};
            LogF("vrcam: heads hang - hands right '%s' left '%s': %s%s", hand[0], hand[1],
                 headSide == 1 ? "the left item is a head, " : headSide == 0 ? "the right item is a head (a shield in the weapon's slot), " : "", kWhy[state]);
        }
    }
    for (int k = 0; k < 6; ++k) in.shieldAdj[k] = g_set.shieldAdj[k].load();
    in.shieldBlink = g_set.shieldBlink.load();
    {   // [hands] shoulder_zone_body: the shoulders' reach (the swap gesture, the weapon's draw, the shield's and the bow's put-away) from the body, set once a pose and told when it changes
        static int toldEyeMm = -1;
        static bool toldBody = false;
        const bool body = g_set.shoulderZoneBody.load();
        const int eyeMm = (int)std::lround(in.userHeadM * 1000.0f);
        if (body != toldBody || (body && std::abs(eyeMm - toldEyeMm) >= 10)) {
            toldBody = body;
            toldEyeMm = eyeMm;
            shoulderswap::SetZone(body ? shoulderswap::BodyZone(in.userHeadM) : shoulderswap::Zone{});
            const shoulderswap::Zone z = shoulderswap::GetZone();
            LogF("vrcam: shoulder zones - %s: behind a shoulder is x %.2f..%.2f (from the spine) y %.2f..%.2f z %.2f..%.2f m from the head%s", body ? "fitted to the body" : "the fixed ones",
                 z.absXMin, z.absXMax, z.yMin, z.yMax, z.zMin, z.zMax, body ? (std::string(" (eye height ") + std::to_string((int)std::lround(in.userHeadM * 100.0f)) + " cm)").c_str() : "");
        }
    }
    {   // what the forearm twist found on this rig and the shield's strap, told when it changes; the rolls with it
        static std::string toldNote;
        static int toldStrap = 0;
        float roll[2];
        const std::string note = skel::ForearmTwistNote(roll);
        const int strap = skel::ShieldStrapped();
        if (in.forearmTwist >= 0.0f && !note.empty() && note != toldNote) {
            toldNote = note;
            Log(("vrcam: forearm twist (share " + std::to_string((int)std::lround(in.forearmTwist * 100.0f)) + "%) - the rig's forearm twist bones: " + note).c_str());
        }
        {   // [shield] x y z pitch yaw roll: what reached the hook and what it did to the strapped shield, told when the numbers change (a slider that seems to do nothing: this says where)
            static float toldAdj[6] = {};
            static int toldAdjStrap = 0;
            float seen[6], moveCm, turnDeg;
            skel::ShieldAdjNote(seen, &moveCm, &turnDeg);
            bool same = toldAdjStrap == strap;
            for (int q = 0; q < 6; ++q) same = same && std::fabs(seen[q] - toldAdj[q]) < 0.05f;
            if (!same && strap != 0) {
                for (int q = 0; q < 6; ++q) toldAdj[q] = seen[q];
                toldAdjStrap = strap;
                LogF("vrcam: shield corrections at the hook: x %.1f y %.1f z %.1f cm, pitch %.0f yaw %.0f roll %.0f deg - the shield %s: moved %.1f cm and turned %.0f deg from the plain strap",
                     seen[0], seen[1], seen[2], seen[3], seen[4], seen[5], strap > 0 ? "is strapped" : "is NOT strapped (no strap, so no corrections)", moveCm, turnDeg);
            }
        }
        if (strap != toldStrap) {
            toldStrap = strap;
            LogF("vrcam: shield %s (rolls about the forearms: right %.0f, left %.0f deg)",
                 strap == 3 ? "strapped to the RIGHT forearm, the left one's strap mirrored - [hands] shoulder_draw" :
                 strap == 2 ? "PUT AWAY at the left shoulder (its attach bone shrunk, not drawn) - [hands] shoulder_draw_shield_stow" :
                 strap > 0 ? "strapped to the left forearm, its bind pose there - [shield] forearm" :
                 strap < 0 ? "NOT strapped to the forearm (the game hides it, or the rig's bind pose did not read) - on the hand as before" :
                             "on the hand as before", roll[0], roll[1]);
        }
    }
    for (int c = 0; c < 8; ++c) { in.fistClose[c] = g_set.fistClose[c].load() * 0.01f; in.fistOpen[c] = g_set.fistOpen[c].load() * 0.01f; }
    for (int side = 0; side < 2; ++side) in.fistCap[side] = engitem::FistCap(side);   // a belt potion in it: round the bottle, not a full fist
    {
        const uint32_t held = gamestate::HandsHeld(), type = gamestate::WeaponType();
        const bool xbowHeld = type == D2RVR_TYPE_CROSSBOW && in.staff;   // held as the staff, see XbowLikeStaff
        const bool ranged = type == D2RVR_TYPE_BOW || (type == D2RVR_TYPE_CROSSBOW && !xbowHeld);
        const bool staffBoth = (type == D2RVR_TYPE_STAFF || xbowHeld) && !(in.staffFreeLeft && in.staff);
        // Held in the right hand alone (staff_free_left): the left is free whichever slot the
        // game keeps it in - a crossbow sits in the left one, and that hand stayed stiff in the
        // animation's grip, never relaxed nor closed with the grip as on the staff (2026-10-05).
        const bool rightHolds = in.staff && in.staffFreeLeft;
        for (int side = 0; side < 2; ++side)
            in.handFree[side] = rightHolds ? side == 1 : !(held & (1u << side)) && !ranged && !staffBoth;
    }
    // [weight] (vr/weight.cpp): a melee weapon held alone, in either hand (not a second weapon in the other hand) is a body
    // of its own - heavy, taken by a hand where its grip closes on the shaft, passed from hand to hand. The
    // staff's and the two-handed weapons' own ways (the left hand on the shaft or hilt) give way to it.
    {
        skel::Input::Weight& w = in.weight;
        const uint32_t type = gamestate::WeaponType(), two = gamestate::TwoHanded(), held = gamestate::HandsHeld();
        using K = heft::hold::Kind;
        const auto kindOf = [](uint32_t t) {
            switch (t) {
            case D2RVR_TYPE_SWORD: return (int)K::Sword;
            case D2RVR_TYPE_AXE: return (int)K::Axe;
            case D2RVR_TYPE_MACE: return (int)K::Mace;
            case D2RVR_TYPE_SCEPTER: return (int)K::Scepter;
            case D2RVR_TYPE_WAND: return (int)K::Wand;
            case D2RVR_TYPE_STAFF: return (int)K::Staff;
            case D2RVR_TYPE_POLEARM: return (int)K::Polearm;
            case D2RVR_TYPE_SPEAR: return (int)K::Spear;
            case D2RVR_TYPE_DAGGER: return (int)K::Dagger;
            case D2RVR_TYPE_THROWING: return (int)K::Throwing;
            case D2RVR_TYPE_JAVELIN: return (int)K::Javelin;
            case D2RVR_TYPE_OTHER: return (int)K::Other;
            default: return -1;   // nothing held, bows and crossbows, claws (on the fist), orbs (in the palm)
            }
        };
        const int kind = kindOf(type);
        // F4 only (vr/view_gate.h): in.mode == 2 is the first-person body on the controllers, and the view test is asked here too
        // A weapon alone in EITHER hand's slot (the game's left one too: «оружие в левой руке - кукла прозрачная», the headset 2026-10-23 - it was left out with
        // the second weapon). A weapon in each hand: the first (the slot the game's kind comes from) is the weight model's, the other a body of its own beside it
        // (vr/weight.cpp StepSecond: «когда два оружия в двух руках, они проходят сквозь куклу») - one hand on each
        const bool alone = meleetouch::HeldAlone((two & D2RVR_LEFT_WEAPON) != 0, (held & 1u) != 0);
        const int otherKind = alone ? -1 : kindOf(gamestate::HandKind((int)(gamestate::WeaponHand() & 1u) ^ 1));
        // (a bow with its quiver: no kind on either side - nothing held, the draw keeps both hands; weight_drive_test)
        const meleetouch::Holds holds = meleetouch::WeightHolds(own && in.mode == 2 && viewgate::F4() && g_set.weightOn.load(), kind, alone, otherKind);
        w.on = holds.on;
        w.second = holds.second;
        w.secondKind = holds.secondKind;
        w.secondLetsGo = IniB(L"weight", L"second_lets_go", true);
        w.dualCollide = IniB(L"weight", L"dual_collide", true);   // [weight] dual_collide: the two weapons of a pair meet each other, steel on steel   // [weight] second_lets_go: the second weapon let go by its grip, as the first
        if (w.on) {
            w.kind = kind;
            w.twoHanded = (two & D2RVR_TWO_HANDED) != 0;
            w.slot = (int)(gamestate::WeaponHand() & 1u);
            w.otherBusy = (held & (1u << (w.slot ^ 1))) != 0;
            // a hand with a belt potion (or one lit to be taken) takes nothing; one whose grip is the weapon swap's (behind a shoulder) takes
            // nothing off the floor and calls nothing back (claimed) - but takes a weapon stowed on the back
            for (int side = 0; side < 2; ++side) {
                // [weight] grab_bodies: a hand that holds a body takes no weapon; one over a body it would take does not call the weapon back from afar (the grab's
                // grip, not the recall's - the headset 0.183: pressed at Warriv, the spear flew into the hand); the weapon in its reach (zone 1, 2) is still taken first
                w.busy[side] = engitem::BeltBusy(side) || ((grabwire::Holding() >> side) & 1u) || (((grabwire::OverBody() >> side) & 1u) && weight::TakeZone(side, nullptr) == 3);
                w.claimed[side] = shoulderwire::Claims(side);
            }
            // The weight model's shaft is the weapon bone AS DRAWN with the very ends the hit-by-touch blade has (bladeshape::ForItem, tuned): the bone's Y
            // toward the tip on the side the blade says (a dagger's blade runs along -Y), the wood so far ahead of the bone's origin and so far behind it
            // (a short staff: by its model 0.75 m of wood ahead of the bone and 0.96 m behind it). The old axis choice ([hands] staff_shaft_axis, spear_shaft_axis,
            // axe_shaft_axis, sword_hilt_axis) belongs to the logic of weight off.
            // (with the weapon model's own ends where the last pose has the unit of its model in metres - vr/weapon_extents.h; the constants where the touch is off and there is no pose)
            float modelUnitM = 0.0f;
            {
                skel::PosedWeaponPose pw;
                if (skel::PosedWeapon(pw) && pw.blade[w.slot & 1].ok) modelUnitM = pw.blade[w.slot & 1].unitM;
            }
            const meleetouch::BladeExtent be = meleetouch::BladeFor(gamestate::HandCode(w.slot), type, (two & D2RVR_TWO_HANDS_ON) != 0, modelUnitM);
            w.tipAxis = be.tipSign < 0 ? -2 : 2;
            w.shaftTipM = be.tipM;
            w.shaftButtM = be.buttM;
            if (w.second) {   // [weight] second_lets_go: the second weapon's own blade, as the first's (its shaft is measured off its drawn bone)
                float unit2 = 0.0f;
                {
                    skel::PosedWeaponPose pw;
                    if (skel::PosedWeapon(pw) && pw.blade[(w.slot & 1) ^ 1].ok) unit2 = pw.blade[(w.slot & 1) ^ 1].unitM;
                }
                const int slot2 = (int)((w.slot & 1) ^ 1);
                const meleetouch::BladeExtent b2 = meleetouch::BladeFor(gamestate::HandCode(slot2), gamestate::HandKind(slot2), false, unit2);
                w.secondTipAxis = b2.tipSign < 0 ? -2 : 2;
                w.shaft2TipM = b2.tipM;
                w.shaft2ButtM = b2.buttM;
                // [weight] dual_collide with [gameplay] melee_head_volume: each weapon's head as its own volume where its model has it (an axe's blade beside the shaft),
                // as hit by touch has it against the monsters - the other weapon of the pair stops on it (the user, 2026-10-26: «меч проходит сквозь лезвие топора но
                // не проходит сквозь древко»). The item's own volume, whether or not this pose's blade is measured (the skeleton checks the drawn bone's own unit: a
                // pose without one - the other hand's throw - kept the head away for a moment, «the axe's blade is not solid»).
                if (w.dualCollide && IniB(L"gameplay", L"melee_head_volume", true)) {
                    // A flat head (an axe's blade, a hammer's, a pick's, a pole-arm's) as the thin slab of capsules its mesh is (vr/weapon_head_parts.h), not the one
                    // capsule round it that is as thick as the head is tall (the user, 2026-10-28: «its collision volume is much bigger than the drawn model»).
                    static_assert(weaponheadparts::kParts == skel::Input::Weight::kHeadParts, "the head parts' table is the skeleton's");
                    const uint32_t code[2] = {gamestate::HandCode(w.slot), gamestate::HandCode(slot2)};
                    const uint32_t kinds[2] = {type, gamestate::HandKind(slot2)};
                    const float units[2] = {modelUnitM, unit2};
                    static uint32_t told[2] = {};
                    for (int k = 0; k < 2; ++k) {
                        metalhead::Volume vol;
                        if (!metalhead::VolumeOf(code[k], strikedir::KindIndex(kinds[k]), &vol)) continue;
                        const weaponheadparts::Row* parts = weaponheadparts::RowOf(code[k]);
                        if (parts && parts->n > 0) {
                            for (int p = 0; p < parts->n; ++p)
                                for (int i = 0; i < 7; ++i) w.headPart[k][p][i] = parts->part[p][i];
                            w.headParts[k] = parts->n;
                        } else {
                            for (int i = 0; i < 3; ++i) { w.headPart[k][0][i] = vol.a[i]; w.headPart[k][0][3 + i] = vol.b[i]; }
                            w.headPart[k][0][6] = vol.r;
                            w.headParts[k] = 1;
                        }
                        if (told[k] != code[k] && units[k] > 0.0f) {   // once an item: its head as the mesh has it, and what it is made of
                            told[k] = code[k];
                            const float um = units[k] * 100.0f;
                            float rLo = 1e9f, rHi = 0.0f;
                            for (int p = 0; p < w.headParts[k]; ++p) { rLo = std::min(rLo, w.headPart[k][p][6]); rHi = std::max(rHi, w.headPart[k][p][6]); }
                            if (parts)
                                LogF("vrcam: weight - the %s weapon's head ('%c%c%c%c'): the mesh's %.0f cm along the shaft, %.0f cm out of it, %.1f cm thick -> %d capsule(s) r %.1f - %.1f cm "
                                     "(the one round it was r %.1f cm)", k ? "second" : "first", (char)(code[k] & 255), (char)((code[k] >> 8) & 255), (char)((code[k] >> 16) & 255),
                                     (char)((code[k] >> 24) & 255), parts->mesh[0] * um, parts->mesh[1] * um, parts->mesh[2] * um, w.headParts[k], rLo * um, rHi * um, vol.r * um);
                            else
                                LogF("vrcam: weight - the %s weapon's head ('%c%c%c%c'): no parts row - the one capsule round it, r %.1f cm", k ? "second" : "first",
                                     (char)(code[k] & 255), (char)((code[k] >> 8) & 255), (char)((code[k] >> 16) & 255), (char)((code[k] >> 24) & 255), vol.r * um);
                        }
                    }
                }
            }
            w.backTakeM = g_set.weightBackTakeCm.load() * 0.01f;
            w.feel = g_set.weightFeel.load() * 0.01f;
            // [flail] ball_weight_pct: a flail whose ball swings on its chain ([flail] chain) weighs its handle and links plus the ball(s) times the slider
            if (g_set.flailChain.load()) w.feel *= flailchain::WeaponMassFactor(flailchain::HeadOfCode(gamestate::HandCode(w.slot)), g_set.flailWeightPct.load() * 0.01f);
            w.strength = g_set.weightStrength.load() * 0.01f;
            w.gravity = g_set.weightGravity.load() * 0.01f;
            w.damping = g_set.weightDamping.load() * 0.01f;
            w.gravityFeel = g_set.weightGravityFeel.load();
            w.handsModel = g_set.weightHandsModel.load();
            w.handsModelFlags = g_set.weightHandsPalm.load() ? 1u : 0u;
            w.gripModel = g_set.weightGripModel.load();
            if (g_set.weightHeroStrength.load()) {   // the hero's Strength and the weapon's requirement, from the game (vr/hero_strength.h)
                const int cls = skel::HeroClass();
                w.heroStrength = true;
                w.heroStr = gamestate::HeroStrength();
                w.reqStr = gamestate::HandRequiredStrength(w.slot);
                w.strengthClassPct = cls >= 0 && cls < 8 ? g_set.weightStrengthClass[cls].load() : 100.0f;
                w.strengthEffect = g_set.weightStrengthEffect.load();
                w.strengthHonest = g_set.weightStrengthHonest.load();
            }
            w.twoHandsDouble = g_set.weightTwoHandsDouble.load();
            w.grabM = g_set.weightGrabCm.load() * 0.01f;
            w.letGoM = g_set.weightLetGoCm.load() * 0.01f;
            for (int k = 0; k < 3; ++k) w.fist[k] = g_set.weightFist[k].load() * 0.01f;
            w.gripFit = g_set.weightGripFit.load();
            w.gripLikeBase = g_set.weightGripLikeBase.load();
            w.gripFingers = g_set.weightGripFingers.load();
            w.attachApart = g_set.weightAttachApart.load();
            for (int k = 0; k < 6; ++k) { w.gripHand[0][k] = g_set.weightGripHand[0][k].load(); w.gripHand[1][k] = g_set.weightGripHand[1][k].load(); }
            // [weight] to_back: a long weapon (a staff, a spear, a polearm, a javelin, anything two-handed) lies diagonally across the back, a short one lower
            w.drawMode = g_set.shoulderDraw.load();   // [hands] shoulder_draw (F4 only, with the weight model: w.on)
            w.drawStowS = g_set.shoulderDrawStowMs.load() * 0.001f;
            w.shieldZone = w.drawMode && g_set.shieldStow.load() && gamestate::LeftShield();   // the left shoulder is the shield's: the weapon is drawn from the right one only
            w.afterDrop = w.drawMode ? 1 : g_set.weightAfterDrop.load();   // (1: with shoulder_draw the weapon goes to the back after the delay, drawn slung there - whatever after_drop says; and a weapon taken by the other hand is drawn where the body has it)
            w.stowAfterS = g_set.weightToBackMs.load() * 0.001f;
            {   // the ground the player sees under the hero: how far the head is over it (vr/weight_floor.h) - a let-go weapon falls to THAT, not to the room's floor
                const float upmWorld = std::max(1.0f, ViewHeight()) / UserEyeHeightM();
                w.headAboveGroundM = weightfloor::HeadAboveGroundM(g_eyeOk.load() ? g_eyeWorld[1].load() : std::nanf(""), g_heroPosNow[1], upmWorld, UserEyeHeightM());
            }
            w.takeHaptic = g_set.weightTakeHaptic.load() ? g_set.weightTakeHapticPct.load() * 0.01f : 0.0f;
            w.recallAny = g_set.weightRecallAny.load();
            w.monsterBodies = g_set.weightMonsterBodies.load() && viewgate::F4();   // [weight] monster_bodies: VR first person only
            w.stabIn = g_set.weightStabIn.load() && viewgate::F4();                 // [weight] stab_in: the same
            w.stabMinSpeed = g_set.stabMinSpeed.load();
            w.stabMaxAngleDeg = g_set.stabMaxAngleDeg.load();
            w.stabResistPct = g_set.stabResistPct.load();
            w.stabDepthCm = g_set.stabDepthCm.load();
            w.stabTearCm = g_set.stabTearCm.load();
            w.stabUnlockN = g_set.stabUnlockN.load();
            {   // [weight] stick_by_strength, stick_pct: how hard it sticks - the strength table's class multiplier (not the hero's Strength stat), as much as strength matters
                const int cls = skel::HeroClass();
                w.stickByStrength = g_set.stickByStrength.load() && g_set.weightHeroStrength.load();   // (the table is the hero's strength group's)
                w.stickPct = g_set.stickPct.load();
                w.stick = (float)stickstr::Factor(w.stickByStrength, cls >= 0 && cls < 8 ? g_set.weightStrengthClass[cls].load() : 100.0f, g_set.weightStrengthEffect.load(), w.stickPct);
            }
            w.stabDebug = g_set.stabDebug.load() && viewgate::F4();
            w.stabWithRun = g_set.stabWithRun.load();
            w.strikeMeasure = g_set.strikeMeasure.load() && viewgate::F4();
            w.strikeByForce = (g_set.strikeByForce.load() || meleedirectclient::WantsForce() || meleedirect::ByRules()) && viewgate::F4();   // [weight] strike_by_force: the strikes measured for hit by touch (also for [gameplay] melee_direct_client's force rule)
            w.monsterRagdoll = w.monsterBodies && g_set.weightMonsterRagdoll.load();   // [weight] monster_ragdoll (probe)
            w.thrownFlyMs = g_set.weightThrownFlyMs.load();
            w.throwReachPct = g_set.weightThrowReachPct.load();
            w.throwMaxCells = g_set.weightThrowMaxCells.load();
            w.unitsPerMetre = std::max(1.0f, ViewHeight()) / UserEyeHeightM();   // world units per metre at the hero's size (a cell is two units): the cap in metres
            w.thrownModel = g_set.weightThrownModel.load();
            // [hands] quick_throw: the game's missile is made that much sooner, so the model hands over to it sooner by the same time (a handoff of 0 stays 0)
            w.thrownHandoffMs = std::max(0.0f, g_set.weightThrownHandoffMs.load() - (float)animcut::CutMs(animcut::Kind::Throw));   // (a potion's rows are not cut: CutMs is 0 while one is in hand)
            w.thrownHandEmptyMs = g_set.weightThrownHandEmptyMs.load();
            w.thrownPotion = gamestate::HandIsThrowPotion(w.slot);   // a throwing potion in the hand: with potion_model_flies its model flies to the landing (vr/weight_throw.h)
            w.potionFlies = g_set.weightPotionFlies.load();
            w.slide = g_set.weightSlide.load();
            w.slideKinds = g_set.weightSlideKinds.load();
            w.hiltOnly = g_set.weightHiltOnly.load();
            for (int i = 0; i < gripzone::kCount; ++i) w.hiltCm[i] = g_set.weightHiltCm[i].load();
            w.backLong = w.twoHanded || type == D2RVR_TYPE_STAFF || type == D2RVR_TYPE_POLEARM || type == D2RVR_TYPE_SPEAR || type == D2RVR_TYPE_JAVELIN;
            for (int k = 0; k < 6; ++k) w.back[k] = (w.backLong ? g_set.weightBackLong[k] : g_set.weightBackShort[k]).load();
            in.staff = false;
            in.staffFreeLeft = false;
            in.leftHilt = false;
            // a hand is free when its slot is empty; the slot's own hand when it lets go of the weapon (skeletons)
            for (int side = 0; side < 2; ++side) in.handFree[side] = !(held & (1u << side));
            // [weight] second_lets_go: the second weapon let go - its hand is empty though the game's slot still has it: free (the empty hand's pose, the fist by its grip)
            if (w.second && weight::SecondLetGo()) in.handFree[w.slot ^ 1] = true;
        }
    }
    {   // staff_free_left: once per staff taken up, what the hook made of it
        static bool told = false;
        static uint32_t toldKind = 0;   // told again for another weapon held this way
        float dir[3], gap = 0.0f;
        const bool now = in.staff && in.staffFreeLeft && in.mode == 2 && skel::StaffAxis(dir, &gap);
        const uint32_t kindNow = gamestate::WeaponType() * 4u + (uint32_t)likeStaff;
        if (now && (!told || kindNow != toldKind)) {
            toldKind = kindNow;
            float sc[2];
            const int found = skel::GripHand(sc);
            const int wrist = in.staffHandAuto && found >= 0 ? found : in.staffHand;
            const uint32_t type = gamestate::WeaponType();
            LogF("vrcam: %s%s in the right hand (the game hangs it on the %s wrist%s; attach bones R %.2f L %.2f), hands %.2f model units "
                 "apart in its grip, shaft %.2f %.2f %.2f; left hand on it %s%s", likeStaff == 2 ? "two-handed " : "",
                 type < D2RVR_TYPE_COUNT ? kD2RVRWeaponTypeNames[type] : "?", wrist == 0 ? "right" : "left",
                 !in.staffHandAuto ? "" : found >= 0 ? ", read from its attach bones" : ", attach bones did not tell - [hands] spear_hand/sword_hand",
                 sc[0], sc[1], gap, dir[0], dir[1], dir[2],
                 in.grip[1] < 0.0f ? "always (bridge sends no grip - older than 0.13)" : "while its grip is held",
                 in.leftHilt ? " (on the hilt, does not turn it)" : "");
        }
        told = now;
        // which bone axis the crossbow's stock was laid along (skeletons GunFrame) - it jumped with the grip
        static int toldAxis = 0;
        if (const int ax = now ? skel::GunAxis() : 0; ax && ax != toldAxis) {
            toldAxis = ax;
            LogF("vrcam: crossbow stock along its bone's %c%c axis (grip gap %.2f)%s", ax < 0 ? '-' : '+', "xyz"[abs(ax) - 1], gap,
                 g_set.xbowStockAxis.load() ? " - [hands] xbow_stock_axis" : " - from the grip");
        }
    }
    in.weaponType = (int)gamestate::WeaponType();
    in.weaponSide = (int)gamestate::WeaponHand();
    // The bow's own work - the rig pose, the string's place, the string hand's pose - is F4's alone: the other views keep the game's bow.
    in.bowRig = own && FullBody() && g_set.bowRigPose.load() && gamestate::WeaponType() == D2RVR_TYPE_BOW;   // [bow] rig_pose
    in.bowRigGrip = g_set.bowRigGrip.load();
    // [flail] chain: F4's alone like the rest; the hook itself knows a flail by its model (any hand slot)
    in.flailChain = own && FullBody() && g_set.flailChain.load();
    in.flailDamping = 0.6f * g_set.flailDampingPct.load() * 0.01f;
    in.flailSwing = g_set.flailSwingPct.load() * 0.01f;
    in.flailHit = g_set.flailHitPct.load() * 0.01f;
    in.flailWeight = g_set.flailWeightPct.load() * 0.01f;
    in.bowString = own && bowdrawwire::WantsModelString() && gamestate::WeaponType() == D2RVR_TYPE_BOW;   // [hands] bow_draw: the touch point off the posed bow
    in.bowHandPose = own && FullBody() && g_set.bowHandPose.load() && gamestate::WeaponType() == D2RVR_TYPE_BOW;   // [bow] hand_pose: relaxed, hooked on the string
    in.bowAim = own && FullBody() && g_set.bowAimAlong.load() && gamestate::WeaponType() == D2RVR_TYPE_BOW;   // [bow] aim_along_hands: F4 only like the rest of the bow's work
    in.bowAimBlendMs = g_set.bowAimBlendMs.load();
    in.bowAimRoll = g_set.bowAimRoll.load();
    for (int i = 0; i < 3; ++i) in.bowRestCm[i] = g_set.bowRestCm[i].load();   // [bow] arrow_rest_*: with the rig pose (the arrow is ours), F4 only like it
    in.bowRestModel = in.bowRig && g_set.bowRestModel.load();   // [bow] arrow_rest_model: the bow's own arrow rest, read off its skeleton
    in.bowRest = in.bowRig && (in.bowRestModel || in.bowRestCm[0] != 0.0f || in.bowRestCm[1] != 0.0f || in.bowRestCm[2] != 0.0f);
    in.bowRestClear = g_set.bowRestClear.load();
    in.bowAxis = own && FullBody() && bowdrawwire::DebugWanted() && gamestate::WeaponType() == D2RVR_TYPE_BOW;   // [debug] bow_vectors: the bow's real axis drawn
    in.bowStave = own && FullBody() && bowquiverwire::WantsOrbs() && gamestate::WeaponType() == D2RVR_TYPE_BOW;   // [bow] quiver_orbs: the bow's stave, for the orbs along it
    in.bowArrow = own && FullBody() && bowquiverwire::WantsArrowFx() && gamestate::WeaponType() == D2RVR_TYPE_BOW;   // [bow] arrow_fx: the bow's own arrow, for the light on it
    in.bowHook = g_set.bowHook.load();
    in.bowHookGame = g_set.bowHookGame.load();
    in.bowHookCurl = g_set.bowHookCurl.load();
    in.bowHookThumbDeg = g_set.bowHookThumbDeg.load();
    in.bowStringAnchor = g_set.bowStringAnchor.load();
    for (int i = 0; i < 3; ++i) in.bowAnchorCm[i] = g_set.bowAnchorCm[i].load();
    // the hand opposite the bow's aim hand (0 right, 1 left): the hand pose's string hand, and - the bow being in the other - where the
    // bow's rig and the aim look for the bow when the attach bones cannot tell (an Amazon's quiver shows both)
    in.bowStringSide = in.bowHandPose || in.bowRig || in.bowString || in.bowAim || in.bowAxis || in.bowStave || in.bowArrow ? AimHand() ^ 1 : -1;
    if (in.bowString) {   // a held bow that is not the hunter's rig: its layout, once, for the rig work
        static std::string toldProbe;
        const std::string probe = skel::BowProbe();
        if (!probe.empty() && probe != toldProbe) { toldProbe = probe; LogF("vrcam: bow rig: %s", probe.c_str()); }
    }
    if (in.bowAim) {   // [bow] aim_along_hands: once for each change, what the aim did (its angle is in the debug's five-a-second line)
        static const char* toldAim = nullptr;
        const char* why = nullptr;
        skel::BowAimState(nullptr, nullptr, nullptr, &why);
        static std::string toldWho;
        const std::string who = skel::BowAimWho();   // which wrist it took the bow to be on, and by what
        if (why != toldAim || who != toldWho) { toldAim = why; toldWho = who; Log(("vrcam: bow aim: " + std::string(why ? why : "") + (who.empty() ? "" : " (" + who + ")")).c_str()); }
    }
    if (in.bowStringAnchor && (in.bowAim || in.bowAxis || in.bowRest || in.bowRig || in.bowString)) {   // [bow] string_anchor: which bones the anchor is made of and in which pose, said once and again when it changes
        static std::string toldAnchor;
        const std::string who = skel::StringAnchorWho();
        if (!who.empty() && who != toldAnchor) { toldAnchor = who; LogF("vrcam: bow string anchor: %s", who.c_str()); }
    }
    if (in.bowRig || in.bowString || in.bowAim || in.bowAxis) {   // every bow skeleton the game poses, whose it is and what the rig did with it: on change, twice a second at most
        static std::string toldBows;
        static ULONGLONG toldBowsAt = 0;
        if (GetTickCount64() - toldBowsAt >= 500) {
            toldBowsAt = GetTickCount64();
            const std::string bows = skel::BowRigWho();
            if (bows != toldBows) { toldBows = bows; Log(("vrcam: bow skeletons: " + bows).c_str()); }
        }
    }
    if (in.bowHandPose) {   // [bow] hand_pose: once for each change, what the string hand's pose did
        static std::string told;
        const std::string now = skel::BowHandPoseDiag();
        if (now != told) { told = now; LogF("vrcam: bow hand pose: %s", now.c_str()); }
    }
    {   // [bow] rig_pose: once for each change, what the bow's rig made of the drawn string
        static const char* told = nullptr;
        const char* now = in.bowRig ? skel::BowRigStatus() : nullptr;
        if (now != told) { told = now; if (now) LogF("vrcam: bow rig pose: %s", now); }
        if (in.bowRig) { const std::string note = skel::BowRigTakeNote(); if (!note.empty()) Log(("vrcam: " + note).c_str()); }   // which bow, which layout, once
        static std::string toldArrow;
        if (in.bowRig) {   // what the game's own pose does with the arrow bone, when its range changes
            std::string a = skel::BowRigDiag();
            a.resize(std::min<size_t>(a.size(), a.find(" (") == std::string::npos ? a.size() : a.find(" (")));
            if (a != toldArrow) { toldArrow = a; Log(("vrcam: bow rig pose: " + a).c_str()); }
        }
    }
    {   // [flail] chain: once for each change, what the hero's flail's chain does; which flail models and chains were learned, once each
        static const char* told = nullptr;
        const char* now = in.flailChain ? skel::FlailStatus() : nullptr;
        if (now != told) { told = now; if (now) LogF("vrcam: flail chain: %s", now); }
        if (in.flailChain) { const std::string note = skel::FlailTakeNote(); if (!note.empty()) Log(("vrcam: " + note).c_str()); }
    }
    {   // [shield] heads_hang: once for each change, what the hero's head does; which head models were learned and where they hang from, once each
        static const char* told = nullptr;
        const char* now = in.headsHang ? skel::HeadStatus() : nullptr;
        if (now != told) { told = now; if (now) LogF("vrcam: head hang: %s", now); }
        if (in.headsHang) { const std::string note = skel::HeadTakeNote(); if (!note.empty()) Log(("vrcam: " + note).c_str()); }
    }
    if (const std::string note = skel::ArmTakeNote(); !note.empty()) Log(("vrcam: " + note).c_str());   // the arms as drawn (once per rig), [arms] full_reach's work
    in.dollTrace = g_set.dollTrace.load();
    in.dollRebuild = g_set.dollRebuild.load();
    {   // what the pose hook noted - [debug] doll_trace: the hero's rig let go and taken again; always: the arms given back to the game's pose (F4)
        const std::string note = skel::DollTakeNote();
        for (size_t at = 0; at < note.size();) {
            const size_t end = std::min(note.find('\n', at), note.size());
            Log(("vrcam: " + note.substr(at, end - at)).c_str());
            at = end + 1;
        }
    }
    in.posedWeapon = meleetouch::Wanted() && FullBody();
    in.dualBlades = in.posedWeapon && bladeshape::DualWield(gamestate::HandKind(0), gamestate::HandKind(1));   // a blade in each hand: both bones kept   // [gameplay] melee_touch (F4 only): skel::PosedWeapon is made only while asked for
    // The grip is taken again for any change of what the two hands hold, not only of the kind: a barbarian's
    // axe and shield, then the shield for a dagger - still "Axe", the dagger hung where the shield's stance had
    // the left attach bone, out of the left hand (2026-10-06). Kept in the bits above the kind's own key.
    const int handsKey = (int)(gamestate::HandsKey() & 0x7FFFu) << 16;
    in.carryAxis = g_set.carryAxis.load();
    in.carryM = g_set.carryCm.load() * 0.01f;
    in.bothAttach = g_set.bothAttach.load() && (gamestate::TwoHanded() & D2RVR_TWO_HANDS_ON) && in.weaponType != D2RVR_TYPE_BOW && in.weaponType != D2RVR_TYPE_CROSSBOW;
    if (in.bothAttach) {   // which attach bone the game shows the weapon on, when that changes
        static int told = -1;
        float sz[2]; skel::AttachSizes(sz);
        const int now = (sz[0] > 0.5f ? 1 : 0) | (sz[1] > 0.5f ? 2 : 0);
        if (now != told) { told = now; LogF("vrcam: two-handed weapon's attach bones as the game animates them: R %.2f L %.2f", sz[0], sz[1]); }
        static ULONGLONG diagAt = 0;   // what the game's animation does with them, once a second ([debug] weapon_diag)
        if (g_set.weaponDiag.load() && GetTickCount64() - diagAt > 1000) {
            diagAt = GetTickCount64();
            float d[14]; skel::WeaponDiag(d);
            LogF("vrcam: 2H diag (game pose) R attach vs grip %.1f cm %.0f deg | L attach vs grip %.1f cm %.0f deg | skins apart %.1f cm | "
                 "attaches apart %.1f cm | from right wrist R %.1f L %.1f cm | walking %d", d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7], g_walkSteady.load() ? 1 : 0);
            LogF("vrcam: 2H ours: R attach vs grip %.1f cm %.0f deg (as we leave it) | det L %.2f R %.2f", d[10], d[11], d[12], d[13]);
            Log(("vrcam: 2H item: " + skel::WeaponItemDiag()).c_str());   // LogF stops at 256
            Log(("vrcam: 2H entity: " + skel::WeaponEntityDiag()).c_str());
        }
    }
    in.gunStock = in.staff && in.weaponType == D2RVR_TYPE_CROSSBOW;
    // a sword in both hands is held otherwise than in one (the game's 2HS animation): its own grip
    if (likeStaff == 2) in.weaponType += 0x100;
    // and the grip is taken relative to the wrist it hangs on: another wrist, another grip
    if (in.staff) in.weaponType += 0x1000 * (in.staffHand + 1);
    in.weaponType += handsKey;
    // Every weapon sits fast in its grip: following the game's animation, the bow,
    // the crossbow and the rest jumped about in the hand at every shot or blow
    // ("fix it all, no movement", 2026-10-05). Its own parts still move (a bow's
    // string). Javelins and throwing weapons too for now - a thrown one is hidden
    // by the game shrinking its bone, which stays the game's (skeletons.cpp);
    // that it flies out of the hand comes back later as an exception.
    in.holdGrip = true;
    in.attacking = g_skillHeld.load();
    for (int i = 0; i < 3; ++i) in.staffOffset[i] = g_set.staffOffset[i].load();
    for (int i = 0; i < 3; ++i) in.leftStaffOffset[i] = (in.gunStock ? g_set.xbowLeftOffset[i] : in.leftHilt ? g_set.leftSwordOffset[i] : g_set.leftStaffOffset[i]).load();
    // Weapon Adjust: the same for every two-handed weapon ("for all of them", 2026-10-06 - it was per kind);
    // a crossbow keeps its own (xbow_left_*), its left hand sits under the stock
    for (int i = 0; i < 3; ++i) in.leftStaffTurnDeg[i] = in.gunStock ? 0.0f : g_set.leftHoldTurn[i].load();
    if (!in.gunStock)
        for (int i = 0; i < 3; ++i) in.leftStaffOffset[i] += g_set.leftHold[i].load();
    in.leftStaffRollDeg = in.gunStock ? g_set.xbowLeftRoll.load() : 0.0f;
    in.leftStaffAtM = in.gunStock ? g_set.xbowLeftAtCm.load() * 0.01f : -1.0f;
    {   // the crossbow's left hand (vr/xbow_grip.h): VR first person (F4) only, each by its own key; elsewhere as before
        const bool f4 = in.gunStock && FullBody();
        in.xbowLatch = f4 && g_set.xbowGripLatch.load();
        in.xbowWrist = f4 && g_set.xbowWristTracked.load();
        // a grip taken from a wide pose has the left hand off the crossbow: only with the wrist on the controller is that hand not read from it
        in.xbowWide = f4 && g_set.xbowGripWide.load() && in.xbowWrist;
        in.xbowModel = f4 && g_set.xbowSupportModel.load();
        in.xbowDropM = g_set.xbowSupportDropCm.load() * 0.01f;
        in.xbowBackM = g_set.xbowSupportBackCm.load() * 0.01f;
        in.xbowBarrelDebug = f4 && g_set.xbowBarrelLine.load();
        in.xbowAimBarrel = f4 && g_set.xbowAimBarrel.load();
        in.xbowHoldGrip = f4 && g_set.xbowHoldGrip.load();
        in.xbowSupportGame = f4 && g_set.xbowSupportGame.load();
        in.xbowSupportClose = f4 && g_set.xbowSupportClose.load();
        in.xbowHandover = f4 && g_set.xbowHandover.load();
        in.xbowBarrelForward = f4 && g_set.xbowBarrelForward.load();
        static std::string toldBarrel;   // what the barrel's reading says, on change
        // the barrel as drawn needs the renderer's world-matrix function hooked (potion_hide's hook, shared; it only reads for us)
        potionhide::WantHooks(in.xbowModel || in.xbowBarrelDebug || in.xbowAimBarrel);
        if (in.xbowModel || in.xbowBarrelDebug || in.xbowAimBarrel) {
            const std::string why = skel::XbowBarrelWhy();
            if (why != toldBarrel) { toldBarrel = why; Log(("vrcam: crossbow barrel (xbow_support_model): " + why).c_str()); }
        } else {
            toldBarrel.clear();
        }
        const std::string note = skel::XbowNoteTake();
        for (size_t at = 0; at < note.size();) {
            const size_t end = std::min(note.find('\n', at), note.size());
            Log(("vrcam: " + note.substr(at, end - at)).c_str());
            at = end + 1;
        }
    }
    in.staffGrabM = g_set.staffGrabCm.load() * 0.01f;
    if (const uint32_t type = gamestate::WeaponType(); type < D2RVR_TYPE_COUNT) memcpy(in.weaponAdj, g_set.weaponAdj[type], sizeof in.weaponAdj);
    memcpy(in.shrinkBone, g_set.shrinkBone, sizeof in.shrinkBone);
    in.lockUpper = own && FullBody() && g_set.lockUpper.load();   // the game's animation in the other views
    {   // [body] crouch (vr/crouch.h; from bearstonem/d2r-pcvr, MIT): you crouch, the camera goes down with your head, and the hero's
        // body goes down as far, his legs bent to keep his feet. VR first person (F4) only, its own tick, off by default: off, or in
        // F1, F2, F3 and the flat views, crouchDrop stays 0 and PoseBody is as before. The drop in metres becomes model units at
        // the camera's own scale - the hero's bind-pose eyes over your eye height (measured, [calibration], or set) - so the neck
        // lands under the lowered camera; nothing tuned by hand. Drawn bones only: no game state, hit box or movement changes.
        static crouch::Tracker tracker;
        static ULONGLONG last = 0;
        const ULONGLONG now = GetTickCount64();
        const float dt = last ? (float)(now - last) * 0.001f : 0.0f;
        last = now;
        const bool want = own && FullBody() && g_set.crouch.load();
        static bool toldOn = false;
        static int toldLegs = -2, toldCrouches = 0;
        if (want != toldOn && g_set.crouch.load() == want) {   // the tick, not a view change
            toldOn = want;
            Log(want ? "vrcam: crouch - the hero crouches when you do ([body] crouch, F4)" : "vrcam: crouch - off, the hero stands as the game poses him");
        }
        if (!want) {
            tracker.Reset();
        } else {
            const ULONGLONG at = g_headDownAt.load();
            const bool fresh = at && now - at < 250;   // the camera follows the head's height and drew a moment ago
            crouch::Params p;
            p.enterM = g_set.crouchCm.load() * 0.01f;
            p.floorM = UserEyeHeightM();
            const bool was = tracker.Crouched();
            const float downM = tracker.Step(fresh, g_headDownM.load(), dt, p);
            in.crouchDrop = downM * std::max(1.0f, ViewHeight()) / UserEyeHeightM() / std::max(0.1f, g_heroScale.load());
            if (tracker.Crouched() && !was && toldCrouches < 5) {
                ++toldCrouches;
                LogF("vrcam: crouch - the head %.0f cm below its standing height (threshold %.0f cm): the body goes down with it", g_headDownM.load() * 100.0f,
                     p.enterM * 100.0f);
            }
            if (const int legs = skel::CrouchLegs(nullptr); legs >= 0 && legs != toldLegs) {
                toldLegs = legs;
                Log(legs ? "vrcam: crouch - the hero's legs found (hip, knee, foot each side): they bend to keep his feet"
                         : "vrcam: crouch - this hero's rig has no leg chain found (a bone named foot or ankle under knee and hip): he stays standing");
            }
        }
    }
#endif
    in.legsUnder = g_set.legsUnder.load();
    // [body] flat_hold_trunk: flat first person (F3), the game's own animation - a swing took the torso out from under the camera. As F4
    // holds the body under the head: the trunk's axis in the bind pose, the hips under it, the arms still the game's (skeletons.cpp PoseBody)
    in.holdTrunk = own && g_set.platform.load() == 0 && g_set.flatHoldTrunk.load();
    in.walkBack = g_set.walkBack.load();
    in.walkBackReverse = g_set.walkBackReverse.load();
    in.heroAnimMode = HeroAnimMode();   // (walk_back_reverse keeps the game's walk and run only - not a Whirlwind, a Leap, a knockback)
    in.followJump = own && g_set.followJump.load();
    in.jumpFrom = g_set.jumpFrom.load();
    {   // the hero's own eye height, once per hero (class) and when it changes
        static float toldEye = 0.0f;
        float eye = 0.0f, lift = 0.0f;
        if (skel::HeroEye(&eye, &lift) && fabsf(eye - toldEye) > 0.01f) {
            toldEye = eye;
            LogF("vrcam: the hero's eyes stand %.2f model units up = %.2f world units (scale %.3f)%s",
                 eye, eye * g_heroScale.load(), g_heroScale.load(),
                 g_set.heightAuto.load() ? " - the camera's height" : " - unused, [camera] height_auto=0");
        }
    }
    static int toldLock = 0;
    if (const int ls = skel::LockState(); ls != toldLock) {
        toldLock = ls;
        Log(ls > 0 ? "vrcam: bind pose read - the body above the pelvis can be held still"
                   : "vrcam: the bind pose did not read as a pose - [body] lock stays off");
    }
    // a measured arm span fits the arm bones to it (skeletons.cpp) and the hands stay 1:1: [arms] scale counts as 1 then
    in.armSpanM = calib::MeasuredSpanM();
    // [calibration] default_span: nothing measured - fitted all the same, to a person's span for the eye height (skeletons.cpp works it out from userHeadM); F4 only
    in.defaultSpan = !(in.armSpanM > 0.0f) && g_set.defaultSpan.load() && viewgate::F4();
    in.scale = in.armSpanM > 0.0f || in.defaultSpan ? 1.0f : g_set.armScale.load();
    in.fullReach = g_set.fullReach.load() && viewgate::F4();   // [arms] full_reach: F4 only
    calib::NoteSpanInUse(in.armSpanM > 0.0f ? in.armSpanM : in.defaultSpan ? bodycal::DefaultSpan(UserEyeHeightM()) : 0.0f);   // the headset panel's Arm span stepper shows it
    in.wrist = g_set.wrist.load();
    in.wristDeg[0] = g_set.wristPitch.load(); in.wristDeg[1] = g_set.wristYaw.load(); in.wristDeg[2] = g_set.wristRoll.load();
    float yaw = 0.0f;
    in.handOffset[0] = g_set.handSide.load() * 0.01f;
    in.handOffset[1] = g_set.handUp.load() * 0.01f;
    in.handOffset[2] = -g_set.handFwd.load() * 0.01f;
    bool eyeOk = false;
    in.haveYaw = on && (in.mode == 2 || in.bodyTurn > 0) && CameraYawInModel(&yaw, in.eyeModel, &eyeOk);
    in.haveEye = in.haveYaw && eyeOk;
    // the offset turns arms and body together: both read "ahead" from this yaw
    in.yawInModel = in.haveYaw ? g_set.facingSign.load() * yaw + g_set.bodyYawDeg.load() * 0.0174532925f : 0.0f;
#if D2RVR_FIRST_PERSON
    // [weight] grab_bodies (vr/grab_wire.cpp): F4 only; a hand may take a body when it is empty - no item in its slot, or the weight model's weapon not in it
    // (dropped, on the back) - holds no bottle, its grip is no other gesture's (the swap behind a shoulder, the bow's string, the throw) and the weight model
    // would not take the weapon with it (TakeZone: the weapon first, as for the belt)
    in.itemsOn = on && in.mode == 2 && g_set.weightGrabItems.load() && viewgate::F4();   // [weight] grab_items
    // [weight] grab_props: the same hands take the level's props (knock_props' world) - either box ticked
    in.grabBodies = g_set.weightGrabBodies.load();
    in.grabProps = g_set.weightGrabProps.load() && knockprops::GrabReady();
    in.grabOn = on && in.mode == 2 && (in.grabBodies || in.grabProps) && viewgate::F4();
    in.grabBodies = in.grabBodies && in.grabOn;
    in.grabProps = in.grabProps && in.grabOn;
    {
        const uint32_t wc = gamestate::WeaponClass();
        const bool ranged = wc == 3 || wc == 4;   // D2RVR_WEAPON_BOW, D2RVR_WEAPON_CROSSBOW: the bow hand holds it, the string hand is free (grabbodies::BusyWhy)
        const int bowHand = ranged ? AimHand() : -1;
        for (int side = 0; side < 2; ++side) {
            grabbodies::HandCase c;
            c.weightOn = in.weight.on;
            c.weightHolds = in.weight.on && weight::HoldsWeapon(side);
            c.secondHere = in.weight.on && weight::SecondHand() == side;
            c.weightSlot = in.weight.on && side == in.weight.slot;
            c.slotFree = in.handFree[side];   // (the staff in both hands too: vrcam's own word)
            c.ranged = ranged;
            c.bowHand = side == bowHand;
            c.bottle = engitem::BeltBusy(side);
            c.shoulder = shoulderwire::Claims(side);
            c.bowString = bowdrawwire::HoldsGrip(side);
            c.throwGrip = throwwire::GripIsThrowsAt(throwhand::XrOfSide(side));
            {   // the weapon taken first only where the grip takes it in reach - its shaft (1) or the back (2); its call from afar (3) gives way to a body under the palm
                const int zone = in.weight.on ? weight::TakeZone(side, nullptr) : 0;
                c.takeZone = zone == 1 || zone == 2;
            }
            in.grabBusy[side] = grabbodies::BusyWhy(c);
            in.grabFree[side] = in.grabOn && in.grabBusy[side] == 0;
            // [weight] grab_items: free as for a body, and the bodies' grab not over a body or holding one with it
            in.itemsFree[side] = in.itemsOn && in.grabBusy[side] == 0 && !(((grabwire::OverBody() | grabwire::Holding()) >> side) & 1u);
        }
    }
    in.itemsReachM = g_set.weightGrabItemsReachCm.load() * 0.01f;
    in.grabReachM = g_set.weightGrabReachCm.load() * 0.01f;
    in.grabBreakM = g_set.weightGrabBreakCm.load() * 0.01f;
    in.grabArmReach = g_set.weightGrabArmReach.load();   // [weight] grab_arm_reach
    in.armWorldLate = g_set.orbDrawnHand.load();   // [hud] hand_vitals_orb_drawn_hand
    in.armBox = handvitals::OrbsOn() && handvitals::Settings().orbOnBone;   // [hud] hand_vitals_orb_on_bone: the forearms' boxes only while the balls want them
    in.grabStrength = g_set.weightStrength.load() * 0.01f;   // ([weight] strength: the hands' strength, as on the weapon)
    in.grabWrap = in.grabOn && g_set.weightGrabWrap.load();   // [weight] grab_wrap (with grab_bodies or grab_props)
    in.grabMuscle = g_set.weightGrabMusclePct.load() * 0.01f;   // [weight] grab_muscle_pct
#endif
    skel::Set(in);
}

}  // namespace vrc
