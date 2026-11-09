// knock_down.h - [weight] knock_down: a blow, a running hit, a sweep at the legs or a shove of the held weapon knocks a humanoid off its feet - as drawn;
// a lighter blow staggers it; [weight] grab_throw: the hands take a humanoid, lift it and throw it (docs/notes/knock_down.md). The author, 2026-10-31:
// «мы должны захватить кости куклы и наложить свою анимацию падения потом свою анимацию вставания поверх анимации игры»; «или шаг назад а потом он должен
// вернуться на свое место после того как встал»; «я хочу варваром поднять нпс руками и отбросить». F4 only, both off by default.
//
// The weight model's pushes come in (meleetouch::PushBody, its thread): vr/knock_trigger.h judges them by the speed they give the body. A knock-down or a
// stagger goes to the knock-down library (vr/fall_lib.h, the private weight library's D2rf* exports) with the body's joints as the game draws it now (the
// humanoid's map, monsterhumanoid::MapJoints); the grab (vr/grab_wire.cpp) hands it a body its hand took (TakeOver), the palm each step, the let-go. The
// library makes the fall, the lying, the getting up, the stagger, the hold and the throw - and the walk back to where the game has the unit (its real place,
// never moved): the drawn body always comes home. Each tick of the touch's timer its joints become a pose for the skinning (dollpose::Pose::knock, laid by
// vr/knock_pose.h on a copy of the game's own pose - headshadow's composite, skel::DollSkin). The unit stands, walks, talks and fights as the game has it:
// nothing is sent, nothing of the game's is written. A body the game makes attack while it lies gets up at once and quicker; one the game kills goes back
// to the game's own death; one that is gone eases back. [weight] death_ragdoll (vr/death_ragdoll.h): a humanoid the game kills falls as a ragdoll instead -
// from the pose it died in, by the blow that killed it - and is drawn lying as it fell for as long as the game keeps its corpse.
#pragma once

#include <cstdint>
#include <string>

#include "doll_pose.h"

namespace skel { struct MonsterRig; }
namespace monsterbodies { struct Snapshot; }

