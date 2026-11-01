// party_cmd.cpp - the headset's Party page: the game's party roster and its Party panel's own buttons (vr/party_cmd.h).
//
// The page asks (Watch / Ask, the draw thread); vrcam's timer queues a run on the UI thread (Tick), where the game's panel handles its clicks: the
// run presses what was asked through the panel's own functions, then reads the roster again for the page.

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

#include "log_async.h"   // [debug] log_async: the plugin's log lines through a ring and a writer thread (vr/log_queue.h); off = ctx->LogInfo as ever
#include "party_cmd.h"
#include "party_cmd_core.h"
#include "sigscan.h"
#include "ui_ticket.h"

namespace gamecmd { void Tap(uint32_t cmd); }   // vr/gamecmd.cpp: one press of a game command, as its key's

using namespace D2RL;

namespace partycmd {
namespace {

using namespace core;
namespace hp = hset::party;

using EntryFn = void* (*)(uint32_t id);
using InviteFn = void (*)(void* entry);
using LeaveFn = void (*)();
using ModeFn = int (*)();
using IndexFn = uint32_t (*)();
using PlayerFn = void* (*)(uint32_t index);

constexpr uint32_t kCmdParty = 0x02;   // the game's "Party screen" key command (CfgParty: P)
constexpr uint64_t RVA_LOCAL_INDEX = 0x8B2D0, RVA_CLIENT_PLAYER = 0x9A480;   // game_sigs.h LocalIndex, ClientPlayer (the skill wheel's)

const PluginContext* g_ctx = nullptr;
const ThreadService* g_threads = nullptr;
uiticket::Ticket g_queued;
std::mutex g_lock;
hp::View g_view;                         // the last read (g_lock)
hp::Request g_ask;                       // a press not done yet (g_lock); what None: none
std::atomic<ULONGLONG> g_watchUntil{0};  // the page was up a moment ago: read on
std::atomic<ULONGLONG> g_lastRead{0};
std::atomic<int> g_faulted{0};
uintptr_t g_entry = 0, g_invite = 0, g_leave = 0, g_mode = 0, g_index = 0, g_player = 0, g_roster = 0;   // UI thread
ULONGLONG g_lastLook = 0;
bool g_toldFound = false;

void Log(const char* text) { if (g_ctx) logasync::Info(g_ctx, text); }

bool Peek(void* d, uintptr_t s, size_t n) noexcept { __try { memcpy(d, (const void*)s, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }

// UI thread: the pieces, each checked before it is used; looked for again twice a second while one is missing.
bool Found() {
    if (g_entry && g_invite && g_leave && g_mode && g_index && g_player && g_roster) return true;
    const ULONGLONG now = GetTickCount64();
    if (now - g_lastLook < 500) return false;
    g_lastLook = now;
    if (!g_entry && d2rsig::Check(RVA_PARTY_ENTRY, kSigPartyEntry, sizeof kSigPartyEntry)) {
        const uintptr_t at = d2rsig::Addr(RVA_PARTY_ENTRY);
        uint8_t tail[sizeof kSigPartyEntryTail] = {};
        int32_t disp = 0;
        if (at && Peek(tail, at + 7, sizeof tail) && !memcmp(tail, kSigPartyEntryTail, sizeof tail) && Peek(&disp, at + 3, 4)) {
            g_roster = (uintptr_t)RipTarget(at, disp, 7);
            g_entry = at;
        }
    }
    if (!g_invite && d2rsig::Check(RVA_PARTY_INVITE, kSigPartyInvite, sizeof kSigPartyInvite)) g_invite = d2rsig::Addr(RVA_PARTY_INVITE);
    if (!g_leave && d2rsig::Check(RVA_PARTY_LEAVE, kSigPartyLeave, sizeof kSigPartyLeave)) {
        const uintptr_t at = d2rsig::Addr(RVA_PARTY_LEAVE);
        uint8_t seven[sizeof kPartyLeaveSeven] = {};
        if (at && Peek(seven, at + kLeaveSevenAt, sizeof seven) && !memcmp(seven, kPartyLeaveSeven, sizeof seven)) g_leave = at;
    }
    if (!g_mode && d2rsig::Check(RVA_GAME_MODE, kSigGameMode, sizeof kSigGameMode)) g_mode = d2rsig::Addr(RVA_GAME_MODE);
    if (!g_index) g_index = d2rsig::Addr(RVA_LOCAL_INDEX);
    if (!g_player) g_player = d2rsig::Addr(RVA_CLIENT_PLAYER);
    const bool all = g_entry && g_invite && g_leave && g_mode && g_index && g_player && g_roster;
    if (all && !g_toldFound) { g_toldFound = true; Log("vrcam: party - the game's party roster and its Party panel's buttons found (the headset's Party page)"); }
    return all;
}

bool CallMode(int* mode) noexcept { __try { *mode = ((ModeFn)g_mode)(); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
bool CallLocalId(uint32_t* id) noexcept {
    __try {
        const uintptr_t unit = (uintptr_t)((PlayerFn)g_player)(((IndexFn)g_index)());
        if (!unit) return false;
        *id = *(const uint32_t*)(unit + 8);   // the unit's id (the game's own 0x34A330 reads it there)
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool CallEntry(uint32_t id, void** entry) noexcept { __try { *entry = ((EntryFn)g_entry)(id); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
bool CallInvite(void* entry) noexcept { __try { ((InviteFn)g_invite)(entry); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
bool CallLeave() noexcept { __try { ((LeaveFn)g_leave)(); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }

// UI thread: the roster as the page shows it; not known outside a multiplayer game.
hp::View Read() {
    hp::View v;
    int mode = 0;
    uint32_t local = 0;
    uintptr_t e = 0;
    if (!CallMode(&mode) || mode == 0 || !CallLocalId(&local) || !Peek(&e, g_roster, sizeof e)) return v;
    std::vector<Raw> roster;
    for (int n = 0; e && n < kMaxEntries; ++n) {
        Raw r;
        if (!Peek(r.b, e, sizeof r.b) || !Peek(&e, e + kNext, sizeof e)) return v;
        roster.push_back(r);
    }
    return Build(roster, local);
}

const char* ActName(hp::Act a) { return a == hp::Act::Invite ? "invite" : a == hp::Act::Accept ? "accept" : a == hp::Act::Cancel ? "cancel invite" : "?"; }

// UI thread: what was asked, as the panel's button would have done it.
void Press(const hp::Request& r) {
    char m[200];
    if (r.what == hp::Ask::OpenPanel) {
        gamecmd::Tap(kCmdParty);
        Log("vrcam: party - the game's Party key command (P): its Party panel");
        return;
    }
    if (!Found()) { Log("vrcam: party - the game's party code is not found (yet): nothing pressed"); return; }
    int mode = 0;
    if (!CallMode(&mode) || mode == 0) { Log("vrcam: party - not in a multiplayer game: nothing pressed"); return; }
    if (r.what == hp::Ask::Leave) {
        if (!CallLeave()) { g_faulted.store(1); Log("vrcam: party - the game's LeaveParty faulted: the headset's Party page is off for this session"); return; }
        Log("vrcam: party - the Party panel's Leave party, pressed");
        return;
    }
    void* entry = nullptr;
    uint32_t state = 0xFFFFFFFF;
    if (!CallEntry(r.id, &entry) || !entry || !Peek(&state, (uintptr_t)entry + kState, sizeof state)) {
        snprintf(m, sizeof m, "vrcam: party - player %u is no longer in the game's roster: nothing pressed", r.id);
        Log(m);
        return;
    }
    if (state != r.state || hp::ActFor(state) != r.act || r.act == hp::Act::None) {   // the game moved on since the page showed it: its button now says another thing
        snprintf(m, sizeof m, "vrcam: party - player %u's state is %u now, not %u as pressed: nothing pressed", r.id, state, r.state);
        Log(m);
        return;
    }
    if (!CallInvite(entry)) { g_faulted.store(1); Log("vrcam: party - the game's ProcessInviteButton faulted: the headset's Party page is off for this session"); return; }
    uint32_t now = state;
    Peek(&now, (uintptr_t)entry + kState, sizeof now);
    snprintf(m, sizeof m, "vrcam: party - the Party panel's button for player %u pressed (%s): state %u -> %u", r.id, ActName(r.act), state, now);
    Log(m);
}

void __cdecl Run(const PluginContext*, void* user) noexcept {
    if (!g_queued.Claim(uiticket::FromUser(user))) return;
    if (g_faulted.load()) return;
    hp::Request ask;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        ask = g_ask;
        g_ask = {};
    }
    if (ask.what != hp::Ask::None) Press(ask);
    const hp::View v = Found() ? Read() : hp::View{};
    g_lastRead.store(GetTickCount64());
    std::lock_guard<std::mutex> lock(g_lock);
    g_view = v;
}

template <class S> const S* Query(const PluginContext* ctx) {
    const S* s = nullptr;
    return ctx->QueryService(&s) == ServiceQueryResult::Success ? s : nullptr;
}

}  // namespace

void Init(const PluginContext* ctx) {
    g_ctx = ctx;
    g_threads = Query<ThreadService>(ctx);
}

void Watch() { g_watchUntil.store(GetTickCount64() + 1000); }

hp::View Now() {
    std::lock_guard<std::mutex> lock(g_lock);
    return g_view;
}

void Ask(const hp::Request& r) {
    std::lock_guard<std::mutex> lock(g_lock);
    g_ask = r;
}

void Tick() {
    if (!g_ctx || !g_threads || !g_threads->runOnUiThread || g_faulted.load()) return;
    const ULONGLONG now = GetTickCount64();
    bool asked;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        asked = g_ask.what != hp::Ask::None;
    }
    const bool readDue = now < g_watchUntil.load() && now - g_lastRead.load() >= 500;
    if (!asked && !readDue) return;
    if (const uint32_t token = g_queued.Start(now))
        if (g_threads->runOnUiThread(g_ctx, &Run, uiticket::ToUser(token)) != Threads::Result::Success) g_queued.Claim(token);
}

}  // namespace partycmd
