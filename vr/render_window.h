#pragma once
// The game's window and the size it is told it has (vr_resolution.h, render_resolution.h).
//
// Shape after bearstonem/d2r-pcvr's steamvr/render_window.h (MIT); written again here, with the window
// calls behind an "environment" so the tests can drive it with fake windows.
//
// The draw thread only POSTS (Request): the game's window procedure may take renderer locks, and the
// window's thread may be waiting on ours. The message comes back through Handle on the window's own thread,
// where the game's procedure is called with a WM_SIZE of the size asked for. The window keeps its real size.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <utility>

#include "vr_resolution.h"

namespace vrres {

// The real window calls.
struct Win32Env {
    UINT Message() const {
        static const UINT id = RegisterWindowMessageW(L"D2RVR.RenderSize.v1");
        return id;
    }
    bool IsWindow(HWND h) const { return ::IsWindow(h) != FALSE; }
    bool Post(HWND h, UINT m) const { return PostMessageW(h, m, 0, 0) != FALSE; }
    bool Client(HWND h, Size* out) const {
        RECT rc{};
        if (!GetClientRect(h, &rc) || rc.right < 1 || rc.bottom < 1) return false;
        *out = {(uint32_t)rc.right, (uint32_t)rc.bottom};
        return true;
    }
    bool Iconic(HWND h) const { return IsIconic(h) != FALSE; }
    bool Zoomed(HWND h) const { return IsZoomed(h) != FALSE; }
    LRESULT Call(WNDPROC proc, HWND h, UINT m, WPARAM w, LPARAM l) const { return CallWindowProcW(proc, h, m, w, l); }
};

template <class Env>
class RenderWindowT {
    Env env_;
    std::atomic<HWND> target_{nullptr};
    std::atomic<uint64_t> desired_{0}, logical_{0};
    std::atomic<bool> queued_{false}, reapply_{false};

    static bool IsMouse(UINT m) {   // the wheel's position is in screen pixels, every other button/move message's in the client's
        return m >= WM_MOUSEFIRST && m <= WM_MOUSELAST && m != WM_MOUSEWHEEL && m != WM_MOUSEHWHEEL;
    }

public:
    explicit RenderWindowT(Env env = Env{}) : env_(std::move(env)) {}
    Env& Environment() { return env_; }

    HWND Window() const { return target_.load(); }
    // The size the game was last told for this window ({}: its own).
    Size Logical(HWND h) const { return h && h == Window() ? Unpack(logical_.load()) : Size{}; }

    // Draw thread. `size` {} = the window's own. `force`: send it even if it is the size the game was told
    // last (the game's options reset its render state without a WM_SIZE).
    bool Request(HWND h, Size size, bool force = false) {
        if (!h || !env_.IsWindow(h) || !env_.Message()) return false;
        if (target_.load() != h) { target_.store(h); logical_.store(0); queued_.store(false); reapply_.store(false); }
        desired_.store(Pack(size));
        if (!size) reapply_.store(false);
        else if (force) reapply_.store(true);
        if (logical_.load() == desired_.load() && !reapply_.load()) return true;
        if (queued_.exchange(true)) return true;
        if (env_.Post(h, env_.Message())) return true;
        queued_.store(false);
        return false;
    }
    void Restore() { if (HWND h = Window()) Request(h, {}); }

    // A point of the window's client area <-> the game's picture (a native ScreenToClient / ClientToScreen
    // made by the game). False: no size is in force, nothing was changed.
    bool Remap(HWND h, Pt* p, bool toGame) const {
        const Size game = Logical(h);
        Size client;
        if (!p || !game || !env_.Client(h, &client)) return false;
        *p = toGame ? ToGame(*p, client, game) : ToClient(*p, client, game);
        return true;
    }

    // The window's procedure, first. True: handled (`result`); false: go on with the message - possibly
    // with `l` changed (a WM_SIZE of the system carries the size the game was told, a mouse message's
    // position is the game's pixel, not the window's).
    bool Handle(HWND h, UINT message, WPARAM w, LPARAM& l, WNDPROC original, LRESULT& result) {
        if (!h || h != Window()) return false;
        if (message == env_.Message()) {
            queued_.store(false);
            Size client;
            if (!env_.Client(h, &client) || env_.Iconic(h)) return true;   // asked for again by the next Request
            const Size next = Unpack(desired_.load());
            const bool forced = reapply_.exchange(false);
            if (next == Unpack(logical_.load()) && !forced) return true;
            logical_.store(Pack(next));
            const Size size = next ? next : client;
            result = env_.Call(original, h, WM_SIZE, env_.Zoomed(h) ? SIZE_MAXIMIZED : SIZE_RESTORED, MAKELPARAM(size.w, size.h));
            return true;
        }
        if (message == WM_CLOSE || message == WM_DESTROY) {
            // The window's own size back before the game can write a windowed size of the render size into its options.
            desired_.store(0);
            queued_.store(false);
            reapply_.store(false);
            if (logical_.exchange(0) && message == WM_CLOSE) {
                Size client;
                if (env_.Client(h, &client)) env_.Call(original, h, WM_SIZE, SIZE_RESTORED, MAKELPARAM(client.w, client.h));
            }
            return false;
        }
        if (message == WM_SIZE && w != SIZE_MINIMIZED) {
            // A real resize of the window (a drag, a monitor change) must not take the game off its size.
            logical_.store(desired_.load());
            if (const Size s = Unpack(logical_.load())) l = MAKELPARAM(s.w, s.h);
            return false;
        }
        if (IsMouse(message)) {
            Pt p{(short)LOWORD(l), (short)HIWORD(l)};
            if (Remap(h, &p, true)) l = MAKELPARAM((WORD)(short)p.x, (WORD)(short)p.y);
        }
        return false;
    }
};

using RenderWindow = RenderWindowT<Win32Env>;

}  // namespace vrres
