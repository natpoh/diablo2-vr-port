// wrist_orb_models.h - [hud] wrist_game_models: the life and mana medallions on the wrists drawn by the GAME (VR F4), from the game's own models and
// materials, as the sky dome and the cave ceiling are - no pass of ours. What D2R VR Settings makes from the player's own game (tools/settings/
// orb_assets.cpp, CascLib, read only) and the plugin places (vrcam engitem::WristOrbsFrame). Pure C++; nothing of Blizzard's in the repository or a
// build. Study and choices: docs/notes/wrist_orbs_game_objects.md.
//
// A medallion is three models laid on the wrist every frame (the place, the frame and the eased level are today's medallion's - vr/hand_orbs.h; only
// the drawing changes), each OUR shape in the game's own potion material (its colour one texel of the potion's own picture - the potions share one uv
// layout), written at the game's build-verification test model paths (bvt: the game ships them and its LOD table names them, nothing in play shows
// them):
//   - the glass: an open dome (no bottom) in the lesser mana potion's glass material at an empty-glass texel (its glass mesh's top tenth). (0.214.1 -
//     0.214.3: the game's bvt glass_sphere as it is drew glossy black; 0.215.8: the healing potion's GlassMaterial, an opaque white egg.)
//   - the base: the effect's own medallion plate - radius 1, a fifth of it thick under the top plane, a raised bezel round the dome's foot (BaseMesh) -
//     for each metal in a potion's own cap material with that metal's embossed rosette (kPlateMetal): clean or aged by the patina (MakePlate).
//   - the liquid: OUR dome cut flat at its level - kLevels meshes in one file, one a level (ZoneMesh, AddMeshes), the plugin shows one with the game's
//     own mesh flipbook (ModelInstance::SetActiveMesh) - put into the game's own lesser healing / lesser mana potion model - the file cut
//     to its glass mesh alone, whose material holds the potion's colour (grannyremesh::KeepOnlyMesh, Remesh), every vertex's uv the one texel of the
//     potion's liquid (LiquidUv: the potions share one uv layout - the glass mesh's vertices at 55 % of its height are on the liquid, red in the healing
//     potion's picture and blue in the mana potion's), the material renamed ours and its own EmissiveIntensity raised (the game's light healing potion
//     glows with 13): a soft glow of the game's own shader where it is dark. Written at two more of the game's test models (bvt hair_sphere and
//     foliage_sphere - paths the game knows, nothing in play names them), each salted so the game never takes one for the other. The plugin scales
//     it along the back of the hand by the level - smooth, no steps - and pulses it with the game's own hover highlight (HighlightComponent) when low.
// The units (ours, at our own paths: the game loads a unit's JSON from anywhere, a model only from a path it knows) name those models.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "granny_remesh.h"
#include "wrist_orb_paths.h"   // the paths: the game's models, ours, the potions, the units (the plugin reads them too)

