// wall_collide.h - [weight] wall_contact_mode 2 (Bear's): the held weapon stops at the level's walls, closed doors and the floor, from the game's own collision
// cells (about 46 cm each, the floor flat around the hero) - D2R VR Unleashed's "Weapons hit walls" (vr/unleashed_wall_cells.h / .cpp, MPL 2.0)
// put into our weight model's walls (cleanroom/heft/walls.h): the weapon stops on a wall's face and slides along it, the drawn hand on it gives
// way while the controller goes on, a knock is felt in the hands that hold it; far past (the weight model's snap) it comes back into the hand.
//
// Only what is drawn and felt, VR first person (F4) only: no game command, no attack, no movement is touched - the game's hits and the hero's place
// stay the game's. The cells are read on the game thread with the game's own query functions (read-only), so only where the game's server runs here
// (single player or the host); a joined game has none and the weapon passes walls as before.
//
// One of the three values of [weight] wall_contact_mode (shared/wall_contact_mode.h): 2, "Bear's". Never together with 1, "Ours" (world_contact, the
// walls from the meshes): load_settings.cpp turns on one or the other; world_contact_hands (the bare hands) keeps its own tick.
//
//   Configure  the ini (live)
//   Tick       vrcam's timer: where the hero stands (render units) - the cells around him are asked for on the game thread
//   Walls      the weight model's step: the walls and the floor near the eyes in the hands' frame through the view the weapon is drawn with
//   NoteWeapon the weight model: what its weapon met - a knock in the hands that hold it, the log
#pragma once

#include <algorithm>
#include <cstdint>

#include "monster_bodies.h"
#include "walls.h"

namespace D2RL {
struct PluginContext;
}
namespace worldcontact {
struct WeaponNote;
}

namespace wallcollide {

using LogFn = void (*)(const char*);
using PulseFn = void (*)(int side, float amplitude, int durationMs);

void Init(const D2RL::PluginContext* ctx, LogFn log, PulseFn pulse);
// [weight] wall_contact_mode 2 (Bear's)
void Configure(bool on);
bool Wanted();

struct Frame {
    bool ok = false;          // F4 and a game area
    float upm = 1.0f;         // render units per metre
    float hero[3] = {};       // the hero's feet, render units
    uint64_t session = 0;     // gamestate::LocalPlayer()
};
void Tick(const Frame& f);

// The walls near the head (heft walls in the hands' frame), up to max; 0 when off, stale, or none.
int Walls(const monsterbodies::View& v, heft::walls::Wall* out, int max);
void NoteWeapon(const worldcontact::WeaponNote& n);

// Pure, for the test (below): a cell's id for the walls' memory (which face it lets the weapon out of) - stable while the cell is near; a closed door's
// carries 0x20000000; the floor's is kFloorId.
constexpr uint32_t kFloorId = 0x57FFFFFFu, kDoorIdBit = 0x20000000u;
inline uint32_t CellId(int x, int z) { return 0x40000000u | ((uint32_t)(x & 0x7FFF) << 15) | (uint32_t)(z & 0x7FFF); }

// The knock: a new strike at approach m/s -> the pulse's amplitude (0: none) - from 0.5 m/s (Unleashed's clank), 0.6 .. 1.0 (PS VR2 feels 0.55+).
inline float KnockAmplitude(float approach) {
    if (!(approach >= 0.5f)) return 0.0f;
    return std::clamp(0.6f + 0.1f * (approach - 0.5f), 0.6f, 1.0f);
}

// A box of the render world (metres, axis-aligned, open faces) into the hands' frame through the view: its centre and each axis as the view turns it.
inline bool ToHead(const monsterbodies::View& v, const float lo[3], const float hi[3], uint8_t open, uint32_t id, heft::walls::Wall* out) {
    if (!out) return false;
    const float c[3] = {(lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f, (lo[2] + hi[2]) * 0.5f};
    float hc[3];
    if (!monsterbodies::RenderToHead(v, c, hc)) return false;
    heft::walls::Wall w;
    w.c = {hc[0], hc[1], hc[2]};
    for (int i = 0; i < 3; ++i) {   // each render axis as the view turns it (the view is a turn about the vertical and a shift)
        float tip[3] = {c[0], c[1], c[2]};
        tip[i] += 1.0f;
        float t[3];
        if (!monsterbodies::RenderToHead(v, tip, t)) return false;
        const heft::V3 d {t[0] - hc[0], t[1] - hc[1], t[2] - hc[2]};
        const double l = heft::Len(d);
        if (!(l > 1e-6)) return false;
        w.axis[i] = heft::Mul(d, 1.0 / l);
    }
    w.half = {(hi[0] - lo[0]) * 0.5f, (hi[1] - lo[1]) * 0.5f, (hi[2] - lo[2]) * 0.5f};
    if (!(w.half.x > 0.0 && w.half.y > 0.0 && w.half.z > 0.0)) return false;
    w.id = id;
    w.body = 0;
    w.open = open;
    *out = w;
    return true;
}

}  // namespace wallcollide
