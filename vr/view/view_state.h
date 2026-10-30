// vr/view/view_state.h - which view the player is in (vr/view/view_state.cpp, docs/plan_modules.md M5). Every answer is read live from
// the settings and the state it names (one round may see two views - the tightening step T-a gives the round one frame::View).
#pragma once

#include <atomic>

namespace vrc {

extern std::atomic<int> g_view;         // while on: 1 first person, 2 third person (F12 goes round)
extern std::atomic<bool> g_menu3dNow;   // the last pair's: a menu open, and out of the picture as a panel

bool ThirdPerson();
int ViewNow();            // [mode]: the view the Home page, F1..F4 or F12 picked
int ViewCount();
bool FullBody();          // VR first person with the body on the player's (F4)
bool FlatViews();         // VR F1, F2, F3
bool InsideFree();        // flat F3
bool NativeHeadView();    // native OpenXR draws it with the head as the camera (F4, F2)
int NativeMode();         // xr::kViewsHead / kViewsWindow / kViewsRoom
bool NativeView();
bool MenuAs3d();          // [openxr] menu_3d
bool FlatVrMenuPanel();   // ... on FlatVR
bool FlatVrLaserWanted();
bool MenuFlat();          // a menu open that the picture shows
bool EffectsPaused();     // the sky, the fog and the cave ceiling stop for it in VR
bool TableView();         // VR F3, the game on the floor
bool TopPersp();          // VR F1 through our camera
bool Overhead();          // TableView or TopPersp
int ArmsMode();
bool MouseLookForView();
bool ClassicNow();        // the interface in the picture (not taken out for FlatVR)

}  // namespace vrc
