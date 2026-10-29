// belt_drop.cpp - see belt_drop.h. The game's own calls, on the game's UI thread (where its key commands and its inventory manager run), every one behind an SEH
// guard; a fault turns the drop off for the session (a thrown full potion then just goes back on the belt).

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

#include "belt_drop.h"
#include "log_async.h"
#include "sigscan.h"
#include "ui_ticket.h"

using namespace D2RL;

namespace beltdrop {
namespace {

// Addresses in 3.3.93787 (D2RLoader 1.3.1's layout). Each function is checked against its first bytes in this build before it is ever called.
struct Fn { uint64_t rva; uint8_t bytes[16]; const char* name; };
const Fn kFns[] = {
    {0x2287B0, {0x48, 0x89, 0x5C, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x70, 0x48, 0x8B, 0x05, 0x07, 0x2B, 0x7A}, "BeltWidget::PickupItem"},
    {0x15BDF0, {0x40, 0x55, 0x53, 0x41, 0x55, 0x41, 0x56, 0x48, 0x8D, 0xAC, 0x24, 0x58, 0xFE, 0xFF, 0xFF, 0x48}, "the cursor item's drop"},
    {0x2282D0, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48}, "BeltWidget::PutCursorItem"},
    {0x15F100, {0x48, 0x83, 0xEC, 0x28, 0xE8, 0xC7, 0xC1, 0xF2, 0xFF, 0x8B, 0xC8, 0xE8, 0x70, 0xB3, 0xF3, 0xFF}, "the cursor's item"},
    {0x846190, {0x48, 0x83, 0xEC, 0x28, 0x48, 0x83, 0x3D, 0xD4, 0x9F, 0xBF, 0x02, 0x00, 0x74, 0x18, 0xE8, 0xFD}, "the UI root"},
    {0x855C90, {0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x48, 0x83, 0xEC, 0x20, 0x4C, 0x89, 0x7C, 0x24, 0x68}, "the UI's widget by name"},
    {0x388390, {0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x63, 0xFA, 0x48, 0x8B, 0xD9}, "the belt's item in a slot"},
    {0x1C7360, {0x48, 0x83, 0xEC, 0x28, 0xBA, 0x02, 0x00, 0x00, 0x00, 0xE8, 0xA2, 0xE4, 0x12, 0x00, 0x48, 0x85}, "an item's request under way"},
    {0x8D3D0, {0x0F, 0xB6, 0x05, 0x0E, 0x60, 0x99, 0x02, 0xC3, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC}, "the client's busy flag"},
    {0x13DE60, {0x48, 0x83, 0xEC, 0x28, 0x83, 0xB9, 0x40, 0x0E, 0x00, 0x00, 0x00, 0x75, 0x10, 0xE8, 0xEE, 0x92}, "the input's lock"},
    {0x13CE90, {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00, 0x8B}, "the input"},
    {0xCE500, {0x48, 0x63, 0xC1, 0x48, 0x8D, 0x0D, 0x96, 0xC8, 0x95, 0x02, 0x0F, 0xB6, 0x04, 0x08, 0xC3, 0xCC}, "a UI state"},
    {0x9A480, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x83, 0xF9, 0x08, 0x0F, 0x83, 0x85}, "the client's player"},
    {0x8B2D0, {0x8B, 0x05, 0x2E, 0x84, 0x99, 0x02, 0xC3, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC}, "the local player's index"},
    {0x34A360, {0x48, 0x89, 0x5C, 0x24, 0x18, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF1, 0x48, 0x85, 0xC9}, "a unit's inventory"},
};
enum { kPickup, kDrop, kPutBack, kCursor, kUiRoot, kWidget, kSlotItem, kBusy, kClientBusy, kInputLock, kInput, kUiState, kPlayer, kLocalIndex, kInventory, kFns_ };
static_assert(sizeof kFns / sizeof kFns[0] == kFns_, "one entry per function");
constexpr uint64_t RVA_BELT_VTABLE = 0x1CE2FA0;   // BeltWidget's vtable: the widget named "Belt" is the belt
constexpr size_t kItemId = 0x08;                  // a unit's id (0x34A330)
// [hands] belt_throw_merc: the belt key's own path (0x228D10) asks "to the mercenary?" (0x160AC0: Shift / the pad's left trigger, false while the UI gate 0x8D3B0
// is up) and hands the answer to the use of the slot - `call 0x160AC0; movzx r8d,al; mov edx,esi; mov rcx,rdi; call <use>` at 0x228D7E. The use (D2RLoader's
// hook of ActivateBeltItem, the game's 0x229440 behind it) is taken from that very call, so ours is what the key calls, with the answer "yes".
constexpr uint64_t RVA_KEY_USE_CALL = 0x228D7E, RVA_UI_GATE = 0x8D3B0;
const uint8_t kKeyUseBytes[19] = {0xE8, 0, 0, 0, 0, 0x44, 0x0F, 0xB6, 0xC0, 0x8B, 0xD6, 0x48, 0x8B, 0xCF, 0xE8, 0, 0, 0, 0};   // 0 = a rel32, not compared
const uint8_t kUiGateBytes[16] = {0x48, 0x8B, 0x05, 0x29, 0x63, 0x99, 0x02, 0x48, 0x85, 0xC0, 0x74, 0x08, 0x80, 0x78, 0x5C, 0x01};
using UseFn = void (*)(uintptr_t widget, int column, uint8_t toMerc);
std::atomic<uintptr_t> g_use{0};   // the use the key path calls; 0 not found

