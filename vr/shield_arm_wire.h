// shield_arm_wire.h - [hands] shoulder_draw, the shield (vr/shield_arm.h) in the game: which forearm it is strapped to, or put away. It follows the sword and can be put away at the left
// shoulder (two ticks): stepped by the skeleton hook once per pose (shieldarmwire::Apply), the answer goes back in skel::Input (shieldArm, weight.shieldArm) and out to whoever else
// follows the shield (the smite, the haptics).
#pragma once

#include "skeletons.h"

namespace shieldarmwire {

// The skeleton hook, before the weight model: with in->shieldHolster (vrcam: shoulder_draw on, F4, the arms on the controllers, a shield in the left slot, and shoulder_draw_shield or
// shoulder_draw_shield_stow ticked) steps the follower on a new head sample and sets in->shieldArm and in->weight.shieldArm (0 the right forearm, 1 the left, 2 put away) and
// in->weight.shieldFollow. in->shieldFollow: the weapon and the shield are always on opposite arms; in->shieldStow: a grip behind the left shoulder puts the shield away and draws it.
// Without in->shieldHolster: forgets everything (the shield is on the left arm again, as the game has it) and leaves shieldArm at -1.
void Apply(skel::Input* in);

// Any thread: the arm the shield is on now - 0 right, 1 left, 2 put away - or -1 when the follower is not in use (the shield is the left arm's, as ever).
int Arm();
bool Stowed();
// The hand side (0 right, 1 left) that wears the shield, -1 when it is not handled or put away.
int WearingHand();

// The hands that hold the weapon or that it flies into, a bit each (1 right, 2 left), 0 when none: vrcam registers weight::WieldingHands. Unregistered: always 0, the shield stays on the left arm.
using WielderFn = unsigned (*)();
void SetWielderSource(WielderFn fn);
// A pulse in a hand (skeleton side) that enters the left shoulder while the shield is put away; vrcam registers haptics::Pulse.
using HapticFn = void (*)(int side, float amplitude, int ms);
void SetHaptic(HapticFn fn);
using LogFn = void (*)(const char*);
void SetLog(LogFn fn);

}  // namespace shieldarmwire
