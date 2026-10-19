// double_swing_game.cpp - [hands] double_swing_by_hands, the game-facing half. The pairs, the window, the skill and the mana are double_swing.h's (pure, tested); this file reads from the game what they
// need and says what was decided:
//   - the pad's ten skill slots, from the game's own controller tables (the way the shield bash finds Smite: shield_smite.cpp UiRead), on the UI thread, five times a second: which slot holds Double Swing
//     (133) and Frenzy (147), and what the attack button (A) holds;
//   - the hero's level in each, from the game's own skill list (the lookup and the "has a positive level" check the skill wheel uses, wheel_game.cpp): total level, points and items;
//   - the hero's mana (vr/hand_vitals_game.cpp), less what our last press took if the read has not caught up (bowmana::Estimate).
// What it hands back is a pad press: the slot's button (shield_smite.h SlotPress). hit by touch (melee_touch.cpp) opens the window in which the game's target lookups answer with the touched monster and presses
// it - the game's own cast of its own skill at the monster, as the shield bash's Smite is. No server function is called, nothing is aimed or attacked here.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include <D2RLPlugin/api.h>

#include "double_swing.h"
#include "double_swing_game.h"
#include "hand_vitals_game.h"
#include "log_async.h"
#include "shield_smite.h"   // the pad slots' buttons (SlotPress), the hold (PulseMs), the order the slots are looked at (kSlotOrder)
#include "sigscan.h"

