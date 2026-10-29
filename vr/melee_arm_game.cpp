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

#include "armed_icon.h"   // Ready: the icon's strength is the hit's own decision
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
std::atomic<bool> g_once{false};             // [gameplay] melee_skill_arm_once: the first blow spends the armed skill
std::atomic<uint64_t> g_onceMs{OnceTimeoutMs(kOnceTimeoutDefS)};   // ... and an unspent one disarms after this long (0 never)
std::atomic<bool> g_icon{true};   // [gameplay] melee_skill_armed_icon
std::atomic<float> g_iconR{armedicon::kIconR}, g_iconLift{0.0f}, g_iconOpacity{1.0f}, g_iconFaint{armedicon::kInactive};   // ... its size, depth, opacity with and without the mana (Placement)
std::atomic<uint64_t> g_allowed{Allowed::Defaults().Bits()};      // [melee_armed] s<id>: the list's ticks
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
// Our last armed hit (its mana is not in the read at once): written at the touch's press, read there and by the effect's thread for the icon ([gameplay] melee_skill_armed_icon).
std::atomic<uint64_t> g_lastCastAt{0};
std::atomic<float> g_lastCost{0.0f};

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

// The hero's mana in points now (negative: not read): the latest read, less what our last armed hit took if the read has not caught up.
float ManaNow(uint64_t now) {
    handvitals::Vitals vit;
    double ageMs = 0.0;
    if (!handvitals::game::Latest(&vit, &ageMs) || !vit.known || ageMs >= 3000.0) return -1.0f;
    const uint64_t at = g_lastCastAt.load();
    return bowmana::Estimate(vit.mana, ageMs, at ? (double)(now - at) : -1.0, g_lastCost.load());
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

void ConfigureOnce(bool once, float timeoutS) {
    const uint64_t ms = OnceTimeoutMs(timeoutS);
    const bool was = g_once.exchange(once);
    const uint64_t msWas = g_onceMs.exchange(ms);
    static bool told = false;
    if (told && was == once && msWas == ms) return;
    told = true;
    if (!once) Log("melee armed: an armed skill stays armed until its button is pressed again ([gameplay] melee_skill_arm_once=0)");
    else if (ms) LogF("melee armed: ONE blow - the first touch the game casts as the armed skill spends it, the next ones are the plain attack; unspent, it disarms after %.1f s ([gameplay] melee_skill_arm_once)",
                      (double)ms / 1000.0);
    else Log("melee armed: ONE blow - the first touch the game casts as the armed skill spends it, the next ones are the plain attack; no time-out ([gameplay] melee_skill_arm_once)");
}

void ConfigureList(uint64_t allowedBits) {
    const uint64_t was = g_allowed.exchange(allowedBits);
    static bool told = false;
    if (told && was == allowedBits) return;
    told = true;
    const Allowed a = Allowed::FromBits(allowedBits), def = Allowed::Defaults();
    char b[700];
    int n = snprintf(b, sizeof b, "melee armed: %d of the list's %d skills are armed by their button ([melee_armed]); not as by default:", a.Count(), kCount);
    bool any = false;
    for (int i = 0; i < kCount && n > 0 && n < (int)sizeof b - 40; ++i) {   // what differs from the defaults, named
        const int32_t id = kRows[i].row.id;
        if (a.Has(id) == def.Has(id)) continue;
        n += snprintf(b + n, sizeof b - n, "%s %s %s", any ? "," : "", kRows[i].name, a.Has(id) ? "ticked" : "unticked");
        any = true;
    }
    if (!any && n > 0 && n < (int)sizeof b - 8) snprintf(b + n, sizeof b - n, " none");
    Log(b);
}

void ConfigureIcon(bool on, float sizePct, float depthPct, float opacityPct, float faintPct) {
    const armedicon::Place p = armedicon::Placement(sizePct, depthPct, opacityPct, faintPct);
    const float rWas = g_iconR.exchange(p.radius), liftWas = g_iconLift.exchange(p.lift), opWas = g_iconOpacity.exchange(p.opacity), faintWas = g_iconFaint.exchange(p.faint);
    const bool was = g_icon.exchange(on);
    static bool told = false;
    if (told && (rWas != p.radius || liftWas != p.lift || opWas != p.opacity || faintWas != p.faint))
        LogF("melee armed: the icon in the mana medallion - %.0f%% of the inner circle, %s (radius %.3f, %.3f over the floor, of the base's radius), %.0f%% opaque with the mana, %.0f%% without",
             p.radius / armedicon::kDomeR * 100.0f, p.lift <= 0.0f ? "on the bottom" : "lifted in the liquid", p.radius, p.lift, p.opacity * 100.0f, p.faint * 100.0f);
    if (told && on == was) return;
    told = true;
    if (on) Log("melee armed: the armed skill's icon lies in the mana medallion on the wrist - full while the hero can pay the next hit, half see-through when he cannot ([gameplay] melee_skill_armed_icon; with melee_skill_armed on and the vitals' medallions, F4)");
    else Log("melee armed: no icon in the mana medallion ([gameplay] melee_skill_armed_icon=0)");
}

bool IconOn() { return g_icon.load(); }

armedicon::Place IconPlace() {
    armedicon::Place p;
    p.radius = g_iconR.load();
    p.lift = g_iconLift.load();
    p.opacity = g_iconOpacity.load();
    p.faint = g_iconFaint.load();
    return p;
}

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
    int32_t untick = g_arm.Armed();
    if (g_arm.SetAllowed(Allowed::FromBits(g_allowed.load()))) {   // [melee_armed]: the armed skill was unticked in the list
        g_flight.Clear();
        g_armed.store(-1);
    } else {
        untick = -1;
    }
    const uint64_t now = GetTickCount64();
    int32_t expired = -1;   // [gameplay] melee_skill_arm_once: armed too long without its blow
    if (g_once.load()) {
        const int32_t was = g_arm.Armed();
        if (g_arm.Expire(now, g_onceMs.load())) {
            expired = was;
            g_armed.store(-1);
        }
    }
    const Step s = g_filter.Poll(*buttons, lt, live, g_arm, [](int slot) { return slot >= 0 && slot < kFaceSlots ? g_slot[slot].load() : -1; }, now);
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
    if (untick >= 0) LogF("melee armed: %s unticked in the list ([melee_armed] s%d=0) - disarmed, every hit by touch is the plain attack", Name(untick), (int)untick);
    if (expired >= 0) {
        directinput::Haptic(0, 0.25f, 25.0f);   // the disarm's buzz: the skill is no longer waiting for a blow
        directinput::Haptic(1, 0.25f, 25.0f);
        LogF("melee armed: %s was not used within %.1f s - disarmed, the next hit by touch is the plain attack ([gameplay] melee_skill_arm_once_s)", Name(expired), (double)g_onceMs.load() / 1000.0);
    }
    if (!s.event) return;
    const bool armedNow = s.outcome != Outcome::Disarmed;
    directinput::Haptic(0, armedNow ? 0.5f : 0.25f, armedNow ? 40.0f : 25.0f);   // a short buzz in both hands: the press was the arming's, not a cast
    directinput::Haptic(1, armedNow ? 0.5f : 0.25f, armedNow ? 40.0f : 25.0f);
    if (armedNow && g_once.load())
        LogF("melee armed: %s (pad %s) %s for ONE blow - the next hit by touch is %s (the plain attack when the mana is short), the ones after it the plain attack; the same button again disarms",
             Name(s.skill), SlotName(s.slot), OutcomeName(s.outcome), Name(s.skill));
    else if (armedNow) LogF("melee armed: %s (pad %s) %s - every hit by touch is %s now, the plain attack when the mana is short; the same button again, or the plain attack's, disarms", Name(s.skill), SlotName(s.slot),
                       OutcomeName(s.outcome), Name(s.skill));
    else LogF("melee armed: disarmed (pad %s, %s) - every hit by touch is the plain attack", SlotName(s.slot), Name(s.skill));
}

