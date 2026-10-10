// The one block of shared memory between the BodyWalk side (D2R Bridge
// plugin, writer) and the game side (vrcam plugin inside D2R, reader).
//
// The head's turn, and the hands relative to the head. Walking, sticks and
// buttons do not cross here: they reach the game as BodyWalk's ordinary virtual
// Xbox pad, and the game plugin turns the left stick by its own camera yaw
// where the game reads the pad.
#pragma once

#include <cstdint>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#define D2RVR_SAMPLE_STAMP_MAGIC 0x48444148u  // "HADH"

// The same clock as FlatVRAfrStampNow (afr_eye_shared.h): QPC in 0.1 ms.
inline uint32_t D2RVRStampNow() {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (uint32_t)((unsigned long long)c.QuadPart / ((unsigned long long)f.QuadPart / 10000ull));
}

#define D2RVR_SHARED_NAME L"Local\\BodyWalkVR_D2R"
#define D2RVR_SHARED_VERSION 4u

#pragma pack(push, 1)
struct D2RVR_Shared {
    uint32_t version;        // D2RVR_SHARED_VERSION
    uint32_t counter;        // bumped on every write; a frozen counter = BodyWalk stopped
    uint32_t headValid;      // 1 while the headset is tracking
    float    headYawDeg;     // head turn since BodyWalk's last recenter, positive = left
    float    headHeightM;    // head height in the room (metres)
    float    headPitchDeg;   // up (+) / down (-) from the horizon
    uint32_t pitchValid;     // 0 when BodyWalk is older than BW_TrackingData version 2
    float    headRollDeg;    // tilted toward the right shoulder (+) / left (-)
    uint32_t rollValid;      // 0 when BodyWalk is older than BW_TrackingData version 3
    // v4: hands from the head, in metres, in the frame of the head's turn only
    // (x right, y up, z back - OpenXR axes, no head pitch or roll in it).
    // handsValid bit 0 = right, bit 1 = left; 0 when BodyWalk is older than
    // BW_TrackingData version 4 (no room yaw to turn them by).
    uint32_t handsValid;
    float    rightHand[3];
    float    leftHand[3];
    float    rightRot[4];    // controller orientation in the same frame, x y z w
    float    leftRot[4];
    // When the head above was sampled, in the clock of afr_eye_shared.h's
    // FlatVRAfrStampNow (QPC in 0.1 ms, low 32 bits); valid while
    // sampleStampMagic says so. The game side stamps its views with it, and
    // FlatVR places the screen at the head pose of that moment.
    uint32_t sampleStamp;
    uint32_t sampleStampMagic;   // D2RVR_SAMPLE_STAMP_MAGIC
    // The user's height as set in BodyWalk (Avatar > Body Height), millimetres;
    // 0 from a bridge older than 0.8 (this word was reserved, always 0).
    uint32_t userHeightMm;
    // The controllers' grip triggers, 0..1, valid while gripMagic says so
    // (bridge 0.13+). Appended without a version bump, like the stamp above:
    // an older vrcam stops reading at 108 bytes, and a newer one reading an
    // older bridge's block finds zeros here (the view is a whole page) - no magic.
    uint32_t gripMagic;      // D2RVR_GRIP_MAGIC
    float    rightGrip;
    float    leftGrip;
    // Where the head is in the room (bridge 0.15+, valid while roomMagic says
    // so; appended like the grips): BodyWalk's room, OpenXR stage axes - x
    // right, y up, z back, metres - and its yaw in that room, not recentered
    // (positive = turned left). For the table view (F5): the player walks
    // round the game laid out on a real table.
    uint32_t roomMagic;      // D2RVR_ROOM_MAGIC
    float    headRoom[3];
    float    headYawRoomDeg;
};
#pragma pack(pop)

#define D2RVR_GRIP_MAGIC 0x50495247u   // "GRIP"
#define D2RVR_ROOM_MAGIC 0x4D4F4F52u   // "ROOM"

static_assert(sizeof(D2RVR_Shared) == 140, "D2RVR_Shared is a wire format");

