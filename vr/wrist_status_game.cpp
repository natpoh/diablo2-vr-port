// wrist_status_game.cpp - [hud] wrist_status (vr/wrist_status.h) in the game: the hero's companions and the effects on it.
//
// What is read, and how (all of it read only, five times a second while the box is ticked, in F4, in a game):
//   - the hero's unit id from the CLIENT's own player (the same two lookups the hand vitals read its life by: LocalIndex, ClientPlayer), on the UI thread;
//   - the local SERVER's game (the one melee touch learns by observing the server's unit lookup, vr/melee_touch.cpp: SetStatusWanted puts that
//     observer in for this, SetStatusHero hands it the id) and from it the hero's server unit, by the game's own lookup - on the GAME's thread, the
//     thread that owns those lists (D2RLoader's runOnGameThread);
//   - the hero's pet lists (vr/wrist_status.h ReadRoster: unit -> player data -> pet root -> one list per pet type) and, for each state of the table
//     that vr/wrist_status.h has, the game's own state check on that unit (the function the game's tint of a hireling uses).
//   - the two tables' row counts (PetType, States) from the loader's data tables, as the bank the server game says it is in.
// Every layout this relies on has its byte guard: the code sites that read those offsets are compared with what 3.3.93787 has (d2rsig::Check, the loader's
// expected-byte check); a game where they differ gets "unknown", never a guess. A read that faults (guarded by __try) or does not add up is dropped, and a
// call into the game that faults switches the reads off until the game restarts.
//
// After bearstonem/d2r-pcvr (MIT, v0.3.18): vr/companions.cpp, vr/active_effects.cpp - the same layout, the same guards and the same state check,
// audited here against this build's image again (docs/wrist_status.md).

#include "log_async.h"   // [debug] log_async: the plugin's log lines through a ring and a writer thread (vr/log_queue.h); off = ctx->LogInfo as ever
#include "wrist_status_game.h"

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

