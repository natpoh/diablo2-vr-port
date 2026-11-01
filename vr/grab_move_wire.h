#pragma once
// grab_move_wire.h - [weight] grab_moves_unit, the game's side (vr/grab_move.cpp; what it is: vr/grab_move.h). Off: nothing is asked, nothing is read.

#include <cstdint>

namespace D2RL {
struct PluginContext;
}

namespace grabmove {

// Once, from the plugin's start: the services.
void Init(const D2RL::PluginContext* ctx);
// [weight] grab_moves_unit, from the ini (every read).
void SetWanted(bool on);
bool Wanted();   // on, and no fault this session
// The knock-down library's timer (vr/knock_down.cpp), for each body a hand took that the library draws held, flying, lying or getting up: it is drawn dx, dy
// cells off its unit (grabmove::CellsApart). Asks the game for the knockback when it is far enough (grabmove::Wants) - only with the server in this process.
void Note(uint32_t id, float dx, float dy, bool hostile, uint64_t nowMs);
// The same timer, every tick: a knockback asked is looked at once it has run (where the unit stands - the log).
void Tick(uint64_t nowMs);

}  // namespace grabmove
