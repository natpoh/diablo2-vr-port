// shoulder_swap_wire.h - the weapon-set swap over the shoulder, wired to the plugin's input (shoulder_swap_wire.cpp).
// The detector is shoulder_swap.h; this listens to direct_input.h's frames (native OpenXR, with or without the
// plugin's own controls) and sends the game's own "Swap weapons" command.
#pragma once

#include <D2RLPlugin/api.h>

// F4 ONLY (vr/view_gate.h): the gesture claims, takes and swaps nothing in another view; the belt gets its bottles back at once.
namespace shoulderwire {

// From D2RLoaderLoadPlugin: the log, and the frame listener.
void Init(const D2RL::PluginContext* ctx);

// [hands] shoulder_swap (the tick box; 0 = nothing changes) and shoulder_swap_hand (0 right, 1 left). From
// LoadSettings, any thread; applied at the next frame.
void Configure(bool on, bool leftHand);

// [hands] shoulder_draw (vr/shoulder_draw.h; read in LoadSettings): the zone behind each shoulder is a holster, not the weapon swap - "Swap weapons" is never pressed
// from it (whatever shoulder_swap says), and a grip pressed behind EITHER shoulder with EITHER hand is the zone's (the weight model draws the weapon with it, vr/weight.cpp).
void SetDraw(bool on);
// shoulder_draw claims the grips behind the shoulders only while the weight model has a weapon to draw (weight::Placed, registered once by vrcam): with a bow in hand, or the
// weight box off, the zone is nothing. Unregistered (null): always.
using WeaponFn = bool (*)();
void SetWeaponSource(WeaponFn weaponPlaced);

// Is this hand's grip the gesture's now (hand 0 right, 1 left, as D2RVR_Shared)? True while the hand is behind a
// shoulder with its grip down, and until that press is let go: a belt potion must not be taken with it. False
// when the box is unticked, and a quarter of a second after the last frame.
bool Claims(int hand);

// [weight] to_back: a weapon let go may be stowed on the hero's back, and a hand behind a shoulder pressing its grip then TAKES it
// (vr/shoulder_grip.h) - that press is not also the weapon swap. The weight model registers its "a weapon is stowed on the back" once;
// unregistered (null), the default: nothing is ever stowed and the swap is as it always was. Any thread.
using StowedFn = bool (*)();
void SetStowedSource(StowedFn stowedOnBack);

}  // namespace shoulderwire
