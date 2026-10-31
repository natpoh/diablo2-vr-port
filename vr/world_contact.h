// world_contact.h - [weight] world_contact / world_contact_hands: the held weapon and the bare hands meet the level's walls, pillars and big props in VR F4
// (docs/notes/world_contact.md). Only DRAWN: no request of the game's, nothing of its state, no attack made or held back - the game's attacks, its hit
// detection, its movement and the hero's place stay the game's.
//
// How it runs:
//   Tick           vrcam's timer: twice a second, on a worker of its own, the level's pieces near the hero as the renderer draws them
//                  (knockprops::LevelBoxes: each one's model bounds under its world matrix) and which of them are walls (vr/world_walls.h)
//   Walls          the weight model's step and the hands' (the skeleton hook): those walls in the hands' frame (metres from the head) through the view the
//                  drawn weapon is put in the world with (monsterbodies::RenderToHead) - the weight model turns them on into its room
//   BlockHands     the skeleton hook, after the weight model and the grab: a bare hand (not on the weapon the weight model holds) is held at a wall's face
//                  while its controller is past it (cleanroom/heft/walls.h Block), sliding along; far past, it goes back to the controller
//   NoteWeapon     the weight model: what its weapon met (a pulse in the hand, the log, the debug view)
//   Marks          [debug] world_contact_draw (view only): the walls near the hands, the one met, the controller against the hand or weapon as drawn -
//                  with the melee boxes' capsules (the effect's Hitboxes)
// Off (both boxes unticked), or not F4: nothing is read, nothing changes.
#pragma once

#include <cstdint>

#include "monster_bodies.h"
#include "walls.h"

namespace skel {
struct Input;
}

namespace worldcontact {

using LogFn = void (*)(const char*);
using PulseFn = void (*)(int side, float amplitude, int durationMs);

// pluginDir: the plugin's folder (with its backslash): the solids catalogue is d2r_vr_props\solids.bin there.
void Init(LogFn log, PulseFn pulse, const wchar_t* pluginDir);
// [weight] world_contact (the weapon), world_contact_hands (the bare hands), [debug] world_contact_draw; from the ini.
void Configure(bool weapon, bool hands, bool draw);
bool WeaponWanted();
bool HandsWanted();
bool DrawWanted();

// What vrcam knows each tick: F4 and a game area (ok), render units per metre, the hero's feet in the render world (units), the game's session.
struct Frame {
    bool ok = false;
    float upm = 1.0f;
    float hero[3] = {};
    uint64_t session = 0;
};
void Tick(const Frame& f);

// The walls near the head (the latest scan's, nearest first) in the hands' frame through the view: up to max. 0 when off, stale or none.
int Walls(const monsterbodies::View& v, heft::walls::Wall* out, int max);

// The skeleton hook (mode 2): the bare hands held at the walls. holds[side]: that hand is on the weapon the weight model holds (the weapon's own contact holds it).
void BlockHands(skel::Input* in, const bool holds[2]);

// The weight model, every step its weapon was given walls: what it met (heft::walls::Report, the room's frame turned back into the render world by the caller),
// the hands that hold it, the controller of the main hand and its hand as drawn on the weapon (render metres) - a pulse at a new strike, the log, the debug view.
struct WeaponNote {
    int walls = 0;
    uint32_t id = 0;
    float approach = 0.0f, depthCm = 0.0f;
    float point[3] = {};
    bool holds[2] = {};
    bool controllerOk = false;
    float controller[3] = {}, drawn[3] = {};
    int returned = 0, ignored = 0;
    float costUs = 0.0f;
};
void NoteWeapon(const WeaponNote& n);

// [debug] world_contact_draw: the capsules to draw (the effect's HitboxA / HitboxB: world units relative to where the hands hang, the melee boxes' kinds); upm: world
// units per metre.
int Marks(uint64_t nowMs, const float eyeWorld[3], float upm, float (*a)[4], float (*b)[4], int max);

// For Settings' status and the log.
const char* StatusText();

}  // namespace worldcontact
