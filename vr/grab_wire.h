// grab_wire.h - [weight] grab_bodies in the game (vr/grab_bodies.h says what it is; the grab itself is the weight library's, vr/grab_lib.h): the skeleton hook's step.
//
// On every pose of the hero (skel::SetGrabHands, after the weight model, before the arms are posed), in F4 with the box ticked (Input::grabOn) and the library 1.4 there:
//   - the library is handed the bodies as the game poses them (meleetouch::GrabBodies: monsters and NPCs) and both hands - the palm and its turn in the render world,
//     the grip, and whether the hand may take anything (Input::grabFree: empty, no bottle, its grip nobody else's);
//   - a hand that holds a body is DRAWN where the library has it (Input::hand); its pulses are played in that hand (lifted for a weak motor); the held bodies' sway
//     goes into the doll (meleetouch::GrabSways), which draws the limb and the lean;
//   - grabs and releases are logged ("grab: ...") with why a hand let go.
// [weight] grab_props: the same hands take the level's props (Input::grabProps: knock_props' world, its physics thread steps them - vr/knock_props.h): handed over
// every pose, a hand that holds a prop is drawn where that world has it and laid round the prop's bounds; a body under the palm is the body grab's, a hand that holds a
// prop takes no body. Each box on its own (grabBodies / grabProps).
// Nothing of the game's moves. Off, another view, no world: every hand lets go at once.
#pragma once

#include "skeletons.h"

namespace grabwire {

using LogFn = void (*)(const char*);
void SetLog(LogFn fn);   // (the grab library's loader logs through it too)

void Apply(skel::Input* in);

// What the hands hold now (for the log / a readout): bit 0 the right, bit 1 the left.
unsigned Holding();
// [multiplayer] pose_share_held: the prop (knock props' entity) hand `side` holds now (*holding true) or held last (false: let go - it flies on, or lies); 0 none yet.
uint32_t PropOf(int side, bool* holding);
// ... the hands over a body they would take now, or holding one (the library's hover): a grip pressed there is the grab's, not the weapon's call from afar.
unsigned OverBody();
// [hud] wrist_status: how far a holding hand is drawn from its controller, in the room (OpenXR stage axes, metres) - the panel on its wrist rides the drawn hand.
// side: 0 right, 1 left. False when the hand holds nothing (or not lately).
bool DrawnShift(int side, float out[3]);

}  // namespace grabwire
