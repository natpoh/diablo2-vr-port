// turn_warp.h - eyes by turns: the eye not drawn this frame drawn on the GPU from pictures already drawn (the maths:
// vr/turn_math.h; who calls it: vr/xr.cpp TurnEye). Into that eye's swapchain image, in three passes on one list:
//   1. its own last picture turned to this frame's head (a homography: rotation only) - what shows where nothing else does;
//   2. the same picture moved with the depth it was drawn with: a grid of kGridStep pixels, each point put back at its
//      distance and seen from where the eye is now (what the hero carries goes with him), nearer over farther;
//   3. the eye drawn this frame moved across with its own depth, over it - so what moves by itself (monsters, missiles)
//      stands where that eye shows it; a cell across an edge of its depth is cut, and pass 2 shows through there.
// With [stereo] turn_overlay_depth (off by default) the depth is moved the same way, into MadeDepth(): the eye made's own scene depth
// (reverse-Z, near / distance, 0 where only the turn reached), so what is drawn over it afterwards (D2R_DepthFog.fx's overlays: the
// wrist orbs, the bow's) is hidden by what is nearer there. It is only as good as the grid: round the hands - where the medallions sit -
// it is the drawn eye's depth at 4-pixel cells, cut at edges and the eye's older picture under it, and the medallions came out doubled
// with it (the author, 2026-10-20); off, nothing hides an overlay in the eye made.
// The technique and the shaders' shape are bearstonem/d2r-pcvr's (MIT, steamvr/turn_warp.cpp); written afresh here.
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
    float edgeScale = 0.0f;            // > 0: cells across an edge are cut (EdgeScale) - the eye drawn this frame
};

constexpr int kSlots = 8;   // descriptor sets, a ring: a set is written again only after the list that read it is done (the caller's fence)

class Gpu {
public:
    // target: the eye swapchain's format (its RTV); made again for another. False: no warp (logged once).
    bool Init(ID3D12Device* dev, DXGI_FORMAT target, void (*log)(const char*));
    bool Ready() const { return pso_[0] != nullptr; }
    bool DepthReady() const { return depthPso_[0] != nullptr; }
    // The made eye's depth (R32_FLOAT, the target's size; PIXEL_SHADER_RESOURCE | NON_PIXEL_SHADER_RESOURCE after Render), nullptr before
    // the first Render with madeDepth. MadeDepthGen: counts the textures made, so a binding of it is made again for a new one at the same address.
    ID3D12Resource* MadeDepth() const { return madeDepth_; }
    uint32_t MadeDepthGen() const { return madeGen_; }
    // The made depth far everywhere (nothing hides an overlay): for a frame whose eye was not made by Render (its last picture as it was).
    void ClearMadeDepth(ID3D12GraphicsCommandList* list);
    DXGI_FORMAT Format() const { return format_; }
    // Into target (RENDER_TARGET, w x h): pass 1 from `base` with `homography`, then `own` (pass 2) and `drawn` (pass 3) when
    // given. `slot` 0..kSlots-1: the descriptor set this list uses. madeDepth: the depth moved the same way into MadeDepth() too
    // ([stereo] turn_overlay_depth); false: the picture alone, one target (as before the made depth).
    bool Render(ID3D12GraphicsCommandList* list, ID3D12Resource* target, uint32_t w, uint32_t h, ID3D12Resource* base,
                const std::array<float, 9>& homography, const Grid* own, const Grid* drawn, int slot, bool madeDepth);
    void Release();   // the device's work on it is done (the caller waited)

private:
    ID3D12Device* dev_ = nullptr;
    ID3D12RootSignature* root_ = nullptr;
    ID3D12RootSignature* depthRoot_ = nullptr;
    ID3D12PipelineState* pso_[2] = {};        // [0] the picture alone, [1] the picture and the made depth
    ID3D12PipelineState* depthPso_[2] = {};
    ID3D12DescriptorHeap* srv_ = nullptr;   // shader-visible, kSlots sets of 4: base, own image + depth, drawn image + depth (the base is pass 2's image)
    ID3D12DescriptorHeap* rtv_ = nullptr;
    ID3D12DescriptorHeap* dsv_ = nullptr;
    ID3D12Resource* depthTarget_ = nullptr;   // the grids' own depth test, the target's size
    ID3D12Resource* madeDepth_ = nullptr;     // the made eye's depth as a picture (MadeDepth), the target's size
    uint32_t madeGen_ = 0;
    uint32_t depthW_ = 0, depthH_ = 0;
    UINT srvInc_ = 0;
    DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
    bool tried_ = false;
};

}  // namespace turnwarp
