#pragma once
// [headset_shot] grid: a calibration grid drawn into each eye's picture as it goes to the headset (Serg, 2026-10-30: «и ещё какую нибудь
// калибровочную сетку наложить как во флатвр мы это делаем»). Native OpenXR, a stereo pair through the head's camera (F2 / F4, not F1's
// window): drawn into the eye's swapchain image after the copy (and into the eye made by turns), before it is released - so the headset, the
// screenshot and the clip all have it. The window on the monitor does not.
//
// The grid is in each eye's own angles, as the image's field of view says: a line every 10 degrees left-right and up-down, numbered, the
// view's axis (0, 0) a thick red cross, the image's own middle a small magenta ring (they part where the field of view is lopsided), and a big
// L or R. Lines at the same angle in both eyes are the same direction far away: with the eyes right, the two grids fuse into one in the headset
// and sit still on the world; a grid seen double, tilted, or swimming against the world says which eye, its angles or its centre is off.
//
// Live ([headset_shot] grid, off by default). The draw: one full-screen triangle per eye, on the list that copies the eye.

#include <d3d12.h>

#include <cstdint>

namespace headgrid {

void SetLogger(void (*log)(const char*));
void LoadSettings(const wchar_t* iniPath);   // [headset_shot] grid (live)
bool On();
void SetOnForTest(bool on);

// The grid over `target` (RENDER_TARGET, left so), drawn on `list` with the render-target view's format `fmt` (typed: the swapchain's).
// tanL tanR tanU tanD: the tangents of the image's edges from the eye's axis (left and down negative). eye 0 left, 1 right. False: not drawn
// (off, or its shader / pipeline could not be made - said once in the log).
bool Draw(ID3D12GraphicsCommandList* list, ID3D12Resource* target, DXGI_FORMAT fmt, uint32_t w, uint32_t h, float tanL, float tanR, float tanU,
          float tanD, int eye);

}  // namespace headgrid
