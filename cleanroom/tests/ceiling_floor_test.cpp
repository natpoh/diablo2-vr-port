// ceiling_floor_test - shared/ceiling_floor.h: [ceiling] follow_floor, the game's cave ceiling a step up wherever the floor under it rises.
//   1. a tile's floor from its rays: one floor; the highest floor met by kAgree rays; a lone higher ray left out; one ray all there is;
//   2. the vertical ray's input as the game's floor ray lays its fields out; a hit off its line not taken;
//   3. the keeper: nearest the window's middle asked first, the budget kept, nothing asked again before its time, a known floor kept
//      when a later ask meets nothing; a not-known tile asked again after kRetryTicks, a known one after kRefreshTicks;
//   4. the tops: known tiles their floor + the height, the rest the highest known neighbour spreading in, none known the fallback;
//   5. the layout: a flat cave - the slab alone; Serg's sketch (a step up in the middle) - the slab at the upper top, a tile under it over
//      every lower place, a face on every edge of the step from the lower top up, bulging toward the upper side; a ramp - a step a tile;
//   6. the hero walking up a step changes no tile's top and lays every piece at the same place in the world (the window moving with him
//      lays the tiles it still holds where they were);
//   7. the matrices: a tile's corner on its world tile, its top at its top, the slab's scale; a face upright on its edge from the lower
//      top, its rock toward the upper side; the tile model's rock the slab's at the same place (seamless: CeilingPatchMesh vs CeilingMesh).
#include "ceiling_floor.h"
#include "game_ceiling.h"
#include "granny_remesh.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <vector>

static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

using namespace ceilfloor;

// A world's floor: the height at (x, z)
using World = std::function<float(float, float)>;

// The keeper fed as the game's thread feeds it, round the hero at (hx, hz), `ticks` ticks from `tick`
static uint32_t Feed(Keeper& k, const World& w, float tile, float hx, float hz, uint32_t tick, int ticks) {
    for (int t = 0; t < ticks; ++t, ++tick) {
        const int x0 = WindowFirst(hx, tile), z0 = WindowFirst(hz, tile);
        int tx[kWindow], tz[kWindow];
        const int n = k.Pick(x0, z0, tick, kBudgetRays / kRays, tx, tz);
        for (int i = 0; i < n; ++i) {
            float h[kRays];
            int got = 0;
            for (int r = 0; r < kRays; ++r) {
                float x, z;
                RayAt(tx[i], tz[i], tile, r, &x, &z);
                const float y = w(x, z);
                if (std::isfinite(y)) h[got++] = y;
            }
            k.Put(tx[i], tz[i], h, got, tick);
        }
    }
    return tick;
}
// The window's tops round (hx, hz)
static void TopsAt(const Keeper& k, float tile, float hx, float hz, float height, float fallback, float* tops, int* x0, int* z0) {
    *x0 = WindowFirst(hx, tile);
    *z0 = WindowFirst(hz, tile);
    float floor[kWindow];
    bool known[kWindow];
    for (int i = 0; i < kWindow; ++i) {
        const Tile* t = k.Find(*x0 + i % kAcross, *z0 + i / kAcross);
        known[i] = t && t->known;
        floor[i] = known[i] ? t->floor : 0.0f;
    }
    Tops(floor, known, height, fallback, tops);
}
// Every piece laid, by what it is and where it stands in the world: tiles by their world tile -> top; faces by edge -> (low, high, side)
struct Laid {
    float slab;
    std::map<std::pair<int, int>, float> tiles;
    std::map<std::tuple<int, int, bool>, std::tuple<float, float, bool>> faces;
};
static Laid LaidOf(const float* tops, int x0, int z0) {
    const Layout L = Lay(tops, x0, z0);
    Laid r;
    r.slab = L.slab;
    for (int i : L.tiles) r.tiles[{x0 + i % kAcross, z0 + i / kAcross}] = tops[i];
    for (const Face& f : L.faces) r.faces[{f.tx, f.tz, f.alongZ}] = {f.low, f.high, f.highPlus};
    return r;
}
static bool Near(float a, float b, float e = 1e-4f) { return std::fabs(a - b) <= e; }
// a point through a row-vector matrix
static void Apply(const float m[16], const float p[3], float out[3]) {
    for (int c = 0; c < 3; ++c) out[c] = p[0] * m[c] + p[1] * m[4 + c] + p[2] * m[8 + c] + m[12 + c];
}

