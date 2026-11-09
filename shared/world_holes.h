// world_holes.h - [holes] the world's open edges: where a model of the game's ends in the air because the isometric camera never looked
// there (Serg 2026-11-09, the bridge out of the Rogue Encampment: "на мосту дыры ... залатать текстурами"). Pure C++ over
// grannyremesh::File / Walk (shared/granny_remesh.h: the file read, its sections decompressed, every member found by name through the
// file's own type tree - never a fixed offset). Used by tools/world_holes (the survey) and by D2R VR Settings (the patches' models).
//
// What it reads of a model (its root "Meshes", every one): each vertex's Position (normalised int16 x4 times ExtendedData VertexScale, or
// Real32), Normal, Tangent and TextureCoordinates0 whatever their stored kind; the topology's Indices16 / Indices and its material Groups
// (MaterialIndex, TriFirst, TriCount); each binding's material name; meshRenderLayer. The positions are the bind pose in model space - the
// game's level pieces have one bone with an identity transform (checked: Bones[0] reported by Skeleton()).
//
// The analysis (Boundaries): vertices welded by position (a model splits a vertex wherever its uv or normal changes - those seams are
// not holes), every undirected edge counted over all the mesh's triangles; an edge one triangle uses is a boundary edge (an edge more than
// two use: "non-manifold", counted). Boundary edges are chained into runs (closed loops, or open chains where three or more meet). Each
// run gets: its length, bounds, the material of the triangles along it, and its open side - the mean, over its edges, of the direction in
// the triangle's plane that points out of the triangle across the edge (where the surface stops), and the mean normal of those triangles.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "granny_remesh.h"

namespace worldholes {

struct Vtx {
    float p[3] = {};    // model units
    float n[3] = {};    // unit normal (0 when the file has none)
    float t[4] = {};    // tangent, w = handedness
    float uv[2] = {};   // TextureCoordinates0
};

struct Group {
    int32_t material = 0;        // index into MaterialBindings
    uint32_t first = 0, count = 0;   // triangles
    std::string materialName;
    std::string technique;       // the material's ExtendedData "technique" (StandardMaterial, TerrainMaterial, GrungeMaterial ...)
};

struct MeshData {
    std::string name;
    float scale = 1.0f;           // VertexScale (1 when stored as Real32)
    int32_t layer = 1;            // meshRenderLayer
    std::vector<Vtx> v;
    std::vector<uint32_t> idx;    // triangles
    std::vector<Group> groups;
    // the vertex type's members as stored: name and Granny kind (13 snorm8, 17 snorm16, 21 half, 10 real32 ...)
    std::vector<std::pair<std::string, int>> members;
    int stride = 0;
};

namespace detail {

using grannyremesh::Addr;
using grannyremesh::Walk;

inline float HalfToFloat(uint16_t h) {
    const uint32_t s = (uint32_t)(h & 0x8000u) << 16;
    int e = (h >> 10) & 0x1F;
    uint32_t m = h & 0x3FFu;
    uint32_t x;
    if (e == 0) {
        if (!m) x = s;
        else {   // subnormal
            e = 1;
            while (!(m & 0x400u)) { m <<= 1; --e; }
            m &= 0x3FFu;
            x = s | (uint32_t)(e + 127 - 15) << 23 | m << 13;
        }
    } else if (e == 31) {
        x = s | 0x7F800000u | m << 13;
    } else {
        x = s | (uint32_t)(e + 127 - 15) << 23 | m << 13;
    }
    float f;
    std::memcpy(&f, &x, 4);
    return f;
}

// Component i of a member of Granny kind `type` at `a`, as a number (normalised kinds to -1 .. 1 / 0 .. 1). False: not a number kind.
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
    case 21: *out = HalfToFloat(g.Get<uint16_t>(a.at(i * 2))); return true;
    default: return false;
    }
}

}  // namespace detail

