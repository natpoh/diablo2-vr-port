// weight_wire.h - the grips of a weapon that has weight ([weight], vr/weight.cpp), wired to the plugin's input (weight_wire.cpp).
//
// A hand that holds the weapon holds it by its grip, so that grip is down all the time - and in direct input's layout a grip held is
// the modifier of the chords (right grip + B = the fourth skill slot, left grip + stick click = recentre, left grip + menu = the map).
// While a hand holds the weapon its grip is TAKEN from the layout (directinput::Take): B stays B. A taken control reads as let go for
// everything after the frame (the shared block's grips too), so the weight model reads the grips as the controllers really have them
// from the frame itself (RawGrips), or it would let go of what its own Take hid.
//
// F4 ONLY (vr/view_gate.h): in another view nothing is taken; a menu open, no focus: nothing is taken either.
#pragma once

#include <D2RLPlugin/api.h>

namespace weightwire {

// From D2RLoaderLoadPlugin: the frame listener.
void Init(const D2RL::PluginContext* ctx);

// The grips as the controllers have them (hand 0 right, 1 left, as D2RVR_Shared), 0..1, from direct_input's newest frame - made
// before any gesture's Take hid anything. False when there is no native frame (no session, or BodyWalk's tracking alone): the shared
// block's grips are then the real ones.
bool RawGrips(float out[2]);

// The triggers the same way (hand 0 right, 1 left, 0..1) - only the grip library's loose hold reads them.
bool RawTriggers(float out[2]);

// [weight] weapon_grip_no_chord (0 by default: as before). The Take above keeps a weapon's grip from the layout's chords, but the skill panel and the settings read the grips from
// the frame itself: the off hand's grip made the wheel's grip + click chord, and the right grip held kept the right A / right trigger from their A / RT skill slots
// ([openxr] skill_rt_a). On: a grip that holds the weapon (or that another gesture holds) is no modifier of those either (vr/wheel_controller.h SetGripsTaken; and of the
// old both-grips settings chord, vr/hset_panel.h FrameIn::gripTaken - which is off anyway unless [debug] settings_two_grip_chord). Read with the ini.
void ConfigureGripNoChord(bool on);
bool GripNoChord();
// The key on, F4, and direct_input's hand (0 left, 1 right) holds the weapon now. Any thread.
bool GripOnWeapon(int hand);

}  // namespace weightwire
