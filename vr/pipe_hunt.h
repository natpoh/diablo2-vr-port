// pipe_hunt.h - the game's passes found by elimination without ReShade: [debug] skip_pipeline, [render] skip_shaders and [debug] patch_hunt 8
// (vr/patch_hunt.h) on the plugin's own D3D12 hooks (vr/native_d3d12.cpp), and Ctrl+F10's one-frame list of the pipelines the game binds.
//
// Why: the old skip and the old Ctrl+F10 trace (vr/uitrace.cpp) are ReShade add-on events, and since 0.203 the mod never loads ReShade - both did
// nothing. Here every pipeline the game makes is hashed as it is made (FNV-1a 64 of its pixel shader - a compute pipeline: of its compute shader -,
// the very hash uitrace printed as "ps" and [render] skip_shaders takes); a bind (SetPipelineState, or a list's Reset with a pipeline) marks the
// list on its thread (a list records on one thread), and its draws, dispatches and indirect calls are left out until the next bind - the game's
// memory is not touched. A skipped compute pass leaves its output as the last frame had it (or empty): for finding a pass, not for play.
//
// The trace (Arm): from one present to the next, every pipeline bound - its kind, hash, its vertex shader's hash, binds, draws, dispatches - to
// d2r_vr_pipetrace.txt, in the order of their first bind; the ones that drew are kept as the list patch_hunt_pipe K counts in (1-based).
#pragma once

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>

#include <cstdint>
#include <string>

namespace pipehunt {

void SetLog(void (*log)(const char*));

// The game's pipelines as made (the native hooks' sinks, vr/render/effects/effects.cpp).
void OnGraphicsPso(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* d, ID3D12PipelineState* pso);
void OnComputePso(const D3D12_COMPUTE_PIPELINE_STATE_DESC* d, ID3D12PipelineState* pso);
void OnStreamPso(const D3D12_PIPELINE_STATE_STREAM_DESC* d, ID3D12PipelineState* pso);
// A pipeline the game binds on cl (the one it asked for, before any swap of ours); nullptr: none (a Reset without one).
void OnBound(ID3D12GraphicsCommandList* cl, ID3D12PipelineState* pso);
// A draw / dispatch / indirect call of the game's on cl: true = leave it out. One relaxed load while nothing is skipped or recorded.
bool Skip(ID3D12GraphicsCommandList* cl, int kind);   // kind: 0 draw, 1 dispatch, 2 indirect
// What the game binds on cl (the trace's columns: only looked at while one records - one relaxed load otherwise).
void OnTargets(ID3D12GraphicsCommandList* cl, unsigned n, const D3D12_CPU_DESCRIPTOR_HANDLE* rts, bool single, ID3D12Resource* ds);
void OnViewport(ID3D12GraphicsCommandList* cl, float w, float h);
// The game's present: a trace starts or ends here.
void OnPresent();

// The hashes skipped: [debug] skip_pipeline (one), [render] skip_shaders (up to 8) - 0 / none = nothing.
void SetSkip(uint64_t hash);
void SetSkipShaders(const uint64_t* hashes, int n);
// [debug] patch_hunt 8: the K-th pipeline (1-based) of the last trace's list skipped, 0 none; on = the step is in force.
void SetHunt(bool on, int k);
// One frame's pipelines to `path` at the next present (false: one is under way already).
bool Arm(const std::wstring& path);
// The last trace's file and whether it was written (vr/capture.cpp waits for its own).
bool LastWritten(std::wstring* path);

struct Now {
    bool hunting = false;     // patch_hunt 8 in force
    int index = 0, count = 0; // its K and the list's length (0: no trace yet)
    uint64_t hash = 0;        // the hash skipped by it (0: none - K 0, or no trace yet)
    char kind = '?';          // 'g' graphics, 'c' compute
    uint32_t draws = 0;       // that pipeline's draws + dispatches in the trace's frame
    uint64_t skipLegacy = 0;  // [debug] skip_pipeline
    int shaders = 0;          // [render] skip_shaders' count
    uint32_t skippedLastSecond = 0;   // calls left out over the last second, all of them
    bool recording = false;
};
Now State();

}  // namespace pipehunt
