// native_d3d12.h - the plugin's own D3D12 / DXGI hooks for [render] no_reshade (docs/plan_no_reshade.md, step 3): native OpenXR eyes by turns
// with no ReShade64.dll in the game. What ReShade's add-on events gave vrcam for that path is taken from the functions themselves:
//   - the game's swap chain: its presents (before the real Present, as ReShade's reshade_present) and its buffers made again (ResizeBuffers);
//     found by hooking the DXGI factory's CreateSwapChain / CreateSwapChainForHwnd, whose pDevice is the game's queue under D3D12;
//   - the game's device (D3D12CreateDevice): its depth-stencil views, so a bound DSV handle says which resource it is;
//   - on the command lists: the depth-stencil bound (OMSetRenderTargets), the barriers (ResourceBarrier) and the first viewport (RSSetViewports) -
//     what vrcam's scenedepth / renderscale count to pick the scene's depth, which the eye made is moved with (turn_warp).
// Our own command list on the game's queue for the depth's copy (OpenList / Submit). The plugin's own depth-stencils (resources named "d2rvr ...",
// turn_warp's) are left out of the map: their binds never reach the sinks. All of it is installed only in that mode; with ReShade nothing here runs.
// Under the D3D12 debug layer ([debug] d3d12_validation) each list call goes through two hooked functions - the layer's list and the runtime's
// list it forwards to: only the outer one reaches the sinks (the game's own list pointer, the layer's).
#pragma once

#include <cstdint>

struct ID3D12Device;
struct ID3D12CommandQueue;
struct ID3D12GraphicsCommandList;
struct ID3D12Resource;
struct IDXGISwapChain3;
struct D3D12_RESOURCE_BARRIER;
struct D3D12_CPU_DESCRIPTOR_HANDLE;
struct D3D12_VIEWPORT;
struct tagRECT;
struct D3D12_GRAPHICS_PIPELINE_STATE_DESC;
struct D3D12_PIPELINE_STATE_STREAM_DESC;
struct D3D12_COMPUTE_PIPELINE_STATE_DESC;
struct ID3D12PipelineState;
struct ID3D12DescriptorHeap;

