// melee_touch.h - hit by touch: the blade of the weapon in hand, swung through a monster's body, picks the TARGET and
// the MOMENT of the game's own attack (docs/plan_melee_by_touch.md). Everything the game does about it stays the
// game's: the animation, the reach (out of reach it walks the hero up, as a click on a monster does), the rolls,
// the speed. Nothing here calls a server-side attack, replaces the game's reach or touches a roll; the module reads
// the game's units, and answers the game's own target lookups for one press.
//
// How it runs:
//   skeletons.cpp   the weapon exactly as the finished pose draws it (skel::PosedWeapon), with the stamp of the hand
//                   sample that posed it
//   NoteHands       vrcam's FreshHands, every pose: that hand sample, in the room (tracking) frame, kept by its stamp
//   Tick            vrcam's timer: pose + hand -> the swing engine (stroke_contact.h) -> the sweep -> the monsters'
//                   body capsules (read-only reads of the client's unit hash, melee_bodies.h) -> the first one the
//                   blade meets that the game's own checks call hostile and unwalled -> Approve
//   Approve         presses the game's own attack (pad A, never picking up - directinput::Action once the
//                   direct-input branch is in, else vrcam's pad hook asks PadPressed) and opens a 350 ms window
//   TargetUnit      asked by vrcam's hooks of the game's target lookups (SpellTarget, HoverUnit, the map click's
//                   ClickTarget) from inside that press: the touched client unit, looked up by its id then and there
#pragma once

#include <cstddef>
#include <cstdint>

#include "melee_bodies.h"
#include "monster_bodies.h"
#include "doll_pose.h"
#include "d2r_vr_grab.h"   // [weight] grab_bodies: the library's sways (D2rgbSway)
#include "item_grab.h"   // [weight] grab_items: the items on the ground (GroundItems)
#include "unit_scan.h"
#include "strike_force.h"   // [weight] strike_by_force: SetStrikeForce

namespace D2RL {
struct PluginContext;
}
struct D2RVR_Shared;
namespace strikedir { struct Rule; }
namespace thrust { struct Rule; }

