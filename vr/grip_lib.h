// grip_lib.h - the optional grip library: d2r_vr_grip.dll, beside the plugin (d2rl-vrcam.dll). Its C interface is shared/d2r_vr_grip.h.
//
// The mod has its own way of holding a weapon (the grip alone holds it, the second hand slides while it is squeezed, the fingers have the game's own grip pose); a library, when it is
// there, can take over how a hand takes hold of the shaft and looks while it does ([weight] grip_model 1): which buttons hold a hand, tight and loose grips, where the hand sits on the
// wood, how the fingers close on it, the pulses. The library is not part of this repository, is not needed to build or run the mod, and may be absent: then grip_model 1 is our own
// way, and the Settings tick is greyed out with a line that says why. This file only finds the DLL, checks its version and puts it behind heft::hold::GripEngine and the hook's
// hand step; nothing of what is inside it is here.
#pragma once

#include "d2r_vr_grip.h"

namespace griplib {

using LogFn = void (*)(const char*);
void SetLog(LogFn fn);

// Where to look for the DLL instead of beside the module that holds this code (tests). Null: beside it again.
void SetPath(const wchar_t* path);

enum class Status { NotLooked, Missing, BadAbi, BrokenExports, Loaded };
Status GetStatus();

// Is the file there? Tells the log once (and again when that changes); does not load it.
bool Probe();
// Loads it when it is there and not loaded yet - at most every 2 s while it is missing or not usable - and makes it the grip engine of the held weapon (heft::hold::SetGripEngine).
// True when a library is in use. Safe to call every frame.
bool Ensure();
bool Loaded();
const char* Name();   // the library's own name and version while one is loaded, else ""
int Abi();            // the interface version the loaded library speaks (D2RG_ABI_OLDEST .. D2RG_ABI_VERSION), else 0

// The library's hand step for the skeleton hook. Negative when no library is loaded (or the library refuses the step). A library of interface version 1 is given what version 1 had:
// the version-2 fields (the shaft against the hand) are cleared, and so is the lean of the line - it lays the hand the version-1 way, as it always did.
int32_t HandStep(const D2rgHandIn& in, const D2rgRig& rig, D2rgHandState* state, D2rgHandOut* out);

}  // namespace griplib
