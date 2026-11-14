// vr/view/view_state.h - which view the player is in (vr/view/view_state.cpp, docs/plan_modules.md M5). Every answer is read live from
// the settings and the state it names (one round may see two views - the tightening step T-a gives the round one frame::View).
#pragma once

#include <atomic>

namespace drawdistance { struct Stop; }

namespace vrc {

extern std::atomic<int> g_view;         // while on: 1 first person, 2 third person (F12 goes round)
extern std::atomic<bool> g_menu3dNow;   // the last pair's: a menu open, and out of the picture as a panel

bool ThirdPerson();
int ViewNow();            // [mode]: the view the Home page, F1..F4 or F12 picked
int ViewCount();
bool FullBody();          // VR first person with the body on the player's (F4)
bool FlatViews();         // VR F1, F2, F3
int VrViewNow();          // the VR view, 1 = F1 ... 4 = F4; 0 not VR (viewgate::Number)
bool InsideFree();        // flat F3
bool NativeHeadView();    // native OpenXR draws it with the head as the camera (F4, F2)
int NativeMode();         // xr::kViewsHead / kViewsWindow / kViewsRoom
bool NativeView();
bool MenuAs3d();          // [openxr] menu_3d
bool FlatVrMenuPanel();   // ... on FlatVR
bool FlatVrLaserWanted();
bool NpcDialogKeepsView();   // [hud] npc_dialog_keep_view: an NPC's talk keeps the pair in native VR F1 - F3 (vr/npc_dialog_view.h)
bool MenuFlat();          // a menu open that the picture shows
bool EffectsPaused();     // the sky, the fog and the cave ceiling stop for it in VR
bool TableView();         // VR F3, the game on the floor
bool TopPersp();          // VR F1 through our camera
bool Overhead();          // TableView or TopPersp
bool TableFull();         // VR F3 with [table] full_width: no board edge, the game's own dome and fog
bool TopSky();            // VR F1 Real geometry with [top] sky: the game's own dome round the world
bool TopFog();            // VR F1 Real geometry with [top] fog: the game's own fog pass
bool GameSkyOverhead();   // TableFull or TopSky: from above with the game's dome, nothing of our post pass's sky
bool GameFogOverhead();   // TableFull or TopFog: from above with the game's fog, nothing of ours, no fog tweak
int RingsNow();           // [render] rings, or F3's own Draw distance stop ([table] draw_distance) while F3 is the view
float ModelRadiusNow();   // [render] model_radius, likewise
float GameSkyRadiusNow(); // [sky] game_sky_radius_m in world units, likewise
const drawdistance::Stop* ViewDistanceStop();   // F3's own Draw distance stop while F3 is the view, nullptr: the shared keys
int ArmsMode();
bool MouseLookForView();
bool ClassicNow();        // the interface in the picture (not taken out for FlatVR)

}  // namespace vrc