void OnPress(bool melee) {
    if (!g_on.load()) return;
    const uint64_t now = GetTickCount64();
    const int armed = melee ? g_armed.load() : -1;
    const float mana = armed >= 0 ? ManaNow(now) : -1.0f;
    const int level = g_levelFor.load() == armed ? g_level.load() : -1;
    const Hit h = Decide(armed, level, mana);
    AcquireSRWLockExclusive(&g_lock);
    g_flight.Set(h.skill, now);
    ReleaseSRWLockExclusive(&g_lock);
    if (h.skill > kAttack) {
        g_lastCost.store(h.cost > 0.0f ? h.cost : 0.0f);
        g_lastCastAt.store(now);
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

int Armed(bool* ready) {
    *ready = false;
    if (!g_on.load()) return -1;
    const int armed = g_armed.load();
    if (armed < 0) return -1;
    const int level = g_levelFor.load() == armed ? g_level.load() : -1;
    *ready = armedicon::Ready(armed, level, ManaNow(GetTickCount64()));
    return armed;
}

int PressSkill() {
    if (!g_on.load()) return kAttack;
    AcquireSRWLockShared(&g_lock);
    const int skill = g_flight.Answer(GetTickCount64());
    ReleaseSRWLockShared(&g_lock);
    return skill;
}

void Used(int skill) {
    if (!g_on.load() || !g_once.load() || skill <= kAttack) return;
    AcquireSRWLockExclusive(&g_lock);
    const bool spent = g_arm.Spend(skill);   // (the flight stays: the game's second read of the same cast still gets the skill)
    if (spent) g_armed.store(-1);
    ReleaseSRWLockExclusive(&g_lock);
    static unsigned told = 0;
    if (spent && ++told <= 40) LogF("melee armed: %s cast by a hit by touch - spent (ONE blow), the next hits are the plain attack until its button is pressed again", Name(skill));
}

}  // namespace meleearm
