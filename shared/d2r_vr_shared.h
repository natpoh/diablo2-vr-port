// D2R VR and BodyWalk, through BodyWalk Game Link (bodywalk/game_link.h): what
// the mod tells BodyWalk about the game - its actions, its Mapping tabs - and
// vrcam's own view of what comes back (vr/link.cpp).
//
// The head's turn, and the hands relative to the head. Walking, sticks and
// buttons do not cross here: they reach the game as BodyWalk's ordinary virtual
// Xbox pad, and the game plugin turns the left stick by its own camera yaw
// where the game reads the pad.
#pragma once

#include <cstdint>
#include <cstdio>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bodywalk/game_link.h>
#include "d2r_vr_build.h"
#include "d2r_vr_state.h"   // the weapon kinds: a Mapping tab each

// FlatVR's clock (afr_eye_shared.h's FlatVRAfrStampNow): QPC in 0.1 ms.
inline uint32_t D2RVRStampNow() { return BW_LinkStampNow(); }

// Who we are in Game Link's Manifest.
#define D2RVR_GAME_ID "d2r"
#define D2RVR_GAME_NAME "Diablo II: Resurrected"
#define D2RVR_MOD_NAME "D2R VR"

#define D2RVR_SAMPLE_STAMP_MAGIC 0x48444148u  // "HADH"
#define D2RVR_SHARED_VERSION 4u

// The head and the hands as vrcam uses them: made from Game Link's Tracking
// (vr/link.cpp, link::Head) or, in native OpenXR, from the game's own session
// (XrIntoShared). The layout of the D2R Bridge's old block, which every reader in
// vrcam was written against.
struct D2RVR_Shared {
    uint32_t version;        // D2RVR_SHARED_VERSION
    uint32_t counter;        // moves with every new head; frozen = BodyWalk stopped
    uint32_t headValid;      // 1 while the headset is tracking
    float    headYawDeg;     // head turn since BodyWalk's last recenter, positive = left
    float    headHeightM;    // head height in the room (metres)
    float    headPitchDeg;   // up (+) / down (-) from the horizon
    uint32_t pitchValid;     // 0 when BodyWalk sends no pitch
    float    headRollDeg;    // tilted toward the right shoulder (+) / left (-)
    uint32_t rollValid;      // 0 when BodyWalk sends no roll
    // Hands from the head, in metres, in the frame of the head's turn only
    // (x right, y up, z back - OpenXR axes, no head pitch or roll in it).
    // handsValid bit 0 = right, bit 1 = left; 0 when BodyWalk sends no room yaw
    // to turn them by.
    uint32_t handsValid;
    float    rightHand[3];
    float    leftHand[3];
    float    rightRot[4];    // controller orientation in the same frame, x y z w
    float    leftRot[4];
    // When the head above was sampled, in the clock of D2RVRStampNow; valid while
    // sampleStampMagic says so. The game side stamps its views with it, and
    // FlatVR places the screen at the head pose of that moment.
    uint32_t sampleStamp;
    uint32_t sampleStampMagic;   // D2RVR_SAMPLE_STAMP_MAGIC
    // The user's height as set in BodyWalk (Avatar > Body Height), millimetres; 0 not known.
    uint32_t userHeightMm;
    // The controllers' grip triggers, 0..1, valid while gripMagic says so.
    uint32_t gripMagic;      // D2RVR_GRIP_MAGIC
    float    rightGrip;
    float    leftGrip;
    // Where the head is in the room, valid while roomMagic says so: BodyWalk's
    // room, OpenXR stage axes - x right, y up, z back, metres - and its yaw in
    // that room, not recentered (positive = turned left). For the table view
    // (F5): the player walks round the game laid out on a real table.
    uint32_t roomMagic;      // D2RVR_ROOM_MAGIC
    float    headRoom[3];
    float    headYawRoomDeg;
    // 1: this head is FlatVR's own, handed out at one of its frames (Game Link's BW_LINK_TRACK_FLATVR_STAMP) - sampleStamp
    // names it and FlatVR's frame clock moves with it ([flatvr] frame_sync starts a pair on it).
    uint32_t flatVrStamped;
};

#define D2RVR_GRIP_MAGIC 0x50495247u   // "GRIP"
#define D2RVR_ROOM_MAGIC 0x4D4F4F52u   // "ROOM"

