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
// [weight] knock_down_moves_unit, from the ini (every read): a body a weapon's blow knocked down is moved too.
void SetKnockWanted(bool on);
// [weight] leap_landing_scatter, from the ini (every read): a body thrown by the hero's landing from a Leap is moved too.
void SetLeapWanted(bool on);
// Whose body: a hand took it, a blow knocked it down, a leap's landing threw it - each under its own switch.
enum Source : int { kGrab = 0, kKnock = 1, kLeap = 2 };
bool WantedFor(int source);   // that source's switch on, and no fault this session
// The knock-down library's timer (vr/knock_down.cpp), for each body a hand took or a blow knocked down that the library draws held, flying, lying, getting up
// or walking home: it is drawn dx, dy cells off its unit (grabmove::CellsApart). Asks the game for the knockback when it is minCells off (grabmove::StepFor
// its phase, grabmove::Wants) - only with the server in this process and that body's switch on. True: a knockback was asked (queued for the game's thread).
bool Note(uint32_t id, float dx, float dy, bool hostile, int source, float minCells, uint64_t nowMs);
// The same timer, every tick: a knockback asked is looked at once it has run (where the unit stands - the log).
void Tick(uint64_t nowMs);

}  // namespace grabmove
