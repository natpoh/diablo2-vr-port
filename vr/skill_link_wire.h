#pragma once
// [gestures] skill_link: the hero's skills to BodyWalk and the casts back (vr/skill_link.h is the pure part, this the wiring). Every ~10 ms
// from vrcam's timer: the skill panel's reads of the game kept going (wheelgame, the game's UI thread), the Skills block written when the
// list changes and once a second, BodyWalk's new casts read and carried out - the skill's pad slot pressed (directinput::PadPulse, the
// game's own pad poll), or the skill put on [gestures] skill_link_slot through the game's own setter first, then that slot pressed.
// F4 ONLY for the casts (vr/view_gate.h): a cast that comes in another view, in a menu or with no hero is dropped (logged). The list is
// written in every view - it is data, BodyWalk shows it. Unticked: nothing is read, written or pressed.

#include <cstdint>

namespace D2RL {
struct PluginContext;
}

namespace skilllinkwire {

// Once, from the plugin's start: the log.
void Init(const D2RL::PluginContext* ctx);

// The settings load: [gestures] skill_link, the spare slot (wheel numbering 0..11, -1 = only skills already on a slot are cast), and
// [gestures] skill_link_settle (default on): a skill put on the spare slot is pressed skilllink::kSettleMs after the game's table shows it
// there (a margin: pressed at once, the game may refuse it), and every such press is watched turning into a cast.
void Configure(bool on, int spareSlot, bool settle = true);
bool On();

// vrcam's timer. play = a game area, first person (F4), no menu; heroMode = the hero's unit mode now (vrcam's HeroAnimMode, -1 not
// known): a press on the spare slot is watched turning into a cast (logged; pressed once more when it does not).
void Tick(bool play, int heroMode = -1);

// A record wish from inside the game (the skill's id; -1 stops a recording): BodyWalk records a gesture for that skill.
void RequestRecord(int32_t skill);
// How BodyWalk's recording stands (BW_LINK_REC_*) and for which skill; NONE without one.
uint32_t RecordState(int32_t* skill);

// [gestures] engine_in_game (vr/gesture_engine_wire.cpp): the gesture engine in the game needs the hero's class and skills and casts through the same
// caster - so the tick reads the game and keeps its list while the engine is on, whether or not the skills go to BodyWalk (skill_link).
void SetEngine(bool on);
// The list as last made (the Skills block's layout: setKey, setName, the skills) and its revision; false while there is none yet. Any thread.
bool LatestSkills(BW_LinkSkills* out, uint32_t* revision);
// The engine recognised a gesture for this skill: cast the game's own way (its slot pressed, or put on the spare slot first), F4 / play only, on the
// next tick - the same rules as BodyWalk's casts. Any thread.
void CastFromEngine(int32_t skill);
// A "select" gesture recognised (Serg, 2026-11-08: "draw the glyph, the spell is chosen, and every pull of the trigger strikes with it until another
// glyph"): the skill is put on the RIGHT TRIGGER's own slot by the game's setter (the skill panel's SKILL EDIT path) - the A slot, or RT with
// [openxr] skill_rt_a - and nothing is cast: the trigger then casts it the game's normal way, every press or hold. F4 / play only, next tick. Any thread.
// `hand` (0 left, 1 right) is the hand that drew it (Serg: "the trigger of the hand that drew the glyph"): the right's slot RT ([gestures]
// trigger_right Glyphs, or [openxr] skill_rt_a) else A; the left's LT + RT (the game's second layer - its Glyphs trigger presses it).
void ArmFromEngine(int32_t skill, int hand = 1);
// The skill a select gesture put on this hand's trigger (-1 none) and when (GetTickCount64): the orb's armed icon. Any thread.
int32_t ArmedSkill(int hand, uint64_t* at = nullptr);
// [gestures] select_icon: the armed skills' icons in the medallions (the right trigger's in the mana one, the left's in the life one). Default on.
void SetSelectIcon(bool on);
bool SelectIcon();

}  // namespace skilllinkwire
