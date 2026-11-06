// direct_hit_mode.h - D2R VR Settings' one "Direct hit" selector (Strikes tab) and the headset's Melee > Attack row, over the keys that decide
// it. Pure; the plugin does not need it: it goes on reading [gameplay] melee_direct, melee_direct_client and melee_direct_scope (vr/vrcam.cpp).
//
// The author (2026-10-28): one place decides the direct hit for single player, the host and a player who joined someone's game; Serg (2026-11-06):
// four choices, "1 выключен 2 только двуручное оружие на вторую и последующие цели по правилам 3 по правилм 4 включено всегда не рекомендуется":
//   Off                          melee_direct 0, melee_direct_client 0                         the touch presses the game's own attack
//   Two-handed extras            melee_direct 2, melee_direct_client 1, melee_direct_scope 0   a two-handed swing's 2nd and further monsters by
//                                                                                             the rules (the cleave); the first: the game's attack
//   By the rules                 melee_direct 2, melee_direct_client 1, melee_direct_scope 1   the first touch on a monster too, then the attack
//                                                                                             time, then speed + force + swing length
//   Always (not recommended)     melee_direct 1, melee_direct_client 2                         every accepted touch is a direct hit
// melee_direct_scope missing (an ini of before 0.205.3): what 0.204 did - with quick blow on, By the rules was the cleave's extras only.
// The old tick boxes' 1 keep their meaning: melee_direct=1 was "every touch" where the server runs, melee_direct_client=1 the joined
// player's rules - so an ini of before reads as Always (the first) or By the rules (the second).
#pragma once

namespace directhit {

enum Mode : int { kOff = 0, kCleave = 1, kRules = 2, kFull = 3, kModes = 4 };

// What the keys say (any value: what is not 1 or 2 is off). Always wins over By the rules. `scope`: melee_direct_scope, -1 when missing;
// `quickBlow`: [gameplay] quick_blow, for a missing scope.
inline int FromKeys(int meleeDirect, int meleeDirectClient, int scope = -1, bool quickBlow = false) {
    if (meleeDirect == 1 || meleeDirectClient == 2) return kFull;
    if (meleeDirect == 2 || meleeDirectClient == 1) return scope == 0 ? kCleave : scope == 1 ? kRules : quickBlow ? kCleave : kRules;
    return kOff;
}
// The keys a choice writes (a value out of range: Off). *scope = -1: melee_direct_scope is not written (Off, Always: it means nothing there).
inline void ToKeys(int mode, int* meleeDirect, int* meleeDirectClient, int* scope = nullptr) {
    const bool rules = mode == kCleave || mode == kRules;
    *meleeDirect = rules ? 2 : mode == kFull ? 1 : 0;
    *meleeDirectClient = rules ? 1 : mode == kFull ? 2 : 0;
    if (scope) *scope = mode == kCleave ? 0 : mode == kRules ? 1 : -1;
}

}  // namespace directhit
