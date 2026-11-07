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

}  // namespace gelib
