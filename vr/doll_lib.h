// doll_lib.h - the optional monsters' weight: the D2rd* exports of the weight library d2r_vr_weight.dll (1.2 or newer), in d2rloader\d2r_vr_libs (or beside the
// plugin, d2rl-vrcam.dll) - one file with the weight and the stab. Its C interface is shared/d2r_vr_doll.h.
//
// [weight] monster_ragdoll gives the monsters weight: a push of the held weapon moves a limb and, less, the whole monster by its mass, and muscles bring them back. How that
// moves is the library's; the mod keeps the rest - the bodies from the game's models, which monsters are humanoids, the weapon's push, F4, the drawing, the settings. The
// library is not part of this repository, is not needed to build or run the mod, and may be absent: then monster_ragdoll does nothing (the log says so once, and the Settings
// tick is greyed out). This file only finds the DLL, checks its version and calls it; nothing of what is inside it is here.
#pragma once

#include "d2r_vr_doll.h"

namespace dolllib {

using LogFn = void (*)(const char*);
void SetLog(LogFn fn);

// Where to look for the DLL instead of d2rloader\d2r_vr_libs / beside the module that holds this code (tests). Null: there again.
void SetPath(const wchar_t* path);

enum class Status { NotLooked, Missing, BadAbi, BrokenExports, Loaded };
Status GetStatus();

// Is the file there? Tells the log once (and again when that changes); does not load it.
bool Probe();
// Loads it when it is there and not loaded yet - at most every 2 s while it is missing or not usable. True when a library is in use. Safe to call every frame.
bool Ensure();
bool Loaded();
const char* Name();   // the library's own name and version while one is loaded, else ""

// The library's calls (shared/d2r_vr_doll.h); with no library: nothing done (false, 0).
bool Push(D2rdState* state, const D2rdParams* params, const D2rdBody* bodies, int n, int i, const float impulse[3], const float point[3]);
bool Step(D2rdState* state, const D2rdParams* params, float dt);
int Apply(const D2rdState* state, const D2rdParams* params, D2rdBody* bodies, int n);
float Mass(const D2rdParams* params, const D2rdBody* bodies, int n, uint32_t id);
float Balance(const D2rdState* state, const D2rdParams* params, uint32_t id, float off[3]);

}  // namespace dolllib
