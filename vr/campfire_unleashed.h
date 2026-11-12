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
};
Boards BoardsNow();
// The interface taken out of the picture as a panel (hud SetPanelOut) for the boards: the scene up with menu_board.
bool PanelOut();
// [campfire] sky (default 0 in D2R VR Authentic, 1 in Unleashed): our sky over the scene's void.
bool SkyWanted();

}  // namespace campfire
