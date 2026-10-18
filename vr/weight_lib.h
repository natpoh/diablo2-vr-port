// weight_lib.h - the optional weight-physics library: d2r_vr_weight.dll, beside the plugin (d2rl-vrcam.dll). Its C interface is shared/d2r_vr_weight.h.
//
// The mod has its own weight physics (cleanroom/heft); a library, when it is there, can move the held weapon instead ([weight] hands_model 1). The library is not part of this
// repository, is not needed to build or run the mod, and may be absent: then hands_model 1 is our own physics, and the Settings tick is greyed out with a line that says why.
// This file only finds the DLL, checks its version and puts it behind heft::hold::Engine; nothing of what is inside it is here.
#pragma once

namespace weightlib {

using LogFn = void (*)(const char*);
void SetLog(LogFn fn);

// Where to look for the DLL instead of beside the module that holds this code (tests). Null: beside it again.
void SetPath(const wchar_t* path);

enum class Status { NotLooked, Missing, BadAbi, BrokenExports, Loaded };
Status GetStatus();

// Is the file there? Tells the log once (and again when that changes); does not load it.
bool Probe();
// Loads it when it is there and not loaded yet - at most every 2 s while it is missing or not usable - and makes it the engine of the held body (heft::hold::SetEngine).
// True when a library is in use. Safe to call every frame.
bool Ensure();
bool Loaded();
const char* Name();   // the library's own name and version while one is loaded, else ""

}  // namespace weightlib
