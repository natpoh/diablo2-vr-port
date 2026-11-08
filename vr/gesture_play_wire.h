#pragma once
// gesture_play_wire.h - the Gestures tab's Play on the hero's own arms (vr/gesture_play.h): the take playing now, once, shared between the skill
// panel's frame (which starts and stops it) and the body's update (vr/body/arms.cpp, which draws the arms by it, F4). While it plays the touch's
// attacks and barrel presses are held back (meleetouch::SetPaused) and arms.cpp lets the grab, the items and the walls' hands rest: the played
// arms strike nothing, take nothing, press nothing. Any thread.

#include <cstdint>

#include "gesture_play.h"

namespace gplaywire {

// Play `take` once from now (a Play already running is replaced). `heightM`: the body the take is measured with (the engine's).
void Start(const gplay::Take& take, float heightM, uint32_t gesture, const char* name);
// Stop it now (B / Y, the window or F4 gone, the player walked away): the arms are the controllers' again at once.
void Stop(const char* why);
bool Playing();
// The skill panel's frame, every frame: a Play past its end is ended even when the body did not ask for the arms (not F4, no arms drawn).
void Tick();
// Where the log lines go.
void SetLog(void (*fn)(const char*));
// The arms this frame (vr/body/arms.cpp, the body's update): the controllers' hands in (the drawing's frame, [0] right), the drawn ones out.
// `headRoom`: where the head is in the room (null: not known) - walked kAwayM from where Play started, it stops. False: nothing playing.
bool ArmsNow(float pitchRad, const float ctl[2][3], const float ctlRot[2][4], const float ctlGrip[2], const float* headRoom, gplay::Arms* out);

}  // namespace gplaywire
