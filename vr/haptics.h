#pragma once
// Controller haptics in native OpenXR ([openxr] haptics, docs: the note of the port-haptics tab).
// Output only: an event becomes a pulse in the right hand; nothing the game does is changed.
//
// For the code that wants a pulse:
//     haptics::Play(haptics::Event::Interact);                  // a named event, its own hand
//     haptics::Play(haptics::Event::Drink, haptics::kLeft);      // ... or in this hand
//     haptics::Pulse(haptics::kRight, 0.4f, 40);                 // your own: amplitude 0..1, duration in ms
//     haptics::BowDraw(draw);                                    // every frame while a string is drawn (0..1; 0 = none)
// All of them: any thread, lock-free, cheap, and silent (a load and a return) unless [openxr] on
// and haptics are ticked. Hands are haptics::kLeft = 0 and kRight = 1 (XrInput's order, not
// skel::Input's - haptics::FromSkel(side) turns that). The pulse is played by the next headset
// frame, through the output action vr/xr.cpp makes; one that finds the session without input focus
// is dropped, not kept.

#include "haptics_pattern.h"

// F4 ONLY (vr/view_gate.h): no pulse is posted, kept or played in another view; what waits when the view changes is dropped.
namespace haptics {

void SetLogger(void (*log)(const char*));

// xr.cpp, on every read of the ini: [openxr] on && haptics, and haptics_scale.
void Configure(bool on, float scale);
bool Enabled();

// The motor the controllers have (haptics_pattern.h Motor): xr.cpp says which headset the runtime named (once per session) and what the ini's
// [openxr] haptics_motor chose (0 by the headset, 1 plain, 2 weak; live). The callers whose gentle pulses a weak motor does not show (the low-life
// beat) lift them through haptics::ForMotor(CurrentMotor(), ...); every other pulse is what it was.
void SetHeadset(const char* systemName);
void SetMotorChoice(int choice);
Motor CurrentMotor();

void Pulse(int hand, float amplitude, int durationMs);
void Play(Event e, int hand = -1);
// The hand a landed blow buzzes: kLeft / kRight, -1 = the pattern's own (the right hand). vrcam registers it - the hand that holds the weapon (a weapon drawn into the left
// hand buzzes the left), the shield's arm for a bash. Read on the game's server thread: atomics only. Unregistered: the pattern's own.
using HitHandFn = int (*)();
void SetHitHandSource(HitHandFn fn);
// The string hand draws, the other holds the bow. Call every frame the bow is drawn with the
// draw 0..1 (0 when not drawn) from one thread; ticks come by themselves. BowNock / BowRelease are
// Play(...) and re-arm the count.
void BowDraw(float draw, int stringHand = kRight);

// directinput::HapticSink's shape (vr/direct_input.h, the direct-input tab): the pulse the gesture code
// asks for through directinput::Haptic lands here. haptics.cpp registers it by itself where that header
// exists (`__has_include`), so the two branches need no line to meet. The frequency is the runtime's.
void Sink(int hand, float amplitude, float durationMs, float frequencyHz);

// xr.cpp, once per frame after the actions are synced: plays what waits through `apply` (false from
// it = the runtime refused). canPlay false (no session focus, no output action): what waits is dropped.
using ApplyFn = bool (*)(int hand, float amplitude, int durationMs);
void Flush(uint64_t nowMs, bool canPlay, ApplyFn apply);

// vrcam, twice a second: the observer of a landed hit, once [openxr] haptics asks for it.
void Install();

}   // namespace haptics
