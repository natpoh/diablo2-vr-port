// flatvr_feed.h - [render] flatvr_feed: without ReShade ([render] no_reshade) FlatVR gets the game's picture and depth from the plugin
// itself, through exactly the shared blocks FlatVR's ReShade add-on (FlatVR_DepthProvider, BodyWalk's - the protocol only, not its code)
// publishes them on: the same names, layouts, handles and counters. FlatVR's ReShade source then takes them without knowing who sent them
// - as it does from its own UEVR bridge, a second producer of the same blocks. Serg, 2026-10-31: "why the depth map - we can extract it,
// it only needs sending to FlatVR". Before this, FlatVR on no_reshade said "No picture from the ReShade addon" and showed nothing.
//
// What goes, per present of the game's window (before the window gets it - monitor_eye covers it after):
//   Local\BodyWalkVR_ShareStatus  {struct_bytes, device_api 0xC000 (D3D12), share_kind 1 (NT handle), pid}: made at once, so FlatVR sees a
//                                 producer is there;
//   Local\BodyWalkVR_ColourFrame  {w, h, format, frame_counter, pid, nt_handle}: the back buffer in a shared texture of its typeless format;
//                                 in stereo (FlatVRAfrEye says which eye a present carries, the add-on's own rule: eye ^ swap, while its
//                                 counter moved within 500 ms) each eye goes into its half of a double-wide picture and only whole pairs
//                                 are handed over (the same eye twice: that pair is started again);
//   Local\BodyWalkVR_AfrHalves    the view stamps of what each half holds (FlatVR places the screen at that pose);
//   Local\BodyWalkVR_DepthMap     the scene's depth (vrcam's own copy, scenedepth) in a shared texture of its typeless format, the colour's
//                                 counter on it: FlatVR's 3D from the depth.
// The shared textures: a default heap with D3D12_HEAP_FLAG_SHARED, ALLOW_SIMULTANEOUS_ACCESS (another device reads them while the game's
// queue writes - without it a render target kept compressed and the reader faulted, FlatVR's bridge found), left in COMMON; an NT handle
// (CreateSharedHandle, GENERIC_ALL) FlatVR duplicates from this process. A texture of an old size is retired kRetireFrames later, its handle
// with it (the GPU on either side may still read it). Not done here (the add-on's own options, off or optional there): the colour ring,
// the present stamp strip, the depth's conversion to R32_FLOAT (FlatVR reads the typeless depth as it is).
#pragma once

#include <cstddef>
#include <cstdint>

struct ID3D12Device;
struct ID3D12CommandQueue;
struct ID3D12Resource;
struct FlatVRAfrEye;

namespace flatvrfeed {

// ---- the wire (mirrors of FlatVR_DepthProvider's blocks; FlatVR compares the layout with its own copy) ----
constexpr const char* kColourName = "Local\\BodyWalkVR_ColourFrame";
constexpr const char* kDepthName = "Local\\BodyWalkVR_DepthMap";
constexpr const char* kStatusName = "Local\\BodyWalkVR_ShareStatus";
constexpr const char* kHalvesName = "Local\\BodyWalkVR_AfrHalves";

struct SharedTexture {   // the colour block and the depth block alike
    uint32_t width;
    uint32_t height;
    uint32_t format;          // the texture's DXGI_FORMAT as FlatVR should view it (it maps the typeless and sRGB ones itself)
    uint32_t frame_counter;
    uint32_t source_pid;
    uint64_t nt_handle;       // an NT handle in source_pid; 0 = none (FlatVR waits)
};
static_assert(sizeof(SharedTexture) == 32 && offsetof(SharedTexture, nt_handle) == 24, "SharedTexture is a wire format");

struct ShareStatus {
    uint32_t struct_bytes;
    uint32_t device_api;      // ReShade's numbering: 0xC000 D3D12
    uint32_t share_kind;      // 1 = NT handle
    uint32_t source_pid;
};
static_assert(sizeof(ShareStatus) == 16, "ShareStatus is a wire format");
constexpr uint32_t kApiD3D12 = 0xC000, kShareNtHandle = 1;

#pragma pack(push, 4)
struct AfrHalves {        // FlatVRAfrHalves (afr_eye_shared.h)
    uint32_t version;         // 1
    uint32_t frame_counter;   // the colour block's counter it was written with
    uint32_t stamp[2];        // each half's view stamp
    uint32_t valid;           // bit 0 / 1: that half has a stamp
    uint32_t reserved[3];
};
#pragma pack(pop)
static_assert(sizeof(AfrHalves) == 32, "AfrHalves is a wire format");

// ---- pure: which eye a present carries, and the pair being filled (tools/flatvr_feed_test.cpp) ----
// The add-on's rule on the mod's eye block: -1 (one picture, not a pair) unless its version is 1, its frame counter moved within the last
// 500 ms (a block left by a mod that is gone keeps its last words) and it says enabled; then eye ^ swap.
struct EyeRule {
    bool seen = false;
    uint32_t last = 0;
    unsigned long long movedAt = 0;
    bool alive = false;   // its counter moved lately (the single picture's stamps need it too)
    int Eye(uint32_t version, uint32_t enabled, uint32_t eye, uint32_t frame, uint32_t swap, unsigned long long nowMs) {
        if (version != 1u) { alive = false; return -1; }
        if (!seen) { seen = true; last = frame; }
        else if (frame != last) { last = frame; movedAt = nowMs; }
        alive = movedAt != 0 && nowMs - movedAt <= 500;
        if (!alive || !enabled) return -1;
        return (int)((eye ^ swap) & 1u);
    }
};
// A pair's halves: true when this eye made it whole (then it starts again); the same eye twice before the other loses that pair.
struct PairFill {
    uint32_t mask = 0;
    bool Add(int eye) {
        const uint32_t bit = 1u << (eye & 1);
        if (mask & bit) mask = 0;
        mask |= bit;
        if (mask != 3u) return false;
        mask = 0;
        return true;
    }
};

// ---- the producer ----
void SetLog(void (*log)(const char*));

// The present of `bb` (in PRESENT) on the game's queue `q`, with its picture final (our effects drawn, the window not covered yet).
// eyeBlock: the mod's FlatVRAfrEye (nullptr: one picture). depth: our copy of the scene's depth in `depthState` (nullptr: none),
// depthFresh: copied for this present. Records on our own list on `q` (natived3d::OpenList / Submit).
void Present(ID3D12Device* dev, ID3D12CommandQueue* q, ID3D12Resource* bb, const FlatVRAfrEye* eyeBlock, ID3D12Resource* depth,
             unsigned depthState, bool depthFresh);
// Native OpenXR took the headset, or the key went off: the blocks say "no picture" (handles 0) - FlatVR shows nothing stale.
void Idle();

struct Stats {
    uint32_t presents = 0, pairs = 0, singles = 0, depths = 0, halfLost = 0;
    bool colour = false, depthOk = false;   // a shared texture made
};
Stats TakeStats();

constexpr uint32_t kRetireFrames = 16;

}  // namespace flatvrfeed
