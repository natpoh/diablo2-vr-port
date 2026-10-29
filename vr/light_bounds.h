#pragma once
// The game's light culling at render sizes that are not 16:10 (vr/light_bounds_shader.h says what is
// wrong with it): its compute shader for each lighting tile's depth range, replaced by ours while VR
// renders ([render] light_bounds_fix) and in flat mode too ([render] light_bounds_flat). Idea and the shader's match values (size and FNV-1a of the exact
// shader of build 3.3.93787): bearstonem/d2r-pcvr, steamvr/light_bounds.cpp (MIT); the shader and the
// code here are our own.
//
// ReShade's init_pipeline is not replayed for a pipeline made before the add-on joined, and the game
// makes its pipelines at startup: Register has to run as soon as ReShade is there (vrcam does it right
// after loading ReShade64.dll), not on the update thread's first tick.

#include <cstdint>

struct ID3D12Device;
struct ID3D12PipelineState;
struct D3D12_COMPUTE_PIPELINE_STATE_DESC;
struct D3D12_PIPELINE_STATE_STREAM_DESC;

namespace lightbounds {

// The four ReShade events, for the game's own D3D12 device only. `log` takes one line at a time. Once.
// The caller has joined ReShade (reshade::register_addon) first.
void Register(void (*log)(const char*));

// From d2r_vr.ini, on every change. `make` = [render] light_bounds_fix: the replacement is made as the game makes its
// pipeline (read at its start - the pipelines are made then; off at the start = nothing is made, the old behaviour
// exactly). `bind` = it is bound now, live: in VR with `make`, in flat mode only with [render] light_bounds_flat too.
// `logAll` = [debug] log_compute_shaders: a line (size and FNV-1a) for every compute shader the game makes, which is
// how the match values are checked on a machine. `flat` = [mode] platform=0 (for the log).
void Configure(bool make, bool bind, bool logAll, bool flat);

// Something for Register to do (either of `make` and `logAll` is on). The caller registers only then, so with both
// off the game and ReShade see nothing of this.
bool Wanted();

// The size the game draws at (the swap chain's), every present: a log line when it or the state changes - what
// the 32 x 20 light grid makes of it (square tiles = the game's own shader is right) and whether ours is bound.
void NoteScreen(unsigned w, unsigned h);

// Without ReShade ([render] no_reshade): the same replacement under the same key ([render] no_reshade), through the plugin's own D3D12 hooks
// (vr/native_d3d12.h). With no ReShade in the game nothing above is ever registered, and the game's own light culling ran at the eyes'
// size (2496 x 2688 on a PS VR2: tiles 78 x 134 px) - the lights vanished in hard black rectangles on the walls' tops (act 2 palace
// cellar, act 1 monastery, Serg 2026-10-29: "на потолке рисуются какие-то черные квадраты у нас, у медведя их нет" - D2R VR
// Unleashed always runs through ReShade). [render] light_bounds_fix off: nothing made, the game's own light culling as before.
// The game's pipeline is kept as it makes it (a description or a stream; one reference of ours, so its address is never another's), a
// copy with our shader made beside it, bound in its place by SetPipelineState under the same rule as above (`make` when it is made, `bind` when it is
// bound). The log says so: found, ready, bound "without ReShade".
void SetLog(void (*log)(const char*));   // (Register sets it with ReShade; without, the plugin's own hooks' Install)
void OnComputePso(ID3D12Device* dev, const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc, ID3D12PipelineState* pso);
void OnStreamPso(ID3D12Device* dev, const D3D12_PIPELINE_STATE_STREAM_DESC* desc, ID3D12PipelineState* pso);
ID3D12PipelineState* Swap(ID3D12PipelineState* pso);   // the pipeline to bind for this one (itself unless it is the game's light bins')
// For cleanroom/tests/light_bounds_native_test.cpp: a shader of the test's own stands for the game's (its size and FNV-1a 64; 0, 0 = the game's)
void MatchForTest(uint32_t size, uint64_t hash);

}  // namespace lightbounds
