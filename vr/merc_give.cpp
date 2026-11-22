// merc_give.cpp - see merc_give.h. The watch runs on vrcam's timer (the weight model's let-go weapon against the mercenary's capsule); the game's own item
// moves run on the game's UI thread (where its controller layout runs them), every call behind an SEH guard - a fault turns the feature off for the session.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <D2RLPlugin/api.h>

#include "log_async.h"
#include "melee_touch.h"
#include "merc_give.h"
#include "merc_tint.h"
#include "sigscan.h"
#include "ui_ticket.h"
#include "weight.h"

using namespace D2RL;

namespace mercgive {
namespace {

// Addresses in 3.3.93787 (D2RLoader 1.3.1's layout). Each function is checked against its first bytes in this build before it is ever called.
struct Fn { uint64_t rva; uint8_t bytes[16]; const char* name; };
const Fn kFns[] = {
    // the equipment slot's click (InventoryPanel's slot widget, 0x2CABA0 / 0x2CAC00; the pad's quick move 0x2AA7E0): (cursor or grid item, body location,
    // controller layout, owner unit id (-1 the hero), the page) - with no item and the controller flag, the slot's item goes into the page's free cell (C->S 0x58);
    // with an inventory item, that item goes into the slot (C->S 0x56)
    {0x15C600, {0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xAC}, "the equipment slot's click"},
    // equip an item on the hireling (the merc portrait's drop 0x1E72F8, the pad's quick move 0x2AA8B3): (item, from the inventory, {body location, set})
    {0x15C220, {0x40, 0x53, 0x57, 0x41, 0x54, 0x41, 0x55, 0x48, 0x83, 0xEC, 0x78, 0x49, 0x8B, 0xF8, 0x44, 0x0F}, "the equip on the hireling"},
    // can the hireling wear it: (item, ask even with his panel closed) - his class and its kind (0x159E60), its requirements (0x36BC50)
    {0x159D20, {0x40, 0x53, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B, 0xD9, 0x84, 0xD2, 0x75, 0x14, 0xB9, 0x1E, 0x00}, "the hireling's can-wear"},
    {0x3886D0, {0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x63, 0xFA, 0x48, 0x8B, 0xD9}, "an inventory's item at a body location"},
    {0x13ABC0, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41}, "a player's pet of a kind"},
    {0x9A5D0, {0x4C, 0x63, 0xCA, 0x48, 0x8D, 0x05, 0x36, 0x93, 0x98, 0x02, 0x8B, 0xD1, 0x44, 0x8B, 0xC1, 0x49}, "the client's unit by id"},
    {0x15F100, {0x48, 0x83, 0xEC, 0x28, 0xE8, 0xC7, 0xC1, 0xF2, 0xFF, 0x8B, 0xC8, 0xE8, 0x70, 0xB3, 0xF3, 0xFF}, "the cursor's item"},
    {0x1C7360, {0x48, 0x83, 0xEC, 0x28, 0xBA, 0x02, 0x00, 0x00, 0x00, 0xE8, 0xA2, 0xE4, 0x12, 0x00, 0x48, 0x85}, "an item's request under way"},
    {0x8D3D0, {0x0F, 0xB6, 0x05, 0x0E, 0x60, 0x99, 0x02, 0xC3, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC}, "the client's busy flag"},
    {0x8D3B0, {0x48, 0x8B, 0x05, 0x29, 0x63, 0x99, 0x02, 0x48, 0x85, 0xC0, 0x74, 0x08, 0x80, 0x78, 0x5C, 0x01}, "the hireling's UI gate"},
    {0x13DE60, {0x48, 0x83, 0xEC, 0x28, 0x83, 0xB9, 0x40, 0x0E, 0x00, 0x00, 0x00, 0x75, 0x10, 0xE8, 0xEE, 0x92}, "the input's lock"},
    {0x13CE90, {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00, 0x8B}, "the input"},
    {0xCE500, {0x48, 0x63, 0xC1, 0x48, 0x8D, 0x0D, 0x96, 0xC8, 0x95, 0x02, 0x0F, 0xB6, 0x04, 0x08, 0xC3, 0xCC}, "a UI state"},
    {0x9A480, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x83, 0xF9, 0x08, 0x0F, 0x83, 0x85}, "the client's player"},
    {0x8B2D0, {0x8B, 0x05, 0x2E, 0x84, 0x99, 0x02, 0xC3, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC}, "the local player's index"},
    {0x34A360, {0x48, 0x89, 0x5C, 0x24, 0x18, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF1, 0x48, 0x85, 0xC9}, "a unit's inventory"},
};
enum { kSlotClick, kMercEquip, kMercCan, kBodyItem, kPet, kUnit, kCursor, kBusy, kClientBusy, kMercGate, kInputLock, kInput, kUiState, kPlayer, kLocalIndex,
       kInventory, kFns_ };
