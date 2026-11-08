// lens_mask.h - [render] lens_mask: the pixels the headset's lenses never show are left out of the game's heavy lit passes (docs/notes/lens_mask.md,
// docs/notes/frame_cost_study.md cut #2 - the standard VR hidden-area mask).
//
// Right after the game clears its scene depth (the one the depth pre-pass draws into), on the game's own list and in its order, the part of each
// eye's picture the runtime says is hidden (XR_KHR_visibility_mask's hidden triangle mesh, vr/xr.h VisibilityMask) - and, with runtime_fov's box or
// fov_override_centred, the part of the picture never submitted - is cleared again to the NEAREST depth. The pre-pass, the depth-equal lit colour
// pass, the depth-tested water / glass / hair / particles and our own depth-tested passes then fail the depth test there. Full-screen passes that do
// not test depth still run there (cheap ones; the grass lighting tests stencil, which this leaves as the game cleared it).
//
// How: a ClearDepthStencilView with rectangles (vr/lens_mask_shape.h: all inside the mask, a margin from every shown pixel) - no pipeline, root
// signature, heap or target of the game's list is touched. Which eye's rectangles:
//   - two passes, eyes by turns: the eye whose pass is recorded (BeginEye .. EndWorld);
//   - [stereo] native_replay (one pass, two pictures): the left pass's lists are run again as the right eye - they carry BOTH eyes' clears, each
//     under D3D12 predication on the replay's per-eye buffer (the one the DLSS twin uses: {1,0} before the left lists, {0,1} before the replay);
//     a replay without that buffer: no mask at all (the right eye would get the left one's).
//   - eyes by turns with [stereo] turn_warp: not drawn (the eye made is moved by this depth: the nearest depth would be pulled into view).
// F4 with native OpenXR's first-person pair only, not canted displays; rendering only. Off = the game's clear alone, as before.
#pragma once

#include <cstdint>

struct ID3D12Resource;
namespace natived3d { struct Sinks; }

namespace lensmask {

using LogFn = void (*)(const char*);

// At the plugin's load, before natived3d::Install (the native path): the clear's sink filled in (always: off, it only passes the clear on).
void FillSinks(natived3d::Sinks& k);
void SetLog(LogFn log);
bool Hooked();

struct Config {
    bool on = false;          // [render] lens_mask
    bool turnWarp = true;     // [stereo] turn_warp (eyes by turns: the eye made moved by the depth - no mask then)
};
void Configure(const Config& c);   // vrcam's settings load, live

// Each present: the eyes' picture's size (what the scene's depth must measure) and the share of it the game draws its scene into (an upscaler's
// top-left part; 1 x 1 without one).
void NoteScreen(uint32_t w, uint32_t h, const float share[2]);

// The replay's state at the moment a scene depth clear is recorded (any thread): 0 no replay is being captured (the clear is the recorded eye's),
// 1 the left pass is captured for a replay with the per-eye predicate (`*pred`: the buffer, u64 [0] nonzero for the left eye, [1] for the right),
// 2 captured without one (no mask).
using ReplayFn = int (*)(ID3D12Resource** pred);
void SetReplay(ReplayFn fn);

// Rectangles one ClearDepthStencilView is given (16 by default: one call with 100 killed the process on an NVIDIA driver). The tests set 1 on WARP,
// which honours only a call's first rectangle.
void SetRectsPerCall(unsigned n);

// The draw thread, before a pass of the game's is drawn (or replayed) as eye 0 / 1 of a native OpenXR first-person pair (`pair`); `byTurns`:
// eyes by turns. EndWorld: after the pair's passes.
void BeginEye(int eye, bool pair, bool byTurns);
void EndWorld();

}  // namespace lensmask
