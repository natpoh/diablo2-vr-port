// shield_smite_game.h - [gameplay] shield_smite, the game-facing half (shield_smite.cpp). The decisions are in the
// pure shield_smite.h; this file is what vrcam, gamestate and the spell-aim hooks call.
#pragma once

#include <cstdint>

namespace D2RL {
struct PluginContext;
}

// F4 ONLY (vr/view_gate.h): the bash, the press and the lookups act in VR first person only; a view change closes the cast window.
namespace gamestate {
bool LeftShield();   // a shield in the left hand of the active set (gamestate.cpp)
}

namespace shieldsmite {

constexpr int kSmiteSkill = 97;   // Skills.txt: Smite (the same as shield_smite.h's kSmite)

// Plugin start: the log.
void Init(const D2RL::PluginContext* ctx);
// [gameplay] shield_smite from the ini (vrcam's settings load): off = nothing below does anything.
void Configure(bool on);
bool On();
// [gameplay] shield_smite_min_speed (m/s, 0.3 .. 8, default 0.8) and shield_smite_stroke_cm (cm, 2 .. 30, default 8) from the ini - the bash's own two numbers,
// apart from hit by touch's (what the shield always used; live): the speed the shield must reach for a bash to count and how far it must move to start one.
void SetTuning(float minSpeedMetresPerSecond, float strokeCm);
// On(), or [debug] skill_triggers is on (the shield is drawn and described then, with shield_smite off too): vrcam's timer calls Tick while this is true.
bool Watching();

// vrcam's timer (every 10 ms or so): `play` = a game area, first person, no menu; `upm` = world units per metre;
// `hero` = the hero's client unit (0 when not known). Reads the left hand and the shield, looks for a monster
// only while the shield is in a hard stroke, and presses Smite's pad slot once when it touches one.
void Tick(bool play, float upm, uintptr_t hero);

// The UI thread (gamestate's read of the hands, 5 times a second): which pad slot Smite sits in, from the game's own
// controller tables.
void UiRead();

// The game's target lookups (vrcam's SpellTarget / HoverUnit hooks). where: 1 = the cast resolver's lookup,
// 2 = the map click's lookup (the click is what carries the target to the game). skill: the skill the lookup is for,
// -1 when it does not say. Returns the monster the shield touched while its cast is on its way, else null - the
// caller then answers as it did before.
void* Target(int where, int skill);
// The cast resolver's point (cell coordinates) once it has run for that cast: the touched monster's place.
void Aim(float* x, float* y);
// A cast of ours is on its way (an atomic read, for the hooks' fast path).
bool Casting();

}  // namespace shieldsmite