static_assert(sizeof kFns / sizeof kFns[0] == kFns_, "one entry per function");
constexpr size_t kUnitId = 0x08;    // a unit's id (0x34A330)
constexpr int kPetHireling = 7;     // the hireling among a player's pets (0x159D20, 0x15C220)
constexpr uint32_t kTypeMonster = 1, kTypeItem = 4;

struct MercLoc { int bodyLoc; uint8_t set; uint8_t pad[3]; };   // 0x15C220's third argument: {body location, set} - set 0: the game picks it
using SlotClickFn = void (*)(uintptr_t item, int bodyLoc, uint8_t controller, int ownerId, uint8_t page);
using MercEquipFn = void (*)(uintptr_t item, uint8_t fromInventory, MercLoc* loc);
using MercCanFn = uint8_t (*)(uintptr_t item, uint8_t evenClosed);
using BodyItemFn = uintptr_t (*)(uintptr_t inv, int bodyLoc);
using PetFn = uint32_t (*)(uintptr_t player, int kind, int index);
using UnitFn = uintptr_t (*)(uint32_t id, uint32_t type);
using CursorFn = uintptr_t (*)();
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
std::atomic<bool> g_on{true};
std::atomic<float> g_reachM{0.30f};
std::atomic<bool> g_trace{false};
// the request: the watch (vrcam's timer) -> the UI thread
std::atomic<int> g_request{0};       // the hand slot + 1; 0 none
std::atomic<bool> g_busy{false};     // a sequence under way (no second one)
// the UI thread's
Phase g_phase = Phase::Idle;
int g_slot = 0, g_bodyLoc = 4;
uint32_t g_id = 0;
ULONGLONG g_phaseAt = 0, g_askedAt = 0;
// the timer's
Watch g_watch;
meleetouch::HirelingView g_merc;     // the mercenary as found at the release (unit 0: none)
uint32_t g_mercRelease = 0;

void Log(const char* text) { if (g_ctx) logasync::Info(g_ctx, text); }
template <typename... A> void LogF(const char* fmt, A... a) {
    char b[480];
    snprintf(b, sizeof b, fmt, a...);
    Log(b);
}

template <class F> F At(int i) { return reinterpret_cast<F>(d2rsig::Addr(kFns[i].rva)); }

bool Check() {
    if (g_ok.load() >= 0) return g_ok.load() == 1;
    for (const Fn& f : kFns)
        if (!d2rsig::Addr(f.rva) || !d2rsig::Check(f.rva, f.bytes, sizeof f.bytes)) {
            LogF("merc give: the game's %s (0x%llX) is not as in 3.3.93787 - no weapon given to the mercenary from the hand", f.name, (unsigned long long)f.rva);
            g_ok.store(0);
            return false;
        }
    g_ok.store(1);
    Log("merc give: ready - the game's equipment slot quick move and its equip on the hireling are where 3.3.93787 has them");
    return true;
}

