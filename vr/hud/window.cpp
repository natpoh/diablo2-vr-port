// vr/hud/window.cpp - cut from vr/vrcam.cpp (docs/plan_modules.md M9); what it gives: vr/hud/hud_vr.h. It sees vrcam.cpp's headers.
#include "../vrcam_headers.h"

using namespace D2RL;
using namespace vrc;   // as in vrcam.cpp

namespace vrc {

// ---- Mouse and keys ----------------------------------------------------------
bool GameFocused() {
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (fg) GetWindowThreadProcessId(fg, &pid);
    return fg && pid == GetCurrentProcessId();
}

// The game's main window: its biggest visible top-level window.
HWND g_gameWnd = nullptr;
WNDPROC g_gameWndProc = nullptr;

BOOL CALLBACK FindGameWindow(HWND h, LPARAM best) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    RECT rc{};
    if (pid != GetCurrentProcessId() || !IsWindowVisible(h) || GetWindow(h, GW_OWNER) || !GetClientRect(h, &rc)) return TRUE;
    HWND* out = (HWND*)best;
    RECT cur{};
    if (!*out || (GetClientRect(*out, &cur) && (rc.right * rc.bottom > cur.right * cur.bottom))) *out = h;
    return TRUE;
}
HWND GameWindow() {
    if (g_gameWnd) return g_gameWnd;
    HWND h = nullptr;
    EnumWindows(FindGameWindow, (LPARAM)&h);
    return h;
}

// The crosshair in place of the game's gauntlet (SetCursor hooked): vr/hud/crosshair.cpp

// W A S D are ours while the shooter controls are on (the game has skills
// and panels on them) - but not while the chat line is open: then they are
// letters, and every other key goes to the game anyway.
// A click or a key in the game's window for the input log, and whether a device or a program sent it.
void InputLogWindow(UINT msg, WPARAM w) {
    INPUT_MESSAGE_SOURCE src{};
    const char* how = !GetCurrentInputMessageSource(&src) ? "its origin unknown"
                      : src.originId == IMO_INJECTED  ? "injected by a program (BodyWalk's keys and mouse, a macro tool...)"
                      : src.originId == IMO_HARDWARE  ? "from a device"
                      : src.originId == IMO_SYSTEM    ? "from Windows"
                                                      : "its origin unknown";
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
        char name[32] = "";
        GetKeyNameTextA((LONG)(MapVirtualKeyW((UINT)w, MAPVK_VK_TO_VSC) << 16), name, sizeof name);
        InputLogF("key %s (0x%02X) in the game's window - %s", name[0] ? name : "?", (unsigned)w, how);
    } else {
        InputLogF("%s in the game's window - %s", msg == WM_LBUTTONDOWN ? "left click" : msg == WM_RBUTTONDOWN ? "right click"
                                                  : msg == WM_MBUTTONDOWN ? "middle click" : "side button", how);
    }
}

