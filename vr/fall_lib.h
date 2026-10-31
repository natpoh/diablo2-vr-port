// fall_lib.h - the optional knock-down: the D2rf* exports of the weight library d2r_vr_weight.dll, in d2rloader\d2r_vr_libs (or beside the plugin,
// d2rl-vrcam.dll) - one file with the weight, the stab and the doll. Its C interface is shared/d2r_vr_fall.h.
//
// [weight] knock_down draws a struck humanoid falling, lying and getting up. How it falls and gets up is the library's; the mod keeps the rest - when a blow
// knocks a body down, which bones are its joints, F4, the drawing, the settings. The library is not part of this repository, is not needed to build or run the
// mod, and may be absent: then knock_down does nothing (the log says so once, and the Settings tick is greyed out). This file only finds the DLL, checks its
// version and calls it; nothing of what is inside it is here. It keeps the library's one world (made when first needed, gone with Reset).
#pragma once

#include "d2r_vr_fall.h"

namespace falllib {

using LogFn = void (*)(const char*);
void SetLog(LogFn fn);

// Where to look for the DLL instead of d2rloader\d2r_vr_libs / beside the module that holds this code (tests). Null: there again.
void SetPath(const wchar_t* path);

// Is the file there? Tells the log once (and again when that changes); does not load it.
bool Probe();
// Loads it when it is there and not loaded yet - at most every 2 s while it is missing or not usable. True when a library is in use. Safe to call often.
bool Ensure();
bool Loaded();
const char* Name();   // the library's own name and version while one is loaded, else ""

// The library's calls on its world (one at a time: the caller keeps them so); with no library: nothing done (0).
int KnockDown(const D2rfKnock& k);
int Step(float dt);
int Poses(D2rfPose* out, int max);
int LetGo(uint32_t id, int how);
int Home(uint32_t id, const float joint[D2RF_JOINTS][3]);
int Grab(const D2rfKnock& body, int hand, const float point[3], int joint = -1);   // (joint >= 0: held by that joint's limb - D2rfGrabPart, or D2rfGrab without it)
int Hand(uint32_t id, int hand, const float pos[3]);
int Release(uint32_t id, int hand);
int Held(uint32_t id, int hand, float a[3], float b[3], float* r);
void Reset();   // the world gone: every body down is let go of

}  // namespace falllib
