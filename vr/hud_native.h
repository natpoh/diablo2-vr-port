#pragma once
// What native OpenXR (xr.cpp) takes from hud.cpp to hang the game's interface in the
// room itself, as FlatVR did from BodyWalkVR_GameHud: the toolbar's and the map's
// textures (R16G16B16A16_FLOAT, premultiplied, alpha = transmittance, left in the
// shader-resource state on the game's queue), whether each is out of the picture now,
// the look ([hud] in d2r_vr.ini) and the hero's forearm the toolbar lies on. And with
// [openxr] menu_3d the whole interface while a menu is open (kNativePanel, SetPanelOut).

#include <cstdint>
#include "game_hud_shared.h"

struct ID3D12Resource;

namespace hud {

enum { kNativeBar = 0, kNativeMap = 1, kNativePanel = 2, kNativePieces = 3 };
struct NativePiece {
    ID3D12Resource* tex = nullptr;
    uint32_t w = 0, h = 0;
    bool visible = false;   // taken out of the picture by the last interface pass, and that pass is fresh
    float stretch = 1.0f;   // the map: drawn this much wider than its own shape (F4 in a picture of another shape, vr/minimap_box.h Stretch)
    float heroU = 0.5f, heroV = 0.5f;   // kNativePanel while big: the hero on the whole-screen map - the canvas's middle (uv of the layer)
    uint32_t mapRows = 0;   // kNativePanel while the map is big ([hud] map_click_big): its rows from the top that are the big map - above the toolbar's strip (0: all)
};
// False when nothing has been published yet.
bool NativeHud(NativePiece out[kNativePieces], FlatVRGameHudLook* look, FlatVRGameHudPose* barPose);
// [hud] map_rotate / map_compass (vr/map_turn.h): the way the head looks on the ground (mapdir::OnGround: the game screen's right, its up),
// held while it looks down at the hand; false when vrcam has sent none for half a second.
bool MapTurnHeading(float* x, float* y);
// [hud] map_click_big (vr/map_laser.h): the map made big - the whole interface goes out as kNativePanel (the game's whole-screen map in it),
// for xr.cpp's big map, not a menu's panel. xr.cpp sets it.
void SetMapBig(bool big);
bool MapBig();
// [hud] monster_bar_room (vr/plate_world.h): the monster's plate - the game's own name and health bar - cut out of the interface for the room.
// vrcam's tick says whether it is to be cut and where the plate is (its rect in the layout's units and the interface scale: plateworld::BoxOf makes the
// box); xr.cpp takes the texture. `encoded`: [hud] colors_as_monitor.
void SetPlateCut(bool want, const int32_t layout[4], float scale);
bool PlatePiece(NativePiece* out, bool* encoded);
// [hud] notice_card (vr/notice_card.h): the game's quest / level-up notice cut out of the interface for a card in the room. vrcam's tick says whether
// to cut (the card in force, the game's panel open), the panel's rect as the widget service gave it (null: the layout's) and the interface scale.
void SetNoticeCut(bool want, const int32_t widget[4], float scale);
bool NoticePiece(NativePiece* out, bool* encoded);
// [openxr] menu_3d: a menu is open in first person and goes to the headset as a panel of its own -
// the whole interface copied out (kNativePanel) and the layer emptied, the world drawn without it.
// vrcam, on the draw thread at each pair's start.
void SetPanelOut(bool out);
// FlatVR's side of it: its reader block looked at (vrcam's timer), whether a FlatVR that hangs a menu
// panel is drawing now, and where it is to hang it (the native panel's width and distance).
void PollMenuReader();
bool FlatVrTakesMenu();
// laser: 0 none, 1 the left hand's, 2 the right hand's (FlatVR's laser the mouse on it); window: the game's.
void SetMenuPlace(float widthM, float distanceM, float belowM, uint32_t laser, uint32_t window);
// FlatVR's laser on the menu panel now (its reader block): 0 idle, 1 the game's mouse, 2 parked (the pad's).
int FlatVrLaserState();

}  // namespace hud