// BodyWalk actions for vrcam to act on inside the game ("D2R: Attack", "D2R:
// Pick up / interact" ...): mapped like any button in BodyWalk's Mapping tab.
// vrcam's own bits; in Game Link they are the Manifest's first actions, in
// kD2RVRActions' order.
enum D2RVRAction : uint32_t {
    D2RVR_ACT_ATTACK = 1u << 0,     // the default attack (pad A) that never picks up or opens anything
    D2RVR_ACT_INTERACT = 1u << 1,   // pick up / open / talk: what pad A does on a target, and nothing else
    D2RVR_ACT_INVENTORY = 1u << 2,  // the pad's View (Back) button: opens and closes the inventory
    D2RVR_ACT_MAP = 1u << 3,        // show / hide the map: the FlatVR orb (and, once found, the game's map flag), never a key press
    D2RVR_ACT_SWAP = 1u << 4,       // swap weapon sets: the pad's right stick click (R3), the game's own swap on a controller
    // The views (the Home page's F1..F5), so a button or gesture in BodyWalk's
    // mapping picks one; acted on at the press. Numbered as the VR views.
    D2RVR_ACT_VIEW_1 = 1u << 5,     // from above
    D2RVR_ACT_VIEW_2 = 1u << 6,     // third person
    D2RVR_ACT_VIEW_3 = 1u << 7,     // first person, mouse and keyboard, the game animates the hero
    D2RVR_ACT_VIEW_4 = 1u << 8,     // first person, the body follows yours
    D2RVR_ACT_VIEW_5 = 1u << 9,     // the table (mixed reality)
    D2RVR_ACT_VIEW_NEXT = 1u << 10, // the next view round, as F12
    D2RVR_ACT_RECENTER = 1u << 11,  // F11: straight ahead is where I look; the game on the floor put down in front of me
};

// The Manifest's actions, in this order: BodyWalk's Mapping (and the mod's own
// profile, installer/bodywalk_profile.json) refers to an action by its place in
// BodyWalk's list. Add new ones at the end of kD2RVRCommands, never in between.
struct D2RVRActionName { const char* name; uint32_t bit; };
inline constexpr D2RVRActionName kD2RVRActions[] = {
    {"D2R: Attack (never picks up)", D2RVR_ACT_ATTACK},
    {"D2R: Pick up / interact", D2RVR_ACT_INTERACT},
    {"D2R: Inventory", D2RVR_ACT_INVENTORY},
    {"D2R: Map (show / hide)", D2RVR_ACT_MAP},
    {"D2R: Swap weapons", D2RVR_ACT_SWAP},
    {"D2R: View - from above", D2RVR_ACT_VIEW_1},
    {"D2R: View - third person", D2RVR_ACT_VIEW_2},
#if D2RVR_FIRST_PERSON
    {"D2R: View - first person (body)", D2RVR_ACT_VIEW_4},
#endif
    {"D2R: View - table", D2RVR_ACT_VIEW_5},
    {"D2R: View - next", D2RVR_ACT_VIEW_NEXT},
    {"D2R: Recenter / put the game in front of me (F11)", D2RVR_ACT_RECENTER},
};
inline constexpr uint32_t kD2RVRActionCount = sizeof kD2RVRActions / sizeof kD2RVRActions[0];

// The game's own key commands (docs/buttons_recon.md): every control of its
// Controls menu, one BodyWalk action each ("D2R key: ..."), after
// kD2RVRActions in the Manifest. vrcam presses the command where the game's key
// would (its command executor, on the UI thread, vr/gamecmd.cpp), so no pad and
// no key is needed. cmd = the index in the game's command table 0x22A7930.
#define D2RVR_GAME_COMMANDS 0x45u   // the game's table: 0x00..0x44

