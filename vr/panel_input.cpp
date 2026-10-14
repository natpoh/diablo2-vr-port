// panel_input.cpp - the laser's mouse in the game's controller layout (native OpenXR,
// [openxr] laser; docs/plan_laser_menus.md).
//
// What to hook and why was found by bearstonem/d2r-pcvr (MIT, a fork of D2R VR 0.152: its
// vr/panel_input.cpp), for this same build; written again here on our signatures
// (game_sigs.h, area "Laser menus"), so another build finds them by pattern.
//
// Mouse input switches D2R to its keyboard-and-mouse layout: the panels are laid out anew, the
// pad's focus is gone, and BodyWalk's next pad report switches it all back. While the laser owns
// the pointer (OwnPointer, a lease the draw thread refreshes every frame) and the layout is the
// pad's, it stays the pad's:
//   - the input-event preflight (0x13B480: asks for KB/M through LayoutRequest(0) and clears the
//     pads) is skipped - false is "no layout change pending", the event goes on to the widgets;
//   - LayoutRequest(0) (0x847630) is refused; LayoutRequest(1), the pad's, is left alone.
// Only the UI's pointer handling sees KB/M - "pointer context", thread-local on the UI thread:
//   - UISystem's six pointer callbacks (move, left down/up, right down/up, wheel), and UI dispatch
//     of InputMessage's mouse commands (FNV-1a of the names at +0 and +0x88, as the game hashes
//     them), run in it: the layout query (0x77E10) says KB/M there, and the focus' snap to the
//     nearest control (0x14F1D0) is off - it took an NPC's Yes/No away to a vendor grid beneath;
//   - FocusManager's update (0x8676E0) runs in it while the laser points, not while the pad
//     navigates (UseController);
//   - a pointer event the UI leaves is taken anyway: a click on a panel's empty part never walks
//     the hero.
// Nothing of the game's is written: no mode byte, no flag. Out of the lease every hook passes
// straight through.
//
// Item tooltips at once ([openxr] laser_tooltips, vr/laser_tooltip.h; also found by d2r-pcvr, its
// InstallTooltips / ControllerMode / OnKey): in the pad's layout the item tooltip waits 1000 ms and
// the equipped-item comparison waits for Shift. The tooltip's producer queues the style with a
// "delayed" flag, which is IsControllerLayout's answer at one call site (0x2C3DCA), and asks the
// key-state query (0x120A100) for Shift at another (0x2C3F07). While the laser hovers, the first
// answers "no" and the second "down" at those two sites only, by the caller's return address; the
// game's own tooltip and comparison, only earlier. Optional: it goes in only after every hook above
// and only when its tick box is on; if anything of it is not found the laser works as before.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <intrin.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include <D2RLPlugin/api.h>

#include "log_async.h"   // [debug] log_async: the plugin's log lines through a ring and a writer thread (vr/log_queue.h); off = ctx->LogInfo as ever
#include "laser_tooltip.h"
#include "view_gate.h"
#include "panel_input.h"
#include "sigscan.h"

using namespace D2RL;

