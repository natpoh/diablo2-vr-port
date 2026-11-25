// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed steamvr/prepare_sewer_backdrop.py (Berk, 4f3a6a3 / 94ac5de / 063360f, 2026-10-18; docs/KURAST_SEWER_ENTRANCES.md):
// geometry() and backdrop_entities() ported step by step with the same numbers (the floor's lowest edge, 1 unit of overlap on the left jamb and
// 3 on the right, half a unit behind the open end, the two faces). Changed for D2R VR Authentic: C++ over our Granny reader
// (shared/world_holes.h, shared/granny_remesh.h) and our JSON reader (shared/props_presets.h); the two entities are spliced into the game's
// own text (every other byte of the preset kept) instead of a re-dump; our entity names and ids. MPL 2.0.
//
// unleashed_sewer_backdrop.h - [holes] sewer_backdrop: the Kurast sewer stairs' open end closed by a dark wall (ported 2026-11-25 while looking for
// Serg's "texture under the Bazaar's ground" - that one was another piece: shared/bazaar_mask.h).
//
// What it is: in Kurast Bazaar (level 80) and Upper Kurast (81) the way down to the Sewers (warp 57) is a stairwell, the game's
// act3_kurast_stairs/stairs_sewers01 model under act3_kurast_architecture/sewer_entrance02, placed by the four presets
// data/hd/env/preset/act3/kurast/burbssewer0/1.json and metrosewer0/1.json. The stairs run below the ground and their lowest floor ends in an
// open edge: no wall closes the end of the tunnel (the camera above never looks into it). In first person you look down the stairwell and
// through its far end - the sky, the void beyond the world. ([render] roofs_two_sided draws its walls from both sides, vr/roof_sides.h; that
// cannot make the wall that is not there - docs/KNOWN_ISSUES.md #27.)
//
// The fix, Bear's: two faces of the game's own black rectangle (act4_fort_stairs/black_square01, a 100 x 100 two-triangle quad the Act 4
// fortress uses the same way) stood up half a unit behind the open floor end, as wide as that end plus 1 unit on the left and 3 on the right
// (the visible opening is wider than the floor's lowest edge), as high as the stairs model plus 1; one face each way. Measured from the
// player's own files (the stairs' floor - its "terrain" material - and the quad's bounds), laid by each preset's own stairs transform.
// Scenery only: the presets' terrain, their other entities, physics and warps are untouched (the legacy map, not these files, holds the
// level's collision and paths). D2R_VR_Settings makes the files (tools/settings/sewer_assets.cpp) into the mod's own data folder.
// cleanroom/tests/sewer_backdrop_test.cpp tests this.
#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "granny_remesh.h"
#include "props_presets.h"
#include "world_holes.h"

