#pragma once
// [fog] spare_effects: the fog spares fire, spells and smoke (docs/game_render_sky_fog.md, step 1).
//
// Our fog is a post pass by the scene's depth, and the game's transparent effects write no depth: a flame in front
// of a far wall was fogged as the wall. The game composes its frame in an order that keeps them apart - the lit
// opaque scene S, the transparent effects into weighted-blended targets (an accumulation and a revealage), the
// additive ones (fire, glows) into a target of their own A - and only then ps_transparent_resolve writes
//     R = a * S + (1 - a) * T + A        (a: the revealage, T: the transparent colour)
// into the frame. At that draw this module copies, inside the game's own list and after the game's own draw,
// the game's four targets (the accumulation, the revealage, A, and R just written) into textures of ours; the
// effect then knows for each pixel how much of it is the opaque scene, (R - E) / R with E = (1 - a) T + A, and
// fogs only that much (fog_spare_math.h; the effect's FOGSPARE block).
//
// Nothing of ours is drawn among the game's draws and none of its bindings change: the game's own draw is issued
// by us with its own state, then copies with barriers from the states the game's own barriers left. Any doubt
// (a target not seen, a state not known, a size that differs) and the game's draw goes on alone, as before.
// The right eye: the replay sends the same lists again, the copies with them - each eye's own.
//
// Like light_bounds: ReShade does not replay init_pipeline for pipelines made before the add-on joined, so
// Register runs as soon as ReShade is there, when the key is on at the game's start.

#include <reshade.hpp>

namespace fogspare {

// The events, for the game's D3D12 device only. Once. `log` takes one line at a time.
void Register(void (*log)(const char*));
// [fog] spare_effects as read (the settings' load): something for Register to do, and the game's pipelines looked at.
void SetWanted(bool wanted);
bool Wanted();
// Live: the copies are made now (the key on, VR F4, our fog on). Off: the game's draw is left alone.
void Configure(bool on);

// reshade_begin_effects: our four textures made to the game's targets' sizes and bound to the effect (once, again
// only when the game's targets change: binding drains the GPU). True when this frame's copies are there to read
// (the effect's SpareOn).
bool Bind(reshade::api::effect_runtime* rt);

}  // namespace fogspare
