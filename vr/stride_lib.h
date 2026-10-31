// stride_lib.h - the optional stepping walk for the hero's legs: the D2rs* exports of d2r_vr_weight.dll (the weight library, stride 2.0 on), in
// d2rloader\d2r_vr_libs (or beside the plugin). Its C interface is shared/d2r_vr_stride.h.
//
// [body] legs_side_ragdoll: going sideways (between legs_rear's front and rear cones) the hero's pelvis and legs are the ragdoll library's own gait (D2rsGait,
// interface 2: the package's walk / run / sprint cycles, the gait core the knock-down's walk home rides on too). The library is not part of this repository, is not needed to build or run the mod, and may be absent or
// older: then the legs stay held as legs_rear holds them, and the log says why. This file only finds the DLL, checks its version and calls it; nothing of
// what is inside it is here.
#pragma once

#include "d2r_vr_stride.h"

namespace stridelib {

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

// The hero's pelvis and legs now (D2rsGait), any thread: false when no library is loaded or it refused.
bool Gait(const D2rsGaitIn& in, D2rsGaitState* state, D2rsGaitOut* out);
// [body] legs_side_steps: the library's stepping walk too (D2rsStep, in the same library since stride 1.0): one step of the feet; false when absent or refused.
bool HasSteps();
bool Step(const D2rsLegs& in, D2rsState* state, D2rsFeet* out);

}  // namespace stridelib