// BodyWalk actions the bridge registers ("D2R: Attack", "D2R: Pick up /
// interact" ...), held or not, for vrcam to act on inside the game. Written by
// the bridge (BW_Plugin_ReceiveAction), read by vrcam. Its own mapping, so an
// older bridge or vrcam simply misses it.
#define D2RVR_ACTIONS_NAME L"Local\\BodyWalkVR_D2R_Actions"
#define D2RVR_ACTIONS_VERSION 1u

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

#pragma pack(push, 4)
struct D2RVR_Actions {
    uint32_t version;   // D2RVR_ACTIONS_VERSION
    uint32_t counter;   // bumped on every change
    uint32_t held;      // D2RVRAction bits held now
    uint32_t reserved;
};
#pragma pack(pop)

static_assert(sizeof(D2RVR_Actions) == 16, "D2RVR_Actions is a wire format");

// The game's own key commands (docs/buttons_recon.md): every control of its
// Controls menu, one BodyWalk action each ("D2R key: ..."), held or not. vrcam
// presses the command where the game's key would (its command executor, on
// the UI thread, vr/gamecmd.cpp), so no pad and no key is needed. Written by
// the bridge, read by vrcam. Bit n of held = the game's command n (the index
// in its command table 0x22A7930), so the list below may grow in any order.
#define D2RVR_COMMANDS_NAME L"Local\\BodyWalkVR_D2R_Commands"
#define D2RVR_COMMANDS_VERSION 1u
#define D2RVR_GAME_COMMANDS 0x45u   // the game's table: 0x00..0x44

#pragma pack(push, 4)
struct D2RVR_Commands {
    uint32_t version;   // D2RVR_COMMANDS_VERSION
    uint32_t counter;   // bumped on every change
    uint64_t held[2];   // bit n (held[n / 64], bit n % 64): the game's command n held now
};
#pragma pack(pop)

static_assert(sizeof(D2RVR_Commands) == 24, "D2RVR_Commands is a wire format");

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

// D2R VR Settings' Start FlatVR / Stop FlatVR: two named auto-reset events the
// bridge makes and looks at in BW_Plugin_Update; set one and the bridge asks
// BodyWalk (host API 7, request_flatvr_running) to start or stop FlatVR, as
// the FlatVR tab's own buttons do. No event = no BodyWalk with the bridge.
#define D2RVR_FLATVR_START_NAME L"Local\\BodyWalkVR_D2R_FlatVRStart"
#define D2RVR_FLATVR_STOP_NAME L"Local\\BodyWalkVR_D2R_FlatVRStop"
// The same for FlatVR's 3D source (host API 8, request_flatvr_stereo_source): a
// flat screen, the game's depth through ReShade, or the stereo pair the game
// draws - D2R VR Settings' "3D in the headset".
#define D2RVR_FLATVR_3D_NONE_NAME L"Local\\BodyWalkVR_D2R_FlatVR3DNone"
#define D2RVR_FLATVR_3D_DEPTH_NAME L"Local\\BodyWalkVR_D2R_FlatVR3DDepth"
#define D2RVR_FLATVR_3D_PAIR_NAME L"Local\\BodyWalkVR_D2R_FlatVR3DPair"
// BodyWalk's Mapping Input Source with native OpenXR (host API 9,
// request_gesture_source): the game's own headset ("D2R VR", through this bridge)
// while the game holds it, FlatVR again after (only if it was the game's). Set by
// D2R VR Settings when "Native OpenXR" is picked or left, and by the game when
// its session starts and ends.
#define D2RVR_GESTURES_GAME_NAME L"Local\\BodyWalkVR_D2R_GesturesGame"
#define D2RVR_GESTURES_FLATVR_NAME L"Local\\BodyWalkVR_D2R_GesturesFlatVR"
// There while the bridge runs in a BodyWalk that takes the game's own headset (host
// API 9: send_xr_frame, request_gesture_source) - D2R VR Settings offers native OpenXR
// only then (or with a BodyWalk of 1.78 on): with an older one the game would show its
// picture but get no buttons and no gestures.
#define D2RVR_BRIDGE_XR_SOURCE_NAME L"Local\\BodyWalkVR_D2R_XrSource"

