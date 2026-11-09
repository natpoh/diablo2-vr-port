// world_holes_scene.h - [holes] a level preset of the game's laid out (data/hd/env/preset/**.json, its prefabs opened) and the floor gaps in it:
// the narrow strips beside a walkable floor through which one sees down past the floor's edge because the modeller let the floor stop short of
// the wall - the isometric camera, looking down over that wall, never sees the strip (the bridge out of the Rogue Encampment, Serg 2026-11-09).
// Pure C++ (propspresets' JSON reader, worldholes' model reader); tools/world_holes runs it over every preset, offline. Read only.
//
// The floor gap rule (Gaps), on a height map of the preset (Raster: every placed model's triangles that face up, front side, the highest one over
// each cell of `cell` units, and which placement it is):
//   a cell is in a gap when, along x or along z, within `reach` units on one side there is a FLAT cell (normal up within 25 degrees, of a piece
//   one walks on: floorModel, a "floor" physics filter, or the terrain - never a wagon's bed or a table) at least `drop`
//   higher than it (the floor whose edge it lies past), and within `reach` on the other side a cell at least as high as that floor less `drop` / 2
//   (the wall, parapet or the floor again) - a trench no wider than `reach` with a floor on its rim. Cells with nothing over them at all count as
//   infinitely low. Gaps are 8-connected into regions; each region: its cells, bounds, the floor's height and piece, the wall's piece, the bottom's.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "props_presets.h"
#include "world_holes.h"

namespace worldscene {

struct Mat { double m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; };   // row major, column vectors: p' = M p

inline Mat Mul(const Mat& a, const Mat& b) {
    Mat r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            double s = 0;
            for (int k = 0; k < 4; ++k) s += a.m[i * 4 + k] * b.m[k * 4 + j];
            r.m[i * 4 + j] = s;
        }
    return r;
}

inline void Apply(const Mat& M, const float* p, float* out) {
    for (int i = 0; i < 3; ++i) out[i] = (float)(M.m[i * 4] * p[0] + M.m[i * 4 + 1] * p[1] + M.m[i * 4 + 2] * p[2] + M.m[i * 4 + 3]);
}

// A preset's TransformDefinitionComponent: position, orientation (x y z w), scale
inline Mat FromTransform(const propspresets::Value& t) {
    auto num = [](const propspresets::Value* o, const char* k, double d) { const propspresets::Value* v = o ? o->Get(k) : nullptr; return v && v->type == propspresets::Value::Number ? v->n : d; };
    const propspresets::Value *p = t.Get("position"), *q = t.Get("orientation"), *s = t.Get("scale");
    const double x = num(q, "x", 0), y = num(q, "y", 0), z = num(q, "z", 0), w = num(q, "w", 1);
    const double sx = num(s, "x", 1), sy = num(s, "y", 1), sz = num(s, "z", 1);
    const double R[9] = {1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w),
                         2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
                         2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)};
    Mat M;
    for (int i = 0; i < 3; ++i) {
        M.m[i * 4 + 0] = R[i * 3 + 0] * sx;
        M.m[i * 4 + 1] = R[i * 3 + 1] * sy;
        M.m[i * 4 + 2] = R[i * 3 + 2] * sz;
    }
    M.m[3] = num(p, "x", 0);
    M.m[7] = num(p, "y", 0);
    M.m[11] = num(p, "z", 0);
    return M;
}

struct Placement {
    std::string name;    // the entity's
    std::string model;   // data/hd/env/...model, as the preset names it
    Mat M;               // model -> preset
    int prefabDepth = 0;
    bool walk = false;   // a floor one walks on: the preset says floorModel, or its physics body's filter is "floor", or it is the terrain
    // its ModelDefinitionComponent as the preset has it (a patch's unit takes its rim's: lit, shadowed and blended as the floor it carries on)
    int lightMask = 19, shadowMask = 0, terrainBlendMode = 1;
    bool floorModel = false, terrainBlendYUp = false;
};

inline bool ReadText(const std::filesystem::path& p, std::string* out) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    *out = ss.str();
    return true;
}

// Where the game's files come from: a file by its name in the game ("data/hd/env/..."), its bytes. The survey reads them from a folder they
// were taken out to (FileReader); D2R VR Settings straight from the player's game (CascLib).
using Reader = std::function<bool(const std::string& name, std::string* out)>;
inline Reader FileReader(std::filesystem::path root) {
    return [root](const std::string& name, std::string* out) { return ReadText(root / name, out); };
}

