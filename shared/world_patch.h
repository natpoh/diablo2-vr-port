// world_patch.h - [holes] the patch over one hole of the world (shared/world_holes_scene.h finds them): geometry generated from the game's own
// model edges, never placed by hand. Pure C++; D2R VR Settings makes the patch's model with it (tools/settings/holes_assets.cpp), the survey tool
// checks it (tools/world_holes: `patch`).
//
// A hole is a gap beside a floor one walks on whose rim is an OPEN edge of that floor (the surface ends in the air: the isometric camera, looking
// down over the wall beyond, never saw the strip). For every open edge of a walkable floor that borders the hole (the cell just past the edge, on
// its open side, is in the hole), from both of its ends a level ray goes out across the hole (at the edge's own height, a hair above it):
//   - it meets an upright face of a solid piece within the reach (a parapet, a wall): a FLOOR STRIP - the quad from the edge to where the rays met
//     the face, at the edge's height: the floor carried on to the wall. (The bridge out of the Rogue Encampment: the deck to its parapet.)
//   - it meets nothing: a SKIRT - the quad from the edge straight down to the surface under the hole just past the edge (the trench's own side,
//     which the floor never had). (The monastery courtyard's drains: the courtyard's floor ends in the air over the lower sheet.)
// The strip faces up, the skirt faces out over the hole (front side: the game's winding, (b - a) x (c - a) along the normal). Every vertex comes
// from the edge's own vertices, the rays' hits on the game's faces, or the heights the game's surfaces have there.
// Then the SIDES (Sides, below): the same floors' other open edges standing over lower ground - the ramps at the deck's ends, open at their side
// from the front - each closed by a skirt down to the ground.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "granny_remesh.h"
#include "world_holes_scene.h"

namespace worldpatch {

constexpr float kPiece = 1.0f;   // the longest piece of an edge one strip or skirt is made from (units)

struct Patch {
    std::vector<float> tri;        // preset space, 9 floats a triangle (the game's winding)
    std::vector<float> nrm;        // each triangle's normal, 3 floats
    int strips = 0, skirts = 0, edges = 0;   // quads of each kind, open edges on the rim
    float area = 0.0f;
    float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
    std::map<std::pair<int, uint32_t>, float> rim;   // (placement, mesh) of the open edges it carries on, and how long they are
    std::set<std::pair<int, size_t>> used;            // (placement, open edge) each strip or skirt over the hole was made from
    int sides = 0;                                    // SIDE skirts (Sides): the rim floors' other open edges over lower ground
    int sideStrips = 0;                               // SIDE strips (Sides): such an edge carried on level to those same walls (the ramps)
    float sideLength = 0.0f, sideLeft = 0.0f;         // how long those edges are; how long the ones left open are (over nothing within the reach)
};

namespace detail {

inline bool RayTri(const float o[3], const float d[3], const float* a, const float* b, const float* c, float* t) {
    const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    const float p[3] = {d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0]};
    const float det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
    if (std::fabs(det) < 1e-9f) return false;
    const float inv = 1.0f / det, s[3] = {o[0] - a[0], o[1] - a[1], o[2] - a[2]};
    const float u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) * inv;
    if (u < 0.0f || u > 1.0f) return false;
    const float q[3] = {s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0]};
    const float v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * inv;
    if (v < 0.0f || u + v > 1.0f) return false;
    *t = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
    return *t > 1e-4f;
}

inline void AddQuad(Patch* p, const float* a, const float* b, const float* c, const float* d, const float want[3]) {
    // two triangles a b c, a c d - turned round if their front side does not look along `want`
    float n[3];
    worldholes::TriNormal(a, b, c, n);
    const bool flip = n[0] * want[0] + n[1] * want[1] + n[2] * want[2] < 0.0f;
    const float* q[2][3] = {{a, b, c}, {a, c, d}};
    for (auto& t : q) {
        if (flip) std::swap(t[1], t[2]);
        for (int k = 0; k < 3; ++k) {
            p->tri.insert(p->tri.end(), t[k], t[k] + 3);
            for (int c2 = 0; c2 < 3; ++c2) { p->mn[c2] = std::min(p->mn[c2], t[k][c2]); p->mx[c2] = std::max(p->mx[c2], t[k][c2]); }
        }
        float m[3];
        worldholes::TriNormal(t[0], t[1], t[2], m);
        p->nrm.insert(p->nrm.end(), m, m + 3);
        const float u[3] = {t[1][0] - t[0][0], t[1][1] - t[0][1], t[1][2] - t[0][2]}, v[3] = {t[2][0] - t[0][0], t[2][1] - t[0][1], t[2][2] - t[0][2]};
        p->area += 0.5f * std::sqrt(std::pow(u[1] * v[2] - u[2] * v[1], 2.0f) + std::pow(u[2] * v[0] - u[0] * v[2], 2.0f) + std::pow(u[0] * v[1] - u[1] * v[0], 2.0f));
    }
}

}  // namespace detail



// A piece of the level's own build: under data/hd/env/model/act<n>/ or expansion/ (walls, bridges, parapets, stones laid in the ground) - never
// a prop of global/prop (knockable: [weight] knock_props moves them), the terrain, or decor (plants, grime)
inline bool Built(const std::string& model) {
    if (worldscene::Decor(model) || model.find("/terrain") != std::string::npos || model.find("/prop/") != std::string::npos) return false;
    return model.rfind("data/hd/env/model/act", 0) == 0 || model.rfind("data/hd/env/model/expansion/", 0) == 0;
}