LRESULT CALLBACK GameWndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    {   // [render] vr_resolution: the size the game is asked for arrives here, on the window's thread (and a mouse position is the game's pixel)
        LRESULT done = 0;
        if (vrres::Handle(h, msg, w, l, g_gameWndProc, done)) return done;
    }
    // [openxr] laser_typing (vr/typed_keys.h): a key-down the plugin did not send - its own carry kOwnKeyTag, its posted Esc is known by
    // the time - lets the game take its keyboard layout while the laser holds the controller one. Raw input may come before the message.
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
        if (!(w == VK_ESCAPE && GetTickCount64() - g_postedEscAt.load() < 200)) panelinput::Typed((uintptr_t)GetMessageExtraInfo());
    } else if (msg == WM_INPUT) {
        RAWINPUT raw{};
        UINT size = sizeof raw;
        if (GetRawInputData((HRAWINPUT)l, RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) != (UINT)-1 && raw.header.dwType == RIM_TYPEKEYBOARD &&
            !(raw.data.keyboard.Flags & RI_KEY_BREAK))
            panelinput::Typed((uintptr_t)raw.data.keyboard.ExtraInformation);
    }
    // the input log: clicks, and keys pressed (not repeats, not the chat line's letters)
    if (g_inputLog.load() > 0 && (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN || msg == WM_XBUTTONDOWN ||
                                  ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && !(l & (1 << 30)) && !ChatOpen())))
        InputLogWindow(msg, w);
    // [gameplay] block_stray_clicks (F4): a mouse button the game gets from outside the plugin while it plays in the world with no panel open
    // (the log of 2026-10-14 16:26: four clicks "from a device" at the rhythm of a bow's draw and release) is dropped before the game sees it.
    // The mouse look's pointer and [bow] mode 2's own Shift + click are the game's by design.
    if (straywalk::DropWindowMouse(msg, gamestate::MenuOpen(), g_inWorld.load(), ShooterActive(), g_set.bowMode.load() == 2)) return 0;
    // (Windows keeps a window to its monitor; vrcam let it be bigger in 0.159-0.160 - a window past the
    // screen's edge flickered the fog in the left eye, and one taller than ~2400 crashes the game itself at
    // its title screen: a square now fits the monitor - BigWindowTick; a bigger one: a virtual monitor.)
    // F1..F3 (F4 in VR) pick the view: the game, which has skills on them, never sees them.
    if ((msg == WM_KEYDOWN || msg == WM_KEYUP) && w >= VK_F1 && (int)w < VK_F1 + ViewCount()) return 0;
    // Enter opening the chat line: its letters may come before the game has
    // set its flag, so they are let through for a moment (not on the Enter
    // that sends the line - W pressed right after it walks).
    if (msg == WM_KEYDOWN && w == VK_RETURN && !(l & (1 << 30)) && (ShooterActive() || VrKeyWalk()))
        g_chatEnterAt.store(ChatFlagFound() && !ChatFlag() ? GetTickCount64() : 0);
    // Still swallowed with the keyboard move (FlatKeyMode): HookKeyMove's vector
    // replaces 0x8A960's answer, never adds to it, so a W A S D the player bound
    // to the game's own Move keys would change nothing - but by default the game
    // has other commands on them (W swaps weapons), which would fire with every step.
    // A key's release always goes through: pressed in F1 / F2 (the game's own walk, its
    // Move byte set), let go in F3, a swallowed release left that byte set and the hero
    // walking by himself back in F1 (2026-10-07). The game's commands are on the press.
    // VR third person with vr_keys_walk (VrKeyWalk): the same - W A S D walk there.
    if ((msg == WM_KEYDOWN || msg == WM_CHAR) && (ShooterActive() || VrKeyWalk()) && !ChatOpen()) {
        const WPARAM k = msg == WM_CHAR && w >= 'a' && w <= 'z' ? w - ('a' - 'A') : w;
        if (k == 'W' || k == 'A' || k == 'S' || k == 'D') return 0;
    }
    // Flat with the keyboard move: the game stays in mouse mode, so the mouse is
    // the game's - its pointer kept in the window's middle (MouseTick), a click
    // goes there: on a monster an attack, on an item a pick-up, on a door or a
    // person the action, on the ground a walk to that point, Shift held = in
    // place - all through the game's own mouse code, along the camera's middle
    // ray (d2rcam's ScreenToRay). Every mouse message carries the middle, so
    // the pointer's travel between two MouseTicks never moves the game's point.
    // Raw mouse input (the game registers none) is dropped as before.
    if (ShooterActive() && KeyMoveMode()) {
        if (msg == WM_SETCURSOR && LOWORD(l) == HTCLIENT && CrosshairNow() && OrigSetCursor)
            if (HCURSOR x = Crosshair()) { OrigSetCursor(x); return TRUE; }
        RECT rc{};
        if (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST && msg != WM_MOUSEWHEEL && msg != WM_MOUSEHWHEEL && GetClientRect(h, &rc)) {
            vrres::Size game;   // [render] vr_resolution: the middle of the game's picture
            if (vrres::GameSize(h, &game)) rc.right = (LONG)game.w, rc.bottom = (LONG)game.h;
            l = MAKELPARAM(rc.right / 2, rc.bottom / 2);
        }
        if (msg == WM_INPUT) {
            RAWINPUTHEADER hdr{};
            UINT size = sizeof hdr;
            if (GetRawInputData((HRAWINPUT)l, RID_HEADER, &hdr, &size, sizeof(RAWINPUTHEADER)) != (UINT)-1 && hdr.dwType == RIM_TYPEMOUSE)
                return DefWindowProcW(h, msg, w, l);
        }
        return CallWindowProcW(g_gameWndProc, h, msg, w, l);
    }
    // The mouse is ours too (KeysAsStick): the pointer kept in the middle would
    // flip the game to mouse mode - its pointer drawn there, a click a walk to it.
    if (ShooterActive()) {
        if (msg == WM_SETCURSOR && LOWORD(l) == HTCLIENT) { SetCursor(nullptr); return TRUE; }
        if ((msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) || msg == WM_MOUSEHOVER || msg == WM_MOUSELEAVE) return 0;
        if (msg == WM_INPUT) {   // raw input: the mouse's dropped, the keyboard's passed on
            RAWINPUTHEADER hdr{};
            UINT size = sizeof hdr;
            if (GetRawInputData((HRAWINPUT)l, RID_HEADER, &hdr, &size, sizeof(RAWINPUTHEADER)) != (UINT)-1 && hdr.dwType == RIM_TYPEMOUSE)
                return DefWindowProcW(h, msg, w, l);
        }
    }
    return CallWindowProcW(g_gameWndProc, h, msg, w, l);
}

