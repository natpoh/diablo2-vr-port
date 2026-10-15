// throw_gesture_wire.h - [hands] throw_gesture / throw_by_grip: a javelin or throwing weapon thrown by the hand's
// motion, ending in the game's own throw (vr/throw_gesture.h detects it; vr/throw_gesture_wire.cpp wires it to the
// headset frames of vr/direct_input.h; vr/vrcam.cpp turns its heading into the aim of the game's attack).
//
// At the release the game's own attack is pressed once (the pad's A through direct_input's Action) and,
// for the few ticks the game takes to read it, the aim of that attack is the hand's way - the same
// aim hooks that [hands] throw_aim steers (HookSpellResolve, HookAttackPoint, UnitFacing). The game's
// missile, speed, collision, ammunition and animation are the game's own.
//
//   throw_gesture: the right TRIGGER pressed, the hand swung, the trigger let go (within 0.08 - 3 s).
//   throw_by_grip: the right GRIP is the weapon "in the hand", held as long as you like; the swing at the end of
//                  it and the release of the grip in the end zone of the swing is the throw; a release with the
//                  hand at rest throws nothing (it is the weapon being put down - the weapon-weight logic's
//                  business, which reads GripIsThrows() and LastThrowTickMs() below). Needs [openxr] direct_input=1.
#pragma once

#include <cstdint>

#include "throw_land.h"
#include "throw_source.h"

namespace D2RL {
struct PluginContext;
}

