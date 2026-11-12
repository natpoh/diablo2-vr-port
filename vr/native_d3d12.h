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
struct IDXGISwapChain;
struct D3D12_RESOURCE_BARRIER;
struct D3D12_CPU_DESCRIPTOR_HANDLE;
struct D3D12_VIEWPORT;
struct tagRECT;
struct D3D12_GRAPHICS_PIPELINE_STATE_DESC;
struct D3D12_PIPELINE_STATE_STREAM_DESC;
struct D3D12_COMPUTE_PIPELINE_STATE_DESC;
struct ID3D12PipelineState;
struct ID3D12DescriptorHeap;
struct ID3D12RootSignature;
struct D3D12_SHADER_RESOURCE_VIEW_DESC;

namespace natived3d {

using LogFn = void (*)(const char*);
struct RootCall;

struct Sinks {
    // The game's swap chain presents (not a test present): before the real Present, on the presenting thread.
    void (*present)(IDXGISwapChain3* sc, ID3D12CommandQueue* q) = nullptr;
    // Its buffers are about to be made again (ResizeBuffers / ResizeBuffers1), or a new swap chain was made.
    void (*swapChainChanged)() = nullptr;
    // ([render] resize_drain) the game's ResizeBuffers / ResizeBuffers1 on its swap chain, before the real call and before swapChainChanged:
    // its old buffers are still alive here (the plugin drains the GPU of what still names them, vr/resize_drain.h).
    void (*beforeResize)() = nullptr;
    // ([render] apply_keep_size, vr/apply_keep_size.h) asked first, on the game's swap chain only: true = this resize is left out - the real call
    // is not made, S_OK goes back, nothing is freed (and neither beforeResize nor swapChainChanged come).
    bool (*keepSize)(IDXGISwapChain* sc, unsigned n, unsigned w, unsigned h, unsigned format, unsigned flags) = nullptr;
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
    // (`dsv`: the depth-stencil view's handle as bound, nullptr none - the effects' depth binds the game's targets again after its draw)
    void (*fxTargets)(ID3D12GraphicsCommandList* cl, unsigned n, const D3D12_CPU_DESCRIPTOR_HANDLE* rts, bool single, ID3D12Resource* ds,
                      const D3D12_CPU_DESCRIPTOR_HANDLE* dsv) = nullptr;
    void (*fxBarrier)(ID3D12GraphicsCommandList* cl, unsigned n, const D3D12_RESOURCE_BARRIER* b) = nullptr;
    void (*fxBound)(ID3D12GraphicsCommandList* cl, ID3D12PipelineState* pso) = nullptr;
    void (*fxHeaps)(ID3D12GraphicsCommandList* cl, unsigned n, ID3D12DescriptorHeap* const* heaps) = nullptr;
    void (*fxRoot)(ID3D12GraphicsCommandList* cl, const RootCall& call) = nullptr;
    // (a draw's whole arguments: the effects' depth draws it again; `indexed` DrawIndexedInstanced, `base` its base vertex)
    void (*fxDraw)(ID3D12GraphicsCommandList* cl, bool indexed, unsigned vertices, unsigned instances, unsigned first, int base,
                   unsigned firstInstance) = nullptr;
    // ... and an ExecuteIndirect of the game's (its arguments not known: the hair and foliage draws' targets swapped for it all the same)
    void (*fxIndirect)(ID3D12GraphicsCommandList* cl) = nullptr;
    // [debug] skip_pipeline / patch_hunt 8, Ctrl+F10's pipe trace (vr/pipe_hunt.h): a pipeline the game binds (SetPipelineState, the one it asked
    // for - before psoSwap; a Reset's, nullptr none), and each draw / dispatch / indirect call of the game's (kind 0 / 1 / 2): true = not made.
    // Given: SetPipelineState, DrawInstanced, Dispatch and ExecuteIndirect are hooked as well.
    void (*huntBound)(ID3D12GraphicsCommandList* cl, ID3D12PipelineState* pso) = nullptr;
    bool (*huntSkip)(ID3D12GraphicsCommandList* cl, int kind) = nullptr;
    // ... and, for the trace's columns, what a list binds: its render targets (the RTV handles as given) and depth-stencil (its resource, nullptr
    // none or not known), its first viewport
    void (*huntTargets)(ID3D12GraphicsCommandList* cl, unsigned n, const D3D12_CPU_DESCRIPTOR_HANDLE* rts, bool single, ID3D12Resource* ds) = nullptr;
    void (*huntViewport)(ID3D12GraphicsCommandList* cl, float w, float h) = nullptr;
    // the capture's intermediate targets (vr/capture.h): a list's barriers before they go in - the sink may record on the list first
    void (*captureBarrier)(ID3D12GraphicsCommandList* cl, unsigned n, const D3D12_RESOURCE_BARRIER* b) = nullptr;
    // [render] foveated (vr/foveation.h), each only from the game (never our own calls): a graphics pipeline it made (from a description, from a
    // stream; after the real call), one a list binds (the game's own, before a swap of ours); what a list binds as targets - before the real
    // call: the first colour target's resource and the depth-stencil's (nullptr: none, not known, or the plugin's own), how many colour
    // targets; its first viewport; a Reset or a list made (a new recording); before each draw (DrawInstanced, DrawIndexedInstanced) and each
    // ExecuteIndirect. The hooks these need beyond the ones above go in only when they are given.
    void (*vrsGraphicsPso)(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc, ID3D12PipelineState* pso) = nullptr;
    void (*vrsStreamPso)(const D3D12_PIPELINE_STATE_STREAM_DESC* desc, ID3D12PipelineState* pso) = nullptr;
    void (*vrsBound)(ID3D12GraphicsCommandList* cl, ID3D12PipelineState* pso) = nullptr;
    void (*vrsTargets)(ID3D12GraphicsCommandList* cl, ID3D12Resource* colour0, ID3D12Resource* depth, unsigned n) = nullptr;
    void (*vrsViewport)(ID3D12GraphicsCommandList* cl, const D3D12_VIEWPORT* vp) = nullptr;
    void (*vrsReset)(ID3D12GraphicsCommandList* cl) = nullptr;
    void (*vrsDraw)(ID3D12GraphicsCommandList* cl) = nullptr;
    void (*vrsIndirect)(ID3D12GraphicsCommandList* cl) = nullptr;
    // [stereo] native_replay (vr/native_replay.h): the game's buffers in CPU-visible memory (an upload heap, or a custom heap the CPU writes),
    // as ReShade's init_resource / destroy_resource gave them to camfix - the camera's constants are read and copied from there. Made: after
    // the real call (CreateCommittedResource, CreatePlacedResource and their later versions), mapped by us for good (`cpu` = its first byte,
    // `va` its GPU address, `size` its bytes); gone: when D3D12 destroys it (a private-data object of ours on it is let go) - `r` is then only a
    // key, never to be called. Given: the device's resource creation is hooked as well. Any thread.
    void (*uploadMade)(ID3D12Resource* r, uint64_t va, uint64_t size, uint8_t* cpu) = nullptr;
    void (*uploadGone)(ID3D12Resource* r) = nullptr;
    // [stereo] replay_alloc_guard (vr/render/stereo/replay_alloc_guard.h): in front of the game's ID3D12CommandAllocator::Reset, any list type, any
    // thread - the replay waits there while its replay of lists recorded in that allocator still runs on the GPU. Given: hooked with the resets.
    void (*allocReset)(ID3D12CommandAllocator* a) = nullptr;
    // [render] shadows_once_a_pair (vr/shadows_once.h): everything a list of the game's WRITES, every list type, each before the real call (only the
    // game's, never our own): a new recording (Reset, or a list made - any type); a bind (each colour target's resource and the depth-stencil's,
    // nullptr none; `unknown`: a view not seen made, or the plugin's own); a clear, copy, resolve or discard into `dst` (a buffer too); a draw
    // (kind 0), a dispatch (1), an indirect call (2), another kind of write (3: a bundle, CopyTiles, an atomic copy, an immediate buffer write, a
    // render pass, a meta command, rays, a mesh dispatch, an enhanced barrier); the barriers. Given: those functions are hooked as well.
    void (*soReset)(ID3D12GraphicsCommandList* cl) = nullptr;
    void (*soTargets)(ID3D12GraphicsCommandList* cl, unsigned n, ID3D12Resource* const* rts, ID3D12Resource* ds, bool unknown) = nullptr;
    void (*soWrite)(ID3D12GraphicsCommandList* cl, ID3D12Resource* dst) = nullptr;
    void (*soWork)(ID3D12GraphicsCommandList* cl, int kind) = nullptr;
    void (*soBarrier)(ID3D12GraphicsCommandList* cl, unsigned n, const D3D12_RESOURCE_BARRIER* b) = nullptr;
    // [render] lens_mask (vr/lens_mask.h): a list of the game's cleared a depth-stencil - AFTER the real ClearDepthStencilView (the sink may
    // record on the list right behind it, in the game's order): its view's handle as given, its resource (nullptr: a view not seen made; the
    // plugin's own are never told), the flags, the values and the rectangles as the game gave them. Given: ClearDepthStencilView is hooked.
    void (*clearDsv)(ID3D12GraphicsCommandList* cl, uint64_t dsv, ID3D12Resource* ds, unsigned flags, float depth, uint8_t stencil,
                     unsigned nRects, const tagRECT* rects) = nullptr;
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
// runs it on q. One thread at a time (the present's). What it records does not reach the sinks. Ours() stays true through Submit's
// ExecuteCommandLists, so a look at the queue's submissions ([stereo] native_replay's capture) can leave ours out.
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
// [sky] game_sky: tell the log when the game makes a shader-resource view of the domes' picture (shared/sky_atlas.h IsAtlasTexture) - the
// place a texture of ours could stand in for it, so the game draws the dome in our colours itself. Looked at only while on.
void WatchDomeAtlas(bool on);
// [sky] dome_in_game_colour (vr/sky/dome_atlas.h StandIn): each view the game makes of the domes' picture, while watched, handed to `fn` first
// with the real CreateShaderResourceView - true: `fn` made a view of its own in that slot, the game's is not made. nullptr: none.
using CreateSrvFn = void(__stdcall*)(ID3D12Device*, ID3D12Resource*, const D3D12_SHADER_RESOURCE_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
using AtlasStandInFn = bool (*)(ID3D12Device*, ID3D12Resource*, const D3D12_SHADER_RESOURCE_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE, CreateSrvFn);
void SetAtlasStandIn(AtlasStandInFn fn);
ID3D12Resource* SrvResource(uint64_t handle);
unsigned SrvIncrement();
// Our recording on a list of the game's (the stand-in for ReShade's command list, vr/native_rsapi.h): what it calls does not reach the sinks,
// as ReShade does not report an add-on's own calls. On this thread, until set back.
void SetOurs(bool ours);
// The native replay's camera hooks (camfix, vr/render/depth/depth_fix.cpp) on list functions hooked here already - [fog] game_fog_before_fx's
// root signature / compute root CBV calls (MinHook takes one hook a function: until 0.209.9 camfix's did not go in and the replay was skipped
// for the session, two passes a pair). HoldsHook: a function hooked here. SetRootChain: camfix's handlers, run first inside these hooks on
// every call (as its own hooks ran): rootSig notes the list's root signature (which 0 graphics, 1 compute); cbv gives the address to bind
// (0: the game's), and that one is what the fog step's sinks and the game's function see.
bool HoldsHook(void* fn);
struct RootChain {
    void (*rootSig)(ID3D12GraphicsCommandList* cl, ID3D12RootSignature* rs, int which) = nullptr;
    uint64_t (*cbv)(ID3D12GraphicsCommandList* cl, unsigned param, uint64_t va, int which) = nullptr;
};
void SetRootChain(const RootChain& c);
bool Ours();

// [sky] game_sky: the game's indexed draws of exactly `indexCount` indices counted (the dome's mesh has a count of its own: 5952), 0 none
// counted (the draw hook then does one compare). TakeWatched: draws and their instances since the last call.
void WatchIndexCount(uint32_t indexCount);
void TakeWatched(uint32_t* draws, uint32_t* instances);

// [stereo] native_replay: the CPU-visible buffers told to uploadMade so far, and the ones told gone (the 10 s line; the tests)
void UploadCounts(uint32_t* made, uint32_t* gone);

// [debug] call_census (docs/notes/vr_cpu_overhead.md): the game's own calls through these hooks, counted per recording thread while on (live;
// view only). TakeCensus: the calls since the last take, those of `drawThread` (the game's draw / present thread) apart from all the others',
// and how many threads recorded. Only the functions hooked in this run are counted (DrawInstanced, Dispatch, ExecuteIndirect and
// SetPipelineState: hooked whenever the pipe hunt's sinks are given - always in the game).
enum CensusKind : int {
    kCensusReset = 0, kCensusDrawIndexed, kCensusDraw, kCensusDispatch, kCensusIndirect, kCensusPso, kCensusTargets, kCensusBarrierCalls,
    kCensusBarriers, kCensusViewports, kCensusScissors, kCensusClearRtv, kCensusClearDsv, kCensusKinds
};
struct Census {
    uint32_t draw[kCensusKinds] = {}, other[kCensusKinds] = {};
    int threads = 0;
    bool lost = false;   // more recording threads than slots: some not counted
};
void SetCensus(bool on);
bool CensusOn();
Census TakeCensus(unsigned long drawThread);

}  // namespace natived3d