using PickupFn = void (*)(uintptr_t widget, int* slot);
using DropFn = uintptr_t (*)(uintptr_t item, uint8_t transfer);
using CursorFn = uintptr_t (*)();
using RootFn = uintptr_t (*)(int);
using WidgetFn = uintptr_t (*)(uintptr_t root, const char* name);
using SlotItemFn = uintptr_t (*)(uintptr_t inv, int slot);
using BusyFn = uint8_t (*)(uintptr_t item);
using FlagFn = uint8_t (*)();
using LockFn = uint8_t (*)(uintptr_t input);
using InputFn = uintptr_t (*)();
using StateFn = uint8_t (*)(int state);
using PlayerFn = uintptr_t (*)(uint32_t index);
using IndexFn = uint32_t (*)();
using InventoryFn = uintptr_t (*)(uintptr_t unit, const char* file, int line);

const PluginContext* g_ctx = nullptr;
const ThreadService* g_threads = nullptr;
uiticket::Ticket g_queued;
std::atomic<int> g_ok{-1};   // -1 not checked yet, 1 the functions are this build's, 0 not (or a fault: off for the session)
// the request: Ask (any thread) -> the UI thread
std::atomic<uint64_t> g_request{0};   // column + 1 in the top 32 bits, the id below; 0 none
std::atomic<bool> g_busy{false};      // a sequence under way (Ask refuses another)
// the UI thread's
Phase g_phase = Phase::Idle;
int g_column = -1;
uint32_t g_id = 0;
ULONGLONG g_askedAt = 0, g_onCursorAt = 0;
bool g_dropSent = false;

void Log(const char* text) { if (g_ctx) logasync::Info(g_ctx, text); }
template <typename... A> void LogF(const char* fmt, A... a) {
    char b[400];
    snprintf(b, sizeof b, fmt, a...);
    Log(b);
}

template <class F> F At(int i) { return reinterpret_cast<F>(d2rsig::Addr(kFns[i].rva)); }

bool Check() {
    if (g_ok.load() >= 0) return g_ok.load() == 1;
    for (const Fn& f : kFns)
        if (!d2rsig::Addr(f.rva) || !d2rsig::Check(f.rva, f.bytes, sizeof f.bytes)) {
            LogF("belt drop: the game's %s (0x%llX) is not as in 3.3.93787 - no drop by the game: a full potion let go of away from the belt goes back on it", f.name,
                 (unsigned long long)f.rva);
            g_ok.store(0);
            return false;
        }
    g_ok.store(1);
    Log("belt drop: ready - the belt slot's click and the cursor's drop are where 3.3.93787 has them");
    return true;
}

