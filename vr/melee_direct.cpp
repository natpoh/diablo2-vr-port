// melee_direct.cpp - "Direct hit" (melee_direct.h): the game's side. EXPERIMENTAL, the user's own option for the A/B test of hit
// by touch against bearstonem/d2r-pcvr's direct mode; [gameplay] melee_direct, default off.
//
// Built on bearstonem/d2r-pcvr's vr/melee.cpp (MIT; Copyright (c) the d2r-pcvr authors - see LICENSE in that project): the call
// of the server's basic attack on the game's thread with a thread-local redirect of the server's target lookup at the attack's
// two call sites, and the reach test answered for that one pair. Written again here on our scanner's addresses and our own types.
//
// THIS CALLS THE GAME'S SERVER-SIDE ATTACK DIRECTLY - the one place in the plugin that does. The plugin's rule is the opposite
// (steer the game's own attacks, never bypass its commands); this option is the user's explicit, per-feature exception, for
// an offline comparison, off until ticked. What is skipped is listed in docs / side-session-notes/attack_path_recon.md.
// The game functions it calls: ServerAttack 0x42FEB0 (the one call that attacks), ServerLookup 0x48FE80, ServerRelation 0x48E460
// and ServerLine 0x3502A0 (read-only checks before it); ServerTarget 0x48FE20 and ServerRange 0x348650 are hooked and answer
// only inside the call.
//
// Safety: every call into the game is behind an SEH guard; a fault turns the option off for the session and the touch goes on
// with the game's own key. No local server found, a signature missing, a hook that did not go in: the same - the key path.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <intrin.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

#include <D2RLPlugin/api.h>

#include "log_async.h"   // [debug] log_async: the plugin's log lines through a ring and a writer thread (vr/log_queue.h); off = ctx->LogInfo as ever
#include "hit_latency.h"
#include "melee_direct.h"
#include "sigscan.h"
#include "ui_ticket.h"
#include "view_gate.h"
#include "frame_trace.h"   // [debug] frame_trace: what the direct hit's job costs a pair

using namespace D2RL;

namespace meleedirect {
namespace {

// Addresses in 3.3.93787 (the keys of game_sigs.h).
constexpr uint64_t RVA_ATTACK = 0x42FEB0, RVA_TARGET = 0x48FE20, RVA_RANGE = 0x348650;
constexpr uint64_t RVA_CALL1 = 0x42FFA5, RVA_CALL2 = 0x430105;   // the two calls of ServerTarget inside ServerAttack
constexpr uint64_t RVA_RELATION = 0x48E460, RVA_LINE = 0x3502A0;
// push rbp; push rsi; push r12; push r14; lea rbp,[rsp-118h]; sub rsp,218h
const uint8_t kSigAttack[22] = {0x40, 0x55, 0x56, 0x41, 0x54, 0x41, 0x56, 0x48, 0x8D, 0xAC, 0x24, 0xE8, 0xFE, 0xFF, 0xFF, 0x48, 0x81, 0xEC, 0x18, 0x02, 0x00, 0x00};
// push rbx; sub rsp,20h; mov rbx,rdx; call ...
const uint8_t kSigTarget[14] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xDA, 0xE8, 0x22, 0x1D, 0x00, 0x00};
// mov [rsp+8],rbx; mov [rsp+18h],rbp; mov [rsp+20h],rsi; push rdi
const uint8_t kSigRange[16] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57};

using AttackFn = int (*)(uintptr_t game, uintptr_t player, int a, int b);
using TargetFn = uintptr_t (*)(uintptr_t game, uintptr_t unit);
using RangeFn = int (*)(uintptr_t attacker, uintptr_t defender, int range, void* out);
using LookupFn = uintptr_t (*)(uintptr_t game, int type, uint32_t id);
using RelationFn = int (*)(uintptr_t game, uintptr_t player, uintptr_t unit);
using LineFn = int (*)(uintptr_t a, uintptr_t b, int flags);

