// link.h - D2R VR's end of BodyWalk Game Link (bodywalk/game_link.h).
//
// vrcam tells BodyWalk who the game is and what it offers (the Manifest: the
// "D2R: ..." and "D2R key: ..." actions, the Mapping tabs) and what it wants now
// (GameState: the tabs for the view and the weapon, FlatVR's Head Lock and
// screen distance - the logic that lived in the D2R Bridge plugin before). It
// reads the head and the hands (Tracking) and the actions held (Actions), and
// asks BodyWalk for FlatVR things (Requests). Native OpenXR: the game's own
// headset to BodyWalk (XrInput), BodyWalk's gesture zones back (VrZones).
#pragma once

#include <cstdint>

#include "d2r_vr_shared.h"
#include "gesture_cmds.h"
#include "skill_link_abi.h"
#include "d2r_vr_state.h"

namespace link {

// Before anything else: the log, d2r_vr.ini ([screen] menu_distance_m) and our
// version for the Manifest, which goes out here.
void Init(void (*log)(const char*), const wchar_t* iniPath, const char* modVersion);

// The head and the hands from BodyWalk's Tracking, as vrcam reads them; null
// until BodyWalk has sent any. A new copy each time BodyWalk sends one - a
// pointer stays good for a good while (a ring of copies), never for ever.
const D2RVR_Shared* Head();

// A consistent copy of BodyWalk's Tracking block as it is now (vr/flat_raw.h makes the FlatVR controllers from it); false when there is none.
bool RawTracking(BW_LinkTracking* out);

// Presses seen so far, one per reader: a press shorter than the reader's
// polling still reads as held, for kTapMs from when this reader saw it.
constexpr unsigned kTapMs = 50;
struct Presses {
    uint32_t manifest = 0;
    uint32_t seen[BW_LINK_MAX_ACTIONS] = {};
    unsigned long long until[BW_LINK_MAX_ACTIONS] = {};   // GetTickCount64: held for a press until then
};
// The D2RVRAction bits held; 0 while BodyWalk's Actions are not of our Manifest.
uint32_t ActionsHeld(Presses& p);
// The game's commands held ("D2R key: ..."): bit n of out = the game's command n.
void CommandsHeld(uint64_t out[2], Presses& p);

// GameState from the game's state, whenever it is published (at least 5 times a
// second while the game runs): BodyWalk takes 3 s of silence for the game gone.
void PublishState(const D2RVR_State& s);
// [openxr] menu_3d on FlatVR (vrcam, from its timer): a menu goes to FlatVR as a panel of its own, so
// FlatVR's screen stays where it is and Head Lock on - the world in stereo round it - as with no menu.
void SetMenuPanel(bool on);

// Requests: FlatVR started or stopped, its 3D source (BW_LINK_STEREO_*), and who
// holds the headset (BodyWalk's Mapping follows: the game's own frames, or FlatVR's).
void RequestFlatVR(bool on);
void RequestStereoSource(int source);
void RequestPresenter(bool game);

// BodyWalk with Game Link is running; caps: BW_LINK_CAP_* of that BodyWalk.
bool HostAlive(uint32_t* caps = nullptr);

// BodyWalk's walk when its Output is "Game Link" (the Walk block): the stick its virtual pad would have had, -1..1, x right, y
// forward. False (both 0): its Output is something else - the walk is on its pad report then - or the block has not been written
// for half a second. vrcam's BodyWalkReport puts it on the report's left stick (vr/walk_path.h WithLinkWalk).
bool Walk(float* x, float* y);

// Native OpenXR: this headset frame's head and controllers to BodyWalk; ended =
// the session is over (the last word: BodyWalk's Mapping goes back to FlatVR).
// `raw`: the same frame's controls before any gesture of ours took one (null: as `in`) - BodyWalk recording a gesture is given those (its pen is the
// trigger the skill panel holds for the take).
void PublishXr(const D2RVR_XrInput& in, bool ended, const D2RVR_XrInput* raw = nullptr);
// BodyWalk's VR gesture zones to draw now (up to max); 0 when none came for half a second.
uint32_t VrZones(BW_LinkVrZone* out, uint32_t max);
// BodyWalk's gesture runes to draw now (up to max): the pictures to hang and the hand's trails (BW_LinkVrRune, the shared block's
// own layout; vr/rune_geom.h reads it). Nothing outside VR first person (F4), with the plugin as the input (direct_input), when the
// block is not BodyWalk's version 1, holds none or has not been written for half a second.
uint32_t VrRunes(BW_LinkVrRune* out, uint32_t max);

// [gestures] skill_link (vr/skill_link_wire.cpp): the hero's skills to BodyWalk (the Skills block, whole), and BodyWalk's Casts back - a
// consistent copy; false when no BodyWalk writes it (none running, one too old for skills, or the block still for 2 s).
void PublishSkills(const BW_LinkSkills& s);
// skill_link on: with the plugin as the input ([openxr] direct_input) BodyWalk is still given the head and hands (controls zeroed, as for
// [openxr] direct_bodywalk_walk, but the triggers' values kept: a trigger held is its glyph pen) and its runes are drawn - it recognises
// the gestures by them; recording: the controls too, its trigger starts and ends the take.
void SetSkillLink(bool on, bool recording);
bool Casts(BW_LinkCasts* out);

// [openxr] gesture_overlay (vr/gesture_overlay.h, the skill panel's Gestures tab): BodyWalk's Gestures block - a consistent copy; false when no
// BodyWalk writes it (none running, one without the block, or the block still for 2 s). One reader: the skill panel's frame.
bool Gestures(BW_LinkGestures* out);
// A command to BodyWalk's gestures into the GestureCmds ring (the whole block written: the entry and lastSequence together). Its sequence, 0 when
// the block cannot be had. The first one of a game continues after the block's last and BodyWalk's cmdDone (`cmdDone`: the Gestures copy's).
uint32_t PushGestureCmd(const gesturecmd::Cmd& c, uint32_t cmdDone);
// The GestureCmds block written once with no command (our ring as it stands, seeded as PushGestureCmd's first push is) - done as soon as BodyWalk's
// Gestures block is there, before any command: BodyWalk's reader takes what it finds at its first look as old, and a block that first appears
// holding our first command would lose that command. Once per game (later calls do nothing); false when the block cannot be had.
bool PrimeGestureCmds(uint32_t cmdDone);
// BodyWalk records for the Gestures tab (or is about to): with the plugin as the input it is given the controls too (its trigger is the pen), as
// for a recording it started itself (SetSkillLink's `recording`).
void SetGestureRecording(bool on);
// The skill link is up: a BodyWalk writes Casts (it takes the game's skills) - skill_link_wire's last look (SetSkillLink's `on`).
bool SkillLinkUp();

}  // namespace link
