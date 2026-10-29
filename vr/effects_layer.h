#pragma once
// effects_layer.h - eyes by turns ([stereo] turns_keep_own 3 or 4): where the game's effects are in the eye drawn this frame.
//
// The game draws its spells, flames and Tyrael's wings into an effects layer: two R16G16B16A16_FLOAT targets of the scene's size,
// tested against the scene's depth but never writing it, laid over the picture near the frame's end (bearstonem/d2r-pcvr's owner's
// draw survey, 2026-10-20: the same in every area surveyed). With no depth of their own, the eye made (vr/turn_warp.h) moved them
// across with the ground behind them and the scene showed through the wings. This finds the layer by its binding (two such targets
// with a depth target, all one size) and follows the game's barriers on it for its state, so xr.cpp can read it into the drawn eye's
// blocks at its present (turnwarp::Gpu::Effects). Nothing of the game's is changed: its bindings are only looked at.
// After bearstonem/d2r-pcvr's steamvr/effects_layer.cpp (MIT, v0.3.24, 931d820); written for our hooks.

#include <d3d12.h>

#include <cstdint>

namespace effectslayer {

// The events, for the game's D3D12 device only. Once. `log` takes one line at a time.
void Register(void (*log)(const char*));
// [render] no_reshade: the same from the plugin's own hooks (vr/native_d3d12.h) - the game binds its targets (their CPU handles, the
// depth-stencil's resource), its barriers. No ReShade, no layer: "the game's effects read 0" every 10 s and turns_keep_own 3 / 4 did nothing
// (PC1 2026-10-28) - spells, flames and wings moved across with what is behind them. With no destroy event here the two targets are held
// (a reference of ours) while they are the layer, let go when another pair is found.
void SetLogNative(void (*log)(const char*));
void OnBindNative(unsigned n, const D3D12_CPU_DESCRIPTOR_HANDLE* rts, bool single, ID3D12Resource* ds);
void OnBarrierNative(unsigned n, const D3D12_RESOURCE_BARRIER* b);
// Looked for only while wanted (eyes by turns with the effects kept): the bindings are not even looked at otherwise.
void SetWanted(bool wanted);
// This frame's layer: both targets and the state the game's barriers last left each in. False: none seen, or its states not yet known.
struct Layer {
    ID3D12Resource* targets[2] = {};
    D3D12_RESOURCE_STATES states[2] = {};
    uint32_t width = 0, height = 0;
};
bool Latest(Layer& out);
// ReShade's resource usage as a D3D12 state: the same bits but for the general (COMMON) and the constant buffer's.
inline D3D12_RESOURCE_STATES State(uint32_t usage) {
    if (usage & 0x80000000u) return D3D12_RESOURCE_STATE_COMMON;
    return D3D12_RESOURCE_STATES((usage & ~0x8000u) | ((usage & 0x8000u) ? D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER : 0));
}

}  // namespace effectslayer