namespace knockdown {

using LogFn = void (*)(const char*);
void SetLog(LogFn fn);

struct Settings {
    bool on = false;            // [weight] knock_down (and the bodies: [weight] monster_bodies)
    bool throwOn = false;       // [weight] grab_throw (and [weight] grab_bodies)
    bool impaleOn = false;      // [weight] impale_drag (and [weight] stab_in)
    float needCms = 80.0f;      // [weight] knock_down_cms: the speed a blow's push must give the body, cm/s (its force)
    float minSwingMs = 5.0f;    // [weight] knock_down_min_speed: the weapon's own speed into the body a blow needs, m/s
    float staggerPct = 45.0f;   // [weight] knock_down_stagger_pct: a blow with this share of both staggers (0: never)
    float lieMs = 1500.0f;      // [weight] knock_down_lie_ms: how long it lies before it gets up
    float massScale = 1.0f;     // [weight] monster_weight_pct / 100
    bool balance = true;        // [weight] grab_balance: held standing and pulled, it steps after the pull (only a pull its legs cannot catch takes it off its feet)
    float strength = 1.0f;      // [weight] strength / 100: the hands that hold a body (each at most D2RF_HAND_MAX_FORCE x it - a body's weight shows in them)
    bool deathFlatOn = false;   // [weight] death_ragdoll_flat: the same in VR F1-F3 and the flat game, along the arc of the game's own swing (skel::WeaponTrail)
    bool deathOn = false;       // [weight] death_ragdoll: a humanoid the game kills falls as our ragdoll and lies as it fell (vr/death_ragdoll.h; and the bodies)
    bool leapOn = false;        // [weight] leap_landing_scatter: the humanoids round a Leap's landing thrown apart (vr/leap_scatter.h; and the bodies)
};
void Configure(const Settings& s);
// [weight] leap_landing_scatter: the hero's model just landed from a Leap or a Leap Attack at `at` (world units; leapscatter::Landing on his matrix's lift over
// the camera's ground - vr/body/hero_facing.cpp, the pose's thread); way: the leap's way across the ground (world x, z; 0, 0 not known). The timer throws the
// humanoids round it one cell outward (F4).
void OnLeapLanded(const float at[3], float peakUnits, const float way[2]);
// [shapeshift] wolf_strength_pct / bear_strength_pct (vr/form_strength.h): the hands' strength times this while the hero is a grafted werewolf / werebear (1 a person) -
// the hold and the throw of a lifted body (the library's strength is Settings::strength x it). Any thread; cheap when it does not change.
void SetFormStrength(float factor);
bool Wanted();        // either of them on
bool ThrowWanted();
bool DeathWanted();   // [weight] death_ragdoll (F4) / death_ragdoll_flat (the other views): the touch's scan lists the dying and the dead (Tick's dead)
bool DeathFlatWanted();   // [weight] death_ragdoll_flat: meleetouch::DeathTick keeps the bodies outside F4 for it

// The weight model's thread: a push it gave body id (N s, render world; point in it); swingMs the weapon's own speed into the body, heftKg the steel and the arm
// behind it (vr/knock_trigger.h: a blow needs both a fast swing and a hard push).
void NotePush(uint32_t id, const float impulse[3], const float point[3], float swingMs, float heftKg);
// The hero's run (eased, a jump no run: knocktrigger::HeroRun) along `way` across the ground, m/s (0 running away, knock-down off or no way). Any thread.
float RunAlong(const float way[3]);
// [weight] impale_drag (Serg: «если я с разгону врезаюсь с копьём в монстра, я должен его насадить на копьё и протащить»): the weight model's stab went into
// body id with the weapon in hand `side` (weight::SetStabEnterSink; its thread). Fast enough - the point's speed and the hero's run along the blade - the
// knock-down library takes the body at the blade's tip and it goes with the blade as drawn (dragged, its feet on the ground or off them); it slides off when
// the hero stops, pulls the blade back or tips it down (then falls, gets up and walks home). Impaled: is body id on a blade now (the weapon's own bodies leave
// it out - the blade is in it).
void OnStabEntered(uint32_t id, int side, float speed);
bool Impaled(uint32_t id);
// Is body id drawn out of the game's pose now (the doll's sway leaves it alone)?
bool IsDown(uint32_t id);
// Where the units of the bodies drawn out of the game's pose last stood (x, z: their rigs' roots, world units), up to max: the scans keep them in reach
// (vr/scan_reach.h) - a body whose unit leaves the scans is let go of, its drawing back at the unit far off (0.196.0: «монстры исчезают»). How many.
int FarPlaces(float (*out)[2], int max);

// The grab's (vr/grab_wire.cpp, the pose's thread): hand `side` (0 right, 1 left) took body id at `point`, its palm at `palm` (render world) - the
// library holds it from now on; false: not a body it can (not a humanoid read lately, the library absent or full). HandAt each step while the grip holds;
// LetGo when it opens (the body flies on as the hand left it). HeldPart: the part held as drawn now (the hand is laid on it). Holding: the body a hand holds (0).
// weaponA / weaponB (may be null): the hand closed on what the body holds (its spear: that capsule's ends) - the body is held by the point fixed to the hand
// that holds it (the nearer wrist as drawn), a lever. Taken while the other hand holds it: both hands have it (a re-grip, a two-hand hold).
bool TakeOver(int side, uint32_t id, const float point[3], const float palm[3], const float* weaponA = nullptr, const float* weaponB = nullptr);
void HandAt(int side, const float palm[3]);
void LetGo(int side);
bool HeldPart(int side, float a[3], float b[3], float* r);
uint32_t Holding(int side);

// [multiplayer] pose_share_grab (vr/grab_share.h): what hand `side` holds for the others to see - the body, its limb (a D2RF_* joint: the weapon's wrist for a body
// taken by what it holds, else the limb nearest the point taken, grabshare::NearestLimb) and the point taken in that limb's frame (grabshare::ToLimb, limb
// lengths), all as the body was drawn when the hand took it; the palm now (render world). False: it holds nothing (or no limb frame).
struct Shared { uint32_t id = 0; int joint = -1; float local[3] = {}; float palm[3] = {}; };
bool SharedHold(int side, Shared* out);
// ... and the others' holds, here: another player's (holder, his hand `side`) took body id by limb `joint` at `local` (as above) - our library takes OUR drawing of
// it there (the point from our own drawing of that limb) by its hand slot `side` (the other when that one is ours on that body), held at `palm` (render world,
// where we draw his hand) each tick by RemoteHandAt, and let go by RemoteLetGo: the next step moves the palm on by `vel` (m/s, render world; may be null) and lets
// go - the body flies on as his hand left it. F4, with knock_down or grab_throw on and the library there; the body must be one the touch read lately (a
// humanoid near us): false otherwise, *why says (may be null). Visual only: a body only another player holds is never moved by our knockbacks.
bool RemoteTake(uint32_t holder, int side, uint32_t id, int joint, const float local[3], const float palm[3], const char** why = nullptr);
void RemoteHandAt(uint32_t holder, int side, const float palm[3]);
void RemoteLetGo(uint32_t holder, int side, const float palm[3], const float vel[3]);
int RemoteHolds();   // how many bodies the others' hands hold here now
// ... and our blows (the author: «если один игрок толкнул или кинул монстра, другие тоже должны это видеть»): each knock-down, stagger or shove our weapon made
// (and a leap's landing) lately - the body, the limb nearest the point it met and that point in the limb's frame (as SharedHold), the speed it gave the body
// (render m/s: its impulse over its weight), its kind (D2RF_BLOW .. D2RF_STAGGER); each given once, none older than 0.3 s. How many.
struct SharedKnock { uint32_t id = 0; int joint = -1; float local[3] = {}; float vel[3] = {}; int kind = 0; };
int TakeSharedKnocks(SharedKnock* out, int max);
// Another player's (striker) knock of body id here: our library knocks OUR drawing of it down (or staggers it) at the same limb and point, with the same
// speed (render m/s, our world) times what it weighs here - on the next tick, as our own blows go. Not for a body down or held here already. Visual only:
// never sent again, no knockback of ours for it. False: not taken, *why says (may be null).
bool RemoteKnock(uint32_t striker, uint32_t id, int joint, const float local[3], const float vel[3], int kind, const char** why = nullptr);

// The world-matrix hook's writer (headshadow::SetAttachWriter -> potion_hide's hook; the renderer's thread): what hangs on a body drawn out of the game's pose -
// its weapon, its shield - goes with the bone it hangs on as drawn.
void OnXform(uint32_t entity, uintptr_t component, uintptr_t out);
// ModelDrawCollect, just before the game's own (headshadow::SetBeforeDraw; the render workers): an entity of a body drawn away from its place has its bounds
// grown again here - after whatever made them again this frame, before the pass decides on it (Serg, 0.192.39: «когда беру за руку нпс, он по-прежнему
// мерцает»: the game made each entity's box again about every frame, and the hook's and the timer's growth came before that in about 40 % of them).
void BeforeDraw(uint32_t entity, bool eyes);
// [weight] death_ragdoll (skel::SetFrozenPoseSource; the render workers): a kept corpse's bones as they came to rest - kept in the world, given in the model's
// frame for the model standing at m3 / root now (n bones). False: none kept for that skeleton.
bool FrozenBonesOf(uintptr_t self, const float m3[9], const float root[3], float (*mats)[16], int n);
// [weight] death_ragdoll: the kept corpses' own table - the ONE source of a kept corpse's drawn pose (0.199.5, Serg / locaine: «трупы дёргаются» while a
// weapon is held). Made by Tick for every kept corpse (its frozen bones, weight 1, never faded, eased or crowded out of the moving bodies' 16), a corpse not
// refreshed one tick keeps its last entry (up to kKeptFreshMs); emptied when the corpse is released. Any thread (the render workers ask per skinning).
// KeptPosesForSkin: the kept corpses of this skeleton (exact: one whose instance's own world pose this is - then it alone); IsKept: self is one of them.
bool KeptPose(uintptr_t self, dollpose::Pose* out);
int KeptPosesForSkin(uintptr_t skeleton, uintptr_t worldPose, dollpose::Pose* out, int max, bool* exact);
bool IsKept(uintptr_t self);
// ... who made the bodies this Tick is called for (the log's count of the table's refreshes): 0 the touch's tick, 1 the grab's, 2 the dead's (meleetouch::DeathTick)
void NoteTickBy(int who);

// The touch's timer, every tick the bodies are made: the scan's units (ids, their game modes), the rigs read and which rig each unit is (rigOf), the hero
// (world units). Puts a pose for every body drawn out of the game's pose into made[n..cap) (replacing one for the same skeleton) and returns the new count.
// dead: [weight] death_ragdoll - the monsters near that the game has dying or dead (mode 0, 12), with their MonStats row's flags (rowOk: it read as theirs).
struct Unit { uint32_t id; int mode; bool hostile; };
struct Dead { uint32_t id; int mode; bool rowOk; uint32_t rowFlags; };
int Tick(const skel::MonsterRig* rigs, size_t nr, const Unit* units, int nUnits, const int* rigOf, uint64_t nowMs, float upm, const float heroRender[3],
         dollpose::Pose* made, int n, int cap, const Dead* dead = nullptr, int nDead = 0);
// ... and then the bodies (vr/monster_bodies.h, render metres - the weapon's, the grab's, the debug boxes') of every body Tick drew out of the game's pose
// laid on it as drawn: each limb's capsule carried with its bone from the game's pose to ours, what it holds with the hand nearer it. The game's pose has
// the body standing at its real place while it is drawn lying, held or walking home (Serg, 0.192.15: «если я взял за нпс его рагдол тоже должен взяться» -
// the hand closed on the drawn body, the capsules stood beside it).
void LayBodies(monsterbodies::Snapshot* s, float upm);

}  // namespace knockdown
