// video3d.h - [cinema] video_3d: the game's cinematics in 3D on the big screen ([cinema] world_screen), every VR view.
//
// Each flat frame while a film plays, xr.cpp hands this the game's back buffer and where the film is in it; this records, on xr.cpp's
// own command list, the film for the left eye into the left half of a render target and for the right eye into the right half:
//   - the film is drawn down to the depth network's input and copied into a readback buffer (when the worker can take one);
//   - a worker thread waits for that list, runs the network (depth_net.h: ZipDepth-base by Fabio Tosi, MIT, on ONNX Runtime + DirectML
//     loaded at run time from d2rloader\d2r_vr_libs), makes the map steady (steady_depth.h) and leaves it in an upload buffer;
//   - the newest map is copied into a depth texture and each eye is drawn from it (video3d_shader.h).
// Nothing waits on the GPU on the game's thread; the depth is a frame or two behind the picture, which a 24 fps film in a 72-120 Hz
// headset does not show. Until the first map (and whenever the runtime or the network is missing) both eyes are the flat film.
//
// Our own code (D2R VR, MIT). It came from the closed d2r_vr_video3d.dll of 0.224, opened 2026-11-26: nothing of FlatVR or BodyWalk in it.
#pragma once

#include <cstdint>

struct ID3D12Device;
struct ID3D12CommandQueue;
struct ID3D12GraphicsCommandList;

namespace video3d {

using LogFn = void (*)(const char*);
void SetLog(LogFn fn);
// Tests: the folder with onnxruntime.dll, DirectML.dll and the network (ending in a backslash), instead of d2rloader\d2r_vr_libs; null: there again.
void SetRuntimeFolder(const wchar_t* dir);

enum : uint32_t { kSrcLinear = 0, kSrcGamma = 1 };   // what sampling the source's view gives: linear light, or gamma-encoded values

struct Frame {
    void* src = nullptr;              // ID3D12Resource*: the game's back buffer this present
    uint32_t srcState = 0;            // D3D12_RESOURCE_STATES it is in; left there again
    uint32_t srcViewFormat = 0;       // DXGI_FORMAT to view it with (not typeless)
    uint32_t srcEncoding = kSrcLinear;
    uint32_t filmX = 0, filmY = 0, filmW = 0, filmH = 0;   // the film's pixels in the source
    uint64_t rtv = 0;                 // D3D12_CPU_DESCRIPTOR_HANDLE.ptr of the output (RENDER_TARGET state, left there)
    uint32_t rtvFormat = 0;           // DXGI_FORMAT of that view; an _SRGB one takes linear light
    uint32_t outW = 0, outH = 0;      // left eye in [0, outW/2), right eye in [outW/2, outW)
    float strengthPct = 2.0f;         // the largest separation between the eyes, % of the film's width
    float convergence = 0.4f;         // 0..1: the depth that lands on the screen (0 far, 1 near)
};

// Ready for this device and queue: the pipeline now, the network on the worker (Record draws flat until it is ready). Once per device;
// a device it failed on is not tried again. True when the pipeline is there.
bool Start(ID3D12Device* device, ID3D12CommandQueue* queue);
// Record this frame's drawing on the open list. 1: the eyes differ (a depth was ready); 0: drawn flat; -1: nothing recorded.
int Record(ID3D12GraphicsCommandList* list, const Frame& frame);
// The list Record recorded on has gone to the queue: our fence is signalled behind it.
void Submitted();
// A new film: forget the last one's depth.
void Reset();
// The device goes: the worker stopped, our work on the device waited for (2 s at most), everything of it freed.
void Stop();

}  // namespace video3d
