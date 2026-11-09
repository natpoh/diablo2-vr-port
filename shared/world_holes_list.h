// world_holes_list.h - [holes] patch_world: the places whose holes get patched, and what is made for each from the player's own game (D2R VR
// Settings, tools/settings/holes_assets.cpp) and read back by the plugin (vrcam engitem::HolesFrame). Pure C++; nothing of Blizzard's in the
// repository or a build - the patch's model is the game's own model file (the rim floor's, its material kept) with the patch's triangles in it.
//
// A place is a line here: its name, its level preset, a point in its hole (from the survey: tools/world_holes survey / patch), and the model path
// the patch goes to - a path the game knows (only its LOD table data/hd/model_lod_desc.json names it: the game makes a model only from a path it
// knows, docs/game_ceiling.md) that nothing in play names and no other piece of ours uses (cleanroom/tests/world_holes_test checks the list).
// Adding a place: survey, pick the hole's point, a free path, a line here; run Settings (it makes the files), look in the headset; the plugin's
// log says where it laid it. See docs/notes/world_holes_pipeline.md.
//
// What is made for a place (Make): the preset laid out (its prefabs opened), its height map, the holes in it (shared/world_holes_scene.h), the
// hole at the point, its patch (shared/world_patch.h); the patch in the space of the preset's anchor piece - a top-level piece of the preset near
// the hole whose place in the preset no other top-level piece shares - moved by minus its place (a level preset is laid in the world unturned:
// the plugin lays the patch at the anchor's place as the renderer has it); the rim floor whose material it takes (SourceOf: the TerrainMaterial
// one if any); for each LOD 0 - 4 that floor's model file at that LOD (the nearest LOD that has the mesh), cut to that mesh alone and given the
// patch's triangles (grannyremesh::KeepOnlyMesh, Remesh); the unit (one entity: the model, the rim floor's own lightMask / shadowMask /
// floorModel / terrain blend from the preset); and a line of the measures file - where to find it, what it is.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

#include "granny_remesh.h"
#include "world_holes.h"
#include "world_holes_file.h"   // the places, their paths, the measures file
#include "world_holes_scene.h"
#include "world_patch.h"