// Every mesh of the file's root "Meshes". False (why) when none reads.
inline bool ReadMeshes(grannyremesh::File& f, std::vector<MeshData>* out, std::string* why) {
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
        MeshData m;
        Addr mt;
        m.name = g.Str(g.Ptr(g.Member(meshType, mesh, "Name", &mt)));
        {
            Addr et, eo, e;
            if (grannyremesh::MeshExt(g, meshType, mesh, &et, &eo)) {
                const Addr vs = g.Member(et, eo, "VertexScale", &e);
                if (vs.ok() && g.Kind(e) == 10) m.scale = g.Get<float>(vs);
                const Addr ly = g.Member(et, eo, "meshRenderLayer", &e);
                if (ly.ok() && (g.Kind(e) == 19 || g.Kind(e) == 20)) m.layer = g.Get<int32_t>(ly);
            }
        }
        // the materials' names and techniques, by binding index
        std::vector<std::string> mats, techs;
        {
            Addr bt;
            const Addr mb = g.Member(meshType, mesh, "MaterialBindings", &bt);
            if (mb.ok() && g.Kind(bt) == 3) {
                const int32_t nb = g.Get<int32_t>(mb);
                const Addr bindType = g.Ref(bt), barr = g.Ptr(mb.at(4));
                const int bstride = g.StructBytes(bindType);
                for (int32_t b = 0; b < nb && b < 256 && bstride > 0; ++b) {
                    Addr matT;
                    const Addr matRef = g.Member(bindType, barr.at((uint32_t)(b * bstride)), "Material", &matT);
                    const Addr mat = g.Ptr(matRef);
                    std::string name, tech;
                    if (mat.ok()) {
                        Addr nt, xt;
                        name = g.Str(g.Ptr(g.Member(g.Ref(matT), mat, "Name", &nt)));
                        const Addr ext = g.Member(g.Ref(matT), mat, "ExtendedData", &xt);
                        if (ext.ok() && g.Kind(xt) == 5) {
                            const Addr et = g.Ptr(ext), eo = g.Ptr(ext.at(8));
                            Addr tt2;
                            const Addr tq = g.Member(et, eo, "technique", &tt2);
                            if (tq.ok() && g.Kind(tt2) == 8) tech = g.Str(g.Ptr(tq));
                        }
                    }
                    mats.push_back(name);
                    techs.push_back(tech);
                }
            }
        }
        // the vertices
        Addr pt;
        const Addr pvd = g.Ptr(g.Member(meshType, mesh, "PrimaryVertexData", &pt));
        if (!pvd.ok()) continue;
        Addr vt;
        const Addr verts = g.Member(g.Ref(pt), pvd, "Vertices", &vt);
        if (!verts.ok() || g.Kind(vt) != 7) continue;
        const Addr vtype = g.Ptr(verts);
        const int32_t nv = g.Get<int32_t>(verts.at(8));
        const Addr vdata = g.Ptr(verts.at(12));
        m.stride = g.StructBytes(vtype);
        if (nv < 1 || nv > (1 << 22) || m.stride <= 0 || !g.In(vdata, (uint32_t)nv * (uint32_t)m.stride)) continue;
        for (Addr a = vtype; g.In(a, 44) && g.Kind(a) != 0 && m.members.size() < 32; a = a.at(44)) m.members.push_back({g.Name(a), g.Kind(a)});
        Addr posT, nrmT, tanT, uvT;
        const Addr pos0 = g.Member(vtype, vdata, "Position", &posT);
        const Addr nrm0 = g.Member(vtype, vdata, "Normal", &nrmT);
        const Addr tan0 = g.Member(vtype, vdata, "Tangent", &tanT);
        const Addr uv0 = g.Member(vtype, vdata, "TextureCoordinates0", &uvT);
        if (!pos0.ok()) continue;
        const int ptype = g.Kind(posT);
        const bool normalised = ptype == 13 || ptype == 17;
        if (!normalised) m.scale = 1.0f;
        m.v.resize((size_t)nv);
        bool ok = true;
        for (int32_t i = 0; i < nv && ok; ++i) {
            Vtx& x = m.v[(size_t)i];
            const uint32_t d = (uint32_t)i * (uint32_t)m.stride;
            for (int c = 0; c < 3 && ok; ++c) {
                ok = Num(g, pos0.at(d), ptype, c, &x.p[c]);
                if (normalised) x.p[c] *= m.scale;
            }
            if (nrm0.ok()) for (int c = 0; c < 3; ++c) Num(g, nrm0.at(d), g.Kind(nrmT), c, &x.n[c]);
            if (tan0.ok()) for (int c = 0; c < 4 && c < std::max(1, g.Width(tanT)); ++c) Num(g, tan0.at(d), g.Kind(tanT), c, &x.t[c]);
            if (uv0.ok()) for (int c = 0; c < 2; ++c) Num(g, uv0.at(d), g.Kind(uvT), c, &x.uv[c]);
            const float l = std::sqrt(x.n[0] * x.n[0] + x.n[1] * x.n[1] + x.n[2] * x.n[2]);
            if (l > 1e-6f) for (float& c : x.n) c /= l;
        }
        if (!ok) continue;
        // the triangles and their material groups
        Addr tt;
        const Addr topo = g.Ptr(g.Member(meshType, mesh, "PrimaryTopology", &tt));
        if (!topo.ok()) continue;
        const Addr topoType = g.Ref(tt);
        Addr i32t, i16t, grt;
        const Addr i32 = g.Member(topoType, topo, "Indices", &i32t), i16 = g.Member(topoType, topo, "Indices16", &i16t);
        if (i32.ok() && g.Get<int32_t>(i32) > 0) {
            const int32_t ni = g.Get<int32_t>(i32);
            const Addr d = g.Ptr(i32.at(4));
            if (ni % 3 || !g.In(d, (uint32_t)ni * 4)) continue;
            m.idx.resize((size_t)ni);
            std::memcpy(m.idx.data(), f.sec[d.s].data() + d.o, (size_t)ni * 4);
        } else if (i16.ok() && g.Get<int32_t>(i16) > 0) {
            const int32_t ni = g.Get<int32_t>(i16);
            const Addr d = g.Ptr(i16.at(4));
            if (ni % 3 || !g.In(d, (uint32_t)ni * 2)) continue;
            m.idx.resize((size_t)ni);
            for (int32_t i = 0; i < ni; ++i) m.idx[(size_t)i] = g.Get<uint16_t>(d.at((uint32_t)i * 2));
        } else {
            continue;
        }
        bool inRange = true;
        for (uint32_t i : m.idx) inRange = inRange && i < (uint32_t)nv;
        if (!inRange) continue;
        const Addr groups = g.Member(topoType, topo, "Groups", &grt);
        if (groups.ok() && g.Kind(grt) == 3) {
            const int32_t ng = g.Get<int32_t>(groups);
            const Addr gtype = g.Ref(grt), garr = g.Ptr(groups.at(4));
            const int gstride = g.StructBytes(gtype);
            for (int32_t q = 0; q < ng && q < 256 && gstride > 0; ++q) {
                const Addr at = garr.at((uint32_t)(q * gstride));
                Addr e;
                Group gr;
                gr.material = g.Get<int32_t>(g.Member(gtype, at, "MaterialIndex", &e));
                gr.first = (uint32_t)g.Get<int32_t>(g.Member(gtype, at, "TriFirst", &e));
                gr.count = (uint32_t)g.Get<int32_t>(g.Member(gtype, at, "TriCount", &e));
                if (gr.material >= 0 && (size_t)gr.material < mats.size()) { gr.materialName = mats[(size_t)gr.material]; gr.technique = techs[(size_t)gr.material]; }
                m.groups.push_back(gr);
            }
        }
        if (m.groups.empty()) m.groups.push_back(Group{0, 0, (uint32_t)(m.idx.size() / 3), mats.empty() ? std::string() : mats[0], techs.empty() ? std::string() : techs[0]});
        out->push_back(std::move(m));
    }
    if (out->empty()) return no("no mesh's triangles read");
    return true;
}

