// gamecmd.cpp - the game's own key commands, pressed from BodyWalk.
//
// Every control in the game's Controls menu (C, I, Tab, F1..F16, 1..4, Shift,
// Alt, W...) is a command in one table, 0x22A7930: 0x45 entries of {down, up,
// flags}, 0x18 bytes each, named by the "Cfg..." strings (docs/buttons_recon.md,
// `python tools/move_recon.py cmds`). A key reaches it through the bindings
// (0x22AA490, 10 bytes: command, key code, type) and the dispatcher 0x11E9E0,
// which hands the matching binding to the executor: 0x11FF30(binding*, table,
// 1) on the press, 0x1203A0(...) on the release. The executor first offers the
// command to the UI (a panel may take it), then calls the entry's down() / up()
// and keeps its "held" byte (+0x12). We call the same executor with a binding
// of that command, so every gate of the game's stays the game's - and no pad
// and no key is involved, the UI stays whatever it is.
//
// What is pressed: BodyWalk's "D2R key: ..." actions (every named command,
// kD2RVRCommands in shared/d2r_vr_shared.h), held as Game Link's Actions say
// (vr/link.cpp: bit n = the game's command n). Tick() (vrcam's 1 ms timer)
// notices a change and queues Apply() on the UI thread, where the game handles
// its keys; Apply() releases what went up and presses what went down. BodyWalk
// gone = everything released.

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

#include "log_async.h"   // [debug] log_async: the plugin's log lines through a ring and a writer thread (vr/log_queue.h); off = ctx->LogInfo as ever
#include "d2r_vr_shared.h"
#include "link.h"
#include "sigscan.h"
#include "ui_ticket.h"

using namespace D2RL;

