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
// bones with a box: every one the skeleton may have - a box is one bone's (the town NPCs have 64 boxed bones and more - Kashya, Warriv: the boxes stopped at
// 64, cut in the meshes' binding order, and whichever came after - the left shin, the author's screenshot 2026-10-24 - had none)
constexpr int kMaxBoxes = kMaxBones;
constexpr int kMaxPerRig = 24;     // capsules a monster gets (the largest, a left and right pair kept or left out together: Keep)
constexpr float kTrifle = 0.04f;   // a box smaller than this share of the body's height on its longest side is a trifle (a finger, a toe)
constexpr int kNameLen = 48;

// A bone's box in its own frame (the model's units).
struct BoneBox {
    int bone = -1;
    int mirror = -1;   // the bone of the other side by its name (a left calf's right calf), -1 none: Keep keeps a pair together
    float min[3] = {}, max[3] = {};
};

// Are two bone names one bone of the left and right sides? Equal once their side's token is taken out - "Bip01 L Calf" / "Bip01 R Calf", "l_shin" / "r_shin",
// "LeftUpLeg" / "RightUpLeg", "arm.l" / "arm.r" (any case): a token "l"/"r" between non-letters or at an end, or "left"/"right" anywhere.
inline bool SideFree(const char* s, char* out, size_t cap, char* side) {
    *side = 0;
    size_t o = 0;
    const size_t n = std::strlen(s);
    const auto lower = [](char c) { return (char)((c >= 'A' && c <= 'Z') ? c + 32 : c); };
    const auto letter = [&](size_t i) { return i < n && ((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z')); };
    for (size_t i = 0; i < n && o + 1 < cap;) {
        if (!*side) {
            const bool edgeL = i == 0 || !letter(i - 1);
            if (n - i >= 4 && lower(s[i]) == 'l' && lower(s[i + 1]) == 'e' && lower(s[i + 2]) == 'f' && lower(s[i + 3]) == 't') { *side = 'l'; out[o++] = '*'; i += 4; continue; }
            if (n - i >= 5 && lower(s[i]) == 'r' && lower(s[i + 1]) == 'i' && lower(s[i + 2]) == 'g' && lower(s[i + 3]) == 'h' && lower(s[i + 4]) == 't') { *side = 'r'; out[o++] = '*'; i += 5; continue; }
            if (edgeL && (lower(s[i]) == 'l' || lower(s[i]) == 'r') && !letter(i + 1)) { *side = lower(s[i]); out[o++] = '*'; ++i; continue; }
        }
        out[o++] = lower(s[i++]);
    }
    out[o] = 0;
    return *side != 0;
}
inline bool Mirrored(const char* a, const char* b) {
    char fa[kNameLen + 8], fb[kNameLen + 8], sa = 0, sb = 0;
    if (!a[0] || !b[0] || !SideFree(a, fa, sizeof fa, &sa) || !SideFree(b, fb, sizeof fb, &sb)) return false;
    return sa != sb && std::strcmp(fa, fb) == 0;
}
// Each box's mirror among the skeleton's bones (names[bone]).
inline void FindMirrors(const char (*names)[kNameLen], int boneCount, BoneBox* boxes, int n) {
    for (int i = 0; i < n; ++i) {
        boxes[i].mirror = -1;
        const int b = boxes[i].bone;
        if (b < 0 || b >= boneCount) continue;
        for (int j = 0; j < boneCount && boxes[i].mirror < 0; ++j)
            if (j != b && Mirrored(names[b], names[j])) boxes[i].mirror = j;
    }
}
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
    FindMirrors(names, boneCount, out->b, out->n);
    return true;
}

// ---- The game's own meshes (the headset test of 2026-10-21: every monster's Granny model read "the model's meshes do not read") ----
// The model a SkeletonInstance poses is the .skeleton file's: bones, no meshes. The meshes are D2R's own Mesh objects, made from the .model file's
// granny_mesh by Mesh::LoadGeometry (0xFA04C0 in 3.3.93787), which copies the mesh's bone bindings into the Mesh: an array of granny_bone_binding
// (the same 0x2C layout, the name pointing at the Mesh's own copy of it) whose count it keeps at Mesh +0xB4 and the array at +0xB8 (Mesh +0x84 is
// a granny_mesh of its own, +0x30 / +0x34 into it). Which meshes a model shows: its entity's ModelComponent (entt, 0x88 bytes; the type the game's
// type_seq<ModelComponent> 0x1DBE40 names) holds the model instance at +0; ModelInstance::GetMeshes (0xF3E900) gives instance +0x310 - a vector
// (data, count at +8) of handles whose first qword is the Mesh - once the instance's resource (+0x20) has its state byte (+0xC8) at 4, loaded.
// The Granny types' own layout (granny_bone_binding 0x2C: BoneName, OBBMin, OBBMax, TriangleIndices; granny_mesh BoneBindings at +0x30) was read
// off the game's type tables in the same image.
constexpr size_t kModelCompInstance = 0x00;                       // ModelComponent +0: the model instance
constexpr size_t kInstanceResource = 0x20, kResourceState = 0xC8; // loaded when the state byte is 4
constexpr uint8_t kLoaded = 4;
constexpr size_t kInstanceMeshes = 0x310;                         // its meshes: data, count at +8
constexpr size_t kMeshBoneCount = 0xB4, kMeshBones = 0xB8;        // a Mesh's bone bindings (count int32, array)

constexpr int kMaxModels = 32;   // model instances merged into one body (a unit's parts)

// The boxes of the meshes several model instances show (a unit's parts: a body, a head, a cape...), by the skeleton's bone names (as BoxesOf); the
// boxes of one bone in several parts are one box. *meshesOut (optional): how many meshes they have. One part still loading: none yet, and it says so
// (the caller asks again); a part that does not read is passed over.
template <class Read>
bool BoxesOfModels(Read&& read, const uintptr_t* instances, int n, uintptr_t skeleton, Boxes* out, int* meshesOut = nullptr) {
    *out = Boxes{};
    if (meshesOut) *meshesOut = 0;
    uintptr_t data[kMaxModels] = {}, bones = 0;
    uint64_t count[kMaxModels] = {};
    int32_t boneCount = 0;
    int reading = 0, withMeshes = 0;
    if (n > kMaxModels) n = kMaxModels;
    for (int i = 0; i < n; ++i) {
        uintptr_t res = 0;
        uint8_t state = 0;
        if (instances[i] < 0x10000 || !read(instances[i] + kInstanceResource, &res, sizeof res) || res < 0x10000 || !read(res + kResourceState, &state, 1)) continue;
        ++reading;
        if (state != kLoaded) { out->why = "the model is not loaded yet"; return false; }
        if (!read(instances[i] + kInstanceMeshes, &data[i], sizeof data[i]) || !read(instances[i] + kInstanceMeshes + 8, &count[i], sizeof count[i]) || count[i] < 1 ||
            count[i] > (uint64_t)grannymesh::kMaxMeshes || data[i] < 0x10000) {
            count[i] = 0;
            continue;
        }
        ++withMeshes;
        if (meshesOut) *meshesOut += (int)count[i];
    }
    if (!reading) { out->why = "the model instance does not read"; return false; }
    if (!withMeshes) { out->why = "the model instance's meshes do not read"; return false; }
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
    int strangers = 0, bindings = 0;
    for (int i = 0; i < n; ++i)
    for (uint64_t m = 0; m < count[i]; ++m) {
        uintptr_t handle = 0, mesh = 0, binds = 0;
        int32_t nb = 0;
        if (!read(data[i] + m * 8, &handle, sizeof handle) || handle < 0x10000 || !read(handle, &mesh, sizeof mesh) || mesh < 0x10000) continue;
        if (!read(mesh + kMeshBoneCount, &nb, sizeof nb) || nb < 1 || nb > kMaxBindings || !read(mesh + kMeshBones, &binds, sizeof binds) || binds < 0x10000) continue;
        bool any = false;
        for (int k = 0; k < nb; ++k) {
            const uintptr_t at = binds + (uintptr_t)k * kBindingStride;
            uintptr_t np = 0;
            char name[kNameLen];
            float mn[3], mx[3];
            if (!read(at + kBindingName, &np, sizeof np) || !grannymesh::String(read, np, name) || !read(at + kBindingMin, mn, sizeof mn) || !read(at + kBindingMax, mx, sizeof mx)) continue;
            ++bindings;
            int bone = -1;
            for (int j = 0; j < boneCount && bone < 0; ++j)
                if (names[j][0] && std::strcmp(names[j], name) == 0) bone = j;
            if (bone < 0) { ++strangers; continue; }
            bool ok = true;
            for (int c = 0; c < 3; ++c) ok = ok && std::isfinite(mn[c]) && std::isfinite(mx[c]) && mx[c] >= mn[c] && mx[c] - mn[c] < 1e4f;
            if (!ok || mx[0] - mn[0] + mx[1] - mn[1] + mx[2] - mn[2] <= 0.0f) continue;
            int slot = -1;
            for (int j = 0; j < out->n && slot < 0; ++j)
                if (out->b[j].bone == bone) slot = j;
            if (slot < 0) {
                if (out->n >= kMaxBoxes) continue;
                slot = out->n++;
                out->b[slot].bone = bone;
                std::memcpy(out->b[slot].min, mn, sizeof mn);
                std::memcpy(out->b[slot].max, mx, sizeof mx);
            } else {
                for (int c = 0; c < 3; ++c) {
                    out->b[slot].min[c] = std::min(out->b[slot].min[c], mn[c]);
                    out->b[slot].max[c] = std::max(out->b[slot].max[c], mx[c]);
                }
            }
            any = true;
        }
        if (any) ++out->meshes;
    }
    if (!bindings) { *out = Boxes{}; out->bones = boneCount; out->why = "its meshes have no bone bindings"; return false; }
    if (strangers > out->n) { const int b = boneCount; *out = Boxes{}; out->bones = b; out->why = "the meshes' bone bindings do not name the skeleton's bones"; return false; }
    if (!out->n) { out->why = "no bone has a box"; return false; }
    FindMirrors(names, boneCount, out->b, out->n);
    return true;
}

// The boxes of the meshes one model instance shows (BoxesOfModels of one).
template <class Read>
bool BoxesOfMeshes(Read&& read, uintptr_t instance, uintptr_t skeleton, Boxes* out, int* meshesOut = nullptr) {
    return BoxesOfModels(read, &instance, 1, skeleton, out, meshesOut);
}

// ---- A unit's parts (the headset test of 2026-10-22: every monster said "the model has no ModelComponent") ----
// A unit (a monster, an NPC) is not one entity but several: the root - its SkeletonComponent, the .skeleton the SkeletonInstance poses, the transform with
// no parent the scan walks - and its parts, one entity each, whose ModelComponent holds the meshes. Read off the image of 3.3.93787: the unit's bounds
// (0xD75CC0) take the root's SkeletonComponent, then for each of 16 part slots look the part's entity up among the root's children (0xD7B630: by its
// UnitPartComponent, 8 bytes, the slot the byte at +0 - the getter 0x298800 is movzx eax, byte [rcx]) and read that entity's ModelComponent. The
// children are the root's AttachedChildrenComponent (entt, 0x28 bytes: +0x00 an array of entity ids (u32), +0x08 their count, its two top bits not
// part of it - the transform walk 0xEE8BE4 clears them with shl 2 / sar 2); "Only the root entity is expected to have children" (the game's assert):
// every part hangs off the root itself. The 16 slots are Diablo II's composite parts: HD TR LG RA LA RH LH SH S1..S8; RH, LH and SH are what the
// monster holds (its weapons, its shield) - not its body (docs/notes/monster_bodies.md, "Parts of their own").
constexpr size_t kChildrenCompSize = 0x28, kChildrenData = 0x00, kChildrenCount = 0x08;
constexpr size_t kPartCompSize = 0x08, kPartSlot = 0x00;
constexpr int kMaxChildren = 64;   // a unit's parts (16 slots and what is attached); more: not read
enum PartSlot { kHD = 0, kTR, kLG, kRA, kLA, kRH, kLH, kSH, kSlots = 16 };

// The children's count as the component keeps it (the two top bits are not part of it); -1 when it is no count.
inline int ChildCount(uint64_t raw) {
    const int64_t c = (int64_t)(raw << 2) >> 2;
    return c < 0 || c > kMaxChildren ? -1 : (int)c;
}

// Is a part in this slot the monster's body? slot -1: the entity has no UnitPartComponent (a model of the unit's own, not a held thing).
inline bool BodySlot(int slot) { return slot != kRH && slot != kLH && slot != kSH; }

// What a unit's children give: the model instances of its body parts.
struct Parts {
    int children = 0;   // the root's children (-1: the root has none / they do not read)
    int models = 0;     // children with a model
    int held = 0;       // of them, left out as held (a weapon, a shield)
    int slots[kMaxModels] = {};
    // [weight] monster_shields: the held parts' model instances and slots (kRH, kLH, kSH) - bodies of their own kind, not the body's
    static constexpr int kMaxHeld = 3;
    uintptr_t heldInst[kMaxHeld] = {};
    int heldSlot[kMaxHeld] = {};
    int nHeld = 0;
};

// The model instances of a root's body parts, into out (at most cap): children the root's AttachedChildrenComponent (its address; 0: it has none);
// modelOf(entity) the model instance of an entity's ModelComponent (0: none), slotOf(entity) its UnitPartComponent's slot (-1: none). Returns how many.
template <class Read, class ModelOf, class SlotOf>
int PartModels(Read&& read, uintptr_t children, ModelOf&& modelOf, SlotOf&& slotOf, uintptr_t* out, int cap, Parts* parts = nullptr) {
    Parts p;
    p.children = -1;
    int n = 0;
    uintptr_t data = 0;
    uint64_t raw = 0;
    const int count = children >= 0x10000 && read(children + kChildrenData, &data, sizeof data) && read(children + kChildrenCount, &raw, sizeof raw) ? ChildCount(raw) : -1;
    if (count == 0 || (count > 0 && data >= 0x10000)) {
        p.children = count;
        uint32_t ids[kMaxChildren];
        if (count > 0 && read(data, ids, (size_t)count * 4)) {
            for (int i = 0; i < count; ++i) {
                const uintptr_t inst = modelOf(ids[i]);
                if (inst < 0x10000) continue;
                const int slot = slotOf(ids[i]);
                if (p.models < kMaxModels) p.slots[p.models] = slot;
                ++p.models;
                if (!BodySlot(slot)) {
                    ++p.held;
                    if (p.nHeld < Parts::kMaxHeld) { p.heldInst[p.nHeld] = inst; p.heldSlot[p.nHeld] = slot; ++p.nHeld; }
                    continue;
                }
                if (n < cap) out[n++] = inst;
            }
        }
    }
    if (parts) *parts = p;
    return n;
}

// The ids of an entity's children (its AttachedChildrenComponent's address; 0: none). Returns how many (up to max), -1 when they do not read.
template <class Read>
int ChildIds(Read&& read, uintptr_t children, uint32_t* ids, int max) {
    uintptr_t data = 0;
    uint64_t raw = 0;
    if (children < 0x10000 || !read(children + kChildrenData, &data, sizeof data) || !read(children + kChildrenCount, &raw, sizeof raw)) return -1;
    const int count = ChildCount(raw);
    if (count <= 0) return count;
    if (data < 0x10000) return -1;
    const int k = count < max ? count : max;
    return read(data, ids, (size_t)k * 4) ? k : -1;
}

// [weight] monster_shields: what a unit holds, as the game builds it - fallen1.json (the game's own data): the "rhand" part is an entity with a
// UnitPartComponent and a UnitAttachmentComponent ("data/hd/items/weapon/sword/_monsters/fallen_scimitar.json" on the bone "R_weapon_attach") and NO
// model; the item is a unit of its own (fallen_scimitar.json: a root entity, and a model entity - ModelDefinitionComponent - with no skeleton) hung under
// it. So: each child of the unit's root that is a held part (RH / LH / SH) or has no model and no part (an attached unit's root) - its children, two
// levels down, carry the item's model. A held part with a model of its own is taken too. childrenOf(entity) the entity's AttachedChildrenComponent
// address (0: none). Returns how many (up to cap); *looked (optional): the children of the root that could hold something.
struct HeldModel { uintptr_t inst = 0; uint32_t entity = 0; int slot = -1; };
template <class Read, class ModelOf, class SlotOf, class ChildrenOf>
int HeldModels(Read&& read, uintptr_t rootChildren, ModelOf&& modelOf, SlotOf&& slotOf, ChildrenOf&& childrenOf, HeldModel* out, int cap, int* looked = nullptr) {
    int n = 0;
    if (looked) *looked = 0;
    uint32_t ids[kMaxChildren];
    const int count = ChildIds(read, rootChildren, ids, kMaxChildren);
    for (int i = 0; i < count && n < cap; ++i) {
        const int slot = slotOf(ids[i]);
        const uintptr_t own = modelOf(ids[i]);
        if (own >= 0x10000 && BodySlot(slot)) continue;   // a body part
        if (slot >= 0 && BodySlot(slot)) continue;       // a body part with no model of its own
        if (looked) ++*looked;
        if (own >= 0x10000) { out[n++] = {own, ids[i], slot}; continue; }
        uint32_t kids[kMaxChildren];
        const int nk = ChildIds(read, childrenOf(ids[i]), kids, kMaxChildren);
        for (int j = 0; j < nk && n < cap; ++j) {
            const uintptr_t m = modelOf(kids[j]);
            if (m >= 0x10000) { out[n++] = {m, kids[j], slot}; continue; }
            uint32_t gk[kMaxChildren];
            const int ng = ChildIds(read, childrenOf(kids[j]), gk, kMaxChildren);
            for (int q = 0; q < ng && n < cap; ++q) {
                const uintptr_t m2 = modelOf(gk[q]);
                if (m2 >= 0x10000) out[n++] = {m2, gk[q], slot};
            }
        }
    }
    return n;
}

// Where a held item is (its transform component: +0x00 its LOCAL matrix - against the part it hangs on - +0x40 its WORLD matrix, +0xC0 its parent): the world
// one when it is within maxApart of the monster's root (pos, world units); the local one only for an item with no parent (its local is the world's), within
// maxApart too. -1 neither (the item is not near the monster: left out), 0 the world matrix, 1 the local one. *apartW / *apartL: how far each puts it (-1 when
// that matrix did not read). The user's 0.179 run: the local one read as the world's put 'navi.skeleton's bow 3556 m off.
inline int HeldSpace(const float* local, const float* world, bool rooted, const float pos[3], float maxApart, float* apartW, float* apartL) {
    const auto apart = [&](const float* m) {
        if (!m) return -1.0f;
        const float dx = m[12] - pos[0], dy = m[13] - pos[1], dz = m[14] - pos[2];
        const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        return std::isfinite(d) ? d : -1.0f;
    };
    *apartW = apart(world);
    *apartL = apart(local);
    if (*apartW >= 0.0f && *apartW <= maxApart) return 0;
    if (rooted && *apartL >= 0.0f && *apartL <= maxApart) return 1;
    return -1;
}

// A rigid item's box (it has no skeleton: its meshes' bone bindings name its own bones, or a root): the union of all its meshes' binding boxes, in the
// model's own frame. False (and *why) when nothing reads.
template <class Read>
bool ItemBox(Read&& read, uintptr_t instance, BoneBox* out, const char** why) {
    *why = "";
    uintptr_t res = 0, data = 0;
    uint8_t state = 0;
    uint64_t count = 0;
    if (instance < 0x10000 || !read(instance + kInstanceResource, &res, sizeof res) || res < 0x10000 || !read(res + kResourceState, &state, 1)) { *why = "the model instance does not read"; return false; }
    if (state != kLoaded) { *why = "the model is not loaded yet"; return false; }
    if (!read(instance + kInstanceMeshes, &data, sizeof data) || !read(instance + kInstanceMeshes + 8, &count, sizeof count) || count < 1 || count > (uint64_t)grannymesh::kMaxMeshes ||
        data < 0x10000) { *why = "its meshes do not read"; return false; }
    float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
    int boxes = 0;
    for (uint64_t m = 0; m < count; ++m) {
        uintptr_t handle = 0, mesh = 0, binds = 0;
        int32_t nb = 0;
        if (!read(data + m * 8, &handle, sizeof handle) || handle < 0x10000 || !read(handle, &mesh, sizeof mesh) || mesh < 0x10000) continue;
        if (!read(mesh + kMeshBoneCount, &nb, sizeof nb) || nb < 1 || nb > kMaxBindings || !read(mesh + kMeshBones, &binds, sizeof binds) || binds < 0x10000) continue;
        for (int k = 0; k < nb; ++k) {
            float a[3], b[3];
            if (!read(binds + (uintptr_t)k * kBindingStride + kBindingMin, a, sizeof a) || !read(binds + (uintptr_t)k * kBindingStride + kBindingMax, b, sizeof b)) continue;
            bool ok = true;
            for (int c = 0; c < 3; ++c) ok = ok && std::isfinite(a[c]) && std::isfinite(b[c]) && b[c] >= a[c] && b[c] - a[c] < 1e4f;
            if (!ok || b[0] - a[0] + b[1] - a[1] + b[2] - a[2] <= 0.0f) continue;
            for (int c = 0; c < 3; ++c) { mn[c] = std::min(mn[c], a[c]); mx[c] = std::max(mx[c], b[c]); }
            ++boxes;
        }
    }
    if (!boxes) { *why = "its meshes have no bone boxes"; return false; }
    *out = BoneBox{};
    for (int c = 0; c < 3; ++c) { out->min[c] = mn[c]; out->max[c] = mx[c]; }
    return true;
}

// Is a model of this path a monster's (or an NPC's) body - not an item lying on the ground, gold, arrows, a missile, a hero? (The headset test:
// 'dropped_items', 'gold_small', 'arrows' came in among the monsters.) The path as the game names the model (its SkeletonInstance's name).
inline bool BodyPath(const char* path) {
    if (!path || !*path) return false;
    char low[160];
    size_t i = 0;
    for (; path[i] && i < sizeof low - 1; ++i) low[i] = (char)((path[i] >= 'A' && path[i] <= 'Z') ? path[i] + 32 : path[i]);
    low[i] = 0;
    if (std::strstr(low, "/player/") || std::strstr(low, "/item") || std::strstr(low, "/missile") || std::strstr(low, "dropped_items") || std::strstr(low, "gold_") ||
        std::strstr(low, "arrows"))
        return false;
    return true;   // (what is left is matched to a hostile monster by where it stands; the paths are logged once each - [debug] monster_rig_dump)
}

// ---- Which boxes are the body (the author's screenshot 2026-10-24: a zombie with "extra limbs" - two capsules floating beside it at the hips, a chain of
// small ones sticking out sideways from the right shoulder like a rope) ----
// A box rides with its bone: the bone's origin is at it or within it (a limb's box runs from its joint along it). A box far from its bone (farther from the
// origin than its own longest side) was not made in that bone's frame - drawn there it floats beside the body: left out.
inline bool AtItsBone(const BoneBox& b) {
    float d2 = 0.0f, longest = 0.0f;
    for (int c = 0; c < 3; ++c) {
        const float o = b.min[c] > 0.0f ? b.min[c] : b.max[c] < 0.0f ? -b.max[c] : 0.0f;   // the origin's distance to the box along this axis
        d2 += o * o;
        longest = std::max(longest, b.max[c] - b.min[c]);
    }
    return std::sqrt(d2) <= longest;
}
// A humanoid's body bones (its skeleton judged by vr/monster_humanoid.h): its limbs' bones (limbOf != 0), the trunk they hang from - every bone between
// them (and the head) and the root - the head and what is under it (a jaw, a helmet), and what hangs from a limb's END (a hand's fingers, a foot's toes) two
// bones down. Not a rope, a strip of cloth, a chain or a prop's bone branching off the trunk or a limb's middle. body[i]: the bone is the body's.
// bodyWord (may be null): each bone whose NAME says it is the body (vr/monster_humanoid.h BodyWord: head, neck, jaw, a trunk word, tail, wing, a limb word)
// is kept too, and every bone up to the root - the head is the head whichever bone the judge took for it (the zombie's head_bind_jnt and neck_bind_jnt
// were left out, the author's screenshot 2026-10-24), a tail is a limb (a quill rat's); a spike or a rope says none of it.
inline void BodyBones(const int16_t* parent, const int16_t* limbOf, int n, int headBone, bool* body, const uint8_t* bodyWord = nullptr) {
    const auto par = [&](int i) { const int p = parent[i]; return p >= 0 && p < n && p != i ? p : -1; };
    for (int i = 0; i < n; ++i) body[i] = false;
    const auto upFrom = [&](int i) { for (int g = 0; i >= 0 && g < n; ++g, i = par(i)) body[i] = true; };   // the bone and its ancestors
    bool hasLimbChild[kMaxBones] = {};
    for (int i = 0; i < n && i < kMaxBones; ++i)
        if (limbOf[i]) {
            upFrom(i);
            const int p = par(i);
            if (p >= 0 && limbOf[p] == limbOf[i]) hasLimbChild[p] = true;
        }
    if (headBone >= 0 && headBone < n) upFrom(headBone);
    if (bodyWord)
        for (int i = 0; i < n; ++i)
            if (bodyWord[i]) upFrom(i);
    for (int i = 0; i < n; ++i) {   // under the head; two bones down from a limb's end
        int a = par(i), g = 1;
        for (; a >= 0 && g <= n; a = par(a), ++g) {
            if (a == headBone) { body[i] = true; break; }
            if (g <= 2 && limbOf[a] && !hasLimbChild[a] && !limbOf[i]) {   // (a is a limb's last bone)
                bool between = true;   // nothing on the way that is a limb's or the head's own
                for (int b = par(i); b >= 0 && b != a; b = par(b)) between = between && !limbOf[b] && b != headBone;
                if (between) { body[i] = true; break; }
            }
        }
    }
}

// A capsule in the world (the units of the matrices: the game's world units).
struct Capsule {
    float a[3] = {}, b[3] = {};
    float radius = 0.0f;
    int bone = -1;
    int mirror = -1;   // the other side's bone (BoneBox::mirror)
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
    out->mirror = box.mirror;
    return std::isfinite(out->a[0] + out->a[1] + out->a[2] + out->b[0] + out->b[1] + out->b[2] + r) && r >= 0.0f;
}

// A limb bone whose box is not at its bone (AtItsBone false: Akara's robe - the vertices bound to her thighs and shins hang far from those bones, so her legs had
// no capsule between the hips and the feet, the headset 0.183): its capsule along the bone itself, from its origin to childOrigin (the farthest bone of the
// same limb hanging from it: the knee for a thigh, the foot for a shin), as thick as its box is across (CapsuleOf's radius) - never thicker than half the
// way to the other side's same bone (mirrorOrigin, may be null: two legs side by side do not overlap) nor than half its own length. bone: its matrix in the
// world. False when it has no length or the matrix is not one.
inline bool AlongBone(const float bone[16], const float childOrigin[3], const float* mirrorOrigin, const BoneBox& box, Capsule* out) {
    Capsule across;
    if (!CapsuleOf(bone, box, &across)) return false;
    const float a[3] = {bone[12], bone[13], bone[14]};
    float d2 = 0.0f;
    for (int k = 0; k < 3; ++k) d2 += (childOrigin[k] - a[k]) * (childOrigin[k] - a[k]);
    const float len = std::sqrt(d2);
    if (!(len > 1e-4f) || !std::isfinite(len)) return false;
    float r = std::min(across.radius, 0.5f * len);
    if (mirrorOrigin) {
        float m2 = 0.0f;
        for (int k = 0; k < 3; ++k) m2 += (mirrorOrigin[k] - a[k]) * (mirrorOrigin[k] - a[k]);
        if (m2 > 1e-8f) r = std::min(r, 0.5f * std::sqrt(m2));
    }
    for (int k = 0; k < 3; ++k) { out->a[k] = a[k]; out->b[k] = childOrigin[k]; }
    out->radius = r;
    out->bone = box.bone;
    out->mirror = box.mirror;
    return r > 0.0f;
}

// [weight] monster_shields: a held item's box as capsules - a weapon one along its longest side (CapsuleOf); a shield, a flat box, four side by side across
// its face (the contact knows capsules only): each along the longest side, as thick as the face's width over four (at least the box's own thickness),
// at an eighth, three eighths, five and seven eighths of the way across. Returns how many (up to max).
inline int HeldCapsules(const float bone[16], const BoneBox& box, bool shield, Capsule* out, int max) {
    if (max <= 0) return 0;
    if (!shield) return CapsuleOf(bone, box, &out[0]) ? 1 : 0;
    float s[3];
    for (int k = 0; k < 3; ++k) {
        s[k] = std::sqrt(bone[k * 4] * bone[k * 4] + bone[k * 4 + 1] * bone[k * 4 + 1] + bone[k * 4 + 2] * bone[k * 4 + 2]);
        if (!(s[k] > 1e-6f) || !std::isfinite(s[k])) return 0;
    }
    float c[3], e[3];
    for (int k = 0; k < 3; ++k) {
        c[k] = 0.5f * (box.min[k] + box.max[k]);
        e[k] = 0.5f * (box.max[k] - box.min[k]) * s[k];
    }
    int ax[3] = {0, 1, 2};   // longest, across, thinnest
    std::sort(ax, ax + 3, [&](int x, int y) { return e[x] > e[y]; });
    const int L = ax[0], U = ax[1];
    constexpr int kStrips = 4;
    const float r = std::max(e[ax[2]], e[U] / kStrips);
    const float half = std::max(0.0f, e[L] - r) / s[L];
    int n = 0;
    for (int q = 0; q < kStrips && n < max; ++q) {
        float p0[3] = {c[0], c[1], c[2]}, p1[3] = {c[0], c[1], c[2]};
        const float off = (-1.0f + (2.0f * q + 1.0f) / kStrips) * e[U] / s[U];
        p0[U] += off;
        p1[U] += off;
        p0[L] -= half;
        p1[L] += half;
        Capsule& o = out[n];
        Through(bone, p0, o.a);
        Through(bone, p1, o.b);
        o.radius = r;
        o.bone = box.bone;
        o.mirror = -1;
        if (std::isfinite(o.a[0] + o.a[1] + o.a[2] + o.b[0] + o.b[1] + o.b[2] + r)) ++n;
    }
    return n;
}

inline float Extent(const Capsule& c) {   // the capsule's longest side: its length plus both rounded ends
    const float d[3] = {c.b[0] - c.a[0], c.b[1] - c.a[1], c.b[2] - c.a[2]};
    return std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) + 2.0f * c.radius;
}

// The capsules of a rig, the trifles left out, the largest kMaxPerRig kept (in place; returns how many) - a left and right pair (Capsule::mirror) kept or
// left out together: a left shin is not dropped for a cape while its right one stays. height: the body's height in the same units (the highest capsule
// point over the lowest - the caller may give its own, e.g. from the game's MonStats; <= 0: worked out here).
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
    if (k <= kMaxPerRig) return k;
    // the largest first, each with its other side: a pair takes two places or none (a single may still take the last place)
    Capsule kept[kMaxPerRig];
    bool used[kMaxBoxes] = {};
    int nk = 0;
    for (int i = 0; i < k && i < kMaxBoxes && nk < kMaxPerRig; ++i) {
        if (used[i]) continue;
        int pair = -1;
        for (int j = i + 1; j < k && j < kMaxBoxes && pair < 0 && caps[i].mirror >= 0; ++j)
            if (!used[j] && caps[j].bone == caps[i].mirror) pair = j;
        if (pair >= 0 && nk + 2 > kMaxPerRig) {   // (no room for both: neither - the other is not taken alone further down)
            used[i] = used[pair] = true;
            continue;
        }
        used[i] = true;
        kept[nk++] = caps[i];
        if (pair >= 0) {
            used[pair] = true;
            kept[nk++] = caps[pair];
        }
    }
    for (int i = 0; i < nk; ++i) caps[i] = kept[i];
    return nk;
}

}  // namespace monsterrig
