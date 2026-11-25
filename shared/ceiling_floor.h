#pragma once
// ceiling_floor.h - [ceiling] follow_floor: the game's cave ceiling stands on the floor under each place, not on the hero's (Serg
// 2026-11-25: "в пещере если пол поднимается то потолок так же должен в этом месте подниматься ступенькой, сейчас у нас потолок просто
// находится на высоте от героя и привязан к нему"; his sketch: the floor with a step up, the ceiling over it with the same step at the same
// place - the ceiling a constant height over the floor under each point).
//
// Until now the slab (shared/game_ceiling.h) was one model at one height: the room's height over "the area's floor" - the lowest the hero
// had stood on since the area changed, which the game's biome being set again every few seconds kept resetting to where he stood - and
// [ceiling] min_headroom_m lifted all of it when he climbed. So the ceiling went up and down with him, and over a raised floor it was low.
//
// Now, with follow_floor:
//   - The floor under each ceiling tile (one picture of the slab, `tile` world units, on the world's grid of whole tiles the slab is laid
//     on) is asked of the game's own floor world: kSide x kSide vertical rays through the tile (the physics manager's closest ray with the
//     level layer's floor filter - the same floor the game puts the hero and his feet on, vr/terrain_heights.h), on the game's thread, a
//     budget of rays a tick, nearest the hero first. The tile's floor: its highest floor that at least kAgree rays met (a ceiling over every
//     floor under it), rounded to kQuant so one floor is one height. Kept per tile for the area (world-fixed: the hero walking up a step
//     asks nothing again and changes nothing); a tile whose rays met nothing (its room not loaded yet) asked again after kRetryTicks, a
//     known one refreshed after kRefreshTicks (rooms the game moves later).
//   - Each tile's ceiling top = its floor + the room's height. A tile not known yet takes the highest of its known neighbours (spreading
//     in), none known: the old floor (`fallback`) - so before the first rays come back the ceiling is the one it was.
//   - Drawn with the game's own pieces: the slab as it was, at the HIGHEST top of the tiles it spans (opaque from below: no part of it can
//     be raised, so it is the top and the lower places are brought down); a tile of the same rock (its own model, one tile of the slab's
//     mesh, the same relief and picture: seamless with the slab and with its neighbours) at its own top under every tile lower than that;
//     and between two tiles of different tops a step face - that same tile model stood upright on the edge, from the lower top up, its
//     rock bulging toward the higher side (the side it is seen from). No void at any step: wherever a lower tile ends, the slab or a
//     higher tile is above it, and the face closes the side.
// Pure; the test is cleanroom/tests/ceiling_floor_test.cpp. The rays: vr/render/render_switches.cpp; the pieces: vr/engine/engitem.cpp.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace ceilfloor {

constexpr int kSide = 4;                    // rays a tile, each way (4 x 4: 10 world units apart at tile 40 - the terrain's own tile)
constexpr int kRays = kSide * kSide;
constexpr float kSame = 0.5f;               // floors this close are one (world units; the hero is ~7.5 tall)
constexpr int kAgree = 2;                   // a floor met by fewer rays than this is not the tile's highest (unless it is all there is)
constexpr float kQuant = 1.0f / 32.0f;      // tops rounded to this: one floor, one height - no hairline steps
constexpr float kRayReach = 300.0f;         // the vertical ray: from this far over the hero's height down to this far under it
constexpr int kHalf = 6;                    // the window: the slab's tiles, kHalf each way from its middle (gameceiling::kTilesHalf)
constexpr int kAcross = 2 * kHalf;
constexpr int kWindow = kAcross * kAcross;
constexpr int kBudgetRays = 96;             // rays a game tick (6 tiles)
constexpr uint32_t kRetryTicks = 50;        // a tile whose rays met nothing: asked again after this many ticks (~2 s)
constexpr uint32_t kRefreshTicks = 250;     // a known tile: asked again after this many (~10 s; rooms the game moves later)
constexpr float kStepMin = 0.25f;           // tops closer than this: one height (no tile, no face)
constexpr int kMaxTiles = kWindow;          // tiles under the slab at most (every one of the window but the highest)
constexpr int kMaxFaces = 192;              // step faces at most (a checkerboard of 12 x 12 would be 264: the rest left out, counted)

