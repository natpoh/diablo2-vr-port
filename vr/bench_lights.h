// bench_lights.h - [render] bench_skip_point_lights: the game's local lights (torches, braziers, fire spells, glowing items, the hero's own
// light circle) out of its frame, so the sun (its directional light and shadow map) and the area's ambient light alone light the world.
// A render bench key like the others (vr/render_bench.h): visual only, live, VR first person (F4) only. Serg 2026-11-08: "от факелов можно
// отключить и включить только солнце".
//
// Where the game's local lights come in (read from its own shaders, D2R 3.3.x - see docs/notes/render_bench.md):
//   - cs_light_bin_c (one thread a screen tile, 8 x 4 a group): clears each tile's light count (u0[tile] and u0[tiles + tile], structured,
//     4 bytes) and appends the lights reaching the tile - four kinds, their numbers in b12[2].xyz and b12[3].x - into the tile's list
//     (u1, 1,024 bytes a tile). The lit passes (ps_models_p, ps_terrain, the grass, water, glass, hair) read the count (t4, low 6 bits per
//     kind) and loop over the list (t5) into the light data (t8); ps_global_attenuation (the mask that lets the ambient light in only
//     inside the lights' reach - the hero's light circle) reads the same lists.
//   - cs_light_grid_c (8 x 1 x 8): the 3D light volumes (u4 - u7, read as t45 - t48 by the lit passes, the particles and the global
//     attenuation): for each cell, the lights of two groups summed (b2[0].z and b2[0].y of them).
// The hero's light circle is one of those lights: the renderer's unit lights give it its power, attenuation and reach (vr/sky/lights.cpp,
// 0x74B324) - it goes with the torches.
//
// How: both compute pipelines are copied as the game makes them, with a shader of ours in the game's own root signature, and bound in
// their place while the key is on in F4 (as [render] light_bounds_fix binds its own depth bounds):
//   - the bins: only the clear - every tile's counts 0, nothing appended. The lit passes then loop over no local light at all.
//   - the grid: what the game's own shader writes with no lights in either group (worked out from it: the colour volumes 0, the direction
//     volumes its default direction (0, -0.01, -0.006) with its own attenuation term) - not zeros, which its readers would normalise.
// Nothing of the game's is changed or skipped: its lists, barriers and dispatches are as recorded; only which pipeline a dispatch runs.
// The sun (its own constants and shadow map) and the area's light from the sky (IBL) are not in either: they stay.
#pragma once

#include <cstdint>

namespace benchlights {

// The game's shaders (data/hd/shaders, one DXBC container each): size and FNV-1a 64 of the bytes (vr/render_bench_hashes.h's hash).
inline constexpr uint32_t kBinSize = 18388;
inline constexpr uint64_t kBinHash = 0xa405ad975083a699ull;   // cs_light_bin_c
inline constexpr uint32_t kGridSize = 5120;
inline constexpr uint64_t kGridHash = 0x7d6db23580b43725ull;  // cs_light_grid_c

// The bins with no light: the game's clear of both counts of each tile, and nothing else. b12: [0].xy tiles across and down, [1].y a row's
// length (the game's: tile = id.y * b12[1].y + id.x; the second count at tiles across x down + tile).
inline constexpr char kBinShader[] = R"(
RWStructuredBuffer<uint> Counts : register(u0);
cbuffer LightBin : register(b12) { int4 B12[4]; };
[numthreads(8, 4, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    const uint tile = id.y * (uint)B12[1].y + id.x;
    Counts[tile] = 0;
    Counts[(uint)(B12[0].x * B12[0].y) + tile] = 0;
}
)";

// The light volumes with no light in either group, as the game's shader leaves them then. b2: [1].xyz the volume's size (cells past it
// untouched), [1].w the attenuation's switch; b10[88].y the attenuation's base.
inline constexpr char kGridShader[] = R"(
RWTexture3D<float4> Grid4 : register(u4);
RWTexture3D<float4> Grid5 : register(u5);
RWTexture3D<float4> Grid6 : register(u6);
RWTexture3D<float4> Grid7 : register(u7);
cbuffer Frame : register(b10) { float4 B10[89]; };
cbuffer LightGrid : register(b2) { float4 B2[4]; };
[numthreads(8, 1, 8)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (any(id >= asuint(B2[1].xyz))) return;
    const float base = B10[88].y;
    const float3 dir = float3(-0.0, -0.01, -0.006);
    Grid6[id] = float4(dir, base);
    Grid7[id] = float4(0.0, 0.0, 0.0, 0.0);
    Grid4[id] = float4(dir, asuint(B2[1].w) != 0u ? 0.0 : base);
    Grid5[id] = float4(0.0, 0.0, 0.0, 0.0);
}
)";

}  // namespace benchlights

#ifdef _WIN32
#include <string>
struct D3D12_COMPUTE_PIPELINE_STATE_DESC;
struct D3D12_PIPELINE_STATE_STREAM_DESC;
struct ID3D12Device;
struct ID3D12PipelineState;
namespace benchlights {
void SetLog(void (*log)(const char*));
// The game's compute pipelines as made (vr/render/effects/effects.cpp's sinks): the two are copied with ours (always - it is only bound on).
void OnComputePso(ID3D12Device* dev, const D3D12_COMPUTE_PIPELINE_STATE_DESC* d, ID3D12PipelineState* pso);
void OnStreamPso(ID3D12Device* dev, const D3D12_PIPELINE_STATE_STREAM_DESC* d, ID3D12PipelineState* pso);
// A pipeline the game binds: ours in its place while on, in F4 (one relaxed load while off).
ID3D12PipelineState* Swap(ID3D12PipelineState* pso);
// [render] bench_skip_point_lights, live
void SetOn(bool on);
bool On();
// The 10 s line (empty while off and already told so): binds swapped a pair, the copies ready
std::string Line();
}  // namespace benchlights
#endif
