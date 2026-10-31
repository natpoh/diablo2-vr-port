// no_reshade.h - [render] no_reshade's decisions (docs/plan_no_reshade.md, step 3), apart from the plugin so tools/no_reshade_test can check them:
// whether ReShade64.dll is loaded at the plugin's start, whether the mode in force is the one this works for, and what the log says is off.
#pragma once

namespace nors {

enum class Start {
    ReShade,      // [render] no_reshade=0: ReShade as before (the default)
    PlayerDxgi,   // no_reshade=1, but the player's own ReShade is beside the game as dxgi.dll: the game loads it itself - joined as before
    None,         // no_reshade=1: ReShade64.dll not loaded, the plugin's own D3D12 hooks (vr/native_d3d12.h)
};
inline Start Decide(bool iniOn, bool dxgiBesideGame) {
    if (!iniOn) return Start::ReShade;
    return dxgiBesideGame ? Start::PlayerDxgi : Start::None;
}

// The modes made for it: the flat game ([mode] platform=0: one picture on the monitor - the plugin's own hooks and, with native_effects, its own
// runtime for the effect cover all it draws there), and VR with native OpenXR, the eyes by turns (afr=1, pair_per_tick=0) with the other eye
// made (turn_warp=1). Any other VR mode has no picture in the headset (FlatVR takes it from ReShade) or no 3D (one pass two pictures needs
// ReShade's replay) - said in the log. (Serg, 2026-10-31: "why does the 2D mode start ReShade" - in flat there was not even the switch.)
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
    "one pass two pictures (the replay), FlatVR's depth and the menus as a panel; the fog, the sky, the cave ceiling and "
    "the overlays apart from the picture the eye made is moved from (labels laid back, wrist orbs, HUD shift, low-life vignette, bow orbs) "
    "only with [render] native_effects";

}  // namespace nors
