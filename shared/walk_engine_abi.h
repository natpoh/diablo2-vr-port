// walk_engine_abi.h - the C interface of BodyWalk's walking engine as a library in the game's process: d2r_vr_walk.dll (closed, binary only).
// Plan: docs/notes/walk_in_place.md (2026-11-19). BodyWalk's sdk/include/bodywalk/walk_engine.h is meant to be this file byte for byte; d2r_vr
// keeps this copy and only finds, checks and calls the library (vr/walk_engine_lib.cpp) - nothing of how steps are found is here.
//
// Rules of the interface (as the gesture engine's, shared/gesture_engine_abi.h):
//   - C, __cdecl, plain structs with a leading `size` where they may grow; strings UTF-8.
//   - Every call is made from ONE thread (the game's headset frame); the library has no thread of its own and never throws out of an export.
//   - BodyWalk is NOT running and need not be installed (Serg 2026-11-19: "бодивалк при этом не запускается", as with the mapping): the library is
//     the whole walk. It reads its walk profile (BodyWalk's own walk settings for this game, when the user also uses BodyWalk) from
//     <dataDir>\games\<gameId>\walk.json if it is there - else its own defaults for a headset - and re-reads it when the file changes (checked on its own
//     frame, no thread); it writes nothing there. The mod's trims (BWWE_SetTuning) are applied on top.
//   - While open it holds the named mutex "Local\BodyWalk.WalkEngine.<gameId>": the BodyWalk app leaves this game's walking to it.
//   - The library is its own file, d2r_vr_walk.dll (Serg 2026-11-19: not inside the gesture engine's d2r_vr_gestures.dll), in the mod's
//     d2rloader\d2r_vr_libs (D2R VR Setup puts it there) or beside the plugin. Optional: without it the Walk tab says so, nothing else changes.
#pragma once

#include <stdint.h>

#define BWWE_ABI 1u
#define BWWE_FILE L"d2r_vr_walk.dll"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 8)
typedef struct BWWE_Pose {
    float pos[3];      // metres, the room's (OpenXR stage) space: x right, y up, z back
    float rot[4];      // x y z w
    uint32_t valid;    // 1 = this frame's
} BWWE_Pose;

typedef struct BWWE_Frame {
    uint32_t size;     // sizeof(BWWE_Frame)
    uint32_t flags;    // BWWE_FRAME_*
    double timeS;      // seconds, monotonic (the headset's display time)
    BWWE_Pose head;    // between the eyes
    BWWE_Pose hand[2]; // [0] left, [1] right (the controllers' grip poses): the library may use them or not
} BWWE_Frame;
enum {
    BWWE_FRAME_PLAY = 1u << 0,   // a game area, no menu, F4: the speed may walk the hero (else the library keeps its state but answers 0)
};

typedef void (*BWWE_LogFn)(const char* line);
typedef struct BWWE_Init {
    uint32_t size;         // sizeof(BWWE_Init)
    uint32_t abi;          // BWWE_ABI
    const char* dataDir;   // BodyWalk's settings folder (%LOCALAPPDATA%\BodyWalkVR)
    const char* gameId;    // "d2r"
    BWWE_LogFn log;        // its log lines (may be null)
} BWWE_Init;

// The player's trims from the game's own settings (d2r_vr.ini [walk_in_place]), each 0..1; 0.5 = BodyWalk's walk profile as it is.
typedef struct BWWE_Tuning {
    uint32_t size;         // sizeof(BWWE_Tuning)
    float sensitivity;     // higher: lighter steps count
    float speed;           // higher: full speed with gentler steps
    float smoothing;       // higher: a steadier speed, slower to start and stop
} BWWE_Tuning;

// The answer to one frame.
typedef struct BWWE_State {
    uint32_t size;         // sizeof(BWWE_State)
    uint32_t flags;        // BWWE_STATE_*
    float speed;           // 0 standing .. 1 full speed (0 while the frame is not BWWE_FRAME_PLAY)
    float cadenceHz;       // steps a second now (for the page's line; 0 unknown)
    uint32_t steps;        // steps counted since the library opened
    uint32_t profileRevision;   // + 1 whenever the walk profile was (re)read
} BWWE_State;
enum {
    BWWE_STATE_WALKING = 1u << 0,   // walking in place now
    BWWE_STATE_STEP = 1u << 1,      // a step was found in this frame
    BWWE_STATE_PROFILE = 1u << 2,   // a walk profile from BodyWalk's folder is in use (else the library's own defaults)
};
#pragma pack(pop)

typedef uint32_t(__cdecl* BWWE_AbiFn)(void);
typedef void*(__cdecl* BWWE_OpenFn)(const BWWE_Init* init);   // null (and a log line) when it cannot
typedef void(__cdecl* BWWE_CloseFn)(void* engine);
typedef void(__cdecl* BWWE_SetTuningFn)(void* engine, const BWWE_Tuning* tuning);
typedef void(__cdecl* BWWE_FeedFn)(void* engine, const BWWE_Frame* frame, BWWE_State* out);   // every headset frame in F4
typedef void(__cdecl* BWWE_ResetFn)(void* engine);   // forget what is under way (a new session, F4 left and back, the tick off and on)

// The exports, by these names:
//   BWWE_Abi BWWE_Open BWWE_Close BWWE_SetTuning BWWE_Feed BWWE_Reset

#ifdef __cplusplus
}
#endif