namespace wriststatus {
namespace game {
namespace {

// Addresses in 3.3.93787: the index of the local player, the client's player by index (the same two as the hand vitals), the game's state check
// (stateCheck(unit, state) -> 1 / 0), and the unit lookup's own address is melee_touch's.
constexpr uint64_t kLocalIndex = 0x8B2D0, kClientPlayer = 0x9A480, kStateCheck = 0x3351B0;
// The guards: the bytes at the code sites that read the layout (player data, pet root, entry stride, list count / room, node) and the state check
// with its tail call.
const uint8_t kSigData[] = {0x48, 0x8B, 0x43, 0x10, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3};
const uint8_t kSigRoot[] = {0x4C, 0x8B, 0xBB, 0x98, 0x00, 0x00, 0x00};
const uint8_t kSigStride[] = {0x48, 0x8B, 0xC3, 0x48, 0xC1, 0xE0, 0x05, 0x48, 0x03, 0x45, 0x00};
const uint8_t kSigCount[] = {0x41, 0xFF, 0x4E, 0x08, 0x41, 0xFF, 0x4E, 0x0C};
const uint8_t kSigNode[] = {0x48, 0x8B, 0x58, 0x18, 0x40, 0x84, 0xF6, 0x75, 0x05, 0xF6, 0x00, 0x01, 0x75, 0x25, 0x44, 0x8B, 0x40, 0x04};
const uint8_t kSigState[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0xDA, 0x48, 0x8B, 0xF1, 0xE8, 0x07, 0x68, 0x01, 0x00};
const uint8_t kSigStateTail[] = {0x5F, 0xE9, 0x9A, 0x0F, 0xFC, 0xFF};

const PluginContext* g_ctx = nullptr;
const ThreadService* g_threads = nullptr;
const DataTableService* g_tables = nullptr;

std::atomic<bool> g_dead{false};          // a call into the game faulted: no read until the game restarts
std::atomic<uint32_t> g_heroId{0};        // the client's player unit's id, 0 none
std::atomic<unsigned> g_petRows[4] = {};      // the PetType table's row count per bank (1 classic, 2 LoD, 3 RotW), 0 not read
std::atomic<unsigned> g_stateRows[4] = {};    // the States table's
uiticket::Ticket g_uiTicket, g_gameTicket;
std::atomic<int> g_toldFail{0};

std::mutex g_m;   // the newest read
Roster g_roster;
Status g_status;
ULONGLONG g_at = 0;

void Log(const char* text) { if (g_ctx) logasync::Info(g_ctx, text); }

// ---- the reads and calls, each behind its own guard: a fault is a "no", never a crash of the game --------------------------------------------------

bool ReadMem(uintptr_t at, void* out, size_t n) noexcept {
    if (at < 0x10000 || at > UINTPTR_MAX - n) return false;
    __try {
        memcpy(out, reinterpret_cast<const void*>(at), n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

using LookupFn = uintptr_t (*)(uintptr_t game, int type, uint32_t id);
bool CallLookup(uintptr_t fn, uintptr_t game, int type, uint32_t id, uintptr_t* out) noexcept {
    __try {
        *out = reinterpret_cast<LookupFn>(fn)(game, type, id);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// 1 / 0 for "the unit is / is not in that state", -1 when the call faulted.
int CallStateCheck(uintptr_t fn, uintptr_t unit, int state) noexcept {
    __try {
        return reinterpret_cast<int (*)(uintptr_t, int)>(fn)(unit, state) ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// The client's player unit's id (the unit's first dword, its type, is 0 for a player; the id is the third dword).
enum class HeroRead { Ok, NoAddress, NoPlayer, Fault };
HeroRead ReadHeroId(uint32_t* id) noexcept {
    const uintptr_t indexFn = d2rsig::Addr(kLocalIndex), playerFn = d2rsig::Addr(kClientPlayer);
    if (!indexFn || !playerFn) return HeroRead::NoAddress;
    __try {
        const int index = reinterpret_cast<int (*)()>(indexFn)();
        if (index < 0 || index >= 8) return HeroRead::NoPlayer;
        const uintptr_t player = reinterpret_cast<uintptr_t (*)(int)>(playerFn)(index);
        if (player < 0x10000) return HeroRead::NoPlayer;
        uint32_t head[3];
        memcpy(head, reinterpret_cast<const void*>(player), sizeof head);
        if (head[0] != 0 || head[2] == 0) return HeroRead::NoPlayer;
        *id = head[2];
        return HeroRead::Ok;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return HeroRead::Fault;
    }
}

// ---- the tables' row counts --------------------------------------------------------------------------------------------------------------------------

// Asks the loader for the two tables of one bank. The tables answer on the thread the loader names for it - which one that is for this call is not
// assumed: it is tried from the UI thread (the hand vitals' tick) and from the game's thread (the read), whichever answers first keeps the counts.
bool PrepareBank(int bank) {
    if (bank < 1 || bank > 3 || !g_tables || !g_tables->getTable || !g_ctx) return false;
    if (g_petRows[bank].load() && g_stateRows[bank].load()) return true;
    DataTables::TableView pets{}, states{};
    pets.structSize = states.structSize = DataTables::TableViewSize;
    const auto b = static_cast<DataTables::Bank>(bank);
    if (g_tables->getTable(g_ctx, b, DataTables::TableId::PetType, &pets) != DataTables::Result::Success || !pets.rows || pets.rowCount < 20 || pets.rowCount > 64) return false;
    if (g_tables->getTable(g_ctx, b, DataTables::TableId::States, &states) != DataTables::Result::Success || !states.rows || states.rowSize != 0x44 || states.rowCount < 180 ||
        states.rowCount > kMaxStates)
        return false;
    g_petRows[bank].store(pets.rowCount);
    g_stateRows[bank].store(states.rowCount);
    char b2[200];
    snprintf(b2, sizeof b2, "vrcam: wrist status - the game's tables (bank %d): %u pet types, %u states", bank, pets.rowCount, states.rowCount);
    Log(b2);
    return true;
}

// ---- the guards: kept once they pass (the game's code is decrypted as it runs: a guard that fails early is asked again) ---------------------------------

bool RosterGuard() {
    static std::atomic<bool> ok{false};
    if (ok.load()) return true;
    const bool r = d2rsig::Check(0x34B27A, kSigData, sizeof kSigData) && d2rsig::Check(0x4FE9A8, kSigRoot, sizeof kSigRoot) && d2rsig::Check(0x50148E, kSigStride, sizeof kSigStride) &&
                   d2rsig::Check(0x5012FD, kSigCount, sizeof kSigCount) && d2rsig::Check(0x5014F6, kSigNode, sizeof kSigNode);
    if (r) ok.store(true);
    return r;
}
bool StateGuard() {
    static std::atomic<bool> ok{false};
    if (ok.load()) return true;
    const bool r = d2rsig::Check(kStateCheck, kSigState, sizeof kSigState) && d2rsig::Check(0x335280, kSigStateTail, sizeof kSigStateTail);
    if (r) ok.store(true);
    return r;
}

// ---- one read, on the game's thread -----------------------------------------------------------------------------------------------------------

struct Sample {
    Roster roster;
    Status status;
    bool ok = false;        // the server's player was found: the parts are what they are (known or not)
    const char* why = "";   // when not: what was missing
};

Sample Take(bool* faulted) {
    Sample s;
    *faulted = false;
    uintptr_t game = 0, lookup = 0;
    const uint32_t hero = g_heroId.load();
    if (!hero) { s.why = "no hero read from the client yet"; return s; }
    if (!meleetouch::ServerLookup(&game, &lookup)) { s.why = "the server's game is not learned yet"; return s; }
    uintptr_t player = 0;
    if (!CallLookup(lookup, game, 0, hero, &player)) { *faulted = true; s.why = "the server's unit lookup faulted"; return s; }
    uint32_t head[3] = {};
    uintptr_t back = 0;
    uint8_t bank = 0;
    if (!player || !ReadMem(player, head, sizeof head) || head[0] != 0 || head[2] != hero || !ReadMem(player + 0xD8, &back, sizeof back) || back != game || !ReadMem(game + 0x106, &bank, 1) ||
        bank < 1 || bank > 3) {
        s.why = "no server player for the hero";
        return s;
    }
    if (!g_petRows[bank].load() || !g_stateRows[bank].load()) PrepareBank(bank);
    s.ok = true;   // the hero is there; each part answers for itself
    const auto read = [](uintptr_t at, auto& value) { return ReadMem(at, &value, sizeof value); };
    if (RosterGuard() && g_petRows[bank].load()) s.roster = ReadRoster(player, g_petRows[bank].load(), read);
    const uintptr_t check = d2rsig::Addr(kStateCheck);
    if (StateGuard() && check && g_stateRows[bank].load()) {
        bool broke = false;
        s.status = Capture(g_stateRows[bank].load(), [&](int state) {
            const int r = CallStateCheck(check, player, state);
            if (r < 0) broke = true;
            return r;
        });
        if (broke) { *faulted = true; s.status = Status{}; }
    }
    return s;
}

void Store(const Roster& r, const Status& st) {
    std::lock_guard<std::mutex> lock(g_m);
    g_roster = r;
    g_status = st;
    g_at = GetTickCount64();
}
void Forget() {
    std::lock_guard<std::mutex> lock(g_m);
    g_roster = Roster{};
    g_status = Status{};
    g_at = 0;
}

void LogChange(const Roster& r, const Status& st) {
    static Roster lastR;
    static Status lastS;
    static ULONGLONG lastAt = 0;
    const ULONGLONG now = GetTickCount64();
    if (r == lastR && st == lastS) return;
    if (now - lastAt < 1000) return;   // a line costs the writer about a millisecond: not more than one a second
    lastR = r;
    lastS = st;
    lastAt = now;
    char line[420];
    int n = snprintf(line, sizeof line, "vrcam: wrist status - companions: %s", r.known ? "" : "unknown");
    for (const Companion& d : kCompanions)
        if (r.known && (r.relevant & Bit(d.type)) && n < (int)sizeof line - 24) n += snprintf(line + n, sizeof line - (size_t)n, " %s=%u", d.tag, (unsigned)r.alive[(size_t)d.type]);
    n += snprintf(line + n, sizeof line - (size_t)n, "; effects: %s", st.known ? "" : "unknown");
    for (const Effect& e : kEffects)
        if (st.Has(e.state) && n < (int)sizeof line - 12) n += snprintf(line + n, sizeof line - (size_t)n, " %d", e.state);
    Log(line);
}

void __cdecl GameJob(const PluginContext*, void* user) noexcept {
    if (!g_gameTicket.Claim(uiticket::FromUser(user))) return;
    bool faulted = false;
    const Sample s = Take(&faulted);
    if (faulted) {
        g_dead.store(true);
        Forget();
        Log("vrcam: wrist status - a call into the game faulted: the wrist status is off until the game restarts");
        return;
    }
    if (!s.ok) {
        Forget();
        if (g_toldFail.fetch_add(1) < 2) {
            char b[200];
            snprintf(b, sizeof b, "vrcam: wrist status - nothing to read yet: %s", s.why);
            Log(b);
        }
        return;
    }
    Store(s.roster, s.status);
    LogChange(s.roster, s.status);
    if ((!s.roster.known || !s.status.known) && g_toldFail.fetch_add(1) < 3) {
        char b[260];
        snprintf(b, sizeof b, "vrcam: wrist status - %s%s%s (the layout guards did not pass, or the tables were not read: another build?)", s.roster.known ? "" : "the companions are unknown",
                 !s.roster.known && !s.status.known ? " and " : "", s.status.known ? "" : "the effects are unknown");
        Log(b);
    }
}

// The UI thread's part: the tables (here or on the game's thread, whichever the loader answers), the client's hero id, and the game-thread read queued.
void __cdecl UiJob(const PluginContext*, void* user) noexcept {
    if (!g_uiTicket.Claim(uiticket::FromUser(user))) return;
    static ULONGLONG nextTables = 0;
    const ULONGLONG now = GetTickCount64();
    if (now >= nextTables) {
        nextTables = now + 5000;
        for (int bank = 1; bank <= 3; ++bank) PrepareBank(bank);
    }
    uint32_t id = 0;
    const HeroRead r = ReadHeroId(&id);
    if (r == HeroRead::Ok) {
        g_heroId.store(id);
        meleetouch::SetStatusHero(id);
    } else {
        g_heroId.store(0);
        Forget();
        if (r == HeroRead::Fault) {
            g_dead.store(true);
            Log("vrcam: wrist status - reading the client's hero faulted: the wrist status is off until the game restarts");
        } else if (g_toldFail.fetch_add(1) < 2) {
            Log(r == HeroRead::NoAddress ? "vrcam: wrist status - the game's player lookup was not found (another build?): no wrist status"
                                         : "vrcam: wrist status - no hero to read yet (a title or character screen, or a load)");
        }
        return;
    }
    if (g_threads && g_threads->runOnGameThread)
        if (const uint32_t token = g_gameTicket.Start(now))
            if (g_threads->runOnGameThread(g_ctx, &GameJob, uiticket::ToUser(token)) != Threads::Result::Success) g_gameTicket.Claim(token);
}

}  // namespace

void Init(const PluginContext* ctx) {
    g_ctx = ctx;
    if (!ctx) return;
    if (ctx->QueryService(&g_threads) != ServiceQueryResult::Success) g_threads = nullptr;
    if (ctx->QueryService(&g_tables) != ServiceQueryResult::Success) g_tables = nullptr;
    if (!g_threads || !g_threads->runOnUiThread || !g_threads->runOnGameThread) Log("vrcam: wrist status - no thread service: the companions and effects cannot be read");
    if (!g_tables || !g_tables->getTable) Log("vrcam: wrist status - no data tables service: the companions and effects cannot be read");
}

void Tick(bool wantRead) {
    meleetouch::SetStatusWanted(wantRead && !g_dead.load());   // the server's lookup is observed while it is wanted (once melee touch has not put it in already)
    static bool wasWanted = false;
    if (!wantRead) {
        if (wasWanted) { wasWanted = false; Forget(); g_heroId.store(0); }
        return;
    }
    wasWanted = true;
    if (!g_ctx || !g_threads || !g_threads->runOnUiThread || g_dead.load()) return;
    static ULONGLONG next = 0;
    const ULONGLONG now = GetTickCount64();
    if (now < next) return;
    next = now + 200;
    if (const uint32_t token = g_uiTicket.Start(now))
        if (g_threads->runOnUiThread(g_ctx, &UiJob, uiticket::ToUser(token)) != Threads::Result::Success) g_uiTicket.Claim(token);
}

bool Latest(Roster* roster, Status* status, double* ageMs) {
    std::lock_guard<std::mutex> lock(g_m);
    if (!g_at || g_dead.load()) return false;
    *roster = g_roster;
    *status = g_status;
    *ageMs = (double)(GetTickCount64() - g_at);
    return true;
}

}  // namespace game
}  // namespace wriststatus
