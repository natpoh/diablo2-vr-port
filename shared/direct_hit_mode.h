// direct_hit_mode.h - D2R VR Settings' one "Direct hit" selector (Strikes tab) over the two keys that decide it. Pure; the plugin
// does not need it: it goes on reading [gameplay] melee_direct and melee_direct_client (vr/vrcam.cpp).
//
// The author (2026-10-28): one place decides the direct hit for single player, the host and a player who joined someone's game:
//   Off                          melee_direct 0, melee_direct_client 0   the touch presses the game's own attack
//   By the rules                 melee_direct 2, melee_direct_client 1   the first touch on a monster, then the attack time, then
//                                                                        speed + force + swing length (vr/melee_direct_client.h)
//   Fully on (not recommended)   melee_direct 1, melee_direct_client 2   every accepted touch is a direct hit (the old experimental box)
// The old tick boxes' 1 keep their meaning: melee_direct=1 was "every touch" where the server runs, melee_direct_client=1 the joined
// player's rules - so an ini of before reads as Fully on (the first) or By the rules (the second).
#pragma once

namespace directhit {

enum Mode : int { kOff = 0, kRules = 1, kFull = 2 };

// What the two keys say (any value: what is not 1 or 2 is off). Fully on wins over By the rules.
inline int FromKeys(int meleeDirect, int meleeDirectClient) {
    if (meleeDirect == 1 || meleeDirectClient == 2) return kFull;
    if (meleeDirect == 2 || meleeDirectClient == 1) return kRules;
    return kOff;
}
// The two keys a choice writes (a value out of range: Off).
inline void ToKeys(int mode, int* meleeDirect, int* meleeDirectClient) {
    *meleeDirect = mode == kRules ? 2 : mode == kFull ? 1 : 0;
    *meleeDirectClient = mode == kRules ? 1 : mode == kFull ? 2 : 0;
}

}  // namespace directhit
