// vr/view/view_state.cpp - which view the player is in, asked by every feature: first person (FullBody), third (ThirdPerson), from
// above or the floor (Overhead, TableView, TopPersp), native's eyes (NativeView), a menu over the picture (MenuFlat, EffectsPaused),
// the interface in the picture (ClassicNow) - cut from vr/vrcam.cpp (docs/plan_modules.md M5). Read live, as before.
#include "view_state.h"
#include "../vrcam_internal.h"
#include "../config/settings.h"
#include "../xr.h"
#include "d2rcam.h"
#include "../../shared/draw_distance.h"
#include "../npc_dialog_view.h"

namespace hud { bool LayerFound(); bool FlatVrTakesMenu(); }   // (vr/hud.cpp)
namespace gamestate { bool MenuOpen(); bool NpcTalkOnly(); }                        // (vr/gamestate.cpp)

namespace vrc {

// F12 goes round: off, first person, third person, off. Third person is the
// same head-driven camera moved back ([third] distance), with the hero whole:
// his head shown, his body and arms the game's own animation.
std::atomic<int> g_view{1};   // while on: 1 first person, 2 third person
bool ThirdPerson() { return g_view.load() == 2; }
// [mode]: the view the Home page, F1..F4 or F12 picked (Settings::platform).
int ViewNow() { return g_set.platform.load() == 1 ? g_set.vrView.load() : g_set.flatView.load(); }
#if D2RVR_FIRST_PERSON
int ViewCount() { return g_set.platform.load() == 1 ? 4 : 3; }
#else
int ViewCount() { return g_set.platform.load() == 1 ? 3 : 2; }   // F4 and flat F3 are first person: coming soon
#endif
// VR first person with the body on the player's: arms on the controllers, the
// trunk held - everything that was here before the Home page.
#if D2RVR_FIRST_PERSON
bool FullBody() { return g_set.platform.load() == 1 && g_set.vrView.load() == 4; }
#else
bool FullBody() { return false; }
#endif
// VR F1, F2, F3 - the views BodyWalk's input belongs to (viewgate::Flat): the only place outside F4 where direct input acts, and only
// for the left stick's walk ([direct] walk_flat_views).
bool FlatViews() { return g_set.platform.load() == 1 && !FullBody(); }
// The VR view's number, F1 = 1 ... F4 = 4 (vrView: 3 is the table, F3), 0 in the flat game: the overlay's Settings pages are the view's (viewgate::Number).
int VrViewNow() { return g_set.platform.load() == 1 ? g_set.vrView.load() : 0; }
// First person whose hero the game animates (flat, or VR view 3): turned only
// about the vertical, mouse and W A S D, the interface in the picture.
#if D2RVR_FIRST_PERSON
bool InsideFree() { return g_enabled.load() && !ThirdPerson() && !FullBody(); }   // flat F3 only: VR has no such view since 2026-10-05
#else
bool InsideFree() { return false; }
#endif
// Native OpenXR ([openxr] on, vr/xr.cpp) draws these views as the headset's own eyes with the head as the camera: first
// person (F4) and third person (F2).
bool NativeHeadView() { return g_set.platform.load() == 1 && (FullBody() || (ThirdPerson() && g_set.vrView.load() == 2)); }
// ... and, with [openxr] views_3d (docs/plan_native_f1_f3.md), F1 from above in perspective ([top] perspective: through our
// camera, TopCamera) and F3, the game on your floor (TableCamera), with the eyes too - F1 as a window of the room, F3 as the
// diorama in it. Without views_3d, or with the game's own orthographic F1, they go to the headset as a flat picture, as ever.
bool TableView();
bool TopPersp();
int NativeMode() {
    if (g_set.platform.load() != 1 || !xr::Views3dOn()) return xr::kViewsHead;
    if (TableView()) return xr::kViewsRoom;
    if (TopPersp()) return xr::kViewsWindow;
    return xr::kViewsHead;
}
bool NativeView() { return NativeHeadView() || NativeMode() != xr::kViewsHead; }
// [openxr] menu_3d (docs/plan_laser_menus.md step 2): a menu open in first person (VR F4, the camera on)
// keeps the stereo pairs, the interface hung in front as a panel of its own (hud SetPanelOut, xr.cpp
// HangPanel) - once hud.cpp has found the interface's layer. Never F1, F2, F3: their flat picture as ever.
bool MenuAs3d() { return xr::Menu3dOn() && g_enabled.load() && FullBody() && !ThirdPerson() && hud::LayerFound(); }

// The same on FlatVR (docs/plan_laser_menus.md step 3): no native session - FlatVR holds the headset -
// and a FlatVR that hangs a menu panel and is drawing (hud FlatVrTakesMenu, its reader block). An older
// FlatVR, or none: the menus stay in the picture, on FlatVR's screen moved back, as before.
// Only in a game area, as native's pairs (d2rcam::InWorld): the title and character screens' menus are
// the picture itself - taken out there, the main menu showed no interface at all (2026-10-13).
extern std::atomic<bool> g_inWorld;
bool FlatVrMenuPanel() {
    return xr::Menu3dWanted() && !xr::On() && !xr::Running() && g_enabled.load() && FullBody() && !ThirdPerson() &&
           g_inWorld.load() && d2rcam::InWorld() && hud::LayerFound() && hud::FlatVrTakesMenu();
}
// [openxr] laser on FlatVR's menu panel: FlatVR's laser is the mouse there; the game keeps its pad layout
// under it (vr/panel_input.cpp) while FlatVR says it is (hud FlatVrLaserState, every present).
bool FlatVrLaserWanted() {
    int hand = 1;
    return xr::LaserWanted(&hand) && FlatVrMenuPanel();
}

// The last pair's: a menu open, and out of the picture as a panel (the world in stereo behind it). Any thread.
std::atomic<bool> g_menu3dNow{false};
// A menu open that the picture shows (the flat one): the effects that lie over the whole picture stay off.
// [hud] npc_dialog_keep_view (vr/npc_dialog_view.h): an NPC's talk the only menu open in native VR F1 - F3 - the pair kept, its box in the picture.
bool NpcDialogKeepsView() {
    npcview::Where w;
    w.on = g_set.npcDialogKeepView.load();
    w.vr = g_set.platform.load() == 1;
    w.firstPerson = FullBody();
    w.nativeView = NativeView();
    w.inWorld = g_inWorld.load();
    w.talkOnly = w.on && gamestate::NpcTalkOnly();
    return npcview::KeepsView(w);
}
bool MenuFlat() { return gamestate::MenuOpen() && !g_menu3dNow.load() && !NpcDialogKeepsView(); }
// The sky, the fog and the cave ceiling stop for a menu in VR, where the flat menu fills the picture; on a monitor the world stays behind
// the panel, and they stay with it (from sajmonsoft's flat fork: opening a chest, a dialogue or the inventory turned them off).
bool EffectsPaused() { return MenuFlat() && g_set.platform.load() != 0; }

// VR view 5: the game laid on the room's floor and looked at from above - our
// own camera where the head is in the room ([table] hero_cm, TableCamera), the
// hero whole as the game animates him, the void black for the headset to key.
bool TableView() { return g_set.platform.load() == 1 && g_set.vrView.load() == 3; }
// VR view 1 with [top] perspective: the game's view from above, but through our
// camera - the FlatVR screen's field of view and real stereo (TopCamera).
// Native OpenXR with views_3d draws F1 in true perspective whatever [top] perspective says: a window onto the game needs our camera.
bool TopPersp() { return g_set.platform.load() == 1 && g_set.vrView.load() == 1 && (g_set.topPersp.load() || xr::Views3dOn()); }

bool Overhead() { return TableView() || TopPersp(); }
// VR F3 with [table] full_width (Serg 2026-11-08, after a player's wish for a sky in F3: "не обрезать квадратом режим ф3, а делать полную
// ширину и рисовать купол и туман"): the board's edge and the keyed void are gone - the world as far as it is drawn, with the game's own sky
// dome ([sky] game_sky) and the game's own fog ([fog] game_fog) round it, as in F4. Only the game's objects and its fog pass: our post pass's
// sky, fog and blur stay out, and no fog tweak. Unticked: the board as before.
bool TableFull() { return TableView() && g_set.tableFull.load(); }
// VR F1 through our camera (Real geometry) with [top] sky / [top] fog (Serg 2026-11-13: "F1 also needs the fog and sky tick"): the game's own
// sky dome ([sky] game_sky) / the game's own fog pass ([fog] game_fog) round the world, as F3 full width has them - each its own tick. Only the
// game's objects and its fog pass: our post pass's sky and fog, the dome fixes and the fog tweaks stay out there, with no fallback of ours. The
// game's own flat F1 (Real geometry off) is the game's camera: neither is drawn there. Unticked: no sky, no fog from above, as before.
bool TopSky() { return TopPersp() && g_set.topSky.load(); }
bool TopFog() { return TopPersp() && g_set.topFog.load(); }
// From above with the game's own dome / fog round the world: VR F3 full width (both) or VR F1 with its tick
bool GameSkyOverhead() { return TableFull() || TopSky(); }
bool GameFogOverhead() { return TableFull() || TopFog(); }
// F3's own Draw distance ([table] draw_distance) while F3 is the view; nullptr: the shared keys (every other view, or 0)
const drawdistance::Stop* ViewDistanceStop() { return TableView() ? drawdistance::ViewStop(g_set.tableDistance.load()) : nullptr; }
int RingsNow() { const drawdistance::Stop* s = ViewDistanceStop(); return s ? drawdistance::FromAboveRings(*s) : g_set.rings.load(); }   // (F3: the tiles seen from above)
float ModelRadiusNow() { const drawdistance::Stop* s = ViewDistanceStop(); return s ? s->modelRadius : g_set.modelRadius.load(); }
float GameSkyRadiusNow() { const drawdistance::Stop* s = ViewDistanceStop(); return s ? s->skyRadius : g_set.gameSkyRadius.load(); }
#if D2RVR_FIRST_PERSON
int ArmsMode() { return FullBody() ? 2 : g_set.armsMode.load() == 1 ? 1 : 0; }   // 1 = the test pose, kept for debugging
#else
int ArmsMode() { return 0; }
#endif
// Mouse look: the flat views and VR view 3 are played with the mouse; the rest as the ini says.
bool MouseLookForView() {
    if (g_set.platform.load() == 0) return ViewNow() != 1;
    if (Overhead()) return false;   // from above and the floor: the head and the pad
    // VR F2 with vr_keys_walk is flat's F2: it starts with mouse look (F9 frees the pointer).
    if (ViewNow() == 2 && g_set.vrKeyWalk.load()) return true;
    return g_set.mouseLook.load();
}
// The interface in the picture (not taken out for FlatVR), per view.
bool ClassicNow() {
    if (g_set.platform.load() == 0) return true;   // no FlatVR to take it
    if (!g_enabled.load() || TopPersp()) return g_set.classicTop.load() == 0;
    if (TableView()) return g_set.classicFloor.load() == 0;   // before ThirdPerson: ApplyView runs the floor as view 2
    if (ThirdPerson()) return g_set.classicThird.load() == 0;
    return !FullBody();
}

}  // namespace vrc