// The holes one place patches: the hole at its point and every other hole over the same stretch (its bounds and a unit round, its floor within
// the drop: a strip the piers or a ramp cut into pieces) - each region's index set in the mask; `span` their bounds together.
inline std::vector<uint8_t> Cluster(const std::vector<worldscene::Gap>& gaps, int best, worldscene::Gap* span, float drop = 2.5f) {
    std::vector<uint8_t> mask(gaps.size(), 0);
    if (best < 0 || (size_t)best >= gaps.size()) return mask;
    const worldscene::Gap& b = gaps[(size_t)best];
    *span = b;
    for (size_t k = 0; k < gaps.size(); ++k) {
        const worldscene::Gap& g = gaps[k];
        if ((int)k != best && (!g.Hole() || g.mx[0] < b.mn[0] - 1.0f || g.mn[0] > b.mx[0] + 1.0f || g.mx[1] < b.mn[1] - 1.0f || g.mn[1] > b.mx[1] + 1.0f ||
                               std::fabs(g.floorY - b.floorY) > drop * 0.6f))
            continue;
        mask[k] = 1;
        for (int c = 0; c < 2; ++c) { span->mn[c] = std::min(span->mn[c], g.mn[c]); span->mx[c] = std::max(span->mx[c], g.mx[c]); }
        if ((int)k != best) span->area += g.area;
    }
    return mask;
}

// The patch over the regions set in `regions` (indices of Gaps' result; `labels` its labels over `hm`, `rims` its rims), `g` their bounds, from
// the open edges of the walkable floors `pl` places; `reach` and `drop` as Gaps'.
inline Patch Make(const std::vector<worldscene::Placement>& pl, worldscene::ModelCache& cache, const worldscene::HeightMap& hm,
                  const std::vector<int32_t>& labels, const std::vector<float>& rims, const std::vector<uint8_t>& regions, const worldscene::Gap& g,
                  float reach = 4.0f, float drop = 2.5f) {
    auto inHole = [&](int32_t l) { return l >= 0 && (size_t)l < regions.size() && regions[(size_t)l]; };
    using namespace worldscene;
    Patch out;
    auto cellOf = [&](float x, float z, size_t* q) {
        const int i = (int)std::floor((x - hm.x0) / hm.cell), j = (int)std::floor((z - hm.z0) / hm.cell);
        if (i < 0 || j < 0 || i >= hm.nx || j >= hm.nz) return false;
        *q = (size_t)j * hm.nx + i;
        return true;
    };
    const float pad = reach + 1.0f;
    const float bx0 = g.mn[0] - pad, bx1 = g.mx[0] + pad, bz0 = g.mn[1] - pad, bz1 = g.mx[1] + pad;
    // the solid faces round the hole, for the rays
    std::vector<float> faces;
    for (const Placement& p : pl) {
        if (Decor(p.model)) continue;
        const ModelTris& t = cache.Get(p.model);
        for (size_t i = 0; i + 8 < t.p.size(); i += 9) {
            float a[3], b[3], c[3];
            Apply(p.M, &t.p[i], a); Apply(p.M, &t.p[i + 3], b); Apply(p.M, &t.p[i + 6], c);
            if (std::max({a[0], b[0], c[0]}) < bx0 || std::min({a[0], b[0], c[0]}) > bx1 || std::max({a[2], b[2], c[2]}) < bz0 || std::min({a[2], b[2], c[2]}) > bz1) continue;
            faces.insert(faces.end(), a, a + 3); faces.insert(faces.end(), b, b + 3); faces.insert(faces.end(), c, c + 3);
        }
    }
    auto ray = [&](const float o[3], const float d[3], float maxT, float* hitT) {
        float best = maxT;
        bool any = false;
        for (size_t i = 0; i + 8 < faces.size(); i += 9) {
            float t;
            if (!detail::RayTri(o, d, &faces[i], &faces[i + 3], &faces[i + 6], &t) || t >= best) continue;
            float n[3];
            worldholes::TriNormal(&faces[i], &faces[i + 3], &faces[i + 6], n);
            if (std::fabs(n[1]) > 0.7071f) continue;   // only an upright face stops it (a wall, a parapet)
            best = t;
            any = true;
        }
        *hitT = best;
        return any;
    };
    for (const Placement& p : pl) {
        if (Decor(p.model) || !p.walk) continue;
        const ModelTris& t = cache.Get(p.model);
        for (size_t i = 0; i + 5 < t.open.size(); i += 6) {
            float a[3], b[3];
            Apply(p.M, &t.open[i], a);
            Apply(p.M, &t.open[i + 3], b);
            const float ex = b[0] - a[0], ez = b[2] - a[2], len = std::hypot(ex, ez);
            if (len < 1e-3f) continue;
            const float mid[3] = {(a[0] + b[0]) * 0.5f, (a[1] + b[1]) * 0.5f, (a[2] + b[2]) * 0.5f};
            if (mid[0] < bx0 || mid[0] > bx1 || mid[2] < bz0 || mid[2] > bz1) continue;
            // its open side: the level way across it whose cell half a cell out is in this hole, lower than the edge
            float out3[3] = {0, 0, 0};
            bool found = false;
            for (int sgn = -1; sgn <= 1 && !found; sgn += 2) {
                const float nx = sgn * ez / len, nz = -sgn * ex / len;
                // just past it, out to three units (a ledge or a rock lower than the floor may lie along it first); a cell as high as the edge on the
                // way: a floor goes on past this edge - not the rim (the outermost edge only, never a lower floor's under it)
                const int steps = (int)std::ceil(3.0f / hm.cell);
                for (int step = 0; step < steps && !found; ++step) {
                    size_t q;
                    if (!cellOf(mid[0] + nx * hm.cell * (0.6f + step), mid[2] + nz * hm.cell * (0.6f + step), &q)) break;
                    if (hm.h[q] > mid[1] - 0.3f) break;
                    // ... and the edge is that cell's rim: the floor the hole was found beside, at its height (a parapet's top edge over it is not)
                    if (inHole(labels[q]) && hm.h[q] < mid[1] - drop * 0.5f && std::fabs(rims[q] - mid[1]) < 0.6f) { out3[0] = nx; out3[2] = nz; found = true; }
                }
            }
            if (!found) continue;
            ++out.edges;
            out.used.insert({(int)(&p - pl.data()), i / 6});
            out.rim[{(int)(&p - pl.data()), t.openMesh[i / 6]}] += std::hypot(len, b[1] - a[1]);
            // in pieces of at most a unit along the edge (each piece's rays meet the faces before it: a pier standing in the strip, the parapet)
            const int pieces = std::max(1, (int)std::ceil(len / kPiece));
            const float a0[3] = {a[0], a[1], a[2]}, b0[3] = {b[0], b[1], b[2]};
            for (int piece = 0; piece < pieces; ++piece) {
            const float u0 = (float)piece / pieces, u1 = (float)(piece + 1) / pieces;
            for (int c = 0; c < 3; ++c) { a[c] = a0[c] + (b0[c] - a0[c]) * u0; b[c] = a0[c] + (b0[c] - a0[c]) * u1; }
            // from both ends, a level ray out across the hole, a hair above the edge
            float hitA = 0, hitB = 0;
            const float oa[3] = {a[0], a[1] + 0.05f, a[2]}, ob[3] = {b[0], b[1] + 0.05f, b[2]};
            const bool wa = ray(oa, out3, reach + 0.5f, &hitA), wb = ray(ob, out3, reach + 0.5f, &hitB);
            if (wa && wb && std::max(hitA, hitB) < hm.cell * 2.0f) continue;   // (a sliver: the edge stands against its own wall - no gap here)
            if (wa && wb) {
                const float qa[3] = {a[0] + out3[0] * hitA, a[1], a[2] + out3[2] * hitA}, qb[3] = {b[0] + out3[0] * hitB, b[1], b[2] + out3[2] * hitB};
                const float up[3] = {0, 1, 0};
                detail::AddQuad(&out, a, b, qb, qa, up);
                ++out.strips;
            } else {
                // down to the surface under the hole just past each end (nothing there: the drop the hole is found by, twice)
                auto bottom = [&](const float* e) {
                    size_t q;
                    if (cellOf(e[0] + out3[0] * hm.cell * 1.5f, e[2] + out3[2] * hm.cell * 1.5f, &q) && hm.h[q] > kNone && hm.h[q] < e[1]) return hm.h[q];
                    return e[1] - drop * 2.0f;
                };
                const float da[3] = {a[0], bottom(a), a[2]}, db[3] = {b[0], bottom(b), b[2]};
                detail::AddQuad(&out, a, b, db, da, out3);
                ++out.skirts;
            }
            }   // (the pieces)
        }
    }
    return out;
}

