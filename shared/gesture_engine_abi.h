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

// The exports, by these names:
//   BWGE_Abi BWGE_Open BWGE_Close BWGE_SetSet BWGE_Feed BWGE_Record BWGE_Stop BWGE_Keep BWGE_Discard BWGE_Clear BWGE_Bind BWGE_SetColor
//   BWGE_SetThreshold BWGE_List BWGE_PollCasts BWGE_Trail

#ifdef __cplusplus
}
#endif
