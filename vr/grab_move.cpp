// grab_move.cpp - [weight] grab_moves_unit (vr/grab_move.h says what it is): the game's side. The knock-down library's timer (vr/knock_down.cpp) notes how
// many cells a carried or thrown body is drawn off its unit; a run on the game's thread asks the server for the knockback that takes the unit there - the
// server's own monster mode change, exactly as its damage code starts the knockback of a Knockback hit, with the cell given as the point.
//
// What it calls (D2R 3.3.93787, the keys of game_sigs.h): ServerLookup 0x48FE80 (the server's unit by id, as hit by touch does), MonModeEvent 0x4471E0 and
// MonModeSet 0x4475C0. Only with the server in this process (meleetouch::ServerLookup: single player, or the host); a joined player's game has none and
// nothing is asked there - the body stays drawn only, as before. Every call behind an SEH guard: a fault turns it off for the session.
// The log says, for the first ones: the unit's cell before, the cell asked, and the cell it stood on once the knockback ran - whether the game's own
// knockback lands a unit on a chosen cell, and how far short of it the game's collision or the mode's length left it.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include <D2RLPlugin/api.h>

#include "grab_move.h"
#include "grab_move_wire.h"
#include "log_async.h"   // [debug] log_async: the plugin's log lines through a ring and a writer thread (vr/log_queue.h)
#include "melee_touch.h"
#include "sigscan.h"
#include "ui_ticket.h"
#include "view_gate.h"

using namespace D2RL;

