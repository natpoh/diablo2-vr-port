// melee_arm_game.cpp - [gameplay] melee_skill_armed, the game-facing half. What is armed, which press arms, and whether a hit is the skill or the plain attack are melee_arm.h's (pure, tested); this file
// reads from the game what they need and says what was decided:
//   - the skills on the pad's eight face-button slots (B X Y RB, with and without LT), from the game's own controller tables - the read [hands] double_swing_by_hands makes (double_swing_game.cpp
//     ReadPadSlots) - on the UI thread, five times a second, and the hero's total level in the armed skill (the skill list's lookup, ReadHeroLevel);
//   - the hero's mana (vr/hand_vitals_game.cpp), less what our last armed hit took if the read has not caught up (bowmana::Estimate);
//   - the pad the game polls (vrcam's XInput hook): a skill button's press that arms is taken out of it (FilterPad), so the game never casts it; a short buzz in both hands says it was taken;
//   - the touch's attack press (OnPress): the armed skill, or the plain attack when the hero cannot pay it, kept for the game's two reads of the attack slot while the press is young (PressSkill).
// No server function is called and nothing is aimed or attacked here: the cast is the game's own, of its own skill, by its own attack button.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>

#include <D2RLPlugin/api.h>

#include "bow_mana.h"
#include "direct_input.h"
#include "double_swing_game.h"   // ReadPadSlots, ReadHeroLevel: the pad's slots and the hero's levels, as double swing reads them
#include "hand_vitals_game.h"
#include "log_async.h"
#include "melee_arm.h"
#include "melee_arm_game.h"
#include "view_gate.h"

namespace gamestate { uint64_t LocalPlayer(); }

