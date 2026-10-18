#pragma once
// The wrist status' side in the game (vr/wrist_status_game.cpp): the hero's companions (its pet lists) and the effects on it (its states), read on
// the game's own thread through the local server's player unit. Nothing of the game is written; nothing is called but the game's unit lookup and
// its state check. The logic and the tables are vr/wrist_status.h (pure, tested).
//
// After bearstonem/d2r-pcvr (MIT): vr/companions.cpp and vr/active_effects.cpp, the same two reads.

#include "wrist_status.h"

namespace D2RL {
struct PluginContext;
}

namespace wriststatus {
namespace game {

// Once, from the plugin's start: the loader's services.
void Init(const D2RL::PluginContext* ctx);

// vrcam's update thread, every tick: while `wantRead`, five reads a second (the server's lookup is observed for it, melee_touch.h SetStatusWanted);
// not wanted: the lookup is let alone and the last read is dropped.
void Tick(bool wantRead);

// The newest read and how old it is, in milliseconds; false when there is none (or the reads were switched off by a fault). Either part may be
// unknown (`known` false): its guard failed, or the game has no such data yet.
bool Latest(Roster* roster, Status* status, double* ageMs);

}  // namespace game
}  // namespace wriststatus
