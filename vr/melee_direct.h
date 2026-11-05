// melee_direct.h - "Direct hit": the user's EXPERIMENTAL option for hit by touch ([gameplay] melee_direct, default off).
//
// Hit by touch (melee_touch.h) picks the target and the moment and asks the game to attack with its own key: the game then
// runs its whole chain - the command check, the gate that sets the attack rate, the animation, the reach, the rolls. That
// chain costs time between the touch and the blow. This option is the other way, the one bearstonem/d2r-pcvr takes
// (MIT, vr/melee.cpp): at the touch the plugin calls the game's own basic attack on the server (Attack 0x42FEB0), on the
// game's thread, with the touched monster given to it and the reach replaced by the blade's contact. What it keeps: the
// roll, the damage, the durability, everything inside the game's attack. What it skips: the animation and its sound, the
// attack-rate gate, the walk up to the target, the command's 50-cell check, the start checks (town, mana, ammo present),
// the skill's delay and the end-of-animation work (docs: side-session-notes/attack_path_recon.md). It only works while the
// server is the game's own (offline): with no local server found, or a signature missing, the key path of melee_touch
// goes on exactly as it does with the box unticked.
//
// This header is the pure part: which route a touch takes, and whether the units the game thread found allow the call.
// The game's side is vr/melee_direct.cpp; the test is cleanroom/tests/melee_direct_test.cpp.
#pragma once

#include <cstdint>

namespace D2RL {
struct PluginContext;
}

