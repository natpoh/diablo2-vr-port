// [render] vr_resolution - see render_resolution.h. The real thing: the game's window, and the two user32 calls
// the game reads its cursor by. Shape after bearstonem/d2r-pcvr's steamvr/render_resolution.cpp (MIT).

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "render_resolution.h"

#include <intrin.h>
#include <MinHook.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <mutex>

#include "render_window.h"

namespace vrres {
namespace {

void (*g_log)(const char*) = nullptr;
void Say(const char* fmt, ...) {
    if (!g_log) return;
    char b[512];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    g_log(b);
}

RenderWindow g_window;
Controller g_ctl;
Fallback g_fallback;                 // (the draw thread's)
std::atomic<bool> g_forgetFallback{false};
std::atomic<HWND> g_attached{nullptr};
std::atomic<bool> g_hero{false};
std::atomic<bool> g_frontend{false};   // [render] menu_wide: the menus' scene has stood a while
std::atomic<bool> g_reshade{true};     // ReShade drives the present: [render] menu_wide stands down (until vrcam says otherwise)
MenuWide g_menuWide;                   // (the draw thread's)

// -- the pointer: the game reads its cursor as GetCursorPos + ScreenToClient and moves it as ClientToScreen +
// SetCursorPos (D2R.exe 3.3.93787 under D2RLoader 1.3.1: 0x14120A020 and 0x14120A290, read off the memory image).
// Called from the game's own code the point is mapped between the window's pixels and the game's; called from
// anything else (ReShade, D2RLoader, this plugin's laser and mouse look) it is the window's, as it always was.
using Convert = BOOL(WINAPI*)(HWND, LPPOINT);
Convert g_screenToClient = nullptr, g_clientToScreen = nullptr;
uintptr_t g_gameBegin = 0, g_gameEnd = 0;
std::atomic<bool> g_hooksReady{false};
std::once_flag g_setup;
std::atomic<uint32_t> g_fromGame{0};

bool FromGame(void* address) {
    const auto p = reinterpret_cast<uintptr_t>(address);
    return p >= g_gameBegin && p < g_gameEnd;
}
void TellFirstCall(void* address, const char* name) {
    if (g_fromGame.fetch_add(1) != 0) return;
    Say("vrcam: render size - the game's own %s call is mapped (first from D2R.exe+%llX)", name,
        (unsigned long long)(reinterpret_cast<uintptr_t>(address) - g_gameBegin));
}
BOOL WINAPI ScreenToClientHook(HWND h, LPPOINT p) {
    void* const from = _ReturnAddress();
    const BOOL ok = g_screenToClient(h, p);
    if (ok && p && FromGame(from)) {
        Pt q{p->x, p->y};
        if (g_window.Remap(h, &q, true)) { p->x = q.x; p->y = q.y; TellFirstCall(from, "ScreenToClient"); }
    }
    return ok;
}
BOOL WINAPI ClientToScreenHook(HWND h, LPPOINT p) {
    void* const from = _ReturnAddress();
    if (p && FromGame(from)) {
        Pt q{p->x, p->y};
        if (g_window.Remap(h, &q, false)) { p->x = q.x; p->y = q.y; TellFirstCall(from, "ClientToScreen"); }
    }
    return g_clientToScreen(h, p);
}

void Setup() {
    g_gameBegin = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(g_gameBegin);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(g_gameBegin + dos->e_lfanew);
    g_gameEnd = g_gameBegin + nt->OptionalHeader.SizeOfImage;
    HMODULE user = GetModuleHandleW(L"user32.dll");
    void* a = user ? reinterpret_cast<void*>(GetProcAddress(user, "ScreenToClient")) : nullptr;
    void* b = user ? reinterpret_cast<void*>(GetProcAddress(user, "ClientToScreen")) : nullptr;
    if (!a || !b) { Say("vrcam: render size - user32 has no ScreenToClient / ClientToScreen to map: the window's own size stays"); return; }
    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) { Say("vrcam: render size - MinHook did not start (%d): the window's own size stays", (int)init); return; }
    if (MH_CreateHook(a, reinterpret_cast<void*>(&ScreenToClientHook), reinterpret_cast<void**>(&g_screenToClient)) != MH_OK) {
        Say("vrcam: render size - ScreenToClient could not be hooked: the window's own size stays");
        return;
    }
    if (MH_CreateHook(b, reinterpret_cast<void*>(&ClientToScreenHook), reinterpret_cast<void**>(&g_clientToScreen)) != MH_OK) {
        MH_RemoveHook(a);
        Say("vrcam: render size - ClientToScreen could not be hooked: the window's own size stays");
        return;
    }
    const bool ok = MH_EnableHook(a) == MH_OK && MH_EnableHook(b) == MH_OK;
    if (!ok) { MH_DisableHook(a); MH_DisableHook(b); MH_RemoveHook(a); MH_RemoveHook(b); }
    g_hooksReady.store(ok);
    Say(ok ? "vrcam: render size - the game's cursor calls (ScreenToClient, ClientToScreen) are watched, D2R.exe %p-%p"
           : "vrcam: render size - the cursor hooks did not enable: the window's own size stays",
        (void*)g_gameBegin, (void*)g_gameEnd);
}

std::atomic<uint32_t> g_clientSizeTold{0};   // [render] vr_resolution_ui: said once for the interface (1) and the cursor (2)

}  // namespace