namespace gamecmd {
namespace {

// Addresses in 3.3.93787 (the keys of game_sigs.h).
constexpr uint64_t RVA_CMD_PRESS = 0x11FF30, RVA_CMD_RELEASE = 0x1203A0, RVA_CMD_TABLE = 0x22A7930;
constexpr uint64_t RVA_BINDINGS = 0x22AA490;   // no signature: read only when the table did not move (same build)
constexpr uint32_t kCommands = 0x45, kBindings = 0x8A, kBindingSize = 10, kEntrySize = 0x18;
// push rbp/rsi/r14/r15; lea rbp, [rsp-0x228] / push rbp/rsi/r12/r14/r15; lea rbp, [rsp-0x220] (no rip, no rel32)
const uint8_t kSigCmdPress[15] = {0x40,0x55,0x56,0x41,0x56,0x41,0x57,0x48,0x8D,0xAC,0x24,0xD8,0xFD,0xFF,0xFF};
const uint8_t kSigCmdRelease[17] = {0x40,0x55,0x56,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x8D,0xAC,0x24,0xE0,0xFD,0xFF,0xFF};

using ExecFn = bool (*)(const void* binding, void* table, bool force);

const PluginContext* g_ctx = nullptr;
const ThreadService* g_threads = nullptr;
uiticket::Ticket g_queued;   // Apply() queued (ui_ticket.h: a run that never comes is queued again)
// One-shot presses asked for by vrcam itself (Tap: the belt's potions drunk from the hand),
// apart from what BodyWalk holds: a bit per command, pressed and let go in one UI-thread run.
std::atomic<uint64_t> g_taps[2] = {};
uiticket::Ticket g_tapQueued;
// The bits Apply() has pressed (UI thread writes, the timer reads): bit n = the game's command n.
std::atomic<uint64_t> g_applied[2] = {0, 0};
std::atomic<int> g_ok{-1};            // -1 not found yet, 1 found, 0 faulted (switched off for the session)

// A binding per command, as the dispatcher hands one over: the game's own
// where this build is the one the table address is from, else ours with no
// key (only +0, the command, is read by the executor; the whole record goes to
// the UI with the command).
#pragma pack(push, 1)
struct Binding { uint32_t cmd; uint16_t key; uint32_t type; };
#pragma pack(pop)
static_assert(sizeof(Binding) == kBindingSize, "the game's binding record");
Binding g_binding[kCommands];

void Log(const char* text) { if (g_ctx) logasync::Info(g_ctx, text); }

template <class S> const S* Query(const PluginContext* ctx) {
    const S* s = nullptr;
    return ctx->QueryService(&s) == ServiceQueryResult::Success ? s : nullptr;
}

bool Read(void* d, const void* s, size_t n) noexcept { __try { memcpy(d, s, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }

// What BodyWalk holds now; nothing while BodyWalk is not there. One Presses per
// caller: a short press reads as held for each of them (link::kTapMs).
void Held(uint64_t out[2], link::Presses& presses) {
    link::CommandsHeld(out, presses);
    out[1] &= (1ull << (kCommands - 64)) - 1;   // commands 64..0x44
}

bool Same(const uint64_t a[2]) { return a[0] == g_applied[0].load() && a[1] == g_applied[1].load(); }
bool Bit(const uint64_t m[2], uint32_t n) { return (m[n / 64] >> (n % 64)) & 1; }

// The action's name for the log ("D2R key: Potion - belt 1").
const char* Name(uint32_t cmd) {
    for (const D2RVRCommand& c : kD2RVRCommands) if (c.cmd == cmd) return c.action;
    return "no name";
}

// On the UI thread before a press: both executors as expected, the table
// found, and the bindings to hand over. Not found is asked again at the next
// press, not given up: the game's code decrypts the first time it runs, and a
// player in VR may not have pressed a single key yet.
bool Ready() {
    if (g_ok.load() >= 0) return g_ok.load() == 1;
    const bool found = d2rsig::Check(RVA_CMD_PRESS, kSigCmdPress, sizeof kSigCmdPress) &&
                       d2rsig::Check(RVA_CMD_RELEASE, kSigCmdRelease, sizeof kSigCmdRelease) &&
                       d2rsig::Addr(RVA_CMD_PRESS) && d2rsig::Addr(RVA_CMD_RELEASE) && d2rsig::Addr(RVA_CMD_TABLE);
    if (!found) {
        static bool told = false;
        if (!told) {
            told = true;
            Log("vrcam: game commands not found (yet) - another build, or the game has not run its key code: "
                "press any key in the game once; BodyWalk's skill, potion and Alt actions do nothing until then");
        }
        return false;
    }
    for (uint32_t c = 0; c < kCommands; ++c) g_binding[c] = Binding{c, 0, 0};
    int own = 0;
    if (d2rsig::Rva(RVA_CMD_TABLE) == RVA_CMD_TABLE) {   // the table where 3.3.93787 has it: so are the bindings
        const uint8_t* b = (const uint8_t*)(d2rsig::Addr(RVA_CMD_TABLE) - RVA_CMD_TABLE + RVA_BINDINGS);
        bool seen[kCommands] = {};
        for (uint32_t i = 0; i < kBindings; ++i) {
            Binding r;
            if (!Read(&r, b + i * kBindingSize, sizeof r)) break;
            if (r.cmd < kCommands && !seen[r.cmd]) { seen[r.cmd] = true; g_binding[r.cmd] = r; ++own; }
        }
    }
    char m[160];
    snprintf(m, sizeof m, "vrcam: game commands found - %d of %u with the game's own key binding, the rest with an empty one",
             own, kCommands);
    Log(m);
    g_ok.store(1);
    return true;
}

bool Exec(uint64_t rva, uint32_t cmd, bool* handled) noexcept {
    __try {
        *handled = ((ExecFn)d2rsig::Addr(rva))(&g_binding[cmd], (void*)d2rsig::Addr(RVA_CMD_TABLE), true);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void Press(uint32_t cmd, bool down) {
    if (cmd >= kCommands) return;
    // An entry with no handler for this edge (no up() for most commands) is the
    // executor's business: it checks down() itself, and the release only clears
    // the held byte when there is no up().
    bool handled = false;
    if (!Exec(down ? RVA_CMD_PRESS : RVA_CMD_RELEASE, cmd, &handled)) {
        g_ok.store(0);
        char m[160];
        snprintf(m, sizeof m, "vrcam: game command 0x%02X (%s) faulted - game commands switched off", cmd, Name(cmd));
        Log(m);
        return;
    }
    char m[192];
    snprintf(m, sizeof m, "vrcam: game command 0x%02X %s (%s)%s", cmd, down ? "down" : "up", Name(cmd),
             handled ? "" : " - not taken (the UI or a game gate said no)");
    Log(m);
}

// UI thread.
void __cdecl Apply(const PluginContext*, void* user) noexcept {
    // first: a change from now on queues another run; a run come after its lease, replaced, does nothing
    if (!g_queued.Claim(uiticket::FromUser(user))) return;
    uint64_t want[2];
    static link::Presses presses;
    Held(want, presses);
    if (Same(want)) return;
    const uint64_t had[2] = {g_applied[0].load(), g_applied[1].load()};
    auto store = [&] { g_applied[0].store(want[0]); g_applied[1].store(want[1]); };
    if (!Ready()) { store(); return; }
    // Releases first: a switch from one command to another lets go before it presses.
    for (uint32_t n = 0; n < kCommands && g_ok.load() == 1; ++n)
        if (Bit(had, n) && !Bit(want, n)) Press(n, false);
    for (uint32_t n = 0; n < kCommands && g_ok.load() == 1; ++n)
        if (!Bit(had, n) && Bit(want, n)) Press(n, true);
    store();
}

// UI thread: the taps asked for, each pressed and let go at once (as a key would be).
void __cdecl ApplyTaps(const PluginContext*, void* user) noexcept {
    if (!g_tapQueued.Claim(uiticket::FromUser(user))) return;
    const uint64_t t[2] = {g_taps[0].exchange(0), g_taps[1].exchange(0)};
    if (!Ready()) return;
    const uint64_t held[2] = {g_applied[0].load(), g_applied[1].load()};
    for (uint32_t n = 0; n < kCommands && g_ok.load() == 1; ++n)
        if (Bit(t, n) && !Bit(held, n)) { Press(n, true); Press(n, false); }   // one BodyWalk holds already: left to it
}

}  // namespace

// One press of a game command, as a key's - from any thread; done on the UI thread soon after.
void Tap(uint32_t cmd) {
    if (cmd < kCommands) g_taps[cmd / 64].fetch_or(1ull << (cmd % 64));
}

// True once the game's executor has pressed this command (Apply has run and the command is down in the game); false while it
// is only asked for, not found, or faulted. vr/stand_still_wire.cpp waits for it before an attack goes out.
bool Pressed(uint32_t cmd) {
    if (cmd >= kCommands || g_ok.load() != 1) return false;
    const uint64_t a = g_applied[cmd / 64].load();
    return ((a >> (cmd % 64)) & 1) != 0;
}

// From D2RLoaderLoadPlugin.
void Init(const PluginContext* ctx) {
    g_ctx = ctx;
    g_threads = Query<ThreadService>(ctx);
    if (!g_threads || !g_threads->runOnUiThread) Log("vrcam: game commands - no UI thread service, BodyWalk's skill and potion actions do nothing");
}

// vrcam's 1 ms timer: a change in what BodyWalk holds goes to the UI thread.
void Tick() {
    if (!g_ctx || !g_threads || !g_threads->runOnUiThread || g_ok.load() == 0) return;
    if (g_taps[0].load() || g_taps[1].load()) {
        if (const uint32_t token = g_tapQueued.Start(GetTickCount64()))
            if (g_threads->runOnUiThread(g_ctx, &ApplyTaps, uiticket::ToUser(token)) != Threads::Result::Success) g_tapQueued.Claim(token);
    }
    uint64_t held[2];
    static link::Presses presses;
    Held(held, presses);
    if (Same(held)) return;
    bool lost = false;
    const uint32_t token = g_queued.Start(GetTickCount64(), &lost);
    if (!token) return;
    static int toldLost = 0;
    if (lost && toldLost < 10) { ++toldLost; Log("vrcam: game commands - the UI thread's run did not come in a second, queued again"); }
    if (g_threads->runOnUiThread(g_ctx, &Apply, uiticket::ToUser(token)) != Threads::Result::Success) g_queued.Claim(token);
}

}  // namespace gamecmd
