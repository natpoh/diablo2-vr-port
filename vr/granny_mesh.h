// granny_mesh.h - [bow] arrow_fx: the box of a bow's arrow mesh, read off the game's own Granny model in memory (the model the bow's skeleton
// comes from). Pure: every read goes through the caller's guarded reader, so cleanroom/tests/granny_mesh_test.cpp drives it on synthetic memory.
//
// Why: the arrow of a bow is a mesh of its own ("weapon_short_bow_arrowShape"), skinned to the arrow bones, and those bones sit at the nock -
// the arrowhead is nowhere among them. The mesh's ExtendedData carries its bounding box (bbMin / bbMax) in the bow's bind frame, the frame the
// bones' bind pose is in: its far end along the arrow is the arrowhead. The game's bows (looked at in the installed 3.3.93787 files: short,
// long, hunter's) all have the arrow from z -0.58 (the nock) to +2.31.
//
// The layout is Granny's own (LE64, packed to four bytes), as the model files' type definitions give it:
//   granny_model_instance  +0x00 granny_model*                      (the skeleton hook's this+0x08 is the instance; its +0x08 is the skeleton)
//   granny_model           +0x08 Skeleton*, +0x54 int32 MeshBindingCount, +0x58 granny_model_mesh_binding* (stride 8: +0 granny_mesh*)
//   granny_mesh            +0x00 char* Name, +0x3C ExtendedData: +0x3C granny_data_type_definition*, +0x44 void* object
//   granny_data_type_definition (stride 44): +0 int32 Type (0 ends), +4 char* Name, +20 int32 ArrayWidth
// The box's offset in the ExtendedData object is not assumed: the fields of its own type definition are walked and summed (bbMax is Real32 x 3).
#pragma once

#include <cstddef>
#include <cstdint>
#include <cctype>
#include <cstring>

