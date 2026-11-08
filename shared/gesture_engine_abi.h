// gesture_engine_abi.h - the C interface of BodyWalk's gesture engine as a library in the game's process: d2r_vr_gestures.dll (closed, binary
// only). Plan: gesture-engine-dll-plan.md (2026-11-07). BodyWalk's sdk/include/bodywalk/gesture_engine.h is meant to be this file byte for byte;
// d2r_vr keeps this copy and only finds, checks and calls the library (vr/gesture_engine_lib.cpp) - nothing of what is inside it is here.
//
// Rules of the interface:
//   - C, __cdecl, plain structs with a leading `size` where they may grow; strings UTF-8.
//   - Every call is made from ONE thread (the game's headset frame); the library has no thread of its own and never throws out of an export.
//   - Results are BW_LINK_GR_* (shared/skill_link_abi.h): 0 OK, 1 no such gesture, 2 bad, 3 busy, 4 no set (no class given yet).
//   - BWGE_List fills the same BW_LinkGestures the Game Link block carried (the list with its pictures, the take's state, the last stroke):
//     recordState READY / DRAWING while a take runs, the take's end (SAVED / FAILED / CANCELLED) after it until the next BWGE_Record;
//     recordGesture the take's gesture (0 once a failed new gesture's row is gone). cmdDone / cmdResult are not the library's (the caller's).
//   - A take is armed by BWGE_Record and starts only at a FRESH press of the pen (a trigger already held when it is armed does not start it).
#pragma once

#include <stdint.h>

#include "skill_link_abi.h"   // BW_LinkGestures, BW_LINK_GT_* / GH_* / REC_* / GR_*, and (bodywalk/game_link.h) BW_LinkVrRune

#define BWGE_ABI 1u
#define BWGE_FILE L"d2r_vr_gestures.dll"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 8)
typedef struct BWGE_Pose {
    float pos[3];      // metres, the room's (OpenXR stage) space: x right, y up, z back
    float rot[4];      // x y z w
    uint32_t valid;    // 1 = this frame's
} BWGE_Pose;

typedef struct BWGE_Frame {
    uint32_t size;     // sizeof(BWGE_Frame)
    uint32_t flags;    // BWGE_FRAME_*
    double timeS;      // seconds, monotonic
    BWGE_Pose head, hand[2];   // [0] left, [1] right (the controllers' grip poses)
    float trigger[2], grip[2]; // 0..1
    uint8_t pen[2];    // the game says which trigger is a pen now (1): only these draw / take; 0 = the game's own control this frame
    uint8_t reserved[6];
    BWGE_Pose chest, pelvis;   // optional (valid 0): a body tracker's, when there is one
} BWGE_Frame;
enum { BWGE_FRAME_PLAY = 1u << 0 };   // a game area, no menu, F4: recognised strokes may cast (else only takes are recorded)

typedef struct BWGE_Skill {
    int32_t id;        // the game's skill id (as the Skills block's)
    char name[48];     // in the game's language
} BWGE_Skill;

typedef void (*BWGE_LogFn)(const char* line);
typedef struct BWGE_Init {
    uint32_t size;     // sizeof(BWGE_Init)
    uint32_t abi;      // BWGE_ABI
    const char* dataDir;   // BodyWalk's settings folder (%LOCALAPPDATA%\BodyWalkVR): the library keeps the game's gestures under games\<gameId>\ there
    const char* gameId;    // "d2r"
    BWGE_LogFn log;        // its log lines (may be null)
    float userHeightM;     // the player's height, metres; 0 = BodyWalk's own setting (config.json userHeight; else 1.7)
    float armLengthM;      // shoulder to hand, metres; 0 = BodyWalk's own setting (config.json userArmLength; else 0.6)
} BWGE_Init;

typedef struct BWGE_Cast {
    int32_t skill;     // the skill the recognised gesture casts
    uint32_t gesture;  // which gesture
    float percent;     // how well it matched
    uint32_t stamp;    // its own counter (each cast once)
} BWGE_Cast;
#pragma pack(pop)

typedef uint32_t(__cdecl* BWGE_AbiFn)(void);
typedef void*(__cdecl* BWGE_OpenFn)(const BWGE_Init* init);   // null (and a log line) when it cannot
typedef void(__cdecl* BWGE_CloseFn)(void* engine);
typedef void(__cdecl* BWGE_SetSetFn)(void* engine, const char* setKey, const char* setName, const BWGE_Skill* skills, uint32_t count);
typedef void(__cdecl* BWGE_FeedFn)(void* engine, const BWGE_Frame* frame);
typedef int(__cdecl* BWGE_RecordFn)(void* engine, uint32_t gesture, uint8_t type, uint8_t hand, int32_t skill);   // gesture 0: a new one of type / hand for skill
typedef int(__cdecl* BWGE_PlainFn)(void* engine);                              // Stop, Keep, Discard
typedef int(__cdecl* BWGE_GestureFn)(void* engine, uint32_t gesture);         // Clear
typedef int(__cdecl* BWGE_BindFn)(void* engine, uint32_t gesture, int32_t skill);
typedef int(__cdecl* BWGE_ColorFn)(void* engine, uint32_t gesture, uint32_t rgba);
typedef int(__cdecl* BWGE_ThresholdFn)(void* engine, uint32_t gesture, float percent);
typedef uint32_t(__cdecl* BWGE_ListFn)(void* engine, BW_LinkGestures* out);   // the list's revision
typedef int(__cdecl* BWGE_PollCastsFn)(void* engine, BWGE_Cast* out, int max);   // matches since the last call, each once
typedef uint32_t(__cdecl* BWGE_TrailFn)(void* engine, BW_LinkVrRune* out, uint32_t max);   // the pen's trail and the match's picture to draw

