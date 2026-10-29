// bow_draw_wire.h - the two-hand bow draw, wired to the plugin's input (bow_draw_wire.cpp).
// The detector is bow_draw.h; this listens to direct_input.h's frames (native OpenXR, with the plugin's own controls
// or with BodyWalk's) and presses the game's own attack, or the pad button of a skill, at the release.
#pragma once

#include <D2RLPlugin/api.h>

#include "bow_touch.h"
#include "direct_input.h"

namespace bowdrawwire {

// vrcam's AimHand(): the hand that holds the bow now, 0 right, 1 left (as D2RVR_Shared).
using HandFn = int (*)();

// vrcam's FullBody(): VR first person, F4. The bow draw and everything that goes with it - the draw itself, the string hand's
// pose, the bow's rig pose, the touch point read off the model, the debug vectors - works there and only there ("direct
// movements work on F4 and all we do; the other views stay as they were"). Leaving F4 mid-draw ends the draw with no shot and
// lets the control go to the layout again.
using ViewFn = bool (*)();

// From D2RLoaderLoadPlugin: the log, the frame listener, who holds the bow, and whether this is F4 now.
void Init(const D2RL::PluginContext* ctx, HandFn bowHand, ViewFn f4);

// The settings, from LoadSettings (any thread; applied at the next frame):
//   on      [hands] bow_draw (the tick box; unticked = nothing changes)
//   button  [hands] bow_draw_button (what the release presses: 0 the game's attack, 1..4 the pad's B X Y RB, 5..8 the same with LT held)
//   start   [hands] bow_draw_start (what takes the string: 0 the trigger, 1 the grip, 2 either)
//   model   [hands] bow_draw_nock (1: the touch point is the string of the posed bow when it reads sanely; 0: always 16 cm behind the grip)
//   debug   [debug] bow_vectors (the vectors in the headset and a 5 Hz log line with the numbers)
//   reachCm [hands] bow_draw_reach_cm (the radius of the zone round the touch point the free hand has to be in to take the string;
//           5..50, 28 = as it was)
//   minPullCm [hands] bow_draw_min_pull_cm (the least pull for a release to shoot: the string hand pulled back this far from where
//           it took the string, 5..40, 12 = as it always was; the hands must also be 16 cm + this far apart - 28 cm at 12)
//   drawOnly [hands] bow_draw_only (a press away from the string shoots nothing while a bow is wielded in F4: in the grab zone the
//           string hand's trigger is the draw's whole - nothing bound to it fires - and away from it the trigger's and the grip's
//           shots (attack, pad A, RT) are dropped while everything else they are bound to works; false = the plain trigger as before.
//           The ini's default is 1, this signature's false so that a caller that does not say keeps the old behaviour)
//   nockShiftCm [hands] bow_draw_nock_shift_cm (the zone's centre moved along the bow's axis, + away from the grip, - toward it;
//           -25..+25, 0 = as it was; for the bow's own string and the fixed point alike)
void Configure(bool on, int button, int start, bool model, bool debug, float reachCm = 28.0f, float nockShiftCm = 0.0f, float minPullCm = 12.0f,
               bool drawOnly = false);
bool On();

// [bow] arrow_rest_aims (any thread; live): the shot's line is the one from the string hand through the arrow's rest point on the bow hand
// (skel::BowRestPoint, a point fixed in the hand's own frame: [bow] arrow_rest_x/y/z_cm) - the line the arrow lies on - instead of through the
// bow hand's reference point. Without a rest point (the offsets are 0, no rig pose) nothing changes either way. Default true.
void SetRestAims(bool on);
// [bow] shot_line_hold (any thread; live): a released shot's line stays the aim the game's hooks get (AimDir, AimLine, AimLive) for bowdraw::kShotHoldSeconds,
// through a new draw too - the game reads a press when it takes it, late when the hero is still in his last attack; then the bow hand's own ray (HandRay)
// aimed the arrow, not the line it was drawn along. Off (the default): 0.4 s, and not while a new draw is held, as before.
void SetShotLineHold(bool on);

// The bow's own string is wanted off the posed bow (skel::BowString): the draw is on and the touch point is the model's, or the
// debug view shows it. For the skeleton hook.
bool WantsModelString();

// The string hand's TRIGGER is the draw's now (nocked with the trigger, or a draw that ended with no shot and the trigger not
// yet let go): the attack that trigger would otherwise start is held back. False when the box is unticked and a quarter of a
// second after the last frame. Any thread.
bool Owned();

// A shot's aim is live: from the release for 0.4 s, the arrow goes along the draw line (the game's own attack and
// skill resolve their point through vrcam's aim hooks, which ask for the line here). False when the box is unticked.
bool AimLive();
// The line in the head's turn frame (x right, y up, z back - what D2RVR_Shared's hands are in), unit; false when it
// is not live. Turned with the head, so it stays the same line in the room.
bool AimDir(float out[3]);
// [bow] pitch_aim: the same line whole - where it starts (the arrow's release point at the release: the bow model's arrow rest point when it was read,
// else the bow hand's own point; metres from the head in the head's turn frame, the hands' frame) and its unit direction (AimDir's). False when not live.
bool AimLine(float start[3], float dir[3]);
// The string is held now (taken, not yet let go or cancelled). False when the box is unticked; stale after a quarter of a second. Any thread.
bool Drawing();

// [debug] bow_latency: the releases that pressed the game's button so far, and the times (std::chrono::steady_clock seconds, the base of
// throwborn::NowS()) of the last one's release and of its button's press by the wire. Any thread.
unsigned ShotCount();
void ShotTimes(double* releaseS, double* pressS);

// The string hand's TRIGGER (direct_input's hand: 0 left, 1 right) is kept from the layout by the bow draw this very moment - the
// draw's own, or "a press away from the string shoots nothing" with a bow wielded in F4 - so nothing else may turn it into a shot
// (the skill wheel's cast on the trigger asks). False when the box is unticked or in any other view; stale after a quarter of a second.
bool KeepsTrigger(int hand);

// The string hand's GRIP is the draw's now (vrcam's hand: 0 right, 1 left): the belt's potions must not take it from the hip.
bool HoldsGrip(int hand);

// A recentre (F11, BodyWalk's, a view change): a draw under way ends with no shot.
void NoteRecenter();

// What the draw knows each frame, for what rides on it (the quiver, vr/bow_quiver_wire.cpp). The hook is called on the draw thread, in the draw's own frame listener, after
// the detector stepped - every frame the draw listens, also with `active` false (the box is unticked, or any view but F4: the receiver lets go of what it holds).
struct DrawInfo {
    bool active = false;      // the bow draw is on and this is F4
    bool allowed = false;     // the headset has the focus, no menu or panel is up, a bow is in hand
    bool drawing = false;     // the string is taken
    int bowHand = 0;          // direct_input's hand that holds the bow (0 left, 1 right)
    int button = 0;           // [hands] bow_draw_button: what the release presses (0 the attack, 1..4 B X Y RB, 5..8 with LT)
    bool haveArrow = false;   // `rest` and `stringHand` are placed (while the string is taken and the head is tracked)
    bowtouch::Vec rest;       // where the arrow rests on the bow hand (the model's rest point when read, else the hand's own), the hands' frame
    bowtouch::Vec stringHand; // the string hand, the same frame
};
using DrawHook = void (*)(const DrawInfo& s, const directinput::Frame& f);
void SetDrawHook(DrawHook fn);

// Holds the draw's start back: asked every frame, on the draw thread, before the detector steps - true while the free hand is at something that is not the string (an orb on the bow:
// vr/bow_quiver_wire.cpp). The string hand's trigger and grip then read as let go to the detector, so the string is not taken there; a draw already taken is not touched.
using StartBlockFn = bool (*)();
void SetStartBlock(StartBlockFn fn);

// Called on the draw thread at every release that shoots, just BEFORE the game's attack button is pressed for it (vr/bow_quiver_wire.cpp: [bow] quiver_plain_no_mana decides there whether
// this release is the plain attack).
using ReleaseFn = void (*)();
void SetReleaseHook(ReleaseFn fn);

// [debug] bow_vectors: the last frame's vectors in the hands' frame (metres from the head, the head's turn frame: what
// D2RVR_Shared's hands are in), for the effect that draws them in the headset. False when the switch is off or the frames
// stopped (a quarter of a second).
bool DebugVectors(bowtouch::VectorSet* out);
// The box is ticked and [debug] bow_vectors is on: the effect that draws them is wanted.
bool DebugWanted();

// [debug] bow_aim_ray: where the arrow goes, as a long ray (the effect draws it: green when the line goes through the bow model's arrow rest point, orange when it is the plain hand-set line).
// SetAimRay is the switch (live); AimRayWanted is true when it is on and the draw is on in F4; AimRayLine gives the last frame's line in the hands' frame (metres from the head, y up): the
// arrow's release point and the unit direction the shot goes along (the detector's aim), while the string is drawn and for the 0.4 s after a shot; false otherwise or a quarter of a second
// after the frames stop. The effect clips it at the floor (bowtouch::MakeAimRay).
void SetAimRay(bool on);
bool AimRayWanted();
bool AimRayLine(bowtouch::Vec* start, bowtouch::Vec* dir, bool* fromModel);

}  // namespace bowdrawwire
