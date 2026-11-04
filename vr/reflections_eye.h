#pragma once
// [render] reflections_per_eye - the game's reflections in puddles, blood and water put in the right place for each eye
// (shared/dxbc_ppr_eye.h says what was wrong: in "one pass, two pictures" the right eye drew each reflection as seen from twice the
// eyes' distance - half its depth, standing above the water; zbiromir, Quest 3S, 2026-11-03).
//
// The game's two reflection pipelines (its compute projection pass and its full-screen resolve) are kept as it makes them, a copy with
// the patched shader made beside each, and the copy bound in the game's place while the key is on in VR - through ReShade's events
// (init_pipeline / bind_pipeline) or, without ReShade, the plugin's own D3D12 hooks (vr/native_d3d12.h), as vr/light_bounds.cpp does.
// The copies are made only when the key is on at the game's start (the game makes its pipelines then): off at the start, nothing is made
// - the old behaviour exactly; on at the start, it follows the key live. No GPU work of ours: one operand of the game's own instruction.

#include <cstdint>

struct ID3D12Device;
struct ID3D12PipelineState;
struct D3D12_COMPUTE_PIPELINE_STATE_DESC;
struct D3D12_GRAPHICS_PIPELINE_STATE_DESC;
struct D3D12_PIPELINE_STATE_STREAM_DESC;

namespace refleye {

// ReShade's four events (the game's own D3D12 device only); once, after the caller joined ReShade. Only when Wanted().
void Register(void (*log)(const char*));
// From d2r_vr.ini on every change: `make` = [render] reflections_per_eye (read at the game's start: the copies are made as the game
// makes its pipelines), `bind` = bound now (VR: [mode] platform != 0).
void Configure(bool make, bool bind);
bool Wanted();
// Without ReShade: the plugin's own hooks hand over the pipelines the game makes and the one it binds.
void SetLog(void (*log)(const char*));
void OnComputePso(ID3D12Device* dev, const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc, ID3D12PipelineState* pso);
void OnGraphicsPso(ID3D12Device* dev, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc, ID3D12PipelineState* pso);
void OnStreamPso(ID3D12Device* dev, const D3D12_PIPELINE_STATE_STREAM_DESC* desc, ID3D12PipelineState* pso);
ID3D12PipelineState* Swap(ID3D12PipelineState* pso);   // the pipeline to bind for this one (itself unless it is a reflection pass's and on)
// Every present: a line every 10 s while the copies are bound (how many binds of each pass: once per game pass - the replay runs the
// same lists again for the right eye), and one when the state changes.
void Tell();
// For the tests: how many of the game's reflection pipelines have a copy (ReShade's and the native hooks' together)
int Copies();

}  // namespace refleye
