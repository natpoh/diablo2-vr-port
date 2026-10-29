// granny_mesh_test - [bow] arrow_fx (vr/granny_mesh.h) on synthetic memory laid out as the game's Granny model is: a model instance, its model
// with two meshes (the bow and its arrow), the arrow mesh's ExtendedData type definition and object - the numbers of the installed short bow
// (weapon_short_bow_arrowShape: bbMin -0.057 -0.079 -0.581, bbMax 0.055 0.079 2.309). The box must be found by its fields' names, whatever
// comes before them, and nothing must be read from a model that is not the skeleton's, a mesh that is not an arrow's or a box that is not one.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#include "granny_mesh.h"

static int g_fail = 0, g_checks = 0;
static void Expect(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_fail; std::printf("FAIL: %s\n", what); }
}

// A memory of its own: addresses from 0x100000 up, reads outside it fail (as a guarded read of a bad pointer would).
struct Mem {
    std::vector<unsigned char> bytes = std::vector<unsigned char>(0x4000, 0);
    static constexpr uintptr_t kBase = 0x100000;
    uintptr_t At(size_t off) const { return kBase + off; }
    template <class T> void Put(size_t off, T v) { std::memcpy(&bytes[off], &v, sizeof v); }
    void PutStr(size_t off, const char* s) { std::memcpy(&bytes[off], s, std::strlen(s) + 1); }
    bool Read(uintptr_t at, void* to, size_t n) const {
        if (at < kBase || at + n > kBase + bytes.size()) return false;
        std::memcpy(to, &bytes[at - kBase], n);
        return true;
    }
};

// Offsets of the pieces in the synthetic memory.
enum : size_t {
    kInst = 0x100, kModel = 0x200, kSkel = 0x300, kBindings = 0x400, kBowMesh = 0x500, kArrowMesh = 0x600, kDefs = 0x800, kObject = 0xC00, kStrings = 0x1000,
};

static Mem Make(bool arrowFirst = false) {
    Mem m;
    m.Put<uintptr_t>(kInst + grannymesh::kInstanceModel, m.At(kModel));
    m.Put<uintptr_t>(kModel + grannymesh::kModelSkeleton, m.At(kSkel));
    m.Put<int32_t>(kModel + grannymesh::kModelMeshCount, 2);
    m.Put<uintptr_t>(kModel + grannymesh::kModelMeshes, m.At(kBindings));
    m.Put<uintptr_t>(kBindings + (arrowFirst ? 8 : 0), m.At(kBowMesh));
    m.Put<uintptr_t>(kBindings + (arrowFirst ? 0 : 8), m.At(kArrowMesh));
    m.PutStr(kStrings + 0x00, "weapon_short_bowShape");
    m.PutStr(kStrings + 0x40, "weapon_short_bow_arrowShape");
    m.Put<uintptr_t>(kBowMesh + grannymesh::kMeshName, m.At(kStrings + 0x00));
    m.Put<uintptr_t>(kArrowMesh + grannymesh::kMeshName, m.At(kStrings + 0x40));
    m.Put<uintptr_t>(kArrowMesh + grannymesh::kMeshExtType, m.At(kDefs));
    m.Put<uintptr_t>(kArrowMesh + grannymesh::kMeshExtObject, m.At(kObject));
    // the ExtendedData type as the game's files have it: MaterialSets (array, 12), bbMin (Real32 x3), bbMax (Real32 x3), meshRenderLayer (Int32),
    // ReflectivePlanes (array, 12), VertexScale (Real32) - and the end
    struct F { int type; const char* name; int width; };
    const F fields[] = {{3, "MaterialSets", 0}, {10, "bbMin", 3}, {10, "bbMax", 3}, {19, "meshRenderLayer", 0}, {3, "ReflectivePlanes", 0}, {10, "VertexScale", 0}};
    size_t name = kStrings + 0x100;
    for (size_t i = 0; i < sizeof fields / sizeof fields[0]; ++i) {
        const size_t d = kDefs + i * grannymesh::kDefSize;
        m.Put<int32_t>(d, fields[i].type);
        m.PutStr(name, fields[i].name);
        m.Put<uintptr_t>(d + grannymesh::kDefName, m.At(name));
        m.Put<int32_t>(d + grannymesh::kDefWidth, fields[i].width);
        name += 0x20;
    }
    m.Put<int32_t>(kDefs + 6 * grannymesh::kDefSize, 0);
    const float mn[3] = {-0.057f, -0.079f, -0.581f}, mx[3] = {0.055f, 0.079f, 2.309f};
    std::memcpy(&m.bytes[kObject + 12], mn, sizeof mn);   // after MaterialSets (12 bytes)
    std::memcpy(&m.bytes[kObject + 24], mx, sizeof mx);
    return m;
}