uint64_t ClientSizeAnswer(Asker asker, uint64_t native, bool on) {
    const uint64_t answer = CanvasAnswer(asker, g_window.Logical(g_attached.load()), native, on);
    if (answer != native) {   // said once for the interface and once for the cursor
        const uint32_t bit = asker == Asker::Ui ? 1u : 2u;
        if (!(g_clientSizeTold.fetch_or(bit) & bit)) {
            const Size n = FromNative(native), a = FromNative(answer);
            Say("vrcam: render size - the game's %s is given %ux%u, not the window's %ux%u ([render] vr_resolution_ui)",
                asker == Asker::Ui ? "interface (the whole picture)" : "cursor clamp (the whole picture)", a.w, a.h, n.w, n.h);
        }
    }
    return answer;
}
void ClientSizeReflow() {
    if (const HWND h = g_attached.load(); h && g_window.Logical(h)) g_window.Request(h, g_window.Logical(h), true);
}

void SetLogger(void (*log)(const char*)) { g_log = log; }
void Attach(HWND window) { g_attached.store(window); }
void SetHeroReady(bool ready) { g_hero.store(ready); }
void SetFrontendReady(bool ready) { g_frontend.store(ready); }
void SetReShade(bool on) { g_reshade.store(on); }
bool Handle(HWND window, UINT message, WPARAM w, LPARAM& l, WNDPROC original, LRESULT& result) {
    return g_window.Handle(window, message, w, l, original, result);
}