namespace panelinput {
namespace {

// Addresses in 3.3.93787 (the keys of game_sigs.h) and the bytes each starts with there.
constexpr uint64_t RVA_INPUT_ACTIVITY = 0x13B480, RVA_ACTIVITY_ASKS_KBM = 0x13B5EA, RVA_LAYOUT_REQUEST = 0x847630,
                   RVA_IS_CONTROLLER = 0x77E10, RVA_UI_DISPATCH = 0x843D90, RVA_MESSAGE_COMMAND = 0x866FBD,
                   RVA_FOCUS_UPDATE = 0x8676E0, RVA_CURSOR_SNAP = 0x14F1D0, RVA_FOCUS_SNAP_CALL = 0x867BD0;
// move, left down, left up, right down, right up, wheel
constexpr uint64_t RVA_POINTER[6] = {0x83F340, 0x83F400, 0x83F870, 0x83FCC0, 0x840120, 0x840DC0};
// sub rsp, 48h; call; test al, al; jne
const uint8_t kSigInputActivity[17] = {0x48,0x83,0xEC,0x48,0xE8,0xF7,0xC8,0xF3,0xFF,0x84,0xC0,0x0F,0x85,0xC2,0x01,0x00,0x00};
// xor ecx, ecx; call LayoutRequest - the preflight asks for KB/M
const uint8_t kSigActivityAsksKbm[7] = {0x33,0xC9,0xE8,0x3F,0xC0,0x70,0x00};
// mov edx, ecx; mov rcx, [manager]; test rcx, rcx; jne transition; xor al, al; ret
const uint8_t kSigLayoutRequest[21] = {0x8B,0xD1,0x48,0x8B,0x0D,0x37,0x8B,0xBF,0x02,0x48,0x85,0xC9,0x0F,0x85,0xFE,0xDD,0x05,0x00,0x32,0xC0,0xC3};
// sub rsp, 28h; call GameGlobals; cmp byte [rax+0DC8h], 0; jne; cmp byte [forced], 0
const uint8_t kSigIsController[25] = {0x48,0x83,0xEC,0x28,0xE8,0xF7,0xE1,0xFF,0xFF,0x80,0xB8,0xC8,0x0D,0x00,0x00,0x00,0x75,0x10,
                                      0x80,0x3D,0x4B,0xB4,0x9A,0x02,0x00};
const uint8_t kSigUiDispatch[16] = {0x40,0x53,0x56,0x57,0x48,0x83,0xEC,0x20,0x4C,0x89,0x7C,0x24,0x58,0x4C,0x8B,0xF9};
// mov rax, [rsi+88h]; mov rcx, FNV-1a("RightMouseButtonDown") - the command's hash at +0x88
const uint8_t kSigMessageCommand[17] = {0x48,0x8B,0x86,0x88,0x00,0x00,0x00,0x48,0xB9,0x8C,0x93,0x7C,0xAC,0x2C,0x17,0xD5,0x02};
const uint8_t kSigFocusUpdate[19] = {0x40,0x55,0x41,0x57,0x48,0x8D,0xAC,0x24,0x38,0xFD,0xFF,0xFF,0x48,0x81,0xEC,0xC8,0x03,0x00,0x00};
// movzx eax, byte [snap]; ret
const uint8_t kSigCursorSnap[8] = {0x0F,0xB6,0x05,0x25,0x95,0x93,0x02,0xC3};
// call CursorSnap; test al, al; je
const uint8_t kSigFocusSnapCall[13] = {0xE8,0xFB,0x75,0x8E,0xFF,0x84,0xC0,0x0F,0x84,0xAC,0x01,0x00,0x00};
const uint8_t kSigPointerMove[14] = {0x40,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xF9,0xE8,0x32,0x8A,0x83,0xFF};
// the five button callbacks start alike (a 0x320-byte frame)
const uint8_t kSigPointerButton[21] = {0x48,0x89,0x74,0x24,0x20,0x55,0x48,0x8D,0xAC,0x24,0xE0,0xFD,0xFF,0xFF,0x48,0x81,0xEC,
                                       0x20,0x03,0x00,0x00};
// Item tooltips at once (laser_tooltip.h): two call sites in the item tooltip's producer and the key-state query
constexpr uint64_t RVA_TOOLTIP_DELAY = 0x2C3DCA, RVA_TOOLTIP_COMPARE = 0x2C3F07, RVA_KEY_STATE = 0x120A100;
// call IsControllerLayout; lea r8, [rsp+20h]; mov [rbp+114h], al - the result is the queued tooltip's "delayed" flag
const uint8_t kSigTooltipDelay[16] = {0xE8,0x41,0x40,0xDB,0xFF,0x4C,0x8D,0x44,0x24,0x20,0x88,0x85,0x14,0x01,0x00,0x00};
// mov ecx, 10h (VK_SHIFT); call KeyState; test eax, eax; jne +8; cmp [rdi+632h], al; je - the comparison's Shift
const uint8_t kSigTooltipCompare[22] = {0xB9,0x10,0x00,0x00,0x00,0xE8,0xEF,0x61,0xF4,0x00,0x85,0xC0,0x75,0x08,
                                        0x38,0x87,0x32,0x06,0x00,0x00,0x74,0x72};
// sub rsp, 28h; call [GetAsyncKeyState]; shr eax, 0Fh; and eax, 1; add rsp, 28h; ret
const uint8_t kSigKeyState[21] = {0x48,0x83,0xEC,0x28,0xFF,0x15,0x86,0x6E,0xAA,0x00,0xC1,0xE8,0x0F,0x83,0xE0,0x01,0x48,0x83,0xC4,0x28,0xC3};
constexpr ULONGLONG kLeaseMs = 300;

using ActivityFn = bool (*)(void* event);
using RequestFn = bool (*)(int method);
using QueryFn = bool (*)();
using DispatchFn = void (*)(const void* message);
using FocusFn = void (*)(void* self, bool sound);
using PointerFn = bool (*)(void* event);
using KeyFn = int (*)(int vk);

const PluginContext* g_ctx = nullptr;
ActivityFn g_origActivity = nullptr;
RequestFn g_origRequest = nullptr;
QueryFn g_origIsController = nullptr, g_origCursorSnap = nullptr;
DispatchFn g_origDispatch = nullptr;
FocusFn g_origFocus = nullptr;
PointerFn g_origPointer[6] = {};
struct In { bool activity, request, controller, dispatch, focus, snap, pointer[6]; };
In g_in{};   // the timer thread's: which hooks are in
std::atomic<bool> g_ready{false};
std::atomic<ULONGLONG> g_until{0};   // GetTickCount64: the laser owns the pointer until then
std::atomic<bool> g_pointing{false};  // the focus follows the pointer (not the pad)
thread_local bool t_pointer = false;  // inside the UI's pointer handling, the laser's

KeyFn g_origKey = nullptr;
bool g_keyIn = false;                     // the timer thread's: the key-state hook is in
std::atomic<bool> g_tipWanted{false};     // [openxr] laser_tooltips
std::atomic<bool> g_tipReady{false};      // the hooks are in and g_delaySite / g_compareSite are known
uintptr_t g_delaySite = 0, g_compareSite = 0;   // the two queries' return addresses; written before g_tipReady
lasertip::HoverLease g_hover;             // the draw thread's: the laser points at the menus' picture, holding nothing

void Log(const char* text) { if (g_ctx) logasync::Info(g_ctx, text); }

bool Owns() { return g_ready.load(std::memory_order_acquire) && GetTickCount64() < g_until.load(); }

// The pointer context for one call: on or off, the one around it back after.
struct Context {
    bool was;
    explicit Context(bool on) : was(t_pointer) { t_pointer = on; }
    ~Context() { t_pointer = was; }
    Context(const Context&) = delete;
};

constexpr uint64_t Fnv(const char* s) {
    uint64_t h = 14695981039346656037ull;
    for (; *s; ++s) h = (h ^ (uint8_t)*s) * 1099511628211ull;
    return h;
}
bool MouseMessage(uint64_t target, uint64_t command) {
    static constexpr uint64_t kInput = Fnv("InputMessage");
    static constexpr uint64_t kMouse[] = {Fnv("MouseMove"), Fnv("MouseScroll"), Fnv("LeftMouseButtonDown"), Fnv("LeftMouseButtonUp"),
                                          Fnv("RightMouseButtonDown"), Fnv("RightMouseButtonUp")};
    static_assert(kMouse[4] == 0x02D5172CAC7C938Cull, "the game's hash (kSigMessageCommand)");
    if (target != kInput) return false;
    for (const uint64_t c : kMouse) if (command == c) return true;
    return false;
}

// The pad's layout is on and the laser owns the pointer: what would leave the layout is skipped.
bool Keep() {
    if (!Owns() || !g_origIsController()) return false;
    static std::atomic<bool> told{false};
    if (!told.exchange(true)) Log("vrcam: laser - the game kept in its controller layout while the laser is the mouse");
    return true;
}

bool HookActivity(void* event) { return Keep() ? false : g_origActivity(event); }
bool HookRequest(int method) { return method == 0 && Keep() ? false : g_origRequest(method); }
void TellFirst(std::atomic<bool>& told, const char* text) { if (!told.exchange(true)) Log(text); }

bool HookIsController() {
    if (t_pointer) return false;
    // the item tooltip's "delayed" flag, asked for at its call site: not delayed while the laser hovers
    const bool at = g_tipReady.load(std::memory_order_acquire) && viewgate::F4() && lasertip::AtSite((uintptr_t)_ReturnAddress(), g_delaySite);   // (F4 only)
    if (at && lasertip::NoDelay(g_hover.Active(GetTickCount64()), at)) {
        static std::atomic<bool> told{false};
        TellFirst(told, "vrcam: laser - an item tooltip shown at once (its delay flag answered 'no delay' under the laser)");
        return false;
    }
    return g_origIsController();
}
// The key-state query: Shift is down for the equipped-item comparison while the laser hovers, nowhere else.
int HookKeyState(int vk) {
    const bool at = g_tipReady.load(std::memory_order_acquire) && viewgate::F4() && lasertip::AtSite((uintptr_t)_ReturnAddress(), g_compareSite);   // (F4 only)
    if (at && lasertip::ShiftDown(g_hover.Active(GetTickCount64()), at, vk)) {
        static std::atomic<bool> told{false};
        TellFirst(told, "vrcam: laser - the equipped-item comparison shown with no Shift held (the comparison's Shift query answered 'down' under the laser)");
        return 1;
    }
    return g_origKey(vk);
}
bool HookCursorSnap() { return !t_pointer && g_origCursorSnap(); }
void HookDispatch(const void* message) {
    uint64_t target = 0, command = 0;
    if (message) { memcpy(&target, message, 8); memcpy(&command, (const char*)message + 0x88, 8); }
    // a nested message (a panel opened by a click) sees the real layout again
    Context c(Owns() && MouseMessage(target, command));
    g_origDispatch(message);
}
void HookFocus(void* self, bool sound) {
    Context c(Owns() && g_pointing.load());
    g_origFocus(self, sound);
}
template <int I> bool HookPointer(void* event) {
    const bool own = Owns();
    Context c(own);
    const bool used = g_origPointer[I](event);
    return own || used;   // what the UI leaves is not passed on to the hero
}
constexpr PointerFn kHookPointer[6] = {&HookPointer<0>, &HookPointer<1>, &HookPointer<2>, &HookPointer<3>, &HookPointer<4>, &HookPointer<5>};

const uint8_t* PointerSig(int i) { return i ? kSigPointerButton : kSigPointerMove; }
uint32_t PointerSigSize(int i) { return i ? sizeof kSigPointerButton : sizeof kSigPointerMove; }

std::atomic<ULONGLONG> g_ownedAt{0};   // the last OwnPointer

// Once the laser's hooks are in, and only while the tick box is on. Optional: what is not found (yet - code the game
// has not run is not readable) leaves the laser with the game's own delay; told once after 30 s.
void InstallTooltips() {
    if (g_tipReady.load() || !g_tipWanted.load()) return;
    std::string missing;
    auto need = [&](const char* name, uint64_t rva, const uint8_t* sig, uint32_t size) {
        if (!d2rsig::Check(rva, sig, size)) missing += std::string(missing.empty() ? "" : ", ") + name;
    };
    need("TooltipDelay", RVA_TOOLTIP_DELAY, kSigTooltipDelay, sizeof kSigTooltipDelay);
    need("TooltipCompare", RVA_TOOLTIP_COMPARE, kSigTooltipCompare, sizeof kSigTooltipCompare);
    if (!g_keyIn) need("KeyState", RVA_KEY_STATE, kSigKeyState, sizeof kSigKeyState);
    const uintptr_t delay = d2rsig::Addr(RVA_TOOLTIP_DELAY), compare = d2rsig::Addr(RVA_TOOLTIP_COMPARE);
    if (missing.empty() && (!delay || !compare)) missing = "TooltipDelay / TooltipCompare (no address)";
    if (missing.empty() && !g_keyIn)
        g_keyIn = d2rsig::Hook(RVA_KEY_STATE, kSigKeyState, sizeof kSigKeyState, (void*)&HookKeyState, (void**)&g_origKey);
    if (!missing.empty() || !g_keyIn) {
        static ULONGLONG since = 0;
        static bool told = false;
        if (!since) since = GetTickCount64();
        if (!told && GetTickCount64() - since > 30000) {
            told = true;
            char b[400];
            snprintf(b, sizeof b, "vrcam: laser - item tooltips at once: %.250s after 30 s - another build, or code the game has not run; "
                     "the laser works, with the game's own tooltip delay and Shift for the comparison",
                     missing.empty() ? "the key-state hook would not go in (tried again)" : ("not found (yet): " + missing).c_str());
            Log(b);
        }
        return;
    }
    g_delaySite = delay + 5;      // the call is 5 bytes
    g_compareSite = compare + 10; // mov ecx, 10h (5) + call (5)
    g_tipReady.store(true, std::memory_order_release);
    Log("vrcam: laser - item tooltips at once: the tooltip's delay flag and the comparison's Shift query answered under the laser, "
        "at their two call sites only ([openxr] laser_tooltips)");
}
}  // namespace

void Init(const PluginContext* ctx) { g_ctx = ctx; }
bool Ready() { return g_ready.load(); }

void Install() {
    if (!g_ctx) return;
    if (g_ready.load()) { InstallTooltips(); return; }   // the laser's own hooks are in: the tooltips' go in after them
    // Every address first, as expected - not a hook in before all of them are: half of them in
    // would leave the game half in KB/M. What does not read yet (code the game has not run) is
    // looked at again on the next tick.
    struct Need { const char* name; uint64_t rva; const uint8_t* sig; uint32_t size; bool in; };
    const Need need[] = {
        {"InputActivity", RVA_INPUT_ACTIVITY, kSigInputActivity, sizeof kSigInputActivity, g_in.activity},
        {"ActivityAsksKbm", RVA_ACTIVITY_ASKS_KBM, kSigActivityAsksKbm, sizeof kSigActivityAsksKbm, false},
        {"LayoutRequest", RVA_LAYOUT_REQUEST, kSigLayoutRequest, sizeof kSigLayoutRequest, g_in.request},
        {"IsControllerLayout", RVA_IS_CONTROLLER, kSigIsController, sizeof kSigIsController, g_in.controller},
        {"UiDispatch", RVA_UI_DISPATCH, kSigUiDispatch, sizeof kSigUiDispatch, g_in.dispatch},
        {"MessageCommand", RVA_MESSAGE_COMMAND, kSigMessageCommand, sizeof kSigMessageCommand, false},
        {"FocusUpdate", RVA_FOCUS_UPDATE, kSigFocusUpdate, sizeof kSigFocusUpdate, g_in.focus},
        {"CursorSnap", RVA_CURSOR_SNAP, kSigCursorSnap, sizeof kSigCursorSnap, g_in.snap},
        {"FocusSnapCall", RVA_FOCUS_SNAP_CALL, kSigFocusSnapCall, sizeof kSigFocusSnapCall, false},
    };
    std::string missing;
    for (const Need& n : need)
        if (!n.in && !d2rsig::Check(n.rva, n.sig, n.size)) missing += std::string(missing.empty() ? "" : ", ") + n.name;
    static const char* const kPointerName[6] = {"UiPointerMove", "UiLeftDown", "UiLeftUp", "UiRightDown", "UiRightUp", "UiScroll"};
    for (int i = 0; i < 6; ++i)
        if (!g_in.pointer[i] && !d2rsig::Check(RVA_POINTER[i], PointerSig(i), PointerSigSize(i)))
            missing += std::string(missing.empty() ? "" : ", ") + kPointerName[i];
    if (!missing.empty()) {
        static ULONGLONG since = 0;
        static bool told = false;
        if (!since) since = GetTickCount64();
        if (!told && GetTickCount64() - since > 30000) {
            told = true;
            char b[400];
            snprintf(b, sizeof b, "vrcam: laser - not found (yet) after 30 s: %.250s - another build, or code the game has not run; "
                     "the laser still clicks, but the game switches to its mouse layout on it", missing.c_str());
            Log(b);
        }
        return;
    }
    if (!g_in.controller) g_in.controller = d2rsig::Hook(RVA_IS_CONTROLLER, kSigIsController, sizeof kSigIsController, (void*)&HookIsController, (void**)&g_origIsController);
    if (!g_in.request) g_in.request = d2rsig::Hook(RVA_LAYOUT_REQUEST, kSigLayoutRequest, sizeof kSigLayoutRequest, (void*)&HookRequest, (void**)&g_origRequest);
    if (!g_in.activity) g_in.activity = d2rsig::Hook(RVA_INPUT_ACTIVITY, kSigInputActivity, sizeof kSigInputActivity, (void*)&HookActivity, (void**)&g_origActivity);
    if (!g_in.dispatch) g_in.dispatch = d2rsig::Hook(RVA_UI_DISPATCH, kSigUiDispatch, sizeof kSigUiDispatch, (void*)&HookDispatch, (void**)&g_origDispatch);
    if (!g_in.focus) g_in.focus = d2rsig::Hook(RVA_FOCUS_UPDATE, kSigFocusUpdate, sizeof kSigFocusUpdate, (void*)&HookFocus, (void**)&g_origFocus);
    if (!g_in.snap) g_in.snap = d2rsig::Hook(RVA_CURSOR_SNAP, kSigCursorSnap, sizeof kSigCursorSnap, (void*)&HookCursorSnap, (void**)&g_origCursorSnap);
    bool all = g_in.controller && g_in.request && g_in.activity && g_in.dispatch && g_in.focus && g_in.snap;
    for (int i = 0; i < 6; ++i) {
        if (!g_in.pointer[i])
            g_in.pointer[i] = d2rsig::Hook(RVA_POINTER[i], PointerSig(i), PointerSigSize(i), (void*)kHookPointer[i], (void**)&g_origPointer[i]);
        all = all && g_in.pointer[i];
    }
    if (!all) {
        static bool told = false;
        if (!told) { told = true; Log("vrcam: laser - a hook would not go in (tried again): the game may switch to its mouse layout on the laser's clicks"); }
        return;
    }
    g_ready.store(true, std::memory_order_release);
    Log("vrcam: laser - the controller layout is kept while the laser is the mouse: preflight, layout request, layout query, "
        "UI dispatch, focus and its snap, and the six pointer callbacks hooked");
    InstallTooltips();
}

void OwnPointer() {
    const ULONGLONG now = GetTickCount64();
    g_pointing.store(true);
    g_until.store(now + kLeaseMs);
    g_ownedAt.store(now);
}

void UseController() {
    g_pointing.store(false);
    g_hover.Update(GetTickCount64(), false);   // the pad's focus is the game's own tooltip, with its delay
}

void WantTooltips(bool on) { g_tipWanted.store(on); }
void ItemHover(bool hovering) { g_hover.Update(GetTickCount64(), hovering); }

void UiTick(bool inGame, bool menu) {
    static ULONGLONG done = 0;   // the lease answered (its last OwnPointer)
    const ULONGLONG at = g_ownedAt.load();
    if (!g_ready.load() || !at || at == done || Owns() || menu || !inGame) return;
    done = at;
    if (GetTickCount64() - at > 10000 || g_origIsController()) return;   // long ago (the pad has pressed since), or the pad's already
    Log("vrcam: laser - in a game with no panel open in the mouse layout after the laser: the controller layout asked for "
        "(the pad's aim and pick-up targets)");
    g_origRequest(1);
}

}  // namespace panelinput