namespace natived3d {

using LogFn = void (*)(const char*);
struct RootCall;

struct Sinks {
    // The game's swap chain presents (not a test present): before the real Present, on the presenting thread.
    void (*present)(IDXGISwapChain3* sc, ID3D12CommandQueue* q) = nullptr;
    // Its buffers are about to be made again (ResizeBuffers / ResizeBuffers1), or a new swap chain was made.
    void (*swapChainChanged)() = nullptr;
    // A list of the game's binds render targets: its depth-stencil's resource (nullptr: none, or a view not seen made) and how many colour targets.
    void (*bindDepth)(ID3D12GraphicsCommandList* cl, ID3D12Resource* ds, unsigned colourTargets) = nullptr;
    // A list's barriers (all kinds; the sink looks at the transitions).
    void (*barrier)(ID3D12GraphicsCommandList* cl, unsigned n, const D3D12_RESOURCE_BARRIER* b) = nullptr;
    // A list's first viewport (w x h).
    void (*viewport)(ID3D12GraphicsCommandList* cl, float w, float h) = nullptr;
    // The game's interface ([hud], vr/hud.cpp through the stand-in for ReShade's API, vr/native_rsapi.h) - before the real call, as ReShade's
    // events: a list of the game's clears a render target; binds its render targets (all of them: `single` - one handle, n in a row); sets
    // its scissors; its viewports; is reset or made (a new recording).
    void (*clearRtv)(ID3D12GraphicsCommandList* cl, const D3D12_CPU_DESCRIPTOR_HANDLE* rtv, const float* colour, unsigned n, const tagRECT* rects) = nullptr;
    void (*targets)(ID3D12GraphicsCommandList* cl, unsigned n, const D3D12_CPU_DESCRIPTOR_HANDLE* rts, bool single) = nullptr;
    void (*scissors)(ID3D12GraphicsCommandList* cl, unsigned n, const tagRECT* rects) = nullptr;
    void (*viewports)(ID3D12GraphicsCommandList* cl, unsigned n, const D3D12_VIEWPORT* vps) = nullptr;
    void (*listReset)(ID3D12GraphicsCommandList* cl) = nullptr;
    // [fog] game_fog_height (vr/fog_height.h): a graphics pipeline the game made (after the real call, made), and one made from a stream
    // (Device2::CreatePipelineState); a pipeline the game binds - the one returned is bound instead (the same: as it was).
    void (*graphicsPso)(ID3D12Device* dev, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc, ID3D12PipelineState* pso) = nullptr;
    void (*streamPso)(ID3D12Device* dev, const D3D12_PIPELINE_STATE_STREAM_DESC* desc, ID3D12PipelineState* pso) = nullptr;
    ID3D12PipelineState* (*psoSwap)(ID3D12PipelineState* pso) = nullptr;
    // [render] light_bounds_fix without ReShade (vr/light_bounds.h): a compute pipeline the game made from a description (after the real call, made)
    void (*computePso)(ID3D12Device* dev, const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc, ID3D12PipelineState* pso) = nullptr;
    // [fog] game_fog_before_fx (vr/fog_before_fx.h): what a list of the game's binds, for the fog step recorded before its transparent resolve -
    // each before the real call (the pipeline: after it, the one bound). The hooks for the root arguments, the heaps and DrawInstanced go in
    // only when these are given.
    void (*fxReset)(ID3D12GraphicsCommandList* cl) = nullptr;
    void (*fxTargets)(ID3D12GraphicsCommandList* cl, unsigned n, const D3D12_CPU_DESCRIPTOR_HANDLE* rts, bool single, ID3D12Resource* ds) = nullptr;
    void (*fxBarrier)(ID3D12GraphicsCommandList* cl, unsigned n, const D3D12_RESOURCE_BARRIER* b) = nullptr;
    void (*fxBound)(ID3D12GraphicsCommandList* cl, ID3D12PipelineState* pso) = nullptr;
    void (*fxHeaps)(ID3D12GraphicsCommandList* cl, unsigned n, ID3D12DescriptorHeap* const* heaps) = nullptr;
    void (*fxRoot)(ID3D12GraphicsCommandList* cl, const RootCall& call) = nullptr;
    void (*fxDraw)(ID3D12GraphicsCommandList* cl, unsigned vertices, unsigned instances) = nullptr;
};
// A root argument set on a list (Sinks::fxRoot): the compute root signature and arguments (given back after a dispatch of ours), and the
// graphics descriptor tables (set again after our heaps were bound)
struct RootCall {
    enum Kind { kComputeRootSignature, kComputeTable, kComputeConstant, kComputeConstants, kComputeCbv, kComputeSrv, kComputeUav, kGraphicsTable,
                kGraphicsRootSignature };
    Kind kind;
    unsigned index = 0;      // the root parameter
    uint64_t value = 0;      // the root signature (a pointer), a table's GPU handle, a view's GPU address, one constant
    unsigned n = 0, offset = 0;   // constants: how many, from which
    const void* data = nullptr;   // ... their values
};

// At the plugin's load, before the game makes its device and swap chain (MinHook initialised). False: a hook did not go in (said in the log).
bool Install(const Sinks& sinks, LogFn log);

// For the once-in-a-while line: since the last call.
struct Stats {
    uint32_t presents = 0;        // the game's presents seen
    uint32_t depthBinds = 0;      // OMSetRenderTargets with a depth-stencil of the game's
    uint32_t unknownViews = 0;    // ... with a depth-stencil view made before the hook (not resolved)
    uint32_t views = 0;           // depth-stencil views known now
    bool device = false, swapChain = false;
    uint32_t lists = 0;           // our lists run (OpenList / Submit)
    uint32_t listWaits = 0;       // ... of them waited for the graphics card to free an allocator
    double listWaitMs = 0.0;      // ... that long in all
};
Stats TakeStats();

// Our own list on the game's queue: an open list (nullptr: no allocator free within 100 ms, or none could be made), then Submit closes it and
// runs it on q. One thread at a time (the present's). What it records does not reach the sinks.
ID3D12GraphicsCommandList* OpenList(ID3D12Device* dev);
void Submit(ID3D12CommandQueue* q);

// The game's queue (its swap chain's), nullptr before it: no reference of ours, used while the game holds it
ID3D12CommandQueue* GameQueue();

// The resource a render-target view of the game's device was made for (its CPU handle; nullptr: not seen made), and the RTV heaps' step.
ID3D12Resource* RtvResource(uint64_t handle);
unsigned RtvIncrement();
// [fog] game_fog_before_fx: the game's shader-resource views of R11G11B10 2D textures (the scene its transparent resolve reads), by the CPU
// handle they were made or copied into - only while asked (TrackSceneViews; the hook then looks at every view made and copied). nullptr:
// not one of them. The pointer may name a resource since freed: compare it, never call it. SrvIncrement: the CBV/SRV/UAV heaps' step.
void TrackSceneViews(bool on);
ID3D12Resource* SrvResource(uint64_t handle);
unsigned SrvIncrement();
// Our recording on a list of the game's (the stand-in for ReShade's command list, vr/native_rsapi.h): what it calls does not reach the sinks,
// as ReShade does not report an add-on's own calls. On this thread, until set back.
void SetOurs(bool ours);
bool Ours();

// [sky] game_sky: the game's indexed draws of exactly `indexCount` indices counted (the dome's mesh has a count of its own: 5952), 0 none
// counted (the draw hook then does one compare). TakeWatched: draws and their instances since the last call.
void WatchIndexCount(uint32_t indexCount);
void TakeWatched(uint32_t* draws, uint32_t* instances);

}  // namespace natived3d