// F4 ONLY (vr/view_gate.h): no stroke, take, press or aim in another view; a view change lets go.
namespace throwwire {

// Once, before anything else: the log, and the question of whether the camera's frame is known (a
// heading in the head's frame can be made the world's). Without it a gesture is dropped, not thrown.
void Init(const D2RL::PluginContext* ctx, bool (*canAim)());

// [hands] throw_gesture and throw_by_grip from the ini, on every change. Either on: listens to direct_input's
// frames (once, kept after) and, with a javelin or a throwing weapon in the hand, takes the right trigger
// (throw_gesture) or the right grip (throw_by_grip, which wins when both are ticked, and works only while
// direct_input feeds the game) from the layout. Both off: everything let go, the aim hooks see nothing of it.
void SetModes(bool byTrigger, bool byGrip);

// A gesture was released lately (the attack is pressed, the game has not had its time to read it):
// the throw's heading in the head's own frame, a unit vector on the ground, `right` of the head and
// `forward` of it. vrcam makes it the world's with the camera's axes, as it does for the hand's ray.
// Any thread.
bool Aim(float* right, float* forward);

// The throw's A attack is pressed now (from the press to its let-go): the plugin's attack that this is a THROW's, not the layout's or a touch's. The
// pad poll (vr/stand_still_wire.cpp) asks it to leave Stand Still out of a throw when [hands] throw_stand_still is 0. Any thread.
bool PressIsThrow();

// vrcam: a hook of the game's aim read the throw's heading (so the log can say when none did).
void NoteRead();

// vrcam, once: the hero's mode now (the game's player mode: 1 neutral, 2 walk, 7 / 8 attack, 10 cast, 11 throw ...) - false when it cannot be read.
// After a throw's press the log says what the hero did: the game's answer to the press.
void SetHeroModeFn(bool (*fn)(uint32_t* mode));

// vrcam, once: the hook that makes the pad's A cast the game's Throw skill by itself ([hands] throw_auto_skill) is in and on. A throw then presses the
// A attack and the game reads it as Throw; without it the pad button of a slot that holds Throw is pressed (the player puts it there), else the A attack.
void SetAutoSkillFn(bool (*fn)());

// The weapon-weight model's account of a throw (vr/throw_source.h; weight::LastThrow in vr/weight.h): once, by the code that has the weight model - it
// fills `out` from weight::ThrowOutcome (the field names are the same) and says whether it is valid. With a source registered, a released throw whose
// outcome is valid and within 300 ms of the release takes its HEADING (Aim, Info) and, for a potion, its LANDING (Info::landingM, already scaled by
// [weight] throw_reach_pct - not to be scaled again) from it; anything else keeps the hand's own estimate. Unregistered, the default: the hand's own.
// The source is read when the throw is released (the outcome is there), and again by the first Aim / Info that finds none; once taken it stays for
// that throw. Any thread.
using WeightThrowFn = bool (*)(throwsrc::Outcome* out);
void SetWeightSource(WeightThrowFn fn);

// [hands] potion_throw_mode (vr/throw_land.h): 0 = a thrown potion's game throw is pressed at the release (the model is taken by the game and vanishes);
// 1 = it is pressed when the weight model's potion has flown its flight time (the model lands, the game's own throw follows it to the spot). Mode 1
// needs the weight model's account of the throw (SetWeightSource); without it the press is at the release. Live; any thread.
void SetPotionMode(int mode);

// A throwing weapon is in the hand with a gesture on, in play: the controller's skill slots are wanted (which pad button holds Throw). vrcam
// keeps the wheel's reads of them going while this is true, whether or not the skill wheel is on.
bool WantsSlots();

// The last released throw, while its aim is live (as Aim): its heading in the head's frame and how the hand was moving at the
// release - what the aim point is made from (vr/throw_aim.h: the cone around the line; a potion's ballistics).
struct ThrowInfo {
    float right = 0.0f, forward = 1.0f;   // the heading on the ground, in the head's frame (a unit vector)
    float speed = 0.0f;                   // m/s, the hand's PEAK in the 0.15 s before the release (throw_gesture.h Result::peak)
    float horizSpeed = 0.0f;              // its ground part
    float vertSpeed = 0.0f;               // its vertical part, up positive
    float releaseSpeed = 0.0f;            // m/s at the last sample in the band, the release's own (for the log)
    float handBelowHeadM = 0.0f;          // the hand's height below the head's at the release (the room's y)
    unsigned seq = 0;                     // + 1 with every released throw
    // The weight model's account of THIS throw, when it has one (SetWeightSource): the heading above is then its heading
    bool fromWeight = false;
    float landingM = 0.0f;                // metres on the ground, final (scaled and capped by the weight model; do not scale again)
    float weightSpeed = 0.0f, flightS = 0.0f, elevationDeg = 0.0f;
    bool weightCapped = false;            // the weight model's cell cap held the landing
    bool headingFromVelocity = false;     // its landing was too near to give a heading: its velocity gave it
};
bool Info(ThrowInfo* out);

// -- for the weapon-weight logic (and anything else that reacts to the right grip) -----------------------------
// Read-only, any thread.
//
// True while a throw_by_grip gesture owns the right grip: the mode is on and direct input feeds the game, a throwing
// weapon or javelin is wielded, in F4 with no menu - the frames in which this module Takes the grip. In such a
// frame a release of the right grip is either a throw (the game's own throw takes the weapon out of the hand: do not
// drop it) or the weapon being put down (drop it).
bool GripIsThrows();
// True while GripIsThrows() and the right grip is squeezed on the weapon - the gesture's own reading of the controller's value (0.60 down, 0.40 up),
// taken from the frame as the controller made it. NOT the shared block's grip: a control the gesture Takes reads as let go there (and for BodyWalk), so
// with a thrown weapon in hand that grip is always 0. Hit by touch with a thrown weapon asks this ("the weapon is in the hand").
bool GripHeld();
// GetTickCount64 (ms) at the last released throw, 0 if none. It is set in this module's frame callback, in the
// frame of the release, BEFORE the press: a listener that runs earlier in the same frame sees it one frame late -
// decide a drop on a grip release no sooner than the next frame (about 11 ms), then compare the release's
// tick with this value (a throw's tick is at or after the release's frame).
uint64_t LastThrowTickMs();
// [debug] bow_latency: the throws whose game button has been pressed so far, and the times (std::chrono::steady_clock seconds, the base of throwborn::NowS()) of the last
// one's release by the gesture and of its press. Any thread.
unsigned ShotCount();
void ShotTimes(double* releaseS, double* pressS);

// Hit by touch with a thrown weapon (vr/melee_touch.h, [gameplay] melee_touch_thrown): the touch pressed the game's own attack at a monster the blade
// met - that press IS the swing's throw. A release of the gesture before GetTickCount64 reaches `untilTickMs` is therefore not thrown (the log says so):
// one throw per swing - the release is still a throw to LastThrowTickMs (the weapon leaves the hand with the game's throw; the weight logic does not put it
// down). Only ever called after such a touch; without one the gesture is exactly as it was. Any thread; a later call replaces the earlier.
void HoldOffRelease(uint64_t untilTickMs);

}  // namespace throwwire
