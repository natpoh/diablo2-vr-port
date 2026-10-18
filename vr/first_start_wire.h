// first_start_wire.h - the first-start guide's wiring (vr/first_start.cpp): [onboarding] from d2r_vr.ini, the input layer's frame in, the
// T-pose measurement asked for, the page to show out. The flow is vr/first_start.h (pure, tested), the words vr/first_start_pages.h, the
// pictures vr/first_start_panel.cpp; xr.cpp hangs the page before the face as it hangs the measurement's prompt.
//
// [onboarding] in d2r_vr.ini:
//   show=0|1   D2R VR Authentic Settings > Body: "Show the first-start guide" - 1 = it comes up by itself while it is not done (default 1)
//   done=0|1   "First-start guide seen": written 1 by the plugin when the guide is finished or closed. Missing (an ini from before 0.171):
//              done for a player with a stored measurement, not done for anyone else
//
// F4 and native OpenXR with the plugin's own controls only. Threads: LoadSettings on vrcam's timer, the frame on the draw thread (a Listen
// callback), PanelNow and Reopen on any.
#pragma once

#include <cstdint>

#include "first_start.h"   // kPageCount

namespace firststart {

void SetLogger(void (*log)(const char*));

// [onboarding] and whether a measurement is stored ([calibration]), on every change of the file (vrcam's LoadSettings).
void LoadSettings(const wchar_t* iniPath);

// Once, at the plugin's start: the frame listener (vr/direct_input.h). It only reads the frame; it takes nothing.
void RegisterInput();

// The headset settings panel's "First-start guide" (its Body page): the guide's pages from the first, as soon as the panel is closed.
void Reopen();

// -- the page: a panel in front of the face, drawn by xr.cpp ----------------------------------------------------------------

constexpr uint32_t kPanelW = 1000, kPanelH = 720;   // pixels of one page
constexpr float kPanelWidthM = 0.85f;                // how wide it hangs, metres
constexpr float kPanelDistanceM = 1.3f;              // how far ahead of the face
constexpr float kPanelBelowM = 0.12f;                // and how far under the eyes (the world ahead stays in sight above it)

struct Panel {
    bool show = false;
    int page = 0;   // 0 .. kPageCount - 1 (vr/first_start.h)
};
Panel PanelNow();

// Paints page `page` (premultiplied RGBA, R in the low byte, kPanelW x kPanelH, top row first) with the paint kit (vr/paint_kit.h).
// False if it could not.
bool PaintPanel(int page, uint32_t* pixels);

}  // namespace firststart
