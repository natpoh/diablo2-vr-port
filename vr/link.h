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
void PublishXr(const D2RVR_XrInput& in, bool ended);
// BodyWalk's VR gesture zones to draw now (up to max); 0 when none came for half a second.
uint32_t VrZones(BW_LinkVrZone* out, uint32_t max);
// BodyWalk's gesture runes to draw now (up to max): the pictures to hang and the hand's trails (BW_LinkVrRune, the shared block's
// own layout; vr/rune_geom.h reads it). Nothing outside VR first person (F4), with the plugin as the input (direct_input), when the
// block is not BodyWalk's version 1, holds none or has not been written for half a second.
uint32_t VrRunes(BW_LinkVrRune* out, uint32_t max);

}  // namespace link
