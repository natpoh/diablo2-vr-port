// grab_lib.h - the optional hands that grab: the D2rgb* exports of the weight library d2r_vr_weight.dll (1.4 or newer), in d2rloader\d2r_vr_libs (or beside the
// plugin, d2rl-vrcam.dll). Its C interface is shared/d2r_vr_grab.h.
//
// [weight] grab_bodies: an empty hand's grip holds a monster's or an NPC's body. The grab itself - which body a hand is over, taking it, the joint, how the hand's strength
// moves the body by its weight, letting go and tearing loose, the pulses - is the library's; the mod hands it the bodies and the hands and draws what comes back (vr/grab_wire.cpp).
// The library is not part of this repository, is not needed to build or run the mod, and may be absent: then grab_bodies does nothing (the log says so once, and the
// Settings tick is greyed out). This file only finds the DLL, checks its version and calls it; nothing of what is inside it is here.
#pragma once

#include "d2r_vr_grab.h"

namespace grablib {

using LogFn = void (*)(const char*);
void SetLog(LogFn fn);

// Where to look for the DLL instead of d2rloader\d2r_vr_libs / beside the module that holds this code (tests). Null: there again.
void SetPath(const wchar_t* path);

enum class Status { NotLooked, Missing, BadAbi, BrokenExports, Loaded };
Status GetStatus();

// Loads it when it is there and not loaded yet - at most every 2 s while it is missing or not usable - and makes its world. True when a library is in use. Any thread
// (one at a time with Step / Sways / ReleaseAll: the caller's).
bool Ensure();
bool Loaded();
const char* Name();   // the library's own name and version while one is loaded, else ""

// The library's calls on its one world (shared/d2r_vr_grab.h); with no library: nothing done (< 0, 0).
int Step(const D2rgbParams* params, const D2rgbBody* bodies, int n, const D2rgbHand* hands, float dt, D2rgbHandOut* out);
int Sways(D2rgbSway* out, int max);
int Links(D2rgbLink* out, int max);
int ReleaseAll();

}  // namespace grablib
