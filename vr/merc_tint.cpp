// merc_tint.cpp - [hud] merc_tint (vr/merc_tint.h) in the game: the merc found, its life read, the capsule handed to the effect.
//
//   - the merc: the one live monster of a hireling class in the client's unit hash (meleetouch::FindHireling, read-only, guarded);
//   - its life: the game's own stat getter on that client unit (stat 6 and 7, eight fractional bits), on the game's UI thread five
//     times a second, as the low-life vignette reads the hero's (vr/low_hp.cpp) - read only;
//   - its place: read again every frame the effect is fed (meleetouch::HirelingPlace), so the red moves with the merc.
// Nothing of the game is written, and nothing is called but the getter and the MonStats row the touch already asks for.

#include "log_async.h"   // [debug] log_async: the plugin's log lines through a ring and a writer thread (vr/log_queue.h); off = ctx->LogInfo as ever
#include "merc_tint_game.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

#include <D2RLPlugin/api.h>

#include "melee_touch.h"
#include "sigscan.h"
#include "ui_ticket.h"

using namespace D2RL;

namespace merctint {
namespace {

constexpr uint64_t kStat = 0x2F5020;   // the unit's stat getter (game_sigs.h "Stat", a jump thunk), as the low-life vignette

const PluginContext* g_ctx = nullptr;
const ThreadService* g_threads = nullptr;
std::atomic<bool> g_on{false};
std::atomic<float> g_threshold{0.5f}, g_strength{0.6f};
uiticket::Ticket g_queued;
std::atomic<bool> g_dead{false};   // a call into the game faulted: no read until the game restarts
std::atomic<int> g_toldFail{0};

// What the UI thread last found (under g_lock).
struct Seen {
    meleetouch::HirelingView merc{};
    Life life{};
    uint64_t at = 0;   // GetTickCount64 of the read; 0 = none
};
std::mutex g_lock;
Seen g_seen;

void Log(const char* text) { if (g_ctx) logasync::Info(g_ctx, text); }

enum class Read { Ok, NoAddress, Fault };

// Plain data only here: __try cannot sit in a function that unwinds C++ objects.
Read ReadLife(uintptr_t unit, int32_t* life, int32_t* maxLife) noexcept {
    const uintptr_t statFn = d2rsig::Addr(kStat);
    if (!statFn) return Read::NoAddress;
    __try {
        // the getter is a jump thunk through a slot the loader fills in: a slot still empty is not a getter
        const uint8_t* thunk = reinterpret_cast<const uint8_t*>(statFn);
        if (thunk[0] == 0xFF && thunk[1] == 0x25) {
            int32_t disp;
            memcpy(&disp, thunk + 2, sizeof disp);
            if (*reinterpret_cast<const volatile uintptr_t*>(statFn + 6 + (intptr_t)disp) < 0x10000) return Read::NoAddress;
        }
        if (*reinterpret_cast<const volatile uint32_t*>(unit) != 1) return Read::Fault;   // not a monster any more
        const auto stat = reinterpret_cast<int (*)(uintptr_t, int, int)>(statFn);
        *life = stat(unit, kStatLife, 0);
        *maxLife = stat(unit, kStatMaxLife, 0);
        return Read::Ok;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return Read::Fault;
    }
}

void __cdecl ReadJob(const PluginContext*, void* user) noexcept {
    if (!g_queued.Claim(uiticket::FromUser(user))) return;
    meleetouch::HirelingView merc;
    if (!meleetouch::FindHireling(kHirelingClasses, kHirelingCount, &merc)) {
        std::lock_guard<std::mutex> lock(g_lock);
        g_seen = Seen{};   // no merc here (none hired, dead, another area): nothing drawn
        return;
    }
    int32_t life = 0, maxLife = 0;
    const Read r = ReadLife(merc.unit, &life, &maxLife);
    if (r == Read::Fault) {
        { std::lock_guard<std::mutex> lock(g_lock); g_seen = Seen{}; }
        if (g_toldFail.fetch_add(1) < 2) Log("vrcam: merc tint - reading the merc's life faulted (it went away as it was read?); tried again");
        return;
    }
    if (r == Read::NoAddress) {
        if (g_toldFail.fetch_add(1) < 2) Log("vrcam: merc tint - the game's stat getter was not found (another build?): no tint");
        return;
    }
    const Life l = LifeFromStats(life, maxLife);
    {
        std::lock_guard<std::mutex> lock(g_lock);
        g_seen.merc = merc;
        g_seen.life = l;
        g_seen.at = GetTickCount64();
    }
    static uint32_t toldId = 0;
    if (merc.id != toldId) {
        toldId = merc.id;
        char b[220];
        if (l.known)
            snprintf(b, sizeof b, "vrcam: merc tint - the merc (class %u, id %u) is read: life %.1f of %.1f (%.0f%%)", merc.cls, merc.id,
                     life / kStatScale, maxLife / kStatScale, l.frac * 100.0f);
        else
            snprintf(b, sizeof b, "vrcam: merc tint - the merc (class %u, id %u) is found, but its life numbers make no sense here (%d, maximum %d): no tint",
                     merc.cls, merc.id, life, maxLife);
        Log(b);
    }
}

}  // namespace

void Init(const PluginContext* ctx) {
    g_ctx = ctx;
    if (ctx->QueryService(&g_threads) != ServiceQueryResult::Success) g_threads = nullptr;
}

void Configure(const Config& config) {
    const Config c = Sanitized(config);
    const bool was = g_on.exchange(c.on);
    g_threshold.store(c.threshold);
    g_strength.store(c.strength);
    if (c.on != was) {
        char b[200];
        snprintf(b, sizeof b, "vrcam: merc tint %s ([hud] merc_tint; below %.0f%% of its life, strength %.2f; VR F4 only)", c.on ? "on" : "off",
                 c.threshold * 100.0f, c.strength);
        Log(b);
        if (!c.on) { std::lock_guard<std::mutex> lock(g_lock); g_seen = Seen{}; }
    }
}

bool Enabled() { return g_on.load(std::memory_order_relaxed); }

void Tick(bool wantRead) {
    if (!wantRead || !g_on.load(std::memory_order_relaxed) || !g_ctx || !g_threads || !g_threads->runOnUiThread || g_dead.load()) return;
    static ULONGLONG next = 0;
    const ULONGLONG now = GetTickCount64();
    if (now < next) return;
    next = now + 200;
    if (const uint32_t token = g_queued.Start(now))
        if (g_threads->runOnUiThread(g_ctx, &ReadJob, uiticket::ToUser(token)) != Threads::Result::Success) g_queued.Claim(token);
}

bool Current(float upm, const float eyeWorld[3], float heroFootY, Uniforms* out) {
    if (!out || !g_on.load(std::memory_order_relaxed)) return false;
    Config c;
    c.on = true;
    c.threshold = g_threshold.load(std::memory_order_relaxed);
    c.strength = g_strength.load(std::memory_order_relaxed);
    Seen s;
    { std::lock_guard<std::mutex> lock(g_lock); s = g_seen; }
    if (!Show(c, s.life, Fresh(GetTickCount64(), s.at))) return false;
    float x = 0.0f, z = 0.0f;
    if (!meleetouch::HirelingPlace(s.merc.unit, s.merc.id, &x, &z)) return false;   // where it is this frame
    if (!(upm >= 1.0f && upm <= 12.0f)) return false;
    stroke::Body body;
    if (!meleetouch::BodyFor(s.merc.row, x, z, heroFootY / upm, upm, body)) return false;
    return Pack(body, upm, eyeWorld, c.strength, out);
}

}  // namespace merctint
