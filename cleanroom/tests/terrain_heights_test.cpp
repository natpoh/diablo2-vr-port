// [render] terrain_heights (vr/terrain_heights.h): each ground tile of the game's terrain at its own floor's height, stitched to its
// neighbours. Made-up windows of 18 x 18 tiles with the game's kinds of ground - two levels with a ledge (the act 2 desert's plateau), a tile
// across the ledge, stairs, a roof the floor query meets, tiles not asked yet or whose rooms are not loaded - and what the game draws (one
// height for all: the hero's floor) against what is drawn now: no step between tiles of one floor, the real drops kept. Then the keeper
// (which tiles are asked each tick) and, given the game's image, the addresses and numbers the plugin relies on.
//   terrain_heights_test [D2R_image.bin] [-v]
#include "terrain_heights.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <vector>

static int g_fail = 0, g_checks = 0;
static bool g_verbose = false;
static void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; }
}

using namespace terrainheights;

// A window whose floor under cell (x, y) is floor(x, y) (NAN: nothing there - its room not loaded); every tile asked at its five cells.
static void Ask(std::vector<Tile>& tiles, int cx, int cy, const std::function<float(float, float)>& floor) {
    tiles.assign(kTiles, Tile{});
    for (int i = 0; i < kTiles; ++i) {
        Tile& t = tiles[i];
        t.asked = kSamples;
        for (int k = 0; k < kSamples; ++k) {
            float x, y;
            SampleCell(cx + i % kBins, cy + i / kBins, k, &x, &y);
            const float f = floor(x, y);
            if (std::isnan(f)) continue;   // the ray hit nothing
            // the game's ray meets the floor on its line through the cell
            const float out[3] = {2.0f * x + kShear * f, f, 2.0f * y + kShear * f};
            float h;
            if (Hit(out, x, y, &h)) t.h[t.n++] = h;
        }
    }
}

// Steps between neighbouring tiles of the window (|difference| >= kSame), counted once a pair.
static int Steps(const float* h, const std::function<bool(int, int)>& count = nullptr) {
    int n = 0;
    for (int i = 0; i < kTiles; ++i) {
        const int x = i % kBins, y = i / kBins;
        if (x + 1 < kBins && std::fabs(h[i] - h[i + 1]) >= kSame && (!count || count(i, i + 1))) ++n;
        if (y + 1 < kBins && std::fabs(h[i] - h[i + kBins]) >= kSame && (!count || count(i, i + kBins))) ++n;
    }
    return n;
}
// Hairline seams: neighbours of one floor (within kSame) whose heights are not the very same.
static int Hairlines(const float* h) {
    int n = 0;
    for (int i = 0; i < kTiles; ++i) {
        const int x = i % kBins, y = i / kBins;
        if (x + 1 < kBins && std::fabs(h[i] - h[i + 1]) < kSame && h[i] != h[i + 1]) ++n;
        if (y + 1 < kBins && std::fabs(h[i] - h[i + kBins]) < kSame && h[i] != h[i + kBins]) ++n;
    }
    return n;
}

static void Show(const char* title, const float* h) {
    if (!g_verbose) return;
    std::printf("%s\n", title);
    for (int y = 0; y < kBins; ++y) {
        for (int x = 0; x < kBins; ++x) std::printf("%6.2f", h[y * kBins + x]);
        std::printf("\n");
    }
}