bool ReadAt(uintptr_t at, void* out, size_t n) noexcept {
    if (at < 0x10000) return false;
    __try { std::memcpy(out, reinterpret_cast<const void*>(at), n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
uint32_t IdOf(uintptr_t unit) {
    uint32_t id = 0;
    return unit && ReadAt(unit + kUnitId, &id, sizeof id) ? id : 0;
}

// The hero's inventory, his mercenary (0: none) and the mercenary's inventory.
struct Units { uintptr_t player = 0, inv = 0, merc = 0, mercInv = 0; };
Units UnitsNow() {   // (inside Look's guard)
    Units u;
    u.player = At<PlayerFn>(kPlayer)(At<IndexFn>(kLocalIndex)());
    if (!u.player) return u;
    u.inv = At<InventoryFn>(kInventory)(u.player, "merc_give.cpp", 1);
    const uint32_t mercId = At<PetFn>(kPet)(u.player, kPetHireling, 0);
    if (mercId != 0xFFFFFFFFu) u.merc = At<UnitFn>(kUnit)(mercId, kTypeMonster);
    if (u.merc) u.mercInv = At<InventoryFn>(kInventory)(u.merc, "merc_give.cpp", 2);
    return u;
}

// What is seen now; *item: our item's unit (0: not known). False on a fault.
bool Look(See* s, uintptr_t* item, bool askCan) noexcept {
    __try {
        *item = 0;
        const Units u = UnitsNow();
        s->gatesOpen = u.player && u.inv && At<FlagFn>(kClientBusy)() == 0 && At<LockFn>(kInputLock)(At<InputFn>(kInput)()) != 0 && At<StateFn>(kUiState)(9) == 0 &&
                       At<StateFn>(kUiState)(0x16) == 0 && At<FlagFn>(kMercGate)() == 0 && At<CursorFn>(kCursor)() == 0;
        const uintptr_t inHand = u.inv ? At<BodyItemFn>(kBodyItem)(u.inv, g_bodyLoc) : 0;
        s->inHand = inHand && IdOf(inHand) == g_id;
        const uintptr_t it = g_id ? At<UnitFn>(kUnit)(g_id, kTypeItem) : 0;
        s->itemFound = it != 0;
        s->itemBusy = it && At<BusyFn>(kBusy)(it) != 0;
        s->onMerc = false;
        if (u.mercInv)
            for (int loc = 1; loc <= 5 && !s->onMerc; ++loc) {
                const uintptr_t m = At<BodyItemFn>(kBodyItem)(u.mercInv, loc);
                s->onMerc = m && IdOf(m) == g_id;
            }
        s->mercCan = askCan && u.merc && it && At<MercCanFn>(kMercCan)(it, 1) != 0;
        *item = it;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool Do(Act a, uintptr_t item) noexcept {
    __try {
        switch (a) {
            case Act::ToGrid: At<SlotClickFn>(kSlotClick)(0, g_bodyLoc, 1, -1, 0); break;
            case Act::Give: { MercLoc loc{0, 0, {}}; At<MercEquipFn>(kMercEquip)(item, 1, &loc); break; }
            case Act::PutBack: At<SlotClickFn>(kSlotClick)(item, g_bodyLoc, 1, -1, 0); break;
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
    LogF("merc give: a fault in %s - off for this session", where);
}

// Our item in the hand slot: its id (0: none). (UI thread, guarded.)
uint32_t HandItem(int bodyLoc) noexcept {
    __try {
        const Units u = UnitsNow();
        const uintptr_t it = u.inv ? At<BodyItemFn>(kBodyItem)(u.inv, bodyLoc) : 0;
        return IdOf(it);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// The UI thread: one step of the sequence.
void __cdecl Run(const PluginContext*, void* user) noexcept {
    if (!g_queued.Claim(uiticket::FromUser(user)) || g_ok.load() != 1) return;
    const ULONGLONG now = GetTickCount64();
    if (g_phase == Phase::Idle) {
        const int r = g_request.exchange(0);
        if (!r) return;
        g_slot = (r - 1) & 1;
        g_bodyLoc = 4 + g_slot;   // the hands as the game's slots ask for them: 4 the right, 5 the left
        g_id = HandItem(g_bodyLoc);
        g_phase = Phase::Asked;
        g_phaseAt = g_askedAt = now;
        if (!g_id) {
            LogF("merc give: the %s hand slot holds nothing - nothing sent", g_slot ? "left" : "right");
            g_phase = Phase::Idle;
            g_busy.store(false);
            return;
        }
    }
    See s;
    uintptr_t item = 0;
    if (!Look(&s, &item, g_phase == Phase::Asked)) { Fault("its look at the hand, the inventory and the mercenary"); return; }
    s.sinceS = (double)(now - g_phaseAt) / 1000.0;
    const Next n = Step(g_phase, s);
    if (n.act != Act::None) {
        if (n.act != Act::ToGrid && !item) { Fault("finding the weapon's unit"); return; }
        if (!Do(n.act, item)) {
            Fault(n.act == Act::ToGrid ? "the slot's quick move to the inventory" : n.act == Act::Give ? "the equip on the hireling" : "the equip back into the hand");
            return;
        }
    }
    if (n.why[0]) LogF("merc give: %s hand's item %u - %s (%.2f s)", g_slot ? "left" : "right", g_id, n.why, (double)(now - g_askedAt) / 1000.0);
    if (n.phase != g_phase) g_phaseAt = now;
    g_phase = n.phase;
    if (g_phase == Phase::Done || g_phase == Phase::Stopped) {
        g_phase = Phase::Idle;
        g_busy.store(false);
    }
}

bool Ask(int slot) {
    if (!Check()) return false;
    bool idle = false;
    if (!g_busy.compare_exchange_strong(idle, true)) return false;
    g_request.store((slot & 1) + 1);
    return true;
}

// The watch: the let-go weapon against the mercenary's capsule (vrcam's timer).
void WatchTick(bool inWorld) {
    weight::FreeNote f;
    if (!inWorld || !weight::LastFree(&f)) return;
    if (f.release != g_mercRelease) {   // a fresh release: who is the mercenary now (a read of the unit hash once a release, not every sample)
        g_mercRelease = f.release;
        g_merc = meleetouch::HirelingView{};
        if (f.state != 0 && !f.thrown && !meleetouch::FindHireling(merctint::kHirelingClasses, merctint::kHirelingCount, &g_merc)) g_merc = meleetouch::HirelingView{};
    }
    Sample s;
    s.state = f.state;
    s.release = f.release;
    s.thrown = f.thrown;
    s.nowS = (double)GetTickCount64() / 1000.0;
    float mx = 0.0f, mz = 0.0f;
    stroke::Body body;
    if (f.state != 0 && g_merc.unit && meleetouch::HirelingPlace(g_merc.unit, g_merc.id, &mx, &mz) &&
        meleetouch::BodyFor(g_merc.row, mx, mz, f.floorY, f.upm, body)) {
        Capsule m;
        m.bottom[0] = body.bottom.x; m.bottom[1] = body.bottom.y; m.bottom[2] = body.bottom.z;
        m.top[0] = body.top.x; m.top[1] = body.top.y; m.top[2] = body.top.z;
        m.radius = body.radius;
        s.mercSeen = true;
        s.outside = ShaftOutside(m, f.butt, f.tip);
    }
    const bool armedWas = g_watch.armed;
    const Verdict v = g_watch.Step(s, g_reachM.load());
    if (g_trace.load() && (armedWas || g_watch.armed))
        LogF("merc give: watch - release %u, %s, mercenary %s, the shaft %.2f m outside him", f.release, f.state == 1 ? "in the air" : f.state == 2 ? "on the floor" : "held",
             s.mercSeen ? "seen" : "not seen", s.mercSeen ? (double)s.outside : 0.0);
    if (v == Verdict::Give) {
        const bool sent = Ask(f.slot);
        LogF("merc give: the %s hand's weapon was put into the mercenary (%s, %.2f m outside his body)%s", f.slot ? "left" : "right", g_watch.why, (double)s.outside,
             sent ? ": the game is asked to move it to him" : g_ok.load() == 0 ? " - NOT sent (the game's functions are not to be had)" : " - NOT sent (one is under way)");
    } else if (v == Verdict::Missed && g_merc.unit && g_trace.load()) {
        LogF("merc give: the let-go weapon was not given - %s", g_watch.why);
    }
}

}  // namespace

void Init(const PluginContext* ctx) {
    g_ctx = ctx;
    const ThreadService* t = nullptr;
    g_threads = ctx && ctx->QueryService(&t) == ServiceQueryResult::Success ? t : nullptr;
}

void Configure(bool on, float reachM, bool trace) {
    const bool was = g_on.exchange(on);
    g_reachM.store(std::isfinite(reachM) ? std::clamp(reachM, 0.0f, 2.0f) : 0.30f);
    g_trace.store(trace);
    if (was != on && g_ctx) LogF("merc give: [hands] merc_give_weapon %s", on ? "on - a weapon let go into the mercenary is given to him" : "off");
}

void Tick(bool inWorld) {
    if (!g_ctx) return;
    if (g_on.load() && g_ok.load() != 0) WatchTick(inWorld);
    if (!g_threads || !g_threads->runOnUiThread || g_ok.load() != 1 || !g_busy.load()) return;
    if (const uint32_t token = g_queued.Start(GetTickCount64()))
        if (g_threads->runOnUiThread(g_ctx, &Run, uiticket::ToUser(token)) != Threads::Result::Success) g_queued.Claim(token);
}

}  // namespace mercgive
