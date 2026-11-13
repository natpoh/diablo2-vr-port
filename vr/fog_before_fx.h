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
//
// With our own effect's fog ([fog] game_fog=0, Params::ours; Serg 2026-10-30: "туман который будет рисоваться раньше чем огонь и дымка
// чтобы их не ломать") the same step lays that fog instead: D2R_DepthFog.fx's curve by the view's depth, its colour taken into the scene's
// units (fog_in_frame_math.h SceneFromDisplay), and the post pass leaves every pixel with a depth of its own to it (FogInFrame, as
// [render] fog_in_frame on the ReShade path) - the void, the sky, the ceiling and the light over the void stay the post pass's.
#pragma once

#include <cstddef>
#include <cstdint>

struct ID3D12Device;
struct ID3D12PipelineState;
struct ID3D12CommandQueue;
struct ID3D12CommandList;
struct ID3D12GraphicsCommandList;
struct D3D12_GRAPHICS_PIPELINE_STATE_DESC;
struct D3D12_PIPELINE_STATE_STREAM_DESC;
namespace natived3d { struct Sinks; }

namespace fogfx {

using LogFn = void (*)(const char*);
void SetLog(LogFn log);
// natived3d's list sinks (fxReset ... fxDraw) for the step
void FillSinks(natived3d::Sinks& k);
// Every ExecuteCommandLists of the game's queues, before the real call (vr/ecl_hook.h's tap): the scene's state followed in the order the
// lists run - the step draws from it only once proven (fog_before_fx.cpp, "the submission order")
void OnExecute(ID3D12CommandQueue* q, unsigned n, ID3D12CommandList* const* lists);
bool Hooked();   // FillSinks was called (the key on at the game's start, no ReShade): the step can be drawn at all
// The game's pipelines as made: the resolve's found by its pixel shader; one that may draw an effect (blended into its first target, no
// depth written) kept for its twin (Params::fxDepth)
void OnGraphicsPso(ID3D12Device* dev, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc, ID3D12PipelineState* pso);
void OnStreamPso(ID3D12Device* dev, const D3D12_PIPELINE_STATE_STREAM_DESC* desc, ID3D12PipelineState* pso);
void MarkResolve(ID3D12PipelineState* pso, int blends = -1);
// (the test: a pipeline taken as a hair / foliage one - fogged by the depth behind it; oit: its pixel shader writes the game's weight)
void MarkFogBehind(ID3D12PipelineState* pso, bool oit = true);
// The pixel shader writes the game's weighted-blended weight 1e6 / (1e5 - z^3): its bytecode holds both constants as immediates (the game's
// see-through permutations of ps_hair_p, ps_foliage_p, ps_models_p, ps_alpha_threshold_p, ps_models_blend_p, ps_grunge_p, ...; their opaque
// twins - same containers, the bench's same stage - hold neither: 2026-11-13, every permutation of the game's 3.3 containers checked)
bool HasOitWeight(const void* bytecode, size_t n);

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
    bool ease = false;                     // [fog] ease_in: that curve eased in over its first 40 units (D2R VR Unleashed's, shared/unleashed_fog.h)
    // Our effect's fog ([fog] game_fog=0), not the game's: that curve by the view's depth (D2R_DepthFog.fx FogAmount: near / depth), not
    // the point's whole length; the colour the post pass lays, in the scene's units. The post pass then leaves a drawn pixel's fog to the
    // step (FogInFrame) - the void, the sky and the ceiling stay its own
    bool ours = false;
    // [fog] game_fog_fx_depth: the effects themselves fogged by their own distance (Serg 2026-10-31, a Rogue camp torch at night: "новый
    // туман, старый туман" - the far palisade's fires at full light in full fog, its see-through bits a band of specks on the horizon).
    // Each draw of the game's into its weighted-blended targets or its half-size additive one is drawn again by a twin of its pipeline -
    // its vertex shaders, no pixel shader - into a depth of ours (the nearest effect at each pixel; a quad's empty corners too, where it
    // adds next to nothing); at the step the weighted-blended colour goes toward the fog's colour and the additive light down by the fog
    // at that depth (never nearer than the scene behind); over the void they fade out by it (the sky goes there after). Its cost (PC1
    // 2026-10-31, 0.192.22, Quest 3 at 2048 x 2048 an eye): 72-74 -> 41-43 frames/s - the targets switched to our depth and back round each
    // of ~1250 effect draws a pair break the GPU's passes, far more than the 1 ms its own timestamps see; off as shipped.
    // (Whatever this says, with the game's fog the step fogs the pixels of no depth beside drawn ground (holes the far trees' dithered edges leave, the one-pixel rim
    // round anything against the void where the game lays its blended edge) by the farthest drawn neighbour - the game's pass gave them
    // its mist, the post pass leaves them to the game.
    bool fxDepth = false;
    // [sky] game_sky: the game's dome shown - a pixel at or under this depth (reverse-Z; D2R_DepthFog.fx SkyDomeDepth) is its sky and left
    // as the void is (the game's pass leaves it: past its linear depth's far end); 0 = no dome
    float skyDepth = 0.0f;
    // [fog] game_fog_planes - "Fog planes before the player" (Serg 2026-11-09, his drawing: "новый вид тумана плоскости перед игроком с
    // затуманиванием"): `planes` camera-facing planes across the whole view at view depths from planesStart to planesEnd, evenly spaced, the
    // last at planesEnd; each covers what lies behind it (its opaque depth farther than the plane - the depth test), and they add up to
    // planesStrength there (the fog's share after plane i: strength x (i + 1) / planes). planesSoft 0: hard planes; 1: each fades in over the
    // spacing in front of it (soft particles: no line where a plane cuts the ground; the fog still from planesStart, whole at planesEnd). In
    // place of the game's exponential; 0 = none (as before).
    // [fog] game_fog_cover: at or past this distance (world units) the fog's colour whole - the world, the hair and foliage, the particles
    // (their light out); 0 = the fog's own curve all the way (as before). The game's fog only
    float cover = 0.0f;
    // The share of the target the game draws its scene in (fx::renderscale: an upscaler - DLSS at 0.667 draws into the top-left 2/3): each
    // pixel's ray, its distance and the dome's test, from that part (1 x 1 without an upscaler: as before). 2026-11-12: with DLSS the dome
    // showed only in a circle, the rest the fog's colour ([fog] game_fog_cover)
    float part[2] = {1.0f, 1.0f};
    int planes = 0;
    float planesStart = 55.0f, planesEnd = 140.0f, planesStrength = 1.0f, planesSoft = 1.0f;
    // [fog] game_fog_hair_strict (2026-11-13: Serg's grass black without DLSS; martin039383's hair and grass fogged at other distances): only
    // draws whose pixel shader writes the game's weight (HasOitWeight) are sent into our hair / foliage target - the effects' weighted colour
    // texture is the game's pooled 2048 RGBA16F one, drawn into by other passes too (the 10 s line saw a shadow pipeline in it), and an
    // opaque lit draw into it was taken from its pass; and nothing of ours is left over from a step that did not fold it (no step at the
    // resolve, the effects' colour or revealage not taken): cleared, never folded into a later frame. false: as 0.215
    bool hairStrict = true;
    // [fog] game_fog_own_distance_behind: the hair / foliage and particles' own distance (read from the game's weight) never past the opaque
    // scene behind them at that pixel (they passed the depth test there) - a weight read too small (overlapping layers, a half-folded frame)
    // fogged near grass as far ground. false: as 0.215
    bool ownBehind = true;
    // [fog] game_fog_scene_one_size: the scene the resolve's own tables name is taken only when it is the depth's size. Serg 2026-11-13 on
    // 0.215.6: the tables also named the game's 1024 x 1024 half-size effects layer (once bound with the 2048 depth); taken as the scene, the
    // step refused it for its size and missed hundreds of frames in 10 s. false: as 0.215 (the first named one)
    bool sceneOneSize = true;
    // [fog] game_fog_scene_by_name: the scene is the view in the resolve's own tables the game named "Main Color 0" / "Main Color 1" (its
    // renderer names its targets, its D3D12 back end passes the names on) - known, not guessed; no such view: the search as before.
    // false: the search only (as 0.215)
    bool sceneByName = true;
    // [fog] game_fog_layers_in_order: the particles and the hair / foliage laid near over far by their own distance and coverage, not
    // averaged by the game's weight (made for its camera far above: from our eyes a near haze particle outweighs a far bush 80 times and
    // the bush took the haze's pale colour - Serg 2026-11-13 on 0.215.8). false: as 0.215 (one weighted average)
    bool layersInOrder = true;
};
void Configure(const Params& p);
// The fog's share each plane adds (the fog after plane i: strength x (i + 1) / n), and its view depth - the shader's PlanesKeep, the log
inline float PlaneAlpha(int i, int n, float strength) {
    const float s = strength < 0.0f ? 0.0f : strength > 1.0f ? 1.0f : strength;
    const float before = s * (float)i / (float)n, after = s * (float)(i + 1) / (float)n;
    return 1.0f - (1.0f - after) / (1.0f - before > 1e-4f ? 1.0f - before : 1e-4f);
}
inline float PlaneAt(int i, int n, float start, float end) { return start + (end - start) * (float)(i + 1) / (float)n; }
// The step ran within the last quarter second: the game's pass is to leave the fog's colour out (fog_height noFog)
bool Drawn();
// ... with our effect's fog, and still set so: the post pass is to leave a drawn pixel's fog to it (D2R_DepthFog.fx FogInFrame)
bool OursDrawn();
// The present, every frame: the 10 s line
void Frame();
// The probe of the last step recorded (its GPU work done - the test waits; the 10 s line reads one 8 steps back): at the shader's five points
// (the middle, then 15 / 35 / 65 / 85 % across at 45 % down) 16 floats each - the depth, the view depth, the distance, what the fog leaves,
// the scene's colour before (3), the sky test (1 / 0), the colour after (3), 1, the resolve's own target's colour before it draws (3), 0.
// False: no step yet.
bool LastProbe(float out[80]);
// The test: the scene check started again in `mode` (0 the tables' scene, 1 the resolve's target, 2 nothing written - the game's start)
void ResetSceneCheck(int mode);
// Only the found scene, depth and effects' targets watched (the sinks no longer search): the test, the 10 s line
bool Watching();
// [fog] game_fog_miss_keeps_fog (vr/fog_height.h SetPick): did the step write at the resolve the game's fog pass on `cl` follows - the list's
// own last resolve since its reset, else the last resolve on any list; none seen yet: true (the colour left out, as before)
bool StepDrewAt(ID3D12GraphicsCommandList* cl);

}  // namespace fogfx