namespace grannymesh {

constexpr size_t kInstanceModel = 0x00;
constexpr size_t kModelSkeleton = 0x08, kModelMeshCount = 0x54, kModelMeshes = 0x58, kBindingStride = 8;
constexpr size_t kMeshName = 0x00, kMeshExtType = 0x3C, kMeshExtObject = 0x44;
constexpr size_t kDefSize = 44, kDefName = 4, kDefWidth = 20;
constexpr int kReal32 = 10;
constexpr int kMaxMeshes = 32, kMaxFields = 64;

// The bytes one element of a Granny member type takes (granny_member_type); 0 for one not known.
inline int TypeBytes(int type) {
    switch (type) {
        case 2: return 8;   case 3: return 12;  case 4: return 12;  case 5: return 16;  case 7: return 20;  case 8: return 8;  case 9: return 68;
        case 10: return 4;  case 11: case 12: case 13: case 14: return 1;
        case 15: case 16: case 17: case 18: case 21: return 2;
        case 19: case 20: return 4;  case 22: return 8;
        default: return 0;
    }
}

// A string of the game's, read with `read(address, out, bytes)` (true when the bytes read), up to N - 1 characters.
template <class Read, size_t N>
bool String(Read&& read, uintptr_t at, char (&out)[N]) {
    if (at < 0x10000) return false;
    for (size_t i = 0; i < N; ++i) {
        if (!read(at + i, &out[i], 1)) return false;
        if (!out[i]) return true;
    }
    out[N - 1] = 0;
    return false;
}

inline bool HasArrow(const char* s) {
    char low[96];
    size_t i = 0;
    for (; s[i] && i < sizeof low - 1; ++i) low[i] = (char)std::tolower((unsigned char)s[i]);
    low[i] = 0;
    return std::strstr(low, "arrow") != nullptr;
}

// The byte offset of the member `name` (of the given type and width) in an object of the type `def`; false when it is not there.
template <class Read>
bool FieldOffset(Read&& read, uintptr_t def, const char* name, int wantType, int wantWidth, uint32_t* offset) {
    if (def < 0x10000) return false;
    uint32_t at = 0;
    for (int i = 0; i < kMaxFields; ++i) {
        const uintptr_t d = def + (uintptr_t)i * kDefSize;
        int32_t type = 0, width = 0;
        uintptr_t namePtr = 0;
        if (!read(d, &type, sizeof type)) return false;
        if (type == 0) return false;
        if (!read(d + kDefName, &namePtr, sizeof namePtr) || !read(d + kDefWidth, &width, sizeof width)) return false;
        const int bytes = TypeBytes(type);
        if (bytes == 0 || width < 0 || width > 64) return false;
        char n[64];
        if (String(read, namePtr, n) && type == wantType && (width ? width : 1) == wantWidth && std::strcmp(n, name) == 0) { *offset = at; return true; }
        at += (uint32_t)(bytes * (width ? width : 1));
    }
    return false;
}

// The arrow mesh's box in the bow's bind frame, from the model instance `instance` whose skeleton is `skeleton` (both as the skeleton hook
// has them: this+0x08 and its +0x08). False when the model is not that skeleton's, has no mesh named for an arrow, or the box does not read.
struct Box { float min[3] = {}, max[3] = {}; char mesh[96] = {}; };
template <class Read>
bool ArrowBox(Read&& read, uintptr_t instance, uintptr_t skeleton, Box* out) {
    uintptr_t model = 0, modelSkeleton = 0, meshes = 0;
    int32_t count = 0;
    if (!out || instance < 0x10000 || !read(instance + kInstanceModel, &model, sizeof model) || model < 0x10000) return false;
    if (!read(model + kModelSkeleton, &modelSkeleton, sizeof modelSkeleton) || modelSkeleton != skeleton) return false;   // the model the bones come from
    if (!read(model + kModelMeshCount, &count, sizeof count) || count < 1 || count > kMaxMeshes) return false;
    if (!read(model + kModelMeshes, &meshes, sizeof meshes) || meshes < 0x10000) return false;
    for (int i = 0; i < count; ++i) {
        uintptr_t mesh = 0, namePtr = 0, def = 0, object = 0;
        if (!read(meshes + (uintptr_t)i * kBindingStride, &mesh, sizeof mesh) || mesh < 0x10000) continue;
        char name[96];
        if (!read(mesh + kMeshName, &namePtr, sizeof namePtr) || !String(read, namePtr, name) || !HasArrow(name)) continue;
        if (!read(mesh + kMeshExtType, &def, sizeof def) || !read(mesh + kMeshExtObject, &object, sizeof object) || object < 0x10000) continue;
        uint32_t minAt = 0, maxAt = 0;
        if (!FieldOffset(read, def, "bbMin", kReal32, 3, &minAt) || !FieldOffset(read, def, "bbMax", kReal32, 3, &maxAt)) continue;
        Box b;
        if (!read(object + minAt, b.min, sizeof b.min) || !read(object + maxAt, b.max, sizeof b.max)) continue;
        bool ok = true;
        for (int k = 0; k < 3; ++k) ok = ok && b.min[k] == b.min[k] && b.max[k] == b.max[k] && b.max[k] >= b.min[k] && b.max[k] - b.min[k] < 1e4f;
        if (!ok) continue;
        std::memcpy(b.mesh, name, sizeof b.mesh);
        *out = b;
        return true;
    }
    return false;
}

// [shield] heads_hang (vr/head_hang.h): the boxes of every mesh of the model, as ArrowBox reads one - up to `max` of them, in the model's
// order. The number read (a mesh whose box does not read is left out); 0 when the model is not that skeleton's or has no meshes - `why`
// (optional) says which. (A Necromancer's head is posed by a skeleton of its own .skeleton file, whose model has no meshes: none there.)
template <class Read>
int MeshBoxes(Read&& read, uintptr_t instance, uintptr_t skeleton, Box* out, int max, const char** why = nullptr) {
    auto none = [&](const char* w) { if (why) *why = w; return 0; };
    uintptr_t model = 0, modelSkeleton = 0, meshes = 0;
    int32_t count = 0;
    if (!out || max <= 0 || instance < 0x10000 || !read(instance + kInstanceModel, &model, sizeof model) || model < 0x10000) return none("no model instance");
    if (!read(model + kModelSkeleton, &modelSkeleton, sizeof modelSkeleton) || modelSkeleton != skeleton) return none("the instance's model is another skeleton's");
    if (!read(model + kModelMeshCount, &count, sizeof count) || count < 1 || count > kMaxMeshes) return none(count == 0 ? "the instance's model has no meshes" : "the model's mesh count is not one");
    if (!read(model + kModelMeshes, &meshes, sizeof meshes) || meshes < 0x10000) return none("the model's mesh list does not read");
    int got = 0;
    for (int i = 0; i < count && got < max; ++i) {
        uintptr_t mesh = 0, namePtr = 0, def = 0, object = 0;
        if (!read(meshes + (uintptr_t)i * kBindingStride, &mesh, sizeof mesh) || mesh < 0x10000) continue;
        if (!read(mesh + kMeshExtType, &def, sizeof def) || !read(mesh + kMeshExtObject, &object, sizeof object) || object < 0x10000) continue;
        uint32_t minAt = 0, maxAt = 0;
        if (!FieldOffset(read, def, "bbMin", kReal32, 3, &minAt) || !FieldOffset(read, def, "bbMax", kReal32, 3, &maxAt)) continue;
        Box b;
        if (!read(object + minAt, b.min, sizeof b.min) || !read(object + maxAt, b.max, sizeof b.max)) continue;
        bool ok = true;
        for (int k = 0; k < 3; ++k) ok = ok && b.min[k] == b.min[k] && b.max[k] == b.max[k] && b.max[k] >= b.min[k] && b.max[k] - b.min[k] < 1e4f;
        if (!ok) continue;
        char name[96] = {};
        if (read(mesh + kMeshName, &namePtr, sizeof namePtr)) String(read, namePtr, name);
        std::memcpy(b.mesh, name, sizeof b.mesh);
        out[got++] = b;
    }
    if (why) *why = got ? nullptr : "no mesh's box read";
    return got;
}

}  // namespace grannymesh
