// video3d_lib.h - the optional 3D cinematics: the D2rv3* exports of the library d2r_vr_video3d.dll, in d2rloader\d2r_vr_libs (or beside the plugin,
// d2rl-vrcam.dll). Its C interface is shared/d2r_vr_video3d.h.
//
// [cinema] video_3d shows the game's cinematics in 3D on the big screen ([cinema] world_screen), in every VR view: the library guesses each film frame's depth and
// draws a left and a right eye from it. The mod keeps the rest - when a film plays, where it is in the frame, the swapchain and the screen in the room, the settings.
// The library is not part of this repository, is not needed to build or run the mod, and may be absent: then the films stay flat (the log says so once). This file
// only finds the DLL, checks its version and calls it; nothing of what is inside it is here.
#pragma once

#include "d2r_vr_video3d.h"

namespace video3dlib {

using LogFn = void (*)(const char*);
void SetLog(LogFn fn);
void SetPath(const wchar_t* path);   // tests: look here instead of d2r_vr_libs / beside the module; null: there again

enum class Status { NotLooked, Missing, BadAbi, BrokenExports, Loaded };
Status GetStatus();

// Loads it when it is there and not loaded yet - at most every 2 s while it is missing or not usable. True when a library is in use.
bool Ensure();
const char* Name();   // the library's own name and version while one is loaded, else ""

// Ready for this device and queue; once per device (a failed start is not tried again for that device). True when ready.
bool Start(void* device, void* queue);
// The library's calls; with no library or not started: nothing done (Record -1).
int Record(void* list, const D2rv3Frame* frame);
void Submitted();
void Reset();
void Stop();   // the device goes: the library lets go of it

}  // namespace video3dlib
