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

// D2R VR Settings' Start FlatVR / Stop FlatVR: two named auto-reset events the
// bridge makes and looks at in BW_Plugin_Update; set one and the bridge asks
// BodyWalk (host API 7, request_flatvr_running) to start or stop FlatVR, as
// the FlatVR tab's own buttons do. No event = no BodyWalk with the bridge.
#define D2RVR_FLATVR_START_NAME L"Local\\BodyWalkVR_D2R_FlatVRStart"
#define D2RVR_FLATVR_STOP_NAME L"Local\\BodyWalkVR_D2R_FlatVRStop"
