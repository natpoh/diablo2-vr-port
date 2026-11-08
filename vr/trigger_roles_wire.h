#pragma once
// trigger_roles_wire.h - [gestures] trigger_right / trigger_left (vr/trigger_roles.h): each trigger's role while the gestures are in use - as before,
// Glyphs (draw + cast the armed skill), Shift, or an Action - on direct input's per-frame callback, F4 only, in play with no panel or game menu up.
// It takes a trigger with a role from the layout, holds the armed slot's / Shift's pad buttons the game reads in its own poll, does an Action the
// game's own way (vr/gesture_actions.h), and tells the gesture engine which triggers are pens now. Both keys 0: nothing is registered or taken.

#include <cstdint>

#include "trigger_roles.h"

namespace trigroleswire {

// The settings load (and the Gestures tab's Triggers screen, live): the roles per hand (0 left, 1 right) and an Action role's action id.
// `castHold`: [gestures] glyph_cast_hold - false the Release rule (a glyph trigger's cast at the release, the default), true the Hold rule.
void Configure(int roleLeft, int roleRight, const char* actionLeft, const char* actionRight, bool castHold = false);
// The gesture engine recognised a gesture now (`time`: direct input's frame clock): a glyph trigger's release then casts nothing.
void Recognised(double time);
// Either trigger has a role (not "as before"): the engine's pens are the Glyphs triggers' (Pen), not [gestures] engine_pen.
bool Active();
trigroles::Role RoleOf(int hand);
const char* ActionOf(int hand);
// The engine's pen for this hand now (a Glyphs trigger's press still a stroke). Any thread.
bool Pen(int hand);
// The hand whose Glyphs trigger last drew a stroke (0 left, 1 right; -1 none) and when (GetTickCount64): an either-hand glyph's drawing hand.
int LastStrokeHand(uint64_t* at = nullptr);
// A glyph trigger held still with a skill armed (the Release rule's aim): its hand (0 left, 1 right) and the skill; false none. Any thread.
bool Aiming(int* hand, int32_t* skill);
// The hand a cast is aimed along for now (0 left, 1 right): a glyph trigger's while it aims and a moment after its release; -1 none.
int CastHand();
// A Select glyph armed this hand's slot (skilllinkwire's): its trigger casts it from now on.
void SetArmed(int hand, bool armed);

}  // namespace trigroleswire