int main() {
    // 1. a tile's floor
    {
        float f = 0.0f;
        const float one[16] = {5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5};
        CHECK(TileFloor(one, 16, &f) && f == 5.0f);
        const float two[16] = {5, 5, 5, 5, 5, 5, 5, 5, 9, 9, 9, 9, 9, 9, 9, 9};   // half up a step: the higher
        CHECK(TileFloor(two, 16, &f) && f == 9.0f);
        const float lone[16] = {5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 40};   // one ray on something higher: not the tile's
        CHECK(TileFloor(lone, 16, &f) && f == 5.0f);
        const float two2[16] = {5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 12, 12};   // a ledge two rays met: the tile's
        CHECK(TileFloor(two2, 16, &f) && f == 12.0f);
        const float single[1] = {7.3f};
        CHECK(TileFloor(single, 1, &f) && Near(f, Quant(7.3f)));
        CHECK(!TileFloor(single, 0, &f));
        const float close[4] = {5.0f, 5.2f, 5.4f, 5.1f};   // one floor (within kSame): its top, rounded
        CHECK(TileFloor(close, 4, &f) && f == Quant(5.4f));
    }
    // 2. the ray
    {
        uint8_t in[kRayInputSize];
        RayInput(10.0f, 20.0f, 300.0f, -300.0f, 0x1234ull, in);
        float from[4], to[4], radius, scale;
        uint64_t filter;
        std::memcpy(from, in, 16); std::memcpy(to, in + 0x10, 16); std::memcpy(&radius, in + 0x50, 4); std::memcpy(&scale, in + 0x54, 4);
        std::memcpy(&filter, in + 0x58, 8);
        CHECK(from[0] == 10.0f && from[1] == 300.0f && from[2] == 20.0f && from[3] == 1.0f);
        CHECK(to[0] == 10.0f && to[1] == -300.0f && to[2] == 20.0f && to[3] == 1.0f);
        CHECK(radius == 0.05f && Near(scale, 1.0f / 60.96f, 1e-6f) && filter == 0x1234ull);
        float h = 0.0f;
        const float good[3] = {10.0f, 3.5f, 20.0f}, aside[3] = {11.0f, 3.5f, 20.0f}, above[3] = {10.0f, 400.0f, 20.0f};
        CHECK(Hit(good, 10.0f, 20.0f, 300.0f, -300.0f, &h) && h == 3.5f);
        CHECK(!Hit(aside, 10.0f, 20.0f, 300.0f, -300.0f, &h) && !Hit(above, 10.0f, 20.0f, 300.0f, -300.0f, &h));
    }
    const float tile = 40.0f, height = 15.6f;
    // 3. the keeper
    {
        Keeper k;
        const World flat = [](float, float) { return 0.0f; };
        int tx[kWindow], tz[kWindow];
        const int n = k.Pick(0, 0, 1, 6, tx, tz);
        CHECK(n == 6);
        for (int i = 0; i < 4; ++i) CHECK(tx[i] >= kHalf - 1 && tx[i] <= kHalf && tz[i] >= kHalf - 1 && tz[i] <= kHalf);   // the middle four first
        for (int i = 4; i < n; ++i) CHECK(tx[i] >= kHalf - 2 && tx[i] <= kHalf + 1 && tz[i] >= kHalf - 2 && tz[i] <= kHalf + 1);   // then the ring round them
        uint32_t tick = Feed(k, flat, tile, 0.0f, 0.0f, 1, 1);
        CHECK(k.Size() == (size_t)(kBudgetRays / kRays));   // the budget a tick
        tick = Feed(k, flat, tile, 0.0f, 0.0f, tick, kWindow);
        CHECK(k.Size() == (size_t)kWindow);
        CHECK(k.Pick(WindowFirst(0.0f, tile), WindowFirst(0.0f, tile), tick, 6, tx, tz) == 0);   // all known, none due again
        CHECK(k.Pick(WindowFirst(0.0f, tile), WindowFirst(0.0f, tile), tick + kRefreshTicks + 10, 6, tx, tz) == 6);   // refreshed later
        // a known floor kept when an ask meets nothing (its room put away for a moment)
        k.Put(3, 3, nullptr, 0, tick);
        const Tile* t = k.Find(3, 3);
        CHECK(t && t->known && t->floor == 0.0f);
        // a tile never met: asked again only after kRetryTicks
        k.Put(100, 100, nullptr, 0, 500);
        CHECK(k.Find(100, 100) && !k.Find(100, 100)->known);
        int got = k.Pick(100 - kHalf, 100 - kHalf, 500 + kRetryTicks - 1, kWindow, tx, tz);
        bool again = false;
        for (int i = 0; i < got; ++i) again = again || (tx[i] == 100 && tz[i] == 100);
        CHECK(!again);
        got = k.Pick(100 - kHalf, 100 - kHalf, 500 + kRetryTicks, kWindow, tx, tz);
        for (int i = 0; i < got; ++i) again = again || (tx[i] == 100 && tz[i] == 100);
        CHECK(again);
    }
    // 4. the tops
    {
        float floor[kWindow] = {}, tops[kWindow];
        bool known[kWindow] = {};
        CHECK(Tops(floor, known, height, 2.0f, tops) == 0);
        for (float t : tops) CHECK(t == Quant(2.0f + height));   // nothing known: the old floor
        floor[0] = 10.0f; known[0] = true;
        floor[kWindow - 1] = 4.0f; known[kWindow - 1] = true;
        CHECK(Tops(floor, known, height, 2.0f, tops) == 2);
        CHECK(tops[0] == Quant(10.0f + height) && tops[kWindow - 1] == Quant(4.0f + height));
        CHECK(tops[1] == tops[0] && tops[kAcross] == tops[0]);   // its neighbours take it
        CHECK(tops[kWindow - 2] == Quant(10.0f + height) || tops[kWindow - 2] == Quant(4.0f + height));
        CHECK(tops[kWindow - 2] == tops[kWindow - 1] || tops[kWindow - 2] > tops[kWindow - 1]);   // the highest known neighbour
    }
    // 5. the layout
    {
        float tops[kWindow];
        for (float& t : tops) t = 20.0f;
        Layout L = Lay(tops, 0, 0);
        CHECK(L.slab == 20.0f && L.tiles.empty() && L.faces.empty());   // a flat cave: the slab alone, as before
        // Serg's sketch: the floor a step up (5 units) from the window's middle on, along x
        for (int i = 0; i < kWindow; ++i) tops[i] = (i % kAcross) < kHalf ? 20.0f : 25.0f;
        L = Lay(tops, 0, 0);
        CHECK(L.slab == 25.0f && L.lowest == 20.0f);
        CHECK((int)L.tiles.size() == kHalf * kAcross);
        for (int i : L.tiles) CHECK(i % kAcross < kHalf && tops[i] == 20.0f);   // a tile over every lower place, at its own top
        CHECK((int)L.faces.size() == kAcross && L.facesLeft == 0);              // a face on every edge of the step
        for (const Face& f : L.faces) CHECK(!f.alongZ && f.tx == kHalf - 1 && f.highPlus && f.low == 20.0f && f.high == 25.0f);
        // a ramp: a step a tile (1 unit each)
        for (int i = 0; i < kWindow; ++i) tops[i] = 20.0f + (float)(i % kAcross);
        L = Lay(tops, 0, 0);
        CHECK(L.slab == 20.0f + kAcross - 1 && (int)L.tiles.size() == (kAcross - 1) * kAcross && (int)L.faces.size() == (kAcross - 1) * kAcross);
        for (const Face& f : L.faces) CHECK(Near(f.high - f.low, 1.0f) && f.highPlus && !f.alongZ);
        // the step going down on the other side: the higher on the - side
        for (int i = 0; i < kWindow; ++i) tops[i] = (i / kAcross) < 3 ? 30.0f : 20.0f;
        L = Lay(tops, 0, 0);
        CHECK((int)L.faces.size() == kAcross);
        for (const Face& f : L.faces) CHECK(f.alongZ && f.tz == 2 && !f.highPlus);
        // too many faces: kept to kMaxFaces, the rest counted
        for (int i = 0; i < kWindow; ++i) tops[i] = ((i % kAcross) + (i / kAcross)) % 2 ? 20.0f : 30.0f;
        L = Lay(tops, 0, 0);
        CHECK((int)L.faces.size() == kMaxFaces && L.facesLeft == 2 * kAcross * (kAcross - 1) - kMaxFaces);
        // steps smaller than kStepMin: one height
        for (int i = 0; i < kWindow; ++i) tops[i] = 20.0f + ((i % 2) ? 0.1f : 0.0f);
        L = Lay(tops, 0, 0);
        CHECK(L.tiles.empty() && L.faces.empty());
    }
    // 6. the hero walking up a step: nothing changes in the world
    {
        // a cave: the floor at 0, a raised part (x past 130) 6 units up - a step the hero climbs; walls (no floor) past |z| 500
        const World cave = [](float x, float z) { return std::fabs(z) > 500.0f ? NAN : x > 130.0f ? 6.0f : 0.0f; };
        Keeper k;
        uint32_t tick = Feed(k, cave, tile, 125.0f, 0.0f, 1, 200);   // the hero below the step (x 125), long enough to know his window
        float a[kWindow], b[kWindow];
        int ax0, az0, bx0, bz0;
        TopsAt(k, tile, 125.0f, 0.0f, height, 0.0f, a, &ax0, &az0);
        // he climbs (stands on the raised part now, x 135, 6 up): the game's thread asks round him, nothing kept changes
        tick = Feed(k, cave, tile, 135.0f, 0.0f, tick, 200);
        TopsAt(k, tile, 135.0f, 0.0f, height, 6.0f, b, &bx0, &bz0);
        CHECK(ax0 == bx0 && az0 == bz0);   // (the same window: within half a tile of the same tile line)
        for (int i = 0; i < kWindow; ++i) CHECK(a[i] == b[i]);
        // the step is at the place of the floor's step: the tile holding x 130 .. 160 (tile 3: 120 .. 160) is up, tile 2 not
        for (int i = 0; i < kWindow; ++i) {
            const int tx = ax0 + i % kAcross;
            CHECK(a[i] == Quant((tx >= 3 ? 6.0f : 0.0f) + height));
        }
        // the hero walks on (x 300, and on to 700): the window moves with him; every tile both windows hold is laid at the same top, every
        // face both hold the same
        const Laid la = LaidOf(a, ax0, az0);
        Feed(k, cave, tile, 300.0f, 0.0f, tick, 300);
        float c[kWindow];
        int cx0, cz0;
        TopsAt(k, tile, 300.0f, 0.0f, height, 6.0f, c, &cx0, &cz0);
        const Laid lc = LaidOf(c, cx0, cz0);
        CHECK(cx0 != ax0);
        for (int i = 0; i < kWindow; ++i) {
            const int tx = cx0 + i % kAcross, tz = cz0 + i / kAcross;
            const int ai = (tz - az0) * kAcross + (tx - ax0);
            if (tx >= ax0 && tx < ax0 + kAcross && tz >= az0 && tz < az0 + kAcross) CHECK(c[i] == a[ai]);
        }
        for (const auto& t : lc.tiles) {
            const auto it = la.tiles.find(t.first);
            if (it != la.tiles.end()) CHECK(it->second == t.second);
        }
        for (const auto& f : lc.faces) {
            const auto it = la.faces.find(f.first);
            if (it != la.faces.end()) CHECK(it->second == f.second);
        }
        CHECK(la.slab == Quant(6.0f + height) && !la.tiles.empty() && !la.faces.empty());
        // the walls (no floor met) take their known neighbours' highest: no step inside the rock at its edge
        Feed(k, cave, tile, 100.0f, 470.0f, tick + 1000, 300);
        float d[kWindow];
        int dx0, dz0;
        TopsAt(k, tile, 100.0f, 470.0f, height, 0.0f, d, &dx0, &dz0);
        for (int i = 0; i < kWindow; ++i) {
            const int tx = dx0 + i % kAcross;
            CHECK(d[i] == Quant((tx >= 3 ? 6.0f : 0.0f) + height));
        }
    }
    // 7. the matrices and the tile model
    {
        const float k = tile / gameceiling::kTile;
        float m[16], p[3];
        TileMatrix(-3, 5, tile, k, 21.5f, m);
        const float corner[3] = {0, 0, 0}, far[3] = {gameceiling::kTile, -gameceiling::kRelief, gameceiling::kTile};
        Apply(m, corner, p);
        CHECK(p[0] == -120.0f && p[1] == 21.5f && p[2] == 200.0f);
        Apply(m, far, p);
        CHECK(Near(p[0], -80.0f) && Near(p[1], 21.5f - gameceiling::kRelief * k) && Near(p[2], 240.0f));
        // a face on the edge x = 160 (between tiles 3 and 4 along x, row 2), from 20 up to 25, the upper side + x
        Face f;
        f.tx = 3; f.tz = 2; f.alongZ = false; f.highPlus = true; f.low = 20.0f; f.high = 25.0f;
        FaceMatrix(f, tile, k, gameceiling::kTile, m);
        const float a0[3] = {0, 0, 0}, a1[3] = {gameceiling::kTile, 0, 0}, top[3] = {0, 0, gameceiling::kTile}, bulge[3] = {0, -gameceiling::kRelief, 0};
        Apply(m, a0, p);
        CHECK(p[0] == 160.0f && p[1] == 20.0f && p[2] == 80.0f);       // on the edge, at the lower top
        Apply(m, a1, p);
        CHECK(Near(p[0], 160.0f) && Near(p[1], 20.0f) && Near(p[2], 120.0f));   // one tile along it
        Apply(m, top, p);
        CHECK(Near(p[0], 160.0f) && Near(p[1], 20.0f + tile) && Near(p[2], 80.0f));   // up a tile (the step 5: past the top inside the rock)
        Apply(m, bulge, p);
        CHECK(p[0] > 160.0f && Near(p[1], 20.0f));                      // its rock toward the upper side
        f.highPlus = false;
        FaceMatrix(f, tile, k, gameceiling::kTile, m);
        Apply(m, bulge, p);
        CHECK(p[0] < 160.0f);
        // a tall step (60): the face as tall as the step
        f.high = 80.0f;
        FaceMatrix(f, tile, k, gameceiling::kTile, m);
        Apply(m, top, p);
        CHECK(Near(p[1], 80.0f));
        // along z: the edge z = (tz + 1) tile
        f.alongZ = true; f.highPlus = true; f.high = 25.0f;
        FaceMatrix(f, tile, k, gameceiling::kTile, m);
        Apply(m, a0, p);
        CHECK(p[0] == 120.0f && p[1] == 20.0f && p[2] == 120.0f);
        Apply(m, a1, p);
        CHECK(Near(p[0], 160.0f) && Near(p[2], 120.0f));
        Apply(m, bulge, p);
        CHECK(p[2] > 120.0f);
        // the tile model: one tile from its corner, the slab's rock at the same place (both decoded from their int16 positions)
        const grannyremesh::Mesh slab = grannyremesh::CeilingMesh(gameceiling::kTile, gameceiling::kTilesHalf, gameceiling::kCells, gameceiling::kRelief);
        const grannyremesh::Mesh one = grannyremesh::CeilingPatchMesh(gameceiling::kTile, 1, gameceiling::kCells, gameceiling::kRelief);
        CHECK(one.idx.size() == gameceiling::kPatchIndexCount);
        CHECK(one.v.size() == (size_t)(gameceiling::kCells + 1) * (gameceiling::kCells + 1));
        CHECK(Near(one.mn[0], 0.0f) && Near(one.mx[0], gameceiling::kTile) && Near(one.mn[2], 0.0f) && Near(one.mx[2], gameceiling::kTile));
        CHECK(one.mx[1] <= 0.0f && one.mn[1] >= -gameceiling::kRelief - 1e-3f);
        const int row = 2 * gameceiling::kTilesHalf * gameceiling::kCells + 1;
        int matched = 0;
        for (int i = 0; i <= gameceiling::kCells; ++i)
            for (int j = 0; j <= gameceiling::kCells; ++j) {
                const grannyremesh::Vertex& a = one.v[i * (gameceiling::kCells + 1) + j];
                // the slab's vertex at the same place within a tile: tile (2, 3) from its corner
                const grannyremesh::Vertex& b = slab.v[(3 * gameceiling::kCells + i) * row + (2 * gameceiling::kCells + j)];
                const float ya = a.pos[1] / 32767.0f * one.scale, yb = b.pos[1] / 32767.0f * slab.scale;
                matched += Near(ya, yb, 0.02f) && a.normal[0] == b.normal[0] && a.normal[1] == b.normal[1] && a.normal[2] == b.normal[2] &&
                           a.tangent[1] == b.tangent[1];
            }
        CHECK(matched == (gameceiling::kCells + 1) * (gameceiling::kCells + 1));
        for (int t = 0; t + 2 < (int)one.idx.size(); t += 3)
            CHECK(one.idx[t] < one.v.size() && one.idx[t + 1] < one.v.size() && one.idx[t + 2] < one.v.size());
        // its unit and paths: the natural caves have one, every one its own, none another piece's
        int withPatch = 0;
        for (int kk = 0; kk < gameceiling::kKindCount; ++kk) {
            const gameceiling::Kind& kind = gameceiling::kKinds[kk];
            CHECK((kind.patch != nullptr) == (kind.relief > 0.0f));
            if (!kind.patch) continue;
            ++withPatch;
            const std::string u = gameceiling::PatchUnitJson(kk);
            CHECK(u.find(gameceiling::ModelPath(kind.patch)) != std::string::npos && u.find(gameceiling::MapPath(kind, "ALB")) != std::string::npos);
            CHECK(gameceiling::PatchUnitPath(kk) != gameceiling::UnitPath(kk, false) && gameceiling::PatchUnitPath(kk) != gameceiling::UnitPath(kk, true));
            for (int o = 0; o < gameceiling::kKindCount; ++o) {
                const gameceiling::Kind& other = gameceiling::kKinds[o];
                CHECK(std::string(kind.patch) != other.slab && (!other.wall || std::string(kind.patch) != other.wall));
                CHECK(o == kk || !other.patch || std::string(kind.patch) != other.patch);
                CHECK(gameceiling::PatchSaltOf(kk) != gameceiling::SaltOf(o, false) && gameceiling::PatchSaltOf(kk) != gameceiling::SaltOf(o, true));
            }
            for (const char* v : gameceiling::kVaultModelFiles) CHECK(std::string(kind.patch) != v);
            CHECK(std::string(kind.patch) != gameceiling::kBeamModelFile);
        }
        CHECK(withPatch == 3);
    }
    std::printf("ceiling_floor_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