namespace sewerbackdrop {

constexpr const char* kPresets[] = {
    "data/hd/env/preset/act3/kurast/burbssewer0.json",
    "data/hd/env/preset/act3/kurast/burbssewer1.json",
    "data/hd/env/preset/act3/kurast/metrosewer0.json",
    "data/hd/env/preset/act3/kurast/metrosewer1.json",
};
constexpr int kPresetCount = (int)(sizeof kPresets / sizeof kPresets[0]);
constexpr const char* kStairs = "data/hd/env/model/act3/kurast/act3_kurast_stairs/stairs_sewers01";
constexpr const char* kBlack = "data/hd/env/model/act4/fort/act4_fort_stairs/black_square01";
constexpr const char* kPrefix = "D2R_VR_Sewer_Backdrop_";   // our entities' names (Bear's are "PCVR_Sewer_Backdrop_")
constexpr int kMeasureLod = 4;                               // the LOD both models are measured at (Bear's: the simplest, same outline)

using V3 = std::array<double, 3>;
using Q = std::array<double, 4>;   // x, y, z, w

inline V3 Add(const V3& a, const V3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
inline V3 Mul(const V3& v, double s) { return {v[0] * s, v[1] * s, v[2] * s}; }
// v rotated by the unit quaternion q
inline V3 Rotate(const V3& v, const Q& q) {
    const double x = q[0], y = q[1], z = q[2], w = q[3];
    const double tx = 2 * (y * v[2] - z * v[1]), ty = 2 * (z * v[0] - x * v[2]), tz = 2 * (x * v[1] - y * v[0]);
    return {v[0] + w * tx + y * tz - z * ty, v[1] + w * ty + z * tx - x * tz, v[2] + w * tz + x * ty - y * tx};
}
// a * b (b first, then a)
inline Q Product(const Q& a, const Q& b) {
    const double x = a[0], y = a[1], z = a[2], w = a[3], u = b[0], v = b[1], t = b[2], s = b[3];
    return {w * u + x * s + y * t - z * v, w * v - x * t + y * s + z * u, w * t + x * v - y * u + z * s, w * s - x * u - y * v - z * t};
}

// A model as measured: its meshes (positions in model units) and the model's own placement (Models[0].InitialPlacement's translation).
struct Model {
    std::vector<worldholes::MeshData> meshes;
    V3 placement{};
};

// Models[0].InitialPlacement: one static model, no rotation or scale; its translation when its flags say it has one.
inline bool ReadModel(const std::string& bytes, Model* out, std::string* why) {
    *out = Model{};
    grannyremesh::File f;
    if (!grannyremesh::Read(bytes, &f, why) || !worldholes::ReadMeshes(f, &out->meshes, why)) return false;
    grannyremesh::Walk g(f);
    const grannyremesh::Addr rt{f.h.rootType.section, f.h.rootType.offset}, ro{f.h.rootObj.section, f.h.rootObj.offset};
    grannyremesh::Addr t;
    const grannyremesh::Addr models = g.Member(rt, ro, "Models", &t);
    if (!models.ok() || g.Kind(t) != 4 || g.Get<int32_t>(models) != 1) { *why = "expected one static model"; return false; }
    const grannyremesh::Addr model = g.Ptr(g.Ptr(models.at(4)));
    grannyremesh::Addr pt;
    const grannyremesh::Addr ip = g.Member(g.Ref(t), model, "InitialPlacement", &pt);
    if (!ip.ok()) { *why = "the model has no placement"; return false; }
    const uint32_t flags = g.Get<uint32_t>(ip);
    float v[16];
    for (int i = 0; i < 16; ++i) v[i] = g.Get<float>(ip.at(4 + 4 * (uint32_t)i));
    const float identity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    bool plain = (flags & ~1u) == 0 && v[3] == 0 && v[4] == 0 && v[5] == 0 && v[6] == 1;
    for (int i = 0; i < 9; ++i) plain = plain && v[7 + i] == identity[i];
    if (!plain) { *why = "unexpected model placement"; return false; }
    if (flags & 1u) out->placement = {v[0], v[1], v[2]};
    return true;
}

// Where the backdrop goes, in the stairs model's space; the quad's own centre (its model space) and its scale.
struct Measured {
    V3 center{};
    Q facing{0, 0, 0, 1};
    V3 scale{1, 1, 1};
    V3 quadCenter{};
    double width = 0, height = 0;   // the floor's open end and the opening's height (for the log)
};

// geometry(): the stairs' lowest floor edge and the reusable black rectangle.
inline bool Measure(const Model& stairs, const Model& black, Measured* out, std::string* why) {
    auto no = [&](const char* w) { *why = w; return false; };
    if (stairs.meshes.size() != 1) return no("unexpected sewer mesh layout");
    const worldholes::MeshData& mesh = stairs.meshes[0];
    if (!(mesh.scale > 1 && mesh.scale < 100)) return no("unexpected sewer geometry scale");
    std::vector<V3> pos;
    pos.reserve(mesh.v.size());
    for (const auto& v : mesh.v) pos.push_back(Add({v.p[0], v.p[1], v.p[2]}, stairs.placement));
    std::set<uint32_t> floor;
    for (const auto& gr : mesh.groups) {
        if ((uint64_t)(gr.first + gr.count) * 3 > mesh.idx.size() || gr.count == 0) return no("invalid sewer triangle group");
        if (gr.materialName == "terrain")
            for (uint64_t i = (uint64_t)gr.first * 3; i < (uint64_t)(gr.first + gr.count) * 3; ++i) floor.insert(mesh.idx[(size_t)i]);
    }
    for (uint32_t i : floor) if (i >= pos.size()) return no("invalid sewer triangle group");
    if (floor.size() < 4) return no("the sewer's floor not identified");
    double lowest = 1e30;
    for (uint32_t i : floor) lowest = std::min(lowest, pos[i][1]);
    std::set<std::array<double, 3>> ends;   // sorted, as Bear's sorted(set(...))
    auto r4 = [](double x) { return std::round(x * 1e4) / 1e4; };
    for (uint32_t i : floor)
        if (std::fabs(pos[i][1] - lowest) < .01) ends.insert({r4(pos[i][0]), r4(pos[i][1]), r4(pos[i][2])});
    if (ends.size() != 2) return no("expected a single open sewer floor edge");
    const V3 e0 = *ends.begin(), e1 = *ends.rbegin();
    const V3 delta{e1[0] - e0[0], e1[1] - e0[1], e1[2] - e0[2]};
    const double width = std::hypot(delta[0], delta[2]);
    if (!(width > 2 && width < 20) || std::fabs(delta[1]) > .05) return no("unexpected sewer floor edge");
    const V3 right{delta[0] / width, 0, delta[2] / width};
    const V3 normal{-right[2], 0, right[0]};
    const V3 middle = Mul(Add(e0, e1), .5);
    // the floor's highest vertex (the first of the highest, in index order) lies on the stairs' side of the edge
    uint32_t top = *floor.begin();
    for (uint32_t i : floor) if (pos[i][1] > pos[top][1]) top = i;
    double side = 0;
    for (int c = 0; c < 3; ++c) side += normal[c] * (pos[top][c] - middle[c]);
    if (side <= 0) return no("the sewer floor's direction changed");
    double upper = -1e30;
    for (const V3& p : pos) upper = std::max(upper, p[1]);
    const double height = upper - lowest + 1.;
    if (!(height > 2 && height < 20)) return no("unexpected sewer opening height");
    // The lowest floor edge is narrower than the visible opening; the first headset preview left a strip at the right jamb: that side
    // overlapped farther, in stair-local coordinates. Half a world unit behind the open floor end, its edges hidden in stone.
    const double leftOverlap = 1., rightOverlap = 3.;
    V3 center = Add(middle, Mul(normal, -.5));
    center = Add(center, Mul(right, (rightOverlap - leftOverlap) / 2));
    center[1] = (lowest + upper) / 2;
    if (black.meshes.size() != 1) return no("unexpected black rectangle layout");
    const worldholes::MeshData& quad = black.meshes[0];
    if (quad.v.size() != 4 || quad.idx.size() != 6) return no("the black backdrop is no longer a two-triangle rectangle");
    V3 lo{1e30, 1e30, 1e30}, hi{-1e30, -1e30, -1e30};
    for (const auto& v : quad.v) {
        const V3 p = Add({v.p[0], v.p[1], v.p[2]}, black.placement);
        for (int c = 0; c < 3; ++c) { lo[c] = std::min(lo[c], p[c]); hi[c] = std::max(hi[c], p[c]); }
    }
    if (hi[1] - lo[1] > .001 || !(hi[0] - lo[0] > 90 && hi[0] - lo[0] < 110) || !(hi[2] - lo[2] > 90 && hi[2] - lo[2] < 110))
        return no("unexpected native black rectangle bounds");
    const double half = std::sqrt(.5);
    const double yaw = -std::atan2(right[2], right[0]);
    out->center = center;
    out->facing = Product({0, std::sin(yaw / 2), 0, std::cos(yaw / 2)}, {half, 0, 0, half});
    out->scale = {(width + leftOverlap + rightOverlap) / (hi[0] - lo[0]), 1., height / (hi[2] - lo[2])};
    out->quadCenter = Mul(Add(lo, hi), .5);
    out->width = width;
    out->height = height;
    return true;
}

// ---- the preset ----------------------------------------------------------------------------------------------------------------------------------

// A number as JSON with a decimal point (as the game's files write theirs)
inline std::string Num(double v) {
    char b[48];
    snprintf(b, sizeof b, "%.9g", v == 0 ? 0.0 : v);
    std::string s = b;
    if (s.find_first_of(".eEn") == std::string::npos) s += ".0";
    return s;
}

inline uint32_t Fnv(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) { h ^= c; h *= 16777619u; }
    return h;
}

inline size_t Count(const std::string& s, const std::string& what) {
    size_t n = 0;
    for (size_t at = s.find(what); at != std::string::npos; at = s.find(what, at + what.size())) ++n;
    return n;
}

inline std::string Lower(std::string s) {
    for (char& c : s) c = c == '\\' ? '/' : (char)std::tolower((unsigned char)c);
    return s;
}

// One entity of ours: the black quad at `location`, turned by `facing`, sized `size`.
inline std::string EntityJson(const std::string& name, uint32_t id, const V3& location, const Q& facing, const V3& size) {
    auto v3 = [](const V3& v) { return "{\"x\":" + Num(v[0]) + ",\"y\":" + Num(v[1]) + ",\"z\":" + Num(v[2]) + "}"; };
    return "{\"type\":\"Entity\",\"name\":\"" + name + "\",\"id\":" + std::to_string(id) +
           ",\"components\":[{\"type\":\"TransformDefinitionComponent\",\"name\":\"" + name + "_transform\",\"position\":" + v3(location) +
           ",\"orientation\":{\"x\":" + Num(facing[0]) + ",\"y\":" + Num(facing[1]) + ",\"z\":" + Num(facing[2]) + ",\"w\":" + Num(facing[3]) +
           "},\"scale\":" + v3(size) + ",\"inheritOnlyPosition\":false},{\"type\":\"ModelDefinitionComponent\",\"name\":\"" + name +
           "_model\",\"filename\":\"" + kBlack +
           ".model\",\"visibleLayers\":1,\"lightMask\":0,\"shadowMask\":0,\"ghostShadows\":false,\"floorModel\":false,"
           "\"terrainBlendEnableYUpBlend\":false,\"terrainBlendMode\":0}]}";
}

enum State {
    kAdded,     // out = the preset with the two faces
    kOurs,      // the preset already has ours: nothing to change
    kRefused,   // the preset is not as known: nothing changed, why says what
};

// backdrop_entities(): the preset with the two faces behind its one sewer stairwell, by that stairwell's own transform.
inline State AddBackdrop(const std::string& json, const Measured& m, std::string* out, std::string* why) {
    out->clear();
    auto no = [&](const char* w) { *why = w; return kRefused; };
    propspresets::Value doc;
    if (!propspresets::Parser(json.data(), json.size()).Parse(doc) || doc.type != propspresets::Value::Object) return no("not JSON");
    const propspresets::Value* type = doc.Get("type");
    if (!type || type->s != "Preset") return no("not a Kurast preset");
    const propspresets::Value* entities = doc.Get("entities");
    const propspresets::Value* deps = doc.Get("dependencies");
    const propspresets::Value* models = deps ? deps->Get("models") : nullptr;
    if (!entities || entities->type != propspresets::Value::Array || !models || models->type != propspresets::Value::Array)
        return no("unexpected preset layout");
    std::set<double> ids;
    const propspresets::Value* stairs = nullptr;
    int found = 0;
    for (const auto& e : entities->items) {
        const propspresets::Value* name = e.Get("name");
        if (name && name->s.rfind(kPrefix, 0) == 0) return kOurs;
        const propspresets::Value* id = e.Get("id");
        if (id && id->type == propspresets::Value::Number) ids.insert(id->n);
        const propspresets::Value* comps = e.Get("components");
        bool isStairs = false;
        for (size_t i = 0; comps && i < comps->items.size(); ++i) {
            const propspresets::Value* t = comps->items[i].Get("type");
            const propspresets::Value* f = comps->items[i].Get("filename");
            if (t && t->s == "ModelDefinitionComponent" && f && Lower(f->s) == std::string(kStairs) + ".model") isStairs = true;
        }
        if (isStairs) { stairs = &e; ++found; }
    }
    if (found != 1) return no("expected exactly one sewer stairwell");
    const propspresets::Value* tr = nullptr;
    int transforms = 0;
    const propspresets::Value* comps = stairs->Get("components");
    for (size_t i = 0; i < comps->items.size(); ++i) {
        const propspresets::Value* t = comps->items[i].Get("type");
        if (t && t->s == "TransformDefinitionComponent") { tr = &comps->items[i]; ++transforms; }
    }
    if (transforms != 1) return no("expected one sewer transform");
    auto vec = [&](const char* key, const char* names, int n, double* outv) {
        const propspresets::Value* o = tr->Get(key);
        for (int i = 0; i < n; ++i) {
            const char k[2] = {names[i], 0};
            const propspresets::Value* v = o ? o->Get(k) : nullptr;
            if (!v || v->type != propspresets::Value::Number || !std::isfinite(v->n) || std::fabs(v->n) >= 1e6) return false;
            outv[i] = v->n;
        }
        return true;
    };
    V3 position, scale;
    Q orientation;
    if (!vec("position", "xyz", 3, position.data()) || !vec("orientation", "xyzw", 4, orientation.data()) || !vec("scale", "xyz", 3, scale.data()))
        return no("invalid native transform");
    double qq = 0;
    for (double c : orientation) qq += c * c;
    if (std::fabs(qq - 1) > .001 || !(scale[0] > .01 && scale[0] < 100) || std::fabs(scale[1] - scale[0]) > .001 || std::fabs(scale[2] - scale[0]) > .001)
        return no("unsupported sewer transform scale/rotation");
    // the two faces, spliced in first in the entity list (the order of the list means nothing to the renderer)
    std::string added;
    const char* sides[2] = {"front", "back"};
    const Q flips[2] = {{0, 0, 0, 1}, {1, 0, 0, 0}};
    for (int k = 0; k < 2; ++k) {
        const std::string name = std::string(kPrefix) + sides[k];
        const uint32_t id = Fnv(name);
        if (ids.count((double)id)) return no("backdrop entity id collision");
        ids.insert((double)id);
        const Q facing = Product(orientation, Product(m.facing, flips[k]));
        const V3 size = Mul(m.scale, scale[0]);
        const V3 center = Add(position, Rotate(Mul(m.center, scale[0]), orientation));
        const V3 offset = Rotate({m.quadCenter[0] * size[0], m.quadCenter[1] * size[1], m.quadCenter[2] * size[2]}, facing);
        const V3 location{center[0] - offset[0], center[1] - offset[1], center[2] - offset[2]};
        added += EntityJson(name, id, location, facing, size) + ",";
    }
    const std::string entKey = "\"entities\":[", modKey = "\"models\":[";
    if (Count(json, entKey) != 1 || Count(json, modKey) != 1) return no("the preset's text is not as known (its lists)");
    const size_t at = json.find(entKey) + entKey.size();
    if (entities->items.empty()) added.pop_back();   // (no comma before the list's end)
    std::string text = json.substr(0, at) + added + json.substr(at);
    bool have = false;
    for (const auto& d : models->items) {
        const propspresets::Value* p = d.Get("path");
        have = have || (p && Lower(p->s) == std::string(kBlack) + ".model");
    }
    if (!have) {
        const size_t mat = text.find(modKey) + modKey.size();
        text.insert(mat, std::string("{\"path\":\"") + kBlack + ".model\"}" + (models->items.empty() ? "" : ","));
    }
    *out = std::move(text);
    return kAdded;
}

}  // namespace sewerbackdrop
