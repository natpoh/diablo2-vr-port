// granny_geometry.h - [weight] world_contact: the triangles of a level piece's model (a Granny 2 .model file of the game's), in the model's own units, for the
// solids D2R VR Settings makes (shared/world_solids.h). Pure C++ over grannyremesh::File / Walk (shared/granny_remesh.h: the file read, its sections
// decompressed, every member found by name through the file's own type tree - never a fixed offset).
//
// What it takes of each mesh (Models' meshes are the file's root "Meshes"): PrimaryVertexData.Vertices' "Position" (Real32 x3, or a normalised Int16 x4 /
// Int8 x4 times the mesh's ExtendedData VertexScale - the model's units), PrimaryTopology's Indices (32-bit) or Indices16, its Name, its ExtendedData
// meshRenderLayer (1 when the file says none) and its first material's name. The positions are the mesh's bind pose in model space; nothing of the skeleton
// is applied (the world_solids catalogue keeps the meshes' own bounds beside the parts, and the plugin lays the parts by the renderer's box of the piece).
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "granny_remesh.h"

namespace grannygeom {

struct Mesh {
    std::string name, material;
    int32_t layer = 1;
    std::vector<float> pos;        // x y z, model units
    std::vector<uint32_t> tri;     // three indices into pos / 3 each
};

namespace detail {

using grannyremesh::Addr;
using grannyremesh::Walk;

// One component of a member, as a number (a normalised type to -1 .. 1 / 0 .. 1); false for a type that is not one.
inline bool Num(const Walk& g, Addr a, int type, int i, float* out) {
    switch (type) {
    case 10: *out = g.Get<float>(a.at(i * 4)); return true;
    case 11: *out = (float)g.Get<int8_t>(a.at(i)); return true;
    case 12: *out = (float)g.Get<uint8_t>(a.at(i)); return true;
    case 13: *out = (float)g.Get<int8_t>(a.at(i)) / 127.0f; return true;
    case 14: *out = (float)g.Get<uint8_t>(a.at(i)) / 255.0f; return true;
    case 15: *out = (float)g.Get<int16_t>(a.at(i * 2)); return true;
    case 16: *out = (float)g.Get<uint16_t>(a.at(i * 2)); return true;
    case 17: *out = (float)g.Get<int16_t>(a.at(i * 2)) / 32767.0f; return true;
    case 18: *out = (float)g.Get<uint16_t>(a.at(i * 2)) / 65535.0f; return true;
    case 19: *out = (float)g.Get<int32_t>(a.at(i * 4)); return true;
    case 20: *out = (float)g.Get<uint32_t>(a.at(i * 4)); return true;
    default: return false;
    }
}

}  // namespace detail

// Every mesh of the file. False (why) when the file has no meshes that read.
inline bool MeshesOf(grannyremesh::File& f, std::vector<Mesh>* out, std::string* why) {
    using namespace detail;
    auto no = [&](const char* w) { if (why) *why = w; return false; };
    out->clear();
    Walk g(f);
    const Addr rt{f.h.rootType.section, f.h.rootType.offset}, ro{f.h.rootObj.section, f.h.rootObj.offset};
    Addr t;
    const Addr meshes = g.Member(rt, ro, "Meshes", &t);
    if (!meshes.ok() || g.Kind(t) != 4) return no("no meshes");
    const Addr meshType = g.Ref(t), arr = g.Ptr(meshes.at(4));
    const int32_t n = g.Get<int32_t>(meshes);
    for (int32_t k = 0; k < n && k < 1024; ++k) {
        const Addr mesh = g.Ptr(arr.at((uint32_t)k * 8));
        if (!mesh.ok()) continue;
        Mesh m;
        Addr mt;
        m.name = g.Str(g.Ptr(g.Member(meshType, mesh, "Name", &mt)));
        // its scale and layer (ExtendedData)
        float scale = 1.0f;
        {
            Addr et, eo, e;
            if (grannyremesh::MeshExt(g, meshType, mesh, &et, &eo)) {
                const Addr vs = g.Member(et, eo, "VertexScale", &e);
                if (vs.ok() && g.Kind(e) == 10) scale = g.Get<float>(vs);
                const Addr ly = g.Member(et, eo, "meshRenderLayer", &e);
                if (ly.ok() && (g.Kind(e) == 19 || g.Kind(e) == 20)) m.layer = g.Get<int32_t>(ly);
            }
        }
        // its first material's name
        {
            Addr bt;
            const Addr mb = g.Member(meshType, mesh, "MaterialBindings", &bt);
            if (mb.ok() && g.Kind(bt) == 3 && g.Get<int32_t>(mb) > 0) {
                const Addr bind = g.Ptr(mb.at(4));
                Addr matT;
                const Addr matRef = g.Member(g.Ref(bt), bind, "Material", &matT);
                const Addr mat = g.Ptr(matRef);
                if (mat.ok()) {
                    Addr nt;
                    m.material = g.Str(g.Ptr(g.Member(g.Ref(matT), mat, "Name", &nt)));
                }
            }
        }
        // the vertices' positions
        Addr pt;
        const Addr pvd = g.Ptr(g.Member(meshType, mesh, "PrimaryVertexData", &pt));
        if (!pvd.ok()) continue;
        Addr vt;
        const Addr verts = g.Member(g.Ref(pt), pvd, "Vertices", &vt);
        if (!verts.ok() || g.Kind(vt) != 7) continue;
        const Addr vtype = g.Ptr(verts);
        const int32_t nv = g.Get<int32_t>(verts.at(8));
        const Addr vdata = g.Ptr(verts.at(12));
        const int stride = g.StructBytes(vtype);
        if (nv < 1 || nv > (1 << 22) || stride <= 0 || !g.In(vdata, (uint32_t)nv * (uint32_t)stride)) continue;
        Addr posT;
        const Addr pos0 = g.Member(vtype, vdata, "Position", &posT);
        if (!pos0.ok()) continue;
        const int ptype = g.Kind(posT), width = g.Width(posT) ? g.Width(posT) : 1;
        if (width < 3) continue;
        const bool normalised = ptype == 13 || ptype == 17;
        m.pos.resize((size_t)nv * 3);
        bool ok = true;
        for (int32_t v = 0; v < nv && ok; ++v) {
            const Addr at = pos0.at((uint32_t)v * (uint32_t)stride);
            for (int c = 0; c < 3 && ok; ++c) {
                float x = 0;
                ok = Num(g, at, ptype, c, &x);
                m.pos[(size_t)v * 3 + c] = normalised ? x * scale : x;
            }
        }
        if (!ok) continue;
        // the triangles
        Addr tt;
        const Addr topo = g.Ptr(g.Member(meshType, mesh, "PrimaryTopology", &tt));
        if (!topo.ok()) continue;
        Addr i32t, i16t;
        const Addr i32 = g.Member(g.Ref(tt), topo, "Indices", &i32t), i16 = g.Member(g.Ref(tt), topo, "Indices16", &i16t);
        if (i32.ok() && g.Get<int32_t>(i32) > 0) {
            const int32_t ni = g.Get<int32_t>(i32);
            const Addr d = g.Ptr(i32.at(4));
            if (ni % 3 || !g.In(d, (uint32_t)ni * 4)) continue;
            m.tri.resize((size_t)ni);
            std::memcpy(m.tri.data(), f.sec[d.s].data() + d.o, (size_t)ni * 4);
        } else if (i16.ok() && g.Get<int32_t>(i16) > 0) {
            const int32_t ni = g.Get<int32_t>(i16);
            const Addr d = g.Ptr(i16.at(4));
            if (ni % 3 || !g.In(d, (uint32_t)ni * 2)) continue;
            m.tri.resize((size_t)ni);
            for (int32_t i = 0; i < ni; ++i) m.tri[(size_t)i] = g.Get<uint16_t>(d.at((uint32_t)i * 2));
        } else {
            continue;
        }
        bool inRange = true;
        for (uint32_t i : m.tri) inRange = inRange && i < (uint32_t)nv;
        if (!inRange) continue;
        out->push_back(std::move(m));
    }
    if (out->empty()) return no("no mesh's triangles read");
    return true;
}

}  // namespace grannygeom