void Update(HWND window, bool running, Size recommended, Size maximum, Size output, const rendersize::Pick& pick, bool menuWide) {
    static Size toldWant, toldSel, toldWide;
    static rendersize::Pick toldPick;
    if (!window || window != g_attached.load()) return;   // vrcam's window procedure is not on this window (yet)
    if (menuWide && !MenuWideApplies(menuWide, g_reshade.load())) {
        static bool told = false;
        if (!told) {
            told = true;
            Say("vrcam: render size - the menus stay in the picture's shape: [render] menu_wide works only without ReShade ([render] no_reshade=1) - "
                "under ReShade a resize of the character screen froze a player's game (0.186)");
        }
        menuWide = false;
    }
    if (g_forgetFallback.exchange(false)) g_fallback.Reset();
    // Desktop with nothing of ours in force - the default - is no work at all, present after present.
    if (pick.kind == rendersize::Kind::Desktop && !menuWide && !g_ctl.InForce() && !g_window.Logical(window)) return;
    Size sel = Select(pick, recommended, maximum);
    // [render] menu_wide: the menus' 16:9 picture, as wide as the size picked - or the window, the window's own size picked
    Size wide;
    if (menuWide && running && maximum) {
        Size base = sel;
        RECT rc{};
        if (!base && GetClientRect(window, &rc)) base = {(uint32_t)rc.right, (uint32_t)rc.bottom};
        wide = Fit(Wide(base), maximum);
    }
    if (sel || wide) std::call_once(g_setup, Setup);
    if (!g_hooksReady.load()) sel = wide = {};   // no way to map the cursor: the window's own size stays
    const Size asked = sel;
    sel = g_fallback.Apply(sel);                 // a size the runtime refused: the last one it took
    if (wide && g_fallback.Apply(wide) != wide) wide = {};   // a refused menus' size: the menus as the size picked has them
    if (maximum && (!(pick == toldPick) || sel != toldSel)) {   // (the runtime's limits come with its session: nothing to say before)
        toldPick = pick;
        toldSel = sel;
        char what[48];
        if (pick.kind == rendersize::Kind::Headset) snprintf(what, sizeof what, "the headset's recommendation");
        else snprintf(what, sizeof what, "a square of %d", pick.side);
        if (pick.kind == rendersize::Kind::Desktop) { /* Desktop: nothing to say unless a size was in force (below) */ }
        else if (!sel)
            Say("vrcam: render size - %s asked for, but none can be had (%s): the window's own size stays", what,
                !g_hooksReady.load() && Select(pick, recommended, maximum) ? "no cursor hooks"
                : asked ? "the runtime refused it and took none before" : "below the least or no recommendation");
        else
            Say("vrcam: render size - %s: %ux%u (the headset recommends %ux%u, takes %ux%u at most)", what, sel.w, sel.h,
                recommended.w, recommended.h, maximum.w, maximum.h);
    }
    const uint64_t now = GetTickCount64();
    const Ask ask = g_menuWide.Step(now, menuWide, g_frontend.load(), g_hero.load(), sel, wide, g_ctl.InForce());
    if (ask.want == wide && wide && wide != toldWide)
        Say("vrcam: render size - the game's menus at 16:9: %ux%u ([render] menu_wide; %s in a game)", wide.w, wide.h,
            sel ? "the size picked" : "the window's own size");
    toldWide = ask.want == wide ? wide : Size{};
    Inputs in;
    in.now = now;
    in.running = running;
    in.hero = ask.now;
    in.want = ask.want;
    in.output = output;
    const Decision d = g_ctl.Step(in);
    g_window.Request(window, d.want, d.force);
    if (d.want != toldWant) {
        RECT rc{};
        GetClientRect(window, &rc);
        if (d.want) Say("vrcam: render size - the game is asked for %ux%u (its window stays %ldx%ld)", d.want.w, d.want.h, rc.right, rc.bottom);
        else if (toldWant) Say("vrcam: render size - the window's own size back (%ldx%ld)", rc.right, rc.bottom);
        toldWant = d.want;
    }
    if (d.note == Note::Retry) Say("vrcam: render size - the game's output is %ux%u, not %ux%u: asked again", output.w, output.h, d.want.w, d.want.h);
    else if (d.note == Note::GaveUp)
        Say("vrcam: render size - the game's output stays %ux%u, not %ux%u: given up until the choice or the game's options change",
            output.w, output.h, d.want.w, d.want.h);
}

void OptionsApplied() { g_ctl.OptionsApplied(GetTickCount64()); }
void SessionEnded() {
    g_ctl.Ended();
    g_forgetFallback.store(true);   // (a new session may hold what this one refused)
    g_window.Restore();
}
void SwapchainMade(uint32_t width, uint32_t height, bool ok) {
    const Size made{width, height}, asked = g_ctl.InForce();
    if (!asked || made != asked) return;   // not the size we asked for: the window's own, or one on its way out
    if (ok) { g_fallback.Took(made); return; }
    if (g_fallback.Refused(made)) {
        const Size back = g_fallback.Good();
        if (back) Say("vrcam: render size - the headset's runtime would not make swapchains of %ux%u: back to %ux%u, the last it took", made.w, made.h, back.w, back.h);
        else Say("vrcam: render size - the headset's runtime would not make swapchains of %ux%u and took none before: the window's own size", made.w, made.h);
    }
}
bool Active() { return (bool)g_ctl.InForce(); }
bool GameSize(HWND window, Size* out) {
    const Size s = g_window.Logical(window);
    if (!s || !out) return false;
    *out = s;
    return true;
}

}  // namespace vrres
