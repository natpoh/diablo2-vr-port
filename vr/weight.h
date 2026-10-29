// weight.h - a held weapon with weight in first person ([weight], docs/plan_weapon_weight.md step 1b):
// Heft's hands (cleanroom/heft/hold.h) in the game. A hand takes the weapon where its grip closes on
// the shaft and holds it there, the weapon goes from hand to hand, and it is heavy: it lags behind a
// hard swing, a two-handed one wants both hands, a spear held by its end is slower than held where the
// game holds it. The skeleton hook (skeletons.cpp) calls it on every pose of the hero, before his arms
// are posed; nothing of the game's own changes - the weapon is only drawn where the hands have it.
#pragma once

#include <cstdint>

#include "monster_bodies.h"
#include "skeletons.h"
#include "strike_measure.h"   // [weight] strike_by_force: LiveStrikes

namespace stabdbg {
struct Note;   // vr/stab_debug.h
}

namespace weight {

struct Out {
    bool on = false;           // this pose's weapon is held by the weight model
    int slot = 0;              // the hand slot the game has it in (0 right, 1 left)
    int main = 0;              // the hand that carries it (the other may hold it too)
    bool asPlaced = false;     // the slot's fist holds it where the game's grip puts it, alone
    bool holds[2] = {};
    float on01[2] = {};        // how far each hand is drawn on the weapon (easing on and off)
    uint32_t grabs[2] = {};    // bumped each time that hand takes it
    // No hand holds it (let go: falling, lying on the floor, flying back into the game's hand): drawn where
    // the weight model has it - its body's frame (the fist that held it first) in BodyWalk's frame.
    bool free = false;
    float body[3] = {}, bodyRot[4] = {0, 0, 0, 1};
    // [weight] after_drop 1 or 2: it has landed, lain for the delay and is now away (free is true too): the skeleton draws it slung on the back
    // (1, vr/weapon_back.h) or not at all (2), and tells where its place on the back is with SetBackBody - the weapon flies from there when taken.
    // midM: metres from the weapon bone's origin to the middle of the shaft, along its tip.
    bool stowed = false;
    // The hand stays empty while the game throws (vr/weight_throw.h EmptyHand): the weapon the weight model holds is NOT drawn - the skeleton squeezes its
    // attach bone to a point - until the game's throw animation is over (then the game's own next one is in the hand, and ours is drawn again).
    bool hidden = false;
    float midM = 0.0f;
    // [weight] grip_model 1 and a grip library loaded: the hands are held the library's way. Each hand's grip mode (0 none, 1 tight, 2 loose) and its turn about the shaft against the
    // grip it took it in (rad); the shaft's wood as the weight model has it, in metres along the weapon bone's tip axis from the bone's origin (up to three cylinders and the ends).
    bool gripLib = false;
    int gripMode[2] = {};
    float gripRoll[2] = {};
    int nPieces = 0;
    float pieceT0[3] = {}, pieceT1[3] = {}, pieceR[3] = {};
    float shaftButtT = 0.0f, shaftTipT = 0.0f;
    // A weapon in each hand (StepSecond): the other slot's weapon, a body of its own. [weight] second_lets_go: let go (falling, lying, flying back) it is drawn where
    // the weight model has it - free2, its body's frame (the fist that held it) in BodyWalk's frame, as free / body are for the first.
    bool second = false;
    int slot2 = 1;
    bool free2 = false;
    float body2[3] = {}, body2Rot[4] = {0, 0, 0, 1};
    // ... [weight] after_drop 1 or 2: it has lain for the delay and is away on the back (free2 too) - drawn slung behind ITS shoulder (the left weapon behind the
    // left one), its place told with SetBackBody2; midM2: metres from its body's origin (the fist that held it) to the middle of its shaft
    bool stowed2 = false;
    float midM2 = 0.0f;
    int hand2 = -1;   // the hand that holds it (its own, or after the hand-over the first's: the skeleton lays it there as the game's own grip has a weapon in that hand)
    // [hands] throw_left_hand: the game threw the second weapon - its hand stays empty while the game throws, as `hidden` for the first: NOT drawn (its attach bone a point),
    // wherever its body is (let go at the release, it does not fall to the floor beside the game's own missile)
    bool hidden2 = false;
};

// Steps the weapon once per new head sample and moves the controllers of the hands on it to where the
// weapon has them (in->hand, in->rot): the arms are then posed onto it.
void Apply(skel::Input* in, Out* out);
// The shaft as the slot's fist holds it where the game puts it, off the pose just drawn: in that hand's
// controller frame (as Apply handed it over), metres - where it passes the palm, and toward the tip.
void SetShaft(const float at[3], const float dir[3]);
// The shaft as the weapon is DRAWN, measured by the skeleton hook against the weight model's body frame (vr/weapon_shaft.h) from the same attach bone the
// hit-by-touch blade is built from: at (m, the point of the line nearest the body's origin) and dir (unit, toward the tip) in the body's frame; butt and tip
// the wood's ends in metres along dir from `at` (the blade's extents: bladeshape::ForItem, tuned); boneAlong the bone's origin along dir from `at`. The
// weight model's grips, ends and lever follow it (Hold::SetDrawnShaft). Called every pose while the weapon is in a hand; supersedes SetShaft.
void SetDrawnShaft(const float at[3], const float dir[3], float butt, float tip, float boneAlong);
// ... the same for the second weapon of a pair (Input::Weight::second), against ITS body's frame (Out::body2): measured by the skeleton while it is in its own hand.
void SetDrawnShaft2(const float at[3], const float dir[3], float butt, float tip, float boneAlong);
// [weight] dual_collide with [gameplay] melee_head_volume: the head's own volume as DRAWN (vr/metal_head.h Volume where the drawn bone puts it - an axe's blade beside
// the shaft), a capsule from a to b, radius m, in the same body frame as SetDrawnShaft's (SetDrawnHead2: the second weapon's, as SetDrawnShaft2). The other weapon of a
// pair stops on it and it stops on the other (Hold::SetHead). radius <= 0 (or null ends): none - a blade, wood, an item with no row, the option off.
void SetDrawnHead(const float a[3], const float b[3], float radius);
void SetDrawnHead2(const float a[3], const float b[3], float radius);
// ... a head of several capsules (vr/weapon_head_parts.h: an axe's blade a thin slab where its mesh is): n of them, each 7 floats (a, b, radius), at most
// kHeadParts; n 0: none. The other weapon of the pair stops on each.
constexpr int kHeadParts = 12;
void SetDrawnHeadParts(const float* parts, int n);
void SetDrawnHeadParts2(const float* parts, int n);
// [weight] grip_fit (vr/grip_fit.h): what the skeleton hook did to a hand on the weapon, told on every pose while it is fitted: the weight model logs it once for each
// take (the log is where the next test sees why a hand did not sit on the shaft). taker: the hand that took the weapon (the fit is snapped and aligned), not the game's slot
// hand (corrections only). settled: the hand is fully on the weapon. offAxisCm / alignDeg: how far the drawn palm's middle was off the shaft's axis and its knuckle line off the
// shaft BEFORE the fit; missCm: how far the palm ended from where the fit wanted it (the arm's reach). tune: the corrections applied (x y z cm, roll tilt tilt degrees).
struct GripFitNote {
    int side = 0;
    bool taker = false, settled = false, snapped = false, aligned = false;
    bool likeBase = false;   // the hand sits on the shaft as the game's own grip has the base hand (like[] is that relation)
    uint32_t grabs = 0;
    float offAxisCm = 0.0f, alignDeg = 0.0f, missCm = 0.0f;
    float tune[6] = {};      // the user's corrections for that hand
    float like[6] = {};      // the base grip's relation: across, up in cm (x is 0), then the lean about the shaft / across / up in degrees
};
void NoteGripFit(const GripFitNote& note);
// A line for the log from the skeleton hook (vrcam: weight - <text>): the game's grip taken, a weapon matrix that had come out shrunk and was set right.
void Say(const char* text);
// The skeleton hook could not measure the drawn shaft this pose (the blade's extents are not known, the bone does not measure): told on every such pose, logged once for each
// reason - the model's line then stands where it was, which is not the drawn weapon's. SetDrawnShaft clears it.
void NoteShaftUnmeasured(const char* why);
// [weight] to_back: where the skeleton drew the stowed weapon's body frame on the back, in the frame of Out::body (metres from the head, the
// head's turn only), told on every pose while Out::stowed: the weapon flies from there when a hand takes it or calls it back.
void SetBackBody(const float body[3], const float rot[4]);
// ... the same for the second weapon of a weapon in each hand (Out::stowed2): where it hangs on the back, so it flies from there when its hand draws it.
void SetBackBody2(const float body[3], const float rot[4]);
// [weight] strike_by_force (vr/strike_force.h): the strikes on now of the weapon(s) the weight model holds - each monster its own, as [debug] strike_measure
// measures them on the contact - with the game's slot of the weapon (0 right, 1 left), its hold kind and its steel. Fresh (the last step within 100 ms) only.
// Any thread. Returns how many (up to cap).
struct LiveStrike {
    strikemeasure::Strike s;
    int slot = 0;
    int kind = 0;
    float steelKg = 0.0f;
};
int LiveStrikes(LiveStrike* out, int cap);
// ... and the slots whose weapon the weight model holds in a hand now - those hit by touch judges by force (bit 0 the right slot, bit 1 the left).
unsigned ForceSlots();
// [weight] take_haptic: a hand enters the take zone (behind a shoulder) while a weapon is away - the pulse that tells the hand where to grab.
// vrcam registers the sink (side 0 right, 1 left; amplitude 0..1; ms); unregistered, nothing is pulsed.
using HapticFn = void (*)(int side, float amplitude, int durationMs);
void SetHaptic(HapticFn fn);
// [weight] clash_sound: the two weapons of a pair struck each other ([weight] dual_collide) - how fast they met (m/s), the game's volume for it (0..255,
// vr/clash_loud.h) and which knocked (for the log). vrcam registers the game's block sound (vr/clash_sound.h); unregistered, the knock is only felt.
using ClashSoundFn = void (*)(float speedMps, uint32_t gameVolume, const char* who);
void SetClashSound(ClashSoundFn fn);
// [weight] cleave (cleanroom/heft/hold.h Settings::cleave): a strong strike goes on through a body into the next. The numbers are times the weapon's own strike
// threshold (vr/strike_force.h, by its family and steel - the strike_force_* numbers, whether strike_by_force is on or not): over - the arrival a body must
// take to be cut through; cost - what each body gone through spends of the stroke's force (its first body's arrival); minSpeed - the blade's speed into the
// body at least, m/s. Only blades cleave (a staff or a maul stops).
// vrcam's read of the ini (live). Off: every body stops the weapon, as before.
struct CleaveSettings {
    bool on = false;
    float over = 2.0f, cost = 2.0f, minSpeed = 4.0f;
    float general = 1.0f, blunt = 1.5f, edged = 1.1f, piercing = 1.5f, massExp = 0.5f, floorNs = 1.0f;   // strike_force_general_pct / 100, _blunt, _edged, _piercing, _mass_exp, _floor_ns
};
void SetCleave(const CleaveSettings& c);
// [weight] thrown_model / thrown_fly_ms: the hero's animation mode (Diablo II's unit mode: 1 neutral, 7 and 8 the attacks, 10 cast, 11 the throw), -1 if not
// known - it tells when the game has TAKEN a throw (the hero goes into an action mode: the thrown model vanishes at once, the game's own missile flies) and,
// with thrown_model 1, when its animation is over (vr/weight_throw.h ThrownFly). Unregistered: a thrown model ends when it lands or thrown_fly_ms is up.
using HeroModeFn = int (*)();
void SetHeroModeSource(HeroModeFn fn);
// [weight] hands_model 1: loads the weight-physics library if it is not loaded yet (vrcam registers weightlib::Ensure; true = a library is in use). Unregistered: there is none, ours.
using LibraryFn = bool (*)();
void SetLibrarySource(LibraryFn ensure);
// [weight] grip_model 1: the same for the grip library (vrcam registers griplib::Ensure).
void SetGripLibrarySource(LibraryFn ensure);
// [weight] monster_bodies (vr/monster_bodies.h, cleanroom/heft/contact.h): the hostile monsters near the hero as bodies - the held weapon is put back on their
// surface and its motion into them taken away (it stops on them, an edge glances off along them), the controllers go on; a pulse in the hands that hold it
// when it strikes one. The source is the hit by touch's scan (vrcam registers meleetouch::Bodies); unregistered, or Input::Weight::monsterBodies off: none.
using BodiesFn = bool (*)(monsterbodies::Snapshot* out);
void SetBodiesSource(BodiesFn fn);
// [weight] stab_in (cleanroom/heft/stab.h): whether hand `side`'s weapon thrusts and is thrusting now - hit by touch's own rule and path (vrcam registers
// meleetouch::StabbingNow). Only then does a point-first strike go into the body; unregistered, or Input::Weight::stabIn off: it stops at the surface.
using StabFn = bool (*)(int side);
void SetStabSource(StabFn fn);
// [debug] stab_debug (vr/stab_debug.h): the stab's word of every step that has one (a strike judged, in, out), its points in the render world - vrcam keeps the
// last for the readout and the marks. Called on the weight model's thread, only with Input::Weight::stabDebug.
using StabNoteFn = void (*)(const stabdbg::Note& note);
void SetStabNoteSink(StabNoteFn fn);
// [weight] monster_ragdoll (probe, the doll library vr/doll_lib.h): each step the weapon rests on or strikes a body, the push it gave that body - body i of the snapshot
// the step met (render world): impulse N s into the body, at point (metres). vrcam registers meleetouch::PushBody; Input::Weight::monsterRagdoll off: never called.
using BodyPushFn = void (*)(const monsterbodies::Snapshot& snap, int i, const float impulse[3], const float point[3]);
void SetBodyPushSink(BodyPushFn fn);
// What the held weapon met on the last step, for the log and the tests: bodies 0 when nothing.
struct ContactNote {
    int bodies = 0;            // how many bodies it touched
    uint32_t id = 0;           // the game's unit id of the one it met hardest
    int bone = -1;             // ... the bone of its skeleton whose limb it met (-1: the game's capsule of it)
    float approachMs = 0.0f;   // m/s into it
    float depthCm = 0.0f;      // how deep it was before it was put back
    float slideMs = 0.0f;      // m/s along the surface after the friction
    float costUs = 0.0f;       // what the bodies cost that step (taken into the room and met), microseconds
};
bool LastContact(ContactNote* out);   // true when it touched a body on the last step
// [weight] hero_strength (vr/hero_strength.h): what the hero's strength did to the weapon last time the model stepped, for the log and the tests. on: the box is ticked and the
// Strength was read (steel and hands are then the factors the model took); reqFromGame: the requirement is the game's own, not a typical one for the kind.
struct StrengthNote {
    bool on = false, reqFromGame = false;
    int str = -1, req = 0;
    float classPct = 100.0f, effectPct = 100.0f, ratio = 1.0f, steel = 1.0f, hands = 1.0f;
};
bool LastStrength(StrengthNote* out);   // true when the box is ticked and the Strength was read
// [weight] thrown_handoff_ms: the game's own animation has passed its ACTION frame - the throw's release point, where its own missile is born. The thrown
// model flies out of the hand along the throw and hands over to that missile there (vanishes; the hand stays empty), or at the fixed handoff time, whichever
// comes first. Unregistered (the default: the unit's animation frame is not read - its layout is not verified), the fixed time alone hands over.
using ActionFrameFn = bool (*)();
void SetActionFrameSource(ActionFrameFn fn);
// A weapon is stowed on the back - or was taken from it a moment ago (so the swap gesture, which reads the same grip press from the input
// frames, does not also swap the weapon set: vr/shoulder_grip.h). The swap gesture asks it through shoulderwire::SetStowedSource.
bool BackStowed();
// A hand on the weapon now (vrcam's belt: it takes no bottle).
bool HoldsWeapon(int side);
// The hand that carries it now (0 right, 1 left); -1 when none does (no weapon held so, or let go).
int CarryingHand();
// The hands that hold the weapon or that it flies into, a bit each (1 right, 2 left); 0 when none (let go, falling, lying). The shield keeps to the other forearm ([hands] shoulder_draw,
// vr/shield_arm.h): whenever this is the shield's hand alone, the shield jumps.
unsigned WieldingHands();
// A weapon is held that way ([weight] on, a melee weapon in hand) - in a hand or not.
bool Placed();
// [weight] knock_props (vr/knock_props.h): the held weapon's steel, kg, as the weight model has it on its last step; 0 when no weapon is held that way.
float SteelKg();
// A weapon in each hand, and the second one is let go now (falling, lying, flying back into its hand): it strikes nothing, and hit by touch leaves its blade out
// (the first weapon's let go is CarryingHand -1: then only the first's blade is left out). Any thread.
bool SecondLetGo();
// ... the hand that holds the second weapon now (0 right, 1 left): its own (the other slot's) - or, after the HAND-OVER (the first went over to that hand), the first's;
// -1 none (let go, not a weapon in each hand). Hit by touch swings the second's blade with this hand's controller. Any thread.
int SecondHand();
// [hands] throw_left_hand - by the game's SLOT (0 right, 1 left): the hands on that slot's weapon now, a bit each (1 right, 2 left; 0: none, or the
// weight model does not have it); the slot of the weight model's first weapon (-1 none); a weapon in each hand, the second a body of its own. Any thread.
unsigned HoldersOf(int slot);
int FirstSlot();
bool SecondPlaced();
// [hands] throw_left_hand: the game's throw took the SECOND weapon (a javelin in the other hand): from now its model is not drawn (Out::hidden2) - the hand is empty,
// as the first's is - and once the game's throw animation is over (or [weight] thrown_hand_empty_ms) a fresh one is in its own hand. Any thread.
void SecondThrown();
// [weight] monster_shields: was a held weapon on this monster's shield (2) or a weapon it holds (1) - and not on its body - within the last withinMs? 0: not.
// Hit by touch presses nothing on a monster the weapon met only so: the blow never reached it (the game's own block roll is for the blows that do).
int Blocked(uint32_t id, uint64_t withinMs);
// [gameplay] melee_not_butt: does this hand (0 right, 1 left) hold the weight model's weapon, and where on its shaft - *aheadM metres ahead (toward the tip)
// of the weapon bone's origin, the game's own grip (negative: behind it, toward the butt)? False: it does not hold it, or the shaft is not measured.
bool HandOnShaft(int side, float* aheadM);
// ... and was it on its BODY (a limb, its trunk) within the last withinMs: the blow reached it.
bool Reached(uint32_t id, uint64_t withinMs);
// [weight] parry_feel (vr/parry.h): the held weapon knocked aside by a monster's blow - the hands that hold it are drawn `metres` along `dirRender` (a unit
// direction in the render world, the bodies' frame) at once and eased back over `ms` (parry::KnockShare); the weapon follows them as it follows the hands.
// Only how the weapon is drawn: nothing of the game's moves. Any thread; a new knock replaces the one going on.
void Knock(const float dirRender[3], float metres, int ms);
// Where a grip pressed now by that hand (0 right, 1 left) would take the weapon, from the last step (any thread): 0 nowhere, 1 the shaft (the second hand on the held weapon, or a weapon on the floor / in
// the air within [weight] grab_cm of the fist), 2 the weapon on the back (behind a shoulder, or within back_take_cm of its drawn shaft). *cm: the fist's distance to that shaft in centimetres (1e6 when
// none). 3 = the weapon is away (on the back, or on the floor / in the air) and this hand's grip would CALL IT BACK from afar - the game's hand, and on the back any hand with recall_any_hand: the
// hand is not at the weapon. Read by the belt potions (vr/grab_priority.h): a hand in the weapon's zone takes the weapon, not a potion.
int TakeZone(int side, float* cm);
// What the AIM of a staff is taken from (vr/aim_source.h): the controllers, never the weapon as it is drawn. holds: that hand holds the weapon now; hand: the
// one that carries it (-1 none: let go, falling, lying, on the back); shaft: the shaft toward the tip in THAT hand's own (controller's) frame, as it grips it - fixed by
// the grip, so the weapon's lag, sag and slide do not move it. True when a hand carries the weapon and the model stepped lately.
struct AimInfo {
    bool holds[2] = {};
    int hand = -1;
    float shaft[3] = {0.0f, 0.0f, -1.0f};
};
bool Aim(AimInfo* out);
// [debug] the shaft's two ends in BodyWalk's frame (metres from the head, the head's turn only), where the
// weight model has them (the hands' offset in, as they are drawn); false when no weapon is held so lately.
bool ShaftEnds(float butt[3], float tip[3]);
using LogFn = void (*)(const char*);
void SetLog(LogFn fn);

// [hands] throw_by_grip (the throw gesture, vr/throw_gesture_wire.h): a grip release that is a THROW does not drop a thrown weapon (a javelin,
// a throwing knife or axe in the right hand) - the game's own throw takes it out of the hand and the weapon sits in the hand again as a fresh
// one (vr/weight_throw.h). The throw gesture registers two functions, once: gripIsThrows - it owns the right grip now (a thrown weapon wielded,
// throw_by_grip on); lastThrowTickMs - GetTickCount64 of its last released throw. Not registered (null), the default: no throw ever, every
// release is a drop, as for any weapon.
// The throw as the weight model works it out: where the weapon would land (preview, while it is in the hand) and where it lands (from the grip release on).
// Worked out HERE, from what the model knows - the weapon's mass, the velocity the hand really gave it (inertia and lag included: a heavy weapon leaves
// slower than a light one for the same swing), the release moment, the real ground - not from the hand's raw velocity.
//   - It is updated on EVERY step while a thrown weapon (a javelin, a throwing weapon) is held (preview = true: what a release now would do; tickMs is the
//     step's, at most one frame old) and FROZEN at the grip release (preview = false, releaseTickMs set): the throw gesture decides at the release, before
//     the model has seen the grip open, so read it when your throw fires - the outcome is there, no wait. The gesture classifies the release "thrown"
//     within ~150 ms after it (weight_throw.h); an outcome older than 10 s is not offered.
//   - Frame: the HEAD frame at the release, as the controllers come (x right, y up, z back, metres, the head's turn only; the origin is the head). The
//     consumer turns it into the world with the camera's axes of that moment and the world's scale (units per metre), as it does the hand's ray.
//   - velocity: m/s, in that frame: the weapon's PEAK velocity of the last 0.15 s before the release (a swing's end-zone slowing does not shorten the throw),
//     times massFactor (the weapon's weight against the arm's push: (2.5 + 0.5) / (2.5 + kg) - a javelin 0.97, a sword 0.81, a two-handed axe 0.54, a polearm
//     0.50, a throwing knife 1.07: the model's own muscles make a heavy weapon lag but then overshoot, so the weight is applied here: light and fast goes
//     far, heavy goes short), its HORIZONTAL part scaled by [weight] throw_reach_pct (800 %: a real arm throws a few metres, a game screen wants tens; the vertical part and so the
//     flight time stay real), slowed if needed to land within [weight] throw_max_cells (the game's own command check refuses an aim point more than 50
//     cells off the hero on an axis - never bypassed; a cell is two world units, so the cap is cells * 2 / upm metres). rawVelocity: the peak, unscaled.
//   - landing: metres in that frame where the flight first touches the ground (the ground under the hero, y = the floor's height in that frame): the FINAL
//     point, already scaled and capped - a consumer must not scale it again. flightS: seconds. elevationDeg: velocity over the horizontal, + up.
//     speed: |velocity| m/s. reachScale: the horizontal factor applied (reach, reduced by the cap). capped: the cap held it.
//   - tickMs: GetTickCount64 of the step it was worked out on; releaseTickMs: when the grip was released (0 while preview). massKg: the steel's.
//   - valid false: nothing yet, or what the model could not work out (a velocity that is a tracking jump, no ground, a weapon that is not a thrown kind) - fall
//     back to your own estimate. [weight] off, F4 only: nothing is ever valid.
// Read from any thread.
struct ThrowOutcome {
    bool valid = false;
    bool preview = false;
    uint64_t tickMs = 0, releaseTickMs = 0;
    float velocity[3] = {}, rawVelocity[3] = {};
    float speed = 0.0f;
    float landing[3] = {};
    float flightS = 0.0f;
    float elevationDeg = 0.0f;
    float reachScale = 1.0f;
    float massFactor = 1.0f;
    bool capped = false;
    float massKg = 0.0f;
};
bool LastThrow(ThrowOutcome* out);   // true and the last outcome when it is valid; false otherwise (out, if given, still gets what there is)

using GripIsThrowsFn = bool (*)();
using LastThrowFn = uint64_t (*)();
void SetThrowSource(GripIsThrowsFn gripIsThrows, LastThrowFn lastThrowTickMs);
// [hands] throw_left_hand: which hand's grip the throw gesture owns (0 right, 1 left) - its release is the throw's. Unset: the right, as ever.
using ThrowSideFn = int (*)();
void SetThrowSide(ThrowSideFn side);

}  // namespace weight
