// belt_trigger_wire.h - belt_trigger.h wired to the plugin's input (belt_trigger_wire.cpp): while a belt bottle is in a hand the triggers are taken from the layout.
//
// The belt (BeltFrame in vr/vrcam.cpp, the render thread) says every frame which hands hold a bottle (Publish); a frame listener of direct_input.h (the headset thread) turns that
// into directinput::Take of the triggers. A word older than 300 ms is not "now" (BeltFrame stopped: a menu, another view): nothing is taken then.
//
// F4 ONLY (vr/view_gate.h); a menu or panel open, no input focus: nothing is taken.
#pragma once

#include <D2RLPlugin/api.h>

namespace belttrigger {

// From D2RLoaderLoadPlugin: the frame listener.
void Init(const D2RL::PluginContext* ctx);

// The belt's word, every BeltFrame (also with nothing held): [belt] hold_takes_trigger, and a bottle in the right / left hand (taken or being drunk).
void Publish(bool enabled, bool rightHolds, bool leftHolds);

}  // namespace belttrigger