// Every model the preset (or prefab) `rel` places, prefabs opened (4 deep).
inline bool Placements(const Reader& read, const std::string& rel, const Mat& parent, int depth, std::vector<Placement>* out, std::vector<std::string>* missing) {
    std::string text;
    if (!read(rel, &text)) { if (missing) missing->push_back(rel); return false; }
    propspresets::Value doc;
    propspresets::Parser parser(text.data(), text.size());
    if (!parser.Parse(doc)) return false;
    const propspresets::Value* ents = doc.Get("entities");
    if (!ents || ents->type != propspresets::Value::Array) return false;
    for (const auto& e : ents->items) {
        const propspresets::Value* comps = e.Get("components");
        if (!comps) continue;
        const propspresets::Value *tr = nullptr, *model = nullptr, *prefab = nullptr;
        bool floorFilter = false;
        for (const auto& c : comps->items) {
            const propspresets::Value* t = c.Get("type");
            if (!t) continue;
            if (t->s == "TransformDefinitionComponent") tr = &c;
            else if (t->s == "ModelDefinitionComponent") model = &c;
            else if (t->s == "PrefabPlacementDefinitionComponent") prefab = c.Get("prefab");
            else if (t->s == "PhysicsBodyDefinitionComponent") { const propspresets::Value* fl = c.Get("filter"); floorFilter = floorFilter || (fl && fl->s == "floor"); }
            else if (t->s == "TerrainDefinitionComponent") floorFilter = true;
        }
        const Mat M = tr ? Mul(parent, FromTransform(*tr)) : parent;
        const propspresets::Value* nm = e.Get("name");
        if (model) {
            const propspresets::Value* f = model->Get("filename");
            const propspresets::Value* fm = model->Get("floorModel");
            if (f && f->type == propspresets::Value::String) {
                Placement pl{nm ? nm->s : std::string(), f->s, M, depth, floorFilter || (fm && fm->b)};
                auto num = [&](const char* k, int d) { const propspresets::Value* v = model->Get(k); return v && v->type == propspresets::Value::Number ? (int)v->n : d; };
                auto yes = [&](const char* k) { const propspresets::Value* v = model->Get(k); return v && v->type == propspresets::Value::Bool && v->b; };
                pl.lightMask = num("lightMask", 19);
                pl.shadowMask = num("shadowMask", 0);
                pl.terrainBlendMode = num("terrainBlendMode", 1);
                pl.floorModel = yes("floorModel");
                pl.terrainBlendYUp = yes("terrainBlendEnableYUpBlend");
                out->push_back(pl);
            }
        }
        if (prefab && prefab->type == propspresets::Value::String && depth < 4) Placements(read, prefab->s, M, depth + 1, out, missing);
    }
    return true;
}

// The file the game loads for a model name at LOD `lod`: <name>_lod<n>.model, else (LOD 0) the name itself.
inline std::string LodName(const std::string& model, int lod) {
    if (model.size() > 6 && model.compare(model.size() - 6, 6, ".model") == 0) return model.substr(0, model.size() - 6) + "_lod" + std::to_string(lod) + ".model";
    return model;
}
inline bool ReadLod(const Reader& read, const std::string& model, int lod, std::string* out) {
    return read(LodName(model, lod), out) || (lod == 0 && read(model, out));
}

// Pieces left out of the floor map: what lies on the floor or hangs (plants, grime decals, cloth), never a floor or a wall of its own.
inline bool Decor(const std::string& model) {
    std::string s = model;
    for (char& c : s) c = (char)tolower((unsigned char)c);
    static const char* kSkip[] = {"/foliage/", "/grunge/", "/vfx/", "/decal", "_decal", "/cobweb", "cobweb", "/light/", "vis_box", "blocker", "/ivy", "_ivy", "skirt_", "poop_skirt", "puddle"};
    for (const char* k : kSkip) if (s.find(k) != std::string::npos) return true;
    return false;
}

struct ModelTris {
    std::vector<float> p;          // model-space triangles, 9 floats each (decals and alpha cards left out: GrungeMaterial, AlphaThreshold...)
    std::vector<uint32_t> mesh;    // which mesh each triangle is in
    std::vector<float> open;       // the solid meshes' open (boundary) edges, model space, 6 floats each - where a surface ends in the air
    std::vector<uint32_t> openMesh;   // which mesh each open edge is of
    std::vector<std::string> meshName, meshTech;   // each mesh's name and its (first solid) material's technique
    bool ok = false;
};

// A material that draws no solid surface: a decal laid over another (grime, wheel tracks), an alpha-tested card (plants, cloth skirts)
inline bool NotSolid(const std::string& technique) {
    return technique.find("Grunge") != std::string::npos || technique.find("AlphaThreshold") != std::string::npos ||
           technique.find("Foliage") != std::string::npos || technique.find("Decal") != std::string::npos || technique.find("Unlit") != std::string::npos;
}