inline float Quant(float h) { return std::round(h / kQuant) * kQuant; }

// The world tile a world x (or z) falls in, `tile` units a tile (the slab's tiles: [k tile, (k + 1) tile))
inline int TileOf(float w, float tile) { return (int)std::floor((double)w / tile); }
// The window's first tile each way: the slab's middle is on the whole tile line nearest the hero (gameceiling::Matrix), kHalf tiles each way
inline int WindowFirst(float heroW, float tile) { return (int)std::floor((double)heroW / tile + 0.5) - kHalf; }

// Where ray k of a tile goes through (world x, z): the middles of a kSide x kSide grid over the tile
inline void RayAt(int tx, int tz, float tile, int k, float* x, float* z) {
    *x = (float)(((double)tx + ((k % kSide) + 0.5) / kSide) * tile);
    *z = (float)(((double)tz + ((k / kSide) + 0.5) / kSide) * tile);
}

// The vertical ray's input, laid out as the game lays its own floor ray (terrainheights::RayInput: the fields RayCastClosest reads), only
// straight down through (x, z) from yTop to yBottom
constexpr size_t kRayInputSize = 0x80, kRayOutSize = 0x100;
inline void RayInput(float x, float z, float yTop, float yBottom, uint64_t filter, uint8_t in[kRayInputSize]) {
    std::memset(in, 0, kRayInputSize);
    const float from[4] = {x, yTop, z, 1.0f}, to[4] = {x, yBottom, z, 1.0f};
    const float one = 1.0f, radius = 0.05f, scale = 0.016404198482632637f, unit[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    std::memcpy(in + 0x00, from, sizeof from);
    std::memcpy(in + 0x10, to, sizeof to);
    std::memcpy(in + 0x20, &one, 4);
    std::memcpy(in + 0x40, unit, sizeof unit);
    std::memcpy(in + 0x50, &radius, 4);
    std::memcpy(in + 0x54, &scale, 4);
    std::memcpy(in + 0x58, &filter, 8);
}
// A hit's point for the ray through (x, z): the floor's height, or false when it is not this ray's answer
inline bool Hit(const float out[3], float x, float z, float yTop, float yBottom, float* h) {
    if (!std::isfinite(out[0]) || !std::isfinite(out[1]) || !std::isfinite(out[2])) return false;
    if (std::fabs(out[0] - x) > 0.1f || std::fabs(out[2] - z) > 0.1f) return false;
    if (out[1] > yTop + 0.5f || out[1] < yBottom - 0.5f) return false;
    *h = out[1];
    return true;
}

// A tile's floor from its rays' heights (`n` of them met a floor): the highest floor at least kAgree of them met (or all there is, fewer
// rays than that), rounded; false: none met
inline bool TileFloor(const float* h, int n, float* floor) {
    if (n <= 0) return false;
    float lv[kRays];
    int count[kRays], m = 0;
    float top[kRays];
    for (int i = 0; i < n && i < kRays; ++i) {   // floors: within kSame of a floor's first
        int j = 0;
        while (j < m && std::fabs(lv[j] - h[i]) >= kSame) ++j;
        if (j == m) { lv[m] = h[i]; top[m] = h[i]; count[m] = 0; ++m; }
        ++count[j];
        if (h[i] > top[j]) top[j] = h[i];
    }
    const int need = n < kAgree ? n : kAgree;
    int best = -1;
    for (int j = 0; j < m; ++j)
        if (count[j] >= need && (best < 0 || top[j] > top[best])) best = j;
    if (best < 0) return false;
    *floor = Quant(top[best]);
    return true;
}

// What is kept of a tile
struct Tile {
    bool asked = false;
    bool known = false;     // a floor met
    float floor = 0.0f;
    uint32_t at = 0;        // the tick it was asked
};

// The tiles' floors kept for the area, by world tile
class Keeper {
public:
    static int64_t Key(int tx, int tz) { return (int64_t(uint32_t(tx)) << 32) | uint32_t(tz); }
    const Tile* Find(int tx, int tz) const {
        const auto it = map_.find(Key(tx, tz));
        return it == map_.end() ? nullptr : &it->second;
    }
    // Up to `budget` tiles of the window starting at (x0, z0) to ask this tick: never asked first, nearest the window's middle first; then
    // those whose rays met nothing, kRetryTicks on; then known ones kRefreshTicks on, the oldest first
    int Pick(int x0, int z0, uint32_t tick, int budget, int* tx, int* tz) const {
        int64_t key[kWindow];
        int cand[kWindow], m = 0;
        for (int i = 0; i < kWindow; ++i) {
            const int x = i % kAcross, z = i / kAcross;
            const Tile* t = Find(x0 + x, z0 + z);
            const int dx = std::abs(2 * x - (kAcross - 1)), dz = std::abs(2 * z - (kAcross - 1)), ring = (dx > dz ? dx : dz) / 2;
            if (!t || !t->asked) key[m] = ring;
            else {
                const uint32_t age = tick - t->at;
                if (!t->known && age >= kRetryTicks) key[m] = 100 + ring;
                else if (t->known && age >= kRefreshTicks) key[m] = 1000 + int64_t(0xFFFFFFFFu - age);
                else continue;
            }
            cand[m++] = i;
        }
        int n = 0;
        for (; n < budget && n < m; ++n) {
            int b = n;
            for (int j = n + 1; j < m; ++j)
                if (key[j] < key[b]) b = j;
            const int64_t k = key[n]; key[n] = key[b]; key[b] = k;
            const int c = cand[n]; cand[n] = cand[b]; cand[b] = c;
            tx[n] = x0 + cand[n] % kAcross;
            tz[n] = z0 + cand[n] / kAcross;
        }
        return n;
    }
    // A tile's rays' answers (`h`, `n` of them met a floor)
    void Put(int tx, int tz, const float* h, int n, uint32_t tick) {
        Tile t;
        t.asked = true;
        t.at = tick;
        t.known = TileFloor(h, n, &t.floor);
        if (!t.known) {   // nothing met now: a floor met before is kept (a room put away for a moment), asked again later
            const Tile* was = Find(tx, tz);
            if (was && was->known) { t.known = true; t.floor = was->floor; }
        }
        map_[Key(tx, tz)] = t;
    }
    void Clear() { map_.clear(); }
    size_t Size() const { return map_.size(); }

private:
    std::unordered_map<int64_t, Tile> map_;
};

// The window's tops: `floor`/`known` kWindow tiles (index z * kAcross + x), `height` the room's ceiling height over its floor; a tile not
// known takes the highest of its known neighbours (W N E S, spreading in), none: `fallback` (the old floor) + height. out: kWindow tops.
// Returns how many were known.
inline int Tops(const float* floor, const bool* known, float height, float fallback, float* out) {
    bool set[kWindow];
    int nKnown = 0;
    for (int i = 0; i < kWindow; ++i) {
        set[i] = known[i];
        if (known[i]) { out[i] = Quant(floor[i] + height); ++nKnown; }
    }
    for (int pass = 0; pass < kAcross * 2; ++pass) {
        bool now[kWindow] = {}, more = false;
        float v[kWindow];
        for (int i = 0; i < kWindow; ++i) {
            if (set[i]) continue;
            const int x = i % kAcross, z = i / kAcross;
            const int nb[4] = {x > 0 ? i - 1 : -1, z > 0 ? i - kAcross : -1, x < kAcross - 1 ? i + 1 : -1, z < kAcross - 1 ? i + kAcross : -1};
            bool any = false;
            float best = 0.0f;
            for (int n : nb)
                if (n >= 0 && set[n] && (!any || out[n] > best)) { best = out[n]; any = true; }
            if (any) { v[i] = best; now[i] = true; }
            else more = true;
        }
        for (int i = 0; i < kWindow; ++i)
            if (now[i]) { out[i] = v[i]; set[i] = true; }
        if (!more) break;
    }
    for (int i = 0; i < kWindow; ++i)
        if (!set[i]) out[i] = Quant(fallback + height);
    return nKnown;
}

// The pieces laid for a window of tops: the slab's top (the highest), the tiles under it and the step faces
struct Face {
    int tx = 0, tz = 0;     // the lower-x (or lower-z) tile of the two
    bool alongZ = false;    // false: the edge between (tx, tz) and (tx + 1, tz), running along z; true: between (tx, tz) and (tx, tz + 1)
    bool highPlus = false;  // the higher top is on the + side (tx + 1 or tz + 1)
    float low = 0.0f, high = 0.0f;
};
struct Layout {
    float slab = 0.0f;                  // the slab's top
    float lowest = 0.0f;                // the lowest top
    std::vector<int> tiles;             // window indices of the tiles laid under the slab (their top lower than it)
    std::vector<Face> faces;
    int facesLeft = 0;                  // faces past kMaxFaces, not laid
};
inline Layout Lay(const float* tops, int x0, int z0) {
    Layout L;
    L.slab = L.lowest = tops[0];
    for (int i = 1; i < kWindow; ++i) {
        if (tops[i] > L.slab) L.slab = tops[i];
        if (tops[i] < L.lowest) L.lowest = tops[i];
    }
    for (int i = 0; i < kWindow; ++i)
        if (tops[i] < L.slab - kStepMin) L.tiles.push_back(i);
    for (int i = 0; i < kWindow; ++i) {
        const int x = i % kAcross, z = i / kAcross;
        for (int d = 0; d < 2; ++d) {   // the + x and the + z neighbour
            if ((d == 0 && x == kAcross - 1) || (d == 1 && z == kAcross - 1)) continue;
            const int j = d == 0 ? i + 1 : i + kAcross;
            const float a = tops[i], b = tops[j];
            if (std::fabs(a - b) < kStepMin) continue;
            if ((int)L.faces.size() >= kMaxFaces) { ++L.facesLeft; continue; }
            Face f;
            f.tx = x0 + x;
            f.tz = z0 + z;
            f.alongZ = d == 1;
            f.highPlus = b > a;
            f.low = a < b ? a : b;
            f.high = a < b ? b : a;
            L.faces.push_back(f);
        }
    }
    return L;
}

// A tile's local matrix (row vectors, the place in [12..14]): the tile model (grannyremesh::CeilingPatchMesh: one tile from its corner (0,
// 0) to (kTile, kTile) model units, top at y 0) scaled `k` (world units a model unit: tile / kTile), its corner on world tile (tx, tz), its
// top at `top`
inline void TileMatrix(int tx, int tz, float tile, float k, float top, float m[16]) {
    const float out[16] = {k, 0, 0, 0, 0, k, 0, 0, 0, 0, k, 0, (float)((double)tx * tile), top, (float)((double)tz * tile), 1};
    std::memcpy(m, out, sizeof out);
}
// A step face's local matrix: the same tile model stood upright on the edge - its x along the edge (one tile), its z up from the lower top
// (a tile's height, or the step's when it is taller: the part over the higher top is inside that one's rock), its rock (model y down)
// bulging toward the higher side, the side it is seen from
inline void FaceMatrix(const Face& f, float tile, float k, float modelTile, float m[16]) {
    const float rise = f.high - f.low, tall = rise > tile ? rise : tile, up = tall / modelTile;
    const float toward = f.highPlus ? -k : k;   // model y is <= 0 (hanging): -y * k toward the higher side
    if (!f.alongZ) {   // the edge x = (tx + 1) tile, z from tz tile along one tile
        const float out[16] = {0, 0, k, 0, toward, 0, 0, 0, 0, up, 0, 0, (float)((double)(f.tx + 1) * tile), f.low, (float)((double)f.tz * tile), 1};
        std::memcpy(m, out, sizeof out);
    } else {           // the edge z = (tz + 1) tile, x from tx tile along one tile
        const float out[16] = {k, 0, 0, 0, 0, 0, toward, 0, 0, up, 0, 0, (float)((double)f.tx * tile), f.low, (float)((double)(f.tz + 1) * tile), 1};
        std::memcpy(m, out, sizeof out);
    }
}

}  // namespace ceilfloor
