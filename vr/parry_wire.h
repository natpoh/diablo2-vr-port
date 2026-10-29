// parry_wire.h - [weight] parry_*: the game-facing half of vr/parry.h - the pulse, the knock, the log. F4 ONLY (vr/view_gate.h): nothing is felt,
// knocked or told in another view. Nothing of the game's is changed here: whether a monster hits, and the game's own block, stay the game's.
//
//   melee_touch.cpp  each pose: the hands' blades and the monsters' bodies (Blades) - the clash of the weapon with a monster's weapon in its attack;
//                    and asks AttackingNow for parry_counts_hit.
//   shield_smite.cpp each frame the shield is posed: its capsule (Shield) - how near it was to the blow, for the block shown; it presses Smite itself.
//   vrcam's timer    the hero's mode and class (Tick) - the game's block shown as a parry.
#pragma once

#include <cstdint>

#include "monster_bodies.h"
#include "parry.h"

namespace parrywire {

struct Settings {
    bool feel = true;          // [weight] parry_feel
    bool blockShow = true;     // [weight] parry_block_show
    bool clawBlock = true;     // [weight] parry_claw_block: the Assassin's Weapon Block (claws in both hands), apart from the block shown
    bool shieldSmite = true;   // [weight] parry_shield_smite (shield_smite.cpp presses)
    bool countsHit = false;    // [weight] parry_counts_hit (melee_touch.cpp presses)
    bool shields = false;      // [weight] monster_bodies && monster_shields: the monsters' weapons are there to meet at all
};
using LogFn = void (*)(const char*);
void SetLog(LogFn fn);
// vrcam's read of the ini (live): each change told once.
void Configure(const Settings& s);
Settings Get();
bool ShieldSmiteWanted();   // parry_shield_smite and the monsters' weapons there, F4
bool CountsHitWanted();     // parry_counts_hit and the monsters' weapons there, F4

// One of the hands' blades as hit by touch sweeps it (render metres, the bodies' frame). hand: 0 right, 1 left. weighted: the weight model holds it
// (a knock moves it). kind: D2RVR_TYPE_*.
struct Blade {
    parry::Capsule c;
    int hand = 0;
    bool weighted = false;
    uint32_t kind = 0;
};
// melee_touch.cpp, each pose (its timer's thread).
void Blades(const Blade* blades, int n, const monsterbodies::Snapshot& snap, uint64_t nowMs);
// shield_smite.cpp, each frame the shield is posed (the same thread): its capsule (render metres) and its hand (0 right, 1 left).
void Shield(const parry::Capsule& c, int hand, const monsterbodies::Snapshot& snap, uint64_t nowMs);
// vrcam's timer: the hero's unit mode (-1 not known) and class (skel::HeroClass, -1 not known).
void Tick(int heroMode, int heroClass, uint64_t nowMs);

// Is this monster in an attack now (its unit's mode at the last scan, parry::MonsterAttacking)? From the bodies snapshot.
bool Attacking(const monsterbodies::Snapshot& snap, uint32_t id);

}  // namespace parrywire