class ModelCache {
  public:
    explicit ModelCache(Reader read) : read_(std::move(read)) {}
    const ModelTris& Get(const std::string& model) {
        auto it = cache_.find(model);
        if (it != cache_.end()) return *it->second;
        if (bytes_ > (size_t)1500 << 20) { cache_.clear(); bytes_ = 0; }
        auto t = std::make_unique<ModelTris>();
        std::string data;
        grannyremesh::File f;
        std::vector<worldholes::MeshData> meshes;
        std::string why;
        if (ReadLod(read_, model, 0, &data) && grannyremesh::Read(data, &f, &why) && worldholes::ReadMeshes(f, &meshes, &why)) {
            for (size_t k = 0; k < meshes.size(); ++k) {
                const auto& m = meshes[k];
                t->meshName.push_back(m.name);
                std::string tech;
                for (const auto& gr : m.groups) if (!NotSolid(gr.technique)) { tech = gr.technique; break; }
                t->meshTech.push_back(tech);
                if (m.layer != 1) continue;   // (another render layer: not drawn by default)
                // only the solid groups' triangles - as their own mesh for the open edges
                worldholes::MeshData solid = m;
                solid.idx.clear();
                for (const auto& gr : m.groups) {
                    if (NotSolid(gr.technique)) continue;
                    for (uint32_t tt = gr.first; tt < gr.first + gr.count && (size_t)tt * 3 + 2 < m.idx.size(); ++tt)
                        for (int c = 0; c < 3; ++c) solid.idx.push_back(m.idx[(size_t)tt * 3 + c]);
                }
                if (solid.idx.empty()) continue;
                for (size_t i = 0; i + 2 < solid.idx.size(); i += 3) {
                    for (int c = 0; c < 3; ++c) t->p.insert(t->p.end(), m.v[solid.idx[i + c]].p, m.v[solid.idx[i + c]].p + 3);
                    t->mesh.push_back((uint32_t)k);
                }
                solid.groups.assign(1, worldholes::Group{0, 0, (uint32_t)(solid.idx.size() / 3), std::string(), std::string()});
                const worldholes::Analysis a = worldholes::Boundaries(solid);
                for (const auto& r : a.runs)
                    for (size_t e = 0; e < r.tris.size(); ++e) {
                        const uint32_t p0 = r.verts[e], p1 = r.verts[(e + 1) % r.verts.size()];
                        t->open.insert(t->open.end(), &a.wp[(size_t)p0 * 3], &a.wp[(size_t)p0 * 3] + 3);
                        t->open.insert(t->open.end(), &a.wp[(size_t)p1 * 3], &a.wp[(size_t)p1 * 3] + 3);
                        t->openMesh.push_back((uint32_t)k);
                    }
            }
            t->ok = true;
        }
        bytes_ += t->p.size() * 4 + t->mesh.size() * 4 + t->open.size() * 4;
        return *(cache_[model] = std::move(t));
    }

    // a model of ours laid in by name (the check of a patch)
    void Put(const std::string& name, ModelTris t) { cache_[name] = std::make_unique<ModelTris>(std::move(t)); }

  private:
    Reader read_;
    std::unordered_map<std::string, std::unique_ptr<ModelTris>> cache_;
    size_t bytes_ = 0;
};

struct HeightMap {
    float x0 = 0, z0 = 0, cell = 0.25f;
    int nx = 0, nz = 0;
    std::vector<float> h;        // -inf: nothing
    std::vector<int32_t> who;    // placement index
    std::vector<uint8_t> flat;   // the top's normal within 25 degrees of up, and its piece one walks on (Placement::walk)
    std::vector<uint8_t> openRim;   // an open edge of a floor one walks on runs through the cell at its top's height (the floor ends in the air here)
    // the upright faces (normal within 45 degrees of level), front side, by the way they look (+x, -x, +z, -z): the lowest and highest of them in each cell
    std::vector<float> wallLo[4], wallHi[4];
    std::vector<int32_t> wallWho[4];   // the placement of the last such face in the cell
    float At(int i, int j) const { return h[(size_t)j * nx + i]; }
};

constexpr float kNone = -1e30f;

