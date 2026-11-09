// arrow_flight_wire.h - [bow] arrow_flight, the game's side (arrow_flight_wire.cpp): the hero's shot arrow drawn by us, from the bow along the shot's line
// (up or down with [bow] pitch_aim) to what the line met - the game's own arrow model, put where we draw it. The maths is vr/arrow_flight.h; the design
// docs/notes/arrow_flight.md.
//
// Visual only. The game's missile - its speed, its collision, its hit, its damage - is the game's and is not touched; only the matrices the renderer computes
// for the hero's arrow missile's entities are written again, in VR first person (F4), for the arrows of a shot noted here within its window. The hooks are
// vr/potion_hide.cpp's (the visual-init callback, the world-matrix function).
#pragma once

#include <cstdint>

#include "arrow_flight.h"
#include "bow_pitch.h"

namespace D2RL {
struct PluginContext;
}

namespace arrowflightwire {

void Init(const D2RL::PluginContext* ctx);
// The hero's client unit id (vrcam's GestureHeroId): whose arrows are the hero's.
void SetHeroIdSource(bool (*fn)(uint32_t* id));
// Where ours is drawn and shots are noted: vrcam's VR F4 and, for [bow] arrow_flight_flat, the flat 3D views (vr/flat_arrow.h). None set: VR F4.
void SetViewGate(bool (*fn)());

// [bow] arrow_flight (the box: off = nothing here does anything, the game draws its arrow as ever), [debug] arrow_flight_trace (log lines: each arrow
// taken, its entities, its flight's summary), [debug] arrow_flight_fx_off (the arrow's effect entities - an elemental arrow's fire, frost, light - switched
// off by the game's own switch while ours flies: for a test, in case the game's effects do not follow the matrix), [bow] arrow_flight_straight (the shot's own
// arrow straight along its line when the line met nothing; off: it joins the game's path, as 0.179 drew it). Any thread, live.
void Configure(bool on, bool trace, bool fxOff, bool straight = true);
bool On();

// vrcam, at the game's own read of a shot's aim (its thread; F4, a bow or crossbow in hand): the shot's line from the arrow's release point o along d (render
// metres: world units over upm - the frame the drawn hands are put in), the heading theta the game was given (atan2(z, x) on the ground), what the line met
// (bowpitchwire::Theta's, null when [bow] pitch_aim did not decide), the hero as drawn (world units) and upm. shot: the bow draw's shot count - the first note
// of a shot stands for it (the hooks ask several times for one press) - or 0 for a line of the hand's (a crossbow, a bow without the draw): every note moves it.
void NoteShot(unsigned shot, const float o[3], const float d[3], float theta, const bowpitch::Result* met, const float heroRender[3], float upm);

// Our own latest shot (world units), while its window is open: what goes to the other players (vr/shot_share.h). False: none.
bool Current(arrowflight::Shot* out);
// Another player's shot as it came over the network (vr/shot_share.h; any thread): his arrows made within the window are drawn along it - in the views ours
// are drawn in, with the box on.
void NoteRemote(uint32_t owner, const arrowflight::Shot& s);

// vrcam's timer: the arrows done with are let go of (and, with the trace, summed up in the log).
void Tick();

}  // namespace arrowflightwire
