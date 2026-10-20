// skeletons.h - what vrcam hands the skeleton hook (skeletons.cpp) every 10 ms.
#pragma once

#include <cstdint>
#include <string>
#include "d2r_vr_build.h"

namespace skel {

struct Input {
    int mode = 0;               // arms: 0 game animation, 1 test pose (arms ahead), 2 controllers
    int bodyTurn = 0;           // 0 the game's facing, 1 above the pelvis, 2 whole body faces where the camera looks
    bool hideHead = false;      // shrink the hero's head away (first person)
    bool lockUpper = false;     // nothing above the pelvis follows the game's animation: bind pose, then ours
    bool legsUnder = false;     // with lockUpper: pelvis and legs kept under that body - no lunge ahead, no hip turn
    float crouchDrop = 0.0f;    // [body] crouch (F4): the body this far down (model units), the legs bent to keep the feet; 0 = as the game poses it
    bool wrist = false;         // mode 2: the hands turn with the controllers, not only follow the forearm
    float wristDeg[3] = {};     // pitch, yaw, roll on top, in the controller's own axes (mirrored for the left)
    float scale = 1.0f;         // arm reach on top of the hero/user height ratio
    float userHeadM = 1.7f;     // the user's head above the floor
    float armSpanM = 0.0f;      // > 0: the measured grip-to-grip span (T-pose, [calibration]): the arm bones are fitted to it, scale is 1
    uint32_t handsValid = 0;    // bit 0 right, bit 1 left
    float hand[2][3] = {};      // [0] right, [1] left: metres from the head, x right y up z back
    float rot[2][4] = {};       // controller orientation in that frame, x y z w
    float handOffset[3] = {};   // added to both hands, metres in the same frame (x right, y up, z back)
    bool haveEye = false;       // eyeModel is known: hands hang off the camera, not the hero's head bone
    float eyeModel[3] = {};     // the camera's eye point in the hero's model space
    bool haveYaw = false;       // yawInModel is known (the hero's model matrix is found)
    float yawInModel = 0.0f;    // where the camera looks, as a yaw in model space (0 = model forward)
    bool followJump = true;     // the held upper body (and HeroEye's lift) goes up with the hips
    float jumpFrom = 0.3f;      // model units of hip rise taken for the stride's bob, not a jump
    float grip[2] = {-1.0f, -1.0f};   // the controllers' grips 0..1 ([0] right); < 0 unknown (an older bridge): the left stays on a staff
    // The posed weapon for PosedWeapon() below (melee by touch): wanted only when asked, so nothing is done for it otherwise.
    bool posedWeapon = false;
    bool dualBlades = false;    // a melee weapon in each hand's slot: PosedWeapon() then keeps the other hand's weapon bone too (blade[])
    uint32_t poseStamp = 0;     // the stamp of the hand sample this pose is made from (sampleStamp of the shared block)
    bool havePoseStamp = false;
    uint32_t worldEpoch = 0;    // bumped whenever the world frame turns under a still hand (a snap turn, a recenter, a view change)
    // The head sample the hands came with (BodyWalk's counter) and where that head stood in the room:
    // what puts the controllers in the room, where a weapon with weight lives (vr/weight.cpp).
    uint32_t sample = 0;
    float headRoom[3] = {};     // metres, OpenXR stage axes (no room from BodyWalk: 0, the head's height, 0)
    float headYawDeg = 0.0f;    // the turn the hands' frame has in the room (positive = left)
#if D2RVR_FIRST_PERSON
    // [weight] (vr/weight.cpp): the weapon in hand is a body of its own - heavy, taken by a hand where its
    // grip closes on the shaft, passed from hand to hand. on: this pose's weapon is held that way.
    struct Weight {
        bool on = false;
        int kind = 0;              // heft::hold::Kind
        bool twoHanded = false;    // the game's "2handed": a longer, heavier shape
        int slot = 0;              // the hand slot the game has it in (0 right, 1 left)
        bool otherBusy = false;    // the other hand holds something of its own (a shield): it takes nothing
        int shieldArm = -1;        // the arm the shield is on when [hands] shoulder_draw handles it (0 right, 1 left, 2 put away): with shieldFollow it is opposite the weapon's hand, so no hand is busy with it; -1: not handled - otherBusy says it
        bool shieldFollow = true;  // [hands] shoulder_draw_shield: the shield jumps to the other arm when the weapon comes into its hand. Off: the hand that wears the shield is busy with it (cannot take the weapon)
        bool busy[2] = {};         // a belt potion in that hand, or one lit to be taken: it takes nothing
        int tipAxis = 2;           // the weapon bone's axis toward its tip, +-1..3 (x y z)
        float feel = 0.5f;         // [weight] feel % / 100: the steel's mass x this (default 50 %)
        float strength = 1.0f;     // [weight] strength % / 100: the muscles' ceilings x this
        float gravity = 1.0f;      // [weight] gravity_pct / 100: the steel's weight x this (0 = only inertia)
        float damping = 1.0f;      // [weight] damping_pct / 100: the muscles' dampers x this
        bool gravityFeel = false;  // [weight] gravity_feel: the soft wrists (cleanroom/heft/hold.h UseGravityFeel); off = the old stiff ones
        int handsModel = 0;        // [weight] hands_model: 0 our own physics; 1 the weight-physics library (vr/weight_lib.h), when it is loaded - else ours
        unsigned handsModelFlags = 0;   // [weight] hands_model_palm: bit 0, the library's weight in the palm
        float grabM = 0.12f;       // [weight] grab_cm: how near the shaft a grip takes it
        float letGoM = 0.25f;      // [weight] let_go_cm: hands pulled this far apart off their places let go
        float fist[3] = {};        // [weight] fist_x/y/z: the closed fist's middle from the right controller, its axes, m
        // The grips as the controllers really have them (direct_input's frame, vr/weight_wire.h): a grip that holds the weapon is taken from
        // the layout, and a taken one reads as let go in Input::grip - so the weight model reads these when there are any.
        bool haveRawGrip = false;
        float rawGrip[2] = {};     // [0] right, [1] left
        bool haveRawTrigger = false;
        float rawTrigger[2] = {};  // the triggers the same way (0..1): only the grip library's loose hold reads them
        int gripModel = 0;         // [weight] grip_model: 0 our own way of holding; 1 the grip library's (vr/grip_lib.h), when it is loaded - else ours
        // [weight] hero_strength (vr/hero_strength.h): the hero's Strength against the weapon's requirement scales the steel's mass and the hands' strength - in both models. heroStr: the
        // hero's Strength stat from the game (< 0 not read: nothing is scaled); reqStr: the weapon's Strength requirement from the game's tables (< 0 not known: a typical one for its
        // kind); strengthClassPct: his class' multiplier in % ([weight] str_<class>); strengthEffect: how much it matters in % ([weight] strength_effect).
        bool heroStrength = false;
        int heroStr = -1, reqStr = -1;
        float strengthClassPct = 100.0f, strengthEffect = 100.0f;
        bool claimed[2] = {};      // that hand's grip is another gesture's (the weapon swap behind a shoulder): it takes nothing off the floor and calls
                                   // nothing back - but it does take a weapon stowed on the back (busy, a belt potion, blocks that too)
        // [weight] after_drop: what becomes of a weapon let go that has landed. 0 it lies on the floor; 1 after stowAfterS it goes to the hero's back
        // and is drawn slung there (vr/weapon_back.h); 2 after stowAfterS it is not drawn at all (still the game's equipped weapon).
        int afterDrop = 0;
        float stowAfterS = 0.6f;   // [weight] to_back_delay_ms / 1000
        // Metres from the head to the game's ground under the hero (vr/weight_floor.h): the floor a let-go weapon falls to is that far below the head in the
        // room, not the room's floor. <= 0: not known - the room's floor.
        float headAboveGroundM = 0.0f;
        bool slide = false;           // [weight] slide_second_hand: the second hand slides along a long shaft while its grip stays squeezed (cleanroom/heft/slide.h)
        bool hiltOnly = false;        // [weight] grip_hilt_only: the second hand takes a sword, a dagger, a scepter, an axe or a mace only on its hilt (vr/grip_zone.h)
        float hiltCm[5] = {};         // [weight] grip_hilt_cm_<kind> (vr/grip_zone.h kKinds): the hilt's length from the pommel, cm; 0 = the weight model's own
        uint32_t slideKinds = 0xFFFFFFFFu;   // [weight] slide_second_hand_<kind>: the weapon kinds whose second hand slides (vr/slide_kinds.h, one bit each); all: every long shaft's, as before the list
        // The wood's extent ahead of and behind the weapon bone's origin, metres - the hit-by-touch blade's (bladeshape::ForItem, tuned): the weight model's
        // shaft is the drawn bone's axis with these ends (vr/weapon_shaft.h). tipAxis (above) is then the blade's: +2, or -2 where the tip lies along -Y.
        float shaftTipM = 0.0f, shaftButtM = 0.0f;   // 0, 0: not known - the shaft is not measured
        float backTakeM = 0.25f;      // [weight] back_take_cm / 100: a fist this near the weapon stowed on the back takes it with the grip, anywhere along it
        float thrownHandoffMs = 350.0f;     // [weight] thrown_handoff_ms: the thrown model flies out of the hand this long (or to the game's action frame) before it hands over to the game's missile and vanishes (0: at once)
        float thrownHandEmptyMs = 900.0f;  // [weight] thrown_hand_empty_ms: the hand is empty at the most this long after the game took a throw (0: not hidden)
        int thrownModel = 0;        // [weight] thrown_model: 0 the model vanishes the moment the game takes the throw (its own missile flies), 1 it flies on physically
        bool thrownPotion = false;  // the weapon in hand is one of the six throwing potions (gamestate::HandIsThrowPotion)
        bool potionFlies = true;    // [weight] potion_model_flies (or the potion tick): a thrown potion's model is not handed over to the game's missile - it flies to its landing and breaks there
        float thrownFlyMs = 3000.0f;  // [weight] thrown_fly_ms: a thrown weapon's model flies at most this long after the release (vr/weight_throw.h)
        // The throw worked out by the weight model (weight::LastThrow, vr/weight_flight.h): the horizontal release velocity scaled by throwReachPct (a real arm
        // throws a few metres), the landing held to throwMaxCells cells (the game refuses an aim point more than 50 cells off the hero on an axis) - a cell is two
        // world units, unitsPerMetre = world units per metre at the hero's size (<= 0: not known, the cap is not applied).
        float throwReachPct = 800.0f, throwMaxCells = 48.0f, unitsPerMetre = 0.0f;
        float takeHaptic = 0.0f;   // [weight] take_haptic: the pulse (amplitude 0..1) in a hand entering the take zone behind a shoulder while a weapon is away; 0 = none
        bool recallAny = false;    // [weight] recall_any_hand: the grip of any free hand, anywhere, calls a weapon that is away
        // [hands] shoulder_draw (vr/shoulder_draw.h): a weapon let go lies where it fell - in the WORLD, not carried by the hero's walking - and the grip of either hand calls it back
        // while it is in the air or lies there; after drawStowS on the floor it goes to the back, and from then on a hand behind a shoulder draws it into that hand, whichever hand it
        // is (afterDrop is then 1 for the skeleton's sake: a weapon taken by the other hand is drawn where the body model has it).
        bool drawMode = false;
        float drawStowS = 1.0f;    // [hands] shoulder_draw_stow_ms / 1000: how long it lies before it goes to the back
        bool shieldZone = false;   // [hands] shoulder_draw_shield_stow: the LEFT shoulder is the shield's (put it away, draw it): the weapon is drawn from the right one only
        // The game's world under the room, as of this pose (vrcam fills it in FreshHands): the camera's place in the world (render world units), the head frame's right and forward there
        // and the world units per metre - vr/world_anchor.h carries a weapon that lies on the floor by how they change from one head sample to the next. worldOk false: not known.
        bool worldOk = false;
        float worldEye[3] = {}, worldCamR[3] = {}, worldCamF[3] = {};
        float worldUpm = 0.0f;
        bool backLong = false;     // the weapon is a long one (a staff, a spear, a polearm, a javelin, anything two-handed): its place on the back
        float back[6] = {};        // that class's live tuning: x y z in cm, pitch yaw roll in degrees (weaponback::Tuning)
        // [weight] grip_fit (vr/grip_fit.h): a hand on the weapon is laid on the shaft as drawn. A hand that TOOK it (closed its fist on the shaft: the second hand, one that took it
        // over from the other, the game's hand taking it up again) has its palm's middle put on the shaft's axis at its grasp point and its knuckle line laid along the shaft - and,
        // with gripLikeBase, sitting on it the way the game's own grip has the base hand (offset from the axis, lean) - and closes with the controller's squeeze (also with [hands]
        // fist off). Every hand has its own live corrections in the shaft's own frame, by HAND (0 right, 1 left): x y z in cm (along the shaft toward the tip, across toward the
        // fingertips, up out of the palm), then the roll about the shaft and the two tilts in degrees. The base hand (the game's, as placed) keeps the game's pose, only these act.
        bool gripFit = false;
        bool gripLikeBase = true;
        // [weight] grip_fit_fingers (vr/grip_pose.h): the hand that took the weapon has the game's own weapon-grip POSE of the fingers and the thumb - read off the game's hand (the
        // slot's) and put on it mirrored as the hand needs (in the hand's own frame, whatever the rig's axes) - held while it holds the weapon, not driven by the squeeze, opening
        // as it lets go. Needs gripFit; off: the fingers close with the squeeze as before.
        bool gripFingers = true;
        // [weight] attach_apart: the weapon's two attach bones are kept apart along the weapon's own axis ([hands] carry_cm, carry_axis) - the game lays a two-handed weapon along the line
        // from one to the other, and with both in one point the weapon is squashed flat along its length. Off: both in one point.
        bool attachApart = true;
        float gripHand[2][6] = {};
    } weight;
    bool staff = false;         // mode 2, a staff in hand: it is turned to run through the other hand too
    int staffHand = 1;          // the hand it is attached to: 0 right, 1 left
    float staffOffset[3] = {};  // cm: where the staff runs past the other hand, along that hand's own axes
    bool staffHands = true;     // both hands turn with the staff (as the game's animation grips it), not with the controllers
    // [hands] staff_free_left: the right hand holds the staff fast (no turning
    // it through the other hand); the left is free, and its grip puts it on the
    // staff, sliding along it with the controller.
    bool staffFreeLeft = false;
    // [hands] fist: a hand holding nothing closes into a fist with its grip
    // and relaxes when it is let go. handFree: holds nothing (vrcam's word,
    // from the game's hand slots); the left on a staff is not free.
    bool fist = false;
    bool handFree[2] = {};
    // [hands] shield_fist: the left hand with a shield on it closes with the controller's grip like a free hand - the fingers only,
    // the shield's bone and the arm stay as the game draws them (vr/shield_fist.h). shieldFist: asked for (box ticked, F4, a shield
    // in the left slot); shieldRaw: the grips as the controllers really have them (before the layout took any; [0] right, [1] left), < 0 not
    // known: grip[] then. With shieldArm (below) the fist is on the hand of the arm the shield is on.
    bool shieldFist = false;
    float shieldRaw[2] = {-1.0f, -1.0f};
    // [hands] shoulder_draw, the shield (vr/shield_arm.h, shield_arm_wire.cpp): shieldHolster asks for it (vrcam: the box ticked, F4, a shield in the left slot); shieldArm
    // is the answer, put in by shieldarmwire::Apply in the hook before the weight model: the shield follows the sword to the forearm opposite the hand that holds it - 0 the RIGHT
    // forearm (mirrored), 1 the left (the game's own place). -1: not asked - the old logic, the left arm only.
    bool shieldHolster = false;
    int shieldArm = -1;          // 0 right, 1 left, 2 put away (not drawn)
    bool shieldFollow = true;    // [hands] shoulder_draw_shield: weapon and shield always on opposite arms
    bool shieldStow = false;     // [hands] shoulder_draw_shield_stow: a grip behind the left shoulder puts the shield away and draws it (by a hand that holds nothing)
    bool shieldBusy[2] = {};     // a hand holds a belt potion: it neither puts the shield away nor draws it
    float shieldTick = 0.0f;     // the pulse (0..1) in a hand entering the left shoulder while the shield is put away; 0 none
    // [hands] shoulder_draw_ranged (vr/ranged_hand.h, ranged_hand_wire.cpp): a bow or crossbow put away behind a shoulder and drawn from it, with the swap into the hand that reached.
    // rangedHolster asks for it (vrcam: shoulder_draw and the tick, F4, a bow or crossbow is the weapon); rangedSwap/rangedBow/rangedHome tell the rules, rangedBusy[side] that the hand
    // has the string. The answer (put in by rangedwire::Apply in the hook): rangedStowed - put away: not drawn, nothing posed round it; rangedSide - the hand that holds it now (0 right,
    // 1 left), -1 when not handled. rangedBone: the side of the attach bone the game hangs the item on (0 right, 1 left), for when the bones' sizes do not tell.
    bool rangedHolster = false, rangedSwap = false, rangedBow = true;
    int rangedHome = 1, rangedBone = 1;
    bool rangedBusy[2] = {};
    bool rangedStowed = false;
    int rangedSide = -1;
    // [hands] forearm_twist: the forearm turns with the wrist (vr/forearm_twist.h) - the controller's roll about the forearm, read
    // against the rig's bind pose, is shared: the forearm bone forearmTwist of it (0..1), a twist bone along the forearm more the nearer
    // the wrist, the hand the rest. < 0: off (the forearm keeps the animation's roll, as before). vrcam: F4 only.
    float forearmTwist = -1.0f;
    // [shield] forearm: the shield on the left arm is strapped to the forearm, not hung on the hand - the rig's own bind pose of its
    // attach bone against the forearm (no animation, no Weapon Adjust of the weapon in the other hand), turned with the forearm's twist
    // where it sits, then [shield] x y z (cm) / pitch yaw roll (deg) in the attach bone's own axes. vrcam: F4 only, a shield in the
    // left slot.
    bool shieldForearm = false;
    bool shieldBlink = false;   // [debug] shield_blink: the strapped shield's bone is hidden half a second in every two (does the shield on the screen blink?)
    float shieldAdj[6] = {};
    // [fist] <class>_close / <class>_open, % / 100: how far a free hand closes with the grip
    // squeezed and with it let go, per hero class (kHeroClasses) - the rigs differ: the full
    // fist rolled the paladin's fingers into his palm (2026-10-06).
    float fistClose[8] = {1.0f, 1.0f, 1.0f, 0.65f, 1.0f, 1.0f, 1.0f, 1.0f};
    float fistOpen[8] = {0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f};
    // >= 0: the hand closes at most this far (a part, as fistClose) - a belt potion in it ([hands] belt_hold_close)
    float fistCap[2] = {-1.0f, -1.0f};
    float staffGrabM = 0.15f;   // the grip takes the staff only with the left hand this close to it (metres)
    float leftStaffAtM = -1.0f;      // >= 0: the left hand takes the shaft at this one place, metres ahead of the right fist (a crossbow); < 0 slides
    float leftStaffRollDeg = 0.0f;   // the left hand on the shaft turned about it (a crossbow's: from below, palm up)
    float leftStaffOffset[3] = {};   // cm: the left hand on the staff moved along its own axes, until the fist closes round the shaft
    float leftStaffTurnDeg[3] = {};  // deg: and turned about its own x, y, z, round its point on the shaft ([hands] left_hold_pitch/yaw/roll)
    // A two-handed sword ([hands] sword_two_hands): the left hand takes the hilt
    // leftHiltAtM from the right fist (toward where the game's grip has the left
    // hand; < 0 the other way) and slides at most leftHiltSlideM either side of
    // it. It only holds: the sword points where the right hand does.
    bool leftHilt = false;
    float leftHiltAtM = 0.1f;
    float leftHiltSlideM = 0.04f;
    // The bone axis the shaft or hilt runs along, from the right fist toward where the left
    // hand takes it, +-1..3 (x y z); 0 = the one nearest the palms in the game's grip. The
    // game's two-handed sword animation keeps the left hand off the hilt: that gave its X,
    // across the blade (2026-10-06); the hilt is its -Y.
    int shaftAxis = 0;
    // The wrist the game hangs it on is read from its attach bones when the grip
    // is taken (the empty one is shrunk), staffHand when they do not tell.
    bool staffHandAuto = false;
    float lineTurnDeg[3] = {34.0f, 15.0f, 0.0f};   // gunStock: the shot line alone (the bolt, the left hand) turned - pitch, yaw, roll about the controller's right, top, forward ([hands] xbow_line_*)
    bool gunFrame = true;       // gunStock: laid in the right controller's frame (GunFrame); false = the game's grip, like the staff ([hands] xbow_gun_frame)
    int stockAxis = 0;          // gunStock: the crossbow's stock as its bone's axis, +-1..3 (x y z); 0 = from the game's grip ([hands] xbow_stock_axis)
    bool gunStock = false;      // a crossbow held as the staff: its stock along the right controller, like a gun
    // The crossbow's left hand (vr/xbow_grip.h), each its own [hands] key, vrcam: VR F4 only (off = as before):
    //   xbowLatch  [hands] xbow_grip_latch: the grip holds it while held, lets go on a weapon / model change and on lost tracking
    //              (the hand free, not on the stock), a squeeze from before takes nothing; no "both hands" fallback while waiting.
    //   xbowWide   [hands] xbow_grip_wide: the game's grip may be taken from a wide pose (placed by the controller anyway).
    //   xbowWrist  [hands] xbow_wrist_tracked: the left hand keeps the controller's turn on the stock (from below or above), its palm's
    //              middle on the place - not turned into the game's grip round the line.
    //   xbowModel  [hands] xbow_support_model: that place is under the crossbow's barrel as drawn (its bolt_helper), xbowDropM below it,
    //              never past the bow's limbs - not on the line set by eye. The shot keeps the old line.
    //   xbowBarrelDebug  [debug] xbow_barrel_line: the barrel and that place drawn (XbowBarrelDebug).
    bool xbowLatch = false, xbowWide = false, xbowWrist = false, xbowModel = false, xbowBarrelDebug = false;
    // [hands] xbow_support_drop_adj_cm / xbow_support_back_adj_cm: xbowModel's place moved from d2r-pcvr's (7 cm below the barrel, 6 cm
    // behind the front of the stock where the limbs meet it), metres - small adjustments around 0
    float xbowDropM = 0.0f;
    float xbowBackM = 0.0f;
    // [hands] xbow_aim_barrel: the shot along the crossbow's barrel as drawn (from bearstonem/d2r-pcvr), the hand-set line only when that
    // is not fresh (100 ms) - vrcam: F4 only
    bool xbowAimBarrel = false;
    // [hands] xbow_hold_grip: the crossbow's grip (where the game's own palm sits on it) in the right palm, not its attach bone's origin
    bool xbowHoldGrip = false;
    // [hands] xbow_support_game: the left hand on the game's own fore-grip of the crossbow (where the game's other hand holds it in its own
    // two-handed hold), learned from the game's pose; before that is learned, under the drawn barrel (xbowModel) or on the shot line
    bool xbowSupportGame = false;
    // [hands] xbow_support_close: on that fore-grip the left hand turned and its fingers closed as the game's own support hand holds it (mirrored)
    bool xbowSupportClose = false;
    // [hands] xbow_handover: the crossbow passed from hand to hand - the free hand takes it with a new squeeze at its grip; the support hand mirrored
    bool xbowHandover = false;
    // [hands] xbow_barrel_forward: the crossbow laid by its own drawn barrel along the controller's forward (its up along the controller's top)
    bool xbowBarrelForward = false;
    // A two-handed weapon in both hands (not a bow or crossbow): the game moves it from one
    // wrist's attach bone to the other's in some animations (a great axe went to the left hand
    // while walking, 2026-10-06) - both attach bones are put where the holding hand has it.
    bool bothAttach = false;
    int carryAxis = 2;
    float carryM = 0.4f;   // [hands] carry_cm / 100   // [hands] carry_axis: the other attach bone goes 40 cm along this axis of the holding one (+-1..3)
    int weaponSide = 0;         // the hand slot the weapon is in (0 right, 1 left), for [debug] bone_axes
    int weaponType = 0;         // D2RVR_WeaponType in hand (d2r_vr_state.h); a new one takes a new grip
    bool holdGrip = false;      // mode 2: the weapon keeps one grip, not the animation's (vrcam: every weapon, 2026-10-05)
    bool attacking = false;     // a skill button is down: no grip is taken from the animation then
    float weaponAdj[6] = {};
    char shrinkBone[48] = {};   // [debug] shrink_bone: the hero's bone of this name drawn shrunk to a point - what hangs on it vanishes    // mode 2: the weapon in the hand moved x y z (cm) and turned pitch yaw roll (deg), its own axes
    // [bow] rig_pose: a bow in hand has its own string, limbs and arrow bones posed by the pull
    // (SetBowDraw, vr/bow_rig.h) instead of the game's animation; off = the game's animation, always.
    bool bowRig = false;
    // [debug] bow_rig_grip: with no SetBowDraw feed, the string hand's squeezed grip is the draw (to try the
    // posing without the draw detector).
    bool bowRigGrip = false;
    // [flail] chain (vr/flail_chain.h): the hero's flail's chain bones are posed from a simulation - the head hangs and swings free with the hand,
    // and a hit by touch (FlailHit) kicks it back - instead of the game's animation of them. vrcam: F4 only. flailDamping: 1/s, how fast a swing
    // dies against the hand ([flail] damping_pct, 100 % = 0.6); flailSwing: the share of the hand's motion felt (0..2, [flail] swing_pct);
    // flailHit: a hit's kick (0..2, [flail] hit_pct). Off = the game's animation of the chain, as before.
    bool flailChain = false;
    float flailDamping = 0.6f, flailSwing = 1.0f, flailHit = 1.0f;
    float flailWeight = 1.0f;   // [flail] ball_weight_pct / 100: the ball's weight against the model's (flailchain::Params::weight)
    // [hands] bow_draw: the posed bow's string place is read (BowString) for the draw's touch point and the debug vectors.
    bool bowString = false;
    // [bow] quiver_orbs: the posed bow's stave (the riser's middle, the upper limb's end, which way the archer is) is read (BowStave) for the orbs along it.
    bool bowStave = false;
    // [bow] arrow_fx: the bow's own arrow as it is drawn (its nock and head) is read (BowArrow) for the loaded skill's fire, frost or arcane light on it.
    bool bowArrow = false;
    // [bow] hand_pose: the free hand of a bow is relaxed and hooks on the string while it is drawn, not the game's animation
    // of it. bowHook: how far the fingers close on the string, 0..1 (the thumb stays loose).
    bool bowHandPose = false;
    float bowHook = 0.85f;
    // [bow] hook_game_pose: with the string taken the Amazon's right hand (the string hand) takes the pose of the game's own bow-draw animation -
    // fingers hooked, the knuckles open, the thumb turned in (vr/bow_hook.h, measured off the animation) - held as it is whatever the controls
    // read, instead of the fist table's hook (which looked like a fist). Any other rig keeps the table's hook (bowHook).
    bool bowHookGame = true;
    // [bow] hook_curl, hook_thumb_deg (live sliders under hook_game_pose): the pose as it is looked at - the four fingers' bend against the
    // measured one (1 = as measured), the thumb turned further in (+) or out (-), degrees (vr/bow_hook.h Tune).
    float bowHookCurl = 1.0f;
    float bowHookThumbDeg = 0.0f;
    // [bow] string_anchor: the bow's string is drawn to the first joints of the hooked fingers (index, middle, ring) - where the game's draw
    // animation has them - not to the controller's grip point in the middle of the palm, a hand's length behind them. bowAnchorCm: moved
    // from there by this many cm along the fingers, toward the thumb side, out of the palm (live). Eased in and out with the draw
    // (bowAimBlendMs). With hand_pose on it needs hook_game_pose and the Amazon's right hand (the rig the pose was measured on); with hand_pose OFF the joints are
    // those of the fingers as the game animates them (any hand); off = the grip point, as before.
    bool bowStringAnchor = true;
    float bowAnchorCm[3] = {0.0f, 0.0f, 0.8f};
    // [bow] aim_along_hands: while the string is taken, the bow (and its arrow, and the bow hand with it) lies along the line from the
    // string hand to the bow hand, vr/bow_aim.h: the hand's plan is turned by the turn that takes the bow's axis onto that line.
    // bowAimBlendMs: the time the turn comes in over and goes out over (take, release, cancel). bowAimRoll: 0 keeps the roll the
    // controller has, 1 keeps the bow upright. bowAxis: the bow's real axis is wanted for the debug vectors ([debug] bow_vectors),
    // whether the turn is on or not.
    bool bowAim = false;
    float bowAimBlendMs = 120.0f;
    int bowAimRoll = 0;
    bool bowAxis = false;
    // [bow] arrow_rest_x_cm / y / z: the point the nocked arrow passes the bow hand at, FIXED in the bow hand's own frame (vr/bow_aim.h
    // SolveRest): this far, cm, from the hand's reference point - x along the bow (+ toward the target), y across it (+ toward the
    // archer's left), z up the limbs - and the arrow (and the bow's turn along the hands) lies on the line from the string hand THROUGH
    // it. All 0 = through the reference point itself, as it was. bowRest: any of them is not 0, with the rig pose (the arrow is ours).
    // It comes in and goes out with the draw over bowAimBlendMs. The arrow bone's (and the bow's turn's) pose only.
    bool bowRest = false;
    float bowRestCm[3] = {0.0f, 0.0f, 0.0f};
    // [bow] arrow_rest_clear: where the arrow's line would pass through the bow hand, the wrist or the forearm (the string hand low or far to a
    // side, the hand turned out) the nock and the arrow are lifted off the arm, up the bow's limb axis, by as little as clears it - at most 8 cm.
    // Off: the point exactly as the offsets above put it. With bowRest.
    bool bowRestClear = true;
    // The hand that takes the string, 0 right 1 left (vrcam: the one opposite the bow's aim hand), for when the draw feed is not
    // there; -1 = not told. The wrist the game hung the bow on is the last resort for the string hand: a bow with a quiver cannot tell it.
    // Told while any of the bow's work is on. The bow hand is the other one, which the bow's rig and the aim use when the attach bones
    // cannot tell (vr/bow_aim.h ChooseBowWrist).
    int bowStringSide = -1;
#endif
};

// The hero classes as the game's model folders name them (data/hd/character/player/<name>/).
inline const char* const kHeroClasses[8] = {"amazon", "sorceress", "necromancer", "paladin", "barbarian", "druid", "assassin", "warlock"};
// The hero's class as last posed: an index into kHeroClasses, -1 not known.
int HeroClass();
// The weapon attach bones' sizes in this pose as the game animates them ([0] right): ~1 shown,
// ~0 the game hides what hangs there - for vrcam's log.
void AttachSizes(float out[2]);
// A two-handed weapon in the game's own pose (vrcam's log): R attach vs its grip cm, deg; L attach vs its
// grip cm, deg; the two skin origins apart cm; the two attach bones apart cm; R and L attach from the right wrist cm.
void WeaponDiag(float out[14]);   // [12] [13]: the game's L and R attach determinants (< 0 mirrored)   // [10] [11]: OUR final R attach against its grip, cm and deg   // [8] [9]: the body's two_hand_weapon_attach from the wrist's, cm and deg, in the game's pose
#if D2RVR_FIRST_PERSON
// HoldWeaponItem's counters since the last call, for vrcam's log (resets them).
std::string WeaponItemDiag();
// The held weapons whose entity's parent is the hero: their TransformComponent local and world.
std::string WeaponEntityDiag();
#endif

void Set(const Input& in);
#if D2RVR_FIRST_PERSON
// Forget the weapon grip: the next calm pose takes it again (F11).
void ResetGrip();
// The hero's forearm as last drawn (side 0 right, 1 left): elbow and wrist,
// and the hand's axis across the arm (turns with the wrist), in BodyWalk's
// hand frame - metres from the head, x right, y up, z back, head yaw only.
// False when the arms have not been posed for a quarter of a second.
bool HeroForearm(int side, float elbow[3], float wrist[3], float across[3]);
// The palm's middle as last drawn (wrist to middle knuckle, halfway, 3 cm out of the palm), the same frame.
bool HeroPalm(int side, float palm[3]);
// [weight] grip_fit, for the debug (vrcam's [debug] phantom_ray): where a fitted hand takes the shaft - the grasp point on its axis - and where its palm's middle really is
// (after the fit and the arm's reach), both from the eye in the head's turn-only frame, metres (as GrabLine). False when that hand was not fitted in the last 300 ms.
bool GripMarks(int side, float grasp[3], float palm[3]);
// [bow] aim_distance_by_arm (vr/arm_reach.h): where each shoulder of the drawn hero stands, and how far it is from there to the controller's grip - the arm as drawn, which is the
// player's own: its bones are fitted to the measured T-pose span ([calibration]), else to the height. `shoulder`: metres from the eye in the head's turn-only frame (x right, y up, z
// back) - the hands' frame, so a hand's position minus it is the arm. `lengthM`: the shoulder to the grip, straight, metres. side 0 right, 1 left. False when the arms have not been
// posed for half a second (no body, not first person with the arms on the controllers).
bool ArmReach(int side, float shoulder[3], float* lengthM);
// [gameplay] shield_smite: the left hand's weapon attach bone (where a shield hangs) as last posed, in the render
// world: its place, the way its Y axis runs, and where the hero stands (all world units - divide by the units per
// metre), with the generation of his model. Posed only while WantShield(true) was asked lately; false when it was
// not posed in the last 100 ms.
struct ShieldPose { float origin[3]; float axis[3]; float player[3]; uint32_t heroGeneration; };
void WantShield(bool on);
// [hands] forearm_twist: the rig's forearm twist bones as found (for the log), and each hand's roll about its forearm as last read
// (degrees; NaN while not twisted). [shield] forearm: 1 strapped to the forearm in the last pose, -1 asked but not (the game hides
// it, no bind pose), 0 not asked.
std::string ForearmTwistNote(float roll[2]);
int ShieldStrapped();
// [shield] x y z (cm) pitch yaw roll (deg) as the last strap was given them in the hook (what really reached it), and how far they moved (cm) and turned (deg) the shield from the plain strap.
void ShieldAdjNote(float seen[6], float* moveCm, float* turnDeg);
bool ShieldAttach(ShieldPose* out);
// The same point in his model space (as his bones are posed): his model matrix puts it in the world.
bool HeroPalmModel(int side, float palm[3]);
// The hero's arm as the hook drew it, in the WORLD (the game's render world, world units; directions unit), made at the very pose from the hero's own model-to-world
// (his TransformComponent's local matrix read then, as NotePosedWeapon does): the elbow (forearm bone), the wrist (hand bone), the way the palm faces from the forearm and the
// knuckles' line (`across`), and - with the knuckles found (`hand`) - the hand's own frame: `point` wrist -> middle knuckle, `face` out of the palm (what a belt bottle in the hand
// is held by). Exactly where the arm is drawn, whatever the IK or the [arms] offsets do; side 0 right, 1 left. False when the arms have not been posed for a quarter of a second,
// or the hero's transform was not readable.
// An arm whose world read failed at the last poses is still the last one read, with its age: `ageMs` is how old it is (0xFFFFFFFF: never), the call fails when it is older than `maxAgeMs`.
struct HeroArm { float elbow[3], wrist[3], across[3], point[3], face[3]; bool hand; };
bool HeroArmWorld(int side, HeroArm* out, unsigned maxAgeMs = 250, unsigned* ageMs = nullptr);
// Why the newest pose could not put the arm in the world ("" when it could): the entity tables not found, the transform not read ...
const char* HeroArmWhy();
#endif
// Called by the hook on every pose, before it is used: the camera's yaw and eye
// point in model space from the hero's model matrix as it is NOW. The 10 ms
// copy in Input lagged while the game turned the hero - the body turned part
// of the way and shook. Returns false to keep the Input's values. In: the
// same sign/offset settings vrcam applies (yawInModel = sign*yaw + offset).
// It also hands over the controllers from the same head sample the camera is
// drawn with (the 10 ms copy came from another moment: arms shook on head turns).
using FreshFn = bool (*)(float* yawInModel, float* eyeModel, bool* eyeOk);
using FreshHandsFn = bool (*)(Input* in);   // fills handsValid, hand, rot, userHeadM
void SetFreshHands(FreshHandsFn fn);
void SetFresh(FreshFn fn);
// [weight] grip_model 1: the grip library's hand step (vrcam registers griplib::HandStep) - where the hand sits on the wood and how its fingers close. The hook uses it only when the
// weight model says the library holds the weapon (weight::Out::gripLib). None registered: the hook has only its own fit.
}  // namespace skel
struct D2rgHandIn;
struct D2rgRig;
struct D2rgHandState;
struct D2rgHandOut;
namespace skel {
using GripHandStepFn = int (*)(const D2rgHandIn&, const D2rgRig&, D2rgHandState*, D2rgHandOut*);
using GripAbiFn = int (*)();   // the interface version the loaded library speaks (griplib::Abi), 0 when none; none registered: the newest
void SetGripHandStep(GripHandStepFn fn, GripAbiFn abi = nullptr);
// 0 nothing learned yet, 1 the bind pose is read and used, -1 it did not read as a pose (no lock)
int LockState();
// The hero's eyes standing (model space y, from his own rig - each class has
// its height) and how far a jump lifts him now (model units, 0 on the ground;
// rawLift: the hips' rise before the stride's bob is cut). False until a pose
// of the hero has been seen in the last second.
bool HeroEye(float* eyeY, float* lift, float* rawLift = nullptr);
// [body] crouch: whether the hero's legs were found to bend (1 both, 0 not - the body is not taken down; -1 not asked yet) and
// how far the body was last taken down (model units, 0 standing).
int CrouchLegs(float* applied);
#if D2RVR_FIRST_PERSON
// staff_free_left: the staff as last posed - its direction (from the left
// hand's place on it toward the right hand, as the game's grip has them) in
// the head's turn-only frame (x right, y up, z back), and how far apart the
// game's animation holds the two hands (model units). False when no staff has
// been held that way in the last 0.3 s. leftOn: the left hand is on it now.
bool StaffAxis(float dir[3], float* handGap = nullptr, bool* leftOn = nullptr);
// The bone axis the crossbow's stock was laid along last, +-1..3 (x y z); 0 = none yet.
int GunAxis();
// staffHandAuto: the wrist the last grip found the weapon on (0 right, 1 left), -1 the
// attach bones did not tell (staffHand used). scale: the two attach bones' sizes then.
int GripHand(float scale[2] = nullptr);
// [debug] the stretch of shaft or hilt the left hand takes a staff, spear or two-handed sword
// by, as last posed: its two ends, same frame as GunBone. False when none lately (a crossbow's
// line is vrcam's HandRay).
bool GrabLine(float a[3], float b[3]);
// [debug] xbow_barrel_line: the crossbow's barrel as drawn (from its bolt_helper bone: a = 40 cm behind the front of the stock,
// b = 30 cm ahead of it) and where [hands] xbow_support_model puts the left palm, same frame as GrabLine. False when none lately.
// why: what the barrel's reading said last (for the log).
// rendered: the barrel as the renderer draws the crossbow (else from the item's own transform, which is not how it is drawn - not used by the hand).
bool XbowBarrelDebug(float a[3], float b[3], float support[3], bool* rendered = nullptr);
const char* XbowBarrelWhy();
// [debug] xbow_barrel_line: the shot's ray as the aim uses it now, live - from the muzzle (the front of the stock on the drawn barrel) along
// the drawn barrel ([hands] xbow_aim_barrel and read) or the hand-set shot line, to where it meets the floor (floor true) or 8 m out; same
// frame as XbowBarrelDebug. barrel: the drawn barrel's direction is the one used. The game's own aim takes only its heading on the ground.
bool XbowShotRay(float a[3], float b[3], bool* barrel = nullptr, bool* floor = nullptr);
// [debug] xbow_barrel_line: the crossbow's left palm's middle (wrist to middle knuckle, halfway) as the pose leaves it, same frame; cm: how
// far from the place it was put on (0 = on it). False when none lately.
bool XbowPalmDebug(float p[3], float* cm = nullptr);
// The world-matrix function's result for an entity (vr/potion_hide.h SetXformObserver): the hero's crossbow as the renderer places it is
// learned against our attach bone (only that entity; cheap for the rest). Renderer's thread, read-only.
void XbowRendered(uint32_t entity, uintptr_t component, uintptr_t out);
// What the crossbow's barrel has to say since the last call (lines, newline-separated): the renderer's placing against the item's own
// transform once a model, and every 3 s with [debug] xbow_barrel_line the points in the head frame.
std::string XbowNoteTake();
// The bone of the weapon in hand as last posed (a crossbow: GunFrame), for vrcam's debug
// drawing: its origin from the eye in metres and its three axes, in the head's turn-only
// frame (x right, y up, z back - as the bridge sends the hands). False when no weapon lately.
bool GunBone(float origin[3], float axes[3][3]);
// [hands] spell_aim_model (vr/spell_aim_model.h): the hands and the weapon in hand as the finished pose draws them, same frame as GunBone (the head's
// turn-only frame, metres from the eyes; directions unit): each hand's palm middle (halfway from the wrist to the middle knuckle), its fingers (wrist ->
// middle knuckle) and the way out of its palm; the weapon's attach bone GunBone gives - its origin, its +Y (the tip side: vr/blade_shape.h) and the wrist
// it is on (0 right, 1 left). A part not posed in the last maxAgeMs is marked not there; false when none is.
struct CastPose {
    bool hand[2] = {};
    float palm[2][3] = {}, fingers[2][3] = {}, face[2][3] = {};
    bool weapon = false;
    int weaponSide = 0;
    float weaponOrigin[3] = {}, weaponTip[3] = {};
};
bool CastPoseNow(CastPose* out, unsigned maxAgeMs = 100);

// The weapon in hand exactly as the finished pose draws it (the final attach bone, after the hand has been put on the
// controller), in the game's WORLD units, for melee by touch. origin: the attach bone's place; axis: its +Y, the
// blade / shaft direction (-Y is the hilt), unit length; hero: the hero model's own origin in the same frame;
// poseStamp: the hand sample (Input::poseStamp) the pose was made from; worldEpoch and heroGeneration as they
// were at that pose. Only made while Input::posedWeapon is on; false when no pose of the weapon came in the
// last 100 ms, or the hero was rebuilt since.
// blade[side]: the weapon bone of that hand slot AS THE DEBUG'S RED LINE HAS IT - its origin and its three axes in the head's
// turn-only frame, metres from the eyes (the numbers GunBone() gives, but kept with the pose's stamp): the way to the blade that lies
// on the drawn weapon (vr/blade_shape.h). Filled for the slot `side` below and, with Input::dualBlades, for the other hand's too.
struct PosedBlade {
    bool ok = false;
    float origin[3]{}, axis[3][3]{};
    // [flail] chain on, a flail in this slot: the ball swinging on its chain - it is what strikes, not the handle. Its centre and the last
    // link's direction (toward the ball) in the same frame, and its radius in metres (vr/flail_chain.h HeadOf: measured on the model's mesh).
    bool ball = false;
    float ballCentre[3]{}, ballDir[3]{};
    float ballRadius = 0.0f;
};
struct PosedWeaponPose {
    float origin[3]{}, axis[3]{}, hero[3]{};
    uint32_t poseStamp = 0, worldEpoch = 0, heroGeneration = 0;
    uint32_t weaponType = 0;
    PosedBlade blade[2];
    int side = 0;   // the hero's hand slot the bone belongs to (0 right, 1 left)
};
bool PosedWeapon(PosedWeaponPose& out);
#endif
#if D2RVR_FIRST_PERSON
// The bow's own rig drawn by the pull ([bow] rig_pose, vr/bow_rig.h). The draw detector feeds this every
// tick (about 10 ms; a feed older than 150 ms counts as no draw); the string hand and the bow hand are the
// controllers the arms already follow, so the minimum is drawing (+ stringHand if the bow is not on the left wrist). While drawing - and 0.7 s
// after - the hero's bow rig is posed by us, never otherwise: no feed, no override.
struct BowDraw {
    bool drawing = false;       // the string is nocked and pulled
    float draw = 0.0f;          // 0..1, how far; used when the hands cannot place the string (a controller lost)
    bool arrowVisible = true;   // the arrow sits on the string
    int stringHand = -1;        // the hand that pulls the string, 0 right 1 left (as Input::hand); -1 = the one opposite the bow's wrist
    // The pull that just ended came to nothing - a cancel: with drawing false, the string goes back to rest over ~0.13 s
    // (bowrig::kCancelMs), eased, the arrow still on it, and then the game's own animation is left alone. Without it (false),
    // drawing going false is a shot's release: the string snaps to rest, the arrow is hidden and the rest pose is held for
    // 0.7 s, so the game's own wind-up, started by the shot, is not played a second time - except that a pull whose largest
    // `draw` was under 0.25 (bowrig::kShortPull, too short to shoot) is read as a cancel anyway. Hold it true from the cancel
    // on until the next pull starts (a feed older than 150 ms counts as none).
    bool cancelled = false;
    // The feeder may place the string itself: nock and the way the arrow points, in the frame of Input::hand
    // (metres from the head, x right, y up, z back; direction: the same axes, unit). Off = the controllers.
    bool anchored = false;
    float nock[3] = {};
    float dir[3] = {};
};
void SetBowDraw(const BowDraw& d);
// [bow] hand_pose: the string hand's own trigger and squeeze (0..1), as the controller has them - read by the draw's wire BEFORE the draw
// takes the control from the layout (a taken control reads as let go in Input::grip). `side` is the hand, 0 right 1 left (as Input::hand).
// A feed older than 150 ms counts as none, and then the hand follows Input::grip as it always did.
void SetBowHandControls(int side, float trigger, float squeeze);
// For vrcam's log: "posed" when the hero's bow was posed within the last second, else why not (a fixed
// string; it changes only while the string is drawn - nothing is looked at otherwise). It is the HERO's bow that this tells of: the
// other bow skeletons the game keeps (his other weapon set's, another's) have their own statuses in BowRigWho.
const char* BowRigStatus();
// Every bow skeleton posed lately and what the rig did with each: "the hero's bow: long_battle_bow.skeleton, 18 bones (skeleton .., entity ..):
// posed | other bow skeletons: light_crossbow.skeleton, 15 bones (.., another's, hangs off entity ..): not the hero's". For the log, on change.
std::string BowRigWho();
// [flail] chain: hit by touch tells a hit of the weapon in that hand slot (0 right, 1 left): the flail's head there is kicked back. Any thread.
void FlailHit(int side);
// For vrcam's log: what the last pose of the hero's flail did with its chain ("posed - ..." or why not; a fixed string), and one line for each
// flail model learned since the last call (its chains, or why it has none). Empty when none.
const char* FlailStatus();
std::string FlailTakeNote();
// And for the log: one line for every bow skeleton learned or refused since the last call - which model, which layout
// (bows are matched by what their bones are: vr/bow_layouts.h), or the bones of one that is not known. Empty when none.
std::string BowRigTakeNote();
// Also for the log: how big the game's own pose leaves the bow's arrow bone against its bind pose (lowest and highest
// seen) - the game shows and hides the arrow, which is part of the bow's mesh, by that size.
std::string BowRigDiag();
// [hands] bow_draw: where the posed bow's string rests (the bowString bone of the bow's rig at its bind pose, on the bow as
// it hangs in the hero's hand now), in the frame of Input::hand - metres from the head, x right, y up, z back, without the ini's
// hand offset: the frame D2RVR_Shared's hands are in. Read while Input::bowString is on; false when not read within the last
// 300 ms (no bow, another rig, the hands not placed).
bool BowString(float out[3]);
// [bow] arrow_fx: the bow's own arrow as it is drawn now, in the same frame as BowStave (metres from the head, x right, y up, z back, the head's turn, without
// the ini's hand offset): its nock and its head. Read off the arrow bone as the last pose of the hero's bow left it - the game's own pose while the string is not
// drawn (the arrow nocked at rest), ours while it is (the arrow on the drawn string, lifted off the arm, along the aim) - so it is the arrow as it is drawn, not a
// line between the controllers. The head: the farthest of the arrow's own bones along the arrow in the bow's bind pose; bones that do not reach past the riser give
// the riser (where the arrow passes the bow). *drawn: our pose (the string drawn). Read while Input::bowArrow is on; false when not read within the last 300 ms
// (no bow, a rig that is not known, the arrow hidden - after a shot).
bool BowArrow(float nock[3], float head[3], bool* drawn = nullptr);
// For vrcam's log, once read: how the head was found ("the arrow bones reach 61 cm ahead of the nock" / "... not past the riser: the head at the riser, 24 cm").
std::string BowArrowWho();
// [bow] quiver_orbs: the bow's stave as it hangs in the hero's hand now, in the frame of Input::hand (metres from the head, x right, y up, z back, the head's turn): the riser's
// middle, the end of the limb that POINTS UP (where the string is tied to it; a bow has no top of its own, its rig's "upper" limb may hang down), and the unit direction from the stave
// toward the archer (the way the nock lies). Read off the bow's own
// rig (vr/bow_layouts.h) at its rest pose, so it follows the bow wherever the hand takes it. Read while Input::bowStave is on; false when not read within the last 300 ms (no
// bow, a rig that is not known, the hands not placed).
bool BowStave(float grip[3], float tip[3], float back[3]);
// [bow] aim_along_hands (and [debug] bow_vectors): the bow's own forward axis as the hero's pose has it now - after the turn, if there
// is one - in the frame of Input::hand (x right, y up, z back, unit), the frame the debug vectors are in. False when not read within
// 300 ms (no bow, its axes not read, the feature and the debug both off).
bool BowAxis(float out[3]);
// How the aim stands, as of the last pose: `blend` how far the turn is in (0..1), `angleDeg` the angle between the bow's forward axis
// and the line from the string hand to the bow hand now, `rawDeg` the same before the turn (as the controller holds the bow), and
// `why` what stopped it if nothing turns it. False when [bow] aim_along_hands is off.
bool BowAimState(float* blend, float* angleDeg, float* rawDeg, const char** why);
// And which wrist the aim took the bow to be on and how it knew, with the attach bones' sizes ("the left wrist, told by the draw's feed;
// attach bones R 1.00 L 1.00"). Empty until a pose has been made for it.
std::string BowAimWho();
// [bow] arrow_rest_*: the point the arrow rests on, where the aim from the string hand passes the bow hand - fixed in the bow hand's own
// frame - in the frame of Input::hand (x right, y up, z back, metres from the head, without the ini's hand offset: what D2RVR_Shared's
// hands are in). False when not made within the last 300 ms (the offsets are 0, no bow, the aim not made). For the draw's aim ([bow]
// arrow_rest_aims: the shot goes along the line through it, as the arrow lies) and the debug dot.
bool BowRestPoint(float out[3]);
// [bow] string_anchor: where the bow's string is held while it is drawn (the fingers' first joints, eased in with the draw) in the same frame as
// BowRestPoint. False when it is not held there (off, not drawn, not the Amazon's right hand, nothing made in the last 300 ms). For the draw's aim
// line under arrow_rest_aims: it starts where the arrow does, so the shot goes where the arrow points.
bool StringAnchorHands(float out[3]);
// And what the anchor is made of, once it has been (empty before): "the first joints of the index, middle and ring fingers (bones R_ind_finger_03_bind_jnt, ...), as the game animates them
// (hand_pose off)". For vrcam's log, on change.
std::string StringAnchorWho();
// For vrcam's log: what the string hand's pose did last ([bow] hand_pose) - "posed - the right hand, told by the draw's feed", or
// why it did not. Changes only when that does.
std::string BowHandPoseDiag();
// For vrcam's log: a held bow whose skeleton is not the hunter's 13-bone rig (its path, bone count and bone names), empty until one
// is met. Read only while Input::bowString is on.
std::string BowProbe();
#endif
// Where the game's own pose points the hero, in model space (deg, atan2 of each bone's z row):
// root, anim, pelvis - for the facing diagnostics in vrcam's log.
void PoseYaws(float out[3]);
bool Dump(const wchar_t* outPath);
void SetHeroPos(float x, float y, float z);

// The hero's TransformComponent in the renderer's ECS (skeletons.cpp, docs/doll_binding.md):
// from the SkeletonInstance the hook identifies as the hero, through its entity, with no memory
// search. comp + 0x00 = local (the model-to-world set for this frame before the poses), + 0x40 =
// world (worked out after the poses), + 0xC0 = parent entity (low 20 bits 0xFFFFF = none).
struct HeroXform { uintptr_t comp = 0; uintptr_t self = 0; uint32_t entity = 0; uint32_t parent = 0xFFFFFFFFu; };
void SetGameBase(uintptr_t base);
// On: the hook notes the hero even while it changes nothing in his pose (needed to find his facing).
void TrackHero(bool on);
// Resolved now (cheap: a few reads; a new SkeletonInstance costs one pass over the storage).
// False with *why set when the chain does not read.
bool HeroTransform(HeroXform* out, const char** why = nullptr);
// Any entity's TransformComponent, the same layout (engine items, vrcam's engitem). The storage's
// array moves when it grows: use the pointer at once, on the registry's own thread.
bool EntityTransform(uint32_t entity, uintptr_t* comp);
// Bumped whenever the hero's SkeletonInstance changes: a weapon swap rebuilds his model.
uint32_t HeroGen();
void** OrigSlot();
void* Detour();

}  // namespace skel
