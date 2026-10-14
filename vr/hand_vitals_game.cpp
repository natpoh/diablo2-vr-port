// hand_vitals_game.cpp - the hero's life and mana, for the hand vitals panel ([hud] hand_vitals, vr/hand_vitals.h).
//
// Read on the game's UI thread five times a second through the game's own stat getter, the way the low-life vignette reads the life
// (vr/low_hp.cpp): unit stat 6 = life, 7 = its maximum, 8 = mana, 9 = its maximum, all kept with eight fractional bits. The getter and how
// the local player is found (LocalIndex, ClientPlayer) are bearstonem/d2r-pcvr's (MIT, vr/native_ui.cpp, which reads ids 6 - 9 the same
// way), found again here through our signatures (game_sigs.h, area "Low life" / "Hand vitals"; the skill wheel and the low-life
// vignette use the same three).
//
// Read only: nothing of the game is written or called other than the getter. A read that fails (no player yet, a title screen, another
// build) shows nothing and is told once; a call into the game that faults switches the read off until the game restarts.

#include "log_async.h"   // [debug] log_async: the plugin's log lines through a ring and a writer thread (vr/log_queue.h); off = ctx->LogInfo as ever
#include "hand_vitals_game.h"

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

#include "sigscan.h"
#include "ui_ticket.h"

using namespace D2RL;

namespace handvitals {
namespace game {
namespace {

// Addresses in 3.3.93787 (the keys of game_sigs.h): the index of the local player, the client's player unit by index, and the unit's stat
// getter (a jump thunk): stat(unit, id, layer) -> the value. The same three as vr/low_hp.cpp.
constexpr uint64_t kLocalIndex = 0x8B2D0, kClientPlayer = 0x9A480, kStat = 0x2F5020;
constexpr int kStatLife = 6, kStatMaxLife = 7, kStatMana = 8, kStatMaxMana = 9;

const PluginContext* g_ctx = nullptr;
const ThreadService* g_threads = nullptr;

std::mutex g_m;            // the newest read
Vitals g_vitals;
ULONGLONG g_at = 0;        // when (GetTickCount64); 0 none yet
uiticket::Ticket g_queued;
std::atomic<bool> g_dead{false};   // a call into the game faulted: no read until the game restarts
std::atomic<int> g_toldFail{0};

void Log(const char* text) { if (g_ctx) logasync::Info(g_ctx, text); }

enum class Read { Ok, NoAddress, NoPlayer, Fault };

struct Raw {
    int32_t life = 0, maxLife = 0, mana = 0, maxMana = 0;
};

// The hero's four stats, raw. The unit must be a player (its first dword, the unit type, is 0). Plain data only here: __try cannot sit
// in a function that unwinds C++ objects.
Read ReadStats(Raw* raw) noexcept {
    const uintptr_t indexFn = d2rsig::Addr(kLocalIndex), playerFn = d2rsig::Addr(kClientPlayer), statFn = d2rsig::Addr(kStat);
    if (!indexFn || !playerFn || !statFn) return Read::NoAddress;
    __try {
        const int index = reinterpret_cast<int (*)()>(indexFn)();
        if (index < 0 || index >= 8) return Read::NoPlayer;
        const uintptr_t player = reinterpret_cast<uintptr_t (*)(int)>(playerFn)(index);
        if (player < 0x10000) return Read::NoPlayer;
        if (*reinterpret_cast<const volatile uint32_t*>(player) != 0) return Read::NoPlayer;
        // the getter is a jump thunk through a slot the loader fills in: a slot still empty is not a getter
        const uint8_t* thunk = reinterpret_cast<const uint8_t*>(statFn);
        if (thunk[0] == 0xFF && thunk[1] == 0x25) {
            int32_t disp;
            memcpy(&disp, thunk + 2, sizeof disp);
            if (*reinterpret_cast<const volatile uintptr_t*>(statFn + 6 + (intptr_t)disp) < 0x10000) return Read::NoAddress;
        }
        const auto stat = reinterpret_cast<int (*)(uintptr_t, int, int)>(statFn);
        raw->life = stat(player, kStatLife, 0);
        raw->maxLife = stat(player, kStatMaxLife, 0);
        raw->mana = stat(player, kStatMana, 0);
        raw->maxMana = stat(player, kStatMaxMana, 0);
        return Read::Ok;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return Read::Fault;
    }
}

void Store(const Vitals& v) {
    std::lock_guard<std::mutex> lock(g_m);
    g_vitals = v;
    g_at = GetTickCount64();
}

void __cdecl ReadJob(const PluginContext*, void* user) noexcept {
    if (!g_queued.Claim(uiticket::FromUser(user))) return;
    Raw raw;
    const Read r = ReadStats(&raw);
    if (r == Read::Ok) {
        const Vitals v = FromStats(raw.life, raw.maxLife, raw.mana, raw.maxMana);
        Store(v);
        static bool told = false;
        if (v.known && !told) {
            told = true;
            char b[240];
            snprintf(b, sizeof b, "vrcam: hand vitals - the hero's life and mana are read: life %.2f of %.2f, mana %.2f of %.2f", v.life, v.maxLife, v.mana, v.maxMana);
            Log(b);
        } else if (!v.known && g_toldFail.fetch_add(1) < 2) {
            char b[240];
            snprintf(b, sizeof b, "vrcam: hand vitals - the life / mana numbers made no sense (life %d of %d, mana %d of %d): nothing is shown until they do", raw.life,
                     raw.maxLife, raw.mana, raw.maxMana);
            Log(b);
        }
        return;
    }
    Store(Vitals{});   // unknown: the panel goes out
    if (r == Read::Fault) {
        g_dead.store(true);
        Log("vrcam: hand vitals - reading the hero's life and mana faulted: the panel is off until the game restarts");
    } else if (g_toldFail.fetch_add(1) < 2) {
        Log(r == Read::NoAddress ? "vrcam: hand vitals - the game's stat getter or player lookup was not found (another build?): no panel"
                                 : "vrcam: hand vitals - no hero to read yet (a title or character screen, or a load): no panel until there is");
    }
}

}  // namespace

void Init(const PluginContext* ctx) {
    g_ctx = ctx;
    if (!ctx) return;
    if (ctx->QueryService(&g_threads) != ServiceQueryResult::Success) g_threads = nullptr;
    if (!g_threads || !g_threads->runOnUiThread) Log("vrcam: hand vitals - no UI thread service: the hero's life and mana cannot be read");
}

void Tick(bool wantRead) {
    if (!wantRead || !g_ctx || !g_threads || !g_threads->runOnUiThread || g_dead.load()) return;
    static ULONGLONG next = 0;
    const ULONGLONG now = GetTickCount64();
    if (now < next) return;
    next = now + 200;
    if (const uint32_t token = g_queued.Start(now))
        if (g_threads->runOnUiThread(g_ctx, &ReadJob, uiticket::ToUser(token)) != Threads::Result::Success) g_queued.Claim(token);
}

bool Latest(Vitals* out, double* ageMs) {
    std::lock_guard<std::mutex> lock(g_m);
    if (!g_at) return false;
    *out = g_vitals;
    *ageMs = (double)(GetTickCount64() - g_at);
    return !g_dead.load();
}

}  // namespace game
}  // namespace handvitals