namespace doubleswing {
namespace {

// Addresses in 3.3.93787 (the keys of game_sigs.h): the pad's tables as the shield bash reads them, and the skill list as the wheel does.
constexpr uint64_t RVA_CTRL_BANK = 0x1195C0, RVA_BUTTON_SLOT = 0x1448C0, RVA_CTRL_TABLE = 0x2A48A90;
constexpr uint64_t RVA_LOCAL_INDEX = 0x8B2D0, RVA_CLIENT_PLAYER = 0x9A480, RVA_SKILL_LOOKUP = 0x33DCD0, RVA_SKILL_POSITIVE = 0x33A660;
constexpr uint32_t kRecordSize = 28;   // the controller table's record: skill (s32), owner (s32), ...

#pragma pack(push, 1)
struct Node {   // a skill node of the player's list (wheel_game.cpp: the layout of 3.3.93787)
    uintptr_t row, next;
    uint8_t pad[0x30];
    int32_t level, bonus, base, owner;
};
#pragma pack(pop)
static_assert(sizeof(Node) == 0x50, "the game's skill node");

const D2RL::PluginContext* g_ctx = nullptr;
std::atomic<bool> g_on{false};
std::atomic<float> g_window{(float)kDefaultWindowMs};
std::atomic<int> g_prefer{0};
// What the UI thread read (UiRead): the pad slot of each skill, its level, the skill on A.
std::atomic<int> g_slot[kSkillCount] = {{-1}, {-1}};
std::atomic<int> g_level[kSkillCount] = {{-1}, {-1}};
std::atomic<int> g_attack{-1};
// The timer's thread: the pairs, and our last press (its mana is not in the read at once).
Tracker g_tracker;
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

bool Rd(void* d, const void* s, size_t n) noexcept {
    __try {
        memcpy(d, s, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
template <class T>
bool Rd(uintptr_t a, T* v) {
    return a > 0x10000 && Rd(v, (const void*)a, sizeof *v);
}

// -- the game's functions, each behind its own guard: a fault is a "no", never a crash of the game --------------------------------
bool CallInt(uintptr_t fn, int* out) noexcept {
    __try {
        *out = ((int (*)())fn)();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool CallSlot(uintptr_t fn, int buttons, bool alternate, int* out) noexcept {
    __try {
        *out = ((int (*)(int, bool))fn)(buttons, alternate);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool CallPlayer(uintptr_t fn, int index, uintptr_t* out) noexcept {
    __try {
        *out = ((uintptr_t(*)(int))fn)(index);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool CallLookup(uintptr_t fn, uintptr_t player, int id, int owner, uintptr_t* out) noexcept {
    __try {
        *out = ((uintptr_t(*)(uintptr_t, int, int))fn)(player, id, owner);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool CallPositive(uintptr_t fn, uintptr_t player, uintptr_t node, bool* out) noexcept {
    __try {
        *out = ((bool (*)(uintptr_t, uintptr_t))fn)(player, node);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The hero's total level in a skill (points and items): -1 when it cannot be read, 0 when the hero does not have it, else the level.
int ReadLevel(int id, int owner) {
    const uintptr_t indexFn = d2rsig::Addr(RVA_LOCAL_INDEX), playerFn = d2rsig::Addr(RVA_CLIENT_PLAYER), lookupFn = d2rsig::Addr(RVA_SKILL_LOOKUP), positiveFn = d2rsig::Addr(RVA_SKILL_POSITIVE);
    if (!indexFn || !playerFn || !lookupFn || !positiveFn) return -1;
    int index = -1;
    uintptr_t player = 0;
    if (!CallInt(indexFn, &index) || index < 0 || index >= 8 || !CallPlayer(playerFn, index, &player) || !player) return -1;
    uint32_t head[2] = {};
    if (!Rd(player, &head) || head[0] != 0) return -1;   // unit type 0: a player
    uintptr_t node = 0;
    if (!CallLookup(lookupFn, player, id, owner, &node)) return -1;
    if (!node) return 0;
    bool positive = false;
    if (!CallPositive(positiveFn, player, node, &positive)) return -1;
    if (!positive) return 0;
    Node n;
    if (!Rd(node, &n)) return -1;
    return std::max(1, n.level + n.bonus);
}

const char* HandName(int hand) { return (hand & 1) ? "left" : "right"; }

// One line for a decision.
void Tell(int hand, uint32_t id, const Verdict& v, const Facts& f, double windowMs) {
    const char* me = HandName(hand);
    const char* other = HandName(hand ^ 1);
    switch (v.why) {
        case Why::Off: return;
        case Why::NotDual:
            return;   // (one weapon: the touch is as it always was; nothing to say for every blow)
        case Why::First:
            LogF("double swing: the %s hand struck monster %u - a normal attack; the %s hand has %.0f ms to strike it too", me, id, other, windowMs);
            return;
        case Why::OtherMonster:
            LogF("double swing: the %s hand struck monster %u - a normal attack (the %s hand's last strike was on another monster); the %s hand has %.0f ms to strike this one too", me, id, other, other, windowMs);
            return;
        case Why::TooLate:
            LogF("double swing: the %s hand struck monster %u %.0f ms after the %s hand - a normal attack (the window is %.0f ms); the %s hand has %.0f ms to strike it too", me, id, v.gapMs, other, windowMs, other, windowMs);
            return;
        case Why::Dual: {
            char mana[80];
            if (v.mana >= 0.0f) snprintf(mana, sizeof mana, "the hero has %.2f", v.mana);
            else snprintf(mana, sizeof mana, "the mana is not read - the game decides");
            LogF("double swing: the %s hand struck monster %u %.0f ms after the %s hand - %s pressed (pad slot %d, level %d%s; it costs %.2f mana, %s)", me, id, v.gapMs, other, SkillName(v.skill), v.slot,
                 v.level, v.levelGuessed ? " - not read, the first level's cost stands in" : "", v.cost, mana);
            return;
        }
        case Why::AlreadyOnAttack:
            LogF("double swing: the %s hand struck monster %u %.0f ms after the %s hand - %s is on the attack button already, so every touch casts it: nothing added", me, id, v.gapMs, other, SkillName(v.skill));
            return;
        case Why::NoSlot:
            LogF("double swing: the %s hand struck monster %u %.0f ms after the %s hand - a normal attack: neither Double Swing nor Frenzy is on one of the pad's ten skill slots", me, id, v.gapMs, other);
            return;
        case Why::NotLearned:
            LogF("double swing: the %s hand struck monster %u %.0f ms after the %s hand - a normal attack: the hero has no level in the skill on the slot", me, id, v.gapMs, other);
            return;
        case Why::NoMana:
            LogF("double swing: the %s hand struck monster %u %.0f ms after the %s hand - a normal attack: %s (level %d%s) costs %.2f mana and the hero has %.2f", me, id, v.gapMs, other, SkillName(v.skill),
                 v.level, v.levelGuessed ? ", not read" : "", v.cost, f.mana);
            return;
    }
}

Facts Gather(uint64_t nowMs) {
    Facts f;
    for (int k = 0; k < kSkillCount; ++k) f.skill[k] = {g_slot[k].load(), g_level[k].load()};
    f.attackSkill = g_attack.load();
    f.prefer = g_prefer.load();
    handvitals::Vitals vit;
    double ageMs = 0.0;
    if (handvitals::game::Latest(&vit, &ageMs) && vit.known && ageMs < 3000.0)
        f.mana = bowmana::Estimate(vit.mana, ageMs, g_lastCastAt ? (double)(nowMs - g_lastCastAt) : -1.0, g_lastCost);
    return f;
}

}  // namespace

void Init(const D2RL::PluginContext* ctx) { g_ctx = ctx; }

void Configure(bool on, float windowMs, int prefer) {
    const bool was = g_on.exchange(on);
    const float w = (float)ClampWindowMs(windowMs);
    const int p = prefer == 1 ? 1 : 0;
    const bool changed = on != was || (on && (w != g_window.load() || p != g_prefer.load()));
    g_window.store(w);
    g_prefer.store(p);
    if (changed) {
        if (on) LogF("double swing: on - with a weapon in each hand, the second hand's touch on the monster the first hand struck within %.0f ms is %s (it must sit on a pad slot, with a level and the mana)", w,
                     p == 1 ? "Frenzy, or Double Swing" : "Double Swing, or Frenzy");
        else Log("double swing: off ([hands] double_swing_by_hands)");
    }
    if (!on) {
        g_slot[0].store(-1);
        g_slot[1].store(-1);
        g_level[0].store(-1);
        g_level[1].store(-1);
        g_attack.store(-1);
    }
}

bool On() { return g_on.load(); }

void Tick() {
    if (g_on.load()) handvitals::game::Tick(true);   // the hero's mana, five times a second, on the UI thread
}

// -- the pad's ten skill slots (the UI thread): shared with [gameplay] melee_skill_armed (melee_arm_game.cpp) --------------------------------
bool ReadPadSlots(int skills[shieldsmite::kSlotCount], int owners[shieldsmite::kSlotCount], const char** why) {
    auto no = [why](const char* text) {
        *why = text;
        return false;
    };
    const uintptr_t bankFn = d2rsig::Addr(RVA_CTRL_BANK), slotFn = d2rsig::Addr(RVA_BUTTON_SLOT), table = d2rsig::Addr(RVA_CTRL_TABLE);
    if (!bankFn || !slotFn || !table) return no("the game's controller tables not found (yet)");
    int bank = -1;
    if (!CallInt(bankFn, &bank) || (bank != 0 && bank != 16)) return no("the controller bank is not 0 or 16");
    unsigned used = 0;
    for (int i = 0; i < shieldsmite::kSlotCount; ++i) {
        const shieldsmite::PadPress p = shieldsmite::SlotPress(i);
        int slot = -1;
        // A customised layout that gives two buttons one slot is refused: the press would cast the wrong skill.
        if (!CallSlot(slotFn, p.buttons, p.lt != 0, &slot) || slot < 0 || slot >= 16 || (used & (1u << slot))) return no("the pad buttons do not map to ten slots");
        used |= 1u << slot;
        int32_t record[2] = {};
        if (!Rd(table + uintptr_t(bank + slot) * kRecordSize, &record) || record[0] < -1 || record[0] > 4095) return no("a controller record does not read");
        skills[i] = record[0];
        owners[i] = record[1];
    }
    return true;
}

int ReadHeroLevel(int id, int owner) { return ReadLevel(id, owner); }

// -- which pad slots hold the two skills (the UI thread) ---------------------------------------------------------------------------
void UiRead() {
    if (!g_on.load()) return;
    static bool toldWhy = false;
    static int toldSlot[kSkillCount] = {-2, -2}, toldLevel[kSkillCount] = {-2, -2}, toldAttack = -3;
    auto fail = [&](const char* why) {
        for (int k = 0; k < kSkillCount; ++k) {
            g_slot[k].store(-1);
            g_level[k].store(-1);
        }
        g_attack.store(-1);
        if (!toldWhy) {
            toldWhy = true;
            LogF("double swing: the pad slots cannot be read (%s) - the second hand's touch is a normal attack", why);
        }
    };
    int skills[shieldsmite::kSlotCount], owners[shieldsmite::kSlotCount];
    const char* why = "";
    if (!ReadPadSlots(skills, owners, &why)) return fail(why);
    toldWhy = false;
    constexpr int kAttackSlot = 8;   // the pad's A: the attack button (shield_smite.h's order)
    g_attack.store(skills[kAttackSlot]);
    if (skills[kAttackSlot] != toldAttack) {
        toldAttack = skills[kAttackSlot];
        if (toldAttack == kDoubleSwing || toldAttack == kFrenzy) LogF("double swing: %s is on the attack button (pad A) - every touch casts it already, the second hand adds nothing", toldAttack == kDoubleSwing ? "Double Swing" : "Frenzy");
    }
    for (int k = 0; k < kSkillCount; ++k) {
        int slot = -1, owner = -1;
        for (const int s : shieldsmite::kSlotOrder)
            if (skills[s] == kSkillId[k]) {
                slot = s;
                owner = owners[s];
                break;
            }
        const int level = slot >= 0 ? ReadLevel(kSkillId[k], owner) : -1;
        g_slot[k].store(slot);
        g_level[k].store(level);
        if (slot != toldSlot[k] || level != toldLevel[k]) {
            toldSlot[k] = slot;
            toldLevel[k] = level;
            if (slot < 0) LogF("double swing: %s is on none of the pad's ten skill slots - the second hand's touch cannot press it", SkillName(k));
            else {
                const shieldsmite::PadPress p = shieldsmite::SlotPress(slot);
                char lv[40];
                if (level < 0) snprintf(lv, sizeof lv, "not read");
                else if (level == 0) snprintf(lv, sizeof lv, "none (not learned)");
                else snprintf(lv, sizeof lv, "%d", level);
                LogF("double swing: %s sits on pad slot %d (button 0x%04X%s), the hero's level in it: %s", SkillName(k), slot, p.buttons, p.lt ? " with LT" : "", lv);
            }
        }
    }
}

Press Strike(int hand, uint32_t id, uint64_t nowMs, bool dualWield) {
    Press p;
    if (!g_on.load()) return p;
    const Facts f = Gather(nowMs);
    const double window = ClampWindowMs(g_window.load());
    const Verdict v = g_tracker.Strike(hand, id, nowMs, dualWield, window, f);
    Tell(hand, id, v, f, window);
    if (v.dual) {
        const shieldsmite::PadPress pp = shieldsmite::SlotPress(v.slot);
        p.dual = true;
        p.buttons = pp.buttons;
        p.lt = pp.lt;
        p.holdMs = shieldsmite::PulseMs(pp);
        p.skillId = kSkillId[v.skill];
        g_lastCastAt = nowMs;
        g_lastCost = v.cost > 0.0f ? v.cost : 0.0f;
    }
    return p;
}

void Note(int hand, uint32_t id, uint64_t nowMs) {
    if (g_on.load()) g_tracker.Record(hand, id, nowMs);
}

void Forget() { g_tracker.Reset(); }

}  // namespace doubleswing
