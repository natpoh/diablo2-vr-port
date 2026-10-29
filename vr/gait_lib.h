// gait_lib.h - the optional gait library: d2r_vr_gait.dll, in d2rloader\d2r_vr_libs (or beside the plugin). Its C interface is shared/d2r_vr_gait.h.
//
// [multiplayer] pose_share_legs: where the other players' heroes put their feet - steps along their travel whatever way their bodies face (vr/remote_pose.h,
// skeletons.cpp PoseRemoteHero). The library is not part of this repository, is not needed to build or run the mod, and may be absent: then the legs keep
// the game's own animation, as before, and the log says why. This file only finds the DLL, checks its version and calls it; nothing of what is inside it is here.
#pragma once

#include "d2r_vr_gait.h"

namespace gaitlib {

using LogFn = void (*)(const char*);
void SetLog(LogFn fn);

// Where to look for the DLL instead of d2rloader\d2r_vr_libs / beside the module that holds this code (tests). Null: there again.
void SetPath(const wchar_t* path);

enum class Status { NotLooked, Missing, BadAbi, BrokenExports, Loaded };
Status GetStatus();

// Loads it when it is there and not loaded yet - at most every 2 s while it is missing or not usable. True when a library is in use. Call it from the
// plugin's own thread (it may load a DLL), never from the game's pose hook.
bool Ensure();
bool Loaded();
const char* Name();   // the library's own name and version while one is loaded, else ""

// One step of one hero's legs (D2rfLegsStep), any thread: false when no library is loaded or it refused.
bool Step(const D2rfLegsIn& in, D2rfState* state, D2rfLegsOut* out);

}  // namespace gaitlib