// The highest front-facing up surface over each cell of the area x0..x1, z0..z1.
inline HeightMap Raster(const std::vector<Placement>& pl, ModelCache& cache, float x0, float x1, float z0, float z1, float cell) {
    HeightMap hm;
    hm.x0 = x0; hm.z0 = z0; hm.cell = cell;
    hm.nx = std::max(1, (int)std::ceil((x1 - x0) / cell));
    hm.nz = std::max(1, (int)std::ceil((z1 - z0) / cell));
    hm.h.assign((size_t)hm.nx * hm.nz, kNone);
    hm.who.assign(hm.h.size(), -1);
    hm.flat.assign(hm.h.size(), 0);
    hm.openRim.assign(hm.h.size(), 0);
    for (int d = 0; d < 4; ++d) { hm.wallLo[d].assign(hm.h.size(), 1e30f); hm.wallHi[d].assign(hm.h.size(), -1e30f); hm.wallWho[d].assign(hm.h.size(), -1); }
    for (size_t k = 0; k < pl.size(); ++k) {
        if (Decor(pl[k].model)) continue;
        const ModelTris& t = cache.Get(pl[k].model);
        if (!t.ok) continue;
        // a mirroring placement turns the winding round
        const double* m = pl[k].M.m;
        const double det = m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) + m[2] * (m[4] * m[9] - m[5] * m[8]);
        for (size_t i = 0; i + 8 < t.p.size(); i += 9) {
            float a[3], b[3], c[3];
            Apply(pl[k].M, &t.p[i], a);
            Apply(pl[k].M, &t.p[i + 3], b);
            Apply(pl[k].M, &t.p[i + 6], c);
            float n[3];
            worldholes::TriNormal(a, b, c, n);
            if (det < 0) for (float& q : n) q = -q;
            if (std::fabs(n[1]) < 0.7071f) {   // an upright face: sampled over its area, each sample marks its cell with its height, by the way it looks
                const int look = std::fabs(n[0]) >= std::fabs(n[2]) ? (n[0] > 0 ? 0 : 1) : (n[2] > 0 ? 2 : 3);
                const float e = std::max({std::hypot(b[0] - a[0], b[1] - a[1], b[2] - a[2]), std::hypot(c[0] - b[0], c[1] - b[1], c[2] - b[2]),
                                          std::hypot(a[0] - c[0], a[1] - c[1], a[2] - c[2])});
                const int ns = std::min(160, std::max(1, (int)std::ceil(e / (cell * 0.5f))));
                for (int u = 0; u <= ns; ++u)
                    for (int v = 0; u + v <= ns; ++v) {
                        const float fu = (float)u / ns, fv = (float)v / ns, fw = 1.0f - fu - fv;
                        const float x = a[0] * fw + b[0] * fu + c[0] * fv, y = a[1] * fw + b[1] * fu + c[1] * fv, z = a[2] * fw + b[2] * fu + c[2] * fv;
                        const int ii = (int)std::floor((x - x0) / cell), j = (int)std::floor((z - z0) / cell);
                        if (ii < 0 || j < 0 || ii >= hm.nx || j >= hm.nz) continue;
                        const size_t q = (size_t)j * hm.nx + ii;
                        hm.wallLo[look][q] = std::min(hm.wallLo[look][q], y);
                        hm.wallHi[look][q] = std::max(hm.wallHi[look][q], y);
                        hm.wallWho[look][q] = (int32_t)k;
                    }
            }
            if (n[1] <= 0.02f) continue;   // facing down or edge on: from above its back (culled) or nothing
            const float mnx = std::min({a[0], b[0], c[0]}), mxx = std::max({a[0], b[0], c[0]});
            const float mnz = std::min({a[2], b[2], c[2]}), mxz = std::max({a[2], b[2], c[2]});
            const int i0 = std::max(0, (int)std::ceil((mnx - x0) / cell - 0.5f)), i1 = std::min(hm.nx - 1, (int)std::floor((mxx - x0) / cell - 0.5f));
            const int j0 = std::max(0, (int)std::ceil((mnz - z0) / cell - 0.5f)), j1 = std::min(hm.nz - 1, (int)std::floor((mxz - z0) / cell - 0.5f));
            if (i1 < i0 || j1 < j0) continue;
            const double d = (double)(b[2] - c[2]) * (a[0] - c[0]) + (double)(c[0] - b[0]) * (a[2] - c[2]);
            if (std::fabs(d) < 1e-12) continue;
            const bool isFlat = n[1] > 0.906f && pl[k].walk;   // cos 25
            for (int j = j0; j <= j1; ++j)
                for (int ii = i0; ii <= i1; ++ii) {
                    const double X = x0 + (ii + 0.5) * cell, Z = z0 + (j + 0.5) * cell;
                    const double l1 = ((b[2] - c[2]) * (X - c[0]) + (c[0] - b[0]) * (Z - c[2])) / d;
                    const double l2 = ((c[2] - a[2]) * (X - c[0]) + (a[0] - c[0]) * (Z - c[2])) / d;
                    const double l3 = 1 - l1 - l2;
                    if (l1 < -1e-9 || l2 < -1e-9 || l3 < -1e-9) continue;
                    const float y = (float)(l1 * a[1] + l2 * b[1] + l3 * c[1]);
                    const size_t q = (size_t)j * hm.nx + ii;
                    if (y > hm.h[q]) { hm.h[q] = y; hm.who[q] = (int32_t)k; hm.flat[q] = isFlat; }
                }
        }
    }
    // the floors' open edges, walked in steps of half a cell: a cell is an open rim when the edge passes at its top's height
    for (size_t k = 0; k < pl.size(); ++k) {
        if (Decor(pl[k].model) || !pl[k].walk) continue;
        const ModelTris& t = cache.Get(pl[k].model);
        for (size_t i = 0; i + 5 < t.open.size(); i += 6) {
            float a[3], b[3];
            Apply(pl[k].M, &t.open[i], a);
            Apply(pl[k].M, &t.open[i + 3], b);
            const float len = std::hypot(b[0] - a[0], b[2] - a[2]);
            const int steps = std::max(1, (int)std::ceil(len / (cell * 0.5f)));
            for (int s = 0; s <= steps; ++s) {
                const float u = (float)s / steps, x = a[0] + (b[0] - a[0]) * u, y = a[1] + (b[1] - a[1]) * u, z = a[2] + (b[2] - a[2]) * u;
                const int ii = (int)std::floor((x - x0) / cell), j = (int)std::floor((z - z0) / cell);
                for (int dj = -1; dj <= 1; ++dj)   // (the cells round it too: an edge on a cell's border belongs to both sides)
                    for (int di = -1; di <= 1; ++di) {
                        const int a2 = ii + di, b2 = j + dj;
                        if (a2 < 0 || b2 < 0 || a2 >= hm.nx || b2 >= hm.nz) continue;
                        const size_t q = (size_t)b2 * hm.nx + a2;
                        if (std::fabs(hm.h[q] - y) < 0.35f) hm.openRim[q] = 1;
                    }
            }
        }
    }
    return hm;
}