// Native OpenXR (vrcam's vr/xr.cpp): the game holds the headset itself and reads
// the head and the controllers from its own session; the bridge hands them to
// BodyWalk as its tracking source instead of FlatVR, so gestures, the Mapping and
// the virtual pad go on as with FlatVR. Written by vrcam once a headset frame
// (at the moment the frame will be shown), read by the bridge. Poses as the
// runtime has them: metres in its STAGE space (the floor at 0; LOCAL without one) - x right, y up, z back -
// rotations x y z w, not recentered.
#define D2RVR_XR_INPUT_NAME L"Local\\BodyWalkVR_D2R_XrInput"
#define D2RVR_XR_INPUT_VERSION 1u

enum D2RVRXrButton : uint32_t {
    D2RVR_XRB_PRIMARY = 1u << 0,     // A on the right controller, X on the left
    D2RVR_XRB_SECONDARY = 1u << 1,   // B / Y
    D2RVR_XRB_STICK = 1u << 2,       // the thumbstick pressed in
    D2RVR_XRB_MENU = 1u << 3,        // the menu button (left), the system one is the runtime's
    D2RVR_XRB_TRIGGER = 1u << 4,     // the trigger pulled past its click (or past 0.8 without one)
    D2RVR_XRB_SQUEEZE = 1u << 5,     // the grip squeezed past its click (or past 0.8)
};

#pragma pack(push, 4)
struct D2RVR_XrPose {
    float pos[3];
    float rot[4];
    uint32_t valid;   // 1 position and orientation tracked, 2 orientation only, 0 none
};
struct D2RVR_XrHand {
    D2RVR_XrPose grip;   // the controller as held (the grip pose)
    D2RVR_XrPose aim;    // where it points (the aim pose)
    float trigger, squeeze, stickX, stickY;   // 0..1, 0..1, -1..1 (+ right, + up)
    uint32_t buttons;    // D2RVRXrButton bits
    uint32_t active;     // 1 while any of its actions is bound (a controller is there)
};
struct D2RVR_XrInput {
    uint32_t version;     // D2RVR_XR_INPUT_VERSION
    uint32_t counter;     // bumped on every write; frozen = no session (or the game paused)
    uint32_t stamp;       // D2RVRStampNow() when written
    uint32_t focused;     // 1 while the session has the input focus
    int64_t displayTime;  // the XrTime the poses are for
    D2RVR_XrPose head;    // between the eyes
    D2RVR_XrHand hand[2]; // 0 left, 1 right
    char profile[2][64];  // the interaction profile each hand runs, e.g. /interaction_profiles/oculus/touch_controller
};
#pragma pack(pop)
static_assert(sizeof(D2RVR_XrInput) == 360, "D2RVR_XrInput is a wire format");

// Native OpenXR: BodyWalk's gesture zones ticked "VR" in its Mapping tab (the belt's
// potions, a holster), placed by BodyWalk from the head in D2RVR_XrInput - so in the
// same STAGE space - and drawn by vrcam as see-through balls, as FlatVR draws them.
// Written by the bridge (BW_Plugin_ReceiveVrZones, host API 10: BodyWalk 1.78) every
// BodyWalk GUI frame while there are zones to show; vrcam lets them lapse when the
// stamp is older than half a second (BodyWalk closed, the zones unticked).
#define D2RVR_XR_ZONES_NAME L"Local\\BodyWalkVR_D2R_XrZones"
#define D2RVR_XR_ZONES_VERSION 1u
#define D2RVR_XR_ZONES_MAX 32u

#pragma pack(push, 4)
struct D2RVR_XrZone {
    float centre[3];   // metres, STAGE space
    float radius;      // metres
    uint32_t rgba;     // the colour for its state now, R in the low byte, alpha = opacity
    float glow;        // halo and rim, 0..1
};
struct D2RVR_XrZones {
    uint32_t version;  // D2RVR_XR_ZONES_VERSION
    uint32_t counter;  // bumped on every write
    uint32_t stamp;    // D2RVRStampNow() when written
    uint32_t count;    // used entries in zone[]
    D2RVR_XrZone zone[D2RVR_XR_ZONES_MAX];
};
#pragma pack(pop)
static_assert(sizeof(D2RVR_XrZones) == 16 + 32 * 24, "D2RVR_XrZones is a wire format");