// One region alone
inline Patch Make(const std::vector<worldscene::Placement>& pl, worldscene::ModelCache& cache, const worldscene::HeightMap& hm,
                  const std::vector<int32_t>& labels, const std::vector<float>& rims, int32_t region, const worldscene::Gap& g, float reach = 4.0f,
                  float drop = 2.5f) {
    std::vector<uint8_t> one((size_t)std::max<int32_t>(region + 1, 1), 0);
    if (region >= 0) one[(size_t)region] = 1;
    return Make(pl, cache, hm, labels, rims, one, g, reach, drop);
}

// The SIDES the rim's floors never had (Serg on 0.209.8 in flat, at the camp bridge: "мост сверху закрыт а спереди не закрыт" - the ramps down
// from the deck stand on nothing at their sides: seen from the front, or from the deck's height, a slot under the ramp's edge down to the grass).
// The floors whose edges the patch carries on (Patch::rim's placements, each whole: the deck with the ramps at both its ends) are walked over
// again. Every other open edge of theirs (not one Make used), in pieces of at most a unit, gets a SIDE skirt - the quad from the edge straight
// down to the ground past it (a hair into it), facing out - when:
//   - the ground just past it (a ray straight down a quarter unit out, onto the game's own up-facing faces) lies lower than the edge by at least
//     `minDrop` at one of its ends, while just inside the floor itself goes on (the same ray a quarter unit in meets it at the edge's height);
//   - nothing covers it there (no up-facing face just above the edge - the game's or the patch's own strips: an edge buried in the ground, a
//     step under a wall's top, the parapet's foot under the floor strip);
//   - no upright face closes it at its own height (a level ray out from inside it, just under its top, meets a wall within 0.6 units past it:
//     the edge runs into a wall, which is its side - under that wall may be the bridge's own arch, never to be shut);
//   - the place past it is not enclosed (a hollow wall the edge runs into: level rays both ways across the edge and one straight up, from just
//     past it, all meet a face within 4 units);
//   - it is not above the floor the hole was found beside (`floorTop`, plus half a unit: a wall's cap, a pillar's top - no floor one walks
//     on from the deck; the ramps go down from it).
// Where the gap past such an edge is a slot to a wall - the deck's strip going on beside the ramp (Serg on 0.209.10: "в мосте дырка не
// заделана", the slit between the ramp and the parapet's foot seen from the deck): level rays from both its ends, a hair above it, meet within 4
// units an upright face of the level's own build (Built: a parapet, never a prop or a plant) that stands along the edge and looks back at it,
// the two hits within two units of each other (the parapet steps), the ground at least half a unit down at one end - a SIDE STRIP instead: the quad from the edge level out to
// those hits, at the edge's own height along its slope. No skirt under it (the strip shuts the slot; under it the ground).
// Nothing under it within `maxDrop`: left open (a drop the hole's own rules are for), counted in sideLeft. Every vertex is the edge's own or the
// height of the game's ground under it.
inline void Sides(const std::vector<worldscene::Placement>& pl, worldscene::ModelCache& cache, Patch* out, float floorTop, bool sideStrips = true,
                  float minDrop = 0.3f, float maxDrop = 10.0f) {
    using namespace worldscene;
    std::set<int> owners;
    for (const auto& kv : out->rim) if (kv.first.first >= 0 && (size_t)kv.first.first < pl.size()) owners.insert(kv.first.first);
    if (owners.empty()) return;
    float bx0 = 1e30f, bx1 = -1e30f, bz0 = 1e30f, bz1 = -1e30f;
    for (int o : owners) {
        const ModelTris& t = cache.Get(pl[(size_t)o].model);
        for (size_t i = 0; i + 2 < t.p.size(); i += 3) {
            float w[3];
            Apply(pl[(size_t)o].M, &t.p[i], w);
            bx0 = std::min(bx0, w[0]); bx1 = std::max(bx1, w[0]); bz0 = std::min(bz0, w[2]); bz1 = std::max(bz1, w[2]);
        }
    }
    if (bx0 > bx1) return;
    bx0 -= 6.0f; bx1 += 6.0f; bz0 -= 6.0f; bz1 += 6.0f;   // (the strips' reach, 4, and 2 more: a parapet three units past a ramp's edge)
    // the game's faces round them: the up-facing ones (the ground under an edge, the floor inside it) and the upright ones (a wall closing it),
    // each with its bounds (x0 x1 y0 y1 z0 z1)
    struct Face { float v[9]; float b[6]; bool built = false; };
    std::vector<Face> ups, walls, all;
    for (const Placement& p : pl) {
        if (Decor(p.model)) continue;
        const ModelTris& t = cache.Get(p.model);
        for (size_t i = 0; i + 8 < t.p.size(); i += 9) {
            Face f;
            f.built = Built(p.model);
            Apply(p.M, &t.p[i], f.v); Apply(p.M, &t.p[i + 3], f.v + 3); Apply(p.M, &t.p[i + 6], f.v + 6);
            for (int c = 0; c < 3; ++c) {
                f.b[c * 2] = std::min({f.v[c], f.v[3 + c], f.v[6 + c]});
                f.b[c * 2 + 1] = std::max({f.v[c], f.v[3 + c], f.v[6 + c]});
            }
            if (f.b[1] < bx0 || f.b[0] > bx1 || f.b[5] < bz0 || f.b[4] > bz1) continue;
            float n[3];
            worldholes::TriNormal(f.v, f.v + 3, f.v + 6, n);
            if (n[1] > 0.3f) ups.push_back(f);
            else if (std::fabs(n[1]) <= 0.7071f) walls.push_back(f);
            all.push_back(f);
        }
    }
    // the patch's own floor strips cover what lies under them
    for (size_t i = 0; i + 8 < out->tri.size(); i += 9) {
        Face f;
        std::copy(&out->tri[i], &out->tri[i] + 9, f.v);
        for (int c = 0; c < 3; ++c) {
            f.b[c * 2] = std::min({f.v[c], f.v[3 + c], f.v[6 + c]});
            f.b[c * 2 + 1] = std::max({f.v[c], f.v[3 + c], f.v[6 + c]});
        }
        if (out->nrm[i / 3 + 1] > 0.3f) ups.push_back(f);
        all.push_back(f);
    }
    // the highest up-facing face at (x, z) between lo and hi (none: -1e30)
    auto ground = [&](float x, float z, float lo, float hi) {
        float best = -1e30f;
        const float o[3] = {x, hi, z}, d[3] = {0.0f, -1.0f, 0.0f};
        for (const Face& f : ups) {
            if (x < f.b[0] || x > f.b[1] || z < f.b[4] || z > f.b[5] || f.b[2] > hi || f.b[3] < lo) continue;
            float t;
            if (!detail::RayTri(o, d, f.v, f.v + 3, f.v + 6, &t)) continue;
            const float y = hi - t;
            if (y >= lo && y > best) best = y;
        }
        return best;
    };
    // an upright face within `len` along the level way d from o
    auto closed = [&](const float o[3], const float d[3], float len) {
        const float e[3] = {o[0] + d[0] * len, o[1], o[2] + d[2] * len};
        for (const Face& f : walls) {
            if (std::max(o[0], e[0]) < f.b[0] || std::min(o[0], e[0]) > f.b[1] || o[1] < f.b[2] || o[1] > f.b[3] || std::max(o[2], e[2]) < f.b[4] ||
                std::min(o[2], e[2]) > f.b[5])
                continue;
            float t;
            if (detail::RayTri(o, d, f.v, f.v + 3, f.v + 6, &t) && t <= len) return true;
        }
        return false;
    };
    // the nearest upright face along the level way d from o within len: its distance; true when it is the level's own build, stands along the
    // way's edge and looks back at it (its normal within 45 degrees of -d)
    auto wallHit = [&](const float o[3], const float d[3], float len, float* hitT) {
        const float e[3] = {o[0] + d[0] * len, o[1], o[2] + d[2] * len};
        float best = len;
        const Face* hit = nullptr;
        for (const Face& f : walls) {
            if (!f.built) continue;   // (a prop in the slot - a shield against the parapet - neither stops it nor is its wall)
            if (std::max(o[0], e[0]) < f.b[0] || std::min(o[0], e[0]) > f.b[1] || o[1] < f.b[2] || o[1] > f.b[3] || std::max(o[2], e[2]) < f.b[4] ||
                std::min(o[2], e[2]) > f.b[5])
                continue;
            float t;
            if (detail::RayTri(o, d, f.v, f.v + 3, f.v + 6, &t) && t <= best) { best = t; hit = &f; }
        }
        *hitT = best;
        if (!hit) return false;
        float n[3];
        worldholes::TriNormal(hit->v, hit->v + 3, hit->v + 6, n);
        return std::fabs(n[0] * d[0] + n[2] * d[2]) > 0.7071f;   // (either winding: the game's walls are seen from both sides)
    };
    // any face within `len` along d from o (enclosed: a hollow wall's inside)
    auto anyHit = [&](const float o[3], const float d[3], float len) {
        float lo[3], hi[3];
        for (int c = 0; c < 3; ++c) { lo[c] = std::min(o[c], o[c] + d[c] * len); hi[c] = std::max(o[c], o[c] + d[c] * len); }
        for (const Face& f : all) {
            if (hi[0] < f.b[0] || lo[0] > f.b[1] || hi[1] < f.b[2] || lo[1] > f.b[3] || hi[2] < f.b[4] || lo[2] > f.b[5]) continue;
            float t;
            if (detail::RayTri(o, d, f.v, f.v + 3, f.v + 6, &t) && t <= len) return true;
        }
        return false;
    };
    const float sink = 0.1f, past = 0.25f;
    for (int o : owners) {
        const Placement& p = pl[(size_t)o];
        const ModelTris& t = cache.Get(p.model);
        for (size_t i = 0; i + 5 < t.open.size(); i += 6) {
            if (out->used.count({o, i / 6})) continue;
            float a0[3], b0[3];
            Apply(p.M, &t.open[i], a0);
            Apply(p.M, &t.open[i + 3], b0);
            const float ex = b0[0] - a0[0], ez = b0[2] - a0[2], len = std::hypot(ex, ez);
            if (len < 1e-3f) continue;   // (an upright edge: no side under it)
            const int pieces = std::max(1, (int)std::ceil(len / kPiece));
            for (int piece = 0; piece < pieces; ++piece) {
                float a[3], b[3];
                const float u0 = (float)piece / pieces, u1 = (float)(piece + 1) / pieces;
                for (int c = 0; c < 3; ++c) { a[c] = a0[c] + (b0[c] - a0[c]) * u0; b[c] = a0[c] + (b0[c] - a0[c]) * u1; }
                const float mid[3] = {(a[0] + b[0]) * 0.5f, (a[1] + b[1]) * 0.5f, (a[2] + b[2]) * 0.5f};
                if (std::max(a[1], b[1]) > floorTop + 0.5f) continue;   // (above the deck: a wall's cap)
                // its open side: past it the ground lower, inside it the floor at its height
                float n[3] = {0, 0, 0};
                bool side = false;
                for (int sgn = -1; sgn <= 1 && !side; sgn += 2) {
                    const float nx = sgn * ez / len, nz = -sgn * ex / len;
                    const float in = ground(mid[0] - nx * past, mid[2] - nz * past, mid[1] - 0.15f, mid[1] + 0.15f);
                    const float outY = ground(mid[0] + nx * past, mid[2] + nz * past, mid[1] - maxDrop, mid[1] + 0.05f);
                    if (in > -1e29f && (outY < -1e29f || outY < mid[1] - minDrop * 0.5f)) { n[0] = nx; n[2] = nz; side = true; }   // (the ends decide below)
                }
                if (!side) continue;
                // each end: covered (a face just above the edge past it), or the ground under it
                float bottom[2];
                bool covered = false, none = false;
                const float* ends[2] = {a, b};
                for (int k = 0; k < 2; ++k) {
                    const float* e = ends[k];
                    const float x = e[0] + n[0] * past, z = e[2] + n[2] * past;
                    if (ground(x, z, e[1] + 0.05f, e[1] + 0.6f) > -1e29f) covered = true;
                    bottom[k] = ground(x, z, e[1] - maxDrop, e[1] + 0.05f);
                    if (bottom[k] < -1e29f) none = true;
                }
                if (covered) continue;
                const float plen = std::hypot(len / pieces, b[1] - a[1]);
                if (none) { out->sideLeft += plen; continue; }
                if (a[1] - bottom[0] < minDrop && b[1] - bottom[1] < minDrop) continue;
                // closed by a wall at its own height: a level ray out from inside it (the edge may run into the wall's face), just under its top
                const float top[3] = {mid[0] - n[0] * 0.4f, mid[1] - 0.05f, mid[2] - n[2] * 0.4f};
                const float half[3] = {top[0], (mid[1] + (bottom[0] + bottom[1]) * 0.5f) * 0.5f, top[2]};
                if (closed(top, n, 1.0f)) continue;
                // enclosed: just past it, halfway down, faces every way across it and above it (the inside of a hollow wall)
                {
                    const float q[3] = {mid[0] + n[0] * past, half[1], mid[2] + n[2] * past};
                    const float back[3] = {-n[0], 0.0f, -n[2]}, upward[3] = {0.0f, 1.0f, 0.0f};
                    if (anyHit(q, n, 4.0f) && anyHit(q, back, 4.0f) && anyHit(q, upward, 4.0f)) continue;
                }
                // the deck's strip going on: level rays from both ends, a hair above the edge, meet the parapet standing along it - a strip;
                // else a skirt. A piece at a parapet's end (one ray meets it, the other not) is cut in halves, twice: the strip up to where the
                // parapet ends, a skirt past it.
                auto lay = [&](auto& self, const float* pa, const float* pb, float botA, float botB, int depth) -> void {
                    const float sl = std::hypot(std::hypot(pb[0] - pa[0], pb[2] - pa[2]), pb[1] - pa[1]);
                    if (sideStrips) {   // (false: skirts alone, as 0.209.10 had them - the check's before)
                        float ta = 0.0f, tb = 0.0f;
                        const float oa[3] = {pa[0], pa[1] + 0.05f, pa[2]}, ob[3] = {pb[0], pb[1] + 0.05f, pb[2]};
                        const bool ha = wallHit(oa, n, 4.0f, &ta), hb = wallHit(ob, n, 4.0f, &tb);
                        if (ha && hb && std::min(ta, tb) > 0.5f && std::fabs(ta - tb) < 2.0f && std::max(pa[1] - botA, pb[1] - botB) >= 0.5f) {
                            const float qa[3] = {pa[0] + n[0] * ta, pa[1], pa[2] + n[2] * ta}, qb[3] = {pb[0] + n[0] * tb, pb[1], pb[2] + n[2] * tb};
                            const float up[3] = {0.0f, 1.0f, 0.0f};
                            detail::AddQuad(out, pa, pb, qb, qa, up);
                            ++out->sideStrips;
                            out->sideLength += sl;
                            return;
                        }
                        if (ha != hb && depth < 2) {
                            const float pm[3] = {(pa[0] + pb[0]) * 0.5f, (pa[1] + pb[1]) * 0.5f, (pa[2] + pb[2]) * 0.5f};
                            float bm = ground(pm[0] + n[0] * past, pm[2] + n[2] * past, pm[1] - maxDrop, pm[1] + 0.05f);
                            if (bm < -1e29f) bm = std::min(botA, botB);
                            self(self, pa, pm, botA, bm, depth + 1);
                            self(self, pm, pb, bm, botB, depth + 1);
                            return;
                        }
                    }
                    if (pa[1] - botA < minDrop && pb[1] - botB < minDrop) return;   // (a half that stands on the ground)
                    const float da[3] = {pa[0], botA - sink, pa[2]}, db[3] = {pb[0], botB - sink, pb[2]};
                    detail::AddQuad(out, pa, pb, db, da, n);
                    ++out->sides;
                    out->sideLength += sl;
                };
                lay(lay, a, b, bottom[0], bottom[1], 0);
            }
        }
    }
}

