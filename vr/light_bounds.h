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

}  // namespace lightbounds
