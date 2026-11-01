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
    bool deathOn = false;       // [weight] death_ragdoll: a humanoid the game kills falls as our ragdoll and lies as it fell (vr/death_ragdoll.h; and the bodies)
};
void Configure(const Settings& s);
bool Wanted();        // either of them on
bool ThrowWanted();
bool DeathWanted();   // [weight] death_ragdoll: the touch's scan lists the dying and the dead (Tick's dead)

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

// The world-matrix hook's writer (headshadow::SetAttachWriter -> potion_hide's hook; the renderer's thread): what hangs on a body drawn out of the game's pose -
// its weapon, its shield - goes with the bone it hangs on as drawn.
void OnXform(uint32_t entity, uintptr_t component, uintptr_t out);
// ModelDrawCollect, just before the game's own (headshadow::SetBeforeDraw; the render workers): an entity of a body drawn away from its place has its bounds
// grown again here - after whatever made them again this frame, before the pass decides on it (Serg, 0.192.39: «когда беру за руку нпс, он по-прежнему
// мерцает»: the game made each entity's box again about every frame, and the hook's and the timer's growth came before that in about 40 % of them).
void BeforeDraw(uint32_t entity, bool eyes);

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
