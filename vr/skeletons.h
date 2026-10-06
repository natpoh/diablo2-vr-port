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
    bool wrist = false;         // mode 2: the hands turn with the controllers, not only follow the forearm
    float wristDeg[3] = {};     // pitch, yaw, roll on top, in the controller's own axes (mirrored for the left)
    float scale = 1.0f;         // arm reach on top of the hero/user height ratio
    float userHeadM = 1.7f;     // the user's head above the floor
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
#if D2RVR_FIRST_PERSON
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
    // [fist] <class>_close / <class>_open, % / 100: how far a free hand closes with the grip
    // squeezed and with it let go, per hero class (kHeroClasses) - the rigs differ: the full
    // fist rolled the paladin's fingers into his palm (2026-10-06).
    float fistClose[8] = {1.0f, 1.0f, 1.0f, 0.65f, 1.0f, 1.0f, 1.0f, 1.0f};
    float fistOpen[8] = {0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f};
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
// 0 nothing learned yet, 1 the bind pose is read and used, -1 it did not read as a pose (no lock)
int LockState();
// The hero's eyes standing (model space y, from his own rig - each class has
// its height) and how far a jump lifts him now (model units, 0 on the ground;
// rawLift: the hips' rise before the stride's bob is cut). False until a pose
// of the hero has been seen in the last second.
bool HeroEye(float* eyeY, float* lift, float* rawLift = nullptr);
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
// The bone of the weapon in hand as last posed (a crossbow: GunFrame), for vrcam's debug
// drawing: its origin from the eye in metres and its three axes, in the head's turn-only
// frame (x right, y up, z back - as the bridge sends the hands). False when no weapon lately.
bool GunBone(float origin[3], float axes[3][3]);
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
// Bumped whenever the hero's SkeletonInstance changes: a weapon swap rebuilds his model.
uint32_t HeroGen();
void** OrigSlot();
void* Detour();

}  // namespace skel