namespace meleearm {
namespace {

constexpr int kFaceSlots = 8;   // B X Y RB, LT + B X Y RB (shield_smite.h SlotPress 0..7)

const D2RL::PluginContext* g_ctx = nullptr;
std::atomic<bool> g_on{false};
std::atomic<int> g_hook{0};   // vrcam's slot-skill hook: 0 not yet, 1 in, 2 not possible
std::atomic<int> g_slot[kFaceSlots];       // the skill on each face-button slot (-1 none / not read)
std::atomic<int> g_owner[kFaceSlots];      // ... its owner in the controller record (for the level's lookup)
struct SlotInit { SlotInit() { for (int i = 0; i < kFaceSlots; ++i) { g_slot[i].store(-1); g_owner[i].store(-1); } } } g_slotInit;
std::atomic<int> g_armed{-1}, g_armedOwner{-1};   // the armed skill (-1 none), mirrored from the pad's thread for the others
std::atomic<int> g_level{-1}, g_levelFor{-1};     // the hero's total level in g_levelFor (-1 not read)
std::atomic<bool> g_clear{false};                 // a new hero, the setting off: the pad's thread forgets the armed skill at its next poll

SRWLOCK g_lock = SRWLOCK_INIT;   // the pad's thread's state, and the press in flight
Arm g_arm;
PadFilter g_filter;
InFlight g_flight;
// The timer's thread: our last armed hit (its mana is not in the read at once).
uint64_t g_lastCastAt = 0;
float g_lastCost = 0.0f;

void Log(const char* text) {
    if (g_ctx) logasync::Info(g_ctx, text);
}
void LogF(const char* fmt, ...) {
    char b[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    Log(b);
}

const char* SlotName(int slot) {
    static const char* const k[kFaceSlots] = {"B", "X", "Y", "RB", "LT + B", "LT + X", "LT + Y", "LT + RB"};
    return slot >= 0 && slot < kFaceSlots ? k[slot] : "?";
}

}  // namespace

void Init(const D2RL::PluginContext* ctx) { g_ctx = ctx; }

void Configure(bool on) {
    const bool was = g_on.exchange(on);
    if (on == was) return;
    g_clear.store(true);
    if (on) Log("melee armed: on - a melee attack skill's pad button (B X Y RB, with and without LT) arms the skill instead of casting it; every hit by touch is then that skill, the plain attack when the mana is short (F4)");
    else Log("melee armed: off ([gameplay] melee_skill_armed) - the skill buttons cast as ever");
}

bool On() { return g_on.load(); }

void SetHookState(int state) {
    if (g_hook.exchange(state) != state && state == 2 && g_on.load()) Log("melee armed: NOT possible - the game's slot-skill reads are not where they were (another build?); the skill buttons cast as ever");
}

void Tick() {
    if (g_on.load() && g_armed.load() >= 0) handvitals::game::Tick(true);   // the hero's mana, five times a second, on the UI thread
}

void UiRead() {
    if (!g_on.load()) return;
    static uint64_t hero = 0;
    if (const uint64_t now = gamestate::LocalPlayer(); now != hero) {
        if (hero && g_armed.load() >= 0) Log("melee armed: another game or hero - nothing armed");
        hero = now;
        g_clear.store(true);
    }
    static bool toldWhy = false;
    int skills[10], owners[10];
    const char* why = "";
    if (!doubleswing::ReadPadSlots(skills, owners, &why)) {
        for (int i = 0; i < kFaceSlots; ++i) g_slot[i].store(-1);
        if (!toldWhy) {
            toldWhy = true;
            LogF("melee armed: the pad slots cannot be read (%s) - the skill buttons cast as ever", why);
        }
        return;
    }
    toldWhy = false;
    for (int i = 0; i < kFaceSlots; ++i) {
        g_slot[i].store(skills[i]);
        g_owner[i].store(owners[i]);
    }
    const int armed = g_armed.load();
    if (armed < 0) return;
    const int level = doubleswing::ReadHeroLevel(armed, g_armedOwner.load());
    g_level.store(level);
    g_levelFor.store(armed);
    static int toldArmed = -2, toldLevel = -2;
    if (armed != toldArmed || level != toldLevel) {
        toldArmed = armed;
        toldLevel = level;
        if (level == 0) LogF("melee armed: the hero has no level in %s - every hit is the plain attack until it is learned or disarmed", Name(armed));
        else if (level > 0) LogF("melee armed: %s, the hero's level %d (%.2f mana a hit)", Name(armed), level, (float)bowmana::RowCostRaw(&Find(armed)->row, level) / 256.0f);
    }
}

void FilterPad(uint16_t* buttons, uint8_t lt, bool armable) {
    const bool live = armable && g_on.load() && g_hook.load() == 1 && viewgate::F4();
    AcquireSRWLockExclusive(&g_lock);
    if (g_clear.exchange(false)) {
        g_arm.Clear();
        g_flight.Clear();
        g_armed.store(-1);
    }
    const Step s = g_filter.Poll(*buttons, lt, live, g_arm, [](int slot) { return slot >= 0 && slot < kFaceSlots ? g_slot[slot].load() : -1; });
    if (s.event) {
        g_armed.store(g_arm.Armed());
        if (s.outcome == Outcome::Armed || s.outcome == Outcome::Switched) {
            g_armedOwner.store(s.slot >= 0 && s.slot < kFaceSlots ? g_owner[s.slot].load() : -1);
            g_level.store(-1);   // (read again by the UI thread within a fifth of a second)
            g_levelFor.store(-1);
        } else {
            g_flight.Clear();
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    *buttons = (uint16_t)(*buttons & ~s.strip);
    if (!s.event) return;
    const bool armedNow = s.outcome != Outcome::Disarmed;
    directinput::Haptic(0, armedNow ? 0.5f : 0.25f, armedNow ? 40.0f : 25.0f);   // a short buzz in both hands: the press was the arming's, not a cast
    directinput::Haptic(1, armedNow ? 0.5f : 0.25f, armedNow ? 40.0f : 25.0f);
    if (armedNow) LogF("melee armed: %s (pad %s) %s - every hit by touch is %s now, the plain attack when the mana is short; the same button again, or the plain attack's, disarms", Name(s.skill), SlotName(s.slot),
                       OutcomeName(s.outcome), Name(s.skill));
    else LogF("melee armed: disarmed (pad %s, %s) - every hit by touch is the plain attack", SlotName(s.slot), Name(s.skill));
}

void OnPress(bool melee) {
    if (!g_on.load()) return;
    const uint64_t now = GetTickCount64();
    const int armed = melee ? g_armed.load() : -1;
    float mana = -1.0f;
    handvitals::Vitals vit;
    double ageMs = 0.0;
    if (armed >= 0 && handvitals::game::Latest(&vit, &ageMs) && vit.known && ageMs < 3000.0)
        mana = bowmana::Estimate(vit.mana, ageMs, g_lastCastAt ? (double)(now - g_lastCastAt) : -1.0, g_lastCost);
    const int level = g_levelFor.load() == armed ? g_level.load() : -1;
    const Hit h = Decide(armed, level, mana);
    AcquireSRWLockExclusive(&g_lock);
    g_flight.Set(h.skill, now);
    ReleaseSRWLockExclusive(&g_lock);
    if (h.skill > kAttack) {
        g_lastCastAt = now;
        g_lastCost = h.cost > 0.0f ? h.cost : 0.0f;
    }
    if (armed < 0) return;
    static unsigned told = 0;
    static Why toldWhy = Why::NotArmed;
    if (++told <= 20 || h.why != toldWhy) {
        toldWhy = h.why;
        char m[60];
        if (mana >= 0.0f) snprintf(m, sizeof m, "the hero has %.2f", mana);
        else snprintf(m, sizeof m, "the mana not read");
        LogF("melee armed: a hit by touch - %s (%s, level %d%s, %.2f mana; %s)", WhyName(h.why), Name(armed), level, h.levelGuessed ? " - not read, the first level's cost stands in" : "", h.cost, m);
    }
}

int PressSkill() {
    if (!g_on.load()) return kAttack;
    AcquireSRWLockShared(&g_lock);
    const int skill = g_flight.Answer(GetTickCount64());
    ReleaseSRWLockShared(&g_lock);
    return skill;
}

}  // namespace meleearm
