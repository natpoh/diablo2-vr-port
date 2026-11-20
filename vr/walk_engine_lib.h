// walk_engine_lib.h - [walk_in_place]: BodyWalk's walking engine as a library in the game's process, d2r_vr_walk.dll (closed, binary only; its C
// interface is shared/walk_engine_abi.h), in d2rloader\d2r_vr_libs or beside the plugin (its own file, not inside the gesture engine's DLL). It finds the
// steps in the headset's motion and answers a walking speed; the mod makes that the game's own walking stick (vr/walk_in_place.h). Absent or not
// usable: the Walk tab's line says so, nothing else changes. This file only finds the DLL, checks its version, opens it and calls it.
//
// Every call from one thread (the headset frame's: vr/walk_in_place_wire.cpp). Status may be asked from any thread.
#pragma once

#include <cstdint>

#include "walk_engine_abi.h"

namespace welib {

using LogFn = void (*)(const char*);
void SetLog(LogFn fn);

// Where to look instead of d2rloader\d2r_vr_libs / beside the module that holds this code (tests): the file itself. Null: there again.
void SetPath(const wchar_t* path);
// The game id the engine is opened with (default "d2r"). Tests use their own. Before Ensure; null: "d2r" again.
void SetGameId(const char* id);

enum class Status { NotLooked, Missing, BadAbi, BrokenExports, OpenFailed, Loaded };
Status GetStatus();
const char* StatusText(Status s);
// The file it was loaded from ("d2r_vr_walk.dll"), "" when none.
const char* FileName();

// Loads and opens it when it is there and not open yet - at most every 2 s while it is missing or not usable. `dataDir`: BodyWalk's settings
// folder (UTF-8). True when an engine is open. Cheap when open: call every frame.
bool Ensure(const char* dataDir);
bool Loaded();
void Shutdown();

// The engine's calls; with none open: nothing done (out zeroed).
void SetTuning(const BWWE_Tuning& t);
void Feed(const BWWE_Frame& frame, BWWE_State* out);
void Reset();

}  // namespace welib