namespace meleedirect {

enum class Route {
    Key,      // the game's own attack key, as with the option off (or not possible)
    Direct,   // the direct call
    Skip,     // inside the minimum interval after the last direct hit: nothing is done for this touch
};

struct Gate {
    bool wanted = false;        // [gameplay] melee_direct
    bool ready = false;         // the signatures and the two hooks are in, no fault, the server's game is known
    uint64_t nowMs = 0;
    uint64_t lastMs = 0;        // when the last direct hit was asked for (0 = none)
    uint32_t minIntervalMs = 0; // [gameplay] melee_direct_min_ms, 0 = none beyond the detector's one hit per monster per stroke
    bool f4 = true;             // the view is VR first person (viewgate::F4()): the option is F4 only like the rest of hit by touch
};

inline Route Decide(const Gate& g) {
    if (!g.f4) return Route::Skip;                  // another view: nothing is done for a touch (a touch there is not accepted at all)
    if (!g.wanted || !g.ready) return Route::Key;   // off, or not possible: today's behaviour
    if (g.minIntervalMs && g.lastMs && g.nowMs >= g.lastMs && g.nowMs - g.lastMs < g.minIntervalMs) return Route::Skip;
    return Route::Direct;
}

// What the game's thread found when the request came (the server's units, read there, then).
struct Found {
    bool player = false;          // the hero's server unit, with its header (type 0, id, back pointer to the game) proven
    uint32_t playerMode = 0;
    bool monster = false;         // the monster's server unit, header proven (type 1, id)
    uint32_t monsterMode = 0;
    bool hostile = false;         // the server's Relation said so
    bool wall = false;            // the server's Line said there is a wall between
};

enum class Verdict { Go, NoPlayer, PlayerMode, NoMonster, MonsterDying, NotHostile, Wall };

inline Verdict Check(const Found& f) {
    if (!f.player) return Verdict::NoPlayer;
    // dead, in the town portal, dying (bearstonem's gates; the same modes melee_touch's probe uses)
    if (f.playerMode == 0 || f.playerMode == 17 || f.playerMode == 5 || f.playerMode == 6) return Verdict::PlayerMode;
    if (!f.monster) return Verdict::NoMonster;
    if (f.monsterMode == 0 || f.monsterMode == 12) return Verdict::MonsterDying;
    if (!f.hostile) return Verdict::NotHostile;
    if (f.wall) return Verdict::Wall;
    return Verdict::Go;
}

inline const char* Name(Verdict v) {
    switch (v) {
        case Verdict::Go: return "go";
        case Verdict::NoPlayer: return "no hero on the server";
        case Verdict::PlayerMode: return "hero's mode (dead, in a portal, in town)";
        case Verdict::NoMonster: return "no such monster on the server";
        case Verdict::MonsterDying: return "monster dying or dead";
        case Verdict::NotHostile: return "not hostile";
        case Verdict::Wall: return "a wall between";
    }
    return "?";
}

// The window of one direct call (thread-local in the game side): the unit pair the hooks answer for. Only while a call of
// ours is in flight on this thread; outside it every hook goes straight to the game's original.
struct Window {
    uintptr_t game = 0, attacker = 0, defender = 0;
    bool active() const { return game && attacker && defender; }
};

// The target hook answers the touched monster only for the attacker, inside the game's basic attack, at its two call sites
// (the return addresses): any other asker of the target gets the game's own answer.
inline bool AnswersTarget(const Window& w, uintptr_t game, uintptr_t unit, uintptr_t ret, uintptr_t site1, uintptr_t site2) {
    return w.active() && w.game == game && w.attacker == unit && (ret == site1 || ret == site2);
}

// The reach hook answers (reach is the contact; the wall check stays the game's) for exactly the pair of this window.
inline bool AnswersRange(const Window& w, uintptr_t attacker, uintptr_t defender) {
    return w.active() && w.attacker == attacker && w.defender == defender;
}

// ---- [gameplay] melee_direct_by_hand: the left weapon's blow for the left hand ----------------------------------------------------
// The game alternates a dual wielder's plain attacks ON THE SERVER: its command parser (0x4FDB40 -> 0x4FC0F0) flips a flag on the
// hero's "Attack" skill node at every attack command and, every other time with a weapon in each hand, swaps the skill for the hidden
// "Left Hand Swing" (skills.txt id 5: weapsel 1, the left weapon; the same server functions as Attack, animation S3). The direct call
// skips that parser, so without this every direct blow of a Barbarian or Assassin with two weapons was the right weapon's
// (releases/analysis/dual_wield_weapon_choice_2026-11-04.md). With the option, a blow whose blade is the game's LEFT-hand weapon is
// struck the way the game strikes its own left swings: for the one call, the hero's current skill is his own "Left Hand Swing" node
// (found by the game's SkillLookup, as 0x4FC0F0 finds it), and the game's weapon getter picks the left weapon by that skill's weapsel.
// The roll, the damage of that weapon, the rest of the attack stay the game's; the current skill is put back right after the call.
// One weapon, a two-handed weapon, the right-hand weapon, a joined player's extra hit (its hand is not known here): as before.
constexpr int kSkillLeftHandSwing = 5;
constexpr uint8_t kWeapselLeft = 1;
inline bool LeftSwing(bool byHand, bool weaponInEachHand, int slot, bool remote) { return byHand && weaponInEachHand && slot == 1 && !remote; }

// The layout the swap stands on, proven from the game's own weapon getter (ServerUnitWeapon 0x34B000) where this build has it: at +0x141
// "mov rcx, [rdi + 0x100]" (the unit's skill list), at +0x14E "call <getter>" with the getter "test rcx, rcx; jne +3; xor eax, eax; ret;
// mov rax, [rcx + 0x18]; ret" (the list's current skill), and at +0x16C "movzx edx, byte [rax + 0x208]" (the skills.txt row's weapsel).
// Each array: the bytes found there (the call's 5: E8 and its rel32, followed by the caller).
constexpr uint32_t kAtSkillList = 0x141, kAtGetterCall = 0x14E, kAtWeapsel = 0x16C;
constexpr uintptr_t kUnitSkillList = 0x100, kListCurrentSkill = 0x18, kRowWeapsel = 0x208;
inline bool LayoutProven(const uint8_t listMov[7], uint8_t callOp, const uint8_t getter[13], const uint8_t weapsel[7]) {
    static const uint8_t kList[7] = {0x48, 0x8B, 0x8F, 0x00, 0x01, 0x00, 0x00};
    static const uint8_t kGetter[13] = {0x48, 0x85, 0xC9, 0x75, 0x03, 0x33, 0xC0, 0xC3, 0x48, 0x8B, 0x41, 0x18, 0xC3};
    static const uint8_t kWeapsel[7] = {0x0F, 0xB6, 0x90, 0x08, 0x02, 0x00, 0x00};
    for (int i = 0; i < 7; ++i) if (listMov[i] != kList[i] || weapsel[i] != kWeapsel[i]) return false;
    if (callOp != 0xE8) return false;
    for (int i = 0; i < 13; ++i) if (getter[i] != kGetter[i]) return false;
    return true;
}
// The node found is the game's "Left Hand Swing": its row's id (int16 at row + 0) is 5 and its weapsel the left weapon.
inline bool IsLeftHandSwing(int16_t rowId, uint8_t weapsel) { return rowId == kSkillLeftHandSwing && weapsel == kWeapselLeft; }

// ---- the game's side (vr/melee_direct.cpp) ----------------------------------------------------------------------------------

struct Request {
    uintptr_t game = 0;       // the server's game (learned by melee_touch's observer of the server's unit lookup)
    uint32_t heroId = 0;      // the hero's unit id, the same on the client and on the server
    uint32_t monsterId = 0;   // the touched monster's id, the same on the client and on the server
    uintptr_t lookup = 0;     // the server's unit lookup (game, type, id)
    bool remote = false;      // [gameplay] melee_direct_client_cleave: a joined player's extra hit, asked of this host (heroId is HIS hero)
    bool leftHand = false;    // [gameplay] melee_direct_by_hand: the blade is the game's left-hand weapon (LeftSwing) - struck as the game's "Left Hand Swing"
};

// [gameplay] melee_direct_client_cleave, the host's side: a joined player's extra hit is struck only within this reach of the
// game's own range test (the server's Range 0x348650 with this range, the original, not the contact) - a bound against a request
// for a monster across the room - and not twice on one monster by one hero within kRemoteGapMs.
constexpr int kRemoteReach = 6;
constexpr uint64_t kRemoteGapMs = 200;

// D2RLoaderLoadPlugin.
void Init(const D2RL::PluginContext* ctx);
// [gameplay] melee_direct and melee_direct_min_ms, from the ini (every read). Off: nothing is hooked, nothing is called.
void SetWanted(bool on);
bool Wanted();
// [gameplay] melee_direct 2 (Settings' "Direct hit: By the rules"): the touches go by melee_direct_client.h's rules (the first touch on a
// monster, then the hero's attack time, then speed + force + length; the two-handed cleave only with its box) - a touch that passes is
// this direct call, one that fails the game's own key. 1 (and the old tick box): every accepted touch, as before. Plan's least interval
// (melee_direct_min_ms) is Full's only.
void SetByRules(bool on);
bool ByRules();
void SetMinIntervalMs(uint32_t ms);
// [gameplay] melee_direct_client_cleave on THIS PC as the host: the joined players' extra hits are struck here (the hooks go in for it
// even with melee_direct off; those requests are struck in any view - the host may be in another one).
void SetRemoteWanted(bool on);
// [gameplay] melee_direct_by_hand (every read): a blow of the left-hand weapon is the game's "Left Hand Swing" (LeftSwing above). Off: the
// right weapon, as before.
void SetByHand(bool on);
bool ByHand();
// The two hooks (the server's target and reach tests) and the proofs of the code, tried again from the timer until they
// are in; only while wanted. No local server found, a signature missing or a fault: Ready() stays false, the key path runs.
void Install();
bool Ready();
// The timer's thread, when a touch is accepted: which route it takes. Direct reserves the interval (stamps it).
Route Plan(uint64_t nowMs, bool serverKnown);
// The timer's thread, when the view is not F4: the requests queued for the game's thread are dropped (none is struck late).
void Flush();
// After Plan said Direct (and the press -> hit clock was started): queue the call for the game's thread.
bool Submit(const Request& r, uint64_t nowMs);

}   // namespace meleedirect