// The check from the hero's eyes (Serg on 0.209.10: "в мосте дырка не заделана" - the slit between the ramp and the parapet's foot, which the
// height map's hole check never counted: beside a ramp the drop is under the hole's 2.5). For every piece (at most a unit) of every open edge of
// the rim's floors (the deck and its ramps, not above `floorTop` + half a unit) with lower ground past it: standing 1.5 units in from it, the
// eyes kEye over the floor there, looking just past the edge at the floor's own height (0.3, 1 and 2 units out; at the piece's middle and 0.4 of
// its length to each side). A look sees THROUGH when it meets nothing, or a face more than a unit below the edge, inside the slot between the edge
// and a wall standing beside it (a level ray from the edge meets an upright face within 4 units, farther out than where the look came down): an
// opening between a floor and its wall, as on the bridge. Ground beside a raised ramp with no wall beyond is the open land, not counted. The
// patch counts as the game's own faces.
struct Seen {
    int pieces = 0, open = 0, looks = 0, through = 0;   // pieces looked at, pieces with an opening; looks, looks through
    float length = 0.0f, openLength = 0.0f;             // how long those pieces are
    std::vector<std::array<float, 5>> where;            // each open piece: its middle x y z, the wall's distance past it, its looks through
};
constexpr float kEye = 5.5f;   // a hero's eyes over the floor, in units (about 1.6 m)
inline Seen SeenOpenings(const std::vector<worldscene::Placement>& pl, worldscene::ModelCache& cache, const Patch& patch, float floorTop) {
    using namespace worldscene;
    Seen s;
    std::set<int> owners;
    for (const auto& kv : patch.rim) if (kv.first.first >= 0 && (size_t)kv.first.first < pl.size()) owners.insert(kv.first.first);
    if (owners.empty()) return s;
    float bx0 = 1e30f, bx1 = -1e30f, bz0 = 1e30f, bz1 = -1e30f;
    for (int o : owners) {
        const ModelTris& t = cache.Get(pl[(size_t)o].model);
        for (size_t i = 0; i + 2 < t.p.size(); i += 3) {
            float w[3];
            Apply(pl[(size_t)o].M, &t.p[i], w);
            bx0 = std::min(bx0, w[0]); bx1 = std::max(bx1, w[0]); bz0 = std::min(bz0, w[2]); bz1 = std::max(bz1, w[2]);
        }
    }
    if (bx0 > bx1) return s;
    bx0 -= 6.0f; bx1 += 6.0f; bz0 -= 6.0f; bz1 += 6.0f;
    struct Face { float v[9]; float b[6]; bool up, upright; };
    std::vector<Face> all;
    auto add = [&](const float* a, const float* b, const float* c) {
        Face f;
        std::copy(a, a + 3, f.v); std::copy(b, b + 3, f.v + 3); std::copy(c, c + 3, f.v + 6);
        for (int k = 0; k < 3; ++k) {
            f.b[k * 2] = std::min({f.v[k], f.v[3 + k], f.v[6 + k]});
            f.b[k * 2 + 1] = std::max({f.v[k], f.v[3 + k], f.v[6 + k]});
        }
        if (f.b[1] < bx0 || f.b[0] > bx1 || f.b[5] < bz0 || f.b[4] > bz1) return;
        float n[3];
        worldholes::TriNormal(f.v, f.v + 3, f.v + 6, n);
        f.up = n[1] > 0.3f;
        f.upright = std::fabs(n[1]) <= 0.7071f;
        all.push_back(f);
    };
    for (const Placement& p : pl) {
        if (Decor(p.model)) continue;
        const ModelTris& t = cache.Get(p.model);
        for (size_t i = 0; i + 8 < t.p.size(); i += 9) {
            float a[3], b[3], c[3];
            Apply(p.M, &t.p[i], a); Apply(p.M, &t.p[i + 3], b); Apply(p.M, &t.p[i + 6], c);
            add(a, b, c);
        }
    }
    for (size_t i = 0; i + 8 < patch.tri.size(); i += 9) add(&patch.tri[i], &patch.tri[i + 3], &patch.tri[i + 6]);
    // the first face along d from o within len (any face; upright only if asked): its distance (none: false)
    auto first = [&](const float o[3], const float d[3], float len, bool uprightOnly, float* hitT) {
        float lo[3], hi[3];
        for (int c = 0; c < 3; ++c) { lo[c] = std::min(o[c], o[c] + d[c] * len); hi[c] = std::max(o[c], o[c] + d[c] * len); }
        float best = len;
        bool any = false;
        for (const Face& f : all) {
            if (uprightOnly && !f.upright) continue;
            if (hi[0] < f.b[0] || lo[0] > f.b[1] || hi[1] < f.b[2] || lo[1] > f.b[3] || hi[2] < f.b[4] || lo[2] > f.b[5]) continue;
            float t;
            if (detail::RayTri(o, d, f.v, f.v + 3, f.v + 6, &t) && t <= best) { best = t; any = true; }
        }
        *hitT = best;
        return any;
    };
    // the highest up-facing face at x z between lo and hi (none: -1e30)
    auto ground = [&](float x, float z, float lo, float hi) {
        float best = -1e30f;
        const float o[3] = {x, hi, z}, d[3] = {0.0f, -1.0f, 0.0f};
        for (const Face& f : all) {
            if (!f.up || x < f.b[0] || x > f.b[1] || z < f.b[4] || z > f.b[5] || f.b[2] > hi || f.b[3] < lo) continue;
            float t;
            if (detail::RayTri(o, d, f.v, f.v + 3, f.v + 6, &t) && hi - t >= lo && hi - t > best) best = hi - t;
        }
        return best;
    };
    for (int o : owners) {
        const Placement& p = pl[(size_t)o];
        const ModelTris& t = cache.Get(p.model);
        for (size_t i = 0; i + 5 < t.open.size(); i += 6) {
            float a0[3], b0[3];
            Apply(p.M, &t.open[i], a0);
            Apply(p.M, &t.open[i + 3], b0);
            const float ex = b0[0] - a0[0], ez = b0[2] - a0[2], len = std::hypot(ex, ez);
            if (len < 1e-3f) continue;
            const int pieces = std::max(1, (int)std::ceil(len / kPiece));
            for (int piece = 0; piece < pieces; ++piece) {
                float a[3], b[3];
                const float u0 = (float)piece / pieces, u1 = (float)(piece + 1) / pieces;
                for (int c = 0; c < 3; ++c) { a[c] = a0[c] + (b0[c] - a0[c]) * u0; b[c] = a0[c] + (b0[c] - a0[c]) * u1; }
                const float mid[3] = {(a[0] + b[0]) * 0.5f, (a[1] + b[1]) * 0.5f, (a[2] + b[2]) * 0.5f};
                if (std::max(a[1], b[1]) > floorTop + 0.5f) continue;
                // its open side, as Sides finds it: the floor inside at its height, the ground past it lower
                float n[3] = {0, 0, 0};
                bool side = false;
                for (int sgn = -1; sgn <= 1 && !side; sgn += 2) {
                    const float nx = sgn * ez / len, nz = -sgn * ex / len;
                    const float in = ground(mid[0] - nx * 0.25f, mid[2] - nz * 0.25f, mid[1] - 0.15f, mid[1] + 0.15f);
                    const float outY = ground(mid[0] + nx * 0.25f, mid[2] + nz * 0.25f, mid[1] - 10.0f, mid[1] + 0.05f);
                    if (in > -1e29f && (outY < -1e29f || outY < mid[1] - 0.15f)) { n[0] = nx; n[2] = nz; side = true; }
                }
                if (!side) continue;
                // the wall beside it, if any (a level ray from the edge, a hair over it)
                float wallT = 0.0f;
                const float wo[3] = {mid[0], mid[1] + 0.05f, mid[2]};
                const bool wall = first(wo, n, 4.0f, true, &wallT);
                // the eyes 1.5 units in, over the floor there
                const float fx = mid[0] - n[0] * 1.5f, fz = mid[2] - n[2] * 1.5f;
                const float floorY = ground(fx, fz, mid[1] - 2.0f, mid[1] + 2.0f);
                const float eye[3] = {fx, (floorY > -1e29f ? floorY : mid[1]) + kEye, fz};
                const float e[3] = {ex / len, 0.0f, ez / len}, plen = len / pieces;
                ++s.pieces;
                s.length += std::hypot(plen, b[1] - a[1]);
                bool open = false;
                int thr = 0;
                for (float u : {0.3f, 1.0f, 2.0f})
                    for (float v : {-0.4f, 0.0f, 0.4f}) {
                        const float tp[3] = {mid[0] + n[0] * u + e[0] * v * plen, mid[1], mid[2] + n[2] * u + e[2] * v * plen};
                        float d[3] = {tp[0] - eye[0], tp[1] - eye[1], tp[2] - eye[2]};
                        const float dl = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                        for (float& c : d) c /= dl;
                        ++s.looks;
                        float ht = 0.0f;
                        const bool hit = first(eye, d, dl * 3.0f, false, &ht);
                        const float hp[3] = {eye[0] + d[0] * ht, eye[1] + d[1] * ht, eye[2] + d[2] * ht};
                        const float out = (hp[0] - mid[0]) * n[0] + (hp[2] - mid[2]) * n[2];   // how far past the edge it came down
                        if (wall && (!hit || (hp[1] < mid[1] - 1.0f && out < wallT))) { ++s.through; ++thr; open = true; }
                    }
                if (open) { ++s.open; s.openLength += std::hypot(plen, b[1] - a[1]); s.where.push_back({mid[0], mid[1], mid[2], wallT, (float)thr}); }
            }
        }
    }
    return s;
}

