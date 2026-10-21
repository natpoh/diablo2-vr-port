// turn_warp.h - eyes by turns: the eye not drawn this frame drawn on the GPU from pictures already drawn (the maths:
// vr/turn_math.h; who calls it: vr/xr.cpp TurnEye). Into that eye's swapchain image, in three passes on one list:
//   1. its own last picture turned to this frame's head (a homography: rotation only) - what shows where nothing else does;
//   2. the same picture moved with the depth it was drawn with: a grid of kGridStep pixels, each point put back at its
//      distance and seen from where the eye is now (what the hero carries goes with him), nearer over farther;
//   3. the eye drawn this frame moved across with its own depth, over it - so what moves by itself (monsters, missiles)
//      stands where that eye shows it. A cell across an edge of its depth goes as two flat layers, each pixel in the one its own
//      depth is nearer (turn_math.h SplitCell): only what the drawn eye could not see behind the edge keeps pass 2's picture.
//      Where pass 2's picture shows the same surface (turn_math.h KeepOwn, [stereo] turns_keep_own) it stays: the game's glow round
//      a blade, hair and its effects layer (Effects) have no depth, and moved across they stood where the drawn eye saw them.
// With [stereo] turn_overlay_hide (on) the depth is moved the same way, into MadeDepth(): the eye made's own scene depth (reverse-Z,
// near / distance, 0 where only the turn reached), written by the same pixel as its colour - so what is drawn over it afterwards
// (D2R_DepthFog.fx's overlays: the wrist orbs, the bow's) is hidden by what that picture shows nearer there. When it was the drawn
// eye's depth at 4-pixel cells cut whole at edges, with the eye's older picture under the cuts, the medallions came out doubled with it
// (the author, 2026-10-20); now pass 3 is to the pixel. Off, nothing hides an overlay in the eye made (each eye's medallions then show
// through its hands every second frame, test-0176).
// The shaders compile once a process and are built on a worker first (Warm): built on the draw thread at the first eye made of a
// session they held it about 0.1-0.2 s (pcvr's measure, 2026-10-20).
// The technique and the shaders' shape are bearstonem/d2r-pcvr's (MIT, steamvr/turn_warp.cpp, v0.3.24); written afresh here.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <array>
#include <cstdint>

#include "turn_math.h"