static void TestHit() {
    float h = -1.0f;
    const float x = 101.5f, y = 47.0f;
    const float zero[3] = {2 * x, 0.0f, 2 * y};
    Check(Hit(zero, x, y, &h) && h == 0.0f, "a floor at 0 is a floor (0x6B0350 itself would answer the same for nothing hit)");
    const float up[3] = {2 * x + kShear * 9.0f, 9.0f, 2 * y + kShear * 9.0f};
    Check(Hit(up, x, y, &h) && h == 9.0f, "a floor 9 up on the game's line through the cell is read as 9");
    const float down[3] = {2 * x - kShear * 4.5f, -4.5f, 2 * y - kShear * 4.5f};
    Check(Hit(down, x, y, &h) && h == -4.5f, "a floor below 0 is read too");
    const float off[3] = {2 * x + 3.0f, 9.0f, 2 * y + kShear * 9.0f};
    Check(!Hit(off, x, y, &h), "a point off the game's line is not this cell's answer");
    const float bad[3] = {NAN, 1.0f, 2.0f};
    Check(!Hit(bad, x, y, &h), "not finite: nothing");
}

// The act 2 desert: ground at 0, a plateau 9 up east of a ledge that runs through tile column 9 (cells x >= 147: not on a tile's edge;
// three of the column's five samples are up); the hero below.
static void TestPlateau() {
    const int cx = 20, cy = 30;   // the window's corner tile: cells 100.. 190 across, 150 .. 240 down
    auto floor = [&](float x, float) { return x >= 100.0f + 9 * 5 + 2.0f ? 9.0f : 0.0f; };   // cells 147 on: tile column 9's east part
    std::vector<Tile> tiles;
    Ask(tiles, cx, cy, floor);
    float out[kTiles], game[kTiles];
    const float hero = 0.0f;
    const Stats s = Stitch(tiles.data(), hero, out);
    for (float& g : game) g = hero;   // the game: one height, the hero's floor
    Show("plateau, stitched:", out);
    // the tiles wholly on one level stand at it
    int wrongLow = 0, wrongHigh = 0, gameWrong = 0;
    for (int i = 0; i < kTiles; ++i) {
        const int x = i % kBins;
        if (x <= 8) { wrongLow += out[i] != 0.0f; gameWrong += game[i] != 0.0f; }
        if (x >= 10) { wrongHigh += out[i] != 9.0f; gameWrong += game[i] != 9.0f; }
    }
    Check(wrongLow == 0, "plateau: every tile of the lower ground at 0");
    Check(wrongHigh == 0, "plateau: every tile of the plateau at 9 (the game: all at the hero's 0)");
    Check(gameWrong == 8 * kBins, "plateau: the game draws the 8 columns of the plateau at the hero's floor");
    // the tile column across the ledge: samples 1 (middle) on.. the column is 2.5 cells of low ground then the ledge
    int across = 0;
    for (int y = 0; y < kBins; ++y) across += out[y * kBins + 9] == 0.0f || out[y * kBins + 9] == 9.0f;
    Check(across == kBins, "plateau: the tiles across the ledge take one of the two levels, nothing in between");
    // one floor, one height; the real drop kept along the ledge only
    Check(Hairlines(out) == 0, "plateau: no hairline seam between tiles of one floor");
    const int steps = Steps(out);
    Check(steps == kBins, "plateau: one step per row, at the ledge - the real drop kept");
    Check(s.levels == 2 && s.low == 0.0f && s.high == 9.0f, "plateau: two heights in the window, 0 and 9");
    // the hero walks up the stairs: the game's sheet jumps to 9 everywhere; ours does not move
    float up[kTiles];
    Stitch(tiles.data(), 9.0f, up);
    Check(std::memcmp(up, out, sizeof up) == 0, "plateau: the hero's floor changing (a step up the stairs) moves no tile");
}