// The floor whose material the patch takes - one of the rim's own meshes: the TerrainMaterial one with the longest edges on the rim (the game's ground,
// drawn by its world place and the area's own biome: no texture coordinates to carry on), else the one with the longest edges. False: no rim.
struct Source { int placement = -1; uint32_t mesh = 0; std::string model, meshName, technique; float length = 0.0f; };
inline bool SourceOf(const std::vector<worldscene::Placement>& pl, worldscene::ModelCache& cache, const Patch& p, Source* out) {
    *out = Source{};
    float bestTerrain = 0.0f, bestAny = 0.0f;
    Source terrain, any;
    for (const auto& [key, len] : p.rim) {
        if (key.first < 0 || (size_t)key.first >= pl.size()) continue;
        const worldscene::ModelTris& t = cache.Get(pl[(size_t)key.first].model);
        if (key.second >= t.meshName.size()) continue;
        Source s{key.first, key.second, pl[(size_t)key.first].model, t.meshName[key.second], t.meshTech[key.second], len};
        if (s.technique == "TerrainMaterial" && len > bestTerrain) { bestTerrain = len; terrain = s; }
        if (len > bestAny) { bestAny = len; any = s; }
    }
    *out = bestTerrain > 0.0f ? terrain : any;
    return out->placement >= 0;
}

