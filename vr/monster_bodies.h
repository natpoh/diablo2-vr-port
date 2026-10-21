// monster_bodies.h - [weight] monster_bodies: the monsters near the hero as bodies the held weapon meets (docs/notes/monster_bodies.md).
// Pure maths (cleanroom/tests/monster_bodies_test.cpp): the hit by touch's scan hands over the monsters' capsules in the render world
// (melee_touch.cpp Bodies, render metres = game units / upm, the frame the touch sweeps in), the weight model takes them into its room
// (weight.cpp) through the head's frame - the very mapping the drawn weapon is put in the world by (vr/blade_shape.h PointToRender), run back.
//
// Step 1 of the bodies: the capsule the game's own numbers give a monster (melee_bodies.h BodyFor: its footprint and height from MonStats),
// less the margin the touch adds to it (kTouchSlackM - a touch may count a little outside the body; the weapon must stop AT it). A body only
// for a monster the server calls hostile and unwalled (the touch's own verdict), so the hero's mercenary, a town's people, a monster behind
// a wall are not in the way. Nothing of the game's is changed: the bodies only shape how the drawn weapon moves.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "melee_bodies.h"

namespace monsterbodies {

constexpr int kMax = 64;           // capsules: a monster's limbs each (vr/monster_rig.h), the nearest monsters first (heft::hold::Hold::kBodies)
constexpr uint64_t kFreshMs = 300; // a snapshot older than this is not used (the scan stopped: another view, a menu, no weapon)

// A capsule of a monster in render metres: one of its limbs (rig: from its skeleton and its meshes' boxes, vr/monster_rig.h), or - when its skeleton was
// not read - the touch's own capsule of it (BodyFor: the margin included; the weight model takes CoreRadius of it).
struct Body {
    uint32_t id = 0;
    float bottom[3] = {}, top[3] = {};
    float radius = 0.3f;
    bool rig = false;
    int bone = -1;      // rig: the skeleton's bone
};
struct Snapshot {
    Body b[kMax];
    int n = 0;
    uint64_t tickMs = 0;   // GetTickCount64 of the scan
    float upm = 0.0f;      // world units per metre it was made with (the debug view puts it back in world units)
};

// The view the hands' frame is hung in the world with (vrcam's g_eyeWorld, g_stickFrame): the eye in world units, the head frame's right and
// forward in the world (on the ground), world units per metre. The same numbers the touch puts the drawn blade in the world with.
struct View {
    float eye[3] = {};
    float camR[3] = {1.0f, 0.0f, 0.0f}, camF[3] = {0.0f, 0.0f, 1.0f};
    float upm = 4.0f;
};

// A point of the render world (metres) -> the head's frame (metres from the eyes, x right, y up, z back, the head's turn only): the inverse of
// bladeshape::PointToRender. False when the view is degenerate.
inline bool RenderToHead(const View& v, const float p[3], float out[3]) {
    if (!(v.upm > 1e-3f) || !std::isfinite(v.upm)) return false;
    const float dx = p[0] - v.eye[0] / v.upm, dz = p[2] - v.eye[2] / v.upm;
    // [dx dz] = vx * (R.x, R.z) - vz * (F.x, F.z)
    const float a = v.camR[0], b = -v.camF[0], c = v.camR[2], d = -v.camF[2];
    const float det = a * d - b * c;
    if (!(std::fabs(det) > 1e-6f) || !std::isfinite(det)) return false;
    const float vx = (dx * d - b * dz) / det, vz = (a * dz - c * dx) / det;
    out[0] = vx;
    out[2] = vz;
    out[1] = p[1] - v.eye[1] / v.upm - (vx * v.camR[1] - vz * v.camF[1]);
    return std::isfinite(out[0] + out[1] + out[2]);
}

// The body the weapon meets: the touch's capsule less the touch's margin, never thinner than 8 cm.
inline float CoreRadius(float touchRadius) { return std::max(0.08f, touchRadius - meleetouch::kTouchSlackM); }

// The bodies of the hostile monsters (the touch's capsules of them, render metres, the nearest first) from their skeletons: each monster takes the nearest
// rig not taken yet whose place (world units, its transform's translation over upm) is within the monster's capsule plus kMatchM across the ground - and
// so its limbs - or, with none there, its own capsule. Rig: anything with root[3], n and c[] of monsterrig::Capsule (skel::MonsterRig). *rigged: how many
// monsters got their skeleton. The snapshot's tick is the caller's.
constexpr float kMatchM = 0.35f;
template <class Rig>
void Assemble(const Body* monsters, int nMonsters, const Rig* rigs, int nRigs, float upm, Snapshot* out, int* rigged = nullptr) {
    out->n = 0;
    if (rigged) *rigged = 0;
    if (!(upm > 1e-3f)) return;
    bool used[64] = {};
    for (int m = 0; m < nMonsters && out->n < kMax; ++m) {
        const Body& mb = monsters[m];
        int best = -1;
        float bestD = 1e30f;
        for (int r = 0; r < nRigs && r < 64; ++r) {
            if (used[r] || rigs[r].n <= 0) continue;
            const float dx = rigs[r].root[0] / upm - mb.bottom[0], dz = rigs[r].root[2] / upm - mb.bottom[2];
            const float d = std::sqrt(dx * dx + dz * dz);
            if (d <= mb.radius + kMatchM && d < bestD) { bestD = d; best = r; }
        }
        if (best < 0) {
            out->b[out->n++] = mb;
            continue;
        }
        used[best] = true;
        if (rigged) ++*rigged;
        for (int i = 0; i < rigs[best].n && out->n < kMax; ++i) {
            const auto& c = rigs[best].c[i];
            Body& b = out->b[out->n++];
            b.id = mb.id;
            for (int k = 0; k < 3; ++k) { b.bottom[k] = c.a[k] / upm; b.top[k] = c.b[k] / upm; }
            b.radius = c.radius / upm;
            b.rig = true;
            b.bone = c.bone;
        }
    }
}

// [debug] melee_boxes: the bodies drawn in the headset (D2R_DepthFog.fx Hitboxes), each limb where the weapon would meet it - so the fit can be looked at
// before [weight] monster_bodies is ticked. The effect's colours: kKindLimb a limb of a monster's own skeleton (pink), kKindOwn a monster that kept the
// game's capsule - its skeleton was not read (blue: drawn as the weapon meets it, less the touch's margin), kKindTouched the capsule the weapon is
// on now (white). World units from where the hands hang (eyeWorld), as melee_boxes.h Pack. touchedId / touchedBone: the capsule the weapon met on the
// weight model's last step - the monster's id and the bone (-1: its own capsule); id 0 none. Returns how many were packed (up to max).
constexpr int kKindLimb = 12, kKindOwn = 13, kKindTouched = 14;
inline int Pack(const Snapshot& s, const float eyeWorld[3], uint32_t touchedId, int touchedBone, float (*a)[4], float (*b)[4], int max) {
    if (!(s.upm > 1e-3f) || max <= 0) return 0;
    int k = 0;
    for (int i = 0; i < s.n && k < max; ++i) {
        const Body& c = s.b[i];
        const float r = c.rig ? c.radius : CoreRadius(c.radius);
        for (int j = 0; j < 3; ++j) {
            a[k][j] = c.bottom[j] * s.upm - eyeWorld[j];
            b[k][j] = c.top[j] * s.upm - eyeWorld[j];
        }
        a[k][3] = r * s.upm;
        const bool on = touchedId != 0 && c.id == touchedId && (c.rig ? c.bone : -1) == touchedBone;
        b[k][3] = static_cast<float>(on ? kKindTouched : c.rig ? kKindLimb : kKindOwn);
        ++k;
    }
    return k;
}

}  // namespace monsterbodies
