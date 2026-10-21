#pragma once
// [render] fog_in_frame: our distance fog drawn inside the game's frame, before its transparent resolve
// (docs/game_render_sky_fog.md, step 1). Fire, spells and smoke are then laid over the fogged scene by the game itself,
// instead of our post pass fogging them by the depth of the wall behind them.
//
// The game composes its frame as: the lit opaque scene S; the transparent effects into weighted-blended targets (an
// accumulation and a revealage) and the additive ones into a target of their own A; then ps_transparent_resolve writes
// R = a S + (1 - a) T + A into the frame's target. At that draw (the pipeline found by its pixel shader's size and hash
// at init_pipeline, as light_bounds and fog_spare find theirs) the game's draw is replaced by three steps of ours,
// recorded into the game's own list:
//   1. the game's pipeline with our pixel shader: S alone, into a copy of ours (the game's state, its stencil test, no
//      stencil written);
//   2. our compute shader: the fog of each pixel by the game's own depth (live, this eye's), from our copy into a
//      texture of ours, copied into the frame's target - where the game's draw reaches (viewport, scissor, stencil);
//   3. the game's pipeline with our second pixel shader and blending: R = (1 - a) T + A + R x a, the game's own stencil
//      state - what its draw would have written, with S fogged.
// Then the game's own pipeline is bound again.
//
// Not all of the game's fire goes through its transparent targets: some (a torch's flame among it) is drawn blended
// straight into the lit scene, before the resolve - fogged with the scene by the depth of what lies behind it (test-0176:
// the flame pale over the fence, bright in the gaps, its bloom halo gone). Every such draw into the scene's target (the
// forward pass's colour target: R11G11B10 with its R8G8B8A8 material target and the resolve's depth) whose blend is
// linear in the scene is drawn twice instead, with the game's own state: into E (cleared to 0 with the scene) with the
// game's own pipeline, and into K (cleared to 1) with a copy whose source factor is zero - then the scene is
// S0 K + E exactly, and the compute step fogs S0 alone: fog(S0) K + E (fog_in_frame_math.h BlendRt). What blends by the
// scene's own colour, writes depth, the surface's other targets or a UAV stays in the scene, fogged; the log names each.
//
// The compute step has its own root signature: the game's graphics root arguments are never touched; its descriptor
// heaps and tables (changed by our table) are set again as the game had them (ReShade raises an event for every table
// the game binds; its root CBVs it reports only when it can resolve the address, so those are better left alone). The
// game's own compute bindings in the resolve's list (its light culling runs there) are given back after ours: the
// root signature, tables, constants and root CBV / SRV / UAV addresses ReShade reported (test-0176: refusing that list
// meant the fog in the frame never ran - every picture of 126 - 133 was the post pass's fog). Any doubt - a target, a
// state, a format, a size not as expected, a compute binding not reported fully - and the game's draw goes on alone, as
// before, with a line in the log.
//
// The right eye: the replay sends the left pass's lists again; our steps are in them, read each eye's own scene, depth
// and effects, and take nothing from the CPU at the time (root constants, recorded into the list) - no ReShade effect
// constants, which ReShade writes in place once per render. ReShade's render_technique is not used: it raises
// reshade_finish_effects, where FlatVR's add-on takes the picture for the headset, and in D3D12 ReShade restores none of
// the list's state after it (state_block.cpp has no D3D12 case).
//
// The post pass leaves the scene's fog to this while it draws (D2R_DepthFog.fx FogInFrame); the void (the sky, the
// ceiling, the fog of the black beyond the world) stays the post pass's.
//
// VR F4 only (the fog of the views from above and of flat mode as before). Read at the game's start: the game makes its
// pipelines then, and ReShade does not replay init_pipeline for pipelines made before the add-on joined.

#include <reshade.hpp>

namespace fogframe {

// The events, for the game's D3D12 device only. Once. `log` takes one line at a time.
void Register(void (*log)(const char*));
// [render] fog_in_frame as read (the settings' load): something for Register to do.
void SetWanted(bool wanted);
bool Wanted();

// What the post pass has (D2R_DepthFog.fx), each effects pass.
struct Params {
    bool on = false;          // the key, VR F4, our fog on, the fog's colour fixed (FogFixed or no sky) - everything for it
    float nearPlane = 1.5f, fogStart = 150.0f, fogEnd = 600.0f, fogCurve = 1.6f, fogStrength = 1.0f;   // (by each pixel's own depth: no blur)
    float fogColour[3] = {0.02f, 0.02f, 0.025f};   // as the post pass lays it on the finished picture (display sRGB)
    float caveHalo = 0.0f;    // CeilOn without a sky: CeilHalo (the warm light kept), else 0
    int paint = 0;            // [debug] fog_in_frame_paint: 2 the effects alone, 3 the scene alone, 4 the silhouettes green (fogframe::Painted)
};
void Configure(const Params& p);
// On (Configure's last `on`): the post pass is to leave the scene's fog to the frame - and so is fog_spare.
bool Armed();
// Armed, and the fog drawn in the frame within the last quarter second: the post pass leaves the scene's fog to it.
bool Drawn();

}  // namespace fogframe