// The patch as the game's vertex type (shared/granny_remesh.h Vertex: int16 x4 position over VertexScale, int8 normal and tangent, half uv), in the
// model space of the piece it rides on: `toModel` takes a preset place there (the inverse of that piece's placement). Each triangle its own three
// vertices, its own normal; the tangent along x; the uv the place's x z (a TerrainMaterial reads only the position). False: too many vertices.
inline bool ToMesh(const Patch& p, const worldscene::Mat& toModel, grannyremesh::Mesh* out) {
    *out = grannyremesh::Mesh{};
    const size_t nv = p.tri.size() / 3;
    if (!nv || nv > 65535) return false;
    std::vector<float> pos(nv * 3);
    double most = 1e-6;
    for (int k = 0; k < 3; ++k) { out->mn[k] = 1e30f; out->mx[k] = -1e30f; }
    for (size_t v = 0; v < nv; ++v) {
        worldscene::Apply(toModel, &p.tri[v * 3], &pos[v * 3]);
        for (int k = 0; k < 3; ++k) {
            const float c = pos[v * 3 + k];
            most = std::max(most, (double)std::fabs(c));
            out->mn[k] = std::min(out->mn[k], c);
            out->mx[k] = std::max(out->mx[k], c);
        }
    }
    out->scale = (float)most;
    const int16_t w = (int16_t)std::lround(std::log2(out->scale) / 13.0 * 32767.0);   // (as GridMesh writes it)
    // the normals turned by the same matrix (no scale in a preset's placement of a level piece; a mirror turns them round)
    const double* m = toModel.m;
    out->v.resize(nv);
    for (size_t v = 0; v < nv; ++v) {
        grannyremesh::Vertex& x = out->v[v];
        for (int k = 0; k < 3; ++k) x.pos[k] = (int16_t)std::lround(pos[v * 3 + k] / out->scale * 32767.0);
        x.pos[3] = w;
        const float* n0 = &p.nrm[(v / 3) * 3];
        float n[3];
        for (int i = 0; i < 3; ++i) n[i] = (float)(m[i * 4] * n0[0] + m[i * 4 + 1] * n0[1] + m[i * 4 + 2] * n0[2]);
        const float l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        for (int i = 0; i < 3; ++i) x.normal[i] = grannyremesh::Snorm8(l > 1e-6f ? n[i] / l : (i == 1 ? 1.0f : 0.0f));
        x.normal[3] = 0;
        // a tangent across the normal along x (or z when the normal is x)
        float t[3] = {1, 0, 0};
        if (std::fabs(n[0]) > 0.9f * std::max(l, 1e-6f)) { t[0] = 0; t[2] = 1; }
        const float d = (t[0] * n[0] + t[1] * n[1] + t[2] * n[2]) / std::max(l * l, 1e-12f);
        for (int i = 0; i < 3; ++i) t[i] -= n[i] * d;
        const float lt = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
        for (int i = 0; i < 3; ++i) x.tangent[i] = grannyremesh::Snorm8(lt > 1e-6f ? t[i] / lt : 0.0f);
        x.tangent[3] = 127;
        x.uv[0] = grannyremesh::Half(p.tri[v * 3] / 40.0f);
        x.uv[1] = grannyremesh::Half(p.tri[v * 3 + 2] / 40.0f);
    }
    out->idx.resize(nv);
    for (size_t i = 0; i < nv; ++i) out->idx[i] = (uint16_t)i;
    return true;
}

