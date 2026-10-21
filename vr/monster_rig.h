// monster_rig.h - [weight] monster_bodies: a monster's body from its OWN skeleton - a capsule on each of its limbs (docs/notes/monster_bodies.md).
// Pure: every read of the game's memory goes through the caller's guarded reader, so cleanroom/tests/monster_rig_test.cpp drives it on synthetic memory.
//
// The author (2026-10-21): «надо не с капсулой монстра, а надевать на монстра скелет на его конечности». Where the bones come from: the renderer keeps
// every model as an entity of its ECS (vr/skeletons.cpp, namespace ecs): its SkeletonComponent is the SkeletonInstance the pose hook works on, its
// TransformComponent the model's place in the world. A monster's bones are read there - its world pose (model space, one matrix a bone, row vectors) times
// its transform - off the pose hook, on the touch's timer: the hook pays nothing for them.
//
// How thick a limb is: the model's own numbers, no hand-set girths. Every Granny mesh carries a box per bone it is skinned to (granny_bone_binding: the
// bone's name and the box of the vertices bound to it, OBBMin / OBBMax, in that BONE'S frame), so the box rides with the bone as the monster moves. A box
// becomes a capsule along its longest side: the radius the mean of the other two half-sides, its ends one radius in from the box's ends. The boxes of one
// bone in several meshes (a body, an armour, a cape) are one box. Bones whose box is a trifle against the whole body (a finger, a toe, the jaw) are left
// out; the largest kMaxPerRig stay.
//
// The layout is Granny's (LE64, packed to four bytes), as the model files' type definitions give it (granny_mesh.h has the model and the mesh's head):
//   granny_mesh          +0x30 int32 BoneBindingCount, +0x34 granny_bone_binding* BoneBindings
//   granny_bone_binding  +0x00 char* BoneName, +0x08 float OBBMin[3], +0x14 float OBBMax[3], +0x20 int32 TriangleCount, +0x24 int32* (stride 0x2C)
//   granny_skeleton      +0x08 int32 BoneCount, +0x0C granny_bone* Bones (stride 0xA4: +0x00 char* Name, +0x08 int32 ParentIndex)
//   granny_world_pose    +0x00 int32 BoneCount, +0x04 float (*)[16] (the SkeletonInstance's +0x20)
// None of it is checked against the game yet in the headset (2026-10-21): BoxesOf refuses what does not read like it (names that are not the skeleton's
// bones, boxes that are not finite or are inside out) and the caller then keeps the game's own capsule for that monster.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "granny_mesh.h"

