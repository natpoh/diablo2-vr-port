// belt_refill.cpp - see belt_refill.h. [belt] refill_key: a tap of the button is the game's own Shift + click at the pointer, a hold is the game's own
// "hold to populate belt" (BeltWidget's handler, its own functions in its own order, on the game's UI thread behind an SEH guard: a fault turns the hold
// off for the session). The belt is read through the SDK before and after each press for the one log line.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <D2RLPlugin/api.h>

#include "belt_refill.h"
#include "log_async.h"
#include "sigscan.h"
#include "typed_keys.h"
#include "ui_ticket.h"

using namespace D2RL;

namespace gamestate { bool InventoryOpen(); }   // (vr/gamestate.cpp: the player's inventory is among the open panels)

namespace beltrefill {
namespace {

// Addresses in 3.3.93787 (D2RLoader 1.3.1's layout). Each function is checked against its first bytes in this build before it is ever called, and the
// game's own fill (BeltWidget's input handler 0x2283E0) is checked to call 0x386FF0 and 0xECA70 where this file says it does.
struct Fn { uint64_t rva; uint8_t bytes[16]; const char* name; };
const Fn kFns[] = {
    {0x846190, {0x48, 0x83, 0xEC, 0x28, 0x48, 0x83, 0x3D, 0xD4, 0x9F, 0xBF, 0x02, 0x00, 0x74, 0x18, 0xE8, 0xFD}, "the UI root"},
    {0x855C90, {0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x48, 0x83, 0xEC, 0x20, 0x4C, 0x89, 0x7C, 0x24, 0x68}, "the UI's widget by name"},
    {0x8460D0, {0x48, 0x8B, 0x05, 0x99, 0xA0, 0xBF, 0x02, 0x48, 0x85, 0xC0, 0x74, 0x08, 0x0F, 0xB6, 0x80, 0xB8}, "the UI's own busy flag (the fill's first gate)"},
    {0x226060, {0x40, 0x53, 0x55, 0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0xE9, 0xE8, 0x71, 0x91, 0xF2, 0xFF, 0x48}, "BeltWidget: can the belt be filled"},
    {0x2A7810, {0x48, 0x83, 0xEC, 0x28, 0x44, 0x8B, 0x81, 0xC4, 0x05, 0x00, 0x00, 0x41, 0x83, 0xF8, 0xFF, 0x75}, "the widget's player"},
    {0x34A360, {0x48, 0x89, 0x5C, 0x24, 0x18, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF1, 0x48, 0x85, 0xC9}, "a unit's inventory"},
    {0x388A70, {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x75, 0x1B, 0x88, 0x4C}, "the inventory's request under way"},
    {0x386FF0, {0x40, 0x55, 0x53, 0x41, 0x55, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xC9, 0x48, 0x81, 0xEC, 0xC8}, "the belt fill's plan"},
    {0xECA70, {0x48, 0x81, 0xEC, 0xA8, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x05, 0x4A, 0xE8, 0x8D, 0x02, 0x48, 0x33}, "the belt fill's command (0x5D)"},
    {0x1A0780, {0x40, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xD1, 0x48, 0x81, 0xEC}, "the UI's sound"},
    {0x1614F0, {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0xE8, 0x45, 0x31, 0xFE, 0xFF, 0x48, 0x8B, 0xD8, 0xE8, 0xCD}, "the inventory's refresh after a move"},
    // the game's own fill inside BeltWidget's handler: `call 0x386FF0; test eax,eax; je; mov eax,[rsp+30]; test eax,eax; jle` and `call 0xECA70; ...`
    {0x228654, {0xE8, 0x97, 0xE9, 0x15, 0x00, 0x85, 0xC0, 0x74, 0x39, 0x8B, 0x44, 0x24, 0x30, 0x85, 0xC0, 0x7E}, "BeltWidget's fill: its plan"},
    {0x228674, {0x10, 0xE8, 0xF6, 0x43, 0xEC, 0xFF, 0x45, 0x33, 0xC9, 0xC6, 0x44, 0x24, 0x20, 0x00, 0x45, 0x33}, "BeltWidget's fill: its command"},
};
enum { kUiRoot, kWidget, kUiBusy, kCanFill, kWidgetPlayer, kInventory, kInvBusy, kPlan, kSend, kSound, kRefresh, kSitePlan, kSiteSend, kFns_ };
static_assert(sizeof kFns / sizeof kFns[0] == kFns_, "one entry per function");
constexpr uint64_t RVA_BELT_VTABLE = 0x1CE2FA0;   // BeltWidget's vtable: the widget named "Belt" is the belt (vr/belt_drop.cpp)
constexpr int kFillSound = 0xEB;                  // the sound BeltWidget's fill plays

using RootFn = uintptr_t (*)(int);
using WidgetFn = uintptr_t (*)(uintptr_t root, const char* name);
using FlagFn = uint8_t (*)();
using CanFillFn = uint8_t (*)(uintptr_t widget);
using WidgetPlayerFn = uintptr_t (*)(uintptr_t widget);
using InventoryFn = uintptr_t (*)(uintptr_t unit, const char* file, int line);
using InvBusyFn = uintptr_t (*)(uintptr_t inv);
using PlanFn = int (*)(uintptr_t unit, void* ids, void* places, void* slots, int* count);
using SendFn = void (*)(const void* ids, const void* places, const void* slots, uint8_t count);
using SoundFn = void (*)(int sound, int a, int b, int c, uint8_t d);
using VoidFn = void (*)();

const PluginContext* g_ctx = nullptr;
const ThreadService* g_threads = nullptr;
const InventoryService* g_inventory = nullptr;
std::atomic<int> g_ok{-1};   // the game's fill: -1 not checked yet, 1 this build's, 0 not (or a fault: the hold off for the session)

// the settings (Configure, the update thread)
std::atomic<bool> g_on{false};
std::atomic<int> g_button{1}, g_key{1};
std::atomic<uint32_t> g_holdMs{kHoldDefaultMs};
// the inputs
std::atomic<bool> g_inWorld{false};
std::atomic<bool> g_padDown{false}, g_padPointing{false};   // the XR thread: the button held (while ours), the laser on the picture as the mouse
std::atomic<bool> g_keyDown{false};                          // the window's thread: the key held (its press was ours)

void Log(const char* text) { if (g_ctx) logasync::Info(g_ctx, text); }
template <typename... A> void LogF(const char* fmt, A... a) {
    char b[512];
    snprintf(b, sizeof b, fmt, a...);
    Log(b);
}

template <class F> F At(int i) { return reinterpret_cast<F>(d2rsig::Addr(kFns[i].rva)); }

bool Check() {
    if (g_ok.load() >= 0) return g_ok.load() == 1;
    for (const Fn& f : kFns)
        if (!d2rsig::Addr(f.rva) || !d2rsig::Check(f.rva, f.bytes, sizeof f.bytes)) {
            LogF("belt refill: the game's %s (0x%llX) is not as in 3.3.93787 - no belt fill on the hold (the tap's Shift + click still works)", f.name,
                 (unsigned long long)f.rva);
            g_ok.store(0);
            return false;
        }
    g_ok.store(1);
    Log("belt refill: ready - BeltWidget's own fill (hold to populate belt: its gate, its plan, its command 0x5D) is where 3.3.93787 has it");
    return true;
}

bool ReadAt(uintptr_t at, void* out, size_t n) noexcept {
    if (at < 0x10000) return false;
    __try { std::memcpy(out, reinterpret_cast<const void*>(at), n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// ---- the UI thread's part ----
enum Job : int { kJobNone, kJobBefore, kJobFill, kJobAfter };
enum FillResult : int { kFillSent, kFillNothing, kFillInvBusy, kFillNoWidget, kFillNoPlayer, kFillNoPlan, kFillUiBusy, kFillOff, kFillFault };
const char* FillWhy(int r) {
    switch (r) {
        case kFillNothing: return "the game's own gate says nothing can go into the belt (no empty slot a potion of the inventory fits, or something on the cursor)";
        case kFillInvBusy: return "the game is still waiting for the server on an item: nothing sent";
        case kFillNoWidget: return "the game's belt widget was not found: nothing sent";
        case kFillNoPlayer: return "no player on the belt widget: nothing sent";
        case kFillNoPlan: return "the game's plan moved nothing: nothing sent";
        case kFillUiBusy: return "the game's interface is busy: nothing sent";
        case kFillOff: return "the game's fill is not where 3.3.93787 has it: nothing sent";
        case kFillFault: return "a fault in the game's fill - off for this session";
        default: return "";
    }
}
constexpr int kInvMax = 160;
struct Snap {
    bool ok = false;
    Belt belt;
    int nInv = 0;
    uint32_t invId[kInvMax] = {}, invCode[kInvMax] = {};
};
struct Result {
    Snap snap;
    int fill = kFillNothing;
    Plan plan;
};
uiticket::Ticket g_queued;
std::atomic<int> g_job{kJobNone};
std::atomic<int> g_doneJob{kJobNone};   // the job the UI thread finished (its result in g_result, written before)
Result g_result;

Inventory::IterationAction __cdecl SnapItem(const PluginContext*, const Items::ItemInfo* i, void* user) noexcept {
    auto* s = static_cast<Snap*>(user);
    if (!i) return Inventory::IterationAction::Continue;
    if (i->container == Items::ItemContainer::Belt) {
        int slot = -1;   // as gamestate's ReadBelt: x 0..15 with y 0, or column x and row y
        if (i->x >= 0 && i->x < kSlots && i->y == 0) slot = i->x;
        else if (i->x >= 0 && i->x < kColumns && i->y >= 0 && i->y < kSlots / kColumns) slot = i->x + kColumns * i->y;
        if (slot >= 0) { s->belt.code[slot] = i->code; s->belt.id[slot] = i->runtimeId; }
    } else if (i->container == Items::ItemContainer::Inventory && s->nInv < kInvMax) {
        s->invId[s->nInv] = i->runtimeId;
        s->invCode[s->nInv] = i->code;
        ++s->nInv;
    }
    return Inventory::IterationAction::Continue;
}
void TakeSnap(const PluginContext* ctx, Snap* s) {
    *s = Snap{};
    PlayerHandle player = InvalidPlayerHandle;
    if (!g_inventory || !g_inventory->forEachInventoryItem || !g_inventory->getLocalPlayer ||
        g_inventory->getLocalPlayer(ctx, &player) != Inventory::Result::Success || player == InvalidPlayerHandle)
        return;
    const Inventory::ItemFilter filter{sizeof filter, 0,
                                       Items::ContainerBit(Items::ItemContainer::Belt) | Items::ContainerBit(Items::ItemContainer::Inventory), 0};
    s->ok = g_inventory->forEachInventoryItem(ctx, player, &filter, &SnapItem, s) == Inventory::Result::Success;
}

// The game's own fill, as BeltWidget's handler does it on a long press: its gates, its plan, its command, its sound and refresh. No C++ objects here (SEH).
bool FillNow(int* result, Plan* plan) noexcept {
    __try {
        const uintptr_t root = At<RootFn>(kUiRoot)(0);
        const uintptr_t w = root ? At<WidgetFn>(kWidget)(root, "Belt") : 0;
        uintptr_t vt = 0;
        if (!w || !ReadAt(w, &vt, sizeof vt) || vt != d2rsig::Addr(RVA_BELT_VTABLE)) { *result = kFillNoWidget; return true; }
        if (At<FlagFn>(kUiBusy)() != 0) { *result = kFillUiBusy; return true; }
        if (!At<CanFillFn>(kCanFill)(w)) { *result = kFillNothing; return true; }
        const uintptr_t unit = At<WidgetPlayerFn>(kWidgetPlayer)(w);
        const uintptr_t inv = unit ? At<InventoryFn>(kInventory)(unit, "belt_refill.cpp", __LINE__) : 0;
        if (!inv) { *result = kFillNoPlayer; return true; }
        if (At<InvBusyFn>(kInvBusy)(inv) != 0) { *result = kFillInvBusy; return true; }
        // the game's buffers are 16 ids (4 bytes), 16 places (4 bytes) and 16 slots (1 byte); ours are bigger
        alignas(16) uint8_t ids[256] = {}, places[256] = {}, slots[256] = {};
        int count = 0;
        const int planned = At<PlanFn>(kPlan)(unit, ids, places, slots, &count);
        if (!planned || count <= 0) { *result = kFillNoPlan; return true; }
        At<SendFn>(kSend)(ids, places, slots, (uint8_t)count);
        At<SoundFn>(kSound)(kFillSound, 0, 0, 0, 0);
        At<VoidFn>(kRefresh)();
        plan->n = count > kPlanMax ? kPlanMax : count;
        for (int i = 0; i < plan->n; ++i) { std::memcpy(&plan->id[i], ids + 4 * i, 4); plan->slot[i] = slots[i]; }
        *result = kFillSent;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void __cdecl Run(const PluginContext* ctx, void* user) noexcept {
    if (!g_queued.Claim(uiticket::FromUser(user))) return;
    const int job = g_job.load();
    if (job == kJobNone) return;
    g_result = Result{};
    TakeSnap(ctx, &g_result.snap);
    if (job == kJobFill) {
        if (!Check()) g_result.fill = kFillOff;
        else if (!FillNow(&g_result.fill, &g_result.plan)) { g_ok.store(0); g_result.fill = kFillFault; }
    }
    g_job.store(kJobNone);
    g_doneJob.store(job);
}

bool Queue(int job) {
    if (!g_ctx || !g_threads || !g_threads->runOnUiThread) return false;
    g_doneJob.store(kJobNone);
    g_job.store(job);
    const uint32_t token = g_queued.Start(GetTickCount64());
    if (!token) return true;   // one is queued still: it runs this job
    if (g_threads->runOnUiThread(g_ctx, &Run, uiticket::ToUser(token)) != Threads::Result::Success) { g_queued.Claim(token); return false; }
    return true;
}

// ---- the keys (the game's own input: SendInput, as the laser's own Shift and click) ----
void SendShift(bool down) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = VK_LSHIFT;
    in.ki.wScan = 0x2A;   // the game reads keys by scan code (vr/input/actions.cpp SendShift)
    in.ki.dwFlags = KEYEVENTF_SCANCODE | (down ? 0 : KEYEVENTF_KEYUP);
    in.ki.dwExtraInfo = typedkeys::kOwnKeyTag;
    SendInput(1, &in, sizeof in);
}
void SendLeft(bool down) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    SendInput(1, &in, sizeof in);
}

// ---- the press, on vrcam's frame ----
enum class St { Idle, WaitBefore, Clicking, WaitFill, Settle, WaitAfter };
St g_st = St::Idle;
Press g_press;
Click g_click;
bool g_isHold = false;
char g_byText[48] = {};       // which control: "B (controller)" / "keyboard B"
char g_where[64] = {};        // the tap: where the click went
uint64_t g_since = 0, g_settleAt = 0;
Snap g_before;
int g_fill = kFillNothing;
Plan g_plan;
PlanView g_planView;
uint32_t g_presses = 0;

constexpr uint64_t kSettleMs = 700;    // the server's answer and the SDK's view of the belt after it
constexpr uint64_t kUiWaitMs = 1500;   // a UI-thread job not done by then: given up

void Finish() { g_st = St::Idle; g_isHold = false; }

void LogResult(const Snap& after) {
    const Moved m = g_before.ok && after.ok ? Diff(g_before.belt, after.belt) : Moved{};
    char kinds[96];
    KindsText(m.count, kinds, sizeof kinds);
    const char* read = g_before.ok && after.ok ? "" : " (the belt could not be read through the SDK - not counted)";
    ++g_presses;
    if (g_isHold) {
        char cols[200] = {};
        size_t at = 0;
        for (int c = 0; c < kColumns; ++c) {
            char k[96];
            KindsText(g_planView.column[c], k, sizeof k);
            if (!std::strcmp(k, "none")) continue;
            const int len = snprintf(cols + at, sizeof cols - at, "%scolumn %d: %s", at ? "; " : "", c + 1, k);
            if (len > 0 && at + (size_t)len < sizeof cols) at += (size_t)len;
        }
        LogF("belt refill: %s held - the game's own belt fill (BeltWidget's hold to populate belt, command 0x5D): the game planned %d (%s) - %d moved into "
             "the belt (%s)%s (%u)", g_byText, g_planView.total, at ? cols : "no column", m.total, kinds, read, g_presses);
    } else {
        LogF("belt refill: %s tapped - the game's own Shift + click %s: %d moved into the belt (%s)%s%s (%u)", g_byText, g_where, m.total, kinds,
             m.total ? "" : " - no potion under the pointer, its column full, or the game moved nothing", read, g_presses);
    }
}

}  // namespace

void Init(const PluginContext* ctx) {
    g_ctx = ctx;
    const ThreadService* t = nullptr;
    g_threads = ctx && ctx->QueryService(&t) == ServiceQueryResult::Success ? t : nullptr;
    const InventoryService* inv = nullptr;
    g_inventory = ctx && ctx->QueryService(&inv) == ServiceQueryResult::Success ? inv : nullptr;
}

void Configure(bool on, int button, int key, float holdMs) {
    button = button < 0 || button >= kButtons ? 0 : button;
    key = key < 0 || key >= kKeys ? 0 : key;
    const uint32_t hold = ClampHold(holdMs);
    bool changed = g_on.exchange(on) != on;   // (each stored: no short cut past one)
    changed = (g_button.exchange(button) != button) || changed;
    changed = (g_key.exchange(key) != key) || changed;
    changed = (g_holdMs.exchange(hold) != hold) || changed;
    static bool told = false;
    if (changed || !told) {
        told = true;
        LogF("belt refill: %s ([belt] refill_key) - controller %s, keyboard %c, hold %u ms: with the inventory open a tap is the game's Shift + click at the "
             "pointer (the laser in VR, the mouse cursor flat), a hold the game's own belt fill", on ? "ON" : "off", PadName(button),
             KeyVk(key) ? (char)KeyVk(key) : '-', hold);
    }
}

Pad PadButton() { return g_on.load() ? PadOf(g_button.load()) : Pad{}; }

bool PadWanted(bool laserOk) {
    When w;
    w.on = g_on.load() && PadOf(g_button.load()).bit != 0;
    w.inventoryOpen = gamestate::InventoryOpen();
    w.inWorld = g_inWorld.load();
    w.laserOk = laserOk;
    return PadOurs(w);
}

void FeedPad(bool down, bool pointing) {
    g_padDown.store(down);
    g_padPointing.store(pointing);
}

bool KeyMessage(unsigned msg, uintptr_t w, intptr_t l, bool chatOpen) {
    if (msg != WM_KEYDOWN && msg != WM_KEYUP) return false;
    const unsigned vk = KeyVk(g_key.load());
    if (!vk || w != vk) return false;
    if (msg == WM_KEYUP) {   // the release of a press that was ours is ours too (the game never saw its press)
        if (!g_keyDown.exchange(false)) return false;
        return true;
    }
    if (g_keyDown.load()) return true;   // (a repeat of a press that is ours)
    When wh;
    wh.on = g_on.load();
    wh.inventoryOpen = gamestate::InventoryOpen();
    wh.inWorld = g_inWorld.load();
    wh.chatOpen = chatOpen;
    if (!KeyOurs(wh) || (l & (1 << 30))) return false;   // (a repeat of a press that was the game's stays the game's)
    g_keyDown.store(true);
    return true;
}

void Tick(bool inWorld) {
    g_inWorld.store(inWorld);
    const uint64_t now = GetTickCount64();
    const bool on = g_on.load();
    // a held key whose release went elsewhere (the window lost the keyboard) is let go here
    if (g_keyDown.load()) {
        const unsigned vk = KeyVk(g_key.load());
        if (!vk || !(GetAsyncKeyState((int)vk) & 0x8000)) g_keyDown.store(false);
    }
    const bool pad = g_padDown.load(), key = g_keyDown.load();
    const bool allowed = on && inWorld && gamestate::InventoryOpen();
    if (!allowed) {
        g_press.Cancel();
        if (g_click.Busy()) {   // stopped half-way: let go of what is held
            bool shift = false, button = false;
            g_click.Abort(&shift, &button);
            if (button) SendLeft(false);
            if (shift) SendShift(false);
            Finish();
        }
    }
    const Ev ev = allowed ? g_press.Step(pad || key, now, g_holdMs.load()) : Ev::None;
    static bool lastPad = false;   // the control pressed last (a tap is seen on the release, when neither is down)
    if (pad) lastPad = true;
    else if (key) lastPad = false;
    if (ev != Ev::None) {
        if (lastPad) snprintf(g_byText, sizeof g_byText, "%s (controller)", PadName(g_button.load()));
        else snprintf(g_byText, sizeof g_byText, "keyboard %c", KeyVk(g_key.load()) ? (char)KeyVk(g_key.load()) : '?');
    }
    if (ev == Ev::Tap && g_st == St::Idle) {
        if (lastPad && !g_padPointing.load()) {
            ++g_presses;
            LogF("belt refill: %s tapped - the laser is not on the inventory as the mouse (off the picture, or the pad navigates): nothing clicked (%u)", g_byText,
                 g_presses);
        } else {
            snprintf(g_where, sizeof g_where, "%s", lastPad ? "at the laser's point" : "at the mouse cursor");
            g_isHold = false;
            g_since = now;
            g_st = Queue(kJobBefore) ? St::WaitBefore : St::Idle;
            if (g_st == St::Idle) Log("belt refill: the game's UI thread could not be asked - nothing done");
        }
    } else if (ev == Ev::Hold && g_st == St::Idle) {
        g_isHold = true;
        g_since = now;
        g_st = Queue(kJobFill) ? St::WaitFill : St::Idle;
        if (g_st == St::Idle) Log("belt refill: the game's UI thread could not be asked - nothing done");
    }
    switch (g_st) {
        case St::WaitBefore:
            if (g_doneJob.load() == kJobBefore) {
                g_doneJob.store(kJobNone);
                g_before = g_result.snap;
                g_click.Start(now);
                g_st = St::Clicking;
            } else if (now - g_since > kUiWaitMs) { Log("belt refill: the game's UI thread did not run in time - nothing clicked"); Finish(); }
            break;
        case St::Clicking:
            switch (g_click.Step(now)) {
                case Send::ShiftDown: SendShift(true); break;
                case Send::ButtonDown: SendLeft(true); break;
                case Send::ButtonUp: SendLeft(false); break;
                case Send::ShiftUp: SendShift(false); g_settleAt = now + kSettleMs; g_st = St::Settle; break;
                default: break;
            }
            break;
        case St::WaitFill:
            if (g_doneJob.load() == kJobFill) {
                g_doneJob.store(kJobNone);
                g_before = g_result.snap;
                g_fill = g_result.fill;
                g_plan = g_result.plan;
                const Snap& b = g_before;
                g_planView = View(g_plan, [&b](uint32_t id) {
                    for (int i = 0; i < b.nInv; ++i) if (b.invId[i] == id) return b.invCode[i];
                    return 0u;
                });
                if (g_fill != kFillSent) {
                    ++g_presses;
                    LogF("belt refill: %s held - the game's own belt fill: %s (%u)", g_byText, FillWhy(g_fill), g_presses);
                    Finish();
                } else {
                    g_settleAt = now + kSettleMs;
                    g_st = St::Settle;
                }
            } else if (now - g_since > kUiWaitMs) { Log("belt refill: the game's UI thread did not run in time - nothing sent"); Finish(); }
            break;
        case St::Settle:
            if (now >= g_settleAt) {
                g_since = now;
                g_st = Queue(kJobAfter) ? St::WaitAfter : St::Idle;
                if (g_st == St::Idle) Finish();
            }
            break;
        case St::WaitAfter:
            if (g_doneJob.load() == kJobAfter) {
                g_doneJob.store(kJobNone);
                LogResult(g_result.snap);
                Finish();
            } else if (now - g_since > kUiWaitMs) { Snap none; LogResult(none); Finish(); }
            break;
        default: break;
    }
}

}  // namespace beltrefill
