// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem, Kudlatytrue and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed PR #518 (merge 1ee027b, commits f57f881 .. 696fb19, Kudlatytrue with Bear's fixes; on by default in
// fix/campfire-on-by-default 79b921a): character creation's campfire scene drawn in stereo round the player - its own camera driven by
// the head, the heroes picked with the right hand's ray, the menu's strip and the picked hero's caption on boards in the scene, the
// fire's own sound and sparks. Taken into D2R VR Authentic as [campfire] with the same keys and defaults; laid onto our native OpenXR
// path (vr/xr.cpp OpenCampfireFrame / CampfireAim) and our camera module instead of Unleashed's transport. The rest of D2R VR
// Authentic is MIT; this file and vr/campfire_unleashed.cpp stay MPL 2.0.
// The main menu in 3D ([frontend] scene_3d): from D2R VR Unleashed PR #583 by Kudlatytrue (a645967, 863697f, 8f25454, 7bd5a0a, 35395da,
// 7b73d3f), merged by bearstonem via PR #584 (9b7abd6, with Berk's 9a276e5) - https://github.com/bearstonem/d2r-pcvr/pull/583.
// Thanks to Kudlatytrue and bearstonem for the 3D character creation and the 3D main menu.
#pragma once
#include <cstdint>
#include <string>

namespace d2rcam { struct WorldView; }

namespace campfire {

// [campfire] from d2r_vr.ini (vrcam's LoadSettings, every change).
void Load();
// The area's name as the game sets it (SkyTick, at every change): character creation is 'ui_characterselectscreen'.
void OnBiome(const std::string& biome);
// The scene drawn round the player now: [campfire] enabled, native OpenXR running, the creation scene up, nothing failed.
bool Now();
// Our camera for the scene's own (VrViewInner / VrProjInner, the game thread): false = the game's own for this pass.
bool View(const d2rcam::WorldView& in, float out[16]);
bool Proj(const d2rcam::WorldView& in, float M[16]);
// Each game frame (VrFrame): the right hand's ray -> the game's pointer, its trigger / A -> the click; the fire's click.
void Frame();
// The update thread's tick: the driven camera's switches, the fallback to the flat screen.
void Tick();
// [campfire] sparks: when the fire was clicked last (GetTickCount64, 0 = never), where it burns (world units).
uint64_t SparkAt();
bool SparksOn();
void FirePos(float out[3]);
// The fire's sound: wanted now, its loudness, and the bursts asked for (a counter).
bool FireSoundWanted(float* ambient, float* burst, uint32_t* bursts);
// The menu board ([campfire] menu_board) and the caption: what the panel code needs (vr/xr.cpp).
struct Boards {
    bool menuBoard = false, caption = false, boardClear = true;
    float menuTop = 0.78f, menuWidth = 1.1f, menuDistance = 1.0f, menuDrop = 0.9f;
    float captionRect[4] = {0.25f, 0.0f, 0.75f, 0.33f};
    float captionWidth = 3.0f, captionDistance = 6.5f, captionRise = 2.0f;
    float pointerM = 4.0f;   // the hand's beam, metres (pick_distance at the scene's scale)
    bool asMenus = false;    // [campfire] board_as_menus: the strip placed and sized as our menus' panel (xr.cpp), not menu_width / _distance / _drop
};
Boards BoardsNow();
// The interface taken out of the picture as a panel (hud SetPanelOut) for the boards: the scene up with menu_board.
bool PanelOut();
// [campfire] sky (default 0 in D2R VR Authentic, 1 in Unleashed): our sky over the scene's void. The main menu: [frontend] sky.
bool SkyWanted();
// The palette our sky takes there (sky_config SkyPaletteFor): "expansion" by the fire (Harrogath's west gate), the act's in the main menu.
const char* SkyPart();

// [frontend] scene_3d (D2R VR Authentic's key; Unleashed's [frontend] scene, from its PR #583 by Kudlatytrue, merged via #584 / 9b7abd6 -
// see vr/campfire_unleashed.cpp): the main menu's act scenes (the title screen and character select, biome act*_frontend_biome) drawn round
// you the same way as the campfire - you stand where the game's camera stands, the whole menu on a board before you.
bool Frontend();   // Now() and it is the main menu's scene (not character creation's)
bool Creation();   // Now() and it is character creation's scene (the pad kept from the game there: vr/input/pad.cpp)
// [frontend] scene_3d on and a menu scene up (the main menu's or character creation's, flat or not): the game's picture keeps the eyes' size -
// no [render] menu_wide 16:9 picture for the flat creation screen in between (each switch remade the game's swap chain and our swapchains:
// the picture froze, the 3D select came back late or not at all; Serg 2026-11-26)
bool MenusKeepSize();
// [campfire] frame_per_eye (default 1; both menu scenes): the headset gets a frame at EVERY game frame - the eye drawn in it new, the other as
// it was last drawn, at the pose it was drawn with (the runtime turns it) - not one frame for every two game frames. The scenes are bound by
// the graphics card (about 100 game frames a second at 1920x1920): a pair that waited for both eyes came 40-60 times a second in a 90 Hz
// headset, and the menu's board, the laser and the keyboard with it ("почему меню лагает", Serg 2026-11-27). 0 = both eyes of one frame, as before.
bool FramePerEye();
// [frontend] menu_16x9 (default 1): the main menu laid out in the 16:9 band (a render size in force), 0 square; the icons over its panel switch it
bool FrontMenuWide();               // the main menu's scene up and the key on
bool FrontBandAll();                // [frontend] band_all (default 1): the 16:9 band to every interface caller of the size query there
void SetFrontMenuWide(bool wide);   // an icon's click: in force at once, written to d2r_vr.ini behind it
uint32_t FaceGen();   // moves each time the scene is faced toward you (arrival, and the main menu's camera turning): its panel placed afresh
int FrontAct();    // the act whose vignette the game's camera is at (1..5; 0 none yet)
bool FrontActReady();   // ... and that act is on [frontend] acts (its scene made for the headset)
// [frontend] sky=1 in the main menu: our sky's numbers there for reshade/D2R_Frontend_Unleashed.fxh (vr/render/effects/effects.cpp sets them
// every frame; on=false: the shader's places off, the sky as anywhere else).
struct FrontSky {
    bool on = false;
    float bright = 1.0f, saturation = 1.0f;                                   // sky_bright / sky_saturation (Act 1), _N for Acts 2-5
    float blend = 0.0f, blendFar = 300.0f, blendLo = 0.03f, blendHi = 0.7f;   // sky_blend, sky_blend_far / _lo / _hi
    bool edges = true;                                                        // sky_edges
    bool cut = false;                                                         // a wall on either side
    float cutDir[2] = {0.0f, 1.0f};                                           // the game camera's right on the ground (x, z)
    float cutRight = 1e9f, cutLeft = 1e9f, voidRight = 2.0f, voidLeft = 2.0f, farRight = 1e9f, farAhead = 1e9f;
};
FrontSky FrontSkyNow();

}  // namespace campfire