// The inverse of a level piece's placement (a rotation and a uniform or axis scale, then a move): its preset places into its model space
inline worldscene::Mat Inverse(const worldscene::Mat& M) {
    const double* m = M.m;
    double a[9] = {m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10]};
    const double det = a[0] * (a[4] * a[8] - a[5] * a[7]) - a[1] * (a[3] * a[8] - a[5] * a[6]) + a[2] * (a[3] * a[7] - a[4] * a[6]);
    worldscene::Mat r;
    if (std::fabs(det) < 1e-12) return r;
    const double inv[9] = {(a[4] * a[8] - a[5] * a[7]) / det, (a[2] * a[7] - a[1] * a[8]) / det, (a[1] * a[5] - a[2] * a[4]) / det,
                           (a[5] * a[6] - a[3] * a[8]) / det, (a[0] * a[8] - a[2] * a[6]) / det, (a[2] * a[3] - a[0] * a[5]) / det,
                           (a[3] * a[7] - a[4] * a[6]) / det, (a[1] * a[6] - a[0] * a[7]) / det, (a[0] * a[4] - a[1] * a[3]) / det};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) r.m[i * 4 + j] = inv[i * 3 + j];
        r.m[i * 4 + 3] = -(inv[i * 3] * m[3] + inv[i * 3 + 1] * m[7] + inv[i * 3 + 2] * m[11]);
    }
    return r;
}

}  // namespace worldpatch
