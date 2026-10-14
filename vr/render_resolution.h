#pragma once
// [render] vr_resolution: the render size the headset is given, apart from the monitor's window.
// The plan: the session notes' "vr-resolution". The pieces: vr_resolution.h (sizes, when to ask),
// render_window.h (the game's window). Idea and shape after bearstonem/d2r-pcvr (MIT, steamvr/render_resolution.*).

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "vr_resolution.h"

namespace vrres {

void SetLogger(void (*log)(const char*));

// vrcam, once its window procedure is the game window's: the window a size may be asked for.
void Attach(HWND window);
// vrcam's window procedure, first thing. True: the message is done (`result`); false: go on with it (`l` may be changed).
bool Handle(HWND window, UINT message, WPARAM w, LPARAM& l, WNDPROC original, LRESULT& result);

// vrcam, a few times a second: the hero has stood in a world long enough (the game died when its screen grew while it
// built one, so a size is asked for only then; once asked it stays through the menus).
void SetHeroReady(bool ready);

// xr.cpp, every present on the draw thread: the headset's session (`running`), what the runtime holds (`recommended`,
// `maximum`: 0 = unknown), the size of the game's output now and [render] vr_resolution / vr_resolution_px (render_size.h).
void Update(HWND window, bool running, Size recommended, Size maximum, Size output, const rendersize::Pick& pick);

// xr.cpp (the draw thread): the runtime made - or refused - swapchains of this size. A refused render size falls back to the
// last one the runtime took (the window's own size when there was none), told once.
void SwapchainMade(uint32_t width, uint32_t height, bool ok);

// gamestate.cpp (the UI thread): the game's own graphics Apply.
void OptionsApplied();
// xr.cpp: the session is gone (or the game is closing) - the window's own size back.
void SessionEnded();

// A size is in force (asked for; the game may still be taking it) - the depth pick must not take a shadow map for the picture.
bool Active();
// The game's pixel space for this window while a size is in force (the window's real client area is another size).
bool GameSize(HWND window, Size* out);

}  // namespace vrres
