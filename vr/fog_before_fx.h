// fog_before_fx.h - [fog] game_fog_before_fx: the game's distance fog (our numbers, [fog] game_fog) laid on the opaque scene BEFORE the game's
// transparent resolve, on the no-ReShade path - the smoke, the mist's particles and the fire then go over a fogged scene, unfogged themselves.
//
// PC1 2026-10-27 (5.webp): with the game's own fog pass (ps_atmosphere_apply) drawing our fog, a cave's haze stood as a light layer with
// hard black holes in it - the pass runs after the resolve and fogs every pixel by the depth of the wall behind it, so the haze in front of
// a far wall went black with the wall, the haze in front of near rock stayed. The game's resolve (ps_transparent_resolve, its 756 bytes)
// reads the scene, its two transparent layers and the additive light (t10 - t13) and nothing else: no depth, no fog constants - the fog
// cannot go into its shader. So a step of ours goes before its draw, recorded into the game's own list:
//   our compute shader reads the scene's target and the game's depth (reverse-Z, no far plane: distance = near / depth) and writes the
//   fogged scene into a texture of ours - the game's exponential fog with its plane and density, the colour in its scene units, thinned
//   with height outdoors as [fog] game_fog_height thins the pass - which is copied back over the scene; the void (depth 0) is left alone.
// Then the game's pass draws no fog's colour of its own (vr/fog_height.h noFog: its volumetric mist still fogged by distance).
//
// The list's state is the game's again after the step: its descriptor heaps, its graphics descriptor tables (another heap bound makes them
// to be set again), its compute root signature and arguments, and the resolve's pipeline. The scene's and the depth's states are what the
// list's last barrier on each said (else any list's, else the resolve's: the scene a pixel shader resource, the depth written). The scene is
// the R11G11B10 target the list last bound with the resolve's depth-stencil. Anything not as expected and the game's draw goes on alone, a
// line in the log. Its cost on the graphics card (timestamps round the step) is in the 10 s line.
#pragma once

#include <cstddef>
#include <cstdint>

struct ID3D12Device;
struct ID3D12PipelineState;
struct D3D12_GRAPHICS_PIPELINE_STATE_DESC;
struct D3D12_PIPELINE_STATE_STREAM_DESC;
namespace natived3d { struct Sinks; }

namespace fogfx {

using LogFn = void (*)(const char*);
void SetLog(LogFn log);
// natived3d's list sinks (fxReset ... fxDraw) for the step
void FillSinks(natived3d::Sinks& k);
// The game's pipelines as made: the resolve's found by its pixel shader
void OnGraphicsPso(ID3D12Device* dev, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc, ID3D12PipelineState* pso);
void OnStreamPso(ID3D12Device* dev, const D3D12_PIPELINE_STATE_STREAM_DESC* desc, ID3D12PipelineState* pso);
void MarkResolve(ID3D12PipelineState* pso);   // (the test: a pipeline taken as the resolve's)

// The fog as the present has it, each frame
struct Params {
    bool on = false;                       // the key, VR F4, the game's fog drawing ours
    float plane = 100.0f, density = 1.0f;   // the game's: t = min(1, exp(-density (distance / plane - 1)))
    float colour[3] = {0, 0, 0};           // the game's scene units
    float nearPlane = 0.05f;               // the projection's (reverse-Z, no far plane)
    float sx = 1.0f, sy = 1.0f, offX = 0.0f;   // its x and y scale and the eye's off-axis shift
    bool height = false;                   // thinned with height: none above `top`, fading in over `soft` below it (world units above the eye)
    float top = 60.0f, soft = 50.0f;
    float up[3] = {0, 1, 0};               // the world's up in the view's axes (right, up, ahead)
    // [fog] game_fog_our_curve: our effect's curve in place of the exponential - strength x saturate((distance - start) / (end - start)) ^ curve
    bool ourCurve = false;
    float start = 50.0f, end = 150.0f, curve = 1.0f, strength = 1.0f;
};
void Configure(const Params& p);
// The step ran within the last quarter second: the game's pass is to leave the fog's colour out (fog_height noFog)
bool Drawn();
// The present, every frame: the 10 s line
void Frame();

}  // namespace fogfx
