#pragma once
// [render] monitor_panels: the panels the headset shows go onto the monitor's picture too - for recording the screen (OBS) while playing
// in VR (Serg, 2026-11-03: the inventory on the hand, the trader's menu, the stash were in the headset and not in the recording).
//
// Native OpenXR hands the runtime the eyes' pictures (a projection layer) and, over them, quad layers of ours: the menu's panel (the
// inventory, the trader, the stash, on the hand or in front - [openxr] menu_3d), the toolbar and the map, the belt's balls, the laser,
// the monster's bar, the notice card, the hand vitals, the settings panel and the skill wheel... The runtime puts those together for the
// headset only; the game's window on the monitor gets the eye's picture alone. Here each frame's quads are noted as they go to
// xrEndFrame (vr/xr.cpp CloseFrame: the image each shows, where it hangs, its size, its eyes, its fade) with the eyes' views, and at the
// window's present (vr/monitor_eye.cpp, after the wanted eye is in the back buffer) each one is drawn over the picture where that eye
// sees it: its corners projected with the eye's view (vr/monitor_layers_math.h), back to front, premultiplied alpha as the runtime blends
// them. Visual only, the window's picture only: the headset's picture and the game are not touched. Off: nothing here runs.
//
// The quads' images are the swapchain images the runtime was given this frame; drawn on the game's queue (the queue their pictures were
// drawn on), from their colour state and back. A swapchain destroyed (or the session's end) forgets the frame noted.

#include <cstdint>

#include "monitor_layers_math.h"

struct ID3D12Device;
struct ID3D12CommandQueue;
struct ID3D12GraphicsCommandList;
struct ID3D12Resource;

namespace monitorlayers {

void SetLogger(void (*log)(const char*));
// From d2r_vr.ini, on every change: [render] monitor_panels.
void Configure(bool on);
bool On();

constexpr uint32_t kMaxQuads = 48;

// One quad layer of the frame, as handed to xrEndFrame.
struct Quad {
    ID3D12Resource* tex = nullptr;   // its swapchain image (the one released this frame, or a static image's only one)
    int format = 0;                  // the swapchain's format (DXGI_FORMAT) - the view's, when the image is typeless
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;   // its image rect, 0..1 of the image
    Pose pose{};
    float width = 0, height = 0;     // m
    int eyes = 0;                    // XrEyeVisibility: 0 both, 1 left, 2 right
    bool blend = true;               // XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT (premultiplied); false: opaque
    bool unpremultiplied = false;    // XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT
    float scale[4] = {1, 1, 1, 1};   // its colour scale on the monitor (ScaleWithoutBrightness)
    char name[28] = {};              // for the log
};
// A frame: the eyes' views as their pictures were drawn (the whole back buffer's tangents) and its quads, back to front.
struct Frame {
    ID3D12CommandQueue* queue = nullptr;   // the queue the quads' pictures were drawn on
    bool eyeOk[2] = {};
    Pose eye[2] = {};
    Tan tan[2] = {};
    uint32_t n = 0;
    Quad quad[kMaxQuads];
};

// vr/xr.cpp, right before xrEndFrame (on the draw thread): this frame's quads. A frame with no eyes (a menu's flat picture, F1's window)
// forgets the last one.
void Note(const Frame& f);
// A swapchain about to be destroyed, the session's end: the frame noted is forgotten (its images may go).
void Forget();

// Whether a frame noted lately has a quad for `eye` (0, 1) and is for the queue `q` - monitor_eye then opens its list for this present.
bool Wanted(ID3D12CommandQueue* q, int eye);
// Into the open `list` (on `q`, its allocator's slot `slot` of `slots`, the slots' lists done): the quads over `bb` (PRESENT, and left so),
// which shows `part` of the eye `eye`'s picture. Its own pipeline and descriptors (made once). Returns the quads drawn.
uint32_t Draw(ID3D12Device* dev, ID3D12CommandQueue* q, ID3D12GraphicsCommandList* list, int slot, int slots, ID3D12Resource* bb, int eye, const Part& part);
// The allocator `slot`'s list is done (its fence passed): its GPU time, if it timed any (the 10 s line).
void SlotDone(int slot);
// Every device gone: our objects released (monitor_eye's DropAll, its lists done or lost).
void Drop();

}  // namespace monitorlayers
