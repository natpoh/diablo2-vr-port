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

// ---- the game's side (vr/melee_direct.cpp) ----------------------------------------------------------------------------------

struct Request {
    uintptr_t game = 0;       // the server's game (learned by melee_touch's observer of the server's unit lookup)
    uint32_t heroId = 0;      // the hero's unit id, the same on the client and on the server
    uint32_t monsterId = 0;   // the touched monster's id, the same on the client and on the server
    uintptr_t lookup = 0;     // the server's unit lookup (game, type, id)
};

// D2RLoaderLoadPlugin.
void Init(const D2RL::PluginContext* ctx);
// [gameplay] melee_direct and melee_direct_min_ms, from the ini (every read). Off: nothing is hooked, nothing is called.
void SetWanted(bool on);
bool Wanted();
void SetMinIntervalMs(uint32_t ms);
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
