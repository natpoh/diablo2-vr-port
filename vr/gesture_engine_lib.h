// gesture_engine_lib.h - [gestures] engine_in_game: BodyWalk's gesture engine as a library in the game's process, d2r_vr_gestures.dll (closed, binary
// only; its C interface is shared/gesture_engine_abi.h), in d2rloader\d2r_vr_libs or beside the plugin (as the weight library: vr/doll_lib.h). It records,
// recognises and keeps the gestures (in BodyWalk's own settings folder, per class); the mod draws the Gestures tab, feeds it the headset's frames,
// tells it the hero's class and skills, and casts what it recognises the game's own way. Absent or not usable: the Gestures tab says so, nothing else
// changes. This file only finds the DLL, checks its version, opens it and calls it - nothing of what is inside it is here.
//
// Every call from one thread (the headset frame's: vr/gesture_engine_wire.cpp). Status and Name may be asked from any thread.
#pragma once

#include <cstdint>

#include "gesture_engine_abi.h"

namespace gelib {

using LogFn = void (*)(const char*);
void SetLog(LogFn fn);

// Where to look for the DLL instead of d2rloader\d2r_vr_libs / beside the module that holds this code (tests). Null: there again.
void SetPath(const wchar_t* path);
// The game id the engine is opened with (default "d2r": its gestures under games\d2r, its "Local\BodyWalk.GestureEngine.d2r" mutex). Tests use their own
// ("d2rtest"), so a BodyWalk running beside them does not hold back its casts. Before Ensure; null: "d2r" again.
void SetGameId(const char* id);

enum class Status { NotLooked, Missing, BadAbi, BrokenExports, OpenFailed, Loaded };
Status GetStatus();
const char* StatusText(Status s);

// Loads and opens it when it is there and not open yet - at most every 2 s while it is missing or not usable. `dataDir`: BodyWalk's settings folder
// (UTF-8). True when an engine is open. Cheap when open: call every frame.
bool Ensure(const char* dataDir);
bool Loaded();
// Closes the engine and lets the DLL go (the session's end, the key off).
void Shutdown();

// The engine's calls (shared/gesture_engine_abi.h); with none open: nothing done (BW_LINK_GR_BAD, 0).
void SetSet(const char* setKey, const char* setName, const BWGE_Skill* skills, uint32_t count);
void Feed(const BWGE_Frame& frame);
int Record(uint32_t gesture, uint8_t type, uint8_t hand, int32_t skill);
int Stop();
int Keep();
int Discard();
int Clear(uint32_t gesture);
int Bind(uint32_t gesture, int32_t skill);
int SetColor(uint32_t gesture, uint32_t rgba);
int SetThreshold(uint32_t gesture, float percent);
uint32_t List(BW_LinkGestures* out);
int PollCasts(BWGE_Cast* out, int max);
uint32_t Trail(BW_LinkVrRune* out, uint32_t max);
// A pose's zone (the optional BWGE_GetPoseZone / BWGE_SetPoseZone: an older library has none - HasPoseZones false, the calls BW_LINK_GR_BAD).
bool HasPoseZones();
int GetPoseZone(uint32_t gesture, BWGE_PoseZone* out);
int SetPoseZone(uint32_t gesture, float radiusM);
// A take's samples (the optional BWGE_GetTake): how many written; 0 none / no call.
int GetTake(uint32_t gesture, BWGE_TakeSample* out, int max);
// Game actions (the optional BWGE_BindAction / BWGE_GetAction / BWGE_PollCasts2): HasActions false without them (then PollCasts as ever).
bool HasActions();
int BindAction(uint32_t gesture, const char* action);
int GetAction(uint32_t gesture, char* out, uint32_t cap);
int PollCasts2(BWGE_Cast2* out, int max);
// A glyph's hand (the optional BWGE_SetHand: LEFT / RIGHT / BOTH = either hand); HasSetHand false without it.
bool HasSetHand();
int SetHand(uint32_t gesture, uint8_t hand);
// A gesture's mode (the optional BWGE_SetMode / BWGE_GetMode: 0 cast once, 1 select); HasModes false without them.
bool HasModes();
int SetMode(uint32_t gesture, uint8_t mode);
int GetMode(uint32_t gesture, uint8_t* out);

}  // namespace gelib