// D2R VR Settings' "Square game window" ([openxr] square_window, window_size): kept from our ini - the
// game writes the size Windows cut its window to back into its Settings.json. As big as asked, but on
// its monitor (its work area, the frame included): past the screen's edge the fog flickered in the left
// eye at 2400 on a 2160 screen, and on a virtual monitor of a higher resolution it did not (2026-10-11).
// Put at the work area's top. Posted (SWP_ASYNCWINDOWPOS): the window's thread may wait on ours.
// Onto another monitor in two steps: moved there at the size it has, made square at the next look, on
// it - across monitors of another scale Windows' WM_DPICHANGED has the game size itself in between.
// What stops it goes into the log once each time it changes, and the monitors as they are seen: a
// player's window stayed small on the main monitor and the log had no word on why (2026-10-12).
void SquareWhy(int why, const char* fmt, ...) {
    static int said = 0;
    if (why == said) return;
    said = why;
    if (!why) return;
    char b[512];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    LogF("vrcam: square window - %s", b);
}
void BigWindowTick() {
    static ULONGLONG at = 0;
    const ULONGLONG now = GetTickCount64();
    if (!g_gameWnd || !g_gameWndProc || now - at < 2000) return;
    at = now;
    if (g_set.platform.load() != 1) { SquareWhy(0, ""); return; }   // flat mode ([mode] platform=0): the game stays in the window the player set - no square
    if (!IniB(L"openxr", L"square_window", false)) { SquareWhy(0, ""); return; }
    if (xr::On() && rendersize::FromIni((int)IniF(L"render", L"vr_resolution", 0.0f), (int)IniF(L"render", L"vr_resolution_px", 0.0f)).kind !=
                        rendersize::Kind::Desktop) {
        SquareWhy(6, "not needed: [render] vr_resolution_px / vr_resolution gives the game a render size of its own, the window keeps its size");
        return;
    }
    if (!xr::On()) {
        wchar_t off[48] = L"";
        GetPrivateProfileStringW(L"openxr", L"off_after_crash", L"", off, 48, g_iniPath);
        SquareWhy(1, "ticked, but native OpenXR is not running ([openxr] on=0%s%s, or its session did not start): the window keeps "
                  "its start size on its monitor - D2R VR Authentic Settings > Performance > 3D in the headset > Native OpenXR",
                  off[0] ? ", switched off by the start guard after a crash at " : "", Utf8(off).c_str());
        return;
    }
    if (IsIconic(g_gameWnd)) return;
    if (IsZoomed(g_gameWnd)) { SquareWhy(2, "the game's window is maximized - restore it (double-click its title bar) for the square"); return; }
    // only in a game, the hero there 3 s: the game dies when its window grows that tall while it builds a
    // screen - its title screen at the start, its frontend scene (grown 4 s after the title: dead 2 s
    // later, 2026-10-11); the menus before keep the size it started with (2400 at most, SafeGameWindow)
    static ULONGLONG heroSince = 0;
    if (!gamestate::LocalPlayer() || !g_inWorld.load()) { heroSince = 0; return; }
    if (!heroSince) heroSince = now;
    if (now - heroSince < 3000) return;
    RECT c{}, r{};
    if (!GetClientRect(g_gameWnd, &c) || !GetWindowRect(g_gameWnd, &r)) return;
    const int fw = (r.right - r.left) - c.right, fh = (r.bottom - r.top) - c.bottom;
    static bool frameTold = false;
    if (fw <= 0 && fh <= 0 && !frameTold) {
        frameTold = true;
        LogF("vrcam: square window - the game's window has no frame (%dx%d): its Options > Video > Window Mode is not Windowed - "
             "the square wants Windowed", (int)c.right, (int)c.bottom);
    }
    const int asked = std::clamp((int)IniF(L"openxr", L"window_size", 2000.0f), 640, 4000);
    // the monitor it is on when that holds the square; else the one that holds the most (a virtual
    // monitor made for it, BodyWalk's Virtual Monitor): the window is moved there, no "make it primary"
    struct Pick { HMONITOR here, bestMon; RECT best; int bestFits; RECT cur; int curFits; int fw, fh; wchar_t hereName[32], bestName[32]; char list[640]; };
    Pick pk{MonitorFromWindow(g_gameWnd, MONITOR_DEFAULTTONEAREST), nullptr, {}, 0, {}, 0, fw, fh, L"", L"", ""};
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR m, HDC, LPRECT, LPARAM p) -> BOOL {
        Pick& k = *(Pick*)p;
        MONITORINFOEXW mi{};
        mi.cbSize = sizeof mi;
        if (!GetMonitorInfoW(m, &mi)) return TRUE;
        const int fits = std::min((int)(mi.rcWork.right - mi.rcWork.left) - k.fw, (int)(mi.rcWork.bottom - mi.rcWork.top) - k.fh);
        if (m == k.here) { k.cur = mi.rcWork; k.curFits = fits; wcscpy_s(k.hereName, mi.szDevice); }
        if (fits > k.bestFits) { k.best = mi.rcWork; k.bestFits = fits; k.bestMon = m; wcscpy_s(k.bestName, mi.szDevice); }
        const size_t n = strlen(k.list);
        snprintf(k.list + n, sizeof k.list - n, "%s%ls %ld,%ld %ldx%ld%s%s", n ? "; " : "", mi.szDevice, mi.rcWork.left, mi.rcWork.top,
                 mi.rcWork.right - mi.rcWork.left, mi.rcWork.bottom - mi.rcWork.top, (mi.dwFlags & MONITORINFOF_PRIMARY) ? " main" : "",
                 m == k.here ? " (the game's)" : "");
        return TRUE;
    }, (LPARAM)&pk);
    {   // the monitors (their work areas) whenever they change
        static std::string seen;
        if (seen != pk.list) { seen = pk.list; LogF("vrcam: square window - monitors: %s; %d asked", pk.list, asked); }
    }
    if (!pk.curFits && !pk.bestFits) return;
    // a move asked for and the window still on its monitor at the next look: something puts it back
    // (the game's Window Mode, another program) - after three, it is made square where it is
    static HMONITOR movedTo = nullptr;
    static int moveMisses = 0;
    if (movedTo) {
        moveMisses = pk.here == movedTo ? 0 : moveMisses + 1;
        if (moveMisses >= 3)
            SquareWhy(4, "the game's window did not go onto %ls three times - it stays on %ls and is made as big as that holds (%d)",
                      pk.bestName, pk.hereName, pk.curFits);
        movedTo = nullptr;
    }
    const bool stay = pk.curFits >= asked || pk.bestFits <= pk.curFits || moveMisses >= 3;
    if (!stay) {
        SetWindowPos(g_gameWnd, nullptr, pk.best.left, pk.best.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
        movedTo = pk.bestMon;
        static int movedTold = 0;
        if (movedTold++ < 8) LogF("vrcam: square window - the game's window %dx%d moved from %ls onto %ls (holds %d, %d asked) - made square there next",
                                  (int)c.right, (int)c.bottom, pk.hereName, pk.bestName, pk.bestFits, asked);
        return;
    }
    const RECT wa = pk.cur;
    const int side = std::max(320, std::min(asked, pk.curFits));
    const int x = std::clamp((int)r.left, (int)wa.left, std::max((int)wa.left, (int)wa.right - (side + fw)));
    const int y = (int)wa.top;
    // a size asked for and not there three looks on: said, still asked again
    static int sizeMisses = 0;
    if (c.right == side && c.bottom == side && r.left == x && r.top == y) {
        sizeMisses = 0;
        if (moveMisses < 3) SquareWhy(0, "");
        return;
    }
    if (++sizeMisses == 4)
        SquareWhy(5, "the game's window does not take %dx%d at %d,%d on %ls: it is %dx%d at %ld,%ld (its DPI %u) - something sizes it back",
                  side, side, x, y, pk.hereName, (int)c.right, (int)c.bottom, r.left, r.top, GetDpiForWindow(g_gameWnd));
    SetWindowPos(g_gameWnd, nullptr, x, y, side + fw, side + fh, SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
    static int told = 0;
    if (told++ < 8) LogF("vrcam: openxr - the game's window %dx%d made %dx%d at %d,%d on %ls (D2R VR Authentic Settings' square window, %d asked%s)",
                         (int)c.right, (int)c.bottom, side, side, x, y, pk.hereName, asked,
                         side < asked ? " - no monitor holds more: a bigger square needs one (BodyWalk's Virtual Monitor) of a higher resolution" : "");
}

// [render] vr_resolution is asked for only once the hero has stood in a world 3 s, as the square window is: the game died
// when its screen grew very tall while it built one (its title screen, 2026-10-11). Once asked it stays through the menus.
bool HeroSettled() {
    static ULONGLONG since = 0;
    if (!gamestate::LocalPlayer() || !g_inWorld.load()) { since = 0; return false; }
    const ULONGLONG now = GetTickCount64();
    if (!since) since = now;
    return now - since >= 3000;
}
// [render] menu_wide (vr/vr_resolution.h MenuWide): the game's menus at 16:9 are asked for only once their scene has stood 3 s -
// never while the game builds a screen (the title screen comes before that biome; a load into a game takes it away at once).
extern std::atomic<bool> g_frontendBiome;
bool FrontendSettled() {
    static ULONGLONG since = 0;
    if (!g_frontendBiome.load() || g_inWorld.load()) { since = 0; return false; }
    const ULONGLONG now = GetTickCount64();
    if (!since) since = now;
    return now - since >= 3000;
}

// [openxr] laser_fit_window (vr/laser_reach.h): the laser puts the cursor where it points with SetCursorPos, which never leaves the desktop, and clicks
// there with SendInput, which lands on whatever window is on top - so the game window's client area is kept inside its monitor's work area while the
// laser is on (VR): moved, and shrunk with its shape kept while a render size is in force (the game's picture is that size whatever the window is:
// render_window.h keeps the real resize from it). Where the window is the game's picture (no render size) it is only moved; in a game the square
// window's own tick sizes it (BigWindowTick). Every 2 s; said in the log when it moves the window, once while it cannot make it fit.
void LaserFitTick() {
    static ULONGLONG at = 0;
    const ULONGLONG now = GetTickCount64();
    if (!g_gameWnd || now - at < 2000) return;
    at = now;
    if (g_set.platform.load() != 1 || !xr::On() || !IniB(L"openxr", L"laser", false) || !IniB(L"openxr", L"laser_fit_window", true)) return;
    if (IsIconic(g_gameWnd) || IsZoomed(g_gameWnd)) return;   // (maximized: its client area is the work area already)
    vrres::Size game{};
    const bool mayResize = vrres::GameSize(g_gameWnd, &game);
    if (!mayResize && HeroSettled() && IniB(L"openxr", L"square_window", false)) return;   // in a game the square's own tick has the window
    RECT wr{}, cr{};
    POINT c0{0, 0};
    if (!GetWindowRect(g_gameWnd, &wr) || !GetClientRect(g_gameWnd, &cr) || !ClientToScreen(g_gameWnd, &c0)) return;
    MONITORINFO mi{sizeof mi};
    if (!GetMonitorInfoW(MonitorFromWindow(g_gameWnd, MONITOR_DEFAULTTONEAREST), &mi)) return;
    const laserreach::Rect window{wr.left, wr.top, wr.right, wr.bottom}, client{c0.x, c0.y, c0.x + cr.right, c0.y + cr.bottom},
                           work{mi.rcWork.left, mi.rcWork.top, mi.rcWork.right, mi.rcWork.bottom};
    const laserreach::Fit f = laserreach::FitWindow(window, client, work, mayResize);
    static bool toldNoFit = false;
    if (!f.fits && !toldNoFit) {
        toldNoFit = true;
        LogF("vrcam: laser - the game's window (client %ldx%ld) does not fit its monitor's work area %ldx%ld and is the game's picture (no render size): "
             "part of it stays out of the laser's reach - [render] vr_resolution_px (Settings > Performance) lets it be made smaller", cr.right, cr.bottom,
             mi.rcWork.right - mi.rcWork.left, mi.rcWork.bottom - mi.rcWork.top);
    }
    if (!f.change) return;
    SetWindowPos(g_gameWnd, nullptr, f.window.l, f.window.t, f.window.W(), f.window.H(),
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS | (f.resized ? 0u : (UINT)SWP_NOSIZE));
    static int told = 0;
    if (told++ < 8)
        LogF("vrcam: laser - the game's window (client %ldx%ld at %ld,%ld) %s onto its monitor's work area (%ld,%ld %ldx%ld): window %dx%d at %d,%d%s, so the laser "
             "reaches all of the picture ([openxr] laser_fit_window)", cr.right, cr.bottom, c0.x, c0.y, f.resized ? "shrunk and moved" : "moved",
             mi.rcWork.left, mi.rcWork.top, mi.rcWork.right - mi.rcWork.left, mi.rcWork.bottom - mi.rcWork.top, f.window.W(), f.window.H(), f.window.l, f.window.t,
             f.resized ? " (a render size is in force: the game's picture keeps its size)" : "");
}

void HookGameWindow() {
    BigWindowTick();
    LaserFitTick();
    vrres::SetHeroReady(HeroSettled());
    vrres::SetFrontendReady(FrontendSettled());
    vrres::SetReShade(!g_noReShade);   // [render] menu_wide only without ReShade (vr/vr_resolution.h MenuWideApplies)
    if (g_gameWndProc) return;
    HWND h = nullptr;
    EnumWindows(FindGameWindow, (LPARAM)&h);
    RECT rc{};
    if (!h || !GetClientRect(h, &rc) || rc.right < 320 || rc.bottom < 200) return;
    g_gameWndProc = (WNDPROC)SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)&GameWndProc);
    if (g_gameWndProc) {
        g_gameWnd = h;
        vrres::Attach(h);   // [render] vr_resolution: a size may be asked for this window now
    }
}