namespace grabmove {
namespace {

constexpr uint64_t RVA_MODE_EVENT = 0x4471E0, RVA_MODE_SET = 0x4475C0;
using ModeEventFn = void (*)(uintptr_t unit, int mode, void* event);
using ModeSetFn = int (*)(uintptr_t game, void* event, int force);
using LookupFn = uintptr_t (*)(uintptr_t game, int type, uint32_t id);
constexpr size_t kEventSize = 0x30;                 // the mode event (MonModeEvent clears it: mode +0, unit +8, target unit +0x10, point +0x18 / +0x1C ...)
constexpr size_t kEventX = 0x18, kEventY = 0x1C;
constexpr size_t kUnitPath = 0x38, kPathX = 0x2, kPathY = 0x6;   // a monster's path, its cell (words: 0x341A20 / 0x341A30)

const PluginContext* g_ctx = nullptr;
const ThreadService* g_threads = nullptr;
std::atomic<bool> g_wanted{false}, g_knockWanted{false}, g_leapWanted{false}, g_faulted{false};   // [weight] grab_moves_unit, knock_down_moves_unit, leap_landing_scatter
const char* SourceSaid(int source) { return source == kKnock ? " (knocked down by a blow)" : source == kLeap ? " (thrown by the leap's landing)" : ""; }
std::atomic<int> g_where{0};   // what was said of where the server is: 0 nothing, 1 here, 2 not here (a joined game)

void Log(const char* text) { if (g_ctx) logasync::Info(g_ctx, text); }
template <class... A> void LogF(const char* fmt, A... a) {
    char b[400];
    snprintf(b, sizeof b, fmt, a...);
    Log(b);
}

bool Read(void* d, uintptr_t s, size_t n) noexcept {
    if (s < 0x10000) return false;
    __try { memcpy(d, reinterpret_cast<const void*>(s), n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallLookup(uintptr_t fn, uintptr_t game, int type, uint32_t id, uintptr_t* out) noexcept {
    __try { *out = reinterpret_cast<LookupFn>(fn)(game, type, id); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallKnockback(uintptr_t eventFn, uintptr_t setFn, uintptr_t game, uintptr_t unit, int x, int y, int* result) noexcept {
    __try {
        alignas(16) uint8_t ev[kEventSize] = {};
        reinterpret_cast<ModeEventFn>(eventFn)(unit, kKnockbackMode, ev);
        memcpy(ev + kEventX, &x, 4);
        memcpy(ev + kEventY, &y, 4);
        *result = reinterpret_cast<ModeSetFn>(setFn)(game, ev, 1);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// The unit's cell on the server, from its path.
bool CellOf(uintptr_t unit, int* x, int* y) {
    uintptr_t path = 0;
    uint16_t px = 0, py = 0;
    if (!Read(&path, unit + kUnitPath, sizeof path) || !Read(&px, path + kPathX, 2) || !Read(&py, path + kPathY, 2) || (!px && !py)) return false;
    *x = px;
    *y = py;
    return true;
}

// ---- the asks: the knock-down timer notes, the game's thread acts --------------------------------------------------------------------------
struct Ask { uint32_t id = 0; float dx = 0.0f, dy = 0.0f; bool hostile = false; int source = kGrab; uint64_t atMs = 0; };
struct Last { uint32_t id = 0; uint64_t atMs = 0; };
// A knockback asked and how it went: checked once it has run (the unit's mode no longer 13, or kCheckMs on)
struct Watch { uint32_t id = 0; int fromX = 0, fromY = 0, toX = 0, toY = 0; uint64_t atMs = 0; bool hostile = false; int source = kGrab; };
constexpr size_t kAsks = 8, kWatches = 8;
constexpr uint64_t kStaleMs = 300, kCheckMs = 1200;
std::mutex g_mutex;
std::array<Ask, kAsks> g_asks;          // one per unit, the newest
std::array<Last, 16> g_last;            // when each unit was last asked for (the timer's thread)
std::array<Watch, kWatches> g_watch;    // (the game's thread only)
constexpr uint64_t kRefusedMs = 3000;
std::array<Last, 8> g_refused;          // (under g_mutex) units the game just refused: not asked again for kRefusedMs (an NPC it will not move walks home)
uiticket::Ticket g_ticket;
struct Counters { unsigned asked = 0, runs = 0, refused = 0, dead = 0, gone = 0, stale = 0, landed = 0, short_ = 0, rounded = 0, inWay = 0; } g_n;   // (the game's thread; read for the report)
unsigned g_told = 0, g_toldLand = 0;
uint64_t g_reportAt = 0;

void CheckWatches(uintptr_t game, uintptr_t lookup, uint64_t now) {
    for (Watch& w : g_watch) {
        if (!w.id) continue;
        uintptr_t unit = 0;
        uint32_t head[4] = {};
        if (!CallLookup(lookup, game, 1, w.id, &unit) || !unit || !Read(head, unit, sizeof head) || head[0] != 1 || head[2] != w.id) { w = Watch{}; continue; }
        if (static_cast<int>(head[3]) == kKnockbackMode && now - w.atMs < kCheckMs) continue;   // still running
        int x = 0, y = 0;
        if (!CellOf(unit, &x, &y)) { w = Watch{}; continue; }
        const int wantX = w.toX - w.fromX, wantY = w.toY - w.fromY, gotX = x - w.fromX, gotY = y - w.fromY;
        const int miss = std::max(std::abs(w.toX - x), std::abs(w.toY - y));
        if (miss <= 1) ++g_n.landed; else ++g_n.short_;
        if (++g_toldLand <= 24)
            LogF("grab moves unit: %s %u's knockback%s ran - it stands on cell %d,%d: moved %+d,%+d of the %+d,%+d asked (%s, after %.2f s, mode %u now)",
                 w.hostile ? "monster" : "NPC", w.id, SourceSaid(w.source), x, y, gotX, gotY, wantX, wantY, miss <= 1 ? "on the cell asked" : "short of it - a wall or a unit in its way (the game's collision), or the knockback's length",
                 (now - w.atMs) / 1000.0f, head[3]);
        w = Watch{};
    }
}

void Act(const Ask& a, uintptr_t game, uintptr_t lookup, uint64_t now) {
    ++g_n.asked;
    const uintptr_t eventFn = d2rsig::Addr(RVA_MODE_EVENT), setFn = d2rsig::Addr(RVA_MODE_SET);
    uintptr_t unit = 0;
    uint32_t head[4] = {};
    if (!eventFn || !setFn) return;
    if (!CallLookup(lookup, game, 1, a.id, &unit) || !unit || !Read(head, unit, sizeof head) || head[0] != 1 || head[2] != a.id) { ++g_n.gone; return; }
    if (!ModeAllows(static_cast<int>(head[3]))) return;   // dead or dying, or its last knockback still runs: the next note asks again
    int ux = 0, uy = 0, tx = 0, ty = 0;
    if (!CellOf(unit, &ux, &uy) || !Target(ux, uy, a.dx, a.dy, &tx, &ty)) return;
    // the hero in the way: the game's knockback stops at his body - the cell asked kept clear of him, a step across him taken round him (ClearOfHero)
    const int askedX = tx, askedY = ty;
    uintptr_t hero = 0;
    uint32_t heroHead[4] = {};
    int hx = 0, hy = 0;
    const uint32_t heroId = meleetouch::HeroId();
    const bool heroOk = heroId && CallLookup(lookup, game, 0, heroId, &hero) && hero && Read(heroHead, hero, sizeof heroHead) && heroHead[0] == 0 &&
                        heroHead[2] == heroId && CellOf(hero, &hx, &hy);
    if (heroOk && !ClearOfHero(static_cast<float>(hx), static_cast<float>(hy), ux, uy, &tx, &ty)) {
        ++g_n.inWay;
        return;
    }
    const bool rounded = tx != askedX || ty != askedY;
    if (rounded) ++g_n.rounded;
    int result = 0;
    if (!CallKnockback(eventFn, setFn, game, unit, tx, ty, &result)) {
        g_faulted.store(true);
        Log("grab moves unit: the game's knockback faulted - OFF for this session (carried, thrown and knocked-down bodies are drawn only, as before)");
        return;
    }
    uint32_t after[4] = {};
    Read(after, unit, sizeof after);
    const Outcome o = Judge(static_cast<int>(after[3]));
    if (o == Outcome::Runs) {
        ++g_n.runs;
        Watch* slot = &g_watch[0];
        for (Watch& w : g_watch) {
            if (w.id == a.id) { slot = &w; break; }
            if (!w.id || w.atMs < slot->atMs) slot = &w;
        }
        *slot = Watch{a.id, ux, uy, tx, ty, now, a.hostile, a.source};
    } else if (o == Outcome::Dead) {
        ++g_n.dead;
    } else {
        ++g_n.refused;
        std::lock_guard<std::mutex> lock(g_mutex);
        Last* slot = &g_refused[0];
        for (Last& r : g_refused) {
            if (r.id == a.id) { slot = &r; break; }
            if (r.atMs < slot->atMs) slot = &r;
        }
        *slot = Last{a.id, now};
    }
    if (++g_told <= 24 || (o == Outcome::Refused && g_told <= 40))
        LogF("grab moves unit: %s %u%s on cell %d,%d - the game's knockback asked to cell %d,%d (%+.1f,%+.1f cells drawn off%s; the hero on %d,%d): %s (returned %d, mode %u -> %u)",
             a.hostile ? "monster" : "NPC", a.id, SourceSaid(a.source), ux, uy, tx, ty, a.dx, a.dy, rounded ? ", kept clear of the hero" : "", hx, hy,
             o == Outcome::Runs ? "it runs" : o == Outcome::Dead ? "dead or dying - nothing" : "REFUSED by the game (its MonStats row: not knocked back - a boss, a big one)",
             result, head[3], after[3]);
}

void Report(uint64_t now) {
    if (now - g_reportAt < 10000) return;
    g_reportAt = now;
    if (!g_n.asked) return;
    LogF("grab moves unit (10 s): %u asked, %u knockbacks ran (%u landed on the cell asked, %u short; %u kept clear of the hero, %u not asked - only the hero's "
         "cells left), %u refused by the game, %u dead, %u gone, %u too old",
         g_n.asked, g_n.runs, g_n.landed, g_n.short_, g_n.rounded, g_n.inWay, g_n.refused, g_n.dead, g_n.gone, g_n.stale);
    g_n = {};
}

void __cdecl Apply(const PluginContext*, void* user) noexcept {
    if (!g_ticket.Claim(uiticket::FromUser(user))) return;
    uintptr_t game = 0, lookup = 0;
    if (!meleetouch::ServerLookup(&game, &lookup)) return;
    const uint64_t now = GetTickCount64();
    CheckWatches(game, lookup, now);
    for (;;) {
        Ask a;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            for (Ask& q : g_asks)
                if (q.id && (!a.id || q.atMs < a.atMs)) a = q;
            if (!a.id) break;
            for (Ask& q : g_asks)
                if (q.id == a.id) q = Ask{};
        }
        if (!WantedFor(a.source) || !viewgate::F4()) continue;
        if (now - a.atMs > kStaleMs) { ++g_n.stale; continue; }
        Act(a, game, lookup, now);
    }
    Report(now);
}

void Queue(uint64_t nowMs) {
    if (!g_threads || !g_threads->runOnGameThread || !g_ctx) return;
    if (const uint32_t token = g_ticket.Start(nowMs))
        if (g_threads->runOnGameThread(g_ctx, &Apply, uiticket::ToUser(token)) != Threads::Result::Success) g_ticket.Claim(token);
}

// Is the server in this process? Said once each way.
bool ServerHere() {
    uintptr_t game = 0, lookup = 0;
    const bool here = meleetouch::ServerLookup(&game, &lookup);
    if (g_where.exchange(here ? 1 : 2) != (here ? 1 : 2))
        Log(here ? "grab moves unit: the game's server runs here (single player, or this PC hosts) - a carried or thrown body is moved by the game's own knockback; "
                   "every player sees it"
                 : "grab moves unit: no game server here (a joined game, or none learned yet) - a carried or thrown body is drawn only, the game keeps it where it was");
    return here;
}

}  // namespace

void Init(const PluginContext* ctx) {
    g_ctx = ctx;
    const ThreadService* t = nullptr;
    g_threads = ctx && ctx->QueryService(&t) == ServiceQueryResult::Success ? t : nullptr;
}

void SetWanted(bool on) {
    if (g_wanted.exchange(on) == on) return;
    meleetouch::SetMoveWanted(on || g_knockWanted.load() || g_leapWanted.load());   // (the server's unit lookup is observed for it too: the server's game is learned from it)
    Log(on ? "grab moves unit: on ([weight] grab_moves_unit) - a body the hands carry or throw is put on the cell under it by the game's own knockback (single player "
             "or host; F4)"
           : "grab moves unit: off");
    if (!on && !g_knockWanted.load() && !g_leapWanted.load()) g_where.store(0);
}
bool Wanted() { return g_wanted.load() && !g_faulted.load(); }
void SetKnockWanted(bool on) {
    if (g_knockWanted.exchange(on) == on) return;
    meleetouch::SetMoveWanted(on || g_wanted.load() || g_leapWanted.load());
    Log(on ? "grab moves unit: knocked-down bodies on ([weight] knock_down_moves_unit) - a body a weapon's blow knocks down is put on the cell where it lies by the "
             "game's own knockback, so it fights and takes hits from there and gets up there (single player or host; F4)"
           : "grab moves unit: knocked-down bodies off - drawn only, the game keeps the unit on its cell");
    if (!on && !g_wanted.load() && !g_leapWanted.load()) g_where.store(0);
}
void SetLeapWanted(bool on) {
    if (g_leapWanted.exchange(on) == on) return;
    meleetouch::SetMoveWanted(on || g_wanted.load() || g_knockWanted.load());
    Log(on ? "grab moves unit: leap-thrown bodies on ([weight] leap_landing_scatter) - a body the hero's landing throws is put on the cell where it lies by the "
             "game's own knockback (single player or host; F4)"
           : "grab moves unit: leap-thrown bodies off");
    if (!on && !g_wanted.load() && !g_knockWanted.load()) g_where.store(0);
}
bool WantedFor(int source) {
    const bool on = source == kGrab ? g_wanted.load() : source == kKnock ? g_knockWanted.load() : source == kLeap && g_leapWanted.load();
    return on && !g_faulted.load();
}

bool Note(uint32_t id, float dx, float dy, bool hostile, int source, float minCells, uint64_t nowMs) {
    if (!WantedFor(source) || !viewgate::F4() || !d2rsig::Addr(RVA_MODE_EVENT) || !d2rsig::Addr(RVA_MODE_SET) || !ServerHere()) return false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (const Last& r : g_refused)
            if (r.id == id && nowMs >= r.atMs && nowMs - r.atMs < kRefusedMs) return false;   // (refused lately: not asked again yet)
        Last* last = &g_last[0];
        for (Last& l : g_last) {
            if (l.id == id) { last = &l; break; }
            if (l.atMs < last->atMs) last = &l;
        }
        if (!Wants(dx, dy, nowMs, last->id == id ? last->atMs : 0, minCells)) return false;
        *last = Last{id, nowMs};
        Ask* slot = &g_asks[0];
        for (Ask& q : g_asks) {
            if (q.id == id) { slot = &q; break; }
            if (!q.id || q.atMs < slot->atMs) slot = &q;
        }
        *slot = Ask{id, dx, dy, hostile, source, nowMs};
    }
    Queue(nowMs);
    return true;
}

void Tick(uint64_t nowMs) {
    if (!WantedFor(kGrab) && !WantedFor(kKnock) && !WantedFor(kLeap)) return;
    bool watching = false;
    for (const Watch& w : g_watch) watching = watching || w.id != 0;   // (read on the timer's thread, racy and harmless: only whether to queue a run)
    if (watching) Queue(nowMs);
}

}  // namespace grabmove
