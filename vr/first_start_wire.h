// first_start_wire.h - the first-start guide's wiring (vr/first_start.cpp): [onboarding] from d2r_vr.ini, the input layer's frame in, the
// request to the settings window out, [onboarding] done written when the window says the guide is over. The flow is vr/first_start.h (pure,
// tested), the words vr/first_start_pages.h; the page itself is the headset settings' Guide tab (vr/hset_scene.h), in the skill panel's
// window (vr/wheel_controller.h, vr/skill_wheel.cpp) or the settings panel of its own (vr/hset_panel.h, vr/xr_headset_settings.h).
//
// [onboarding] in d2r_vr.ini:
//   show=0|1   D2R VR Authentic Settings > Body: "Show the first-start guide" - 1 = it comes up by itself while it is not done (default 1)
//   done=0|1   "First-start guide seen": written 1 by the plugin when the guide is finished or closed. Missing (an ini from before 0.171):
//              done for a player with a stored measurement, not done for anyone else
//
// F4 and native OpenXR with the plugin's own controls only. Threads: LoadSettings on vrcam's timer, the frame on the draw thread (a Listen
// callback), the rest on the draw thread too (the windows' own Listen callbacks) or any.
#pragma once

#include <cstdint>

#include "first_start.h"   // kPageCount

namespace firststart {

void SetLogger(void (*log)(const char*));

// [onboarding] and whether a measurement is stored ([calibration]), on every change of the file (vrcam's LoadSettings).
void LoadSettings(const wchar_t* iniPath);

// Once, at the plugin's start: the frame listener (vr/direct_input.h). It only reads the frame; it takes nothing.
void RegisterInput();

// -- the window's side ---------------------------------------------------------------------------------------------------------------

// The guide asks the window to come up on Settings > Guide: the page it starts on (kOfferPage, or 1), -1 none. It stays until the window has
// come up (OpenRequestTaken). Asked every frame by the window that is the host (the skill panel's Settings tab, or the settings panel of its own).
int OpenRequest();
void OpenRequestTaken();

// The guide page was on show and the window closed (finished = Done was pressed): the guide is over, [onboarding] done=1 is written.
// Whatever opened it - the first start, the Body page's button, the Guide tab.
void PanelEnded(bool finished);

// The settings panel of its own (vr/xr_headset_settings.h) can host the guide while `on` (the skill panel's Settings tab is not there), and
// `open`: it is up now. (The skill panel's window says the same by directinput::SetUiUp.)
void SetStandalone(bool on, bool open);

}  // namespace firststart
