// fog_height.h - [fog] game_fog_height / game_fog_top / game_fog_soft: the game's own distance fog thinned with height, in its own fog
// pass (shared/dxbc_fog_height.h patches its shader). Serg (2026-10-27): the game's fog covered [sky] game_sky's dome overhead too - "надо
// подумать, как ограничить его высоту", its height on a slider. The game's fog pass's pipelines are kept as it makes them (native D3D12
// hooks, [render] no_reshade); a variant with the patched shader is made for the top and the softness set (again when they change) and bound
// in place of the game's while it is on. Off: the game's own, untouched. Nothing of ours runs on the GPU: a few instructions in its pass.
#pragma once

#include <cstddef>
#include <cstdint>

struct ID3D12Device;
struct ID3D12PipelineState;
struct D3D12_GRAPHICS_PIPELINE_STATE_DESC;
struct D3D12_PIPELINE_STATE_STREAM_DESC;

namespace fogheight {

using LogFn = void (*)(const char*);
void SetLog(LogFn log);

// natived3d's sinks (any thread): a pipeline the game made; one it binds - ours in its place while on
void OnGraphicsPso(ID3D12Device* dev, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc, ID3D12PipelineState* pso);
void OnStreamPso(ID3D12Device* dev, const D3D12_PIPELINE_STATE_STREAM_DESC* desc, ID3D12PipelineState* pso);
ID3D12PipelineState* Swap(ID3D12PipelineState* pso);

// The present, each frame: on or off, the fog's top (world units above the eye: none above it), how far below it the fog fades in, and
// whether the game's volumetric mist is fogged by distance too ([fog] game_fog_mist: in a cave with the game drawing our black fog, its
// torch-lit mist stayed whole over the far walls fogged black - a light band, PC1 2026-10-27), and whether the void (past the linear
// depth's far end) is left alone instead of given the mist whole ([fog] game_fog_caves_black, underground: past a cave ceiling's rim the
// same band). The variants are made when these change.
// [fog] game_fog_our_curve: the fog's share by our effect's curve (start, end, curve, strength) in place of the game's exponential.
// ease: [fog] ease_in, the share eased in over its first 40 units (D2R VR Unleashed's, shared/unleashed_fog.h).
struct Curve {
    bool on = false;
    float start = 0.0f, end = 1.0f, curve = 1.0f, strength = 1.0f;
    bool ease = false;
};
void Frame(bool on, float top, float soft, bool mist, bool voidAlone = false, bool noFog = false, const Curve& curve = Curve{});
// [fog] game_fog_before_fx: the distance fog's colour left out of the pass (the scene fogged before the transparent resolve), its mist still
// fogged - noFog above. A stream's pixel shader (its bytecode in the stream as given), for the resolve's pipeline found the same way.
// How many times our patched pass was bound in the game's place so far: it grows while the game's fog is drawn by it (height, our curve)
uint32_t Bound();
bool StreamPixelShader(const D3D12_PIPELINE_STATE_STREAM_DESC* desc, const void** code, size_t* size);

}  // namespace fogheight
