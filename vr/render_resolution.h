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
// [render] menu_wide (vr_resolution.h MenuWide): vrcam, a few times a second - the game's menus' scene (its frontend biome) has stood
// a while, no hero in a world.
void SetFrontendReady(bool ready);
// vrcam: ReShade drives the game's present (not [render] no_reshade) - [render] menu_wide stands down (vr_resolution.h MenuWideApplies).
void SetReShade(bool on);

// xr.cpp, every present on the draw thread: the headset's session (`running`), what the runtime holds (`recommended`,
// `maximum`: 0 = unknown), the size of the game's output now and [render] vr_resolution / vr_resolution_px (render_size.h);
// `menuWide` [render] menu_wide.
void Update(HWND window, bool running, Size recommended, Size maximum, Size output, const rendersize::Pick& pick, bool menuWide);

// xr.cpp (the draw thread): the runtime made - or refused - swapchains of this size. A refused render size falls back to the
// last one the runtime took (the window's own size when there was none), told once.
void SwapchainMade(uint32_t width, uint32_t height, bool ok);

// gamestate.cpp (the UI thread): the game's own graphics Apply.
void OptionsApplied();
// xr.cpp: the session is gone (or the game is closing) - the window's own size back.
void SessionEnded();

// [render] vr_resolution_ui: vrcam's hook on the game's own client-size query asks here what to answer `asker` (vr_resolution.h
// CanvasAnswer: the interface a 16:9 band of the game's picture, the cursor clamp the whole picture, while a size is in force;
// `native` is the query's own answer, `on` the ini key). Any thread.
uint64_t ClientSizeAnswer(Asker asker, uint64_t native, bool on);
// The hook went in after a size was already in force: the game is told it once more, so its interface is laid out again.
void ClientSizeReflow();

// A size is in force (asked for; the game may still be taking it) - the depth pick must not take a shadow map for the picture.
bool Active();
// The game's pixel space for this window while a size is in force (the window's real client area is another size).
bool GameSize(HWND window, Size* out);

}  // namespace vrres
