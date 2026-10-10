#pragma once
// Native OpenXR (docs/plan_native_openxr.md): the game shows its own stereo pair in
// the headset as an OpenXR app, instead of handing it to FlatVR. A prototype behind
// [openxr] on=0: first person (F4) with "Stereo - one pass, two pictures" (the
// replay); outside it each present goes to the headset as a flat quad.
//
// Everything here runs on the game's draw thread except Start's worker and
// Shutdown. vrcam wraps every call that may touch the game's queue in its own
// "ours" flag, so its queue hooks let the runtime's work through.

#include <cstdint>

struct ID3D12Device;
struct ID3D12CommandQueue;
struct ID3D12Resource;
struct D2RVR_XrInput;   // shared/d2r_vr_shared.h
struct HWND__;

namespace xr {

void SetLogger(void (*log)(const char*));
// The game's window: where the mouse pointer is laid on the flat picture.
void SetWindow(HWND__* wnd);
// [openxr] from d2r_vr.ini, on every change: `on` switched on starts a session at the next
// first-person pair, switched off ends it and gives the headset back to FlatVR.
void LoadSettings(const wchar_t* iniPath);
bool On();

// The eyes for the frame open now, recentred: metres and a rotation in the head's
// frame at the last recentre (x right, y up, z back), and each eye's field of view
// as tangents (left and down negative), narrowed to the window's aspect with
// [openxr] fov_crop=1.
struct Eyes {
    float pos[2][3];
    float quat[2][4];   // x, y, z, w
    float tanL[2], tanR[2], tanU[2], tanD[2];
};

// Before a pair's left pass (F4, F2): replayed, or two passes before the replay is on (dev
// and queue null then: the session's own). True: a frame is open and PairEyes has its views.
bool BeginPair(ID3D12Device* dev, ID3D12CommandQueue* queue);
bool PairEyes(Eyes* out);
// The head's yaw (radians, + turned left) in the views of the last stereo pair opened, from the
// last recentre - what PairEyes turns the eyes by, for the body. False with no session, after a
// flat frame or a pair that did not open, or when the last pair is older than 250 ms. Any thread.
bool PairHeadYaw(float* rad);
// One eye's finished picture (its effects run, the back buffer in the PRESENT state)
// copied into that eye's swapchain image, on the game's queue.
void CopyEye(int eye, ID3D12Resource* backBuffer);
// The head and the controllers as the frame open now locates them (what goes to the D2R
// Bridge): room space, not recentered. False without a session or controllers.
bool PairInput(D2RVR_XrInput* out);
// After the pair: both eyes as one projection layer (none if an eye is missing).
void EndPair();
// Every present (ReShade's reshade_present), with the game's device and queue (native):
// the session starts from the first one ([openxr] on - the menus too), and outside a pair
// the back buffer goes to the headset as a flat quad, paced by the runtime. pairEye: the eye this
// present's pass drew (0 or 1) inside a pair of two passes, else -1 - copied into its swapchain
// inside a native pair, never shown flat as an eye of its own outside one.
void OnPresent(ID3D12Device* dev, ID3D12CommandQueue* queue, ID3D12Resource* backBuffer, int pairEye = -1);
// After every pair, native or not (its presents done): the next presents may open flat frames again.
void PairOver();
// Twice a second: BodyWalk's bridge come after the session was made gets its signal again.
void FollowBridge();

// A session is running: the runtime paces the game (xrWaitFrame), not vrcam.
bool Running();
// [openxr] lean_m: how far the head may move off the body (0: not at all - a neck model then).
float LeanM();
// [openxr] on went to 0 while a session is up: the next present ends it.
bool StopPending();
// The next views taken as straight ahead (F11, BodyWalk's recentre).
void Recenter();
// Session and instance gone, FlatVR started again; from the game's exit paths.
void Shutdown();
// A D3D12 device is going (ReShade's destroy_device): the session's own ends with it.
void DeviceGone(ID3D12Device* dev);

}  // namespace xr
