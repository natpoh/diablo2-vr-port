// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed: 7c642e33d2c8c04043c6f2ac47e7169b54e5693c "Apply foveated rendering to the visible shading pass and add strength"
//   (steamvr/foveation.cpp, foveation.h, vrs_compat.h) - rewritten for D2R VR Authentic's own D3D12 hooks; published before Unleashed moved
//   to the MPL (2026-10-28), so also available under its MIT notice. Kept MPL here, as the other ports. The rest of D2R VR Authentic is MIT.
// foveation.h - [render] foveated: fixed foveated rendering with D3D12 variable rate shading (tier 2), from D2R VR Unleashed (bearstonem,
// steamvr/foveation.cpp; the tile rule and the strength levels in shared/unleashed_foveation.h, MPL 2.0).
//
// The game's opaque world draws (a pixel shader, depth tested, no blending, one sample, into a full-size colour + depth pair: Bear's rule)
// shade one pixel in 2x2 - or 4x4 - outside an ellipse round each eye's lens centre; inside it every pixel is shaded as before. The
// interface, labels, transparency, effects, shadows, our own passes and everything outside the native OpenXR first-person pair (F4) stay
// whole. Rendering only: nothing of the game's state is touched.
//
// How each eye gets its own centre in every stereo mode: one "live" shading-rate image is bound in the game's lists; before each eye's pass
// is submitted (vrcam's SetEye: both eyes a frame, eyes by turns, and before the replay runs the left eye's lists again as the right eye)
// a copy on the game's queue puts that eye's mask into it. The masks come from the eye's frustum as drawn (xr::PairLens): Bear's ellipse,
// measured in the part of the picture the headset shows, and - [render] foveated_unseen - the part it never shows at the coarsest rate.
//
// Only without ReShade (the plugin's own D3D12 hooks, vr/native_d3d12.h) and on a GPU with VRS tier 2; otherwise the picture stays as it is
// and the log says why. The hooks go in at the game's start when [render] foveated is on then; it is switched on and off live after that
// (and [render] foveated_ab alternates it by itself to measure frames/s).
#pragma once

#include <cstdint>

struct ID3D12Device;
namespace natived3d { struct Sinks; }

namespace foveation {

using LogFn = void (*)(const char*);

// At the plugin's load, before natived3d::Install, when [render] foveated is on: the hooks' sinks filled in.
void FillSinks(natived3d::Sinks& k, LogFn log);
void SetLog(LogFn log);   // the log for the lines said without the hooks too (always, at the plugin's load)
// tools/foveation_native_test: a pixel shader of its own (its FNV-1a 64) taken as one of the game's heavy passes (fullscreenpasses::Kind)
void TestKind(uint64_t psHash, int kind);
bool Hooked();   // FillSinks ran (the hooks are in from this start)
// The hooks are in and the GPU has variable rate shading tier 2: [render] foveated works live now ([autotune]'s ladder rung)
bool Usable();
// Variable rate shading tier 2 on this graphics card: 1 yes, 0 no, -1 not known yet (asked at the first present, hooked or not)
int VrsTier2();
// The plugin's load asked D3D12 (shared/vrs_probe.h) and no hardware adapter has it: no hooks went in (FillSinks not called), and this
// says so for VrsTier2 from the start - [render] foveated / grass_lighting_coarse are a true no-op on such a card
void NoteNoVrs();

// [render] foveated, foveated_strength (0 Low .. 3 Maximum, Bear's levels), foveated_radius (0: the level's; else 0.30 - 0.85),
// foveated_unseen, foveated_ab (seconds, 0 off) - vrcam's settings load, live.
struct Config {
    bool on = false;
    int strength = 1;
    float radius = 0.0f;
    bool unseen = true;
    int abSeconds = 0;
    bool gaze = true;   // foveated_gaze: the sharp area follows the eyes where the runtime gives their gaze (asked for at the session's start)
    int fullscreen = 0;    // foveated_fullscreen: 0 Bear's rule alone, 1 + the grass lighting and the reflections passes, 2 + the hair (vr/fullscreen_passes.h)
    int grassCoarse = 0;   // grass_lighting_coarse: 0 as the game, 1 the grass lighting shaded 2x1 everywhere, 2 2x2 (works without foveated too)
};
void Configure(const Config& c);

// Each present of the game: its device and the picture's size (the eyes' picture: what a full-size scene target is measured against).
void NoteScreen(ID3D12Device* dev, uint32_t w, uint32_t h);

// The draw thread, before a pass of the game's is drawn (or replayed) as eye 0 / 1 of a native OpenXR first-person pair (`pair`); outside
// one (`pair` false: a flat picture, a menu, F1 - F3) the rate image is not bound. EndWorld: after the pair's passes.
void BeginEye(int eye, bool pair);
void EndWorld();

}  // namespace foveation