namespace holeslist {

// ---- the unit -----------------------------------------------------------------------------------------------------------------------------------
inline std::string UnitJson(const Place& p, const worldscene::Placement& rim, const std::vector<std::string>& textures) {
    std::string tex;
    for (const std::string& t : textures) tex += (tex.empty() ? "" : ",") + std::string(R"({"path":")") + t + R"("})";
    char masks[256];
    snprintf(masks, sizeof masks, R"("visibleLayers":1,"lightMask":%d,"shadowMask":%d,"ghostShadows":false,"floorModel":%s,"terrainBlendEnableYUpBlend":%s,"terrainBlendMode":%d)",
             rim.lightMask, rim.shadowMask, rim.floorModel ? "true" : "false", rim.terrainBlendYUp ? "true" : "false", rim.terrainBlendMode);
    const std::string model = ModelPath(p);
    return R"({"dependencies":{"particles":[],"models":[{"path":")" + model + R"("}],"skeletons":[],"animations":[],"textures":[)" + tex +
           R"(],"physics":[],"json":[],"variantdata":[],"objecteffects":[],"other":[]},"type":"UnitDefinition","name":"d2r_vr_hole_)" + p.name +
           R"(","entities":[{"type":"Entity","name":"entity_root","id":1000000003,"components":[{"type":"UnitRootComponent","name":"component_root",)"
           R"("state_machine_filename":"","doNotInheritRotation":false,"rotationOverride":{"x":0.0,"y":0.0,"z":0.0,"w":1.0},)"
           R"("doNotUseHDHeight":true,"hideAllMeshWhenInOpenedMode":false,"onCreateEventName":"","animations":[]},)"
           R"({"type":"TransformDefinitionComponent","name":"entity_root_TransformDefinition","position":{"x":0.0,"y":0.0,"z":0.0},)"
           R"("orientation":{"x":0.0,"y":0.0,"z":0.0,"w":1.0},"scale":{"x":1.0,"y":1.0,"z":1.0},"inheritOnlyPosition":false}]},)"
           R"({"type":"Entity","name":"entity_model","id":1000000004,"components":[{"type":"ModelDefinitionComponent",)"
           R"("name":"component_model_hole_)" + p.name + R"(","filename":")" + model + R"(",)" + masks + "}]}]}";
}

// The textures a model file names (its root Textures' FromFileName that are the game's .texture files): the unit's dependencies
inline std::vector<std::string> TexturesOf(grannyremesh::File& f) {
    std::vector<std::string> out;
    grannyremesh::Walk g(f);
    const grannyremesh::Addr rt{f.h.rootType.section, f.h.rootType.offset}, ro{f.h.rootObj.section, f.h.rootObj.offset};
    grannyremesh::Addr t;
    const grannyremesh::Addr tex = g.Member(rt, ro, "Textures", &t);
    if (!tex.ok() || g.Kind(t) != 4) return out;
    const grannyremesh::Addr type = g.Ref(t), arr = g.Ptr(tex.at(4));
    for (int32_t k = 0; k < g.Get<int32_t>(tex) && k < 256; ++k) {
        grannyremesh::Addr e;
        const std::string name = g.Str(g.Ptr(g.Member(type, g.Ptr(arr.at((uint32_t)k * 8)), "FromFileName", &e)));
        if (name.size() > 8 && name.compare(name.size() - 8, 8, ".texture") == 0 && name.rfind("data/hd/", 0) == 0) {
            bool have = false;
            for (const std::string& o : out) have = have || o == name;
            if (!have) out.push_back(name);
        }
    }
    return out;
}

// ---- making a place's files -----------------------------------------------------------------------------------------------------------------------
// A piece of the level's own build: under data/hd/env/model/act<n>/ or expansion/ (walls, bridges, stones laid in the ground) - never a prop of
// global/prop (knockable: [weight] knock_props moves them), the terrain, or decor (plants, grime)
inline bool Built(const std::string& model) { return worldpatch::Built(model); }

struct Made {
    std::vector<std::pair<std::string, std::string>> files;   // its path in the mod (under the mpq), its bytes
    Line line;
    std::string why;   // not made: why
};

inline bool Make(const worldscene::Reader& read, const Place& place, Made* out) {
    *out = Made{};
    auto no = [&](const std::string& w) { out->why = w; return false; };
    worldscene::ModelCache cache(read);
    std::vector<worldscene::Placement> pl;
    if (!worldscene::Placements(read, place.preset, worldscene::Mat{}, 0, &pl, nullptr) || pl.empty()) return no(std::string("its preset does not read: ") + place.preset);
    float x0 = 1e30f, x1 = -1e30f, z0 = 1e30f, z1 = -1e30f;
    for (const auto& p : pl) {
        if (p.model.find("/terrain") != std::string::npos) continue;
        x0 = std::min(x0, (float)p.M.m[3]); x1 = std::max(x1, (float)p.M.m[3]);
        z0 = std::min(z0, (float)p.M.m[11]); z1 = std::max(z1, (float)p.M.m[11]);
    }
    if (x0 > x1) return no("its preset places nothing");
    x0 -= 30; x1 += 30; z0 -= 30; z1 += 30;
    const worldscene::HeightMap hm = worldscene::Raster(pl, cache, x0, x1, z0, z1, 0.25f);
    std::vector<int32_t> labels;
    std::vector<float> rims;
    const std::vector<worldscene::Gap> gaps = worldscene::Gaps(hm, 4.0f, 2.5f, 1.0f, &labels, &rims);
    int best = -1;
    for (size_t k = 0; k < gaps.size() && best < 0; ++k)
        if (gaps[k].Hole() && place.x >= gaps[k].mn[0] - 0.5f && place.x <= gaps[k].mx[0] + 0.5f && place.z >= gaps[k].mn[1] - 0.5f && place.z <= gaps[k].mx[1] + 0.5f)
            best = (int)k;
    if (best < 0) return no("no hole at its point (the game's preset changed?)");
    worldscene::Gap g;
    const std::vector<uint8_t> regions = worldpatch::Cluster(gaps, best, &g);
    worldpatch::Patch patch = worldpatch::Make(pl, cache, hm, labels, rims, regions, g);
    if (patch.tri.empty()) return no("its hole has no open floor edge on its rim");
    worldpatch::Source src;
    if (!worldpatch::SourceOf(pl, cache, patch, &src)) return no("no rim floor to take the material of");
    worldpatch::Sides(pl, cache, &patch, g.floorY);   // ... and the sides the rim's floors never had (the ramps at the deck's ends)
    // the anchor and the witness: top-level pieces of the level's own build (never a prop - [weight] knock_props moves those -, a plant or a decal)
    // whose places no other top-level piece shares - the anchor the nearest to the hole, the witness the nearest to it at least 5 units off
    const float cx = (g.mn[0] + g.mx[0]) * 0.5f, cz = (g.mn[1] + g.mx[1]) * 0.5f;
    auto unique = [&](size_t i) {
        for (size_t j = 0; j < pl.size(); ++j)
            if (j != i && pl[j].prefabDepth == 0 && std::fabs(pl[j].M.m[3] - pl[i].M.m[3]) < 1e-3 && std::fabs(pl[j].M.m[7] - pl[i].M.m[7]) < 1e-3 &&
                std::fabs(pl[j].M.m[11] - pl[i].M.m[11]) < 1e-3)
                return false;
        return true;
    };
    int anchor = -1, witness = -1;
    float da = 1e30f, dw = 1e30f;
    for (size_t i = 0; i < pl.size(); ++i) {
        if (pl[i].prefabDepth != 0 || !Built(pl[i].model) || !unique(i)) continue;
        const float d = (float)std::hypot(pl[i].M.m[3] - cx, pl[i].M.m[11] - cz);
        if (d < da) { da = d; anchor = (int)i; }
    }
    if (anchor < 0) return no("no top-level piece of its own place in the preset");
    const double* A = pl[(size_t)anchor].M.m;
    for (size_t i = 0; i < pl.size(); ++i) {
        if ((int)i == anchor || pl[i].prefabDepth != 0 || !Built(pl[i].model) || !unique(i)) continue;
        const float d = (float)std::hypot(pl[i].M.m[3] - A[3], pl[i].M.m[11] - A[11]);
        if (d >= 5.0f && d < dw) { dw = d; witness = (int)i; }
    }
    if (witness < 0) return no("no second top-level piece to tell its preset by");
    // the patch at the anchor's place: moved by minus it (the preset is laid unturned)
    worldscene::Mat toAnchor;
    toAnchor.m[3] = -A[3]; toAnchor.m[7] = -A[7]; toAnchor.m[11] = -A[11];
    grannyremesh::Mesh mesh;
    if (!worldpatch::ToMesh(patch, toAnchor, &mesh)) return no("the patch has too many vertices for one model");
    // the rim floor's model at each LOD, cut to its mesh, given the patch
    std::vector<std::string> textures;
    std::string last;
    for (int lod = 0; lod < kLods; ++lod) {
        std::string data, why;
        grannyremesh::File f;
        std::vector<worldholes::MeshData> meshes;
        int k = -1;
        for (int l = lod; l >= 0 && k < 0; --l) {   // that LOD's file, or the nearest finer one, holding the rim floor's mesh (by name, else by material)
            if (!worldscene::ReadLod(read, src.model, l, &data) || !grannyremesh::Read(data, &f, &why) || !worldholes::ReadMeshes(f, &meshes, &why)) continue;
            for (size_t i = 0; i < meshes.size() && k < 0; ++i) if (meshes[i].name == src.meshName) k = (int)i;
            for (size_t i = 0; i < meshes.size() && k < 0; ++i)
                if (meshes[i].groups.size() == 1 && meshes[i].groups[0].technique == src.technique && meshes[i].stride == (int)sizeof(grannyremesh::Vertex)) k = (int)i;
        }
        if (k < 0) return no("the rim floor's model has no such mesh at LOD " + std::to_string(lod) + ": " + src.model);
        if (meshes[(size_t)k].groups.size() != 1) return no("the rim floor's mesh has more than one material: " + src.meshName);
        if (lod == 0) textures = TexturesOf(f);
        grannyremesh::Done done;
        if (!grannyremesh::KeepOnlyMesh(f, k, &why) || !grannyremesh::Remesh(f, mesh, grannyremesh::Renames{}, lod, &done, &why))
            return no("the rim floor's model could not take the patch (" + why + "): " + src.model);
        out->files.push_back({LodPath(place, lod), grannyremesh::Write(f)});
    }
    out->files.push_back({UnitPath(place), UnitJson(place, pl[(size_t)src.placement], textures)});
    // the check: the hole left with the patch laid in
    {
        worldscene::ModelTris mt;
        mt.p = patch.tri;
        mt.mesh.assign(patch.tri.size() / 9, 0);
        mt.ok = true;
        cache.Put("__patch__", std::move(mt));
        std::vector<worldscene::Placement> with = pl;
        with.push_back(worldscene::Placement{"patch", "__patch__", worldscene::Mat{}, 0, true});
        const worldscene::HeightMap hm2 = worldscene::Raster(with, cache, x0, x1, z0, z1, 0.25f);
        for (const auto& a : worldscene::Gaps(hm2))
            if (a.Hole() && a.mx[0] >= g.mn[0] && a.mn[0] <= g.mx[0] && a.mx[1] >= g.mn[1] && a.mn[1] <= g.mx[1]) out->line.left += a.area;
    }
    Line& l = out->line;
    l.name = place.name;
    l.unit = UnitPath(place);
    for (int c = 0; c < 3; ++c) { l.anchor[c] = (float)A[c * 4 + 3]; l.witness[c] = (float)pl[(size_t)witness].M.m[c * 4 + 3]; }
    l.triangles = (int)(patch.tri.size() / 9);
    l.strips = patch.strips;
    l.skirts = patch.skirts;
    l.sides = patch.sides;
    l.sideLeft = patch.sideLeft;
    l.ramps = patch.sideStrips;
    l.seen = worldpatch::SeenOpenings(pl, cache, patch, g.floorY).openLength;   // the eyes' check
    l.area = patch.area;
    l.holeArea = g.area;
    l.source = src.model;
    l.sourceMesh = src.meshName;
    l.technique = src.technique;
    return true;
}

}  // namespace holeslist