namespace turnwarp {

// A picture moved with its depth (passes 2 and 3).
struct Grid {
    ID3D12Resource* image = nullptr;   // PIXEL_SHADER_RESOURCE, viewed as Gpu's target format
    ID3D12Resource* depth = nullptr;   // PIXEL_SHADER_RESOURCE | NON_PIXEL_SHADER_RESOURCE
    DXGI_FORMAT depthView = DXGI_FORMAT_UNKNOWN;   // its shader-readable format (R32_FLOAT_X8X24_TYPELESS, R24_UNORM_X8_TYPELESS, ...)
    float scale[2] = {1.0f, 1.0f};     // the part of it the scene was drawn into (an upscaler draws into a corner)
    float nearPlane = 0.0f;            // the projection's near, world units: distance = near / depth
    Tan drawnTan, nowTan;
    std::array<float, 12> move{};      // turnwarp::Move
    std::array<float, 12> carry{};     // turnwarp::Carry (= move when nothing is carried)
    float nearW = 0.0f, farW = 0.0f;   // carried nearer than this, the world's farther (world units); 0, 0: all by move
    float edgeScale = 0.0f;            // > 0: cells across an edge go as two layers (EdgeScale) - the eye drawn this frame
    Keep keep;                         // the eye drawn this frame: pass 2's picture kept where it shows the same surface (KeepOf)
};

constexpr int kSlots = 8;   // descriptor sets, a ring: a set is written again only after the list that read it is done (the caller's fence)

// Builds the passes for `target` on `dev` on a worker of its own (the device held till it is done), once a pair; true once that is
// done - Init then takes a few milliseconds, the compile and the driver's first pipelines behind it. Any thread.
bool Warm(ID3D12Device* dev, DXGI_FORMAT target);

class Gpu {
public:
    // target: the eye swapchain's format (its RTV); made again for another. False: no warp (logged once). retire: a texture of ours
    // made again at another size goes there (the GPU may still read it); nullptr: released at once.
    bool Init(ID3D12Device* dev, DXGI_FORMAT target, void (*log)(const char*), void (*retire)(ID3D12Resource*) = nullptr);
    bool Ready() const { return pso_[0] != nullptr; }
    bool DepthReady() const { return depthPso_[0] != nullptr; }
    bool NearReady() const { return spreadPso_ != nullptr && reducePso_ != nullptr; }
    bool EffectsReady() const { return effectsPso_ != nullptr && NearReady(); }
    // The game's effects layer (effects_layer.h) as `eye` was drawn this frame: its two targets, in `states` and left so, into that
    // eye's blocks, each the strongest of the layer within `reach` of its pixels (Grid::keep's keepEffects). `slot`: as Render's.
    bool Effects(ID3D12GraphicsCommandList* list, ID3D12Resource* const layer[2], const D3D12_RESOURCE_STATES states[2], int eye, float reach, int slot);
    void ForgetEffects(int eye) { if (eye == 0 || eye == 1) effectsKept_[eye] = false; }
    // The made eye's depth (R32_FLOAT, the target's size; PIXEL_SHADER_RESOURCE | NON_PIXEL_SHADER_RESOURCE after Render), nullptr before
    // the first Render with madeDepth. MadeDepthGen: counts the textures made, so a binding of it is made again for a new one at the same address.
    ID3D12Resource* MadeDepth() const { return madeDepth_; }
    uint32_t MadeDepthGen() const { return madeGen_; }
    // The made depth far everywhere (nothing hides an overlay): for a frame whose eye was not made by Render (its last picture as it was).
    void ClearMadeDepth(ID3D12GraphicsCommandList* list);
    DXGI_FORMAT Format() const { return format_; }
    // Into target (RENDER_TARGET, w x h): pass 1 from `base` with `homography`, then `own` (pass 2) and `drawn` (pass 3) when
    // given. `slot` 0..kSlots-1: the descriptor set this list uses. `eye`: the eye made (0 left, 1 right) - its effects blocks and the
    // drawn eye's. madeDepth: the depth moved the same way into MadeDepth() too ([stereo] turn_overlay_hide); false: the picture alone.
    // kept (may be null): whether pass 2's picture was kept where it showed the same surface.
    bool Render(ID3D12GraphicsCommandList* list, ID3D12Resource* target, uint32_t w, uint32_t h, ID3D12Resource* base,
                const std::array<float, 9>& homography, const Grid* own, const Grid* drawn, int slot, bool madeDepth, int eye = 0,
                bool* kept = nullptr);
    void Release();   // the device's work on it is done (the caller waited)

private:
    void Drop(ID3D12Resource*& r);   // retire_ or released
    ID3D12Device* dev_ = nullptr;
    void (*retire_)(ID3D12Resource*) = nullptr;
    ID3D12RootSignature* root_ = nullptr;
    ID3D12RootSignature* depthRoot_ = nullptr;
    ID3D12RootSignature* nearRoot_ = nullptr;
    ID3D12PipelineState* pso_[2] = {};        // [0] the picture alone, [1] the picture and the made depth
    ID3D12PipelineState* depthPso_[2] = {};
    ID3D12PipelineState* reducePso_ = nullptr;    // the nearest depth of each kNearBlock block
    ID3D12PipelineState* spreadPso_ = nullptr;    // ... the nearest within reach of it (across, then down)
    ID3D12PipelineState* effectsPso_ = nullptr;   // the strongest of the effects layer in each block
    ID3D12DescriptorHeap* srv_ = nullptr;   // shader-visible, kSlots sets of kPerSlot (turn_warp.cpp)
    ID3D12DescriptorHeap* rtv_ = nullptr;   // the picture, the made depth, the near blocks, the effects blocks
    ID3D12DescriptorHeap* dsv_ = nullptr;   // pass 2's depth test, pass 3's
    ID3D12Resource* depthTarget_ = nullptr;   // pass 2's depth test, the target's size (R32 typeless: pass 3 reads it, Grid::keep)
    ID3D12Resource* drawnTarget_ = nullptr;   // pass 3's
    ID3D12Resource* ownTurned_ = nullptr;     // the target as pass 2 left it (Grid::keep), the target's size and format
    ID3D12Resource* nears_[3] = {};           // the drawn eye's near blocks, this eye's, one between (R32_FLOAT, a block a texel)
    ID3D12Resource* effects_[4] = {};         // each eye's effects blocks, two between
    bool effectsKept_[2] = {};
    ID3D12Resource* madeDepth_ = nullptr;     // the made eye's depth as a picture (MadeDepth), the target's size
    uint32_t madeGen_ = 0;
    uint32_t depthW_ = 0, depthH_ = 0;
    UINT srvInc_ = 0, rtvInc_ = 0;
    DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
    bool tried_ = false;
};

}  // namespace turnwarp
