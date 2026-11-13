// ao_vr.h - [render] ao_vr / ao_vr_radius_m: the game's own ambient occlusion (ASSAO) with a radius for eyes a few metres from the
// ground, in first person (VR F4 and the flat game's F3). shared/dxbc_ao_radius.h says why: the game's radius and near limit are its numbers for its camera from
// above; from our eyes dense grass a metre away filled the AO's disk, and the see-through grass cards (their colour times the AO's copy)
// went black - Serg 2026-11-13, without DLSS. The game's generate pipelines (ps_assao_generate_p) are kept as it makes them (native D3D12
// hooks), a variant with the radius as an immediate made beside each for the current radius, and bound in the game's place while on there.
// Off, or elsewhere (VR F1 - F3, the flat game from above or behind, menus): the game's own pipeline, untouched. No GPU work of ours: two operands of the game's shader.
#pragma once

#include <cstdint>

struct ID3D12Device;
struct ID3D12PipelineState;
struct D3D12_GRAPHICS_PIPELINE_STATE_DESC;
struct D3D12_PIPELINE_STATE_STREAM_DESC;

namespace aovr {

using LogFn = void (*)(const char*);
void SetLog(LogFn log);

// natived3d's sinks (any thread): a pipeline the game made; one it binds - ours in its place while on
void OnGraphicsPso(ID3D12Device* dev, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc, ID3D12PipelineState* pso);
void OnStreamPso(ID3D12Device* dev, const D3D12_PIPELINE_STATE_STREAM_DESC* desc, ID3D12PipelineState* pso);
ID3D12PipelineState* Swap(ID3D12PipelineState* pso);

// The present, each frame: on (the key, F4, in a world) and the radius in world units (the caller's metres times the world's units a
// metre). Variants are made again when the radius moves by more than 5 % (at most once a second) - none per frame.
void Frame(bool on, float radiusUnits, float metres);
// For the tests: the game's generate pipelines kept, variants made in all, binds of ours
int Kept();
uint32_t Builds();
uint32_t Bound();

}  // namespace aovr
