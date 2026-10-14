#pragma once
// The hand vitals' side in the game (vr/hand_vitals_game.cpp): the hero's life and mana read on the game's UI thread. Nothing of the
// game is written; the numbers are read through the game's own stat getter. The logic is vr/hand_vitals.h (pure, tested).

#include "hand_vitals.h"

namespace D2RL {
struct PluginContext;
}

namespace handvitals {
namespace game {

// Once, from the plugin's start: the loader's services (the UI thread).
void Init(const D2RL::PluginContext* ctx);

// vrcam's update thread, every tick: queues the read of the hero's numbers on the UI thread (five a second while `wantRead`).
void Tick(bool wantRead);

// The newest read: true with the numbers when there is one (`known` says whether they made sense) and how old it is, in milliseconds.
bool Latest(Vitals* out, double* ageMs);

}  // namespace game
}  // namespace handvitals
