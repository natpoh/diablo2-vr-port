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

// The settings load: [gestures] skill_link, and the spare slot (wheel numbering 0..11, -1 = only skills already on a slot are cast).
void Configure(bool on, int spareSlot);
bool On();

// vrcam's timer. play = a game area, first person (F4), no menu.
void Tick(bool play);

// A record wish from inside the game (the skill's id; -1 stops a recording): BodyWalk records a gesture for that skill.
void RequestRecord(int32_t skill);
// How BodyWalk's recording stands (BW_LINK_REC_*) and for which skill; NONE without one.
uint32_t RecordState(int32_t* skill);

}  // namespace skilllinkwire
