// move_attack_wire.h - [gameplay] move_while_attacking on the game (vr/move_attack_wire.cpp; the decisions are vr/move_attack.h, why and
// what the game does: docs/notes/move_while_attacking.md). EXPERIMENTAL, off by default, VR first person (F4) only, spear / polearm /
// javelin / bow / crossbow only, where the game's server runs in this process: single player, or a TCP/IP game we host - then for every hero
// in it (the host decides). Joined to another's game our client sends nothing extra.
#pragma once

#include <cstdint>

#include "move_attack.h"

namespace moveattack {

void SetLogger(void (*log)(const char*));
// [gameplay] move_while_attacking, live (vrcam's LoadSettings). Off: the hooks (if they went in) pass everything through, the pad is untouched.
void SetWanted(bool on);
bool Wanted();
// The three hooks - the server's attack-mode gate and frame trigger, the controller's per-player update - and the proofs of the two game
// functions called; tried again from vrcam's install round until they are in, only once the option was ticked.
void Install();

// The game's pad poll (vrcam's ApplyActions, after Stand Still made its verdict): `attackBits` are the pad's attack and skill buttons of
// ours this poll (A only when it is an attack, not a pick-up). While a walk has cut an attack short they are taken off until the cut
// animation would have ended; one pressed in that time is pressed then. Off, or not F4: an empty answer.
PadOut Pad(uint64_t nowMs, uint16_t attackBits);

}  // namespace moveattack
