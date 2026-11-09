#pragma once
// [render] monitor_eye: the game's window on the monitor shows one eye, steady - for capturing the screen (OBS) while playing in VR.
// The idea is bearstonem/d2r-pcvr's ([render] desktop_one_eye, steamvr/transport.cpp, MIT).
//
// In stereo the game's window presents the eyes' pictures as they are drawn: by turns left, right, left... (Native OpenXR - eyes by
// turns, two passes a pair), or the right eye alone (one pass, two pictures: the left is held back). Each present goes to the headset
// first; then, before the window gets it, the picture of the eye not wanted is covered with the wanted eye's: its newest picture -
// with the eyes by turns the one made on the GPU this frame for the eye not drawn (what the headset shows for it now; or, with
// monitor_eye_drawn_only, its last picture the game drew), else the one kept from its own last present or the left eye's hold. With
// a VR render size apart from the window (a square picture in a wide window, which DXGI stretches) the eye's middle in the window's
// shape is drawn over the back buffer instead ([render] monitor_eye_fit), so the window shows it at its own proportions.
// One copy a present (a draw when cropped), on the queue of the present. Off (0): nothing here runs - the window as before.
//
// Everything on the game's draw thread; vrcam keeps its queue hooks out of our submissions (replay::t_ours).

#include <cstdint>

struct ID3D12CommandQueue;
struct ID3D12Resource;
struct HWND__;

namespace monitoreye {

void SetLogger(void (*log)(const char*));
// From d2r_vr.ini, on every change: [render] monitor_eye (0 as before, 1 left, 2 right), monitor_eye_fit, monitor_eye_drawn_only.
void Configure(int setting, bool fit, bool drawnOnly);
bool On();   // a setting other than 0

// The left eye's picture, final (its effects run, the headset has it), in `src` (the back buffer, PRESENT) while the present that
// follows carries the right eye: the replay's hold (one pass, two pictures). Kept when it is the eye wanted. On `q`.
void KeepHeld(ID3D12CommandQueue* q, int eye, ID3D12Resource* src);
// Eyes by turns: the eye not drawn made for this frame in `made` (the back buffer's size and family, RENDER_TARGET) - this frame's
// present may take it. Forgotten at that present.
void Made(int eye, ID3D12Resource* made);
// Right before the window's present of `bb` (PRESENT): the eye it carries (0, 1; -1: none - a menu, mono, a flat picture: nothing
// done), the window it goes to (its client size: the crop). After every copy of this present to the headset.
void Present(ID3D12CommandQueue* q, HWND__* wnd, ID3D12Resource* bb, int eye);

// [render] monitor_eye_fit: what the headset shows of eye `eye` (vrcam, the draw thread, before Present): the runtime's hidden-area mesh
// (XR_KHR_visibility_mask: xy tangent pairs on the view's z = -1 plane, triangle indices; `gen` moves when it is fetched again; none: nVerts
// 0), the frustum the eye's picture is drawn with and the part of it the headset is given (tangents l r u d, left and down negative). The
// window then gets the biggest box of its own shape inside what the lens shows, not the masked edges (monitor_eye_math.h VisibleBox).
// nullptr: not known (no first-person pair, canted displays) - the whole picture as before.
struct Lens {
    const float* xy = nullptr;
    uint32_t nVerts = 0;
    const uint32_t* idx = nullptr;
    uint32_t nIdx = 0;
    uint32_t gen = 0;
    float drawn[4] = {};
    float seen[4] = {};
    bool seenOk = false;
};
void SetLens(int eye, const Lens* lens);

}  // namespace monitoreye
