#pragma once
// The allies' portraits' side in the game (vr/ally_portraits_game.cpp): the party's players (the roster) and our mercenary and summons (the client's pet
// list), read on the game's UI thread from the CLIENT's own memory - where its portrait panel reads them, so single player, the host and a TCP/IP joiner
// alike. Nothing of the game is written; nothing is called but the two getters of the client's own hero (as the hand vitals have them) and the game's
// string lookup (the names, in the game's language, when D2RLoader's localization service does not answer). The logic and the layouts are
// vr/ally_portraits.h (pure, tested).

#include "ally_portraits.h"

namespace D2RL {
struct PluginContext;
}

namespace allyportraits {
namespace game {

// Once, from the plugin's start: the loader's services.
void Init(const D2RL::PluginContext* ctx);

// vrcam's update thread, every tick: while `wantRead`, five reads a second; not wanted: the last read is dropped.
void Tick(bool wantRead);

// The newest read: its tiles' look (vr/ally_portraits.h MakeLook) and how old it is, in milliseconds; false when there is none (no hero, a read that
// did not add up, the guards did not pass).
bool Latest(Look* look, double* ageMs);

}  // namespace game
}  // namespace allyportraits