namespace monsterrig {

constexpr size_t kMeshBindingCount = 0x30, kMeshBindings = 0x34;
constexpr size_t kBindingStride = 0x2C, kBindingName = 0x00, kBindingMin = 0x08, kBindingMax = 0x14;
constexpr size_t kSkelCount = 0x08, kSkelBones = 0x0C, kBoneStride = 0xA4, kBoneName = 0x00, kBoneParent = 0x08;
constexpr int kMaxBones = 160;     // a monster's rig (the heroes' have ~100); more: not read
constexpr int kMaxBindings = 256;  // per mesh
constexpr int kMaxBoxes = 64;      // bones with a box
constexpr int kMaxPerRig = 24;     // capsules a monster gets (the largest)
constexpr float kTrifle = 0.04f;   // a box smaller than this share of the body's height on its longest side is a trifle (a finger, a toe)
constexpr int kNameLen = 48;

// A bone's box in its own frame (the model's units).
struct BoneBox {
    int bone = -1;
    float min[3] = {}, max[3] = {};
};
struct Boxes {
    int n = 0;
    BoneBox b[kMaxBoxes];
    int bones = 0;         // the skeleton's bone count
    int meshes = 0;        // meshes that had boxes
    const char* why = "";  // why there are none
};

// The boxes of a model's meshes, by its skeleton's bones: instance is the granny_model_instance (the SkeletonInstance's +0x08), skeleton its skeleton (that +0x08).
// False (and out->why) when nothing reads as such.
template <class Read>
bool BoxesOf(Read&& read, uintptr_t instance, uintptr_t skeleton, Boxes* out) {
    *out = Boxes{};
    uintptr_t model = 0, modelSkeleton = 0, meshes = 0, bones = 0;
    int32_t meshCount = 0, boneCount = 0;
    if (instance < 0x10000 || !read(instance + grannymesh::kInstanceModel, &model, sizeof model) || model < 0x10000) { out->why = "no model"; return false; }
    if (!read(model + grannymesh::kModelSkeleton, &modelSkeleton, sizeof modelSkeleton) || modelSkeleton != skeleton) { out->why = "the model is not its skeleton's"; return false; }
    if (!read(skeleton + kSkelCount, &boneCount, sizeof boneCount) || boneCount < 1 || boneCount > kMaxBones || !read(skeleton + kSkelBones, &bones, sizeof bones) || bones < 0x10000) {
        out->why = "the skeleton's bones do not read";
        return false;
    }
    out->bones = boneCount;
    static thread_local char names[kMaxBones][kNameLen];
    for (int i = 0; i < boneCount; ++i) {
        uintptr_t p = 0;
        names[i][0] = 0;
        if (!read(bones + (uintptr_t)i * kBoneStride + kBoneName, &p, sizeof p) || !grannymesh::String(read, p, names[i])) names[i][0] = 0;
    }
    if (!read(model + grannymesh::kModelMeshCount, &meshCount, sizeof meshCount) || meshCount < 1 || meshCount > grannymesh::kMaxMeshes ||
        !read(model + grannymesh::kModelMeshes, &meshes, sizeof meshes) || meshes < 0x10000) {
        out->why = "the model's meshes do not read";
        return false;
    }
    int strangers = 0;   // a binding whose name is no bone of the skeleton: the layout is not what it is taken for
    for (int m = 0; m < meshCount; ++m) {
        uintptr_t mesh = 0, binds = 0;
        int32_t nb = 0;
        if (!read(meshes + (uintptr_t)m * grannymesh::kBindingStride, &mesh, sizeof mesh) || mesh < 0x10000) continue;
        if (!read(mesh + kMeshBindingCount, &nb, sizeof nb) || nb < 1 || nb > kMaxBindings || !read(mesh + kMeshBindings, &binds, sizeof binds) || binds < 0x10000) continue;
        bool any = false;
        for (int k = 0; k < nb; ++k) {
            const uintptr_t at = binds + (uintptr_t)k * kBindingStride;
            uintptr_t np = 0;
            char name[kNameLen];
            float mn[3], mx[3];
            if (!read(at + kBindingName, &np, sizeof np) || !grannymesh::String(read, np, name) || !read(at + kBindingMin, mn, sizeof mn) || !read(at + kBindingMax, mx, sizeof mx)) continue;
            int bone = -1;
            for (int i = 0; i < boneCount && bone < 0; ++i)
                if (names[i][0] && std::strcmp(names[i], name) == 0) bone = i;
            if (bone < 0) { ++strangers; continue; }
            bool ok = true;
            for (int c = 0; c < 3; ++c) ok = ok && std::isfinite(mn[c]) && std::isfinite(mx[c]) && mx[c] >= mn[c] && mx[c] - mn[c] < 1e4f;
            if (!ok) continue;
            if (mx[0] - mn[0] + mx[1] - mn[1] + mx[2] - mn[2] <= 0.0f) continue;   // a bone with no vertices of this mesh
            int slot = -1;
            for (int j = 0; j < out->n && slot < 0; ++j)
                if (out->b[j].bone == bone) slot = j;
            if (slot < 0) {
                if (out->n >= kMaxBoxes) continue;
                slot = out->n++;
                out->b[slot].bone = bone;
                std::memcpy(out->b[slot].min, mn, sizeof mn);
                std::memcpy(out->b[slot].max, mx, sizeof mx);
            } else {   // the same bone in another mesh: one box round both
                for (int c = 0; c < 3; ++c) {
                    out->b[slot].min[c] = std::min(out->b[slot].min[c], mn[c]);
                    out->b[slot].max[c] = std::max(out->b[slot].max[c], mx[c]);
                }
            }
            any = true;
        }
        if (any) ++out->meshes;
    }
    if (strangers > out->n) { *out = Boxes{}; out->why = "the bone bindings do not name the skeleton's bones"; return false; }
    if (!out->n) { out->why = "no bone has a box"; return false; }
    return true;
}

// A capsule in the world (the units of the matrices: the game's world units).
struct Capsule {
    float a[3] = {}, b[3] = {};
    float radius = 0.0f;
    int bone = -1;
};

// A row-vector point through a row-major 4x4: p * M.
inline void Through(const float m[16], const float p[3], float out[3]) {
    for (int c = 0; c < 3; ++c) out[c] = p[0] * m[c] + p[1] * m[4 + c] + p[2] * m[8 + c] + m[12 + c];
}

// A bone's box (its own frame) as a capsule in the world: bone the bone's matrix in the world (its pose times the model's place). False for a matrix that
// is not one (not finite, or a scale of nothing).
inline bool CapsuleOf(const float bone[16], const BoneBox& box, Capsule* out) {
    float s[3];   // how much each of the bone's axes is scaled into the world
    for (int k = 0; k < 3; ++k) {
        s[k] = std::sqrt(bone[k * 4] * bone[k * 4] + bone[k * 4 + 1] * bone[k * 4 + 1] + bone[k * 4 + 2] * bone[k * 4 + 2]);
        if (!(s[k] > 1e-6f) || !std::isfinite(s[k])) return false;
    }
    float c[3], e[3];   // the box's middle (bone frame) and its half-sides in the world
    for (int k = 0; k < 3; ++k) {
        c[k] = 0.5f * (box.min[k] + box.max[k]);
        e[k] = 0.5f * (box.max[k] - box.min[k]) * s[k];
    }
    const int L = e[0] >= e[1] && e[0] >= e[2] ? 0 : e[1] >= e[2] ? 1 : 2;
    const int u = (L + 1) % 3, v = (L + 2) % 3;
    const float r = 0.5f * (e[u] + e[v]);
    const float half = std::max(0.0f, e[L] - r) / s[L];   // the segment's half length, in the bone's units along L
    float p0[3] = {c[0], c[1], c[2]}, p1[3] = {c[0], c[1], c[2]};
    p0[L] -= half;
    p1[L] += half;
    Through(bone, p0, out->a);
    Through(bone, p1, out->b);
    out->radius = r;
    out->bone = box.bone;
    return std::isfinite(out->a[0] + out->a[1] + out->a[2] + out->b[0] + out->b[1] + out->b[2] + r) && r >= 0.0f;
}

inline float Extent(const Capsule& c) {   // the capsule's longest side: its length plus both rounded ends
    const float d[3] = {c.b[0] - c.a[0], c.b[1] - c.a[1], c.b[2] - c.a[2]};
    return std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) + 2.0f * c.radius;
}

// The capsules of a rig, the trifles left out, the largest kMaxPerRig kept (in place; returns how many). height: the body's height in the same units
// (the highest capsule point over the lowest - the caller may give its own, e.g. from the game's MonStats; <= 0: worked out here).
inline int Keep(Capsule* caps, int n, float height = 0.0f) {
    if (n <= 0) return 0;
    if (!(height > 0.0f)) {
        float lo = 1e30f, hi = -1e30f;
        for (int i = 0; i < n; ++i) {
            lo = std::min({lo, caps[i].a[1] - caps[i].radius, caps[i].b[1] - caps[i].radius});
            hi = std::max({hi, caps[i].a[1] + caps[i].radius, caps[i].b[1] + caps[i].radius});
        }
        height = hi - lo;
    }
    int k = 0;
    for (int i = 0; i < n; ++i)
        if (Extent(caps[i]) >= kTrifle * height) caps[k++] = caps[i];
    std::sort(caps, caps + k, [](const Capsule& x, const Capsule& y) {
        return x.radius * x.radius * Extent(x) > y.radius * y.radius * Extent(y);   // by the room they take
    });
    return std::min(k, kMaxPerRig);
}

}  // namespace monsterrig
