// melee_arm_game.h - [gameplay] melee_skill_armed, the game-facing half (melee_arm_game.cpp). The decisions are in the pure melee_arm.h; this file reads what they need from the game (the pad slots, the
// armed skill's level, the hero's mana), keeps the armed skill, and tells the log. Nothing of the game is written or called that attacks: a press of ours is taken out of the pad the game polls, and
// the touch's own attack press is read by the game as the armed skill through vrcam's slot-skill hook.
#pragma once

#include <cstdint>

#include "armed_icon.h"   // Place

namespace D2RL {
struct PluginContext;
}

// F4 ONLY (vr/view_gate.h): the arming and the armed hits act in VR first person only, with hit by touch swinging a melee weapon.
namespace meleearm {

// Plugin start: the log.
void Init(const D2RL::PluginContext* ctx);
// [gameplay] melee_skill_armed from the ini (every read). Off: nothing below does anything, the armed skill is forgotten.
void Configure(bool on);
bool On();
// [melee_armed] s<id> from the ini (every read): which skills of the list (melee_arm.h kRows) a button arms - Allowed::Bits. A skill unticked while armed is disarmed at the next poll.
void ConfigureList(uint64_t allowedBits);
// [gameplay] melee_skill_armed_icon from the ini (every read): the armed skill's icon in the mana medallion (vr/armed_icon.h; drawn only while melee_skill_armed is on too). Default on.
// sizePct / depthPct / opacityPct / faintPct: [gameplay] melee_skill_armed_icon_size_pct / _depth_pct / _opacity_pct / _faint_pct (armed_icon.h Placement; the bow's chosen skill's
// icon lies the same way).
void ConfigureIcon(bool on, float sizePct, float depthPct, float opacityPct, float faintPct);
bool IconOn();
armedicon::Place IconPlace();   // Placement of the sliders (any thread)
// vrcam's slot-skill hook (the one [bow] quiver_plain_no_mana and the throw use) is in (1), not yet (0) or not possible (2). Without it a press is never taken: it could arm nothing.
void SetHookState(int state);

// The UI thread (gamestate's read of the hands, five times a second; nothing when off): the skills on the pad's eight face-button slots, the hero's level in the armed skill, a new hero.
void UiRead();
// hit by touch's timer: the hero's mana is read for the decisions (five times a second, on the UI thread). Nothing when off.
void Tick();

// vrcam's XInput hook, every poll of pad 0 (the pad as the layout, BodyWalk or a real pad made it - before the gestures' own pulses are added): takes the press of a skill button that arms out of
// it. `armable`: F4, hit by touch swinging a melee weapon (not a thrown one) - vrcam knows. `buttons` XInput wButtons, changed in place; `lt` the left trigger.
void FilterPad(uint16_t* buttons, uint8_t lt, bool armable);

// hit by touch (melee_touch.cpp Approve), the game's attack press of an accepted touch, just BEFORE its button goes down: decides whether this press is the armed skill or the plain attack. `melee` false: a
// thrown weapon's touch (a point-blank throw) - never the armed skill.
void OnPress(bool melee);
// [gameplay] melee_skill_armed_icon (vr/armed_icon.h): the armed skill (-1 none, or off), and whether the hero can pay a hit with it now (`ready`: the same decision OnPress makes, from the
// latest mana read). Any thread (the effect's, every frame).
int Armed(bool* ready);
// vrcam's slot-skill hook, at the cast's two reads of the attack slot when that slot holds the plain attack: the skill this touch's press is to be read as (0: as the game has it). Also
// [gameplay] attack_in_place (vr/stand_still_wire.cpp): a touch read as a skill that moves the hero (Whirlwind, Leap Attack, Charge, Dragon Flight) is not held with Stand Still.
int PressSkill();

}  // namespace meleearm
