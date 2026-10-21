// bow_quiver_wire.h - [bow] quiver, wired to the plugin (bow_quiver_wire.cpp): a bow skill is ARMED from the quiver - with a bow, a button of the string hand while the string is
// drawn, or an orb on the bow's stave taken with the free hand's trigger ([bow] quiver_orbs) - as our own state, never written into the game's slots; each release of the string with
// a bow or a crossbow in hand is then read by the game as that skill through vrcam's slot-skill hook (ShotSkill), as the armed melee skill is. The arrow shows which one in its colour.
// The logic is bow_quiver.h, bow_orbs.h and quiver_input.h; the draw is bow_draw_wire.cpp's hook.
#pragma once

#include <D2RLPlugin/api.h>

#include "bow_draw_wire.h"
#include "bow_touch.h"

namespace bowquiverwire {

// From D2RLoaderLoadPlugin, after bowdrawwire::Init (the draw's listener must be first): the log, the draw's per-frame hook, the frame listener, and vrcam's AimHand() - the hand
// that holds the bow or crossbow now, 0 right, 1 left.
void Init(const D2RL::PluginContext* ctx, bowdrawwire::HandFn weaponHand);

// The settings, from LoadSettings (any thread; applied at the next frame):
//   on    [bow] quiver (the tick box; unticked = the draw is as it was)
//   orbs  [bow] quiver_orbs: a row of energy orbs along the bow's stave, one for every bow skill the hero has and one for the plain shot; the free hand's trigger on one arms that skill
//         (the armed one's orb again, or the plain shot's, disarms)
//   orbTrigger [bow] quiver_orb_trigger: the trigger picks (the default); off: the grip, as the orbs first were. At an orb the other control does nothing
//   next  [direct] quiver_next: the control that arms the next bow skill while the string is drawn (onto the plain shot: disarmed) (left_x left_y left_stick left_menu right_a right_b right_stick right_menu, or
//         string_primary / string_secondary / string_stick, bow_primary / bow_secondary / bow_stick for either handedness; "none" = off)
//   prev  [direct] quiver_prev: the one that arms the previous
void Configure(bool on, bool orbs, bool orbTrigger, const char* next, const char* prev);

// The quiver wants the wheel's reads of the hero's skills and slots kept going (on, F4, a bow or a crossbow in hand; or the old quiver's arrow skill to take off the A slot - Cleanup):
// for vrcam's tick, which keeps wheelgame::Tick running when the wheel is off.
bool WantsSlots();
// The string is taken now: the reads go at the quick cadence.
bool Drawing();

// [bow] quiver_orbs, the sliders (any thread, live; centimetres, as the ini has them): the orb's radius, the grab radius round its middle, where the row starts on the stave (from the
// grip), the spacing, the shift of the row along the bow's own axes (x across it, y along the stave, z toward the archer), and the row's turn about those axes (degrees, pivoting on its
// first orb). Limits: boworbs::Tuning::Clamped.
void SetOrbTuning(float sizeCm, float reachCm, float startCm, float spacingCm, float xCm, float yCm, float zCm, float turnXDeg, float turnYDeg, float turnZDeg);

// [bow] quiver_orbs: the posed bow's stave is wanted from the skeleton hook (skel::Input::bowStave): the quiver and the orbs are on.
bool WantsOrbs();
// The orbs, in the hands' frame (metres from the head, the head's turn), for the effect: the middle, the radius, the colour, whether the slot holds the skill, how near the hand is.
// False when there is nothing to draw now: the tick off, no bow in hand, not F4, no stave read for half a second, no skill learned. NOT the draw, the release, a menu or a hand near them.
struct OrbDraw {
    int n = 0;
    float pos[12][3] = {};
    float radius[12] = {};
    float rgb[12][3] = {};
    bool current[12] = {};     // the armed skill (the plain shot's orb when none)
    float hover[12] = {};      // 0..1: how near the free hand is (1: it is at this orb, the one a grip takes)
};
bool OrbsWanted();
bool Orbs(OrbDraw* out);
// The free hand is at an orb this very moment (or a press that took one is still down): the draw must not take the string there (bowdrawwire::SetStartBlock). Stale after a quarter of a second.
bool BlocksDraw();

// [bow] arrow_fx, live (any thread): the arrow burns, freezes or crackles by the skill that is armed (vr/bow_quiver.h Element, the effect's ARROWFX block) - nocked at rest from the moment
// it is chosen, and through the draw; size and brightness in percent (100 = the defaults: a flame 4.5 cm round the head). Off: the coloured glow while the string is drawn, as it was. A
// skill with no element of its own keeps the glow either way.
// alongCm ([bow] arrow_fx_along_cm, -20..60): the effect's centre moved along the arrow's shaft, forward toward the arrowhead (0 = where the head is found).
void SetArrowFx(bool on, float sizePct, float brightPct, float alongCm = 0.0f);
// [bow] arrow_fire_cut, live (any thread): the arrow's effect goes out at the release that shoots (OnRelease), or `ms` ([bow] arrow_fire_cut_ms, 0..2000) after it, and lights again on
// the next arrow - a new draw, the game showing an arrow again after the shot, another skill (vr/arrow_fire_cut.h). Visual only: the game's missile is the game's. Off: the effect stays on
// the arrow in the hand until the game hides it, as it was.
void SetArrowFireCut(bool on, float ms);
// What the effect draws for it, in the hands' frame (metres from the head, the head's turn): the effect's kind (AfOn: 1 fire, 2 frost, 3 arcane), the arrow's nock end and head, the size
// (the flame's radius, metres) and the strength. False when there is nothing to draw now (off, no element - the plain shot included - a menu, a moment after a shot).
struct ArrowFxDraw {
    int kind = 0;
    float tail[3] = {};
    float head[3] = {};
    float size = 0.045f;
    float strength = 1.0f;
};
// The bow's own arrow is wanted (skel Input::bowArrow): the quiver and the effect on, an effect file that has it.
bool WantsArrowFx();
bool ArrowFxWanted();
bool ArrowFx(ArrowFxDraw* out);
// vrcam found D2R_DepthFog.fx without the ARROWFX uniforms (an older effect): the coloured glow stands in, and the log says so once.
void ArrowFxEffectMissing();

// [bow] quiver_plain_no_mana, live (any thread): with an arrow skill armed and too little mana to pay it - by the game's own numbers (vr/bow_mana.h) - the release is the game's plain
// attack instead of the game's refusal ("not enough mana", nothing shot); the skill stays armed, so with the mana back the next release is the skill again. Off: the release is the armed
// skill all the same, and the game refuses it.
void SetPlainNoMana(bool on);
// The quiver is on: vrcam's slot-skill hook is wanted (the release is read as the armed skill through it).
bool WantsHook();
// [bow] arrow_fx_needs_mana, live (any thread): with an arrow skill armed that the hero cannot pay - the game's own numbers, the same as the release's - the arrow looks plain: the effect (and the
// coloured glow) goes out, eased, and lights again when the mana is back. At rest and while drawn. Off: the effect stays whatever the mana.
void SetArrowFxNeedsMana(bool on);
// [bow] quiver_icon, live (any thread; default on): the armed arrow skill's icon on the bottom of the MANA medallion (vr/armed_icon.h - the melee armed skill's icon, the same place and sliders).
void SetIcon(bool on);
// The armed bow skill for that icon (any thread: the effect's, every frame): true with `skill` and whether the hero can pay a shot with it (`ready`: the release's own decision, vr/bow_quiver.h
// DecideShot) while the quiver is on, F4, a bow or crossbow in hand and a bow skill armed; false (skill -1) for nothing armed, no bow (the armed skill does nothing then), another view, the tick off.
bool ChosenIcon(int* skill, bool* ready);
// vrcam says whether the slot-skill hook is in (it is installed from a signature check that another build may fail): without it a release is the game's plain shot.
void SetHookReady(bool ready);
// vrcam's slot-skill hook, at the cast's two reads of the attack slot when that slot holds the plain attack and a bow or a crossbow is in hand: the skill this release is to be read as
// (0: as the game has it). Set by OnRelease for that release only (meleearm::InFlight's window): one release, one cast.
int ShotSkill();
// The draw's release hook (bowdrawwire::SetReleaseHook), just before the release's button goes down: decides whether this release is the armed skill or the plain shot.
void OnRelease();

// The glow is wanted by the effect: on, the string is drawn and a bow skill is armed.
bool GlowWanted();
// Adds the glow (the arrow's head and trail, in the armed skill's colour) to the effect's picture; false when there is none or the picture is full.
bool AddGlow(bowtouch::VectorSet* vs);

}  // namespace bowquiverwire