// A tile across the ledge with no majority (2 samples up, 2 down, its middle in a hole): it takes the level its neighbours stand on.
static void TestAcrossLedge() {
    std::vector<Tile> tiles(kTiles);
    for (int i = 0; i < kTiles; ++i) {
        const int x = i % kBins, y = i / kBins;
        Tile& t = tiles[i];
        t.asked = kSamples;
        const float h = x >= 9 ? 9.0f : 0.0f;
        for (int k = 0; k < kSamples; ++k) t.h[t.n++] = h;
        (void)y;
    }
    // tile (9, 5): two samples at 0, two at 9, the middle met nothing; its neighbours: west 0, north 9, east 9, south 9
    Tile& m = tiles[5 * kBins + 9];
    m = Tile{};
    m.asked = kSamples;
    m.h[m.n++] = 0.0f; m.h[m.n++] = 9.0f; m.h[m.n++] = 0.0f; m.h[m.n++] = 9.0f;
    float out[kTiles];
    Stats s = Stitch(tiles.data(), 0.0f, out);
    Check(out[5 * kBins + 9] == 9.0f && s.mixed == 1, "across the ledge: the level three of its neighbours stand on (9)");
    // the same tile at the plateau's west edge, its neighbours west 0, north 0, south 0, east 9: the lower ground's
    for (int y = 0; y < kBins; ++y)
        for (int x = 0; x < kBins; ++x) {
            Tile& t = tiles[y * kBins + x];
            if (y * kBins + x == 5 * kBins + 9) continue;
            for (int k = 0; k < t.n; ++k) t.h[k] = x >= 10 ? 9.0f : 0.0f;
        }
    s = Stitch(tiles.data(), 9.0f, out);
    Check(out[5 * kBins + 9] == 0.0f, "across the ledge: the level its neighbours stand on (0, three of them), whatever the hero's floor");
    // a tie between the levels (2 and 2 neighbours): the lower one (under the cliff's top, not out past its face)
    Tile iso = m;
    std::vector<Tile> tie(kTiles);
    for (int i = 0; i < kTiles; ++i) {
        const int x = i % kBins, y = i / kBins;
        tie[i].asked = kSamples;
        const float h = (y < 5 || (y == 5 && x < 9)) ? 0.0f : 9.0f;
        for (int k = 0; k < kSamples; ++k) tie[i].h[tie[i].n++] = h;
    }
    tie[5 * kBins + 9] = iso;   // west 0, north 0, east 9, south 9
    s = Stitch(tie.data(), 9.0f, out);
    Check(out[5 * kBins + 9] == 0.0f, "across the ledge, neighbours 2 and 2: the lower level");
    // most samples on one level: that level, whatever the neighbours
    Tile most{};
    most.asked = kSamples;
    for (float h : {9.0f, 9.0f, 9.0f, 0.0f, 0.0f}) most.h[most.n++] = h;
    tiles.assign(kTiles, Tile{});
    for (Tile& t : tiles) { t.asked = kSamples; for (int k = 0; k < kSamples; ++k) t.h[t.n++] = 0.0f; }
    tiles[100] = most;
    s = Stitch(tiles.data(), 0.0f, out);
    Check(out[100] == 0.0f && s.lone == 1, "three samples of five on the plateau, all four neighbours on the ground: a lone height, the ground's");
    tiles[101] = most;
    tiles[100 + kBins] = most;
    s = Stitch(tiles.data(), 0.0f, out);
    Check(out[100] == 9.0f && out[101] == 9.0f && out[100 + kBins] == 9.0f, "the same with plateau neighbours: the majority, 9");
}

// A roof the query met over a tile of flat ground (a lone height), and a big flat roof (several tiles: kept - a known limit).
static void TestLone() {
    std::vector<Tile> tiles;
    Ask(tiles, 0, 0, [](float x, float y) { return (x > 45 && x < 50 && y > 45 && y < 50) ? 25.0f : 3.0f; });
    float out[kTiles];
    const Stats s = Stitch(tiles.data(), 3.0f, out);
    int off = 0;
    for (float h : out) off += h != 3.0f;
    Check(off == 0 && s.lone == 1, "a lone tile 22 up among ground at 3: drawn on the ground");
}

