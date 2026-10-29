#pragma once
// [openxr] laser_fit_window: the laser reaches the whole picture. Two players (0.182): "the laser cannot reach some parts of the screen" (the
// inventory's Options tab among them).
//
// The laser puts the game's cursor where it points (xr.cpp LaserStep): u, v on the picture -> that pixel of the game window's client area
// (ClientPixel) -> the screen (ClientToScreen) -> SetCursorPos; its trigger is a SendInput click there. The game then reads the cursor back
// (GetCursorPos + ScreenToClient, mapped to its picture by render_resolution.cpp while a render size is in force). Every step of that maps the
// whole picture onto the whole client area - except Windows itself: SetCursorPos never puts the cursor off the desktop, and a click goes to
// whatever window is on top at its point. So every row or column of the client area that lies past its monitor's edge cannot be reached, and
// one under the taskbar is clicked on the taskbar. And the window is easily that big: D2R VR Settings' square window writes window_size (1920
// by default) into the game's Settings.json; the plugin cuts only its HEIGHT at the start (SafeGameWindow) and the game puts it where it likes;
// the square-window tick (vrcam BigWindowTick) fits it to its monitor only in a game - and never while [render] vr_resolution_px gives the game a
// render size of its own (the shipped default, 1536): "the window keeps its size". The author's 4K monitor holds a 1920 square; a 1080p or
// 1440p one does not - the picture's rows past the bottom (or, the window placed higher, its top: the tab bar) were out of the laser's reach.
//
// FitWindow keeps the window - frame and all - inside its monitor's work area (the taskbar left out): moved, and, where it may be (a render size
// in force: the game's picture is that size whatever the window is - render_window.h keeps a real resize from reaching it), shrunk to fit with
// its shape kept. Where it may not (the window is the game's picture: the menus before a game, the plain square window), it is only moved -
// its top left kept on the screen first (the tabs, the menus' titles). Pure: rectangles in, a rectangle out. Test: cleanroom/tests/laser_reach_test.cpp.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace laserreach {

struct Rect {
    int32_t l = 0, t = 0, r = 0, b = 0;
    int32_t W() const { return r - l; }
    int32_t H() const { return b - t; }
    bool Holds(const Rect& o) const { return o.l >= l && o.t >= t && o.r <= r && o.b <= b; }
};
struct Pt { int32_t x = 0, y = 0; };

constexpr int32_t kMinClient = 320;   // never shrunk below (the window's own minimum in HookGameWindow's search is 320 x 200)

// LaserStep's mapping: u, v on the picture (0..1 from its top left) -> the client area's pixel (w x h).
inline Pt ClientPixel(float u, float v, int32_t w, int32_t h) {
    const auto at = [](float f, int32_t n) {
        const float x = std::isfinite(f) ? f * (float)n : 0.0f;
        return std::clamp((int32_t)std::floor(x), (int32_t)0, std::max<int32_t>(0, n - 1));
    };
    return {at(u, w), at(v, h)};
}

struct Fit {
    bool change = false;    // the window is to be put at `window`
    bool resized = false;   // ... a size other than it has
    bool fits = true;       // after it, all of the client area is in the work area
    Rect window;
};

// window: its rectangle on the screen (GetWindowRect); client: its client area on the screen (GetClientRect + ClientToScreen); work: the work
// area of its monitor. mayResize: the window's size is not the game's picture (a render size in force).
inline Fit FitWindow(const Rect& window, const Rect& client, const Rect& work, bool mayResize) {
    Fit f;
    f.window = window;
    const int32_t fl = std::max(0, client.l - window.l), ft = std::max(0, client.t - window.t);   // the frame round the client area
    const int32_t fr = std::max(0, window.r - client.r), fb = std::max(0, window.b - client.b);
    int32_t cw = client.W(), ch = client.H();
    if (cw < 1 || ch < 1 || work.W() < 1 || work.H() < 1) return f;
    const int32_t roomW = work.W() - fl - fr, roomH = work.H() - ft - fb;
    if (mayResize && (cw > roomW || ch > roomH) && roomW >= kMinClient && roomH >= kMinClient) {
        const double s = std::min((double)roomW / cw, (double)roomH / ch);   // its shape kept
        cw = std::max(kMinClient, (int32_t)std::floor(cw * s));
        ch = std::max(kMinClient, (int32_t)std::floor(ch * s));
        f.resized = true;
    }
    const int32_t w = cw + fl + fr, h = ch + ft + fb;
    // inside the work area; too big for it (not to be shrunk): its top left on the screen
    const int32_t x = std::max(work.l, std::min(window.l, work.r - w));
    const int32_t y = std::max(work.t, std::min(window.t, work.b - h));
    f.window = {x, y, x + w, y + h};
    f.change = f.resized || x != window.l || y != window.t;
    f.fits = work.Holds({x + fl, y + ft, x + fl + cw, y + ft + ch});
    return f;
}

}  // namespace laserreach