const PluginContext* g_ctx = nullptr;
const ThreadService* g_threads = nullptr;
std::atomic<bool> g_wanted{false}, g_faulted{false};
std::atomic<uint32_t> g_minMs{0};
std::atomic<int> g_state{0};                 // 0 not tried, 1 hooks in, 2 not possible (another build?)
TargetFn g_origTarget = nullptr;
RangeFn g_origRange = nullptr;
std::atomic<uintptr_t> g_ret1{0}, g_ret2{0}; // where ServerAttack's two target calls return to

thread_local Window t_win;                   // the unit pair of a call of ours in flight on this thread

void Log(const char* text) {
    frametrace::Scope trace(frametrace::kLog);   // [debug] frame_trace: what the host's log costs the writer
    if (g_ctx) logasync::Info(g_ctx, text);
}
template <class... A> void LogF(const char* fmt, A... a) {
    char b[320];
    snprintf(b, sizeof b, fmt, a...);
    Log(b);
}

bool Read(void* d, uintptr_t s, size_t n) noexcept {
    if (s < 0x10000) return false;
    __try { memcpy(d, reinterpret_cast<const void*>(s), n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool WriteZeroInt(void* p) noexcept {
    if (!p) return true;
    __try { *static_cast<int*>(p) = 0; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// The game's functions, each behind its own guard: a fault is a "no" and turns the option off, never a crash.
bool CallLookup(uintptr_t fn, uintptr_t game, int type, uint32_t id, uintptr_t* out) noexcept {
    __try { *out = reinterpret_cast<LookupFn>(fn)(game, type, id); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallRelation(uintptr_t fn, uintptr_t game, uintptr_t player, uintptr_t unit, int* out) noexcept {
    __try { *out = reinterpret_cast<RelationFn>(fn)(game, player, unit); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallLine(uintptr_t fn, uintptr_t a, uintptr_t b, int* out) noexcept {
    __try { *out = reinterpret_cast<LineFn>(fn)(a, b, 0x804); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallAttack(uintptr_t fn, uintptr_t game, uintptr_t player) noexcept {
    __try { reinterpret_cast<AttackFn>(fn)(game, player, 0, 1); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// ---- the two hooks: they answer only inside a call of ours -------------------------------------------------------------------------

__declspec(noinline) uintptr_t HookTarget(uintptr_t game, uintptr_t unit) {
    if (t_win.game) {   // (cheap when no call of ours is in flight on this thread)
        const uintptr_t ret = reinterpret_cast<uintptr_t>(_ReturnAddress());
        if (AnswersTarget(t_win, game, unit, ret, g_ret1.load(std::memory_order_relaxed), g_ret2.load(std::memory_order_relaxed)))
            return t_win.defender;
    }
    return g_origTarget(game, unit);
}

int HookRange(uintptr_t attacker, uintptr_t defender, int range, void* out) {
    if (t_win.game && AnswersRange(t_win, attacker, defender)) {
        // The blade touched the monster: that is the reach. The game's own wall check stays (a hit through a wall is no hit).
        WriteZeroInt(out);
        int wall = 1;
        return CallLine(d2rsig::Addr(RVA_LINE), attacker, defender, &wall) && wall == 0 ? 1 : 0;
    }
    return g_origRange(attacker, defender, range, out);
}

// ---- the requests: the timer's thread queues, the game's thread strikes ---------------------------------------------------------------

constexpr size_t kQueue = 6;
constexpr ULONGLONG kStaleMs = 250;   // a touch not struck within this is not struck late (never a backlog)
struct Queued { Request r; ULONGLONG at = 0; };
std::mutex g_mutex;
std::array<Queued, kQueue> g_queue;
size_t g_count = 0;
uiticket::Ticket g_ticket;
std::atomic<uint64_t> g_lastMs{0};

struct Counters {
    unsigned asked = 0, called = 0, stale = 0;
    unsigned refused[8] = {};
    unsigned faults = 0;
} g_n;   // the game's thread only (and the report on the timer's thread, racy and harmless: counts for a log line)
ULONGLONG g_reportAt = 0;

void Report(ULONGLONG now) {
    if (now - g_reportAt < 10000) return;
    g_reportAt = now;
    if (!g_n.asked && !g_n.called) return;
    LogF("melee direct: %u asked, %u called the game's attack, %u too old, refused: %u no hero, %u hero's mode, %u no monster, %u dying, %u not hostile, %u wall",
         g_n.asked, g_n.called, g_n.stale, g_n.refused[static_cast<int>(Verdict::NoPlayer)], g_n.refused[static_cast<int>(Verdict::PlayerMode)],
         g_n.refused[static_cast<int>(Verdict::NoMonster)], g_n.refused[static_cast<int>(Verdict::MonsterDying)],
         g_n.refused[static_cast<int>(Verdict::NotHostile)], g_n.refused[static_cast<int>(Verdict::Wall)]);
    g_n = {};
}

void Strike(const Request& r) noexcept {
    ++g_n.asked;
    Found f;
    uintptr_t player = 0, monster = 0;
    uint32_t head[4] = {};
    const uintptr_t lookup = r.lookup, relation = d2rsig::Addr(RVA_RELATION), attack = d2rsig::Addr(RVA_ATTACK);
    if (!lookup || !relation || !attack || !r.game) return;
    // the hero, proven by its header (type 0, its id, the pointer back to the game at +0xD8)
    uintptr_t back = 0;
    if (CallLookup(lookup, r.game, 0, r.heroId, &player) && player && Read(head, player, sizeof head) && head[0] == 0 && head[2] == r.heroId &&
        Read(&back, player + 0xD8, sizeof back) && back == r.game) {
        f.player = true;
        Read(&f.playerMode, player + 0xC, 4);
    }
    if (f.player && CallLookup(lookup, r.game, 1, r.monsterId, &monster) && monster && Read(head, monster, sizeof head) && head[0] == 1 && head[2] == r.monsterId) {
        f.monster = true;
        f.monsterMode = head[3];
        int rel = 0, wall = 1;
        f.hostile = CallRelation(relation, r.game, player, monster, &rel) && rel != 0;
        f.wall = f.hostile && !(CallLine(d2rsig::Addr(RVA_LINE), player, monster, &wall) && wall == 0);
    }
    const Verdict v = Check(f);
    if (v != Verdict::Go) {
        ++g_n.refused[static_cast<int>(v)];
        static unsigned told = 0;
        if (++told <= 12) LogF("melee direct: a touch on monster %u was not struck - %s", r.monsterId, Name(v));
        return;
    }
    // The call: the touched monster is the target and the reach inside this one call only.
    t_win = {r.game, player, monster};
    const bool ok = CallAttack(attack, r.game, player);
    t_win = {};
    if (!ok) {
        ++g_n.faults;
        g_faulted.store(true);
        Log("melee direct: the game's attack faulted - the option is OFF for this session (the touch goes on with the game's own key)");
        return;
    }
    ++g_n.called;
    static unsigned toldCall = 0;
    if (++toldCall <= 12) LogF("melee direct: the game's attack called on monster %u", r.monsterId);
}

void __cdecl Apply(const PluginContext*, void* user) noexcept {
    frametrace::Scope trace(frametrace::kDirect);   // [debug] frame_trace: the job on the game's thread, the game's own attack it calls included
    if (!g_ticket.Claim(uiticket::FromUser(user))) return;
    for (;;) {
        Queued q;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (!g_count) return;
            q = g_queue[0];
            for (size_t i = 1; i < g_count; ++i) g_queue[i - 1] = g_queue[i];
            --g_count;
        }
        if (g_faulted.load() || !g_wanted.load() || !viewgate::F4()) continue;   // (F4 only: a request found in another view is not struck)
        if (GetTickCount64() - q.at > kStaleMs) { ++g_n.stale; continue; }
        Strike(q.r);
    }
}

}  // namespace

void Init(const PluginContext* ctx) {
    g_ctx = ctx;
    const ThreadService* t = nullptr;
    g_threads = ctx && ctx->QueryService(&t) == ServiceQueryResult::Success ? t : nullptr;
}

void SetWanted(bool on) {
    if (g_wanted.exchange(on) != on)
        Log(on ? "melee direct: on - EXPERIMENTAL: a touch calls the game's basic attack directly (offline only; skips the game's attack timing, reach and start checks)"
               : "melee direct: off");
}
bool Wanted() { return g_wanted.load(); }
void SetMinIntervalMs(uint32_t ms) { g_minMs.store(ms); }
bool Ready() { return g_state.load() == 1 && !g_faulted.load() && g_threads && g_threads->runOnGameThread; }

void Install() {
    static ULONGLONG retryAt = 0;
    static unsigned tries = 0;
    Report(GetTickCount64());
    if (!g_wanted.load() || g_state.load() != 0 || !g_ctx || GetTickCount64() < retryAt) return;
    retryAt = GetTickCount64() + (tries < 30 ? 2000 : 30000);   // the server's code is decrypted as it runs: tried again
    const uintptr_t attack = d2rsig::Addr(RVA_ATTACK), target = d2rsig::Addr(RVA_TARGET), range = d2rsig::Addr(RVA_RANGE);
    const uintptr_t call1 = d2rsig::Addr(RVA_CALL1), call2 = d2rsig::Addr(RVA_CALL2);
    if (!attack || !target || !range || !call1 || !call2 || !d2rsig::Addr(RVA_RELATION) || !d2rsig::Addr(RVA_LINE)) return;   // not all found (yet)
    if (!d2rsig::Check(RVA_ATTACK, kSigAttack, sizeof kSigAttack)) {
        if (++tries == 30) Log("melee direct: the game's attack is not where it was (another build?) - the key path stays");
        return;
    }
    // The two calls of the target lookup inside the attack: E8 rel32 to ServerTarget. Their return addresses are the only askers answered.
    for (const uintptr_t site : {call1, call2}) {
        uint8_t b[5];
        int32_t rel = 0;
        if (!Read(b, site, sizeof b) || b[0] != 0xE8) { g_state.store(2); Log("melee direct: the attack's target calls are not as expected (another build?) - the key path stays"); return; }
        memcpy(&rel, b + 1, 4);
        if (site + 5 + static_cast<intptr_t>(rel) != target) { g_state.store(2); Log("melee direct: the attack's target calls go elsewhere (another build?) - the key path stays"); return; }
    }
    g_ret1.store(call1 + 5);
    g_ret2.store(call2 + 5);
    const bool tIn = d2rsig::Hook(RVA_TARGET, kSigTarget, sizeof kSigTarget, reinterpret_cast<void*>(&HookTarget), reinterpret_cast<void**>(&g_origTarget));
    const bool rIn = tIn && d2rsig::Hook(RVA_RANGE, kSigRange, sizeof kSigRange, reinterpret_cast<void*>(&HookRange), reinterpret_cast<void**>(&g_origRange));
    if (tIn && rIn) {
        g_state.store(1);
        Log("melee direct: ready - the server's target and reach tests are hooked (they answer only inside the call), the game's attack is found");
    } else if (++tries == 30 || (tIn && !rIn)) {
        // (a target hook without the reach hook would leave the attack with the game's reach: the option stays off)
        g_state.store(2);
        Log("melee direct: the hooks could not be put in - the key path stays");
    }
}

Route Plan(uint64_t nowMs, bool serverKnown) {
    Gate g;
    g.wanted = g_wanted.load();
    g.ready = serverKnown && Ready();
    g.nowMs = nowMs;
    g.lastMs = g_lastMs.load();
    g.minIntervalMs = g_minMs.load();
    g.f4 = viewgate::F4();
    const Route r = Decide(g);
    if (r == Route::Direct) g_lastMs.store(nowMs ? nowMs : 1);
    Report(GetTickCount64());
    return r;
}

void Flush() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_count = 0;
}

bool Submit(const Request& r, uint64_t nowMs) {
    if (!g_threads || !g_threads->runOnGameThread || !g_ctx) return false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_count >= kQueue) return false;
        g_queue[g_count].r = r;
        g_queue[g_count].at = nowMs;
        ++g_count;
    }
    if (const uint32_t token = g_ticket.Start(nowMs)) {   // (a run already queued will take this one too)
        if (g_threads->runOnGameThread(g_ctx, &Apply, uiticket::ToUser(token)) != Threads::Result::Success) g_ticket.Claim(token);
    }
    return true;
}

}  // namespace meleedirect