// Tiles not asked yet, or whose query met nothing (their room not loaded): their neighbours' height; with nothing known, the hero's floor.
static void TestUnknown() {
    std::vector<Tile> tiles;
    Ask(tiles, 0, 0, [](float x, float) { return x < 40 ? 4.0f : x < 60 ? NAN : -9.0f; });
    float out[kTiles];
    Stats s = Stitch(tiles.data(), 4.0f, out);
    Show("a hole between ground at 4 and -9:", out);
    int bad = 0;
    for (int i = 0; i < kTiles; ++i) bad += !(out[i] == 4.0f || out[i] == -9.0f);
    Check(bad == 0 && s.filled > 0, "tiles whose floor is not known take a neighbour's height, never something in between");
    Check(out[0] == 4.0f && out[kBins - 1] == -9.0f, "the known tiles keep theirs");
    tiles.assign(kTiles, Tile{});
    s = Stitch(tiles.data(), 6.5f, out);
    int hero = 0;
    for (float h : out) hero += h == 6.5f;
    Check(hero == kTiles && s.hero == kTiles, "nothing known yet: every tile at the hero's floor (the game's own)");
    // ground at exactly 0 (most levels' base): known, whatever the hero stands on
    Ask(tiles, 0, 0, [](float, float) { return 0.0f; });
    s = Stitch(tiles.data(), 9.0f, out);
    int zero = 0;
    for (float h : out) zero += h == 0.0f;
    Check(zero == kTiles && s.known == kTiles, "ground at exactly 0, the hero up on 9: every tile at 0");
}

// Floor heights with float noise (a floor at 9 met at 9.0004 here, 8.9997 there): one height for the floor, no hairline seams.
static void TestNoise() {
    std::vector<Tile> tiles;
    unsigned seed = 12345;
    Ask(tiles, 3, 7, [&](float, float) {
        seed = seed * 1103515245u + 12345u;
        return 9.0f + (float((seed >> 16) % 1000) - 500.0f) * 1e-6f;
    });
    float out[kTiles];
    Stitch(tiles.data(), 0.0f, out);
    Check(Hairlines(out) == 0 && out[0] == 9.0f, "float noise of one floor: the very same height for all its tiles (9)");
}

// Stairs: a run of steps 1.5 up each tile row - real drops between the rows, kept.
static void TestStairs() {
    std::vector<Tile> tiles;
    Ask(tiles, 0, 0, [](float, float y) { return std::floor(y / 5.0f) * 1.5f; });
    float out[kTiles];
    Stitch(tiles.data(), 0.0f, out);
    int bad = 0;
    for (int i = 0; i < kTiles; ++i) bad += out[i] != (i / kBins) * 1.5f;
    Check(bad == 0, "stairs: each row of tiles at its own step");
    Check(Steps(out) == (kBins - 1) * kBins, "stairs: a step between every two rows, none along them");
}

static void TestKeeper() {
    Keeper k;
    k.Corner(10, 10);
    int idx[kBudget];
    int n = k.Pick(1, kBudget, idx);
    Check(n == kBudget, "the keeper: a full budget of tiles to ask");
    bool middle = true;
    for (int j = 0; j < 4; ++j) {
        const int x = idx[j] % kBins, y = idx[j] / kBins;
        middle = middle && (x == 8 || x == 9) && (y == 8 || y == 9);
    }
    Check(middle, "the keeper: the four tiles round the middle (the hero) first");
    int ticks = 1;
    int last[kBudget], lastN = 0;
    for (;;) {
        for (int j = 0; j < n; ++j) { Tile t; t.asked = kSamples; k.Put(idx[j], t, ticks); last[j] = idx[j]; }
        lastN = n;
        Tile win[kTiles];
        k.Window(win);
        int asked = 0;
        for (const Tile& t : win) asked += t.asked > 0;
        if (asked == kTiles) break;
        n = k.Pick(++ticks, kBudget, idx);
        if (ticks > 100) break;
    }
    Check(ticks == (kTiles + kBudget - 1) / kBudget, "the keeper: the whole window asked in ceil(324 / budget) ticks");
    n = k.Pick(ticks, kBudget, idx);
    bool again = false;
    for (int j = 0; j < n; ++j)
        for (int q = 0; q < lastN; ++q) again = again || idx[j] == last[q];
    Check(n == kBudget && !again, "the keeper: tiles asked this tick are not asked again in it");
    n = k.Pick(ticks + 1, kBudget, idx);
    Check(n == kBudget, "the keeper: next tick, the oldest asked again");
    // the window moves one tile east: one new column, asked first
    k.Corner(11, 10);
    n = k.Pick(ticks + 2, kBudget, idx);
    int newCol = 0;
    for (int j = 0; j < kBins && j < n; ++j) newCol += idx[j] % kBins == kBins - 1;
    Check(newCol == kBins, "the keeper: a window one tile on - its new column asked first");
    const size_t kept = k.Size();
    Check(kept >= size_t(kTiles - kBins), "the keeper: a step keeps what was asked");
    k.Corner(40, 10);
    Check(k.Size() == 0, "the keeper: a jump (another area, a waypoint) forgets everything");
}