struct Gap {
    int cells = 0;
    float area = 0;
    float mn[2] = {1e30f, 1e30f}, mx[2] = {-1e30f, -1e30f};   // x z
    float floorY = 0, bottomY = 0;        // the floor on the rim (median), the gap's bottom (median; kNone = nothing under it)
    int32_t floorPiece = -1, wallPiece = -1, bottomPiece = -1;   // the most common placement at each
    int voidCells = 0;                    // cells with nothing under them at all
    float openRim = 0;                    // the share of its cells whose floor rim (within a cell of the rim cell) is an open edge
    float walled = 0;                     // the share of its cells with an upright face under the rim looking into the gap at half its depth (a ditch's wall)
    bool Hole() const { return openRim >= 0.3f && walled < 0.5f; }   // the floor ends in the air over it and nothing closes its side: a hole, not a gutter
};

// labels (may be null): each cell's region (its index in the result), -1 none; rims (may be null): each gap cell's floor on its rim (its height)
inline std::vector<Gap> Gaps(const HeightMap& hm, float reach = 4.0f, float drop = 2.5f, float minArea = 1.0f, std::vector<int32_t>* labels = nullptr,
                             std::vector<float>* rims = nullptr) {
    const int R = std::max(1, (int)std::lround(reach / hm.cell));
    const size_t N = hm.h.size();
    std::vector<uint8_t> gap(N, 0);
    std::vector<float> rimY(N, 0);
    std::vector<int32_t> rimWho(N, -1), wallWho(N, -1);
    std::vector<uint8_t> rimOpen(N, 0), walledAt(N, 0);
    const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int j = 0; j < hm.nz; ++j)
        for (int i = 0; i < hm.nx; ++i) {
            const float h = hm.At(i, j);
            for (int d = 0; d < 4 && !gap[(size_t)j * hm.nx + i]; ++d) {
                // the floor's rim on this side: the first flat cell at least `drop` higher
                int fi = -1, fj = -1, steps = 0;
                for (int s = 1; s <= R; ++s) {
                    const int a = i + dirs[d][0] * s, b = j + dirs[d][1] * s;
                    if (a < 0 || b < 0 || a >= hm.nx || b >= hm.nz) break;
                    const size_t q = (size_t)b * hm.nx + a;
                    if (hm.h[q] >= h + drop) { if (hm.flat[q]) { fi = a; fj = b; steps = s; } break; }
                }
                if (fi < 0) continue;
                const float floorY = hm.At(fi, fj);
                // and on the other side, within what is left of the reach: something as high as that floor (less drop / 2)
                for (int s = 1; s <= R - steps + 1; ++s) {
                    const int a = i - dirs[d][0] * s, b = j - dirs[d][1] * s;
                    if (a < 0 || b < 0 || a >= hm.nx || b >= hm.nz) break;
                    const size_t q = (size_t)b * hm.nx + a;
                    if (hm.h[q] >= floorY - drop * 0.5f) {
                        const size_t me = (size_t)j * hm.nx + i;
                        gap[me] = 1;
                        rimY[me] = floorY;
                        for (int dj = -1; dj <= 1; ++dj)
                            for (int di = -1; di <= 1; ++di) {
                                const int ra = fi + di, rb = fj + dj;
                                if (ra >= 0 && rb >= 0 && ra < hm.nx && rb < hm.nz && hm.openRim[(size_t)rb * hm.nx + ra]) rimOpen[me] = 1;
                            }
                        // a face under the rim looking back into the gap (the way opposite to d) at half the gap's depth: a ditch's wall
                        {
                            const int back = d ^ 1;
                            const float ht = h > kNone ? (floorY + h) * 0.5f : floorY - drop;
                            for (int s2 = 0; s2 <= steps + 1 && !walledAt[me]; ++s2) {
                                const int a2 = i + dirs[d][0] * s2, b2 = j + dirs[d][1] * s2;
                                if (a2 < 0 || b2 < 0 || a2 >= hm.nx || b2 >= hm.nz) break;
                                const size_t q2 = (size_t)b2 * hm.nx + a2;
                                if (hm.wallLo[back][q2] <= ht && hm.wallHi[back][q2] >= ht) walledAt[me] = 1;
                            }
                        }
                        rimWho[me] = hm.who[(size_t)fj * hm.nx + fi];
                        wallWho[me] = hm.who[q];
                        break;
                    }
                }
            }
        }
    // regions
    std::vector<Gap> out;
    std::vector<int32_t> stack, members;
    std::vector<uint8_t> seen(N, 0);
    std::vector<int32_t> lab(N, -1);
    for (size_t s0 = 0; s0 < N; ++s0) {
        if (!gap[s0] || seen[s0]) continue;
        Gap g;
        std::vector<float> rims, bottoms;
        std::map<int32_t, int> fw, ww, bw;
        int open = 0, walls = 0;
        stack.assign(1, (int32_t)s0);
        members.clear();
        seen[s0] = 1;
        while (!stack.empty()) {
            const size_t q = (size_t)stack.back();
            stack.pop_back();
            members.push_back((int32_t)q);
            const int i = (int)(q % hm.nx), j = (int)(q / hm.nx);
            ++g.cells;
            const float x = hm.x0 + (i + 0.5f) * hm.cell, z = hm.z0 + (j + 0.5f) * hm.cell;
            g.mn[0] = std::min(g.mn[0], x); g.mx[0] = std::max(g.mx[0], x);
            g.mn[1] = std::min(g.mn[1], z); g.mx[1] = std::max(g.mx[1], z);
            rims.push_back(rimY[q]);
            if (hm.h[q] > kNone) bottoms.push_back(hm.h[q]); else ++g.voidCells;
            ++fw[rimWho[q]]; ++ww[wallWho[q]]; ++bw[hm.who[q]];
            open += rimOpen[q];
            walls += walledAt[q];
            for (int dj = -1; dj <= 1; ++dj)
                for (int di = -1; di <= 1; ++di) {
                    const int a = i + di, b = j + dj;
                    if (a < 0 || b < 0 || a >= hm.nx || b >= hm.nz) continue;
                    const size_t r = (size_t)b * hm.nx + a;
                    if (gap[r] && !seen[r]) { seen[r] = 1; stack.push_back((int32_t)r); }
                }
        }
        g.area = g.cells * hm.cell * hm.cell;
        g.openRim = (float)open / (float)g.cells;
        g.walled = (float)walls / (float)g.cells;
        if (g.area < minArea) continue;
        auto median = [](std::vector<float>& v) { if (v.empty()) return kNone; std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end()); return v[v.size() / 2]; };
        g.floorY = median(rims);
        g.bottomY = median(bottoms);
        auto most = [](const std::map<int32_t, int>& m) { int32_t best = -1; int n = -1; for (const auto& [k, c] : m) if (c > n) { n = c; best = k; } return best; };
        g.floorPiece = most(fw); g.wallPiece = most(ww); g.bottomPiece = most(bw);
        for (int32_t q : members) lab[(size_t)q] = (int32_t)out.size();
        out.push_back(g);
    }
    // largest first (the labels follow)
    std::vector<int32_t> order(out.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = (int32_t)i;
    std::sort(order.begin(), order.end(), [&](int32_t a, int32_t b) { return out[(size_t)a].area > out[(size_t)b].area; });
    std::vector<int32_t> rank(out.size());
    std::vector<Gap> sorted;
    for (size_t i = 0; i < order.size(); ++i) { rank[(size_t)order[i]] = (int32_t)i; sorted.push_back(out[(size_t)order[i]]); }
    if (rims) *rims = rimY;
    if (labels) {
        labels->assign(N, -1);
        for (size_t q = 0; q < N; ++q) if (lab[q] >= 0) (*labels)[q] = rank[(size_t)lab[q]];
    }
    return sorted;
}

