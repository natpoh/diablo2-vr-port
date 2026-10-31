// players_cmd.cpp - the game's players count from the headset: "/players N" into the game's own chat commands (vr/players_cmd.h).
//
// The headset's Options page asks (Ask, the draw thread); vrcam's timer queues a run on the UI thread (Tick), where the game handles a sent
// chat line, and the run hands "/players N" to the game's parser - as the chat box does. The parser checks the mode, clamps the number, writes
// the game's setting and the game says so. What the row shows is read back from the setting (Now): what the game took, or did not.

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
#include "players_cmd.h"
#include "players_cmd_core.h"
#include "sigscan.h"
#include "ui_ticket.h"

using namespace D2RL;

namespace playerscmd {
namespace {

using namespace core;

using ParseFn = void (*)(const char* text, const char* line, void* unused, int start);

const PluginContext* g_ctx = nullptr;
const ThreadService* g_threads = nullptr;
uiticket::Ticket g_queued;
std::atomic<int> g_wanted{0};             // the count asked for and not sent yet (0 none)
std::atomic<uintptr_t> g_parser{0};       // the game's chat command parser, once checked
std::atomic<uintptr_t> g_setting{0};      // the Offline Difficulty Scaling setting object
std::atomic<uintptr_t> g_mode{0};         // the game's mode (an int)
std::atomic<int> g_faulted{0};            // the parser faulted once: switched off for the session
std::atomic<ULONGLONG> g_lastLook{0};     // the last look for what is not found yet (at most twice a second)

void Log(const char* text) { if (g_ctx) logasync::Info(g_ctx, text); }

bool Peek(void* d, uintptr_t s, size_t n) noexcept { __try { memcpy(d, (const void*)s, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }

// The address an instruction at `at` (its first bytes `op`, a disp32 after them, `len` long) points to; 0 when the bytes are not that.
uintptr_t RipOf(uintptr_t at, const uint8_t* op, size_t opLen, uint32_t len) {
    uint8_t b[8] = {};
    int32_t disp = 0;
    if (!at || !Peek(b, at, opLen) || memcmp(b, op, opLen) != 0 || !Peek(&disp, at + opLen, 4)) return 0;
    return (uintptr_t)RipTarget(at, disp, len);
}

// The three pieces, each checked before it is used; looked for again (twice a second at most) while one is missing - a page of the
// game's code may still be encrypted, or this is another build (then it stays missing, the row shows "-").
void Find() {
    if (g_parser.load() && g_setting.load() && g_mode.load()) return;
    const ULONGLONG now = GetTickCount64();
    if (now - g_lastLook.load() < 500) return;
    g_lastLook.store(now);
    if (!g_setting.load() && d2rsig::Check(RVA_PLAYERS_SETTING, kSigPlayersSetting, sizeof kSigPlayersSetting))
        g_setting.store(RipOf(d2rsig::Addr(RVA_PLAYERS_SETTING) + kSettingLeaAt, kLeaRcx, sizeof kLeaRcx, 7));
    if (!g_mode.load() && d2rsig::Check(RVA_GAME_MODE, kSigGameMode, sizeof kSigGameMode)) {
        const uintptr_t at = d2rsig::Addr(RVA_GAME_MODE);
        uint8_t ret = 0;
        if (at && Peek(&ret, at + kModeRet, 1) && ret == 0xC3) g_mode.store(RipOf(at, kSigGameMode, sizeof kSigGameMode, kModeRet));
    }
    if (!g_parser.load() && d2rsig::Check(RVA_CHAT_COMMANDS, kSigChatCommands, sizeof kSigChatCommands)) {
        const uintptr_t at = d2rsig::Addr(RVA_CHAT_COMMANDS);
        const uintptr_t word = RipOf(at + kPlayersWordAt, kLeaRdx, sizeof kLeaRdx, 7);   // its /players branch compares the line with "/players"
        char w[sizeof kPlayersWord] = {};
        if (word && Peek(w, word, sizeof w) && memcmp(w, kPlayersWord, sizeof w) == 0) g_parser.store(at);
    }
    if (g_parser.load() && g_setting.load() && g_mode.load()) Log("vrcam: players - the game's /players command and its setting found (the headset's Options page)");
}

bool Mode(int* mode) { const uintptr_t a = g_mode.load(); return a && Peek(mode, a, sizeof *mode); }

int Count() {
    const uintptr_t s = g_setting.load();
    int n = 0;
    return s && Peek(&n, s + kSettingValue, sizeof n) ? n : 0;
}

bool Send(uintptr_t parser, const char* line) noexcept {
    __try {
        ((ParseFn)parser)(line, line, nullptr, 0);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// UI thread: the line, as the chat box hands it over.
void __cdecl Run(const PluginContext*, void* user) noexcept {
    if (!g_queued.Claim(uiticket::FromUser(user))) return;
    const int n = g_wanted.exchange(0);
    if (n <= 0 || g_faulted.load()) return;
    Find();
    const uintptr_t parser = g_parser.load();
    int mode = -1;
    char m[224];
    if (!parser || !Mode(&mode)) {
        Log("vrcam: players - the game's /players command is not found (yet): nothing sent");
        return;
    }
    if (!TakesPlayers(mode)) {   // the game would not take it here (a guest of a TCP/IP game, online): nothing is sent
        snprintf(m, sizeof m, "vrcam: players - not sent: the game takes /players only in single player or as a TCP/IP host (mode %d)", mode);
        Log(m);
        return;
    }
    char line[0x101] = {};
    snprintf(line, sizeof line, "%s", Line(n).c_str());
    const int was = Count();
    if (!Send(parser, line)) {
        g_faulted.store(1);
        snprintf(m, sizeof m, "vrcam: players - \"%s\" faulted in the game's chat commands: the headset's Players row is off for this session", line);
        Log(m);
        return;
    }
    snprintf(m, sizeof m, "vrcam: players - \"%s\" into the game's chat commands (mode %d): the game's count %d -> %d", line, mode, was, Count());
    Log(m);
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

void Ask(int players) { g_wanted.store(Clamp(players)); }

hset::Players Now() {
    if (g_faulted.load()) return {};
    Find();
    int mode = -1;
    const bool modeRead = Mode(&mode);
    const uintptr_t s = g_setting.load();
    int n = 0;
    const bool countRead = s && Peek(&n, s + kSettingValue, sizeof n);
    hset::Players p = core::Read(countRead, n, modeRead, mode);
    if (!g_parser.load()) p.takes = false;   // no command to send
    return p;
}

void Tick() {
    if (!g_ctx || !g_threads || !g_threads->runOnUiThread || !g_wanted.load()) return;
    if (const uint32_t token = g_queued.Start(GetTickCount64()))
        if (g_threads->runOnUiThread(g_ctx, &Run, uiticket::ToUser(token)) != Threads::Result::Success) g_queued.Claim(token);
}

}  // namespace playerscmd
