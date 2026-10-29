// portal_oval.h - [input] flat_portal_no_oval (flat F2 / F3) and [render] portal_no_oval (VR F2 / F3 / F4): the town portal's opaque black oval is
// not drawn; its animated blue ring is. F1, the game's own view from above, keeps it.
// From sajmonsoft's flat fork (github.com/sotodeshi/d2r3dflat, docs CHANGES.md section 6, 2026-10): the oval is one mesh, turned once for the
// game's old 45-degree camera, while the ring's meshes turn to face the camera as they are born - from any other view the black oval stands
// still beside a ring that turns. Pure part; the test is cleanroom/tests/portal_oval_test.cpp, the game side vr/vrcam.cpp (HookMeshMaterials).
//
// Which meshes (his decoding of vfx_town_portal*.particles): the solid sphere models fx_mesh_portalSphere(_01, _convex) - 380 triangles, 230
// vertices - are the black centre; every other "portalsphere" model is the ring or the closing animation: the four
// ..._long_hole_vertexalpha* (the ring: a sphere with its middle cut out), ..._innerflow_3 and fx_mesh_townportal_lip_crawl (closing).
//
// How (adapted): the game builds a render mesh's materials once, at 0xFA1700(render mesh, model path, file, Granny mesh, pass) - its caller's
// render mesh is the first argument there (his build found it by scanning the stack for the register the caller keeps it in). The render
// mesh's triangle, vertex and sub-mesh triangle counts at +0x158, +0x1CC, +0x21C (his offsets, 3.3.0) are set to 0 while hidden and back
// to what they were in F1, in VR, or with the tick off. Every write is checked first: the three counts must still be the ones remembered (or
// the zeros we wrote) and the first bytes of the record unchanged - a render mesh the game freed and reused is never written.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace portaloval {

constexpr size_t kOffTri = 0x158, kOffVert = 0x1CC, kOffSub = 0x21C;
constexpr size_t kHeadBytes = 0x20;   // the start of the record, kept to know it is still the same one

// A model path (any case, '/' or '\\') of the portal's black centre: a "portalsphere" model that is not the ring's or the closing's.
inline bool IsOvalPath(const char* path) {
    if (!path || !path[0]) return false;
    char low[512];
    size_t n = 0;
    for (; path[n] && n + 1 < sizeof low; ++n) {
        unsigned char c = (unsigned char)path[n];
        if (c == '\\') c = '/';
        else if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + 32);
        low[n] = (char)c;
    }
    if (path[n]) return false;   // longer than any model path
    low[n] = 0;
    auto has = [&](const char* needle) { return strstr(low, needle) != nullptr; };
    if (has("innerflow") || has("lip_crawl") || has("long_hole")) return false;
    return has("portalsphere");
}

// The counts as found: a mesh's own (both triangle counts equal, a few to a few thousand, vertices below three per triangle).
struct Counts { uint32_t tri = 0, vert = 0, sub = 0; };
inline bool Plausible(const Counts& c) {
    return c.tri > 0 && c.tri == c.sub && c.tri <= 100000 && c.vert > 2 && c.vert <= 3 * c.tri;
}
inline bool Zero(const Counts& c) { return c.tri == 0 && c.vert == 0 && c.sub == 0; }

// One remembered render mesh: where, its own counts and the head of its record.
struct Entry {
    uintptr_t rm = 0;
    Counts own{};
    uint8_t head[kHeadBytes] = {};
};

// What to write to an entry now, given what is there. hide: zeros; else its own counts. False: leave it (not ours any more, or already so).
inline bool WriteNow(const Entry& e, const Counts& now, const uint8_t head[kHeadBytes], bool hide, Counts* out) {
    if (!e.rm || memcmp(head, e.head, kHeadBytes) != 0) return false;   // the record changed: freed, or another one
    const bool isOwn = now.tri == e.own.tri && now.vert == e.own.vert && now.sub == e.own.sub;
    if (!isOwn && !Zero(now)) return false;   // neither its counts nor our zeros: not ours to touch
    if (hide == Zero(now)) return false;      // already as wanted
    *out = hide ? Counts{} : e.own;
    return true;
}

}  // namespace portaloval
