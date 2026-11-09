// world_holes_test - [holes] patch_world (shared/world_holes.h, world_holes_scene.h, world_patch.h, world_holes_file.h, world_holes_list.h).
//   world_holes_test                       the rules on scenes of our own: a bridge-like deck short of its parapet (a floor strip), a courtyard
//                                          ditch whose floor ends in the air over a lower sheet (skirts), a ditch with its own walls (no hole),
//                                          a wagon's bed over the ground (no floor one walks on: no hole), the bridge's ramps open at their sides
//                                          (side skirts; none at a wall, a foot on the ground, a cap above the deck); the patch's mesh, the measures file,
//                                          the unit, the places' list
//   world_holes_test <root>                also every place made from the game's own files taken out under <root> (data\hd\...): its hole found,
//                                          its patch, five models that read back as one mesh of the rim's own material, what is left of the hole
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "game_ceiling.h"
#include "world_holes_list.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

namespace {

using worldscene::ModelTris;
using worldscene::Placement;

// a quad a b c d (the game's winding: its front where (b - a) x (c - a) points)
void Quad(ModelTris* t, const float a[3], const float b[3], const float c[3], const float d[3], uint32_t mesh = 0) {
    const float* q[2][3] = {{a, b, c}, {a, c, d}};
    for (auto& tri : q) {
        for (auto* p : tri) t->p.insert(t->p.end(), p, p + 3);
        t->mesh.push_back(mesh);
    }
}
void Edge(ModelTris* t, const float a[3], const float b[3], uint32_t mesh = 0) {
    t->open.insert(t->open.end(), a, a + 3);
    t->open.insert(t->open.end(), b, b + 3);
    t->openMesh.push_back(mesh);
}
// a level floor x0..x1, z0..z1 at y, facing up, its four sides open
ModelTris Floor(float x0, float x1, float z0, float z1, float y, const char* tech = "TerrainMaterial") {
    ModelTris t;
    const float a[3] = {x0, y, z0}, b[3] = {x0, y, z1}, c[3] = {x1, y, z1}, d[3] = {x1, y, z0};   // (a b c: (b-a)x(c-a) is +y)
    Quad(&t, a, b, c, d);
    Edge(&t, a, b); Edge(&t, b, c); Edge(&t, c, d); Edge(&t, d, a);
    t.meshName = {"floor"};
    t.meshTech = {tech};
    t.ok = true;
    return t;
}
// an upright wall at z, x0..x1, y0..y1, looking toward -z (lookNeg) or +z
ModelTris WallZ(float z, float x0, float x1, float y0, float y1, bool lookNeg) {
    ModelTris t;
    const float a[3] = {x0, y0, z}, b[3] = {x0, y1, z}, c[3] = {x1, y1, z}, d[3] = {x1, y0, z};   // (b-a)x(c-a) = (0,1,0)x(1,0,0) = -z
    if (lookNeg) Quad(&t, a, b, c, d); else Quad(&t, a, d, c, b);
    t.meshName = {"wall"};
    t.meshTech = {"StandardMaterial"};
    t.ok = true;
    return t;
}

Placement Put(worldscene::ModelCache& cache, const std::string& name, ModelTris t, bool walk) {
    cache.Put(name, std::move(t));
    Placement p{name, name, worldscene::Mat{}, 0, walk};
    p.lightMask = 19; p.shadowMask = 3; p.floorModel = true;
    return p;
}

worldscene::Reader Nothing() { return [](const std::string&, std::string*) { return false; }; }

void Bridge() {
    // a deck at y 4 from z 0 to 18, its parapet's inner face at z 21 (only from y 3 up: under it nothing), the river at -4 under it all,
    // the other parapet closing the deck's far side at z 0
    worldscene::ModelCache cache(Nothing());
    std::vector<Placement> pl;
    pl.push_back(Put(cache, "deck", Floor(0, 40, 0, 18, 4.0f), true));
    pl.push_back(Put(cache, "parapet", WallZ(21.0f, -2, 42, 3.0f, 7.0f, true), false));
    pl.push_back(Put(cache, "parapet_top", Floor(-2, 42, 21, 22, 7.0f, "StandardMaterial"), false));
    pl.push_back(Put(cache, "near_parapet", WallZ(0.0f, -2, 42, 3.0f, 7.0f, false), false));
    pl.push_back(Put(cache, "data/hd/env/preset/test/terrain.model", Floor(-10, 50, -10, 30, -4.0f), true));
    const worldscene::HeightMap hm = worldscene::Raster(pl, cache, -12, 52, -12, 32, 0.25f);
    std::vector<int32_t> labels;
    std::vector<float> rims;
    const std::vector<worldscene::Gap> gaps = worldscene::Gaps(hm, 4.0f, 2.5f, 1.0f, &labels, &rims);
    int hole = -1;
    for (size_t k = 0; k < gaps.size(); ++k) if (gaps[k].Hole()) { CHECK(hole < 0); hole = (int)k; }
    CHECK(hole >= 0);
    if (hole < 0) return;
    const worldscene::Gap& g = gaps[(size_t)hole];
    std::printf("bridge: hole %.1f sq units, x %.2f..%.2f z %.2f..%.2f, floor %.2f, open rim %.2f, walled %.2f\n", g.area, g.mn[0], g.mx[0], g.mn[1], g.mx[1], g.floorY, g.openRim,
                g.walled);
    CHECK(std::fabs(g.floorY - 4.0f) < 0.01f);
    CHECK(g.mn[1] > 17.9f && g.mx[1] < 21.1f);   // the strip between the deck and the parapet
    CHECK(g.area > 100.0f && g.area < 125.0f);   // 40 x 3
    CHECK(g.openRim > 0.9f && g.walled < 0.1f);
    const worldpatch::Patch p = worldpatch::Make(pl, cache, hm, labels, rims, hole, g);
    std::printf("bridge: patch %d edges, %d strips, %d skirts, %.1f sq units, y %.2f..%.2f z %.2f..%.2f\n", p.edges, p.strips, p.skirts, p.area, p.mn[1], p.mx[1], p.mn[2], p.mx[2]);
    CHECK(p.edges == 1 && p.strips == 40 && p.skirts == 0);   // the deck's one edge on the rim, in pieces of a unit, carried on to the parapet's face
    CHECK(std::fabs(p.area - 120.0f) < 0.5f);
    CHECK(std::fabs(p.mn[1] - 4.0f) < 1e-3f && std::fabs(p.mx[1] - 4.0f) < 1e-3f);   // at the deck's own height
    CHECK(std::fabs(p.mn[2] - 18.0f) < 1e-3f && std::fabs(p.mx[2] - 21.0f) < 1e-3f);  // from the edge to the face
    for (size_t t = 0; t < p.nrm.size() / 3; ++t) CHECK(p.nrm[t * 3 + 1] > 0.99f);  // facing up, front side
    worldpatch::Source src;
    CHECK(worldpatch::SourceOf(pl, cache, p, &src) && src.model == "deck" && src.technique == "TerrainMaterial");
    // the check: laid in, no hole left
    ModelTris mt;
    mt.p = p.tri;
    mt.mesh.assign(p.tri.size() / 9, 0);
    mt.ok = true;
    cache.Put("__patch__", std::move(mt));
    std::vector<Placement> with = pl;
    with.push_back(Placement{"patch", "__patch__", worldscene::Mat{}, 0, true});
    float left = 0.0f;
    for (const auto& a : worldscene::Gaps(worldscene::Raster(with, cache, -12, 52, -12, 32, 0.25f))) if (a.Hole()) left += a.area;
    std::printf("bridge: left with the patch: %.2f sq units\n", left);
    CHECK(left < 1.0f);
    // the patch as the game's vertex type, in an anchor's space at 10 0 5: decoded back, the same places less the anchor
    worldscene::Mat toAnchor;
    toAnchor.m[3] = -10; toAnchor.m[11] = -5;
    grannyremesh::Mesh mesh;
    CHECK(worldpatch::ToMesh(p, toAnchor, &mesh));
    CHECK(mesh.v.size() == p.tri.size() / 3 && mesh.idx.size() == mesh.v.size());
    for (size_t v = 0; v < mesh.v.size(); ++v) {
        for (int c = 0; c < 3; ++c) {
            const float back = mesh.v[v].pos[c] / 32767.0f * mesh.scale, want = p.tri[v * 3 + c] - (c == 0 ? 10.0f : c == 2 ? 5.0f : 0.0f);
            CHECK(std::fabs(back - want) < mesh.scale / 16000.0f);
        }
        CHECK(mesh.v[v].normal[1] == 127);
    }
    // the inverse of a placement
    worldscene::Mat M;
    M.m[0] = 0; M.m[2] = 1; M.m[8] = -1; M.m[10] = 0; M.m[3] = 7; M.m[7] = 2; M.m[11] = -3;   // a quarter turn round y, moved
    const worldscene::Mat I = worldpatch::Inverse(M);
    const float q[3] = {1.5f, 2.5f, -4.0f};
    float w[3], back[3];
    worldscene::Apply(M, q, w);
    worldscene::Apply(I, w, back);
    for (int c = 0; c < 3; ++c) CHECK(std::fabs(back[c] - q[c]) < 1e-4f);
}

void Ditch(bool walled) {
    // the courtyard: its floor at 0 on both sides of a ditch from z 10 to 13, the floor ending in the air at the ditch; a lower sheet at -3 under
    // all of it. walled: the ditch has its own side walls (each looking into the ditch) - a gutter, not a hole
    worldscene::ModelCache cache(Nothing());
    std::vector<Placement> pl;
    ModelTris upper = Floor(0, 30, 0, 10, 0.0f);
    {
        const ModelTris other = Floor(0, 30, 13, 23, 0.0f);
        upper.p.insert(upper.p.end(), other.p.begin(), other.p.end());
        upper.mesh.insert(upper.mesh.end(), other.mesh.begin(), other.mesh.end());
        upper.open.insert(upper.open.end(), other.open.begin(), other.open.end());
        upper.openMesh.insert(upper.openMesh.end(), other.openMesh.begin(), other.openMesh.end());
    }
    pl.push_back(Put(cache, "data/hd/env/preset/test/terrain.model", upper, true));
    pl.push_back(Put(cache, "lower", Floor(-5, 35, -5, 28, -3.0f), true));
    if (walled) {
        pl.push_back(Put(cache, "wall_a", WallZ(10.0f, 0, 30, -3.0f, 0.0f, false), false));   // at z 10 looking +z (into the ditch)
        pl.push_back(Put(cache, "wall_b", WallZ(13.0f, 0, 30, -3.0f, 0.0f, true), false));    // at z 13 looking -z
    }
    const worldscene::HeightMap hm = worldscene::Raster(pl, cache, -7, 37, -7, 30, 0.25f);
    std::vector<int32_t> labels;
    std::vector<float> rims;
    const std::vector<worldscene::Gap> gaps = worldscene::Gaps(hm, 4.0f, 2.5f, 1.0f, &labels, &rims);
    int holes = 0, hole = -1;
    for (size_t k = 0; k < gaps.size(); ++k) if (gaps[k].Hole()) { ++holes; hole = (int)k; }
    std::printf("ditch (%s): %zu gaps, %d holes%s\n", walled ? "walled" : "open", gaps.size(), holes, gaps.empty() ? "" : "");
    CHECK(!gaps.empty());
    if (walled) { CHECK(holes == 0); for (const auto& g : gaps) CHECK(g.walled > 0.5f); return; }
    CHECK(holes == 1);
    if (hole < 0) return;
    const worldpatch::Patch p = worldpatch::Make(pl, cache, hm, labels, rims, hole, gaps[(size_t)hole]);
    std::printf("ditch: patch %d edges, %d strips, %d skirts, %.1f sq units, y %.2f..%.2f\n", p.edges, p.strips, p.skirts, p.area, p.mn[1], p.mx[1]);
    CHECK(p.edges == 2 && p.skirts == 60 && p.strips == 0);   // both rims (in pieces of a unit) hang down to the lower sheet
    CHECK(std::fabs(p.mn[1] + 3.0f) < 1e-3f && std::fabs(p.mx[1]) < 1e-3f);
    CHECK(std::fabs(p.area - 2 * 30 * 3) < 0.5f);
    for (size_t t = 0; t < p.nrm.size() / 3; ++t) CHECK(std::fabs(p.nrm[t * 3 + 1]) < 1e-3f);   // upright
}

void Wagon() {
    // a wagon's bed at 4 over the ground with its side boards: not a floor one walks on - no hole under it
    worldscene::ModelCache cache(Nothing());
    std::vector<Placement> pl;
    pl.push_back(Put(cache, "wagon", Floor(0, 10, 0, 5, 4.0f, "StandardMaterial"), false));
    pl.push_back(Put(cache, "side", WallZ(7.0f, 0, 10, 3.0f, 6.0f, true), false));
    pl.push_back(Put(cache, "data/hd/env/preset/test/terrain.model", Floor(-10, 20, -10, 20, 0.0f), true));
    const worldscene::HeightMap hm = worldscene::Raster(pl, cache, -12, 22, -12, 22, 0.25f);
    CHECK(worldscene::Gaps(hm).empty());
}

void Ramps(bool parapet) {
    // the bridge with its ramps (Serg on 0.209.8: "мост сверху закрыт а спереди не закрыт"): one floor piece - the deck at y 4 from x 0 to 40,
    // z 0 to 18, a ramp down to the ground at each end (x -12..0 and 40..52, 4 -> 0) - the north parapet a wall down to the ground at z 0, the
    // river at -4 under the deck, the banks at 0; a pillar's cap at y 7.5 in the same piece. The deck's south edge: the floor strip (Make).
    //   parapet false: the south wall at z 21 only from y 3 up and no piece of the level's build (a name of no act folder) - the ramps' south
    //     edges, open over the bank: SIDE skirts, a unit each, down into the bank; and from the eyes the slot to that wall still seen.
    //   parapet true (Serg on 0.209.10: "в мосте дырка не заделана"): the south parapet the level's own, down to the ground - the ramps' south
    //     edges carried on level to it: SIDE strips (but the foot's first unit, under half a unit high: a skirt); from the eyes nothing seen.
    // Their north edges (the wall), their feet (on the ground) and the cap (above the deck): nothing, either way
    worldscene::ModelCache cache(Nothing());
    std::vector<Placement> pl;
    ModelTris t;
    const float w0[3] = {-12, 0, 0}, w1[3] = {-12, 0, 18}, d0[3] = {0, 4, 0}, d1[3] = {0, 4, 18}, d2[3] = {40, 4, 18}, d3[3] = {40, 4, 0}, e0[3] = {52, 0, 0},
                e1[3] = {52, 0, 18};
    Quad(&t, w0, w1, d1, d0);   // the west ramp, the deck, the east ramp (each facing up: (b-a)x(c-a) along +y)
    Quad(&t, d0, d1, d2, d3);
    Quad(&t, d3, d2, e1, e0);
    Edge(&t, w0, w1); Edge(&t, w1, d1); Edge(&t, d1, d2); Edge(&t, d2, e1); Edge(&t, e1, e0); Edge(&t, e0, d3); Edge(&t, d3, d0); Edge(&t, d0, w0);
    const float c0[3] = {10, 7.5f, 21}, c1[3] = {10, 7.5f, 22}, c2[3] = {11, 7.5f, 22}, c3[3] = {11, 7.5f, 21};
    Quad(&t, c0, c1, c2, c3);
    Edge(&t, c0, c1); Edge(&t, c1, c2); Edge(&t, c2, c3); Edge(&t, c3, c0);
    t.meshName = {"deck"};
    t.meshTech = {"TerrainMaterial"};
    t.ok = true;
    pl.push_back(Put(cache, "deck", std::move(t), true));
    if (parapet) pl.push_back(Put(cache, "data/hd/env/model/act1/test/parapet.model", WallZ(21.0f, -14, 54, -4.0f, 7.0f, true), false));
    else pl.push_back(Put(cache, "parapet", WallZ(21.0f, -14, 54, 3.0f, 7.0f, true), false));
    pl.push_back(Put(cache, "parapet_top", Floor(-14, 54, 21, 22, 7.0f, "StandardMaterial"), false));
    pl.push_back(Put(cache, "north_wall", WallZ(0.0f, -14, 54, -4.0f, 7.0f, false), false));
    pl.push_back(Put(cache, "data/hd/env/preset/test/terrain.model", Floor(0, 40, -10, 30, -4.0f), true));
    pl.push_back(Put(cache, "data/hd/env/preset/test/terrain2.model", Floor(-30, 0, -10, 30, 0.0f), true));
    pl.push_back(Put(cache, "data/hd/env/preset/test/terrain3.model", Floor(40, 70, -10, 30, 0.0f), true));
    const worldscene::HeightMap hm = worldscene::Raster(pl, cache, -32, 72, -12, 32, 0.25f);
    std::vector<int32_t> labels;
    std::vector<float> rims;
    const std::vector<worldscene::Gap> gaps = worldscene::Gaps(hm, 4.0f, 2.5f, 1.0f, &labels, &rims);
    int hole = -1;
    for (size_t k = 0; k < gaps.size() && hole < 0; ++k)   // the hole at the strip's middle (as the maker takes a place's)
        if (gaps[k].Hole() && gaps[k].mn[0] <= 20.0f && gaps[k].mx[0] >= 20.0f && gaps[k].mn[1] <= 19.5f && gaps[k].mx[1] >= 19.5f) hole = (int)k;
    for (size_t k = 0; k < gaps.size(); ++k)
        std::printf("ramps: gap %zu: %.1f sq units x %.2f..%.2f z %.2f..%.2f floor %.2f open rim %.2f walled %.2f%s\n", k, gaps[k].area, gaps[k].mn[0], gaps[k].mx[0],
                    gaps[k].mn[1], gaps[k].mx[1], gaps[k].floorY, gaps[k].openRim, gaps[k].walled, gaps[k].Hole() ? " (a hole)" : "");
    CHECK(hole >= 0);
    if (hole < 0) return;
    worldscene::Gap g;
    const std::vector<uint8_t> regions = worldpatch::Cluster(gaps, hole, &g);
    worldpatch::Patch p = worldpatch::Make(pl, cache, hm, labels, rims, regions, g);
    const size_t holeTris = p.tri.size() / 9;
    const worldpatch::Seen before = worldpatch::SeenOpenings(pl, cache, p, g.floorY);
    worldpatch::Sides(pl, cache, &p, g.floorY);
    const worldpatch::Seen after = worldpatch::SeenOpenings(pl, cache, p, g.floorY);
    std::printf("ramps (%s): hole %.1f sq units, floor %.2f; %d strips, %d skirts; %d side strips, %d side skirts (%.1f units of edge), %.1f left open; "
                "from the eyes %.1f -> %.1f units of edge with an opening seen\n", parapet ? "the level's parapet" : "a wall from y 3", g.area, g.floorY, p.strips,
                p.skirts, p.sideStrips, p.sides, p.sideLength, p.sideLeft, before.openLength, after.openLength);
    CHECK(p.strips == 40 && p.skirts == 0);
    CHECK(p.sideLeft < 1e-3f);
    if (parapet) {
        CHECK(p.sideStrips == 22 && p.sides == 2);
        CHECK(before.open > 0 && after.open == 0);   // the slot beside the ramps seen before, shut after
    } else {
        CHECK(p.sideStrips == 0 && p.sides == 24);
        CHECK(after.open > 0);                       // skirts alone leave the slot to the wall open from above (0.209.10)
    }
    for (size_t k = holeTris; k < p.tri.size() / 9; ++k) {
        const float* v = &p.tri[k * 9];
        const bool strip = p.nrm[k * 3 + 1] > 0.5f;   // (up along the ramp's slope: 0.95 at 1 in 3)
        CHECK(strip || p.nrm[k * 3 + 2] > 0.99f);   // a strip facing up, or a skirt facing out of the ramp over the bank (+z)
        float lo = 1e30f, hi = -1e30f;
        for (int c = 0; c < 3; ++c) {
            if (strip) CHECK(std::fabs(v[c * 3 + 2] - 18.0f) < 1e-3f || std::fabs(v[c * 3 + 2] - 21.0f) < 1e-3f);   // from the edge to the parapet's face
            else CHECK(std::fabs(v[c * 3 + 2] - 18.0f) < 1e-3f);                                                   // on the ramp's own edge
            CHECK(v[c * 3] <= 1e-3f || v[c * 3] >= 40.0f - 1e-3f);   // the ramps only: not the deck (its strip), not the cap
            lo = std::min(lo, v[c * 3 + 1]);
            hi = std::max(hi, v[c * 3 + 1]);
        }
        CHECK(lo > -0.11f && hi < 4.0f + 1e-3f);   // between the bank (a hair into it) and the deck, never the river's depth
        if (strip) {   // level across: each strip triangle's heights are the ramp edge's own (the slope along x: y = x/3 + 4 west, 4 - (x-40)/3 east)
            for (int c = 0; c < 3; ++c) {
                const float x = v[c * 3], want = x <= 0.0f ? 4.0f + x / 3.0f : 4.0f - (x - 40.0f) / 3.0f;
                CHECK(std::fabs(v[c * 3 + 1] - want) < 1e-3f);
            }
        }
    }
}

void Files() {
    // the measures file both ways
    holeslist::Line l;
    l.name = "a_place"; l.unit = "data/hd/env/model/d2r_vr/holes/a_place_unit.json";
    l.anchor[0] = 516; l.anchor[1] = -4; l.anchor[2] = 180; l.witness[0] = 504; l.witness[1] = -4; l.witness[2] = 187;
    l.triangles = 20; l.strips = 10; l.skirts = 0; l.area = 200.7f; l.holeArea = 170.4f; l.left = 9.4f; l.sides = 21; l.sideLeft = 0.9f; l.ramps = 18; l.seen = 0.5f;
    l.source = "data/hd/env/model/act1/outdoors/act1_outdoors_bridge/bridge_nw_tiles01.model"; l.sourceMesh = "polySurfaceShape1754"; l.technique = "TerrainMaterial";
    std::vector<holeslist::Line> back;
    CHECK(holeslist::Parse(holeslist::Write({l, l}), &back) && back.size() == 2);
    if (back.size() == 2) {
        CHECK(back[1].name == l.name && back[1].unit == l.unit && back[1].triangles == 20 && back[1].strips == 10 && back[1].sourceMesh == l.sourceMesh && back[1].sides == 21 &&
              std::fabs(back[1].sideLeft - 0.9f) < 1e-3f && back[1].ramps == 18 && std::fabs(back[1].seen - 0.5f) < 1e-3f);
        for (int c = 0; c < 3; ++c) CHECK(std::fabs(back[1].anchor[c] - l.anchor[c]) < 1e-3f && std::fabs(back[1].witness[c] - l.witness[c]) < 1e-3f);
    }
    CHECK(!holeslist::Parse("holes 99\n", &back));   // another version: not read
    // the unit: what the game needs (one root, one model), the rim's own masks, the textures
    Placement rim{"r", "m", worldscene::Mat{}, 0, true};
    rim.lightMask = 19; rim.shadowMask = 3; rim.floorModel = true; rim.terrainBlendMode = 1;
    const std::string u = holeslist::UnitJson(holeslist::kPlaces[0], rim, {"data/hd/env/texture/x_alb.texture"});
    CHECK(u.find("\"type\":\"UnitDefinition\"") != std::string::npos);
    size_t roots = 0;
    for (size_t at = u.find("\"UnitRootComponent\""); at != std::string::npos; at = u.find("\"UnitRootComponent\"", at + 1)) ++roots;
    CHECK(roots == 1);
    CHECK(u.find("\"filename\":\"" + holeslist::ModelPath(holeslist::kPlaces[0]) + "\"") != std::string::npos);
    CHECK(u.find("\"lightMask\":19,\"shadowMask\":3") != std::string::npos && u.find("\"floorModel\":true") != std::string::npos);
    CHECK(u.find("x_alb.texture") != std::string::npos);
    // the places: names and paths their own, never a ceiling's, under data/hd/env/model/, their units under ours
    std::set<std::string> names, models;
    for (const holeslist::Place& p : holeslist::kPlaces) {
        CHECK(names.insert(p.name).second);
        CHECK(models.insert(p.model).second);
        CHECK(holeslist::ModelPath(p).rfind("data/hd/env/model/", 0) == 0 && holeslist::UnitPath(p).rfind(holeslist::kDir, 0) == 0);
        for (const gameceiling::Kind& k : gameceiling::kKinds) {
            CHECK(holeslist::ModelPath(p) != gameceiling::ModelPath(k.slab));
            if (k.wall) CHECK(holeslist::ModelPath(p) != gameceiling::ModelPath(k.wall));
        }
        CHECK(holeslist::ModelPath(p) != gameceiling::ModelPath(gameceiling::kBeamModelFile));
        for (const char* v : gameceiling::kVaultModelFiles) CHECK(holeslist::ModelPath(p) != gameceiling::ModelPath(v));
    }
}

// Every place made from the game's own files (taken out under root): what Settings writes
void Game(const char* root) {
    const worldscene::Reader read = worldscene::FileReader(std::filesystem::path(root));
    // the free paths: named by the game's LOD table, by no other file of the game's we took out
    std::string lod;
    const bool haveLod = read("data/hd/model_lod_desc.json", &lod);
    for (const holeslist::Place& place : holeslist::kPlaces) {
        if (haveLod) CHECK(lod.find("\"" + holeslist::ModelPath(place) + "\"") != std::string::npos);
        holeslist::Made m;
        const bool ok = holeslist::Make(read, place, &m);
        std::printf("game: %s %s%s\n", place.name, ok ? "made" : "NOT made: ", ok ? "" : m.why.c_str());
        CHECK(ok);
        if (!ok) continue;
        const holeslist::Line& l = m.line;
        std::printf("game: %s - %d triangles (%d strips, %d skirts, %d ramp strips, %d side skirts, %.1f units of side left open; %.1f units of edge with an "
                    "opening seen from the eyes) over %.1f of a %.1f sq unit hole, %.1f left; anchor %.2f %.2f %.2f; %s %s (%s)\n", l.name.c_str(), l.triangles,
                    l.strips, l.skirts, l.ramps, l.sides, l.sideLeft, l.seen, l.area, l.holeArea, l.left,
                    l.anchor[0], l.anchor[1], l.anchor[2], l.source.c_str(), l.sourceMesh.c_str(), l.technique.c_str());
        CHECK(l.triangles > 0 && l.left < l.holeArea * 0.1f);
        CHECK(l.ramps + l.sides >= 18 && l.sideLeft < 2.0f);   // both ramps' open sides (Serg on 0.209.8: the ramp's side open from the front)
        CHECK(l.ramps >= 14 && l.seen < 1.5f);                  // ... and the slot to the parapet shut from the eyes (0.209.10: "в мосте дырка не заделана")
        CHECK(m.files.size() == (size_t)holeslist::kLods + 1);
        for (int lodN = 0; lodN < holeslist::kLods && lodN < (int)m.files.size(); ++lodN) {
            CHECK(m.files[(size_t)lodN].first == holeslist::LodPath(place, lodN));
            grannyremesh::File f;
            std::vector<worldholes::MeshData> meshes;
            std::string why;
            CHECK(grannyremesh::Read(m.files[(size_t)lodN].second, &f, &why) && worldholes::ReadMeshes(f, &meshes, &why));
            CHECK(meshes.size() == 1);
            if (meshes.size() != 1) continue;
            CHECK(meshes[0].groups.size() == 1 && meshes[0].groups[0].technique == l.technique);
            CHECK(meshes[0].idx.size() == (size_t)l.triangles * 3 && meshes[0].v.size() == (size_t)l.triangles * 3);
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    Bridge();
    Ditch(false);
    Ditch(true);
    Wagon();
    Ramps(false);
    Ramps(true);
    Files();
    if (argc >= 2) Game(argv[1]);
    std::printf(g_fail ? "world_holes_test: %d FAILED\n" : "world_holes_test: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