// ---- the other kind: a wall seen from behind -------------------------------------------------------------------------------------------------
// A wall the isometric camera only ever saw from its front: modelled as one surface, its back culled. Where a floor one walks on lies behind it
// (within `behind` units on its back side, the floor under the face's top by at least 1 unit and not far under its foot), and nothing on that
// side looks back (no upright face looking the other way between the wall and that floor, over the floor's height), first person sees through
// the wall: a hole into the void or the next room. Cells so are 8-connected into regions (the same Gap: floor = that floor, wall = the
// one-sided wall's piece; openRim / walled unused).
inline std::vector<Gap> BackFaces(const HeightMap& hm, float behind = 0.75f, float minArea = 1.0f) {
    const int R = std::max(1, (int)std::lround(behind / hm.cell));
    const size_t N = hm.h.size();
    std::vector<uint8_t> hit(N, 0);
    std::vector<float> fl(N, 0.0f);
    std::vector<int32_t> who(N, -1);
    const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int j = 0; j < hm.nz; ++j)
        for (int i = 0; i < hm.nx; ++i) {
            const size_t c = (size_t)j * hm.nx + i;
            for (int d = 0; d < 4 && !hit[c]; ++d) {
                if (hm.wallHi[d][c] < -1e29f) continue;
                const float lo = hm.wallLo[d][c], top = hm.wallHi[d][c];
                if (top - lo < 1.0f) continue;   // (a low trim: nothing to see through)
                // behind it: the way opposite to the one it looks
                for (int s = 1; s <= R; ++s) {
                    const int a = i - dirs[d][0] * s, b = j - dirs[d][1] * s;
                    if (a < 0 || b < 0 || a >= hm.nx || b >= hm.nz) break;
                    const size_t q = (size_t)b * hm.nx + a;
                    const float h = hm.h[q];
                    if (h > top - 1.0f) break;   // (something as high as the wall behind it: no floor to stand on there)
                    if (!hm.flat[q] || h < lo - 2.0f) continue;
                    // a face looking back toward that floor, from the wall to it, over the floor's height: the wall's own back - closed
                    bool closed = false;
                    for (int s2 = 0; s2 <= s && !closed; ++s2) {
                        const int a2 = i - dirs[d][0] * s2, b2 = j - dirs[d][1] * s2;
                        const size_t q2 = (size_t)b2 * hm.nx + a2;
                        if (hm.wallHi[d ^ 1][q2] >= h + 0.5f && hm.wallLo[d ^ 1][q2] <= top) closed = true;
                    }
                    if (!closed) { hit[c] = 1; fl[c] = h; who[c] = hm.wallWho[d][c]; }
                    break;
                }
            }
        }
    std::vector<Gap> out;
    std::vector<uint8_t> seen(N, 0);
    std::vector<int32_t> stack;
    for (size_t s0 = 0; s0 < N; ++s0) {
        if (!hit[s0] || seen[s0]) continue;
        Gap g;
        std::map<int32_t, int> ww;
        std::vector<float> floors;
        stack.assign(1, (int32_t)s0);
        seen[s0] = 1;
        while (!stack.empty()) {
            const size_t q = (size_t)stack.back();
            stack.pop_back();
            const int i = (int)(q % hm.nx), j = (int)(q / hm.nx);
            ++g.cells;
            const float x = hm.x0 + (i + 0.5f) * hm.cell, z = hm.z0 + (j + 0.5f) * hm.cell;
            g.mn[0] = std::min(g.mn[0], x); g.mx[0] = std::max(g.mx[0], x);
            g.mn[1] = std::min(g.mn[1], z); g.mx[1] = std::max(g.mx[1], z);
            floors.push_back(fl[q]);
            ++ww[who[q]];
            for (int dj = -1; dj <= 1; ++dj)
                for (int di = -1; di <= 1; ++di) {
                    const int a = i + di, b = j + dj;
                    if (a < 0 || b < 0 || a >= hm.nx || b >= hm.nz) continue;
                    const size_t r = (size_t)b * hm.nx + a;
                    if (hit[r] && !seen[r]) { seen[r] = 1; stack.push_back((int32_t)r); }
                }
        }
        g.area = g.cells * hm.cell * hm.cell;
        if (g.area < minArea) continue;
        std::nth_element(floors.begin(), floors.begin() + floors.size() / 2, floors.end());
        g.floorY = floors[floors.size() / 2];
        int n = -1;
        for (const auto& [k, c] : ww) if (c > n) { n = c; g.wallPiece = k; }
        out.push_back(g);
    }
    std::sort(out.begin(), out.end(), [](const Gap& a, const Gap& b) { return a.area > b.area; });
    return out;
}

