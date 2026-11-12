// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed vr/game_patches.h - commits 853607f (transparent_filter), 133465a (profiler_accounting), 30083a1 (draw_debug_names), 9b9e28e (selection_walk, shadows_once, cull_once), 1289b9a (cull volume), at tag v0.3.33-windows-preview.
// Ported to D2R VR Authentic: each patch behind its own [game_patches] key, default 0 (the game as before); present_once left out.
// This file stays MPL 2.0.
//
#pragma once
// [game_patches]: six small changes to the game's own render code, each behind its own key in [game_patches] of d2r_vr.ini and its own
// tick in D2R VR Settings (Performance > "The game's own render code"); 0 / unticked = the game's code as it is (docs/notes/game_patches.md;
// the bytes each one needs are in vr/unleashed_game_patch_signatures.h, what it writes in vr/unleashed_game_patch_plan.h):
//   transparent_filter    the transparent pass's Ghost filter tests a draw record's flags before it reads the record's constants
//   profiler_accounting   a draw no longer takes the game's GPU profiler lock to be counted
//   draw_debug_names      the game no longer gives every draw a formatted debug name (its debug event level 2 -> 1)
//   selection_walk        the debug selection tool's walk over every drawn entity left out (a call made a no-op)
//   shadows_once          the second eye pass of a pair keeps the first's sun shadow views, map and culled casters (two passes only)
//   cull_once             the first eye pass culls with one volume holding both eyes; the second, inside it, does not cull again (two passes only)
// The first four act in every draw the game makes, flat and every view; the two "once" ones only in the second pass of a pair the game draws
// itself from one game frame ("Native two passes", and the two-pass pairs of "Native one pass" while it is paused). "Native one pass" draws the
// game's frame once and replays it for the right eye, so there is no second pass for them; "Native eyes by turns" has none either. Our
// [render] shadows_once_a_pair is the one-pass counterpart of shadows_once: the replay leaves the shadow lists out for the right eye.
//
// A patch goes in, and comes out, only in BetweenFrames: on the thread that draws, before a frame's first eye pass, when the game's render
// tasks of the frame before have all ended and no thread is in the code that is changed. The game's version is never asked: the bytes at
// each place are compared with what this mod knows (D2R 3.3.93787 under D2RLoader), and a patch whose bytes differ is left out and says so
// once in the log. Rendering only: nothing of the game's state, units or rules is touched.
namespace gamepatches {

struct Wanted {
    bool transparentFilter = false;   // [game_patches] transparent_filter
    bool profilerAccounting = false;  // [game_patches] profiler_accounting
    bool drawDebugNames = false;      // [game_patches] draw_debug_names
    bool selectionWalk = false;       // [game_patches] selection_walk
    bool shadowsOnce = false;         // [game_patches] shadows_once
    bool cullOnce = false;            // [game_patches] cull_once
};

// Where the lines go (vrcam's log). Before the first Configure.
void SetLogger(void (*log)(const char*));

// From the settings, on any thread, as often as they are read. The first call says what is wanted in the log.
void Configure(const Wanted& wanted);

// On the draw thread, at the top of a frame's draw (never inside an eye pass or between the two of a pair). inWorld: the frame draws the
// world (a game is running) - a patch goes in only then; one taken out goes at once. Costs one flag test once everything wanted is in or refused.
void BetweenFrames(bool inWorld);

// Which draw of the game's frame begins now: the first or the second eye pass of a pair drawn from one game
// frame, or a draw that stands alone (a menu, a loading screen, an eye by turns, the left pass of a replayed pair, a pass that is not drawn
// twice). On the draw thread, before each of a pair's two draws and Alone after each; shadows_once and
// cull_once act in a Second pass only, and only on what the First of the same pair left. Alone at any moment
// is safe: the rest of that draw runs as the game does.
enum class Pass : unsigned char { Alone, First, Second };
void EyePass(Pass pass);

}  // namespace gamepatches