// Action name -> the game's command. All of them with a name in the game
// (0x38 has none); Move up/right/down/left are the game's own, north on its
// screen - vrcam's camera-relative W A S D is another thing.
struct D2RVRCommand { const char* action; uint8_t cmd; };
inline constexpr D2RVRCommand kD2RVRCommands[] = {
    {"D2R key: Character screen", 0x00},
    {"D2R key: Inventory", 0x01},
    {"D2R key: Party screen", 0x02},
    {"D2R key: Message log", 0x03},
    {"D2R key: Quest log", 0x04},
    {"D2R key: Chat", 0x05},
    {"D2R key: Help", 0x06},
    {"D2R key: Automap", 0x07},
    {"D2R key: Automap - centre", 0x08},
    {"D2R key: Automap - fade", 0x09},
    {"D2R key: Automap - party", 0x0A},
    {"D2R key: Automap - names", 0x0B},
    {"D2R key: Skill tree", 0x0C},
    {"D2R key: Choose skill", 0x0D},
    {"D2R key: Skill 1 (F1)", 0x0E},
    {"D2R key: Skill 2 (F2)", 0x0F},
    {"D2R key: Skill 3 (F3)", 0x10},
    {"D2R key: Skill 4 (F4)", 0x11},
    {"D2R key: Skill 5 (F5)", 0x12},
    {"D2R key: Skill 6 (F6)", 0x13},
    {"D2R key: Skill 7 (F7)", 0x14},
    {"D2R key: Skill 8 (F8)", 0x15},
    {"D2R key: Show belt", 0x16},
    {"D2R key: Potion - belt 1", 0x17},
    {"D2R key: Potion - belt 2", 0x18},
    {"D2R key: Potion - belt 3", 0x19},
    {"D2R key: Potion - belt 4", 0x1A},
    {"D2R key: Say 0", 0x1B},
    {"D2R key: Say 1", 0x1C},
    {"D2R key: Say 2", 0x1D},
    {"D2R key: Say 3", 0x1E},
    {"D2R key: Say 4", 0x1F},
    {"D2R key: Say 5", 0x20},
    {"D2R key: Say 6", 0x21},
    {"D2R key: Run (hold)", 0x22},
    {"D2R key: Run / walk switch", 0x23},
    {"D2R key: Stand still (Shift)", 0x24},
    {"D2R key: Show items (Alt)", 0x25},
    {"D2R key: Close all panels", 0x26},
    {"D2R key: Next skill", 0x27},
    {"D2R key: Previous skill", 0x28},
    {"D2R key: Clear messages", 0x29},
    {"D2R key: Screenshot", 0x2A},
    {"D2R key: Party portraits", 0x2B},
    {"D2R key: Swap weapons", 0x2C},
    {"D2R key: Minimap", 0x2D},
    {"D2R key: Skill 9 (F9)", 0x2E},
    {"D2R key: Skill 10 (F10)", 0x2F},
    {"D2R key: Skill 11 (F11)", 0x30},
    {"D2R key: Skill 12 (F12)", 0x31},
    {"D2R key: Skill 13 (F13)", 0x32},
    {"D2R key: Skill 14 (F14)", 0x33},
    {"D2R key: Skill 15 (F15)", 0x34},
    {"D2R key: Skill 16 (F16)", 0x35},
    {"D2R key: Mercenary", 0x36},
    {"D2R key: Say 7", 0x37},
    {"D2R key: Zoom", 0x39},
    {"D2R key: Legacy graphics", 0x3A},
    {"D2R key: Force move", 0x3B},
    {"D2R key: Horadric Cube", 0x3C},
    {"D2R key: Stash - previous tab", 0x3D},
    {"D2R key: Stash - next tab", 0x3E},
    {"D2R key: Move up (screen)", 0x3F},
    {"D2R key: Move right (screen)", 0x40},
    {"D2R key: Move down (screen)", 0x41},
    {"D2R key: Move left (screen)", 0x42},
    {"D2R key: Show items, unfiltered", 0x43},
    {"D2R key: Loot filter", 0x44},
};
inline constexpr uint32_t kD2RVRCommandCount = sizeof kD2RVRCommands / sizeof kD2RVRCommands[0];
static_assert(kD2RVRActionCount + kD2RVRCommandCount <= BW_LINK_MAX_ACTIONS, "the Manifest holds them all");

// The Manifest's Mapping tabs, in order: tab(name, description) for each. One per
// weapon kind ("D2R Bow", "D2R Sword" ...), whichever weapon set it is in, or the
// menu's, each on top of "main" - vr/link.cpp's PublishState picks them. Sent by
// vrcam, and written by D2R VR Settings for the installer (--game-link-manifest).
template <class F> void D2RVRTabs(F&& tab) {
#if D2RVR_FIRST_PERSON
    for (uint32_t k = D2RVR_TYPE_UNARMED; k < D2RVR_TYPE_COUNT; ++k) {
        char name[48], desc[120];
        snprintf(name, sizeof name, "D2R %s", kD2RVRWeaponTypeNames[k]);
        snprintf(desc, sizeof desc, "D2R: %s in the hands (F4)", kD2RVRWeaponTypeNames[k]);
        tab(name, desc);
    }
#endif
    tab("D2R Menu", "D2R: a trade, inventory or other panel is open");
    tab("D2R Third Person", "D2R: the third-person view (F2)");
    tab("D2R Top Down", "D2R: the view from above (F1)");
    tab("D2R Floor", "D2R: the game on the floor (F3)");   // VR F3: its own buttons, nothing of first person
#if D2RVR_FIRST_PERSON
    tab("D2R First Person", "D2R: first person (F4), no panel open, any weapon");
    // A weapon in the left hand too (a barbarian's pair of blades, an assassin's two claws), on top
    // of the right one's tab: actions for the second weapon (2026-10-06).
    tab("D2R Left Hand", "D2R: first person (F4), a weapon in the left hand too");
#endif
}

// Native OpenXR (vrcam's vr/xr.cpp): the game holds the headset itself and reads
// the head and the controllers from its own session; vrcam hands them to
// BodyWalk through Game Link's XrInput, so gestures, the Mapping and the virtual
// pad go on as with FlatVR. vrcam's own copy, once a headset frame (at the
// moment the frame will be shown): poses as the runtime has them, metres in its
// STAGE space (the floor at 0; LOCAL without one) - x right, y up, z back -
// rotations x y z w, not recentered. Buttons are BW_LINK_XRB_* bits.
using D2RVR_XrPose = BW_LinkXrPose;
using D2RVR_XrHand = BW_LinkXrHand;
#define D2RVR_XR_INPUT_VERSION 1u
struct D2RVR_XrInput {
    uint32_t version;     // D2RVR_XR_INPUT_VERSION
    uint32_t counter;     // moves with every frame
    uint32_t stamp;       // D2RVRStampNow() when written
    uint32_t focused;     // 1 while the session has the input focus
    int64_t displayTime;  // the XrTime the poses are for
    D2RVR_XrPose head;    // between the eyes
    D2RVR_XrHand hand[2]; // 0 left, 1 right
    char profile[2][64];  // the interaction profile each hand runs, e.g. /interaction_profiles/oculus/touch_controller
};