namespace wristorbs {

using Reader = std::function<bool(const std::string& name, std::string* out)>;

// The glow: the liquid material's EmissiveIntensity ([hud] wrist_game_glow, baked by Settings; the game's light healing potion has 13)
constexpr float kGlowDefault = 13.0f, kGlowMax = 40.0f;   // (the default: the game's own light healing potion's glow)
inline float GlowClamp(float g) { return std::isfinite(g) ? (g < 0.0f ? 0.0f : g > kGlowMax ? kGlowMax : g) : kGlowDefault; }

// ---- the liquid's shape -----------------------------------------------------------------------------------------------------------------------------

constexpr int kLensSegments = 32, kLensRings = 8;
// A half ball of radius 1 standing on y 0 (its top at y 1), closed below by a flat disc; normals out, both windings (whatever the game's cull mode and
// handedness: the inside is never seen), every vertex's uv `uv` (one texel: one colour, the potion's liquid). The tangent runs round (its sign the
// potions' -127, as the card's). VertexScale 1: model units, the plugin scales it to the dome.
inline grannyremesh::Mesh LensMesh(float u, float v, bool closed = true) {
    using grannyremesh::Half;
    using grannyremesh::Snorm8;
    grannyremesh::Mesh m;
    m.scale = 1.0f;
    for (int k = 0; k < 3; ++k) { m.mn[k] = 1e30f; m.mx[k] = -1e30f; }
    const uint16_t hu = Half(u), hv = Half(v);
    auto vertex = [&](double px, double py, double pz, double nx, double ny, double nz, double a) {
        grannyremesh::Vertex x{};
        const double p[3] = {px, py, pz}, n[3] = {nx, ny, nz};
        for (int k = 0; k < 3; ++k) {
            x.pos[k] = (int16_t)std::lround(p[k] * 32767.0);
            x.normal[k] = Snorm8((float)n[k]);
            if ((float)p[k] < m.mn[k]) m.mn[k] = (float)p[k];
            if ((float)p[k] > m.mx[k]) m.mx[k] = (float)p[k];
        }
        x.pos[3] = 0;   // log2(VertexScale 1) / 13
        x.tangent[0] = Snorm8((float)std::cos(a)); x.tangent[1] = 0; x.tangent[2] = Snorm8((float)-std::sin(a)); x.tangent[3] = -127;
        x.uv[0] = hu;
        x.uv[1] = hv;
        m.v.push_back(x);
        return (uint16_t)(m.v.size() - 1);
    };
    auto both = [&](uint16_t a, uint16_t b, uint16_t c) {
        m.idx.insert(m.idx.end(), {a, b, c});
        m.idx.insert(m.idx.end(), {a, c, b});
    };
    const int row = kLensSegments + 1;
    const double pi = 3.14159265358979;
    // the dome: rings from the rim (elevation 0) up; the top one point
    for (int i = 0; i < kLensRings; ++i) {
        const double e = (double)i / kLensRings * pi * 0.5;
        for (int j = 0; j <= kLensSegments; ++j) {
            const double a = (double)j / kLensSegments * 2.0 * pi;
            const double d[3] = {std::cos(e) * std::sin(a), std::sin(e), std::cos(e) * std::cos(a)};
            vertex(d[0], d[1], d[2], d[0], d[1], d[2], a);
        }
    }
    const uint16_t top = vertex(0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0);
    for (int i = 0; i < kLensRings; ++i)
        for (int j = 0; j < kLensSegments; ++j) {
            const uint16_t a = (uint16_t)(i * row + j), b = (uint16_t)(i * row + j + 1);
            if (i + 1 == kLensRings) { both(a, b, top); continue; }
            const uint16_t c = (uint16_t)(a + row), d = (uint16_t)(b + row);
            both(a, b, c);
            both(b, d, c);
        }
    if (!closed) return m;   // (the glass: an open dome - no bottom on the plane, where the liquid's own bottom lies)
    // the bottom: a disc on y 0 facing down
    const uint16_t mid = vertex(0.0, 0.0, 0.0, 0.0, -1.0, 0.0, 0.0);
    const uint16_t first = (uint16_t)m.v.size();
    for (int j = 0; j <= kLensSegments; ++j) {
        const double a = (double)j / kLensSegments * 2.0 * pi;
        vertex(std::sin(a), 0.0, std::cos(a), 0.0, -1.0, 0.0, a);
    }
    for (int j = 0; j < kLensSegments; ++j) both(mid, (uint16_t)(first + j), (uint16_t)(first + j + 1));
    return m;
}

// ---- the base: the effect's medallion plate (0.214.6 - 0.215.4 squashed the half ball - its top a curve, flush only at the middle: the liquid's unlit
// underside showed as a black crescent past it, and its rim tapered into the skin) ----------------------------------------------------------------------

// The plate of radius 1, kMedPlateT thick under y 0, with the raised bezel round the dome's foot (kMedDomeR .. kMedBezelOuter, kMedBezelH high), revolved
// round y: the floor inside the dome, the bezel's inner wall, its top, its outer wall, the lip out to the rim, the side, the underside - each its own band
// with its own normal (flat-shaded, as the effect's metal reads), both windings, every uv (u, v). VertexScale 1: units of the base's radius.
inline grannyremesh::Mesh BaseMesh(float u, float v, float mapR = 0.0f) {   // (mapR > 0: every uv (u + x mapR, v + z mapR): a picture laid across it from above)
    using grannyremesh::Half;
    using grannyremesh::Snorm8;
    grannyremesh::Mesh m;
    m.scale = 1.0f;
    m.mn[0] = -1.0f; m.mn[1] = -kMedPlateT; m.mn[2] = -1.0f;
    m.mx[0] = 1.0f; m.mx[1] = kMedBezelH; m.mx[2] = 1.0f;
    const uint16_t hu = Half(u), hv = Half(v);
    struct Seg { float r0, y0, r1, y1, nr, ny; };   // from (r0, y0) to (r1, y1); its normal (radial, up)
    // the bezel's top starts where the dome's sphere reaches its height (the effect's Rin = sqrt(Rd^2 - Hb^2), OneMedalCore): its inner edge lies on the glass
    const float rin = std::sqrt(kMedDomeR * kMedDomeR - kMedBezelH * kMedBezelH);
    const Seg segs[] = {
        {0.0f, 0.0f, rin, 0.0f, 0.0f, 1.0f},                                // the floor inside the dome
        {rin, 0.0f, rin, kMedBezelH, -1.0f, 0.0f},                          // the bezel's inner wall (inside the glass's foot)
        {rin, kMedBezelH, kMedBezelOuter, kMedBezelH, 0.0f, 1.0f},          // the bezel's top
        {kMedBezelOuter, kMedBezelH, kMedBezelOuter, 0.0f, 1.0f, 0.0f},     // its outer wall
        {kMedBezelOuter, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f},                     // the lip out to the rim
        {1.0f, 0.0f, 1.0f, -kMedPlateT, 1.0f, 0.0f},                        // the side
        {1.0f, -kMedPlateT, 0.0f, -kMedPlateT, 0.0f, -1.0f},                // the underside
    };
    const double pi = 3.14159265358979;
    const int row = kLensSegments + 1;
    for (const Seg& s : segs) {
        const uint16_t first = (uint16_t)m.v.size();
        for (int end = 0; end < 2; ++end) {
            const double r = end ? s.r1 : s.r0, y = end ? s.y1 : s.y0;
            for (int j = 0; j <= kLensSegments; ++j) {
                const double a = (double)j / kLensSegments * 2.0 * pi, sa = std::sin(a), ca = std::cos(a);
                grannyremesh::Vertex x{};
                const double p[3] = {r * sa, y, r * ca}, n[3] = {s.nr * sa, s.ny, s.nr * ca};
                for (int k = 0; k < 3; ++k) { x.pos[k] = (int16_t)std::lround(p[k] * 32767.0); x.normal[k] = Snorm8((float)n[k]); }
                x.pos[3] = 0;
                x.tangent[0] = Snorm8((float)ca); x.tangent[1] = 0; x.tangent[2] = Snorm8((float)-sa); x.tangent[3] = -127;
                x.uv[0] = mapR > 0.0f ? Half(u + (float)p[0] * mapR) : hu;
                x.uv[1] = mapR > 0.0f ? Half(v + (float)p[2] * mapR) : hv;
                m.v.push_back(x);
            }
        }
        for (int j = 0; j < kLensSegments; ++j) {
            const uint16_t a = (uint16_t)(first + j), b = (uint16_t)(first + j + 1), c = (uint16_t)(first + row + j), d = (uint16_t)(first + row + j + 1);
            m.idx.insert(m.idx.end(), {a, b, c, a, c, b, b, d, c, b, c, d});
        }
    }
    return m;
}

// ---- the liquid cut flat at its level: one mesh a level, the game's own mesh flipbook picks one (0.214.5: the lens scaled down "sank") ------------------

// The half ball of radius 1 cut flat at height `h` (0 - 1): its sphere wall from y 0 up to h and a flat top at h (radius sqrt(1 - h^2)) - open below, it
// stands on the base's floor; normals out (the wall's the sphere's, the top's up), both windings, every uv (u, v). Its bounds are the whole half ball's (so
// every level of a file shares one box). h 1: the whole half ball with a one-point top.
inline grannyremesh::Mesh ZoneMesh(float h, float u, float v) {
    using grannyremesh::Half;
    using grannyremesh::Snorm8;
    grannyremesh::Mesh m;
    m.scale = 1.0f;
    m.mn[0] = -1.0f; m.mn[1] = 0.0f; m.mn[2] = -1.0f;
    m.mx[0] = 1.0f; m.mx[1] = 1.0f; m.mx[2] = 1.0f;
    h = std::isfinite(h) ? std::min(1.0f, std::max(1.0f / 4096.0f, h)) : 1.0f;
    const uint16_t hu = Half(u), hv = Half(v);
    auto vertex = [&](double px, double py, double pz, double nx, double ny, double nz, double a) {
        grannyremesh::Vertex x{};
        const double p[3] = {px, py, pz}, n[3] = {nx, ny, nz};
        for (int k = 0; k < 3; ++k) { x.pos[k] = (int16_t)std::lround(p[k] * 32767.0); x.normal[k] = Snorm8((float)n[k]); }
        x.pos[3] = 0;
        x.tangent[0] = Snorm8((float)std::cos(a)); x.tangent[1] = 0; x.tangent[2] = Snorm8((float)-std::sin(a)); x.tangent[3] = -127;
        x.uv[0] = hu;
        x.uv[1] = hv;
        m.v.push_back(x);
        return (uint16_t)(m.v.size() - 1);
    };
    auto both = [&](uint16_t a, uint16_t b, uint16_t c) {
        m.idx.insert(m.idx.end(), {a, b, c});
        m.idx.insert(m.idx.end(), {a, c, b});
    };
    const int row = kLensSegments + 1;
    const double pi = 3.14159265358979, top = std::asin((double)h);
    // the wall: kLensRings + 1 rings from the rim (elevation 0) up to the cut
    for (int i = 0; i <= kLensRings; ++i) {
        const double e = top * i / kLensRings;
        for (int j = 0; j <= kLensSegments; ++j) {
            const double a = (double)j / kLensSegments * 2.0 * pi;
            const double d[3] = {std::cos(e) * std::sin(a), std::sin(e), std::cos(e) * std::cos(a)};
            vertex(d[0], d[1], d[2], d[0], d[1], d[2], a);
        }
    }
    for (int i = 0; i < kLensRings; ++i)
        for (int j = 0; j < kLensSegments; ++j) {
            const uint16_t a = (uint16_t)(i * row + j), b = (uint16_t)(i * row + j + 1), c = (uint16_t)(a + row), d = (uint16_t)(b + row);
            both(a, b, c);
            both(b, d, c);
        }
    // the flat top at the cut (its own ring: the normal up)
    const double rt = std::cos(top), yt = std::sin(top);
    const uint16_t mid = vertex(0.0, yt, 0.0, 0.0, 1.0, 0.0, 0.0);
    const uint16_t firstTop = (uint16_t)m.v.size();
    for (int j = 0; j <= kLensSegments; ++j) {
        const double a = (double)j / kLensSegments * 2.0 * pi;
        vertex(rt * std::sin(a), yt, rt * std::cos(a), 0.0, 1.0, 0.0, a);
    }
    for (int j = 0; j < kLensSegments; ++j) both(mid, (uint16_t)(firstTop + j), (uint16_t)(firstTop + j + 1));
    // (no bottom: the liquid stands on the base's floor at y 0 and is never seen from below - a bottom there would only fight the floor)
    return m;
}
// The level a fill shows (0 none, 1 - kLevels): the nearest
inline int LevelOf(float fill) {
    if (!std::isfinite(fill) || fill <= 0.0f) return 0;
    const int k = (int)std::lround(fill * kLevels);
    return k < 0 ? 0 : k > kLevels ? kLevels : k;
}

// The file's first mesh (already ours: Remesh) followed by one more mesh for each of `more` - each a copy of the first's own object with its own
// vertices, its own topology (one group, its own triangles) and its own name ("orb_level_<n>"), sharing its material, its bones and its box - all of
// them in the root's Meshes and TriTopologies and bound by the first model (MeshBindings), in that order. What the game's mesh flipbooks are: one mesh
// of a model shown at a time (ModelInstance::SetActiveMesh). False with `why` (nothing half done is written by the caller).
inline bool AddMeshes(grannyremesh::File& f, const std::vector<grannyremesh::Mesh>& more, std::string* why) {
    using namespace grannyremesh;
    auto no = [&](const char* w) { if (why) *why = w; return false; };
    if (more.empty()) return true;
    Walk g(f);
    // a copy of an object (`bytes` long, its pointers' fixups with it) appended to section 0
    auto copy = [&](Addr src, int bytes) -> Addr {
        if (!src.ok() || bytes <= 0 || !g.In(src, (uint32_t)bytes)) return {};
        std::vector<uint8_t> b(f.sec[src.s].begin() + src.o, f.sec[src.s].begin() + src.o + bytes);
        std::vector<std::pair<uint32_t, SecPtr>> ptrs;
        for (const Fixup& x : f.fix[src.s]) if (x.at >= src.o && x.at < src.o + (uint32_t)bytes) ptrs.push_back({x.at - src.o, x.ptr});
        const Addr dst = g.Append(0, b.data(), b.size(), 8);
        for (const auto& p : ptrs) g.SetPtr(dst.at(p.first), Addr{p.second.section, p.second.offset});
        return dst;
    };
    const Addr rt{f.h.rootType.section, f.h.rootType.offset}, ro{f.h.rootObj.section, f.h.rootObj.offset};
    Addr t;
    const Addr meshes = g.Member(rt, ro, "Meshes", &t);
    if (!meshes.ok() || g.Kind(t) != 4 || g.Get<int32_t>(meshes) < 1) return no("no mesh");
    const Addr meshType = g.Ref(t), mesh0 = g.Ptr(g.Ptr(meshes.at(4)));
    const int meshBytes = g.StructBytes(meshType);
    if (!mesh0.ok() || meshBytes <= 0) return no("no mesh");
    std::vector<Addr> all{mesh0}, topos;
    Addr topoType;
    {
        Addr pt;
        const Addr p = g.Member(meshType, mesh0, "PrimaryTopology", &pt);
        if (!p.ok() || g.Kind(pt) != 2) return no("the mesh's topology is not a reference");
        topoType = g.Ref(pt);
        topos.push_back(g.Ptr(p));
    }
    for (size_t k = 0; k < more.size(); ++k) {
        const Mesh& m = more[k];
        const Addr nm = copy(mesh0, meshBytes);
        if (!nm.ok()) return no("the mesh could not be copied");
        // its name
        Addr e;
        const Addr name = g.Member(meshType, nm, "Name", &e);
        if (name.ok() && g.Kind(e) == 8) {
            char b[32];
            std::snprintf(b, sizeof b, "orb_level_%02d", (int)k + 2);
            g.SetPtr(name, g.Append(0, b, std::strlen(b) + 1, 4));
        }
        // its vertices: a copy of the vertex data object, our vertices in it
        Addr vt;
        const Addr pvdRef = g.Member(meshType, nm, "PrimaryVertexData", &vt);
        if (!pvdRef.ok() || g.Kind(vt) != 2) return no("the mesh's vertex data is not a reference");
        const Addr vdType = g.Ref(vt), pvd = copy(g.Ptr(pvdRef), g.StructBytes(vdType));
        if (!pvd.ok()) return no("the vertex data could not be copied");
        g.SetPtr(pvdRef, pvd);
        Addr va;
        const Addr verts = g.Member(vdType, pvd, "Vertices", &va);
        if (!verts.ok() || g.Kind(va) != 7) return no("no vertex array");
        g.Put<int32_t>(verts.at(8), (int32_t)m.v.size());
        g.SetPtr(verts.at(12), g.Append(0, m.v.data(), m.v.size() * sizeof(Vertex), 4));
        // its topology: a copy, one group of all its triangles, its own indices
        const Addr topoRef = g.Member(meshType, nm, "PrimaryTopology", &e);
        const Addr topo = copy(g.Ptr(topoRef), g.StructBytes(topoType));
        if (!topo.ok()) return no("the topology could not be copied");
        g.SetPtr(topoRef, topo);
        Addr gt, it16, it32;
        const Addr groups = g.Member(topoType, topo, "Groups", &gt), i16 = g.Member(topoType, topo, "Indices16", &it16), i32 = g.Member(topoType, topo, "Indices", &it32);
        if (!groups.ok() || g.Kind(gt) != 3 || !i16.ok() || g.Kind(it16) != 3) return no("the topology is not the card's");
        const Addr groupType = g.Ref(gt), g0 = copy(g.Ptr(groups.at(4)), g.StructBytes(groupType));
        if (!g0.ok()) return no("the topology's group could not be copied");
        g.SetPtr(groups.at(4), g0);
        g.Put<int32_t>(groups, 1);
        const Addr first = g.Member(groupType, g0, "TriFirst", &e), count = g.Member(groupType, g0, "TriCount", &e), mat = g.Member(groupType, g0, "MaterialIndex", &e);
        if (!first.ok() || !count.ok()) return no("the topology's group has no triangles");
        g.Put<int32_t>(first, 0);
        g.Put<int32_t>(count, (int32_t)(m.idx.size() / 3));
        if (mat.ok()) g.Put<int32_t>(mat, 0);
        if (i32.ok()) g.Put<int32_t>(i32, 0);
        g.Put<int32_t>(i16, (int32_t)m.idx.size());
        g.SetPtr(i16.at(4), g.Append(0, m.idx.data(), m.idx.size() * sizeof(uint16_t), 4));
        all.push_back(nm);
        topos.push_back(topo);
    }
    // the root's Meshes: every one
    {
        std::vector<uint8_t> zeros(all.size() * 8, 0);
        const Addr arr = g.Append(0, zeros.data(), zeros.size(), 8);
        for (size_t k = 0; k < all.size(); ++k) g.SetPtr(arr.at((uint32_t)k * 8), all[k]);
        g.Put<int32_t>(meshes, (int32_t)all.size());
        g.SetPtr(meshes.at(4), arr);
    }
    // the root's TriTopologies: every one
    {
        const Addr tts = g.Member(rt, ro, "TriTopologies", &t);
        if (tts.ok() && g.Kind(t) == 4) {
            std::vector<uint8_t> zeros(topos.size() * 8, 0);
            const Addr arr = g.Append(0, zeros.data(), zeros.size(), 8);
            for (size_t k = 0; k < topos.size(); ++k) g.SetPtr(arr.at((uint32_t)k * 8), topos[k]);
            g.Put<int32_t>(tts, (int32_t)topos.size());
            g.SetPtr(tts.at(4), arr);
        }
    }
    // the first model binds them all, in order
    const Addr models = g.Member(rt, ro, "Models", &t);
    if (!models.ok() || g.Kind(t) != 4 || g.Get<int32_t>(models) < 1) return no("no model");
    const Addr modelType = g.Ref(t), model = g.Ptr(g.Ptr(models.at(4)));
    Addr bt;
    const Addr mb = g.Member(modelType, model, "MeshBindings", &bt);
    if (!mb.ok() || g.Kind(bt) != 3 || g.Get<int32_t>(mb) < 1) return no("the model binds no mesh");
    const Addr bindType = g.Ref(bt), b0 = g.Ptr(mb.at(4));
    const int bindBytes = g.StructBytes(bindType);
    if (bindBytes <= 0) return no("the model's binding has no size");
    Addr arr;
    for (size_t k = 0; k < all.size(); ++k) {
        const Addr b = copy(b0, bindBytes);
        if (!b.ok()) return no("the model's binding could not be copied");
        if (k == 0) arr = b;
        else if (b.s != arr.s || b.o != arr.o + (uint32_t)(k * bindBytes)) return no("the bindings did not land one after another");
        Addr e;
        const Addr ref = g.Member(bindType, b, "Mesh", &e);
        if (!ref.ok()) return no("the model's binding names no mesh");
        g.SetPtr(ref, all[k]);
    }
    g.Put<int32_t>(mb, (int32_t)all.size());
    g.SetPtr(mb.at(4), arr);
    return true;
}

// ---- the liquid's colour: one texel of the potion's own picture -------------------------------------------------------------------------------------

constexpr float kUvBandLow = 0.50f, kUvBandHigh = 0.60f;   // of the glass mesh's height: the band whose texels are the liquid (both potions)
constexpr float kGlassBandLow = 0.90f, kGlassBandHigh = 1.01f;   // the lesser mana potion's glass mesh's top tenth: empty glass (82 85 107; 0.214.5 - 0.215.7)
constexpr float kPlateBandLow = 0.20f, kPlateBandHigh = 0.30f;   // of the cork-and-cap mesh's height: the gold cap (189 142 99)
// The uv of the first vertex (in the file's order) of the mesh whose height is in the band - else the one nearest its middle. False: no vertices.
inline bool LiquidUv(const grannyremesh::Mesh& glass, float* u, float* v, float* height = nullptr, float bandLow = kUvBandLow, float bandHigh = kUvBandHigh) {
    if (glass.v.empty()) return false;
    int lo = 32767, hi = -32768;
    for (const grannyremesh::Vertex& x : glass.v) { lo = std::min<int>(lo, x.pos[1]); hi = std::max<int>(hi, x.pos[1]); }
    const float span = (float)(hi - lo);
    if (!(span > 0.0f)) return false;
    int pick = -1;
    float best = 1e30f;
    for (size_t i = 0; i < glass.v.size(); ++i) {
        const float f = (float)(glass.v[i].pos[1] - lo) / span;
        if (f >= bandLow && f < bandHigh) { pick = (int)i; break; }
        const float d = std::fabs(f - 0.5f * (bandLow + bandHigh));
        if (d < best) { best = d; pick = (int)i; }
    }
    auto toF = [](uint16_t h) {   // half -> float
        const uint32_t s = (uint32_t)(h & 0x8000u) << 16, e = (h >> 10) & 0x1Fu, mm = h & 0x3FFu;
        uint32_t bits;
        if (e == 0) { const float r = std::ldexp((float)mm, -24); return (h & 0x8000u) ? -r : r; }
        if (e == 31) bits = s | 0x7F800000u | (mm << 13);
        else bits = s | ((e + 112u) << 23) | (mm << 13);
        float out;
        std::memcpy(&out, &bits, 4);
        return out;
    };
    *u = toF(glass.v[(size_t)pick].uv[0]);
    *v = toF(glass.v[(size_t)pick].uv[1]);
    if (height) *height = (float)(glass.v[(size_t)pick].pos[1] - lo) / span;
    return std::isfinite(*u) && std::isfinite(*v);
}

// ---- the material: renamed ours, its own glow ------------------------------------------------------------------------------------------------------

// The first mesh's material (its MaterialBindings[0]): its name made `name` (a new string; every other user of the old one keeps it) and its
// ExtendedData's EmissiveIntensity `emissive` (< 0: set to 0 where it has one, left where it has none). Found by the file's own types. False with `why`.
inline bool EditMaterial(grannyremesh::File& f, const std::string& name, float emissive, std::string* oldName, float* oldEmissive, std::string* why) {
    using namespace grannyremesh;
    auto no = [&](const char* w) { if (why) *why = w; return false; };
    Walk g(f);
    const Addr rt{f.h.rootType.section, f.h.rootType.offset}, ro{f.h.rootObj.section, f.h.rootObj.offset};
    Addr t;
    const Addr meshes = g.Member(rt, ro, "Meshes", &t);
    if (!meshes.ok() || g.Kind(t) != 4 || g.Get<int32_t>(meshes) < 1) return no("no mesh");
    const Addr meshType = g.Ref(t), mesh = g.Ptr(g.Ptr(meshes.at(4)));
    Addr bt;
    const Addr mb = g.Member(meshType, mesh, "MaterialBindings", &bt);
    if (!mb.ok() || g.Kind(bt) != 3 || g.Get<int32_t>(mb) < 1) return no("the mesh binds no material");
    Addr mt;
    const Addr ref = g.Member(g.Ref(bt), g.Ptr(mb.at(4)), "Material", &mt);
    if (!ref.ok()) return no("the mesh's binding names no material");
    const Addr mat = g.Ptr(ref), matType = g.Ref(mt);
    if (!mat.ok() || !matType.ok()) return no("the material is not in the file");
    Addr nt;
    const Addr nameAt = g.Member(matType, mat, "Name", &nt);
    if (!nameAt.ok() || g.Kind(nt) != 8) return no("the material has no name");
    if (oldName) *oldName = g.Str(g.Ptr(nameAt));
    Addr xt;
    const Addr ext = g.Member(matType, mat, "ExtendedData", &xt);
    if (!ext.ok() || g.Kind(xt) != 5) return no("the material has no extended data");
    Addr e;
    const Addr em = g.Member(g.Ptr(ext), g.Ptr(ext.at(8)), "EmissiveIntensity", &e);
    const bool hasEm = em.ok() && g.Kind(e) == 10;
    if (!hasEm && emissive >= 0.0f) return no("the material has no EmissiveIntensity (a float)");
    if (oldEmissive) *oldEmissive = hasEm ? g.Get<float>(em) : 0.0f;
    if (hasEm) g.Put<float>(em, emissive < 0.0f ? 0.0f : emissive);
    const Addr str = g.Append(0, name.c_str(), name.size() + 1, 4);
    g.SetPtr(nameAt, str);
    return true;
}

// The first mesh's index among the file's meshes by its name; -1 none
inline int MeshIndex(grannyremesh::File& f, const char* name) {
    using namespace grannyremesh;
    Walk g(f);
    const Addr rt{f.h.rootType.section, f.h.rootType.offset}, ro{f.h.rootObj.section, f.h.rootObj.offset};
    Addr t;
    const Addr meshes = g.Member(rt, ro, "Meshes", &t);
    if (!meshes.ok() || g.Kind(t) != 4) return -1;
    const Addr meshType = g.Ref(t), arr = g.Ptr(meshes.at(4));
    for (int32_t k = 0; k < g.Get<int32_t>(meshes) && k < 4096; ++k) {
        Addr nt;
        const Addr n = g.Member(meshType, g.Ptr(arr.at((uint32_t)k * 8)), "Name", &nt);
        if (n.ok() && g.Str(g.Ptr(n)) == name) return k;
    }
    return -1;
}

// The first model's InitialPlacement made the identity (flags 0, no move, no turn, scale 1): the potions' models carry a placement of their own (about
// -0.02, 0.6, -0.16 model units: the bottle stood up off its origin), which the game applies before the unit's matrix - scaled by our matrices it lifted
// and shifted each part by its own size (0.215.11: the liquid narrower than the dome's opening and egg-like, the base up off the skin). False: no model.
inline bool ResetPlacement(grannyremesh::File& f) {
    using namespace grannyremesh;
    Walk g(f);
    const Addr rt{f.h.rootType.section, f.h.rootType.offset}, ro{f.h.rootObj.section, f.h.rootObj.offset};
    Addr t;
    const Addr models = g.Member(rt, ro, "Models", &t);
    if (!models.ok() || g.Kind(t) != 4 || g.Get<int32_t>(models) < 1) return false;
    const Addr modelType = g.Ref(t), model = g.Ptr(g.Ptr(models.at(4)));
    Addr e;
    const Addr ip = g.Member(modelType, model, "InitialPlacement", &e);
    if (!ip.ok() || g.Kind(e) != 9) return false;
    const float id[16] = {0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 1};   // pos, rot (x y z w), scale / shear 3 x 3
    g.Put<uint32_t>(ip, 0);
    for (int c = 0; c < 16; ++c) g.Put<float>(ip.at(4 + c * 4), id[c]);
    return true;
}

// The game's .texture files a model file names (its root Textures' FromFileName): the unit's dependencies
inline std::vector<std::string> TexturesOf(grannyremesh::File& f) {
    using namespace grannyremesh;
    std::vector<std::string> out;
    Walk g(f);
    const Addr rt{f.h.rootType.section, f.h.rootType.offset}, ro{f.h.rootObj.section, f.h.rootObj.offset};
    Addr t;
    const Addr tex = g.Member(rt, ro, "Textures", &t);
    if (!tex.ok() || g.Kind(t) != 4) return out;
    const Addr texType = g.Ref(t), arr = g.Ptr(tex.at(4));
    for (int32_t k = 0; k < g.Get<int32_t>(tex) && k < 256; ++k) {
        Addr e;
        const Addr n = g.Member(texType, g.Ptr(arr.at((uint32_t)k * 8)), "FromFileName", &e);
        const std::string s = n.ok() ? g.Str(g.Ptr(n)) : std::string();
        if (s.size() > 8 && s.compare(s.size() - 8, 8, ".texture") == 0 && s.rfind("data/", 0) == 0) {
            bool have = false;
            for (const std::string& o : out) have = have || o == s;
            if (!have) out.push_back(s);
        }
    }
    return out;
}

// ---- the units ------------------------------------------------------------------------------------------------------------------------------------

// A unit of ours: one entity with `model`, its textures as dependencies. lightMask 19 (the game's lights reach it), shadowMask 0 (it throws none).
inline std::string UnitJson(const std::string& name, const std::string& model, const std::vector<std::string>& textures) {
    std::string tex;
    for (const std::string& t : textures) tex += (tex.empty() ? "" : ",") + std::string(R"({"path":")") + t + R"("})";
    return R"({"dependencies":{"particles":[],"models":[{"path":")" + model + R"("}],"skeletons":[],"animations":[],"textures":[)" + tex +
           R"(],"physics":[],"json":[],"variantdata":[],"objecteffects":[],"other":[]},"type":"UnitDefinition","name":"d2r_vr_)" + name +
           R"(","entities":[{"type":"Entity","name":"entity_root","id":1000000003,"components":[{"type":"UnitRootComponent","name":"component_root",)"
           R"("state_machine_filename":"","doNotInheritRotation":false,"rotationOverride":{"x":0.0,"y":0.0,"z":0.0,"w":1.0},)"
           R"("doNotUseHDHeight":true,"hideAllMeshWhenInOpenedMode":false,"onCreateEventName":"","animations":[]},)"
           R"({"type":"TransformDefinitionComponent","name":"entity_root_TransformDefinition","position":{"x":0.0,"y":0.0,"z":0.0},)"
           R"("orientation":{"x":0.0,"y":0.0,"z":0.0,"w":1.0},"scale":{"x":1.0,"y":1.0,"z":1.0},"inheritOnlyPosition":false}]},)"
           R"({"type":"Entity","name":"entity_model","id":1000000004,"components":[{"type":"ModelDefinitionComponent",)"
           R"("name":"component_model_)" + name + R"(","filename":")" + model + R"(",)"
           R"("visibleLayers":1,"lightMask":19,"shadowMask":0,"ghostShadows":false,"floorModel":false,"terrainBlendEnableYUpBlend":false,)"
           R"("terrainBlendMode":1}]}]})";
}

// ---- what is made ---------------------------------------------------------------------------------------------------------------------------------

struct Liquid {
    float u = 0.0f, v = 0.0f, height = 0.0f;   // the texel taken, and where on the glass mesh it was
    std::string oldMaterial;
    float oldEmissive = 0.0f;
    std::vector<std::string> textures;
};
struct Made {
    std::vector<std::pair<std::string, std::string>> files;   // (path under the mod's data root "data/...", bytes)
    Liquid liquid[2];
    Liquid glass, plate;   // (their texel and material: the same record)
    std::string why;   // what failed (nothing written then)
};

// One part: `source`'s file cut to its mesh `meshName`, our shape in it (salted), every uv the texel of `band` of that mesh's height, the material
// renamed `material` with EmissiveIntensity `emissive` (< 0: none).
struct Part {
    const char* source;
    const char* meshName;
    float bandLow, bandHigh;
    const char* material;
    float emissive;
    bool closed;   // the lens with its bottom (the liquid, the base) or the open dome (the glass)
    unsigned salt;
    bool levels = false;   // the liquid: kLevels meshes, the dome cut flat at each level (ZoneMesh), in place of the one lens
    bool base = false;     // the base: the effect's plate with its bezel (BaseMesh)
};
inline bool MakePart(const Reader& read, const Part& p, std::string* bytes, Liquid* info, std::string* why) {
    std::string src;
    if (!read(p.source, &src)) { *why = std::string("could not read ") + p.source; return false; }
    grannyremesh::File f;
    std::string w;
    if (!grannyremesh::Read(src, &f, &w)) { *why = std::string(p.source) + ": " + w; return false; }
    const int k = MeshIndex(f, p.meshName);
    if (k < 0) { *why = std::string(p.source) + ": no mesh " + p.meshName; return false; }
    if (!grannyremesh::KeepOnlyMesh(f, k, &w)) { *why = std::string(p.source) + ": " + w; return false; }
    grannyremesh::Mesh mesh;
    if (!grannyremesh::MeshOf(f, &mesh, &w)) { *why = std::string(p.source) + " " + p.meshName + ": " + w; return false; }
    if (!LiquidUv(mesh, &info->u, &info->v, &info->height, p.bandLow, p.bandHigh)) { *why = std::string(p.source) + ": no texel in " + p.meshName; return false; }
    if (!EditMaterial(f, p.material, p.emissive, &info->oldMaterial, &info->oldEmissive, &w)) { *why = std::string(p.source) + ": " + w; return false; }
    grannyremesh::Mesh lens = p.levels ? ZoneMesh(1.0f / kLevels, info->u, info->v) : p.base ? BaseMesh(info->u, info->v) : LensMesh(info->u, info->v, p.closed);
    grannyremesh::Salt(&lens, p.salt);
    grannyremesh::Done done;
    if (!grannyremesh::Remesh(f, lens, grannyremesh::Renames{}, 0, &done, &w)) { *why = std::string(p.source) + ": " + w; return false; }
    if (!ResetPlacement(f)) { *why = std::string(p.source) + ": its model has no InitialPlacement"; return false; }
    if (p.levels) {   // levels 2 .. kLevels after it: one mesh each
        std::vector<grannyremesh::Mesh> more;
        for (int k = 2; k <= kLevels; ++k) more.push_back(ZoneMesh((float)k / kLevels, info->u, info->v));
        if (!AddMeshes(f, more, &w)) { *why = std::string(p.source) + ": the levels: " + w; return false; }
    }
    info->textures = TexturesOf(f);
    *bytes = grannyremesh::Write(f);
    return true;
}
// One liquid: the potion's glass mesh's material, its liquid's texel, glowing.
inline bool MakeLiquid(const Reader& read, int kind, float glow, std::string* bytes, Liquid* info, std::string* why) {
    return MakePart(read, Part{kSource[kind], kSourceMesh, kUvBandLow, kUvBandHigh, kMaterialName[kind], GlowClamp(glow), true, (unsigned)kind + 1, true}, bytes, info, why);
}

// One metal's base: the potion's file cut to its mesh holding the picture, the plate in it twice - [0] clean (one texel), [1] aged (the rosette laid across
// it) - the material renamed ours, the potion's placement reset.
inline bool MakePlate(const Reader& read, int metal, std::string* bytes, Liquid* info, std::string* why) {
    const PlateMetal& pm = kPlateMetal[metal];
    std::string src;
    if (!read(pm.source, &src)) { *why = std::string("could not read ") + pm.source; return false; }
    grannyremesh::File f;
    std::string w;
    if (!grannyremesh::Read(src, &f, &w)) { *why = std::string(pm.source) + ": " + w; return false; }
    const int k = MeshIndex(f, pm.sourceMesh);
    if (k < 0) { *why = std::string(pm.source) + ": no mesh " + pm.sourceMesh; return false; }
    if (!grannyremesh::KeepOnlyMesh(f, k, &w)) { *why = std::string(pm.source) + ": " + w; return false; }
    if (!EditMaterial(f, pm.material, -1.0f, &info->oldMaterial, &info->oldEmissive, &w)) { *why = std::string(pm.source) + ": " + w; return false; }
    grannyremesh::Mesh clean = BaseMesh(pm.cleanU, pm.cleanV);
    grannyremesh::Salt(&clean, pm.salt);
    grannyremesh::Done done;
    if (!grannyremesh::Remesh(f, clean, grannyremesh::Renames{}, 0, &done, &w)) { *why = std::string(pm.source) + ": " + w; return false; }
    if (!ResetPlacement(f)) { *why = std::string(pm.source) + ": its model has no InitialPlacement"; return false; }
    if (!AddMeshes(f, {BaseMesh(pm.cu, pm.cv, pm.r)}, &w)) { *why = std::string(pm.source) + ": the aged base: " + w; return false; }
    info->u = pm.cleanU;
    info->v = pm.cleanV;
    info->textures = TexturesOf(f);
    *bytes = grannyremesh::Write(f);
    return true;
}

// Everything: the two liquids, the glass, the three bases and the six units.
inline bool Make(const Reader& read, float glow, Made* out) {
    *out = Made{};
    std::string bytes;
    for (int kind = 0; kind < 2; ++kind) {
        if (!MakeLiquid(read, kind, glow, &bytes, &out->liquid[kind], &out->why)) return false;
        out->files.push_back({kLiquidFile[kind], bytes});
        out->files.push_back({kLiquidUnit[kind], UnitJson(kind == kLife ? "wrist_orb_life" : "wrist_orb_mana", kLiquidModel[kind], out->liquid[kind].textures)});
    }
    if (!MakePart(read, Part{kGlassSource, kGlassSourceMesh, kGlassBandLow, kGlassBandHigh, kGlassMaterial, -1.0f, false, 3u}, &bytes, &out->glass, &out->why)) return false;
    out->files.push_back({kGlassFile, bytes});
    out->files.push_back({kGlassUnit, UnitJson("wrist_orb_glass", kGlassModel, out->glass.textures)});
    for (int metal = 0; metal < 3; ++metal) {
        Liquid info;
        if (!MakePlate(read, metal, &bytes, &info, &out->why)) return false;
        if (metal == 0) out->plate = info;
        out->files.push_back({kPlateMetal[metal].file, bytes});
        out->files.push_back({kPlateMetal[metal].unit, UnitJson(std::string("wrist_orb_plate_") + kPlateMetal[metal].name, kPlateMetal[metal].model, info.textures)});
    }
    return true;
}

// Every file Make may write: ours to take out again
inline std::vector<std::string> OurPaths() {
    return {kLiquidFile[0], kLiquidFile[1], kLiquidUnit[0], kLiquidUnit[1], kGlassFile, kGlassUnit, kPlateMetal[0].file, kPlateMetal[0].unit,
            kPlateMetal[1].file, kPlateMetal[1].unit, kPlateMetal[2].file, kPlateMetal[2].unit};
}

}  // namespace wristorbs