// ---- the third kind: an overhead floor seen from below ------------------------------------------------------------------------------------------
// An upper floor, a balcony, a roof modelled with its top side only (the isometric camera looked down on it): from a floor one walks on under it,
// first person looks up at its back - culled - and sees through it to the sky or the void. (The floor under it: a built one - the terrain runs on
// under every building, where nobody stands.) Every solid face is laid in layers over cells of
// `cell` units (up to kLayers each: its height, which way it looks, whether a floor one walks on); for each floor one walks on in a cell, the first
// face over it (more than half a unit up, within `reach` up) that looks up - not down - is an underside seen from below. Cells so are
// 8-connected into regions (floorY = the floor under it, bottomY = the face over it - here a height over the floor, wallPiece = its piece).
constexpr int kLayers = 12;
struct Layer { float y; int32_t who; uint8_t up, walk; };
inline std::vector<Gap> Undersides(const std::vector<Placement>& pl, ModelCache& cache, float x0, float x1, float z0, float z1, float cell = 0.5f,
                                   float reach = 30.0f, float minArea = 2.0f) {
    const int nx = std::max(1, (int)std::ceil((x1 - x0) / cell)), nz = std::max(1, (int)std::ceil((z1 - z0) / cell));
    const size_t N = (size_t)nx * nz;
    std::vector<Layer> lay(N * kLayers);
    std::vector<uint8_t> count(N, 0);
    for (size_t k = 0; k < pl.size(); ++k) {
        if (Decor(pl[k].model)) continue;
        const ModelTris& t = cache.Get(pl[k].model);
        if (!t.ok) continue;
        const double* m = pl[k].M.m;
        const double det = m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) + m[2] * (m[4] * m[9] - m[5] * m[8]);
        for (size_t i = 0; i + 8 < t.p.size(); i += 9) {
            float a[3], b[3], c[3];
            Apply(pl[k].M, &t.p[i], a); Apply(pl[k].M, &t.p[i + 3], b); Apply(pl[k].M, &t.p[i + 6], c);
            float n[3];
            worldholes::TriNormal(a, b, c, n);
            if (det < 0) for (float& q : n) q = -q;
            if (std::fabs(n[1]) < 0.5f) continue;   // (upright faces: not a floor or a ceiling)
            // (the terrain runs on under every building - nobody stands there: only a built floor counts as one under an overhead)
            const bool up = n[1] > 0.0f, walk = up && n[1] > 0.906f && pl[k].walk && pl[k].model.find("/terrain") == std::string::npos;
            const float mnx = std::min({a[0], b[0], c[0]}), mxx = std::max({a[0], b[0], c[0]});
            const float mnz = std::min({a[2], b[2], c[2]}), mxz = std::max({a[2], b[2], c[2]});
            const int i0 = std::max(0, (int)std::ceil((mnx - x0) / cell - 0.5f)), i1 = std::min(nx - 1, (int)std::floor((mxx - x0) / cell - 0.5f));
            const int j0 = std::max(0, (int)std::ceil((mnz - z0) / cell - 0.5f)), j1 = std::min(nz - 1, (int)std::floor((mxz - z0) / cell - 0.5f));
            if (i1 < i0 || j1 < j0) continue;
            const double d = (double)(b[2] - c[2]) * (a[0] - c[0]) + (double)(c[0] - b[0]) * (a[2] - c[2]);
            if (std::fabs(d) < 1e-12) continue;
            for (int j = j0; j <= j1; ++j)
                for (int ii = i0; ii <= i1; ++ii) {
                    const double X = x0 + (ii + 0.5) * cell, Z = z0 + (j + 0.5) * cell;
                    const double l1 = ((b[2] - c[2]) * (X - c[0]) + (c[0] - b[0]) * (Z - c[2])) / d;
                    const double l2 = ((c[2] - a[2]) * (X - c[0]) + (a[0] - c[0]) * (Z - c[2])) / d;
                    const double l3 = 1 - l1 - l2;
                    if (l1 < -1e-9 || l2 < -1e-9 || l3 < -1e-9) continue;
                    const float y = (float)(l1 * a[1] + l2 * b[1] + l3 * c[1]);
                    const size_t q = (size_t)j * nx + ii;
                    Layer* L = &lay[q * kLayers];
                    // the same height and way already there (a mesh's two triangles over one cell): once
                    bool have = false;
                    for (int e = 0; e < count[q] && !have; ++e) have = L[e].up == up && std::fabs(L[e].y - y) < 0.05f;
                    if (have) continue;
                    if (count[q] < kLayers) L[count[q]++] = Layer{y, (int32_t)k, (uint8_t)up, (uint8_t)walk};
                }
        }
    }
    std::vector<uint8_t> hit(N, 0);
    std::vector<float> fl(N, 0.0f), over(N, 0.0f);
    std::vector<int32_t> who(N, -1);
    for (size_t q = 0; q < N; ++q) {
        Layer* L = &lay[q * kLayers];
        std::sort(L, L + count[q], [](const Layer& a, const Layer& b) { return a.y < b.y; });
        for (int e = 0; e < count[q] && !hit[q]; ++e) {
            if (!L[e].walk) continue;
            for (int f = e + 1; f < count[q]; ++f) {
                if (L[f].y <= L[e].y + 0.5f) continue;
                if (L[f].y > L[e].y + reach) break;
                // (the terrain over a built floor: that floor lies under the ground - a pool's, the lava's - not under a roof)
                if (L[f].up && pl[(size_t)L[f].who].model.find("/terrain") == std::string::npos) { hit[q] = 1; fl[q] = L[e].y; over[q] = L[f].y - L[e].y; who[q] = L[f].who; }
                break;
            }
        }
    }
    std::vector<Gap> out;
    std::vector<uint8_t> seen(N, 0);
    std::vector<int32_t> stack;
    for (size_t s0 = 0; s0 < N; ++s0) {
        if (!hit[s0] || seen[s0]) continue;
        Gap g;
        std::map<int32_t, int> ww;
        std::vector<float> floors, overs;
        stack.assign(1, (int32_t)s0);
        seen[s0] = 1;
        while (!stack.empty()) {
            const size_t q = (size_t)stack.back();
            stack.pop_back();
            const int i = (int)(q % nx), j = (int)(q / nx);
            ++g.cells;
            const float x = x0 + (i + 0.5f) * cell, z = z0 + (j + 0.5f) * cell;
            g.mn[0] = std::min(g.mn[0], x); g.mx[0] = std::max(g.mx[0], x);
            g.mn[1] = std::min(g.mn[1], z); g.mx[1] = std::max(g.mx[1], z);
            floors.push_back(fl[q]);
            overs.push_back(over[q]);
            ++ww[who[q]];
            for (int dj = -1; dj <= 1; ++dj)
                for (int di = -1; di <= 1; ++di) {
                    const int a = i + di, b = j + dj;
                    if (a < 0 || b < 0 || a >= nx || b >= nz) continue;
                    const size_t r = (size_t)b * nx + a;
                    if (hit[r] && !seen[r]) { seen[r] = 1; stack.push_back((int32_t)r); }
                }
        }
        g.area = g.cells * cell * cell;
        if (g.area < minArea) continue;
        std::nth_element(floors.begin(), floors.begin() + floors.size() / 2, floors.end());
        std::nth_element(overs.begin(), overs.begin() + overs.size() / 2, overs.end());
        g.floorY = floors[floors.size() / 2];
        g.bottomY = overs[overs.size() / 2];
        int n = -1;
        for (const auto& [k, c] : ww) if (c > n) { n = c; g.wallPiece = k; }
        out.push_back(g);
    }
    std::sort(out.begin(), out.end(), [](const Gap& a, const Gap& b) { return a.area > b.area; });
    return out;
}

}  // namespace worldscene
