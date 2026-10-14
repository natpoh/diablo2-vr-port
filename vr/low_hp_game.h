#pragma once
// The low-life vignette's side in the game and the effect (vr/low_hp.cpp): the hero's life read on the game's UI
// thread, the beat stepped once a pair, the effect's uniforms handed out, the haptic pulse sent. The logic is
// vr/low_hp.h (pure, tested); nothing of the game is written - the life is read, the picture gets a colour.

#include "low_hp.h"

namespace D2RL {
struct PluginContext;
}

namespace lowhp {

// Once, from the plugin's start: the loader's services (the UI thread).
void Init(const D2RL::PluginContext* ctx);

// On every read of d2r_vr.ini ([hud] low_hp_*, [debug] low_hp_*). Live: no restart.
void Configure(const Config& config);

// [hud] low_hp_vignette is ticked. Any thread.
bool Enabled();

// vrcam's update thread, every tick: queues the read of the hero's life on the UI thread (five a second while
// `wantRead`, a no-op when the setting is off).
void Tick(bool wantRead);

// Once a pair, on the draw thread (NativePairBegin): steps the beat by the real time since the last pair, so both
// eyes of the pair get the same opacity. `menuOpen`: the game has a menu up. tanX, tanY: how wide the eyes' view is
// as the tangent of its half-angle on each axis (lowhp::HalfTan of the runtime's field of view), 0 = not known.
void Frame(const Where& where, bool menuOpen, float tanX, float tanY);

// What the effect gets for the eye being recorded: false = nothing to tint (it is left as it is, bit for bit).
bool Current(Uniforms* out);

}  // namespace lowhp
