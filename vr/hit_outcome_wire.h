// hit_outcome_wire.h - [debug] hit_outcome on the game (vr/hit_outcome_wire.cpp; the ring and the lines are vr/hit_outcome.h).
#pragma once

#include <cstdint>

namespace hitoutcome {

void SetLogger(void (*log)(const char*));
// [debug] hit_outcome, live (vrcam's LoadSettings). Read only: it logs, it changes nothing the game does.
void SetWanted(bool on);

// vrcam's timer, twice a second: puts the observer on the game's damage function 0x44CE80 once the key asks for it (the roll's observer is
// vr/haptics.cpp's, haptics::Install puts it in for this key too), and drains what both observers saw into the log.
void Install();
void Drain(uint64_t nowMs);

}  // namespace hitoutcome