bool ReadAt(uintptr_t at, void* out, size_t n) noexcept {
    if (at < 0x10000) return false;
    __try { std::memcpy(out, reinterpret_cast<const void*>(at), n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
uint32_t IdOf(uintptr_t unit) {
    uint32_t id = 0;
    return unit && ReadAt(unit + kItemId, &id, sizeof id) ? id : 0;
}

// The use of a belt slot the key path calls, read out of that call (and the UI gate checked): once; 0 when they are not as in 3.3.93787.
uintptr_t UseOfKey() {
    static std::atomic<int> state{0};   // 0 not looked, 1 found, -1 not
    if (state.load() == 0) {
        uint8_t got[sizeof kKeyUseBytes] = {};
        const uintptr_t at = d2rsig::Addr(RVA_KEY_USE_CALL);
        bool ok = at && d2rsig::Addr(RVA_UI_GATE) && d2rsig::Check(RVA_UI_GATE, kUiGateBytes, sizeof kUiGateBytes) && ReadAt(at, got, sizeof got);
        for (size_t i = 0; ok && i < sizeof got; ++i) {
            const bool rel = (i >= 1 && i <= 4) || i >= 15;
            if (!rel && got[i] != kKeyUseBytes[i]) ok = false;
        }
        if (ok) {
            int32_t rel = 0;
            std::memcpy(&rel, got + 15, 4);
            g_use.store(at + 14 + 5 + (intptr_t)rel);
        }
        state.store(ok ? 1 : -1);
        if (ok) LogF("belt drop: the belt key's use of a slot found (the key path calls %p) - a potion thrown to the mercenary is given the game's own way", (void*)g_use.load());
        else Log("belt drop: the belt key's path is not as in 3.3.93787 - no potion to the mercenary by a throw");
    }
    return g_use.load();
}

// What is seen now, and the belt widget (0: none). False on a fault.
bool Look(See* s, uintptr_t* widget) noexcept {
    __try {
        *widget = 0;
        s->gatesOpen = At<FlagFn>(kClientBusy)() == 0 && At<LockFn>(kInputLock)(At<InputFn>(kInput)()) != 0 && At<StateFn>(kUiState)(9) == 0 &&
                       At<StateFn>(kUiState)(0x16) == 0;
        const uintptr_t player = At<PlayerFn>(kPlayer)(At<IndexFn>(kLocalIndex)());
        const uintptr_t inv = player ? At<InventoryFn>(kInventory)(player, "belt_drop.cpp", 1) : 0;
        const uintptr_t item = inv ? At<SlotItemFn>(kSlotItem)(inv, g_column) : 0;
        s->slotOurs = item && IdOf(item) == g_id;
        s->slotBusy = s->slotOurs && At<BusyFn>(kBusy)(item) != 0;
        const uintptr_t cursor = At<CursorFn>(kCursor)();
        s->cursorEmpty = cursor == 0;
        s->cursorOurs = cursor && IdOf(cursor) == g_id;
        s->cursorBusy = s->cursorOurs && At<BusyFn>(kBusy)(cursor) != 0;
        const uintptr_t root = At<RootFn>(kUiRoot)(0);
        const uintptr_t w = root ? At<WidgetFn>(kWidget)(root, "Belt") : 0;
        uintptr_t vt = 0;
        if (w && ReadAt(w, &vt, sizeof vt) && vt == d2rsig::Addr(RVA_BELT_VTABLE)) *widget = w;
        if (!*widget) s->gatesOpen = false;   // no belt widget: no click on it
        s->mercGateOpen = reinterpret_cast<FlagFn>(d2rsig::Addr(RVA_UI_GATE))() == 0;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool Do(Act a, uintptr_t widget) noexcept {
    __try {
        int slot = g_column;
        switch (a) {
            case Act::Pickup: At<PickupFn>(kPickup)(widget, &slot); break;
            case Act::Drop: { const uintptr_t cursor = At<CursorFn>(kCursor)(); if (cursor && IdOf(cursor) == g_id) At<DropFn>(kDrop)(cursor, 0); break; }
            case Act::PutBack: At<PickupFn>(kPutBack)(widget, &slot); break;
            case Act::Give: reinterpret_cast<UseFn>(g_use.load())(widget, g_column, 1); break;
            default: break;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void Fault(const char* where) {
    g_ok.store(0);
    g_phase = Phase::Idle;
    g_busy.store(false);
    LogF("belt drop: a fault in %s - off for this session (a full potion let go of away from the belt goes back on it)", where);
}

// The UI thread: one step of the sequence.
void __cdecl Run(const PluginContext*, void* user) noexcept {
    if (!g_queued.Claim(uiticket::FromUser(user)) || g_ok.load() != 1) return;
    if (g_phase == Phase::Idle) {
        const uint64_t r = g_request.exchange(0);
        if (!r) return;
        g_column = (int)((r >> 32) & 0xFF) - 1;
        g_id = (uint32_t)r;
        g_phase = (r >> 40) & 1 ? Phase::Giving : Phase::Asked;
        g_askedAt = GetTickCount64();
        g_onCursorAt = 0;
        g_dropSent = false;
    }
    See s;
    uintptr_t widget = 0;
    if (!Look(&s, &widget)) { Fault("its look at the belt and the cursor"); return; }
    const ULONGLONG now = GetTickCount64();
    if (s.cursorOurs && !g_onCursorAt) g_onCursorAt = now;
    s.sinceS = (double)(now - g_askedAt) / 1000.0;
    s.onCursorS = g_onCursorAt ? (double)(now - g_onCursorAt) / 1000.0 : 0.0;
    s.dropSent = g_dropSent;
    const Next n = Step(g_phase, s);
    if (n.act != Act::None) {
        if (n.act != Act::Drop && !widget) { Fault("finding the belt widget"); return; }
        if (!Do(n.act, widget)) {
            Fault(n.act == Act::Pickup ? "the belt slot's click" : n.act == Act::Drop ? "the cursor's drop" : n.act == Act::Give ? "the use to the mercenary" : "the put-back");
            return;
        }
        if (n.act == Act::Drop) g_dropSent = true;
    }
    if (n.why[0]) LogF("belt drop: belt %d, item %u - %s (%.2f s)", g_column + 1, g_id, n.why, s.sinceS);
    g_phase = n.phase;
    if (g_phase == Phase::Done || g_phase == Phase::Stopped) {
        g_phase = Phase::Idle;
        g_busy.store(false);
    }
}

}  // namespace

void Init(const PluginContext* ctx) {
    g_ctx = ctx;
    const ThreadService* t = nullptr;
    g_threads = ctx && ctx->QueryService(&t) == ServiceQueryResult::Success ? t : nullptr;
}

bool Ask(int column, uint32_t id) {
    if (column < 0 || column > 3 || !id || !Check()) return false;
    bool idle = false;
    if (!g_busy.compare_exchange_strong(idle, true)) return false;
    g_request.store(((uint64_t)(column + 1) << 32) | id);
    return true;
}

bool Give(int column, uint32_t id) {
    if (column < 0 || column > 3 || !id || !Check() || !UseOfKey()) return false;
    bool idle = false;
    if (!g_busy.compare_exchange_strong(idle, true)) return false;
    g_request.store((1ull << 40) | ((uint64_t)(column + 1) << 32) | id);
    return true;
}

void Tick() {
    if (!g_ctx || !g_threads || !g_threads->runOnUiThread || g_ok.load() != 1 || !g_busy.load()) return;
    if (const uint32_t token = g_queued.Start(GetTickCount64()))
        if (g_threads->runOnUiThread(g_ctx, &Run, uiticket::ToUser(token)) != Threads::Result::Success) g_queued.Claim(token);
}

}  // namespace beltdrop
