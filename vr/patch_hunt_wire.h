// patch_hunt_wire.h - [debug] patch_hunt, the plugin's side (vr/patch_hunt.cpp): the step from the ini (LoadSettings) or Ctrl+F8, the game's own
// graphics settings put to the step's values and given back (its setting objects and their own setter, on the UI thread - vr/patch_hunt.h has the
// places), a line at the top of the flat picture (D2RLoader's overlay) and a line in the log at every change. The steps: vr/patch_hunt.h.
#pragma once

#include <D2RLPlugin/context.h>

#include <cstdint>

namespace patchhunt {

// What the other suspects are as the ini has them (the log's line at a change tells them beside the step).
struct Suspects {
    int depthFixMode = 0;            // [render] depth_fix_mode as read (before the step)
    bool lightBoundsFix = false, lightBoundsFlat = false;
    bool fog = false, sky = false, gameSky = false, ceiling = false, gameCeiling = false, blur = false;
    int platform = 0;                // [mode] platform
    float flatShiftX = 0.0f, flatShiftY = 0.0f;
};

// From D2RLoaderLoadPlugin: the overlay (D2RLoader takes a frame callback only at the load) and the UI thread's service.
void Init(const D2RL::PluginContext* ctx, const wchar_t* iniPath);
// From LoadSettings, every read of the ini: the step ([debug] patch_hunt), the pipeline's K ([debug] patch_hunt_pipe) and the rest as read.
void Configure(int step, int pipeK, const Suspects& s);
// The step in force (the update thread, the draw thread: one atomic load).
int Step();
// vrcam's update thread, every 1 ms: Ctrl+F8 next step, Ctrl+Shift+F8 back, Ctrl+Alt+F8 / Ctrl+Alt+Shift+F8 the next / last pipeline of step 8 -
// written to the ini (the reload brings them back through Configure); and the game's settings due to the UI thread.
void KeyTick(bool focus);
void Tick();
// The game's graphics options now (AO, shadows, lights, anti aliasing, DLSS, resolution scale - vr/patch_hunt.h's order), -1 not known. Any thread
// but the picture's (it takes the lock the UI thread's run holds).
void GameValues(int out[6]);
// A short line at the top left of the flat picture for `ms` (the capture key's "capture saved").
void Notice(const char* text, unsigned ms);

}  // namespace patchhunt
