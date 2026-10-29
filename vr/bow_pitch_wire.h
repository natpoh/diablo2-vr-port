// bow_pitch_wire.h - [bow] pitch_aim, the game's side (bow_pitch_wire.cpp): the scene a bow's shot is decided in, read on vrcam's timer, and the
// heading the game's aim hooks give the shot. The maths is vr/bow_pitch.h; the design docs/notes/bow_pitch.md.
//
// The game's own attack, as ever: its animation, rate, mana and arrows, its missile from the hero. Only the heading of the point the aim hooks
// already give a bow's shot (vrcam HookAttackPoint, HookAttackTarget, HookUnitFacing, HookSpellResolve) is turned - from the line's heading on the
// ground to the heading of the cell of what the line meets. No target unit is handed to the game (no chase), no server call is made.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstdint>

#include "bow_pitch.h"

namespace bowpitchwire {

void Init(const D2RL::PluginContext* ctx);

// The settings (any thread): [bow] pitch_aim (the box; unticked = nothing here does anything, the shot's heading is the line's own as before) and
// [debug] bow_pitch_trace (a line in the log for every shot: what the line met, the heading against the line's own, the scene it was decided in).
void Configure(bool on, bool trace);
bool On();

// vrcam's timer (MeleeTouchTick), every pass. want: F4, a bow or crossbow in hand, in the world, no menu (the box is asked here). bodies: the models
// standing near are read too (skel::MonsterRigs - the string is drawn, a shot is under way, a crossbow is aimed). heroUnit: the hero's client unit;
// heroRender: his place as drawn (world units: vrcam's g_heroPosNow, the camera's look-at - its height is his ground); upm: world units a metre.
// About every 100 ms: the monsters meleetouch's poll keeps (PolledMonsters: their cells, rows and the server's verdicts) and the models matched to
// them by the ground's shear (bowpitch::MatchUnit) made into a bowpitch::Scene.
void Poll(bool want, bool bodies, uintptr_t heroUnit, const float heroRender[3], float upm);

// The game's aim hooks (its thread): the shot's heading on the grid, atan2(z, x), for the line o + d (render metres: world units over upm, the frame
// the drawn hands are put in). shot: the bow draw's shot count - one decision for one shot, the hooks ask several times for one press - or 0 for a
// line that is the hand's now (a crossbow, a bow without the draw: decided at every ask). False: nothing to turn, the line's own heading stays (the box
// unticked, not F4, no fresh scene, the line meets nothing within reach, its target on the hero's own cell). met (optional): what the line met, for the
// same shot the same answer ([bow] arrow_flight flies our arrow to it); untouched when the box is unticked or not F4.
bool Theta(const float o[3], const float d[3], unsigned shot, float* theta, bowpitch::Result* met = nullptr);

// [debug] bow_aim_ray: what the line o + d (render metres) meets now, for the ray's end. False: the box unticked, not F4, no fresh scene.
bool Meets(const float o[3], const float d[3], bowpitch::Result* out);

}  // namespace bowpitchwire
