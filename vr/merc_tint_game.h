// merc_tint_game.h - [hud] merc_tint in the game (vr/merc_tint.cpp): what vrcam calls. The look and the decisions are vr/merc_tint.h.
#pragma once

#include "merc_tint.h"

namespace D2RL {
struct PluginContext;
}

namespace merctint {

// Once, at the plugin's load: the threads service (the merc's life is read on the game's UI thread).
void Init(const D2RL::PluginContext* ctx);
// The ini, every read. Off: nothing is read and Current gives nothing.
void Configure(const Config& config);
bool Enabled();
// vrcam's timer: wantRead = on, VR F4, in the world. Five times a second the merc is looked for and its life read on the UI thread.
void Tick(bool wantRead);
// The render thread, for the effect: this frame's capsule (MercA, MercB), false = no tint now. upm: world units per metre; eyeWorld:
// where the hands hang (the effect's frame); heroFootY: the hero's feet, render world units (the merc is taken on the same floor).
bool Current(float upm, const float eyeWorld[3], float heroFootY, Uniforms* out);

}  // namespace merctint