// The game's image: the plugin's addresses and numbers as the game has them.
static bool ReadFile(const char* path, std::vector<uint8_t>* out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out->assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return out->size() > 0x3500000;
}
static void TestImage(const char* path) {
    std::vector<uint8_t> img;
    if (!ReadFile(path, &img)) { std::printf("FAIL: cannot read the image %s\n", path); ++g_fail; return; }
    auto bytes = [&](uint64_t rva, std::initializer_list<int> b) {
        size_t i = 0;
        for (int v : b) if (img[rva + i++] != uint8_t(v)) return false;
        return true;
    };
    auto same = [&](uint64_t rva, const uint8_t* b, size_t n) { return std::memcmp(&img[rva], b, n) == 0; };
    auto rip = [&](uint64_t insn, size_t dispAt, size_t len) {
        int32_t d;
        std::memcpy(&d, &img[insn + dispAt], 4);
        return uint64_t(int64_t(insn) + int64_t(len) + d);
    };
    auto call = [&](uint64_t insn) { return img[insn] == 0xE8 ? rip(insn, 1, 5) : 0; };
    auto f32 = [&](uint64_t rva) { float v; std::memcpy(&v, &img[rva], 4); return v; };
    Check(same(kRvaTerrainTiles, kSigTerrainTiles, sizeof kSigTerrainTiles), "image: UpdateTerrainTiles' first bytes (the hook's)");
    Check(same(kRvaTerrainOrigin, kSigTerrainOrigin, sizeof kSigTerrainOrigin), "image: TerrainSystem origin's first bytes (the hook's)");
    Check(same(kRvaPhysics, kSigPhysics, sizeof kSigPhysics), "image: the physics manager getter's first bytes");
    Check(same(kRvaRayCast, kSigRayCast, sizeof kSigRayCast), "image: RayCastClosest's first bytes");
    // UpdateTerrainTiles: the floor under the hero (0x6B0350), its height [rax+4], into the origin call
    Check(call(0x6B5666) == kRvaFloorUnder, "image: UpdateTerrainTiles asks the floor under the hero (call 0x6B0350)");
    Check(bytes(0x6B5672, {0xF3, 0x0F, 0x10, 0x70, 0x04}), "image: ... and takes its height (movss xmm6, [rax+4])");
    Check(call(0x6B5888) == kRvaTerrainOrigin, "image: ... and hands it to the origin (call 0xE79000)");
    Check(call(0x6B4076) == kRvaTerrainTiles, "image: LevelTranslationLayer::SimulationTick calls UpdateTerrainTiles");
    // the origin: one height into every bin, 0x20 apart, 0x34FCB20 .. 0x34FF390 (324 bins), then the corner (tile - 9) at 0x34FF608
    Check(rip(0xE7903F, 3, 7) == kRvaBins - 8 && rip(0xE79046, 3, 7) == kRvaBins - 8 + kTiles * kBinStride - 0x10,
          "image: the origin's loop runs over 324 bins of 0x20 from the tile pointers at 0x34FCB20");
    Check(bytes(0xE7907E, {0xF3, 0x0F, 0x11, 0x73, 0x08}) && bytes(0xE79083, {0x48, 0x83, 0xC3, 0x20}),
          "image: ... writing the one height at +8 of the pointer (movss [rbx+8], xmm6; add rbx, 20h)");
    Check(bytes(0xE79095, {0x83, 0xC1, 0xF7}) && bytes(0xE79098, {0x83, 0xC2, 0xF7}), "image: the corner is the tile less 9 (add -9)");
    Check(rip(0xE790A6, 2, 6) == kRvaCorner && rip(0xE790E8, 3, 7) == kRvaCorner, "image: the corner tile kept at 0x34FF608");
    Check(rip(0xE790F4, 4, 8) == 0x34FF604, "image: the one height also at 0x34FF604 (only its debug panel reads it)");
    // the tile's drawing and culling read their bin's own height: index (ty - corner y) * 18 + (tx - corner x), * 0x20
    Check(rip(0xE7760D, 2, 6) == kRvaCorner && rip(0xE77613, 2, 6) == kRvaCorner + 4, "image: the tile culling subtracts the corner");
    Check(bytes(0xE77619, {0x83, 0xF9, 0x11}) && bytes(0xE77623, {0x8D, 0x04, 0xC0, 0x8D, 0x04, 0x41}),
          "image: ... index = x + 18 y, in 0..17 each (cmp 11h; lea eax, [rax+rax*8]; lea eax, [rcx+rax*2])");
    Check(rip(0xE7762B, 3, 7) == kRvaBins && bytes(0xE77632, {0x48, 0xC1, 0xE0, 0x05, 0xF3, 0x0F, 0x10, 0x14, 0x08}),
          "image: ... its height = [0x34FCB28 + index * 0x20] (shl rax, 5; movss xmm2, [rax+rcx])");
    Check(call(0xE7763F) == 0xE769D0, "image: ... into the tile's matrix (0xE769D0)");
    // the tile's matrix: x, z = 10 * tile + 5 + 1.2247 h, y = h (h * -2 * -0.6124, - h * -2 * 0.5)
    Check(f32(0x1CBFD88) == 10.0f && f32(0x1CBD0D0) == 5.0f, "image: a tile is 10 world units, its middle 5 in");
    Check(f32(0x1CD8A6C) == -2.0f && std::fabs(-2.0f * f32(0x1DFF13C) - kShear) < 1e-6f && f32(0x1CBA900) == 0.5f,
          "image: a tile h high is drawn at x, z + 1.2247 h - the shear of flat_ground.h");
    // the floor query: a ray along the game's view, +-1000 x (0.612, 0.5, 0.612) through (2x, 0, 2y)
    Check(f32(0x1D513C0) == 2.0f && f32(0x1D513C0 + 4) == 0.0f && f32(0x1D513C0 + 8) == 2.0f, "image: the floor query's cell -> (2x, 0, 2y)");
    Check(std::fabs(f32(0x1D4D3B0) - 0.6123725f) < 1e-6f && std::fabs(f32(0x1D4D3B4) - 0.5f) < 1e-6f && f32(0x1D51470) == 1000.0f,
          "image: ... along (0.612, 0.5, 0.612) x 1000 - its hit is on the line the tile's shear draws");
    Check(bytes(0x6B040E, {0x48, 0x8B, 0x8C, 0xC1, 0x60, 0x1E, 0x00, 0x00}) && kLayerFloorFilter == 0x1E60,
          "image: ... with the layer's floor filter [layer + world * 8 + 0x1E60]");
    // its ray, as RayInput lays it out: the input at rbp-0x80 to RayCastClosest (manager 0x7E65A0(), out rbp+0, world 0)
    Check(call(0x6B0477) == kRvaPhysics && call(0x6B048A) == kRvaRayCast, "image: the floor query casts through 0x7E65A0() / 0x7E7DD0");
    Check(bytes(0x6B047C, {0x45, 0x33, 0xC9, 0x4C, 0x8D, 0x45, 0x80, 0x48, 0x8D, 0x55, 0x00}), "image: ... world 0, input rbp-0x80, out rbp+0");
    Check(bytes(0x6B048F, {0x44, 0x0F, 0xB6, 0xF0, 0x84, 0xC0}) && bytes(0x6B0497, {0x0F, 0x28, 0x45, 0x00}),
          "image: ... its bool is the hit, the out's first floats its point");
    Check(bytes(0x6B045B, {0xF3, 0x0F, 0x11, 0x5D, 0x80}) && bytes(0x6B0473, {0x0F, 0x29, 0x55, 0x90}), "image: input +0 from, +0x10 to");
    Check(bytes(0x6B0430, {0xF3, 0x0F, 0x11, 0x45, 0xA0}) && f32(rip(0x6B0428, 4, 8)) == 1.0f, "image: input +0x20 1.0");
    Check(bytes(0x6B043C, {0x0F, 0x29, 0x45, 0xC0}) && f32(rip(0x6B0435, 3, 7) + 12) == 1.0f && f32(rip(0x6B0435, 3, 7)) == 0.0f,
          "image: input +0x40 (0, 0, 0, 1)");
    Check(bytes(0x6B044B, {0xF3, 0x0F, 0x11, 0x45, 0xD0}) && f32(rip(0x6B0440, 4, 8)) == 0.05f, "image: input +0x50 0.05");
    Check(bytes(0x6B03C9, {0xF3, 0x0F, 0x11, 0x4D, 0xD4}) && f32(rip(0x6B03B9, 4, 8)) == 0.016404198482632637f, "image: input +0x54 0.0164");
    Check(bytes(0x6B041E, {0x48, 0x89, 0x4D, 0xD8}), "image: input +0x58 the filter");
    Check(f32(0x1D4D3B0) == kRayDir[0] && f32(0x1D4D3B4) == kRayDir[1] && f32(0x1D51470) == kRayLength, "image: the ray's direction and length");
    // RayInput: the same bytes where the game puts them
    uint8_t in[kRayInputSize];
    RayInput(10.0f, 20.0f, 0x0001000000000ABCull, in);
    float v[4];
    std::memcpy(v, in, 16);
    Check(v[0] == 20.0f + 1000.0f * kRayDir[0] && v[1] == 1000.0f * kRayDir[1] && v[2] == 40.0f + 1000.0f * kRayDir[2] && v[3] == 1.0f,
          "RayInput: from = (2x, 0, 2y) + 1000 x the direction, w 1");
    std::memcpy(v, in + 0x10, 16);
    Check(v[0] == 20.0f - 1000.0f * kRayDir[0] && v[1] == -1000.0f * kRayDir[1] && v[3] == 1.0f, "RayInput: to = (2x, 0, 2y) - 1000 x it");
    uint64_t f = 0;
    std::memcpy(&f, in + 0x58, 8);
    std::memcpy(v, in + 0x50, 8);
    Check(f == 0x0001000000000ABCull && v[0] == 0.05f && v[1] == 0.016404198482632637f, "RayInput: the filter, 0.05, 0.0164");
}

int main(int argc, char** argv) {
    const char* image = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "-v")) g_verbose = true;
        else image = argv[i];
    }
    TestHit();
    TestPlateau();
    TestAcrossLedge();
    TestLone();
    TestUnknown();
    TestNoise();
    TestStairs();
    TestKeeper();
    if (image) TestImage(image);
    else std::printf("(no image given: the game's addresses not checked)\n");
    std::printf("terrain_heights_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
