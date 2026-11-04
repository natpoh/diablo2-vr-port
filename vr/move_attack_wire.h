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

// The flat game ([gameplay] flat_move_while_attacking: 0 off, 1 the F4 option's weapons, 2 every weapon), live: our hero in the monitor's F2 / F3 or VR F1 / F2 / F3
// walks on with W A S D once the blow is out. The same hooks as above (Install puts them in once either key asks).
void SetFlatWanted(int key);
// Our hero in the flat key's views now (vr/attack_path_wire.cpp).
bool PathViewNow();
// vrcam, once: true in the flat key's views (the monitor's F2 / F3 with our camera, VR F1 / F2 / F3).
void SetFlatCameraFn(bool (*fn)());
// vrcam, once: the monitor game's view number (1 F1, 2 F2, 3 F3; 0 in VR) - the per-attack log names the view.
void SetFlatViewFn(int (*fn)());
// vrcam's map click hook, first thing, for every click of our hero: true = the click waits for the game's attack rate (the hook answers 1,
// as the game does for a click it took, and does not call the game). Only in the flat key's views with the key on; a press held back is made
// again by the plugin when the cut animation would have ended.
bool HoldClick(void* unit, int type, int x, int y, uint64_t flags);

}  // namespace moveattack
