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
#include "unit_scan.h"

namespace D2RL {
struct PluginContext;
}
struct D2RVR_Shared;
namespace strikedir { struct Rule; }

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
// [gameplay] melee_min_speed_<kind>, melee_stroke_start_cm_<kind>: the engine's two numbers for one weapon kind (strikedir::KindIndex), 0 = the general ones (SetStrokeTuning).
void SetKindTuning(int kind, float minSpeed, float startCm);
// The blade the touch sweeps for the item in a hand, as it is NOW (the kind's and base item's constants with the live tuning on top): how far the wood
// runs ahead of the weapon bone's origin (tipM, on the side tipSign says of the bone's +Y) and behind it (buttM). [weight]'s shaft takes the same ends
// from the same bone, so its line lies on this blade's axis. code: the item's code (bladeshape::Code), 0 = unknown; type: D2RVR_WeaponType.
struct BladeExtent { float tipM = 0.0f, buttM = 0.0f; int tipSign = 1; };
BladeExtent BladeFor(uint32_t code, uint32_t type, bool twoHandsOn);
// [debug] melee_touch_unchecked: skip the server's hostility / wall check (to find out whether it is what stops a touch).
void SetUnchecked(bool on);
// [gameplay] melee_touch_thrown, from the ini (every read; default on): a javelin, a throwing knife or axe in hand is swung like any weapon - its
// blade (blade_shape.h) swept through a monster presses the game's own attack at it (the game's Throw: a point-blank throw, the ammunition spent as
// the game spends it; never the Direct hit route). Throwing potions never. Off: a thrown weapon does nothing by touch, as before. F4 only.
void SetThrownTouch(bool on);
bool ThrownTouch();
// What the touch needs of the throw by gesture (vr/throw_gesture_wire.h, [hands] throw_by_grip / throw_gesture), given once by vrcam: GripIsThrows (the grip
// gesture owns the right grip: the weapon is "in the hand" while it is squeezed), LastThrowTickMs (the touch stands down 0.7 s after a released throw) and
// HoldOffRelease(until): after a touch pressed the attack with a thrown weapon the gesture's release of that swing throws nothing until then (vr/thrown_touch.h).
using GripIsThrowsFn = bool (*)();
using GripHeldFn = bool (*)();
using LastThrowFn = uint64_t (*)();
using HoldOffFn = void (*)(uint64_t untilTickMs);
// gripHeld: throwwire::GripHeld - the right grip squeezed, as the gesture reads the controller (the shared block's grip reads 0 while the gesture owns it).
void SetThrowLink(GripIsThrowsFn gripIsThrows, GripHeldFn gripHeld, LastThrowFn lastThrowTickMs, HoldOffFn holdOff);

// [debug] bow_latency: a text about the hero's animation row (the one his unit points at: name, frames, the action frame) for the press->hit line, written into out; empty when it is not
// wanted or not readable. Given by vrcam (any thread, read only).
using AnimNoteFn = size_t (*)(char* out, size_t n);
void SetAnimNote(AnimNoteFn fn);

// vrcam's FreshHands, on the skeleton hook's thread, on every pose: the hand sample of this pose (the shared block's
// stamp and head / hand data), and the world epoch (vrcam's g_gen).
void NoteHands(const D2RVR_Shared* sh, uint32_t epoch);

struct Frame {
    bool ok = false;            // a game area, first person with the hands on the controllers, no panel open
    uint32_t weaponType = 0;    // D2RVR_WeaponType in hand
    bool twoHandsOn = false;    // held in both hands
    uint32_t epoch = 0;         // vrcam's g_gen: the world frame's generation
    uint64_t session = 0;       // gamestate::LocalPlayer(): changes with the game / the hero
    float upm = 4.0f;           // game units per metre at the hero's size
    // The weapon AS DRAWN (vr/blade_shape.h): the view the skeleton hook's bone (in the head's turn-only frame) is put in the world
    // with - where the hands hang (g_eyeWorld, world units) and the view's right and forward on the ground (g_stickFrame).
    bool headOk = false;
    float camR[3] = {}, camF[3] = {}, eyeWorld[3] = {};
    uint32_t kind[2] = {};      // D2RVR_TYPE_* of each hand's slot alone (right, left): 0 empty / shield / quiver. Two melee kinds = dual wield
    uint32_t code[2] = {};      // each hand's item code (gamestate::HandCode, bladeshape::Code): the blade is per base item where it is known
    // [weight] (vr/weight.cpp, weight::Placed / CarryingHand): with weights the weapon is carried by whichever hand holds it (it goes from
    // hand to hand) and, let go, falls and lies - then it strikes nothing. Without [weight]: the weapon's slot hand, as ever (weightdrive.h).
    bool weightPlaced = false;
    int weightCarrying = -1;
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

}  // namespace meleetouch