// The mouse's turn on top of the head: with the shooter controls the pointer's
// travel from the window's middle (it is put back there - a crosshair, clicks
// go where you look), else a middle-button drag.
bool g_dragging = false;
POINT g_dragLast{};

void MouseTick() {
    // Flat: while the mouse look was not keeping the pointer in the middle (a dialogue, a chest, the inventory, Esc, the window not in
    // front) the pointer went wherever the mouse took it - that travel is no turn. The first look after it only puts the pointer back
    // in the middle; it was taken as one, and leaving a dialogue threw the view at the sky (from sajmonsoft's flat fork, 2026-10).
    static bool lookWasOn = false;
    const bool flat = g_set.platform.load() == 0;
    if (!g_enabled.load() || !GameFocused()) { g_dragging = false; lookWasOn = false; return; }
    POINT p{};
    if (!GetCursorPos(&p)) return;
    int dx = 0, dy = 0;
    RECT rc{};
    if (ShooterActive() && g_gameWnd && GetClientRect(g_gameWnd, &rc)) {
        POINT mid{rc.right / 2, rc.bottom / 2};
        ClientToScreen(g_gameWnd, &mid);
        dx = p.x - mid.x; dy = p.y - mid.y;
        if (dx || dy) SetCursorPos(mid.x, mid.y);
        g_dragging = false;
        const bool resumed = flat && !lookWasOn;
        lookWasOn = true;
        if (resumed) return;
    } else {
        lookWasOn = false;
        if (!(GetAsyncKeyState(VK_MBUTTON) & 0x8000)) { g_dragging = false; return; }
        if (g_dragging) { dx = p.x - g_dragLast.x; dy = p.y - g_dragLast.y; }
        g_dragging = true;
        g_dragLast = p;
    }
    if (!dx && !dy) return;
    const float k = g_set.mouseSpeed.load();
    g_mouseYaw.store(WrapDeg(g_mouseYaw.load() + dx * k));                       // right turns right
    g_mousePitch.store(std::clamp(g_mousePitch.load() + dy * k, -90.0f, 90.0f));  // down looks down
    g_gen.fetch_add(1);
}