int main() {
    auto reader = [](const Mem& m) { return [&m](uintptr_t at, void* to, size_t n) { return m.Read(at, to, n); }; };
    {
        const Mem m = Make();
        grannymesh::Box b;
        Expect(grannymesh::ArrowBox(reader(m), m.At(kInst), m.At(kSkel), &b), "the arrow mesh's box is read");
        Expect(b.max[2] == 2.309f && b.min[2] == -0.581f && b.max[0] == 0.055f && b.min[1] == -0.079f, "its numbers, from the bbMin / bbMax fields");
        Expect(std::strcmp(b.mesh, "weapon_short_bow_arrowShape") == 0, "the mesh it came from");
        uint32_t at = 0;
        Expect(grannymesh::FieldOffset(reader(m), m.At(kDefs), "bbMax", 10, 3, &at) && at == 24, "bbMax at 24: MaterialSets (12) and bbMin (12) before it");
        Expect(grannymesh::FieldOffset(reader(m), m.At(kDefs), "VertexScale", 10, 1, &at) && at == 52, "VertexScale at 52, after the second array");
        Expect(!grannymesh::FieldOffset(reader(m), m.At(kDefs), "bbMax", 10, 2, &at) && !grannymesh::FieldOffset(reader(m), m.At(kDefs), "bbMax", 19, 3, &at),
               "a field of another width or type is not that one");
        Expect(!grannymesh::FieldOffset(reader(m), m.At(kDefs), "bbCentre", 10, 3, &at), "a field that is not there");
    }
    {   // the arrow mesh first in the list: found as well
        const Mem m = Make(true);
        grannymesh::Box b;
        Expect(grannymesh::ArrowBox(reader(m), m.At(kInst), m.At(kSkel), &b) && b.max[2] == 2.309f, "the arrow mesh found wherever it is in the list");
    }
    {   // nothing from what is not this
        Mem m = Make();
        grannymesh::Box b;
        Expect(!grannymesh::ArrowBox(reader(m), m.At(kInst), m.At(kSkel) + 8, &b), "a model whose skeleton is another: nothing");
        Expect(!grannymesh::ArrowBox(reader(m), 0x10, m.At(kSkel), &b) && !grannymesh::ArrowBox(reader(m), m.At(kInst), m.At(kSkel), nullptr), "no instance, no output");
        Mem noArrow = Make();
        noArrow.PutStr(kStrings + 0x40, "weapon_short_bow_stringShape");
        Expect(!grannymesh::ArrowBox(reader(noArrow), noArrow.At(kInst), noArrow.At(kSkel), &b), "no mesh named for an arrow: nothing");
        Mem upper = Make();
        upper.PutStr(kStrings + 0x40, "Weapon_Long_Bow_ARROWShape");
        Expect(grannymesh::ArrowBox(reader(upper), upper.At(kInst), upper.At(kSkel), &b), "an arrow named in capitals is an arrow");
        Mem many = Make();
        many.Put<int32_t>(kModel + grannymesh::kModelMeshCount, 5000);
        Expect(!grannymesh::ArrowBox(reader(many), many.At(kInst), many.At(kSkel), &b), "a mesh count that is not one: nothing");
        Mem noMax = Make();
        noMax.PutStr(kStrings + 0x100 + 2 * 0x20, "bbTop");
        Expect(!grannymesh::ArrowBox(reader(noMax), noMax.At(kInst), noMax.At(kSkel), &b), "no bbMax field: nothing");
        Mem nan = Make();
        const float bad = std::nanf("");
        std::memcpy(&nan.bytes[kObject + 24 + 8], &bad, sizeof bad);
        Expect(!grannymesh::ArrowBox(reader(nan), nan.At(kInst), nan.At(kSkel), &b), "a NaN in the box: nothing");
        Mem inverted = Make();
        const float lo = 3.0f;
        std::memcpy(&inverted.bytes[kObject + 12 + 8], &lo, sizeof lo);
        Expect(!grannymesh::ArrowBox(reader(inverted), inverted.At(kInst), inverted.At(kSkel), &b), "a box whose min is above its max: nothing");
        Mem badType = Make();
        badType.Put<int32_t>(kDefs, 99);
        Expect(!grannymesh::ArrowBox(reader(badType), badType.At(kInst), badType.At(kSkel), &b), "a member type not known before the box: nothing (its size is not known)");
        Mem badName = Make();
        badName.Put<uintptr_t>(kArrowMesh + grannymesh::kMeshName, 0x20);
        Expect(!grannymesh::ArrowBox(reader(badName), badName.At(kInst), badName.At(kSkel), &b), "a name pointer that does not read: nothing");
    }
    {   // [shield] heads_hang: every mesh's box, whatever its name
        Mem m = Make();
        grannymesh::Box boxes[4];
        Expect(grannymesh::MeshBoxes(reader(m), m.At(kInst), m.At(kSkel), boxes, 4) == 1, "all boxes: the one mesh that has a box (the bow mesh has no ExtendedData)");
        Expect(boxes[0].max[2] == 2.309f && std::strcmp(boxes[0].mesh, "weapon_short_bow_arrowShape") == 0, "its numbers and its mesh");
        // the bow mesh given a box of its own (the same type definition, another object)
        m.Put<uintptr_t>(kBowMesh + grannymesh::kMeshExtType, m.At(kDefs));
        m.Put<uintptr_t>(kBowMesh + grannymesh::kMeshExtObject, m.At(kObject + 0x100));
        const float mn[3] = {-0.3f, -1.4f, -0.2f}, mx[3] = {0.3f, 0.5f, 0.2f};
        std::memcpy(&m.bytes[kObject + 0x100 + 12], mn, sizeof mn);
        std::memcpy(&m.bytes[kObject + 0x100 + 24], mx, sizeof mx);
        Expect(grannymesh::MeshBoxes(reader(m), m.At(kInst), m.At(kSkel), boxes, 4) == 2, "two meshes, two boxes");
        Expect(boxes[0].min[1] == -1.4f && boxes[1].max[2] == 2.309f, "in the model's order");
        Expect(grannymesh::MeshBoxes(reader(m), m.At(kInst), m.At(kSkel), boxes, 1) == 1, "no more than asked for");
        Expect(grannymesh::MeshBoxes(reader(m), m.At(kInst), m.At(kSkel) + 8, boxes, 4) == 0, "a model whose skeleton is another: none");
        Expect(grannymesh::MeshBoxes(reader(m), m.At(kInst), m.At(kSkel), nullptr, 4) == 0 && grannymesh::MeshBoxes(reader(m), 0x10, m.At(kSkel), boxes, 4) == 0, "no output, no instance: none");
        const float bad = std::nanf("");
        std::memcpy(&m.bytes[kObject + 0x100 + 12], &bad, sizeof bad);
        Expect(grannymesh::MeshBoxes(reader(m), m.At(kInst), m.At(kSkel), boxes, 4) == 1 && boxes[0].max[2] == 2.309f, "a box that is not numbers is left out, the others kept");
        const char* why = nullptr;
        Mem none = Make();
        none.Put<int32_t>(kModel + grannymesh::kModelMeshCount, 0);   // a Necromancer head's .skeleton model: no meshes
        Expect(grannymesh::MeshBoxes(reader(none), none.At(kInst), none.At(kSkel), boxes, 4, &why) == 0 && why && std::strcmp(why, "the instance's model has no meshes") == 0,
               "a model with no meshes: none, and why");
        Expect(grannymesh::MeshBoxes(reader(m), m.At(kInst), m.At(kSkel) + 8, boxes, 4, &why) == 0 && why && std::strstr(why, "another skeleton"), "another skeleton's: why");
        Expect(grannymesh::MeshBoxes(reader(m), m.At(kInst), m.At(kSkel), boxes, 4, &why) == 1 && !why, "read: no why");
    }
    std::printf(g_fail ? "%d of %d FAILED\n" : "granny mesh: all %d checks passed\n", g_fail ? g_fail : g_checks, g_checks);
    return g_fail ? 1 : 0;
}
