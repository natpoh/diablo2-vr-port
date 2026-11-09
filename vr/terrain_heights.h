// terrain_heights.h - [render] terrain_heights: each ground tile of the game's terrain drawn at the height of its own floor, stitched to
// the tiles round it, instead of all of them at the height of the floor under the hero. Pure; the test is
// cleanroom/tests/terrain_heights_test.cpp, the wiring is vr/render/render_switches.cpp (HookTerrainOrigin).
//
// The complaint (2026-11-08, Serg, from the streams): in areas with more than one level of ground - the act 2 desert with its stairs up to
// a raised plateau, ledges - the ground's tiles are drawn at the wrong height: the sand of the upper level lies down at the lower one,
// pushed aside, while the upper level's stones and cliffs stand up where they belong; walk up the stairs and the whole ground jumps up,
// the lowland's sand now hanging in the air. The models (cliffs, stairs, stones, houses) are right; only the ground's own tiles are not.
//
// The cause, read in the game's code (3.3.93787, D2R_image.bin; static reading, not run against the game):
//   - The outdoor ground (sand, grass, snow, the paving and cracked earth over it) is the terrain system (D2Render\src\terrain.cpp,
//     "TerrainSystem"): a window of 18 x 18 tiles round the hero (a tile = 5 x 5 cells = 10 x 10 world units), each tile a bin of 0x20
//     bytes from 0x34FCB10: +0x10 the tile's data (null: none there), +0x18 the tile's HEIGHT (float). The window's corner tile is at
//     0x34FF608 / 0x34FF60C; a bin's index is (ty - corner y) * 18 + (tx - corner x).
//   - Each bin's height is what everything of that tile is drawn with: the tile's quad (0xE769D0: at (10 tx + 5 + 1.2247 h, h,
//     10 ty + 5 + 1.2247 h) - raised and pushed along the game's own view, as every raised thing is, flat_ground.h), its culling (0xE775D3),
//     its stamps (0xE71988) and decals (0xE7284A). The system was made for a height per tile.
//   - But the only writer, TerrainSystem's origin (0xE79000 (const int2* tile, float height)), puts ONE height in all 324 bins, and its one
//     caller in a game, LevelTranslationLayer::UpdateTerrainTiles (0x6B54C0, every tick of the level layer), gives it the floor under the
//     hero: 0x6B0350 (layer, out, the hero's cell, hero, 0), the game's "floor under a cell" query - the floor world's surface along the
//     game's view through the cell (2x, 0, 2y). The other height the game keeps (0x34FF604, "Height: %0.2f") is read by its debug panel only.
//   - From the game's camera above a tile at any height sits exactly where it should (drawn along that view), so one height for all never
//     shows. From ours the tiles of the other level are a sheet at the hero's level, pushed aside by 1.22 x the difference, and when the
//     floor under the hero changes (a step of the stairs) the whole sheet jumps.
//
// What is done (visual only - the game's world is a 2D grid; nothing of the paths, the fight or the server uses these heights): right
// after the game's origin call, every bin gets the height of its own tile's floor, asked the way the game asks for the hero's at five cells
// of the tile (its middle and four inset points), then stitched:
//   - samples of one floor (within kSame) are one level; a tile whose samples agree, or most of them, takes that level;
//   - a tile across a ledge (samples on two levels, no majority) takes the level its neighbours stand on - it continues the ground round it
//     instead of leaving a step at its own seam; a tie: the level with more samples, then the lower one (under a cliff's top, not out in the
//     air past its face);
//   - a lone tile on a level none of its four agreeing neighbours has (the query met something that is not ground) takes theirs;
//   - a tile not asked yet, or whose rays met nothing (its room not loaded), takes its neighbours' most common height, and with none
//     known, the game's own (the hero's floor);
//   - heights are rounded to 1/32 of a unit, so the tiles of one floor have the very same height - no hairline seams between them.
// A real drop - a cliff, a ledge, the stairs' run - stays: tiles on each side keep their own floor. The stamps and decals of a tile go with
// its height as the game draws them.
//
// The ray: 0x6B0350 itself cannot be used - with nothing hit it answers the cell at height 0, the same as a floor at 0, and the ground of
// most levels is at 0. Its ray is cast as it casts it (RayInput: the same fields, the same filter, the same world), through the physics
// manager's RayCastClosest (0x7E7DD0 (manager 0x7E65A0(), out, input, world 0) -> hit), which says whether it hit; its point is the out's
// first three floats (0x6B0497).
//
// Cost: one ray of the floor world per sample. Answers are kept per tile (by its place on the grid); each tick asks at most kBudget
// tiles - the new ones first (a window that moved), then the oldest again - so a window of 324 tiles is all asked in ~14 ticks and kept
// fresh after (rooms loaded later, room heights moved). Kept answers are forgotten when the window jumps (a waypoint, another area) or a
// game is left.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <unordered_map>

