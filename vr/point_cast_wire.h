// point_cast_wire.h - [gameplay] teleport_circle, the game's side (point_cast_wire.cpp): which pad slots hold a skill cast onto a point (the list: [gameplay]
// point_skills), the press of one taken out of the pad the game polls while it is held, the arc and circle kept for the drawing, and on the release the slot's
// own button pressed once. The decisions are vr/point_cast.h's; the arc's start is vrcam's (the staff's tip as drawn, else the hand); the cast's point is
// vrcam's spell resolver (with leap_by_gaze's answers), which asks CastPoint.
//
// F4 ONLY (vr/view_gate.h). Nothing here casts past the game: the press is the slot's own button in the game's own poll; mana, cooldown and checks are the game's.
#pragma once

#include <cstdint>

#include "point_cast.h"

namespace D2RL {
struct PluginContext;
}

namespace pointwire {

void Init(const D2RL::PluginContext* ctx);
// [gameplay] teleport_circle (the box), point_skills (the list, ids; empty or missing = pointcast::kDefaultSkills), leap_arc (Leap and Leap Attack join it).
// Off: the pad is never touched, CastPoint is false - the skills cast as before.
void Configure(bool on, const char* skills, bool leapArc);
bool On();
// The skill is cast onto a point by the arc and the circle (the box on and the skill on the list, or a Leap with leap_arc).
bool IsPoint(int skill);

// The UI thread (gamestate's read of the hands, 5 times a second; nothing when off): the skill on each of the game's twelve controller skill slots, from the
// skill panel's own read of the table it binds through (wheelgame::Latest; vrcam keeps that read going while this is on: WantsSlots) - before its first read
// the controller tables as double swing reads them (all but RT and LT + RT).
void UiRead();
bool WantsSlots();

// What vrcam knows at the game's pad poll.
struct PollIn {
    bool allowed = false;     // a press may be ours now: VR F4, the box, in the world, no menu or panel
};
// The arc for the press under way (vrcam builds it every poll while one is up, from the staff or the hand): set before FilterPad.
// arcFor(skill, &arc): false = no arc can be made now (no pose of the hand, a line straight up or down).
using ArcFn = bool (*)(int skill, pointcast::Arc* out);
// vrcam's XInput hook, pad 0, after melee_skill_armed's filter and AFTER the gestures' pulses - the right trigger's RT slot ([openxr] skill_rt_a) and the skill
// panel's picks are pulses, and a point skill's press there is taken too; the release's own pulse is told apart (it starts nothing): a listed skill's press is
// taken out of `buttons` (or `rt`, the right trigger, set to 0) while held; with the circle up the arc follows the hand (arcFor). The release presses the slot's
// own control once.
void FilterPad(uint16_t* buttons, uint8_t lt, uint8_t* rt, const PollIn& in, ArcFn arcFor);

// The arc and circle drawn now (world units; the circle against the hero's place); false when none is up.
bool Shown(pointcast::Arc* out);
// The render side, every present Shown was true: whether the effect took the arc (its PaOn) - or not (an older effect: the capsules stand in). Counted
// for the release's log line, which says how many presents drew the arc while it was held.
void NoteDrawn(bool byEffect);
// For the cast's resolver: the circle's point of a release of `skill` made within maxAgeMs - true, *out - or false (a tap, nothing released lately: the gaze's).
bool CastPoint(int skill, uint64_t nowMs, pointcast::Offset* out, uint64_t maxAgeMs = 800);
// The game's map click has asked for the cast's target: the release's point is spent.
void CastDone();

}  // namespace pointwire