// ---- the open edges ----------------------------------------------------------------------------------------------------------------

struct Run {
    std::vector<uint32_t> verts;       // welded vertex ids in order (closed: the first is not repeated)
    std::vector<uint32_t> tris;        // the triangle along each edge (edge k = verts[k] .. verts[k+1])
    bool closed = false;
    float length = 0.0f;
    float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
    float open[3] = {};                // mean direction out of the surface across its edges (length-weighted, normalised)
    float normal[3] = {};              // mean normal of the triangles along it (normalised)
    int32_t group = -1;                // the material group most of its triangles are in
};

struct Analysis {
    std::vector<float> wp;             // welded positions, x y z
    std::vector<uint32_t> weldOf;      // mesh vertex -> welded id
    size_t edges = 0, boundaryEdges = 0, nonManifoldEdges = 0, degenerate = 0;
    std::vector<Run> runs;
};

inline uint32_t GroupOfTri(const MeshData& m, uint32_t tri) {
    for (size_t q = 0; q < m.groups.size(); ++q)
        if (tri >= m.groups[q].first && tri < m.groups[q].first + m.groups[q].count) return (uint32_t)q;
    return 0;
}

inline void TriNormal(const float* a, const float* b, const float* c, float* n) {
    const float u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, v[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    n[0] = u[1] * v[2] - u[2] * v[1];
    n[1] = u[2] * v[0] - u[0] * v[2];
    n[2] = u[0] * v[1] - u[1] * v[0];
    const float l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (l > 1e-12f) for (int k = 0; k < 3; ++k) n[k] /= l;
}

// The mesh's boundary runs; vertices closer than `weld` model units are one.
inline Analysis Boundaries(const MeshData& m, float weld = 1e-3f) {
    Analysis a;
    // weld on a grid of `weld`, looking at the 27 cells round (two points just across a cell line are still one)
    struct Key { int64_t x, y, z; bool operator==(const Key& o) const { return x == o.x && y == o.y && z == o.z; } };
    struct KeyHash { size_t operator()(const Key& k) const { return (size_t)(k.x * 73856093 ^ k.y * 19349663 ^ k.z * 83492791); } };
    std::unordered_map<Key, std::vector<uint32_t>, KeyHash> grid;
    a.weldOf.resize(m.v.size());
    for (size_t i = 0; i < m.v.size(); ++i) {
        const float* p = m.v[i].p;
        const Key k{(int64_t)std::floor(p[0] / weld), (int64_t)std::floor(p[1] / weld), (int64_t)std::floor(p[2] / weld)};
        uint32_t found = ~0u;
        for (int dx = -1; dx <= 1 && found == ~0u; ++dx)
            for (int dy = -1; dy <= 1 && found == ~0u; ++dy)
                for (int dz = -1; dz <= 1 && found == ~0u; ++dz) {
                    const auto it = grid.find(Key{k.x + dx, k.y + dy, k.z + dz});
                    if (it == grid.end()) continue;
                    for (uint32_t w : it->second) {
                        const float* q = &a.wp[(size_t)w * 3];
                        if (std::fabs(q[0] - p[0]) <= weld && std::fabs(q[1] - p[1]) <= weld && std::fabs(q[2] - p[2]) <= weld) { found = w; break; }
                    }
                }
        if (found == ~0u) {
            found = (uint32_t)(a.wp.size() / 3);
            a.wp.insert(a.wp.end(), p, p + 3);
            grid[k].push_back(found);
        }
        a.weldOf[i] = found;
    }
    // undirected edges: count, and the (directed) triangle edge of a lone one
    struct E { uint32_t count = 0, tri = 0, from = 0, to = 0; };
    std::unordered_map<uint64_t, E> edges;
    const size_t nt = m.idx.size() / 3;
    for (size_t t = 0; t < nt; ++t) {
        const uint32_t w[3] = {a.weldOf[m.idx[t * 3]], a.weldOf[m.idx[t * 3 + 1]], a.weldOf[m.idx[t * 3 + 2]]};
        if (w[0] == w[1] || w[1] == w[2] || w[0] == w[2]) { ++a.degenerate; continue; }
        for (int e = 0; e < 3; ++e) {
            const uint32_t p = w[e], q = w[(e + 1) % 3];
            const uint64_t key = p < q ? ((uint64_t)p << 32 | q) : ((uint64_t)q << 32 | p);
            E& x = edges[key];
            if (!x.count) { x.tri = (uint32_t)t; x.from = p; x.to = q; }
            ++x.count;
        }
    }
    a.edges = edges.size();
    // the boundary: vertex -> its boundary edges
    std::unordered_map<uint32_t, std::vector<uint64_t>> at;
    for (const auto& [key, x] : edges) {
        if (x.count == 1) { ++a.boundaryEdges; at[x.from].push_back(key); at[x.to].push_back(key); }
        else if (x.count > 2) ++a.nonManifoldEdges;
    }
    std::unordered_map<uint64_t, bool> used;
    auto other = [](uint64_t key, uint32_t v) { const uint32_t lo = (uint32_t)(key >> 32), hi = (uint32_t)key; return v == lo ? hi : lo; };
    auto walk = [&](uint32_t start, uint64_t firstEdge) {
        Run r;
        r.verts.push_back(start);
        uint32_t v = start;
        uint64_t e = firstEdge;
        while (true) {
            used[e] = true;
            r.tris.push_back(edges[e].tri);
            const uint32_t nx = other(e, v);
            if (nx == start) { r.closed = true; break; }
            r.verts.push_back(nx);
            v = nx;
            const auto& list = at[v];
            if (list.size() != 2) break;   // an end, or three or more meet: the chain stops here
            uint64_t next = 0;
            bool any = false;
            for (uint64_t c : list) if (!used[c]) { next = c; any = true; break; }
            if (!any) break;
            e = next;
        }
        return r;
    };
    // open chains first (from ends and junctions), then closed loops
    for (int pass = 0; pass < 2; ++pass)
        for (const auto& [v, list] : at) {
            if (pass == 0 && list.size() == 2) continue;
            for (uint64_t e : list)
                if (!used[e]) a.runs.push_back(walk(v, e));
        }
    // each run's measures
    std::map<uint32_t, float> groupLen;
    for (Run& r : a.runs) {
        float open[3] = {}, nrm[3] = {};
        groupLen.clear();
        const size_t ne = r.tris.size();
        for (size_t k = 0; k < ne; ++k) {
            const uint32_t p = r.verts[k], q = r.verts[(k + 1) % r.verts.size()];
            const float* P = &a.wp[(size_t)p * 3];
            const float* Q = &a.wp[(size_t)q * 3];
            const size_t t = r.tris[k];
            const float* A = &a.wp[(size_t)a.weldOf[m.idx[t * 3]] * 3];
            const float* B = &a.wp[(size_t)a.weldOf[m.idx[t * 3 + 1]] * 3];
            const float* C = &a.wp[(size_t)a.weldOf[m.idx[t * 3 + 2]] * 3];
            float n[3];
            TriNormal(A, B, C, n);
            const float e[3] = {Q[0] - P[0], Q[1] - P[1], Q[2] - P[2]};
            const float len = std::sqrt(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
            r.length += len;
            // out of the triangle across the edge: in its plane, perpendicular to the edge, away from its third corner
            float o[3] = {e[1] * n[2] - e[2] * n[1], e[2] * n[0] - e[0] * n[2], e[0] * n[1] - e[1] * n[0]};
            const float mid[3] = {(P[0] + Q[0]) * 0.5f, (P[1] + Q[1]) * 0.5f, (P[2] + Q[2]) * 0.5f};
            const float cen[3] = {(A[0] + B[0] + C[0]) / 3, (A[1] + B[1] + C[1]) / 3, (A[2] + B[2] + C[2]) / 3};
            if (o[0] * (mid[0] - cen[0]) + o[1] * (mid[1] - cen[1]) + o[2] * (mid[2] - cen[2]) < 0) for (float& c : o) c = -c;
            for (int c = 0; c < 3; ++c) { open[c] += o[c]; nrm[c] += n[c] * len; }
            groupLen[GroupOfTri(m, (uint32_t)t)] += len;
            for (int c = 0; c < 3; ++c) { r.mn[c] = std::min(r.mn[c], std::min(P[c], Q[c])); r.mx[c] = std::max(r.mx[c], std::max(P[c], Q[c])); }
        }
        float lo = std::sqrt(open[0] * open[0] + open[1] * open[1] + open[2] * open[2]);
        float ln = std::sqrt(nrm[0] * nrm[0] + nrm[1] * nrm[1] + nrm[2] * nrm[2]);
        for (int c = 0; c < 3; ++c) { r.open[c] = lo > 1e-9f ? open[c] / lo : 0; r.normal[c] = ln > 1e-9f ? nrm[c] / ln : 0; }
        float best = -1;
        for (const auto& [q, l] : groupLen) if (l > best) { best = l; r.group = (int32_t)q; }
    }
    std::sort(a.runs.begin(), a.runs.end(), [](const Run& x, const Run& y) { return x.length > y.length; });
    return a;
}

}  // namespace worldholes