// F4 ONLY (vr/view_gate.h): the touch, its press, its window and the Direct hit act in VR first person only; a view change ends them at once.
namespace meleetouch {

// D2RLoaderLoadPlugin.
void Init(const D2RL::PluginContext* ctx);
// [gameplay] melee_touch, from the ini (every read). Off: nothing is read, hooked or pressed.
void SetWanted(bool on);
bool Wanted();
// [hud] wrist_status (vr/wrist_status.h): the lookup hook is put in for it too, and learns the server's game by the hero's unit id `SetStatusHero` is given. ServerLookup: the server's game
// and the game's own unit lookup (the original, not the hook), false until the game is learned. For read-only reads on the game's thread.
void SetStatusWanted(bool on);
bool StatusWanted();
void SetStatusHero(uint32_t id);
bool ServerLookup(uintptr_t* game, uintptr_t* lookup);
// The server's game learned is dropped: leaving a game frees it, and the next game is another one. Called when the client's own player read
// finds no hero; the lookup hook learns the next game again. `why` goes to the log.
void ForgetServerGame(const char* why);
// The world camera up or down (vrcam's watch of it). Down (the title, the character screen, a load): the game is forgotten and none is learned
// until it is up again - a game torn down after the camera went is never learned back.
void SetInWorld(bool world);
// [debug] melee_blade_length_pct (50..150, the tip side), melee_blade_radius_cm (0 = the weapon's own, else 0.5..15), melee_blade_flip,
// melee_blade_butt_pct (0..150, the side behind the bone): the touch's blade tuned live. What is drawn ([debug] melee_boxes) is the blade
// the touch tests; the game's reach and rolls are not touched.
void SetBladeTuning(float lengthPct, float radiusCm, bool flip, float buttPct);
// [debug] melee_thrown_length_pct (50..250, the tip side) and melee_thrown_butt_pct (0..300, the side behind the bone): the same tuning for the thrown
// kinds ONLY (javelin, throwing knife and axe), on top of the general one - their blades are estimates and the general sliders move every weapon.
void SetThrownTuning(float lengthPct, float buttPct);
// [gameplay] melee_min_speed (m/s, 0.3-20, default 0.8) and melee_stroke_start_cm (cm, 2-30, default 8): the swing engine's two numbers, live
// (vr/stroke_tuning.h): a blade moving slower than the first does nothing, a movement is a stroke only after the second. One threshold for
// every weapon. Only when the touch presses the game's own attack - never its reach, rolls or the attack itself.
void SetStrokeTuning(float minSpeed, float startCm);
// [gameplay] melee_toward*, melee_head_only*, melee_amplitude*, melee_amp_* (vr/strike_direction.h): what a contact must be to count as an attack - toward the monster (the touching point's velocity
// within melee_toward_deg of the direction to the monster's axis), with the head of the weapon (maces, scepters), by a swing of at least the kind's amplitude. Only decides whether a contact
// counts as an attack: the game's attack, damage and rolls are the game's. Every rule off: every contact counts as it did. F4 only, as the whole touch.
void SetStrikeRule(const strikedir::Rule& rule);
// [weight] strike_by_force (vr/strike_force.h, live): a weapon the weight model holds in a hand is judged by what its strikes deliver into the monsters' bodies (the impulse,
// the cut), against a threshold from its own steel, instead of by its speed - the press is the game's own attack, once per strike per monster (strike_force_stuck 1: again
// while it is pushed in, at the game's attack timing). on false (or no weight model, no monster bodies): the speed rule, exactly as before.
void SetStrikeForce(const strikeforce::Settings& s);
// [debug] strike_force_debug: every refused and counted strike by force told (else the first twelve refusals and forty presses)
void SetStrikeForceDebug(bool on);
// [gameplay] melee_min_speed_<kind>, melee_stroke_start_cm_<kind>: the engine's two numbers for one weapon kind (strikedir::KindIndex), 0 = the general ones (SetStrokeTuning).
void SetKindTuning(int kind, float minSpeed, float startCm);
// [gameplay] melee_thrust, melee_thrust_min_speed, melee_thrust_cm, melee_thrust_along_pct and per kind melee_thrust_<kind>, melee_thrust_min_speed_<kind>, melee_thrust_cm_<kind>
// (vr/thrust_stroke.h): a poke along the blade's own axis, point first, counts with numbers of its own - a second engine on the same samples, for the weapons that thrust (by
// the game's weapon class, 1ht / 2ht, or the kind's own setting). Only whether a contact counts: the game's own attack is pressed as for a swing. F4 only, as the whole touch.
void SetThrustRule(const thrust::Rule& rule);
// The blade the touch sweeps for the item in a hand, as it is NOW (the weapon model's own ends - vr/weapon_extents.h at unitM metres per model unit, the pose's
// skel::PosedBlade::unitM - or, with no unit, the kind's and base item's constants, with the live tuning on top): how far the wood
// runs ahead of the weapon bone's origin (tipM, on the side tipSign says of the bone's +Y) and behind it (buttM). [weight]'s shaft takes the same ends
// from the same bone, so its line lies on this blade's axis. code: the item's code (bladeshape::Code), 0 = unknown; type: D2RVR_WeaponType.
struct BladeExtent { float tipM = 0.0f, buttM = 0.0f; int tipSign = 1; };
BladeExtent BladeFor(uint32_t code, uint32_t type, bool twoHandsOn, float unitM = 0.0f);
// [debug] melee_touch_unchecked: skip the server's hostility / wall check (to find out whether it is what stops a touch).
void SetUnchecked(bool on);
// [debug] melee_touch_no_attack: a touch that would attack presses nothing - to try the weapon's stab and the monsters' push without killing them.
void SetNoAttack(bool on);
// [debug] melee_touch_client_verdict: the client's own units answer "hostile? a wall?" even where the server is in this process
// (vr/client_verdict.h) - what a TCP/IP client gets, tried offline.
void SetClientVerdict(bool on);
// [weight] monster_bodies (vr/monster_bodies.h): the touch's scans hand over the bodies of the monsters the server calls hostile and unwalled - their limbs
// from their own skeletons (vr/monster_rig.h, every 10 ms) or the game's capsule, kMax capsules in render metres - while it or [debug] melee_boxes wants them. Bodies: the last of them, fresh (kFreshMs) and in F4 - false otherwise (off,
// another view, no scan lately: hit by touch off, no melee weapon). Any thread. Nothing is attacked or pressed for it: the bodies only shape the drawn weapon.
void SetBodiesWanted(bool on);
// [gameplay] melee_head_volume (vr/metal_head.h Volume, live): a headed weapon (an axe, a polearm, a hammer, a mace, a spear) has its head as a volume of its own where
// its model has it - an axe's blade beside the shaft - and its shaft as thin as the model's; the debug's boxes draw it. Off: one capsule along the weapon, as before.
void SetHeadVolume(bool on);
// [weight] npc_bodies (live): the bodies (Bodies, DrawBodies) also of the non-hostile units near - town NPCs, hirelings - each marked hostile = false
// (monsterbodies::Body::hostile), for trying [weight] stab_in and the monsters' push on them. They are only bodies: the touch still presses only on a
// monster the server calls hostile.
void SetNpcBodies(bool on);
// [weight] monster_shields: the monsters' shields and weapons stop the held weapon; a blow they stop presses nothing (off: transparent, as before)
void SetShieldsWanted(bool on);
// ... a BLOCKED BLOW: a blow that passed every rule a hit is judged by (the least speed, the stroke's or the thrust's, toward the monster, the head of the
// blade, the verdict) - it would have counted - but the monster's shield (held 2) or its weapon (held 1) stopped it. Once per stroke per monster; count
// grows by one each. For the shield-hit sound (the author: «это должен быть удар а не просто прикосновение ... как для удара по монстру»): a touch or a
// blade resting on the shield is none. LastBlockedBlow: the last one (false: none yet).
struct BlockedBlow {
    uint32_t id = 0;
    int side = 0;        // the hand (0 right, 1 left)
    int held = 0;        // 2 its shield, 1 its weapon
    float speed = 0.0f;  // m/s where it touched
    bool thrust = false;
    unsigned long long ms = 0;   // GetTickCount64
    unsigned count = 0;
};
bool LastBlockedBlow(BlockedBlow* out);
bool Bodies(monsterbodies::Snapshot* out);
// [weight] stab_in (cleanroom/heft/stab.h): is hand `side`'s weapon (0 right, 1 left) moving point first now - a weapon that thrusts for hit by touch thrusting
// (thrust::ThrustingNow on its thrust path), or ANY pointed blade (thrust::PointedKind: a sword too) moving point first by the stab's own judge (thrust::PointFirst) -
// fresh within the last 150 ms. What the weight model is told so a point-first strike goes into the body; what hit by touch counts as a hit is not changed. Any thread.
bool StabbingNow(int side);
// [weight] monster_ragdoll (probe, the doll library: vr/doll_lib.h, vr/doll_view.h): the bodies give way to the held weapon by their weight and spring back. PushBody: the push the weight
// model's step gave body i of the snapshot it met (weight::SetBodyPushSink; the weight thread). While on, Bodies hands the bodies over swayed - the weight
// model meets them there and [debug] melee_boxes draws them there; off, the sway is let go at once. Nothing of the game's moves.
void SetSwayWanted(bool on);
// [weight] monster_weight_pct / monster_push_pct: the monsters' weight and the weapon's push, % (live).
void SetSwayTuning(float weightPct, float pushPct);
// [debug] monster_ragdoll_view: DrawBodies adds the push's arrow and each humanoid's weight and balance (dollview::PackDebug) - while the doll is on.
void SetSwayView(bool on);
// [weight] monster_ragdoll_pose: the monster as the game draws it gives way with its doll - its limbs and lean as turns of its own bones (vr/doll_pose.h).
// MonsterPose: the turns for that SkeletonInstance now (the skeleton hook asks, skel::SetMonsterPoseSource); false - none. Humanoids only, F4 only.
void SetPoseWanted(bool on);
bool MonsterPose(uintptr_t self, dollpose::Pose* out);
// ... how many turns monster id's drawn pose has now (its limbs and its lean; 0: none fresh), -1 when the pose is off. For [weight] grab_bodies' log.
int PosedTurns(uint32_t id);
bool SwayView();
// [weight] grab_bodies (vr/grab_bodies.h, vr/grab_wire.cpp; the grab is the weight library's, vr/grab_lib.h): an empty hand's grip holds a body. SetGrabWanted: the
// bodies are made for it (as for monster_bodies) and the scan keeps the non-hostile units near too - town NPCs, hirelings - for the grab ONLY: Bodies (the weight model,
// the debug boxes) leaves them out unless [weight] npc_bodies. GrabBodies: every body of the last scan as the game poses them (no sway), fresh and in F4 - false: none.
// GrabDollOn: [weight] monster_ragdoll's doll is there to draw a held limb giving way. GrabSways: the library's sways of the held bodies put into the doll's state (its
// limbs and its lean drawn from there; let go, the doll springs them back). Nothing of the game's moves. The skeleton hook's thread.
void SetGrabWanted(bool on);
// ... GrabTick: vrcam's timer, after Tick: when the touch's tick keeps no bodies (no melee weapon swung - a bow, bare hands, hit by touch off), the same read-only scan
// of who is near and their limbs is made for the grab alone (ok: vrcam's Frame::ok; heroRender: the hero in world units, as the bow's poll has it).
void GrabTick(bool ok, const float heroRender[3], float upm);
bool GrabBodies(monsterbodies::Snapshot* out, bool swayed = false);   // (swayed: as drawn, the doll's sway in - [weight] grab_wrap lays the hand on that)
// [weight] grab_items (vr/item_grab.h): the items lying on the ground within 3 m of the hero, each at its cell on the hero's floor in render metres (read only). SetItemsWanted:
// scanned at all; ItemsTick: vrcam's timer (every 50 ms at most; heroRender as GrabTick's); GroundItems: the last scan's, *fresh when it is under 200 ms old. Any thread.
void SetItemsWanted(bool on);
void ItemsTick(bool ok, const float heroRender[3], float upm);
int GroundItems(itemgrab::Item* out, int max, bool* fresh);
bool GrabDollOn();
void GrabSways(const D2rgbSway* sways, int n);
// ... GrabLinks: the held limbs as the library holds them (each step; n 0: none). Bodies, GrabBodies(swayed) and the debug boxes put those limbs there (the doll's
// sway moves the rest) - the bodies stand as the held monster is drawn.
void GrabLinks(const D2rgbLink* links, int n);
// ... the doll's numbers the library holds a monster by (its springs, limits and the weight slider: the same as the doll's own).
void GrabDollTuning(float* limbHz, float* bodyHz, float* damping, float* maxLimbM, float* maxBodyM, float* massScale);
void PushBody(const monsterbodies::Snapshot& snap, int i, const float impulse[3], const float point[3]);
// [debug] melee_boxes: the bodies drawn (monsterbodies::Pack: a monster's limbs pink, a monster on the game's capsule blue, the capsule the weapon is on white), into
// the effect's capsules a / b (world units from where the hands hang), up to max; how many. touchedId / touchedBone: weight::LastContact's (0: none). Wanted by
// melee_boxes alone - [weight] monster_bodies need not be on. The render thread.
int DrawBodies(uint64_t nowMs, const float eyeWorld[3], uint32_t touchedId, int touchedBone, float (*a)[4], float (*b)[4], int max);
// [gameplay] melee_touch_thrown, from the ini (every read; default on): a javelin, a throwing knife or axe in hand is swung like any weapon - its
// blade (blade_shape.h) swept through a monster presses the game's own attack at it (the game's Throw: a point-blank throw, the ammunition spent as
// the game spends it; never the Direct hit route). Throwing potions never. Off: a thrown weapon does nothing by touch, as before. F4 only.
void SetThrownTouch(bool on);
bool ThrownTouch();
// [gameplay] melee_touch_thrown_held, from the ini (every read; default off): with the grip throw on, a thrown weapon is in the hand from the moment it is
// equipped, like a sword - its touch does not wait for the grip to be squeezed (the weight model's "a hand holds it" still decides). Off: as before,
// the touch counts only while the throw's grip is squeezed (vr/thrown_touch.h). The throw by gesture is the same either way.
void SetThrownHeld(bool on);
// What the touch needs of the throw by gesture (vr/throw_gesture_wire.h, [hands] throw_by_grip / throw_gesture), given once by vrcam: GripIsThrows (the grip
// gesture owns the right grip: the weapon is "in the hand" while it is squeezed), LastThrowTickMs (the touch stands down 0.7 s after a released throw) and
// HoldOffRelease(until): after a touch pressed the attack with a thrown weapon the gesture's release of that swing throws nothing until then (vr/thrown_touch.h).
using GripIsThrowsFn = bool (*)();
using GripHeldFn = bool (*)();
using LastThrowFn = uint64_t (*)();
using HoldOffFn = void (*)(uint64_t untilTickMs);
// gripHeld: throwwire::GripHeld - the right grip squeezed, as the gesture reads the controller (the shared block's grip reads 0 while the gesture owns it).
void SetThrowLink(GripIsThrowsFn gripIsThrows, GripHeldFn gripHeld, LastThrowFn lastThrowTickMs, HoldOffFn holdOff);
// ... the same two questions asked of ONE hand (0 right, 1 left): the hand that swings the weapon - with a thrown weapon in each hand ([hands] throw_left_hand)
// each grip is its own weapon's. Registered, they are asked instead of the two above.
using GripAtFn = bool (*)(int side);
void SetThrowGripAt(GripAtFn gripIsThrowsAt, GripAtFn gripHeldAt);

// [debug] bow_latency: a text about the hero's animation row (the one his unit points at: name, frames, the action frame) for the press->hit line, written into out; empty when it is not
// wanted or not readable. Given by vrcam (any thread, read only).
using AnimNoteFn = size_t (*)(char* out, size_t n);
void SetAnimNote(AnimNoteFn fn);

// [debug] melee_press_trace (vr/press_trace.h): the touch's attack press (the key route; `hand` the skeleton side, 0 right / 1 left) and the hit counter's verdict on each press
// (hit after `ms`, or lost). Given by vrcam; called on the timer's thread. Observing only.
using PressNoteFn = void (*)(uint32_t monster, int hand);
using PressOutcomeFn = void (*)(bool hit, double ms);
void SetPressTrace(PressNoteFn press, PressOutcomeFn outcome);

// vrcam's FreshHands, on the skeleton hook's thread, on every pose: the hand sample of this pose (the shared block's
// stamp and head / hand data), and the world epoch (vrcam's g_gen).
void NoteHands(const D2RVR_Shared* sh, uint32_t epoch);

struct Frame {
    bool ok = false;            // a game area, first person with the hands on the controllers, no panel open
    uint32_t weaponType = 0;    // D2RVR_WeaponType in hand
    bool twoHandsOn = false;    // held in both hands
    bool twoHanded = false;     // the weapon is two-handed (the game's own flag of the item: [gameplay] melee_direct_client_cleave)
    uint32_t epoch = 0;         // vrcam's g_gen: the world frame's generation
    uint64_t session = 0;       // gamestate::LocalPlayer(): changes with the game / the hero
    float upm = 4.0f;           // game units per metre at the hero's size
    // The weapon AS DRAWN (vr/blade_shape.h): the view the skeleton hook's bone (in the head's turn-only frame) is put in the world
    // with - where the hands hang (g_eyeWorld, world units) and the view's right and forward on the ground (g_stickFrame).
    bool headOk = false;
    float camR[3] = {}, camF[3] = {}, eyeWorld[3] = {};
    uint32_t kind[2] = {};      // D2RVR_TYPE_* of each hand's slot alone (right, left): 0 empty / shield / quiver. Two melee kinds = dual wield
    uint32_t code[2] = {};      // each hand's item code (gamestate::HandCode, bladeshape::Code): the blade is per base item where it is known
    int thrustClass[2] = {-1, -1};   // each hand's item: 1 its class in the game's data is a thrust (1ht, 2ht), 0 another, -1 not read (gamestate::HandThrust; vr/thrust_stroke.h)
    // [weight] (vr/weight.cpp, weight::Placed / CarryingHand): with weights the weapon is carried by whichever hand holds it (it goes from
    // hand to hand) and, let go, falls and lies - then it strikes nothing. Without [weight]: the weapon's slot hand, as ever (weightdrive.h).
    bool weightPlaced = false;
    int weightCarrying = -1;
    // A weapon in each hand ([weight] StepSecond, second_lets_go): the second is let go (falling, lying, flying back) - its blade is left out; the first let go
    // (weightCarrying -1) leaves out only the first's: the weapon still in a hand strikes on (the headset 2026-10-24: one dropped, the other lost its hitbox).
    bool secondLetGo = false;
    int secondHand = -1;        // ... the hand that holds the second (weight::SecondHand; the hand-over: the first's), -1 its own as ever
    int heroMode = -1;          // [weight] strike_by_force, strike_force_stuck 1: the hero's unit mode (vrcam's HeroAnimMode; -1 not known) - a repeat waits for the game's attack to end
};
// vrcam's timer (about every millisecond; returns at once when no new pose has come).
void Tick(const Frame& f);

// ---- the game's side -----------------------------------------------------------------------------------------

// The server's verdict on a monster as the touch last asked it (not older than 0.7 s): 1 hostile and unwalled, 0 refused (not hostile, or a wall
// between), -1 none (hit by touch is off, or the monster is not among the ones it asks about). Read only; any thread. For [debug] melee_boxes'
// shield line (vr/shield_smite.cpp): the shield bash itself leaves the hostility to the game.
int HostileVerdict(uint32_t id);

// The target lookups of the press in flight: a touched monster is waiting for its attack to be aimed.
bool TargetPending();

// Hit by touch is on, in first person, and swung a weapon - melee, bare hands, a throwing weapon with [gameplay] melee_touch_thrown on (never a bow or a crossbow) - posed, tracked, striking - at most
// `maxAgeMs` ago; *weaponType (optional): its D2RVR_WeaponType. Any thread. For [gameplay] melee_touch_only: the trigger need not attack while the blade does (vr/melee_trigger_wire.cpp). False when the
// touch is off, stands down (another view, a menu, no pose, the weapon let go, a throwing weapon with its touch off, a throwing potion) or never ran.
bool Live(uint64_t maxAgeMs, uint32_t* weaponType = nullptr);
// Inside one of the game's target lookups (its UI thread): the client unit of the touched monster, looked up by its id
// now, 0 for none.
// where: 1 = the cast resolver's / the hover's lookup, 2 = the map click's (the click is what carries the target to the game; the pad press of a dual cast - [hands] double_swing_by_hands - is let go there), 0 = not said.
uintptr_t TargetUnit(int where = 0);

// The press as vrcam's pad hook asks it (only while direct_input.h is not in the tree): hold pad A now.
bool PadPressed();
// The touch's attack press is down now, whichever route holds it (vrcam's pad hook: Stand Still reads such a press as the touch's - the armed melee skill - and names it so).
bool PressDown();

// Hooks on the server's unit lookup (observing only, to learn the game the hostility check runs in): tried again from
// the timer until they are in. Cheap when they are.
void Install();

// ---- a throw's auto-aim ([hands] throw_autoaim_deg, vr/throw_aim.h) -------------------------------------------------------
// The same reads as the touch, for a thrown weapon in hand (the touch stands down for those): which monsters are near the hero, and the
// server's verdict on each (hostile? no wall between?), asked ahead on the game's thread. Read-only; nothing is attacked or aimed here.
struct ThrowMonster {
    uint32_t id;
    float x, z;   // its place on the grid, in game WORLD units (twice the cell)
};
// While a throw by gesture wants it (the ini, any thread): Install also hooks the server's lookup, with melee touch off.
void SetThrowWanted(bool on);
bool ThrowWanted();
// vrcam's timer, F4 with a thrown weapon in hand: about every 100 ms reads the monsters within 48 cells of the hero's client unit (the
// aim function's hero, or 0 for none) and starts the server's hostility check on the nearest 48. Clears the list when it is not wanted.
void ThrowPoll(uintptr_t heroUnit);
// Any thread, from the game's aim hooks: the monsters of the last poll (not older than 0.5 s) that the server called hostile and
// unwalled, up to max. *polled (optional): the list is fresh at all - false is "no poll lately", not "no monster".
size_t ThrowTargets(ThrowMonster* out, size_t max, bool* polled = nullptr);
// [bow] pitch_aim (vr/bow_pitch_wire.cpp): the same poll for a bow or crossbow in hand (Install hooks the server's lookup for it too), and every monster
// of it (not older than 0.5 s) - its place, its MonStats row (rowOk: it read) and whether the server called it hostile and unwalled - up to max.
struct PolledMonster {
    uint32_t id = 0;
    float x = 0.0f, z = 0.0f;   // its place on the grid, in game WORLD units
    MonsterRow row;
    bool rowOk = false, hostile = false;
};
void SetBowWanted(bool on);
bool BowWanted();
// [gameplay] leap_by_gaze (vrcam LeapAimNow): the same poll for a Barbarian in F4 - Leap Attack goes at the hostile monster the gaze is on (ThrowTargets);
// Install hooks the server's lookup for it too.
void SetLeapWanted(bool on);
// Inside one of the game's target lookups (its UI thread): the live client monster of this id now, 0 for none (the leap's monster, handed to the game's lookups).
uintptr_t MonsterUnit(uint32_t id);
size_t PolledMonsters(PolledMonster* out, size_t max, bool* polled = nullptr);

// ---- the monsters near a point, for the shield bash (vr/unit_scan.h) --------------------------------------------------------------
// The live client monsters within reachUnits game WORLD units of the grid point (heroX, heroZ), each with its capsule (the nearest cap
// when more are there): the very scan hit by touch makes, from the client's unit hash and the game's own MonStats rows. Read-only, guarded.
// vrcam's timer thread only (the rows are kept for it).
size_t MonstersNear(float heroX, float heroZ, float reachUnits, float upm, float floorM, unitscan::Near* out, size_t cap);

// ---- [debug] throw_trace (vr/throw_trace.h): what the game does after a throw's press ------------------------------------------
// Read-only: the client's missile units (the game's type 3 row of its unit hash) - class (the missiles.txt row), mode, place in game WORLD units
// (twice the cell), and the client's own count of the frames the missile has left to live (the float the game keeps in the missile's data; -1 when
// it could not be read) - and the hero's own place. The same reads as the monsters' above; any thread.
struct MissileView {
    uint32_t id = 0, cls = 0, mode = 0, flags = 0;   // flags: the game's flag word at unit + 0x124 (bit 21: the client's "ended" mark)
    float x = 0.0f, z = 0.0f;
    float life = -1.0f;
};
size_t MissileScan(MissileView* out, size_t max);
// Is this client unit in the type-3 row of the client's unit hash - the row MissileScan (the throw trace, bow latency) walks? *seen: how many units that row held. For a
// diagnosis only (potion hide: the trace reported no potion missile while a visual of one was being hidden). Any thread; read-only, guarded.
bool MissileInHash(uintptr_t unit, unsigned* seen);
// A unit's place on the grid in game world units (its path's coordinates), false when it could not be read.
bool UnitPlace(uintptr_t unit, float* x, float* z);

// ---- [hud] merc_tint (vr/merc_tint.h): the hero's mercenary among the client's monsters -----------------------------------------
// Read-only, any thread (the reads and the MonStats call guarded): the first live monster of one of these classes in the client's
// unit hash - its address, id, class, place on the grid (game WORLD units) and its MonStats row. False when there is none.
struct HirelingView {
    uintptr_t unit = 0;
    uint32_t id = 0, cls = 0;
    float x = 0.0f, z = 0.0f;
    MonsterRow row{};
};
bool FindHireling(const uint32_t* classes, size_t count, HirelingView* out);
// That unit's place now, while it is still that monster (the same id) and alive; false otherwise.
bool HirelingPlace(uintptr_t unit, uint32_t id, float* x, float* z);

// ---- [multiplayer] pose_share (vr/pose_share.h): where the players are ----------------------------------------------------------------
// Read-only, any thread (guarded): every player unit in the client's unit hash (the type-0 row) - its id and place on the grid (game
// WORLD units, as UnitPlace). Returns how many were written.
struct PlayerSpot {
    uint32_t id = 0;
    float x = 0.0f, z = 0.0f;
};
size_t PlayersNow(PlayerSpot* out, size_t max);
// [multiplayer] pose_share_bodies: the other players near are bodies in Bodies / GrabBodies / DrawBodies as NPCs are (vr/player_bodies.h): their whole capsule and their
// own skeletons' limbs, never hostile, ids no game unit has. Live.
void SetPlayerBodies(bool on);
// [multiplayer] pose_share_bodies: our blades as the touch swept them last (the weapon bone as drawn, the model's own ends; render metres, as the bodies),
// [0] the first weapon's; none older than maxAgeMs. Only while hit by touch runs (F4, a melee weapon in hand). Any thread. Returns how many (0..2).
struct OwnBlade {
    float butt[3] = {}, tip[3] = {};
    float radius = 0.04f;
    int hand = 0;        // the controller that swings it: 0 right, 1 left
    uint32_t kind = 0;   // the weapon's kind (D2RVR_TYPE_*)
};
size_t OwnBlades(OwnBlade out[2], uint64_t maxAgeMs);

}  // namespace meleetouch
