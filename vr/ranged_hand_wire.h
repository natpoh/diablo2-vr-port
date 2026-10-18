// ranged_hand_wire.h - [hands] shoulder_draw_ranged, a bow or crossbow put away behind a shoulder and drawn from it (vr/ranged_hand.h) in the game. Stepped by the skeleton hook once per pose
// (rangedwire::Apply); the answer goes back in skel::Input (rangedStowed, rangedSide) and out to whoever reads the bow's hand (vrcam's AimHand, the bow draw).
#pragma once

#include "skeletons.h"

namespace rangedwire {

// The skeleton hook, after the shield's: with in->rangedHolster (vrcam: shoulder_draw and the tick, F4, a bow or crossbow is the weapon) steps the holster on a new head sample and sets
// in->rangedStowed and in->rangedSide. Without it: forgets everything and leaves rangedSide at -1 (not handled).
void Apply(skel::Input* in);

// Any thread: put away now; and the hand that holds it (0 right, 1 left), -1 when the holster is not in use.
bool Stowed();
int Side();

// A pulse in a hand (skeleton side) that enters a shoulder while the weapon is put away; vrcam registers haptics::Pulse.
using HapticFn = void (*)(int side, float amplitude, int ms);
void SetHaptic(HapticFn fn);
using LogFn = void (*)(const char*);
void SetLog(LogFn fn);

}  // namespace rangedwire
