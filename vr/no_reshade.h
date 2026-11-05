// no_reshade.h - [render] no_reshade's decisions (docs/plan_no_reshade.md, step 3), apart from the plugin so tools/no_reshade_test can check them:
// whether ReShade64.dll is loaded at the plugin's start, whether the mode in force is the one this works for, and what the log says is off.
#pragma once

namespace nors {

// Since 0.203 (Serg, 2026-11-05: "from the new build don't use ReShade at all - we take the depth map without it") there is no ReShade path:
// ReShade64.dll is never loaded, a ReShade in the game is never joined, and [render] no_reshade in an old ini means nothing. Crash reports
// 231 / 232 / 251: our D2R_DepthFog.fx defines made ReShade rebuild the effect synchronously in its present - minutes on an i7-4790, the
// game killed as hung - also through a player's own dxgi.dll ReShade, which no_reshade=1 used to join anyway.
enum class Start {
    None,            // the plugin's own D3D12 hooks (vr/native_d3d12.h), nothing else
    BesidePlayers,   // the same, with the player's own ReShade beside the game as dxgi.dll: the game loads it itself, it is left exactly as it is
                     // (never renamed, moved or deleted) and runs its own effects; nothing of D2R VR goes through it
};
inline Start Decide(bool dxgiBesideGame) { return dxgiBesideGame ? Start::BesidePlayers : Start::None; }

// The modes made for it: the flat game ([mode] platform=0: one picture on the monitor - the plugin's own hooks and, with native_effects, its own
// runtime for the effect cover all it draws there), and VR with native OpenXR, the eyes by turns (afr=1, pair_per_tick=0) with the other eye
// made (turn_warp=1). Any other VR mode has no 3D in one pass (one pass two pictures needs ReShade's replay: the pair is drawn in two passes)
// - said in the log; FlatVR gets its picture and depth from the plugin since [render] flatvr_feed (vr/flatvr_feed.h; before it FlatVR took them
// from ReShade's add-on alone and showed nothing). (Serg, 2026-10-31: "why does the 2D mode start ReShade" - in flat there was not even the switch.)
struct Mode {
    bool vr = false, openxr = false, byTurns = false, pairPerTick = false, turnWarp = false;
};
inline bool FitsVr(const Mode& m) { return m.vr && m.openxr && m.byTurns && !m.pairPerTick && m.turnWarp; }
inline bool Fits(const Mode& m) { return !m.vr || FitsVr(m); }

// What the flat game goes without with it: ReShade itself - its own overlay (the Home key), its screenshots and any effect of the player's own
// in its presets; what D2R VR draws in flat (the sky, the fog, the cave ceiling, the crosshair, the interface's pieces) is drawn by the plugin.
inline constexpr const char* kOffFlat = "ReShade's own overlay (the Home key), its screenshots and any effect of your own in its presets; D2R VR's "
                                        "sky, fog, cave ceiling and crosshair only with [render] native_effects";
// What is off with it, as the log says it once at the start. With [render] native_effects (the plugin's own runtime for D2R_DepthFog.fx,
// vr/fx_native.h) the fog, the sky, the cave ceiling and the overlays drawn apart come back; without it they are off too.
inline constexpr const char* kOff =
    "one pass two pictures (the replay) and the menus as a panel (FlatVR's picture and depth come from the plugin: [render] flatvr_feed); the fog, the sky, the cave ceiling and "
    "the overlays apart from the picture the eye made is moved from (labels laid back, wrist orbs, HUD shift, low-life vignette, bow orbs) "
    "only with [render] native_effects";

}  // namespace nors
