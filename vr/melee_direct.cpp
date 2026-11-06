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
#include "hit_outcome.h"   // [debug] hit_outcome: the rolls and the damage inside our call are marked as Direct hit's
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
constexpr uint64_t RVA_SERVER_WEAPON = 0x34B000, RVA_SKILL_LOOKUP = 0x33DCD0;   // [gameplay] melee_direct_by_hand: the weapon getter (its layout), SkillLookup (unit, id, owner)
constexpr uint64_t RVA_SLOT_SKILL = 0x117D20;   // SlotSkill(slot): the controller slot's skill (0 = A, the attack slot), the UI thread's read (as melee_direct_client.cpp)
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
std::atomic<bool> g_remote{false};   // [gameplay] melee_direct_client_cleave as the host: the joined players' extra hits
std::atomic<bool> g_quick{false};    // [gameplay] quick_blow: the reach of a quick attack's own blow (QuickReachBegin), the hooks go in for it
std::atomic<bool> g_byRules{false};  // [gameplay] melee_direct 2: by the rules (melee_direct_client.h)
std::atomic<bool> g_byHand{false};   // [gameplay] melee_direct_by_hand: the left weapon's blow as the game's "Left Hand Swing"
std::atomic<int> g_leftLayout{0};    // ... its layout proven from the game's weapon getter: 0 not tried, 1 proven, 2 not (the left swap stays off)
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
bool CallRangeRaw(RangeFn fn, uintptr_t a, uintptr_t d, int range, int* out, int* result) noexcept {
    __try { *result = fn(a, d, range, out); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
using SkillLookupFn = uintptr_t (*)(uintptr_t unit, int id, int owner);
bool CallSkillLookup(uintptr_t fn, uintptr_t unit, int id, uintptr_t* out) noexcept {
    __try { *out = reinterpret_cast<SkillLookupFn>(fn)(unit, id, -1); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool WritePtr(uintptr_t at, uintptr_t v) noexcept {
    if (at < 0x10000) return false;
    __try { *reinterpret_cast<uintptr_t*>(at) = v; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
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
    unsigned remoteAsked = 0, remoteCalled = 0, remoteFar = 0, remoteSoon = 0;
    unsigned leftSwings = 0, leftNot = 0;   // [gameplay] melee_direct_by_hand: left-hand blows struck as "Left Hand Swing" / not (the node not found)
} g_n;
// [gameplay] melee_direct_client_cleave: when each (hero, monster) was last struck for a joined player (the game's thread only)
struct RemoteSlot { uint32_t hero = 0, monster = 0; ULONGLONG at = 0; };
std::array<RemoteSlot, 32> g_remoteLast;
bool RemoteTooSoon(uint32_t hero, uint32_t monster, ULONGLONG now) {
    RemoteSlot* oldest = &g_remoteLast[0];
    for (RemoteSlot& s : g_remoteLast) {
        if (s.hero == hero && s.monster == monster) {
            if (now - s.at < kRemoteGapMs) return true;
            s.at = now;
            return false;
        }
        if (s.at < oldest->at) oldest = &s;
    }
    *oldest = {hero, monster, now};
    return false;
}   // the game's thread only (and the report on the timer's thread, racy and harmless: counts for a log line)
ULONGLONG g_reportAt = 0;

void Report(ULONGLONG now) {
    if (now - g_reportAt < 10000) return;
    g_reportAt = now;
    if (!g_n.asked && !g_n.called) return;
    LogF("melee direct: %u asked, %u called the game's attack, %u too old, refused: %u no hero, %u hero's mode, %u no monster, %u dying, %u not hostile, %u wall; "
         "joined players' extra hits: %u asked, %u struck, %u beyond the reach, %u too soon on the same monster; left-hand blows: %u as \"Left Hand Swing\", %u not",
         g_n.asked, g_n.called, g_n.stale, g_n.refused[static_cast<int>(Verdict::NoPlayer)], g_n.refused[static_cast<int>(Verdict::PlayerMode)],
         g_n.refused[static_cast<int>(Verdict::NoMonster)], g_n.refused[static_cast<int>(Verdict::MonsterDying)],
         g_n.refused[static_cast<int>(Verdict::NotHostile)], g_n.refused[static_cast<int>(Verdict::Wall)],
         g_n.remoteAsked, g_n.remoteCalled, g_n.remoteFar, g_n.remoteSoon, g_n.leftSwings, g_n.leftNot);
    g_n = {};
}

// [gameplay] melee_direct_by_hand: the layout the left swap writes by, proven once from the game's own weapon getter (LayoutProven). Its code
// is decrypted as it runs: not readable yet, tried again at the next left-hand blow; read and not as expected (another build), off for good.
bool LeftLayout() {
    if (g_leftLayout.load() != 0) return g_leftLayout.load() == 1;
    const uintptr_t fn = d2rsig::Addr(RVA_SERVER_WEAPON);
    uint8_t list[7] = {}, call[5] = {}, getter[13] = {}, weapsel[7] = {};
    if (!fn || !Read(list, fn + kAtSkillList, sizeof list) || !Read(call, fn + kAtGetterCall, sizeof call) || !Read(weapsel, fn + kAtWeapsel, sizeof weapsel)) return false;
    int32_t rel = 0;
    memcpy(&rel, call + 1, 4);
    const uintptr_t target = fn + kAtGetterCall + 5 + static_cast<intptr_t>(rel);
    if (call[0] != 0xE8 || !Read(getter, target, sizeof getter)) {
        if (call[0] == 0xE8) return false;   // (the getter not readable yet: tried again)
        g_leftLayout.store(2);
        Log("melee direct: by hand - the game's weapon getter is not as expected (another build?): every direct blow stays the right weapon's");
        return false;
    }
    const bool ok = LayoutProven(list, call[0], getter, weapsel);
    g_leftLayout.store(ok ? 1 : 2);
    Log(ok ? "melee direct: by hand - the hero's skill list and current skill are where the game's weapon getter reads them; a left-hand blow is the game's \"Left Hand Swing\""
           : "melee direct: by hand - the game's weapon getter is not as expected (another build?): every direct blow stays the right weapon's");
    return ok;
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
    if (r.remote) ++g_n.remoteAsked;
    if (v != Verdict::Go) {
        ++g_n.refused[static_cast<int>(v)];
        static unsigned told = 0;
        if (++told <= 12) LogF("melee direct: a %s on monster %u was not struck - %s", r.remote ? "joined player's extra hit" : "touch", r.monsterId, Name(v));
        return;
    }
    if (r.remote) {   // a joined player's extra hit: within the game's own reach (loosely) and not twice on one monster within kRemoteGapMs
        int out = 0;
        int inReach = 0;
        const bool rangeOk = g_origRange && CallRangeRaw(g_origRange, player, monster, kRemoteReach, &out, &inReach);
        if (!rangeOk || !inReach) {
            ++g_n.remoteFar;
            static unsigned toldFar = 0;
            if (++toldFar <= 12) LogF("melee direct: a joined player's extra hit on monster %u was not struck - beyond the reach on the server", r.monsterId);
            return;
        }
        if (RemoteTooSoon(r.heroId, r.monsterId, GetTickCount64())) { ++g_n.remoteSoon; return; }
    }
    // [gameplay] melee_direct_by_hand: the left-hand weapon's blow - for this one call the hero's current skill is his own "Left Hand Swing", the
    // skill the game swaps in for its own left swings (0x4FC0F0); its weapon getter then takes the left weapon. Put back right after the call.
    uintptr_t currentAt = 0, wasCurrent = 0;
    bool swapped = false;
    if (r.leftHand && !r.remote && g_byHand.load() && LeftLayout()) {
        uintptr_t list = 0, node = 0, row = 0;
        int16_t id = -1;
        uint8_t weapsel = 0;
        const uintptr_t lookupSkill = d2rsig::Addr(RVA_SKILL_LOOKUP);
        if (lookupSkill && Read(&list, player + kUnitSkillList, sizeof list) && list && Read(&wasCurrent, list + kListCurrentSkill, sizeof wasCurrent) &&
            CallSkillLookup(lookupSkill, player, kSkillLeftHandSwing, &node) && node && Read(&row, node, sizeof row) && row && Read(&id, row, sizeof id) &&
            Read(&weapsel, row + kRowWeapsel, sizeof weapsel) && IsLeftHandSwing(id, weapsel)) {
            currentAt = list + kListCurrentSkill;
            swapped = WritePtr(currentAt, node);
        }
        ++(swapped ? g_n.leftSwings : g_n.leftNot);
        static unsigned toldNot = 0;
        if (!swapped && ++toldNot <= 6) LogF("melee direct: a left-hand blow on monster %u - the hero's \"Left Hand Swing\" not found: struck with the right weapon", r.monsterId);
    }
    // The call: the touched monster is the target and the reach inside this one call only.
    t_win = {r.game, player, monster};
    hitoutcome::t_direct = true;
    const bool ok = CallAttack(attack, r.game, player);
    hitoutcome::t_direct = false;
    t_win = {};
    if (swapped && !WritePtr(currentAt, wasCurrent)) g_faulted.store(true);   // (the hero's own current skill back - never left swapped)
    if (!ok) {
        ++g_n.faults;
        g_faulted.store(true);
        Log("melee direct: the game's attack faulted - the option is OFF for this session (the touch goes on with the game's own key)");
        return;
    }
    ++g_n.called;
    if (r.remote) ++g_n.remoteCalled;
    static unsigned toldCall = 0;
    if (++toldCall <= 12 || (r.remote && toldCall <= 40))
        LogF(r.remote ? "melee direct: the game's attack called on monster %u for a joined player's hero %u (an extra hit of his swing)" : "melee direct: the game's attack called on monster %u",
             r.monsterId, r.heroId);
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
        if (g_faulted.load()) continue;
        if (q.r.remote ? !g_remote.load() : (!g_wanted.load() || !viewgate::F4())) continue;   // (our own touch: F4 only - a request found in another view is not struck)
        if (GetTickCount64() - q.at > kStaleMs) { ++g_n.stale; continue; }
        Strike(q.r);
    }
}

// ---- the attack slot's skill (Gate::slotPlain): the UI thread reads it, the timer's thread asks ---------------------------------------------

using SlotSkillFn = int (*)(int slot);
bool CallSlotSkill(int slot, int* out) noexcept {
    const uintptr_t fn = d2rsig::Addr(RVA_SLOT_SKILL);
    if (!fn) return false;
    __try { *out = reinterpret_cast<SlotSkillFn>(fn)(slot); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
uiticket::Ticket g_slotTicket;
std::atomic<int32_t> g_slotSkill{-1};     // the A slot's skill as last read (-1 none yet)
std::atomic<bool> g_slotRead{false};      // the last look could read it
std::atomic<uint64_t> g_slotAt{0};        // when (GetTickCount64), 0 = never
std::atomic<uint64_t> g_slotAskedAt{0};

void __cdecl LookSlot(const PluginContext*, void* user) noexcept {
    if (!g_slotTicket.Claim(uiticket::FromUser(user))) return;
    int skill = -1;
    const bool ok = CallSlotSkill(0, &skill);
    g_slotSkill.store(ok ? skill : -1);
    g_slotRead.store(ok);
    const uint64_t now = GetTickCount64();
    g_slotAt.store(now ? now : 1);
    static int told = -100;
    const int key = ok ? skill : -2;
    if (key != told) {
        told = key;
        if (!ok) Log("melee direct: the attack slot's skill could not be read - every touch is the game's own attack (key path)");
        else if (skill == kSkillAttack) Log("melee direct: the attack slot holds the plain attack - a touch may be a direct hit");
        else LogF("melee direct: the attack slot holds skill %d - a touch is the game's own attack, the game casts that skill (no direct hit)", skill);
    }
}
void AskSlot(uint64_t now) {
    if (!g_ctx || !g_threads || !g_threads->runOnUiThread) return;
    if (now - g_slotAskedAt.load() < kSlotLookMs) return;
    g_slotAskedAt.store(now);
    if (const uint32_t token = g_slotTicket.Start(now))
        if (g_threads->runOnUiThread(g_ctx, &LookSlot, uiticket::ToUser(token)) != Threads::Result::Success) g_slotTicket.Claim(token);
}

}  // namespace

bool SlotPlain(uint64_t nowMs) { return SlotIsPlain(g_slotRead.load(), g_slotSkill.load(), nowMs, g_slotAt.load()); }

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
void SetByHand(bool on) {
    if (g_byHand.exchange(on) != on)
        Log(on ? "melee direct: by hand - a blow of the left-hand weapon is the game's \"Left Hand Swing\" (the left weapon), the right hand's the plain attack"
               : "melee direct: every direct blow with the right weapon (the game's alternation is not repeated)");
}
bool ByHand() { return g_byHand.load(); }

void SetByRules(bool on) {
    if (g_byRules.exchange(on) != on) Log(on ? "melee direct: by the rules (the first touch, the attack time, speed + force + length)" : "melee direct: every accepted touch");
}
bool ByRules() { return g_byRules.load(); }
std::atomic<int> g_scope{-1};   // [gameplay] melee_direct_scope
void SetScope(int scope) {
    scope = scope < -1 ? -1 : scope > 1 ? 1 : scope;
    if (g_scope.exchange(scope) != scope)
        Log(scope == 0 ? "melee direct: [gameplay] melee_direct_scope=0 - by the rules, a two-handed swing's further monsters only (the first: the game's attack)"
            : scope == 1 ? "melee direct: [gameplay] melee_direct_scope=1 - by the rules, every touch (the first one too, quick blow or not)"
                         : "melee direct: [gameplay] melee_direct_scope missing - with quick blow the cleave's extras, without it every touch by the rules");
}
int Scope() { return g_scope.load(); }
void SetRemoteWanted(bool on) {
    if (g_remote.exchange(on) != on)
        Log(on ? "melee direct: the joined players' extra hits are struck on this PC when it hosts ([gameplay] melee_direct_client_cleave)"
               : "melee direct: the joined players' extra hits are not struck here");
}
void SetMinIntervalMs(uint32_t ms) { g_minMs.store(ms); }
void SetQuickWanted(bool on) {
    if (g_quick.exchange(on) != on)
        Log(on ? "melee direct: [gameplay] quick_blow - a quick attack's blow reaches the monster it is for (the reach test answered, the wall test the game's)"
               : "melee direct: [gameplay] quick_blow off - the game's reach for every blow of the key");
}

bool QuickReachBegin(uintptr_t game, uintptr_t attacker) {
    if (!g_quick.load(std::memory_order_relaxed) || g_state.load() != 1 || g_faulted.load() || !g_origTarget || t_win.active()) return false;
    uintptr_t target = 0;
    __try { target = g_origTarget(game, attacker); } __except (EXCEPTION_EXECUTE_HANDLER) { g_faulted.store(true); Log("melee direct: the server's target (quick blow) faulted - the option is off for this session"); return false; }
    uint32_t type = 0;
    if (!target || !Read(&type, target, sizeof type) || type != 1) return false;   // a monster only (a player, an object: the game's reach)
    t_win.game = game;
    t_win.attacker = attacker;
    t_win.defender = target;
    return true;
}
void QuickReachEnd() { t_win = Window{}; }
bool Ready() { return g_state.load() == 1 && !g_faulted.load() && g_threads && g_threads->runOnGameThread; }

void Install() {
    static ULONGLONG retryAt = 0;
    static unsigned tries = 0;
    Report(GetTickCount64());
    if (g_wanted.load()) AskSlot(GetTickCount64());   // the attack slot's skill, fresh for Plan (Gate::slotPlain)
    if ((!g_wanted.load() && !g_remote.load() && !g_quick.load()) || g_state.load() != 0 || !g_ctx || GetTickCount64() < retryAt) return;
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

Route Plan(uint64_t nowMs, bool serverKnown, bool armed) {
    Gate g;
    g.wanted = g_wanted.load();
    g.ready = serverKnown && Ready();
    g.armed = armed;
    g.slotPlain = SlotPlain(GetTickCount64());   // (the look's clock: GetTickCount64, like g_slotAt)
    g.nowMs = nowMs;
    g.lastMs = g_lastMs.load();
    g.minIntervalMs = g_byRules.load() ? 0 : g_minMs.load();   // (by the rules: the attack time per monster is the pace, not this)
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
