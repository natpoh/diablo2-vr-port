// melee_trigger_wire.h - [gameplay] melee_touch_only wired to the plugin's input (melee_trigger_wire.cpp). The decision is melee_trigger.h (pure, tested); this listens to direct_input.h's frames and, while hit
// by touch is swinging a weapon (anything but a bow or a crossbow), withholds the triggers' attack outputs from the plugin's own layout.
#pragma once

#include <D2RLPlugin/api.h>

// F4 ONLY (vr/view_gate.h): nothing is withheld in another view. Only the plugin's own layout is reached (native OpenXR with [openxr] direct_input): with BodyWalk's own input the trigger is BodyWalk's.
namespace meleetrigger {

// From D2RLoaderLoadPlugin: the log, and the frame listener.
void Init(const D2RL::PluginContext* ctx);
// [gameplay] melee_touch_only from the ini (every read, any thread; applied at the next frame; default ON - a part of hit by touch, the key is the way back). Off: nothing below does anything.
void Configure(bool on);
bool On();

}  // namespace meleetrigger