// [debug] flat_pick, on the timer after MouseTick: with the flat mouse look on, says when the game's mouse code stops asking for a ray, asks it
// for a pixel away from its viewport's middle (the crosshair), or the game's window loses the keyboard - and once when all is well again.
void FlatPickWatch() {
    static ULONGLONG lastCheck = 0, lastBad = 0;
    static bool bad = false;
    const ULONGLONG now = GetTickCount64();
    if (!g_set.flatPickLog.load() || now - lastCheck < 1000) return;
    lastCheck = now;
    if (!ShooterActive() || !KeyMoveMode()) { bad = false; return; }
    d2rcam::LastPickInfo lp{};
    const bool have = d2rcam::LastPick(&lp);
    const bool stale = !have || now - lp.at > 1500;
    const bool off = have && !flatpick::AtCentre(lp.px, lp.py, lp.vpW, lp.vpH);
    const bool focus = GameFocused();
    if (!stale && !off && focus) {
        if (bad) Log("vrcam: flat pick: the game's pixel is the crosshair's again");
        bad = false;
        return;
    }
    if (bad && now - lastBad < 5000) return;
    bad = true;
    lastBad = now;
    RECT rc{};
    if (g_gameWnd) GetClientRect(g_gameWnd, &rc);
    LogF("vrcam: flat pick: %s%s%s - the game's pixel %.0f,%.0f of %.0fx%.0f (%.0f%% off, from 0x%llX), the window %ldx%ld", !focus ? "the game's window is not in front; " : "",
         stale ? "the game asked no mouse ray for over 1.5 s; " : "", off ? "the game's pixel is NOT the middle" : "", lp.px, lp.py, lp.vpW, lp.vpH,
         have ? 100.0f * flatpick::OffCentre(lp.px, lp.py, lp.vpW, lp.vpH) : 0.0f, lp.ret, (long)rc.right, (long)rc.bottom);
}

}  // namespace vrc
