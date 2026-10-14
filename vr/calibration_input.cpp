// calibration_input.cpp - the measurement's one wiring to the input layer (vr/direct_input.h, API 1): while it runs - and
// until the player has let go once afterwards, a cancel with both triggers still down included - both triggers and the right
// hand's B (the cancel) are taken from the layout, so the T-pose is not an attack, a skill slot or a menu command. In native
// OpenXR with direct_input on AND with BodyWalk's input (a take reads as let go in what BodyWalk is sent too).
//
// Without vr/direct_input.h (the tree before the input layer) RegisterInput does nothing and the controls are the layout's.

#include "calibration.h"

#if __has_include("direct_input.h")

#include "body_calibration.h"
#include "direct_input.h"

namespace calib {
namespace {

bodycal::ReleaseGate g_gate;   // used on the draw thread only (a Listen callback)

// Once per headset frame, after calib::Feed (xr.cpp Publish) and before the layout is made from the frame. The Frame's
// triggers and buttons are the player's hands as they are, before any take.
void OnFrame(const directinput::Frame& f, void*) {
    const bool b = (f.hand[1].buttons & directinput::kSecondary) != 0;
    if (!g_gate.Step(OwnsTriggers(), f.hand[0].trigger, f.hand[1].trigger, b)) return;
    for (int hand = 0; hand < 2; ++hand)
        directinput::Take(hand, directinput::kTrigger | directinput::kTakeTrigger | (hand == 1 ? directinput::kSecondary : 0u),
                          directinput::kSrcCalibration);
}

}  // namespace

void RegisterInput() { directinput::Listen(&OnFrame, nullptr); }

}  // namespace calib

#else

namespace calib {
void RegisterInput() {}
}  // namespace calib

#endif