namespace terrainheights {

constexpr int kBins = 18;                       // the window: 18 x 18 tiles
constexpr int kTiles = kBins * kBins;           // 324 (the game's own loops: cmp 0x144)
constexpr size_t kBinStride = 0x20;             // a bin; its height at +0x18, its tile's data at +0x10
constexpr ptrdiff_t kBinDataFromHeight = -8;    // the tile's data pointer, from the height field
constexpr float kCellsPerTile = 5.0f;           // 10 world units = 5 cells (0x1CBFD88 10.0, 0x1CBD0D0 5.0)
constexpr float kShear = 1.22474487139f;        // a floor y high is drawn at x, z + y * kShear (flat_ground.h)
constexpr int kSamples = 5;
constexpr float kSame = 0.5f;                   // samples / heights this close are one floor (world units; the hero is ~7.5 tall)
constexpr float kQuant = 1.0f / 32.0f;          // heights rounded to this: one floor, one height
constexpr int kBudget = 24;                     // tiles asked per tick (5 rays each)

// The game's (3.3.93787 / D2RLoader 1.3.1; cleanroom/sigscan finds them in another build - tools/make_sigs.py TerrainTiles ..
// TerrainCorner). The test checks each against the image (terrain_heights_test <D2R_image.bin>).
constexpr uint64_t kRvaTerrainTiles = 0x6B54C0;    // LevelTranslationLayer::UpdateTerrainTiles (layer, span<ROOM*>, bool), hooked
constexpr uint64_t kRvaTerrainOrigin = 0xE79000;   // TerrainSystem's origin (const int2* corner tile, float height), hooked
constexpr uint64_t kRvaFloorUnder = 0x6B0350;      // the floor under a cell (layer, out, bcVec2 cell, unit, world): its ray modelled, not called
constexpr uint64_t kRvaPhysics = 0x7E65A0;         // the physics manager (its thread-safe static), called
constexpr uint64_t kRvaRayCast = 0x7E7DD0;         // D2PhysicsManager's closest ray (manager, out, input, world) -> hit, called
constexpr size_t kLayerFloorFilter = 0x1E60;       // the layer's floor filter, world 0 (0x6B040E: [layer + world * 8 + 0x1E60])
constexpr uint64_t kRvaBins = 0x34FCB28;           // bin 0's height (the bins from 0x34FCB10)
constexpr uint64_t kRvaCorner = 0x34FF608;         // the window's corner tile, int x, y
// mov r11, rsp; push rbp/rbx/rsi/rdi; lea rbp, [r11-428h]; sub rsp, 508h
inline constexpr uint8_t kSigTerrainTiles[21] = {0x4C,0x8B,0xDC,0x55,0x53,0x56,0x57,0x49,0x8D,0xAB,0xD8,0xFB,0xFF,0xFF,0x48,0x81,0xEC,0x08,0x05,0x00,0x00};
// mov [rsp+18h], rbx; push rbp/rsi/rdi; sub rsp, 50h (then a rip compare: not in the pattern)
inline constexpr uint8_t kSigTerrainOrigin[12] = {0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57,0x48,0x83,0xEC,0x50};
// push rbx; sub rsp, 20h; mov rax, gs:[58h] (the static's thread check)
inline constexpr uint8_t kSigPhysics[15] = {0x40,0x53,0x48,0x83,0xEC,0x20,0x65,0x48,0x8B,0x04,0x25,0x58,0x00,0x00,0x00};
// mov [rsp+8], rbx; mov [rsp+10h], rbp; mov [rsp+18h], rsi; mov [rsp+20h], rdi; push r14
inline constexpr uint8_t kSigRayCast[22] = {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7C,0x24,0x20,
                                            0x41,0x56};

// The ray's input as 0x6B0350 lays it out for cell (x, y) (0x6B03D7 .. 0x6B0473; the fields RayCastClosest reads, 0x98E989 .. 0x98E9B5):
//   +0x00 from = (2x, 0, 2y) + 1000 x the view's direction, w 1     +0x10 to = (2x, 0, 2y) - 1000 x it, w 1
//   +0x20 1.0   +0x28 0 (8)   +0x30 0 (4)   +0x40 (0, 0, 0, 1)   +0x50 0.05   +0x54 0.0164042 (1 / 60.96)   +0x58 the floor filter
//   +0x60 0 (8)   +0x70 0 (2). The game's bytes between them are its stack as it was: never read.
constexpr size_t kRayInputSize = 0x80, kRayOutSize = 0x100;
constexpr float kRayDir[3] = {0.6123724579811096f, 0.4999999403953552f, 0.6123724579811096f};   // 0x1D4D3B0
constexpr float kRayLength = 1000.0f;                                                          // 0x1D51470
inline void RayInput(float x, float y, uint64_t filter, uint8_t in[kRayInputSize]) {
    for (size_t i = 0; i < kRayInputSize; ++i) in[i] = 0;
    const float c[3] = {2.0f * x, 0.0f, 2.0f * y};
    float from[4], to[4];
    for (int i = 0; i < 3; ++i) {
        const float d = kRayLength * kRayDir[i];
        from[i] = c[i] + d;
        to[i] = c[i] - d;
    }
    from[3] = to[3] = 1.0f;
    const float one = 1.0f, radius = 0.05f, scale = 0.016404198482632637f, unit[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    auto put = [&](size_t at, const void* v, size_t n) {
        const uint8_t* b = static_cast<const uint8_t*>(v);
        for (size_t i = 0; i < n; ++i) in[at + i] = b[i];
    };
    put(0x00, from, sizeof from);
    put(0x10, to, sizeof to);
    put(0x20, &one, 4);
    put(0x40, unit, sizeof unit);
    put(0x50, &radius, 4);
    put(0x54, &scale, 4);
    put(0x58, &filter, 8);
}

inline float Quant(float h) { return std::round(h / kQuant) * kQuant; }

// The cells a tile is asked at (cell coordinates, as the query takes them): its middle first, then four points 1 cell in from its corners.
inline void SampleCell(int tx, int ty, int k, float* x, float* y) {
    static constexpr float kAt[kSamples][2] = {{2.5f, 2.5f}, {1.0f, 1.0f}, {4.0f, 1.0f}, {1.0f, 4.0f}, {4.0f, 4.0f}};
    *x = float(tx) * kCellsPerTile + kAt[k][0];
    *y = float(ty) * kCellsPerTile + kAt[k][1];
}

// A hit's point for cell (x, y): the floor's height, or false when it is not one. The ray runs along the game's view through (2x, 0, 2y),
// so a floor h high is met at (2x + 1.22 h, h, 2y + 1.22 h); a point off that line is not this ray's answer (an out laid out otherwise).
inline bool Hit(const float out[3], float x, float y, float* h) {
    if (!std::isfinite(out[0]) || !std::isfinite(out[1]) || !std::isfinite(out[2])) return false;
    if (std::fabs(out[0] - 2.0f * x - kShear * out[1]) > 0.1f || std::fabs(out[2] - 2.0f * y - kShear * out[1]) > 0.1f) return false;
    if (std::fabs(out[1]) > 10000.0f) return false;
    *h = out[1];
    return true;
}

// What a tile's samples said: the heights of those that met a floor.
struct Tile {
    uint8_t asked = 0;       // samples asked (0: not asked yet)
    uint8_t n = 0;           // of them, met a floor
    float h[kSamples] = {};
};

// A tile's levels: its samples grouped into floors (within kSame of the group's first), the biggest first.
struct Levels {
    int n = 0;
    float h[kSamples] = {};
    int count[kSamples] = {};
};
inline Levels Cluster(const Tile& t) {
    Levels L;
    for (int i = 0; i < t.n; ++i) {
        int j = 0;
        while (j < L.n && std::fabs(L.h[j] - t.h[i]) >= kSame) ++j;
        if (j == L.n) { L.h[L.n] = t.h[i]; L.count[L.n] = 0; ++L.n; }
        ++L.count[j];
    }
    for (int a = 0; a < L.n; ++a)   // biggest first; equal: the lower first
        for (int b = a + 1; b < L.n; ++b)
            if (L.count[b] > L.count[a] || (L.count[b] == L.count[a] && L.h[b] < L.h[a])) {
                const float h = L.h[a]; L.h[a] = L.h[b]; L.h[b] = h;
                const int c = L.count[a]; L.count[a] = L.count[b]; L.count[b] = c;
            }
    return L;
}

struct Stats {
    int known = 0;      // tiles with a floor met
    int mixed = 0;      // across a ledge: took the neighbours' level
    int lone = 0;       // a lone height among agreeing neighbours: took theirs
    int filled = 0;     // not known: took the neighbours' height
    int hero = 0;       // not known, no neighbour known: the hero's floor
    int levels = 0;     // distinct heights in the window
    float low = 0.0f, high = 0.0f;
};

// The window's heights. tiles: kTiles of them, index (ty - corner y) * kBins + (tx - corner x); hero: the game's height (the floor under
// the hero). out: kTiles heights.
inline Stats Stitch(const Tile* tiles, float hero, float* out) {
    Stats s;
    bool set[kTiles] = {};
    Levels lv[kTiles];
    auto at = [](int i, int d) {   // the neighbour of tile i on side d (W N E S), -1 at the window's edge
        const int x = i % kBins, y = i / kBins;
        switch (d) {
            case 0: return x > 0 ? i - 1 : -1;
            case 1: return y > 0 ? i - kBins : -1;
            case 2: return x < kBins - 1 ? i + 1 : -1;
            default: return y < kBins - 1 ? i + kBins : -1;
        }
    };
    // 1. the tiles whose samples agree, or most of them
    for (int i = 0; i < kTiles; ++i) {
        lv[i] = Cluster(tiles[i]);
        if (lv[i].n == 0) continue;
        ++s.known;
        if (lv[i].n == 1 || 2 * lv[i].count[0] > tiles[i].n) { out[i] = Quant(lv[i].h[0]); set[i] = true; }
    }
    // 2. across a ledge: the level its set neighbours stand on (ties: more samples, then lower - Cluster's order)
    bool ledge[kTiles] = {};
    for (int i = 0; i < kTiles; ++i) {
        if (set[i] || lv[i].n == 0) continue;
        int best = 0, bestScore = -1;
        for (int j = 0; j < lv[i].n; ++j) {
            int score = 0;
            for (int d = 0; d < 4; ++d) {
                const int n = at(i, d);
                score += n >= 0 && set[n] && std::fabs(out[n] - lv[i].h[j]) < kSame;
            }
            if (score > bestScore) { best = j; bestScore = score; }
        }
        out[i] = Quant(lv[i].h[best]);
        ledge[i] = true;
        ++s.mixed;
    }
    for (int i = 0; i < kTiles; ++i) set[i] = set[i] || ledge[i];
    // 3. a lone height: all four neighbours set, agreeing with one another, and this tile on none of their level
    float fix[kTiles];
    bool fixed[kTiles] = {};
    for (int i = 0; i < kTiles; ++i) {
        if (!set[i]) continue;
        int n[4], c = 0;
        for (int d = 0; d < 4; ++d) { n[d] = at(i, d); c += n[d] >= 0 && set[n[d]]; }
        if (c < 4) continue;
        bool agree = true;
        for (int d = 1; d < 4; ++d) agree = agree && std::fabs(out[n[d]] - out[n[0]]) < kSame;
        if (agree && std::fabs(out[i] - out[n[0]]) >= kSame) { fix[i] = out[n[0]]; fixed[i] = true; }
    }
    for (int i = 0; i < kTiles; ++i)
        if (fixed[i]) { out[i] = fix[i]; ++s.lone; }
    // 4. not known: the most common height of the set neighbours (ties: the nearest the hero's floor), spreading in from the known ones
    for (int pass = 0; pass < 2 * kBins; ++pass) {
        bool more = false;
        bool now[kTiles] = {};
        for (int i = 0; i < kTiles; ++i) {
            if (set[i]) continue;
            float h[4];
            int c[4] = {}, m = 0;
            for (int d = 0; d < 4; ++d) {
                const int n = at(i, d);
                if (n < 0 || !set[n]) continue;
                int j = 0;
                while (j < m && std::fabs(h[j] - out[n]) >= kSame) ++j;
                if (j == m) h[m++] = out[n];
                ++c[j];
            }
            if (!m) { more = true; continue; }
            int b = 0;
            for (int j = 1; j < m; ++j)
                if (c[j] > c[b] || (c[j] == c[b] && std::fabs(h[j] - hero) < std::fabs(h[b] - hero))) b = j;
            out[i] = h[b];
            now[i] = true;
            ++s.filled;
        }
        for (int i = 0; i < kTiles; ++i) set[i] = set[i] || now[i];
        if (!more) break;
    }
    for (int i = 0; i < kTiles; ++i)
        if (!set[i]) { out[i] = Quant(hero); ++s.hero; }
    // the window's levels, for the log
    float seen[kTiles];
    for (int i = 0; i < kTiles; ++i) {
        int j = 0;
        while (j < s.levels && std::fabs(seen[j] - out[i]) >= kSame) ++j;
        if (j == s.levels) seen[s.levels++] = out[i];
        if (i == 0 || out[i] < s.low) s.low = out[i];
        if (i == 0 || out[i] > s.high) s.high = out[i];
    }
    return s;
}

// The answers kept per tile, by its place on the grid, and which tiles to ask this tick.
class Keeper {
public:
    struct Entry { Tile t; uint32_t at = 0; };
    // A new window corner: a jump (more than kJump tiles) forgets everything - another area, a waypoint.
    static constexpr int kJump = 4;
    void Corner(int x, int y) {
        if (has_ && (std::abs(x - cx_) > kJump || std::abs(y - cy_) > kJump)) Clear();
        if (has_ && (x != cx_ || y != cy_)) Prune(x, y);
        cx_ = x; cy_ = y; has_ = true;
    }
    // Up to `budget` tiles of the window to ask now, as window indices: never asked first (nearest the middle first), then the oldest.
    int Pick(uint32_t tick, int budget, int* idx) const {
        int64_t key[kTiles];
        int cand[kTiles], m = 0;
        for (int i = 0; i < kTiles; ++i) {
            const int x = i % kBins, y = i / kBins;
            const Entry* e = Find(cx_ + x, cy_ + y);
            if (!e) {   // never asked: by its ring round the middle (0 the middle four)
                const int dx = std::abs(2 * x - (kBins - 1)), dy = std::abs(2 * y - (kBins - 1));
                key[m] = (dx > dy ? dx : dy) / 2;
            } else {    // asked: the oldest first; asked this tick: not again
                const uint32_t a = tick - e->at;
                if (a == 0) continue;
                key[m] = 1000 + int64_t(0xFFFFFFFFu - a);
            }
            cand[m++] = i;
        }
        int n = 0;
        for (; n < budget && n < m; ++n) {   // the `budget` smallest keys
            int b = n;
            for (int j = n + 1; j < m; ++j)
                if (key[j] < key[b]) b = j;
            const int64_t k = key[n]; key[n] = key[b]; key[b] = k;
            const int c = cand[n]; cand[n] = cand[b]; cand[b] = c;
            idx[n] = cand[n];
        }
        return n;
    }
    void Put(int i, const Tile& t, uint32_t tick) { map_[Key(cx_ + i % kBins, cy_ + i / kBins)] = {t, tick}; }
    // The window's tiles as kept (not asked: asked = 0).
    void Window(Tile* out) const {
        for (int i = 0; i < kTiles; ++i) {
            const Entry* e = Find(cx_ + i % kBins, cy_ + i / kBins);
            out[i] = e ? e->t : Tile{};
        }
    }
    void Clear() { map_.clear(); has_ = false; }
    size_t Size() const { return map_.size(); }

private:
    static int64_t Key(int x, int y) { return (int64_t(uint32_t(x)) << 32) | uint32_t(y); }
    const Entry* Find(int x, int y) const {
        const auto it = map_.find(Key(x, y));
        return it == map_.end() ? nullptr : &it->second;
    }
    // Tiles well out of the window are forgotten (they are asked again when it comes back: rooms may have moved).
    void Prune(int x, int y) {
        for (auto it = map_.begin(); it != map_.end();) {
            const int tx = int(int32_t(uint32_t(uint64_t(it->first) >> 32))), ty = int(int32_t(uint32_t(it->first)));
            if (tx < x - kBins || tx >= x + 2 * kBins || ty < y - kBins || ty >= y + 2 * kBins) it = map_.erase(it);
            else ++it;
        }
    }
    std::unordered_map<int64_t, Entry> map_;
    int cx_ = 0, cy_ = 0;
    bool has_ = false;
};

}   // namespace terrainheights
