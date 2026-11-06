// hit_outcome_wire.cpp - [debug] hit_outcome: see hit_outcome.h for why. Read only: the hook tells what it sees and passes everything on.
//
// The damage function 0x44CE80 (SUNITDMG_ExecuteEvents in D2MOO; "ApplyDamage" in bearstonem/d2r-pcvr, MIT: vr/combat.cpp - the address, the
// arguments, the +0x04 result flags and the +0x134 life damage are theirs, for 3.3.93787): (game, attacker, defender, flag, damage record) - every
// blow that landed, Direct hit's too. Around the game's call the monster's life (stat 6) is read before and after; nothing is written.
// The roll (0x44B720) is vr/haptics.cpp's hook: it pushes its record here through hit_outcome.h.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>

#include "hit_outcome.h"
#include "hit_outcome_wire.h"
#include "sigscan.h"

namespace hitoutcome {
namespace {

// 0x44CE80 in 3.3.93787 (d2r-pcvr's ApplyDamageRva): push rbp; push rbx; push rsi; push rdi; push r14; push r15; lea rbp,[rsp-118h].
// Not in game_sigs.h yet (tools/make_sigs.py has its entry "DamageEvents"; the header is made from the game's image snapshot): until then it is
// looked for where 3.3.93787 has it only.
constexpr uint64_t RVA_DAMAGE = 0x44CE80;
const uint8_t kSigDamage[17] = {0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0xE8, 0xFE, 0xFF, 0xFF};
using DamageFn = uint64_t (*)(uintptr_t game, uintptr_t attacker, uintptr_t defender, int flag, uintptr_t damage);
// The unit's stat (unit, stat id, layer): a jump thunk through a slot the loader fills in (game_sigs.h "Stat"). Life is 6, in 1/256.
constexpr uint64_t RVA_STAT = 0x2F5020;
constexpr int kStatLife = 6;
// The damage record (d2r-pcvr): the result flags (a word) at +0x04, the life damage at +0x134.
constexpr uintptr_t kRecResultFlags = 0x04, kRecLife = 0x134;
constexpr int kLinesPerDrain = 40;   // twice a second: 80 lines a second at most (Whirlwind, Fury); the rest only counted

void (*g_log)(const char*) = nullptr;
DamageFn g_orig = nullptr;
std::atomic<bool> g_in{false};
std::atomic<uintptr_t> g_stat{0};
Tally g_tally;                        // (the timer's)
uint64_t g_tallyAt = 0;

void Log(const char* s) { if (g_log) g_log(s); }

// Plain data only: __try cannot sit in a function that unwinds C++ objects.
bool ReadHeader(uintptr_t unit, uint32_t* head3) noexcept {
    __try { memcpy(head3, reinterpret_cast<const void*>(unit), 3 * sizeof(uint32_t)); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
int Life(uintptr_t unit) noexcept {
    const uintptr_t fn = g_stat.load(std::memory_order_relaxed);
    if (!fn) return -1;
    __try {
        const int v = reinterpret_cast<int (*)(uintptr_t, int, int)>(fn)(unit, kStatLife, 0);
        return v >= 0 ? v : -1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}
bool ReadRecord(uintptr_t rec, uint32_t* hitFlags, uint16_t* result, int32_t* life) noexcept {
    if (rec < 0x10000) return false;
    __try {
        *hitFlags = *reinterpret_cast<const volatile uint32_t*>(rec);
        *result = *reinterpret_cast<const volatile uint16_t*>(rec + kRecResultFlags);
        *life = *reinterpret_cast<const volatile int32_t*>(rec + kRecLife);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
// The getter only once the loader has filled its slot (as vr/gamestate.cpp ReadHeroStrength checks it).
uintptr_t StatGetter() noexcept {
    const uintptr_t fn = d2rsig::Addr(RVA_STAT);
    if (!fn) return 0;
    __try {
        const uint8_t* thunk = reinterpret_cast<const uint8_t*>(fn);
        if (thunk[0] == 0xFF && thunk[1] == 0x25) {
            int32_t disp;
            memcpy(&disp, thunk + 2, sizeof disp);
            if (*reinterpret_cast<const volatile uintptr_t*>(fn + 6 + (intptr_t)disp) < 0x10000) return 0;
        }
        return fn;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// The hero's blow on a monster only (attacker a player, type 0; defender a monster, type 1): a missile's own unit as the attacker (arrows,
// throws) is left out in this first stage, as are a merc's and the monsters' blows.
uint64_t HookDamage(uintptr_t game, uintptr_t attacker, uintptr_t defender, int flag, uintptr_t damage) {
    uint32_t a[3], d[3];
    if (!Wanted() || attacker < 0x10000 || defender < 0x10000 || !ReadHeader(attacker, a) || a[0] != 0 || !ReadHeader(defender, d) || d[0] != 1)
        return g_orig(game, attacker, defender, flag, damage);
    Record r;
    r.monClass = d[1];
    r.monId = d[2];
    r.attacker = a[2];
    r.path = flag;
    r.lifeBefore = Life(defender);
    const uint64_t result = g_orig(game, attacker, defender, flag, damage);
    r.lifeAfter = Life(defender);
    uint32_t hitFlags = 0;
    uint16_t resultFlags = 0;
    int32_t life = -1;
    if (ReadRecord(damage, &hitFlags, &resultFlags, &life)) {
        r.hitFlags = hitFlags;
        r.flags = resultFlags;
        r.recordLife = life;
    }
    r.ms = GetTickCount64();
    PushDamage(r);
    return result;
}

}  // namespace

void SetLogger(void (*log)(const char*)) { g_log = log; }

void SetWanted(bool on) {
    if (WantedFlag().exchange(on) == on) return;
    Log(on ? "vrcam: hit outcome - [debug] hit_outcome=1: every roll of the hero's blow on a monster (the game's hit flags) and every blow that landed (the "
             "monster's life before and after, the crit bit) is told in the log; read only, nothing changes in play"
           : "vrcam: hit outcome - [debug] hit_outcome=0");
}

void Install() {
    static int tries = 0;
    static bool failed = false;
    if (g_in.load() || failed || !Wanted()) return;
    if (!d2rsig::Check(RVA_DAMAGE, kSigDamage, sizeof kSigDamage)) {
        if (d2rsig::NotInBuild(RVA_DAMAGE) || ++tries >= 120) {   // (an address not in game_sigs.h is only looked at where it was: a minute, then give up)
            failed = true;
            Log("vrcam: hit outcome - the game's damage function was not found (looked for at 0x44CE80, where 3.3.93787 has it): no damage lines, the rolls "
                "are still told (needs \"DamageEvents\" in cleanroom/sigscan/game_sigs.h - tools/make_sigs.py)");
        }
        return;
    }
    g_stat.store(StatGetter());
    const bool in = d2rsig::Hook(RVA_DAMAGE, kSigDamage, sizeof kSigDamage, (void*)&HookDamage, (void**)&g_orig);
    g_in.store(in);
    failed = !in;
    char b[256];
    snprintf(b, sizeof b, in ? "vrcam: hit outcome - damage observer in (read only: the game's damage function passes through untouched)%s"
                             : "vrcam: hit outcome - damage observer FAILED to install: no damage lines, the rolls are still told%s",
             g_stat.load() ? "" : "; the life stat getter is not ready: life not read");
    Log(b);
}

void Drain(uint64_t nowMs) {
    if (!g_tallyAt) g_tallyAt = nowMs;
    Record r;
    uint32_t lost = 0;
    int told = 0;
    while (Global().Pop(&r, &lost)) {
        g_tally.Add(r);
        if (told >= kLinesPerDrain) { ++g_tally.notTold; continue; }
        char line[400], b[440];
        Line(r, line, sizeof line);
        snprintf(b, sizeof b, "vrcam: hit outcome - %s", line);
        Log(b);
        ++told;
    }
    g_tally.lost += lost;
    if (nowMs - g_tallyAt < 10000) return;
    if (g_tally.Any()) {
        char line[400], b[440];
        TallyLine(g_tally, line, sizeof line);
        snprintf(b, sizeof b, "vrcam: hit outcome - last 10 s: %s", line);
        Log(b);
    }
    g_tally = Tally{};
    g_tallyAt = nowMs;
}

}  // namespace hitoutcome