// A pose's zone (optional exports: a library without them has no zones to show - look them up by name, a missing one is not an error).
// A held pose matches when each of its hands is within radiusM of its recorded spot, turned alike (|quaternion dot| >= rotationMatch,
// the turn about the vertical taken out) and, where the take was a fist, closed. The spots are in the body's frame the engine measures
// in, given here in metres from the pelvis, x to the body's right, y up, z behind it (as BW_LinkGesture.joints):
//   chest  = head - 0.07*heightM * headForward - 0.04*heightM * headUp - 0.11*heightM * worldUp   (head's own forward / up, pitch included)
//   pelvis = chest - 0.42 m * worldUp
//   axes   = the head's forward laid level (its heading); the hands' offsets from the chest scale with armM.
#pragma pack(push, 8)
typedef struct BWGE_PoseZone {
    uint32_t size;          // sizeof(BWGE_PoseZone)
    float radiusM;          // each hand within this of its spot, metres
    float rotationMatch;    // and turned alike: |quaternion dot| at least this, 0..1
    float centre[2][3];     // the hands' spots ([0] left, [1] right), metres from the pelvis (x right, y up, z back)
    uint32_t handValid;     // bit 0 left, bit 1 right: the hands the pose holds
    float heightM, armM;    // the body the engine measures with
} BWGE_PoseZone;
#pragma pack(pop)
typedef int(__cdecl* BWGE_GetPoseZoneFn)(void* engine, uint32_t gesture, BWGE_PoseZone* out);   // BW_LINK_GR_BAD: not a pose
typedef int(__cdecl* BWGE_SetPoseZoneFn)(void* engine, uint32_t gesture, float radiusM);        // 0.03 .. 0.50 m

// A game action instead of a skill (optional exports): the game's own id ("d2r.town_portal", "pad.A", "key.0x54" ...), kept with the
// gesture and handed back when it fires - the game does it. Binding an action takes the gesture's skill away (skill -1) and BWGE_Bind
// takes the action away. BWGE_PollCasts2 drains the same queue as BWGE_PollCasts (use one of them): an action's cast has skill -1 and
// its action; BWGE_PollCasts hands out only the skills' casts.
#pragma pack(push, 8)
typedef struct BWGE_Cast2 {
    int32_t skill;     // the skill; -1 for an action
    uint32_t gesture;
    float percent;
    uint32_t stamp;
    char action[32];   // the action; "" for a skill
} BWGE_Cast2;
#pragma pack(pop)
typedef int(__cdecl* BWGE_BindActionFn)(void* engine, uint32_t gesture, const char* action);        // "" unbinds
typedef int(__cdecl* BWGE_GetActionFn)(void* engine, uint32_t gesture, char* out, uint32_t cap);   // "" = a skill or nothing
typedef int(__cdecl* BWGE_PollCasts2Fn)(void* engine, BWGE_Cast2* out, int max);

// A take to play back (optional export), once: its samples in time, the hands in the joints' frame (as BWGE_PoseZone.centre: metres
// from the pelvis, x right, y up, z back) and turned in it (x y z w; the controller as it would be held facing -z). A pose has one sample.
#pragma pack(push, 8)
typedef struct BWGE_TakeSample {
    float timeS;           // from the take's start
    float hand[2][3];      // [0] left, [1] right
    float rot[2][4];
    float grip[2];         // 0 open .. 1 fist
    uint32_t handValid;    // bit 0 left, bit 1 right
} BWGE_TakeSample;
#pragma pack(pop)
typedef int(__cdecl* BWGE_GetTakeFn)(void* engine, uint32_t gesture, BWGE_TakeSample* out, int max);   // the samples written; 0 none

// A glyph's hand (optional export): BW_LINK_GH_LEFT / RIGHT, or BOTH = either hand - a take armed with BOTH is drawn by whichever pen
// presses first, and in play a stroke from either pen matches it. A glyph keeps only the drawing hand's path; changing the hand keeps that
// path as the glyph (no re-recording). Saved in the per-class gesture. Not a glyph: BW_LINK_GR_BAD; its take running: BW_LINK_GR_BUSY.
typedef int(__cdecl* BWGE_SetHandFn)(void* engine, uint32_t gesture, uint8_t hand);

// A gesture's mode (optional exports): what the game does when it is recognised - 0 casts it once, 1 selects it (the game puts the skill
// on its trigger's slot). The engine only keeps it (saved in the per-class gesture); its casts come through BWGE_PollCasts2 either way.
typedef int(__cdecl* BWGE_SetModeFn)(void* engine, uint32_t gesture, uint8_t mode);   // 0 cast, 1 select; others BW_LINK_GR_BAD
typedef int(__cdecl* BWGE_GetModeFn)(void* engine, uint32_t gesture, uint8_t* out);

// The exports, by these names:
//   BWGE_Abi BWGE_Open BWGE_Close BWGE_SetSet BWGE_Feed BWGE_Record BWGE_Stop BWGE_Keep BWGE_Discard BWGE_Clear BWGE_Bind BWGE_SetColor
//   BWGE_SetThreshold BWGE_List BWGE_PollCasts BWGE_Trail
//   optional: BWGE_GetPoseZone BWGE_SetPoseZone BWGE_BindAction BWGE_GetAction BWGE_PollCasts2 BWGE_GetTake BWGE_SetHand BWGE_SetMode
//             BWGE_GetMode

#ifdef __cplusplus
}
#endif
