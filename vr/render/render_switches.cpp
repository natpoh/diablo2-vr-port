// vr/render/render_switches.cpp - the game's render switched as the ini says: room heights ([render] room_stitch), the draw distance
// ([render] rings, model_radius, big_radius), the memory pools, the background sleep, solid walls, the roofs kept, the props once a
// pair (cut from vr/vrcam.cpp, docs/plan_modules.md M13 - the render group). What it gives: vr/render/render_switches.h.
#include "../vrcam_headers.h"

using namespace D2RL;
using namespace vrc;   // as in vrcam.cpp

namespace vrc {

// LevelTranslationLayer::CalculateHeightOffset 0x6A2A40(layer, DRLG_ROOM*, const float edges[4]) -> the height a room is drawn at, stitched
// to ONE of the neighbours drawn at that moment. Hooked for [render] room_stitch (below). ([render] floors_stay,
// which kept a room's first height for the game, was here until 0.209.9: room_stitch fixes the same step near the hero - Serg 2026-11-09,
// "floors_stay отключи и удали".)
constexpr uint64_t RVA_ROOM_HEIGHT = 0x6A2A40;
// mov rax, rsp; push rbx; push rsi; push rdi; push r13; sub rsp, 58h; movaps [rax-48h], xmm7
const uint8_t kSigRoomHeight[16] = {0x48,0x8B,0xC4,0x53,0x56,0x57,0x41,0x55,0x48,0x83,0xEC,0x58,0x0F,0x29,0x78,0xB8};
using RoomHeightFn = float (*)(void* layer, const void* room, const float* edges);
RoomHeightFn OrigRoomHeight = nullptr;
std::atomic<int> g_roomHeightHook{0};   // 0 not in yet, 1 in, 2 not possible

// [render] room_stitch - from D2R VR Unleashed (bearstonem/d2r-pcvr d416861, by Cal; the pure part MPL 2.0 in
// vr/unleashed_room_stitch.h, this wiring ours). The game's pick of the built neighbour a room's height meets
// (the hero's room outright, else the largest candidate - arbitrary for rooms several rings out) becomes the built neighbour nearest the
// hero's room, so dungeon floors join along the way he comes instead of hanging over a void a step above or below the next room. Our copy
// of the game's pick is checked against the game's answer every time; where they differ the game's answer stands (told once). On by
// default since 0.209.9 (Serg: "оставляем код медведя включенным по умолчанию"). Every view - flat F1 - F3, VR F4 and the plain flat game
// with the game's own camera (Serg: "сшивка должна работать везде", "и на 2д режиме тоже"): the HD renderer builds the same rooms in all of
// them, and a room keeps the height it was built at whatever view comes next (from the game's own camera above a room's height only
// slides it along the line of sight, so nothing shows there). Legacy graphics (G) draws the old sprites, not these rooms. Visual and the
// HD client's floor only - the game's world, its paths, collision and the server are the 2D grid; units, missiles and items find their
// drawn ground with a ray along the same line.
namespace roomstitchwire {
namespace rs = unleashed::roomstitch;
std::atomic<bool> g_layoutOk{false};   // the table lookup 0x6B7450 is the one this reads (checked at the hook's install)
std::atomic<uint32_t> g_built{0}, g_moved{0}, g_disagree{0};
std::atomic<ULONGLONG> g_reportAt{0};
template <class T> bool Rd(uintptr_t a, T& out) { return a >= 0x10000 && SafeRead(&out, (const void*)a, sizeof(T)); }

// The client's local hero (the game's LocalIndex and ClientPlayer), 0 when not there. (Its own function: __try beside no C++ objects.)
uintptr_t HeroUnit() noexcept {
    const uintptr_t indexFn = d2rsig::Addr(RVA_SEWER_LOCAL_INDEX), playerFn = d2rsig::Addr(RVA_SEWER_CLIENT_PLAYER);
    if (!indexFn || !playerFn) return 0;
    __try {
        const int index = reinterpret_cast<int (*)()>(indexFn)();
        if (index < 0 || index >= 8) return 0;
        const uintptr_t unit = reinterpret_cast<uintptr_t (*)(int)>(playerFn)(index);
        if (unit < 0x10000 || *reinterpret_cast<const volatile uint32_t*>(unit) != 0) return 0;   // a player's unit has type 0
        return unit;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}
// The hero's DRLG room (unit +0x38 path, +0x20 its room, +0x18 the DRLG room) and its place on the grid.
bool HeroRoom(uintptr_t* drlg, int32_t* x, int32_t* y) {
    uintptr_t path = 0, room = 0;
    const uintptr_t hero = g_inWorld.load() ? HeroUnit() : 0;
    if (!hero || !Rd(hero + 0x38, path) || !Rd(path + 0x20, room) || !Rd(room + 0x18, *drlg) || *drlg < 0x10000 ||
        !Rd(*drlg + rs::PosX, *x) || !Rd(*drlg + rs::PosY, *y))
        return false;
    return true;
}

float Pick(void* layer, const void* room, const float* edges, float game) {
    if (!g_set.roomStitch.load(std::memory_order_relaxed) || !g_layoutOk.load(std::memory_order_relaxed) || !room || !edges)
        return game;
    rs::Candidate c[4];
    const auto read = [](uintptr_t a, auto& v) { return Rd(a, v); };
    if (!rs::Candidates((uintptr_t)layer, (uintptr_t)room, edges, read, c)) return game;
    uintptr_t hero = 0;
    int32_t hx = 0, hy = 0;
    const bool known = HeroRoom(&hero, &hx, &hy);
    if (!known) hero = 0;
    const int theirs = rs::GameChoice(c, hero);
    if (std::fabs(rs::HeightOf(c, theirs) - game) > 0.01f) {   // our copy of the game's pick is off (a stale hero room, another layout)
        if (g_disagree.fetch_add(1) == 0)
            LogF("vrcam: room stitch - the game picked height %.2f, our copy of its pick %.2f; the game's stands where they differ ([render] room_stitch)",
                 game, rs::HeightOf(c, theirs));
        return game;
    }
    const int ours = rs::Choice(c, hero, known, hx, hy);
    const float h = rs::HeightOf(c, ours);
    g_built.fetch_add(1, std::memory_order_relaxed);
    const ULONGLONG now = GetTickCount64();
    if (now - g_reportAt.load() >= 60000 && g_moved.load()) {
        g_reportAt.store(now);
        LogF("vrcam: room stitch - %u of %u rooms built met the way toward the hero instead of the game's pick ([render] room_stitch)", g_moved.load(),
             g_built.load());
    }
    if (ours == theirs || std::fabs(h - game) < 0.01f) return game;
    if (g_moved.fetch_add(1) == 0) {
        int32_t x = 0, y = 0;
        Rd((uintptr_t)room + rs::PosX, x);
        Rd((uintptr_t)room + rs::PosY, y);
        LogF("vrcam: room stitch - the first room moved: (%d, %d) at height %.2f from its %s neighbour (nearest the hero) instead of %.2f from its %s",
             x, y, h, rs::DirName(ours), game, rs::DirName(theirs));
    }
    return h;
}
}  // namespace roomstitchwire

// The game's room-height pick, hooked: the stitch's pick (every view).
float HookRoomHeight(void* layer, const void* room, const float* edges) {
    const float game = OrigRoomHeight ? OrigRoomHeight(layer, room, edges) : 0.0f;
    return roomstitchwire::Pick(layer, room, edges, game);
}

// Once [render] room_stitch asks for it; tried with the other hooks until the bytes are there.
void InstallRoomHeightHook() {
    static int misses = 0;
    if (!g_ctx || g_roomHeightHook.load() != 0 || !g_set.roomStitch.load()) return;
    if (!Matches(RVA_ROOM_HEIGHT, kSigRoomHeight, sizeof kSigRoomHeight)) {
        if (g_inWorld.load() && ++misses >= 20) {
            g_roomHeightHook.store(2);
            Log("vrcam: room heights: hook NOT possible (0x6A2A40 not as expected - another build?) - no room stitch, the game's own pick");
        }
        return;
    }
    roomstitchwire::g_layoutOk.store(Matches(unleashed::roomstitch::FindRva, unleashed::roomstitch::FindSig, sizeof unleashed::roomstitch::FindSig));
    const bool in = d2rsig::Hook(RVA_ROOM_HEIGHT, kSigRoomHeight, sizeof kSigRoomHeight, (void*)&HookRoomHeight, (void**)&OrigRoomHeight);
    g_roomHeightHook.store(in ? 1 : 2);
    Log(in ? "vrcam: room heights: hook in - [render] room_stitch (every view) meets the built neighbour nearest the hero's room"
           : "vrcam: room heights: hook FAILED - the game's own pick");
    if (in && !roomstitchwire::g_layoutOk.load())
        Log("vrcam: room stitch - the game's room table lookup 0x6B7450 not as expected (another build?): the game's own pick stays");
}

// [render] terrain_heights (vr/terrain_heights.h): the outdoor ground's tiles each at their own floor's height, stitched to the tiles round
// them. LevelTranslationLayer::UpdateTerrainTiles 0x6B54C0 (layer, span<ROOM*>, bool) asks the floor under the hero (0x6B0350) and hands it to
// TerrainSystem's origin 0xE79000 (const int2* corner tile, float height), which puts that one height in all 18 x 18 bins (0x34FCB10, the
// height at +0x18); every tile, its stamps and decals are drawn at their bin's height. Right after the origin call (still inside the game's
// UpdateTerrainTiles, before its culling job reads the bins) each bin gets its own tile's floor, by the ray the game casts for the hero's.
// Visual only, every view, live.
constexpr uint64_t RVA_TERRAIN_TILES = terrainheights::kRvaTerrainTiles, RVA_TERRAIN_ORIGIN = terrainheights::kRvaTerrainOrigin,
                   RVA_PHYSICS = terrainheights::kRvaPhysics, RVA_RAYCAST = terrainheights::kRvaRayCast;
constexpr uint64_t RVA_TERRAIN_BINS = terrainheights::kRvaBins, RVA_TERRAIN_CORNER = terrainheights::kRvaCorner;
using terrainheights::kSigTerrainTiles;
using terrainheights::kSigTerrainOrigin;
using terrainheights::kSigPhysics;
using terrainheights::kSigRayCast;
using TerrainTilesFn = void (*)(void* layer, void* rooms, uint8_t flag);
using TerrainOriginFn = void (*)(const int32_t* tile, float height);
using PhysicsFn = void* (*)();
using RayCastFn = bool (*)(void* mgr, void* out, const void* input, uint8_t world);
TerrainTilesFn OrigTerrainTiles = nullptr;
TerrainOriginFn OrigTerrainOrigin = nullptr;
std::atomic<int> g_terrainHook{0};   // 0 not in yet, 1 in, 2 not possible
std::atomic<bool> g_terrainHeightsClear{false};   // a game left: the next one's tiles are other tiles
thread_local void* t_terrainLayer = nullptr;   // the layer of the UpdateTerrainTiles running on this thread
thread_local int t_terrainDepth = 0;
terrainheights::Keeper g_terrainKeeper;   // the game's thread only (inside UpdateTerrainTiles)

// The game's floor ray through cell (x, y), as 0x6B0350 casts it for the hero's: out = the hit's point; false when nothing was hit (or
// it faulted).
bool TerrainFloor(void* layer, float x, float y, float out[3]) noexcept {
    const uintptr_t phys = d2rsig::Addr(RVA_PHYSICS), cast = d2rsig::Addr(RVA_RAYCAST);
    uint64_t filter = 0;
    if (!phys || !cast || !layer || !SafeRead(&filter, (const uint8_t*)layer + terrainheights::kLayerFloorFilter, sizeof filter)) return false;
    alignas(16) uint8_t in[terrainheights::kRayInputSize];
    alignas(16) uint8_t res[terrainheights::kRayOutSize] = {};
    terrainheights::RayInput(x, y, filter, in);
    bool hit = false;
    __try {
        void* mgr = ((PhysicsFn)phys)();
        hit = mgr && ((RayCastFn)cast)(mgr, res, in, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    memcpy(out, res, 3 * sizeof(float));
    return hit;
}

// Each bin its own tile's floor, stitched (terrainheights::Stitch). hero: the game's height for all.
void TerrainHeightsFill(void* layer, float hero) {
    using namespace terrainheights;
    static void* lastLayer = nullptr;
    static uint32_t tick = 0;
    static ULONGLONG toldAt = 0;
    static bool toldFirst = false;
    static LONGLONG spent = 0, spentTicks = 0;
    static int offLine = 0;   // hits whose point is not on the ray's line (an out laid out otherwise: not used)
    const uintptr_t bins = d2rsig::Addr(RVA_TERRAIN_BINS), cornerAt = d2rsig::Addr(RVA_TERRAIN_CORNER);
    int32_t corner[2] = {};
    if (!bins || !cornerAt || !SafeRead(corner, (const void*)cornerAt, sizeof corner) || !std::isfinite(hero)) return;
    if (g_terrainHeightsClear.exchange(false) || layer != lastLayer) g_terrainKeeper.Clear();
    lastLayer = layer;
    g_terrainKeeper.Corner(corner[0], corner[1]);
    LARGE_INTEGER t0, t1;
    QueryPerformanceCounter(&t0);
    int idx[kBudget];
    const int n = g_terrainKeeper.Pick(++tick, kBudget, idx);
    for (int j = 0; j < n; ++j) {
        const int i = idx[j];
        Tile t;
        t.asked = kSamples;
        for (int k = 0; k < kSamples; ++k) {
            float x, y, h;
            float o[3] = {};
            SampleCell(corner[0] + i % kBins, corner[1] + i / kBins, k, &x, &y);
            if (TerrainFloor(layer, x, y, o)) {
                if (Hit(o, x, y, &h)) t.h[t.n++] = h;
                else ++offLine;
            }
        }
        g_terrainKeeper.Put(i, t, tick);
    }
    static Tile win[kTiles];
    static float out[kTiles];
    g_terrainKeeper.Window(win);
    const Stats s = Stitch(win, hero, out);
    QueryPerformanceCounter(&t1);
    spent += t1.QuadPart - t0.QuadPart;
    ++spentTicks;
    float* heights = (float*)bins;
    if (!SafeWriteMem(heights, heights, sizeof(float))) return;   // the bins are the game's own .data; written as it writes them
    for (int i = 0; i < kTiles; ++i) heights[i * (kBinStride / sizeof(float))] = out[i];
    const ULONGLONG now = GetTickCount64();
    if (!toldFirst || now - toldAt > 20000) {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        LogF("vrcam: terrain heights: corner tile %d,%d, the hero's floor %.2f; %d of %d tiles' floors known, %d height(s) %.2f..%.2f, "
             "%d across a ledge, %d lone, %d from neighbours, %d at the hero's; %.0f us a tick%s ([render] terrain_heights)", corner[0],
             corner[1], hero, s.known, kTiles, s.levels, s.low, s.high, s.mixed, s.lone, s.filled, s.hero,
             spentTicks ? double(spent) * 1e6 / double(f.QuadPart) / double(spentTicks) : 0.0,
             offLine ? " - SOME HITS OFF THE RAY'S LINE, not used (the game's ray answers otherwise?)" : "");
        offLine = 0;
        toldFirst = true;
        toldAt = now;
        spent = spentTicks = 0;
    }
}

void HookTerrainTiles(void* layer, void* rooms, uint8_t flag) {
    t_terrainLayer = layer;
    ++t_terrainDepth;
    if (OrigTerrainTiles) OrigTerrainTiles(layer, rooms, flag);
    --t_terrainDepth;
}
void HookTerrainOrigin(const int32_t* tile, float height) {
    if (OrigTerrainOrigin) OrigTerrainOrigin(tile, height);
    // only the level layer's call (the other caller, 0x67AB00, puts the menus' terrain at 0)
    if (t_terrainDepth > 0 && g_set.terrainHeights.load(std::memory_order_relaxed)) TerrainHeightsFill(t_terrainLayer, height);
}

// Once [render] terrain_heights asks for it; tried with the other hooks until the bytes are there.
void InstallTerrainHeightHook() {
    static int misses = 0;
    if (!g_ctx || g_terrainHook.load() != 0 || !g_set.terrainHeights.load()) return;
    if (!Matches(RVA_TERRAIN_TILES, kSigTerrainTiles, sizeof kSigTerrainTiles) ||
        !Matches(RVA_TERRAIN_ORIGIN, kSigTerrainOrigin, sizeof kSigTerrainOrigin) ||
        !Matches(RVA_PHYSICS, kSigPhysics, sizeof kSigPhysics) || !Matches(RVA_RAYCAST, kSigRayCast, sizeof kSigRayCast) ||
        !d2rsig::Addr(RVA_TERRAIN_BINS) ||
        !d2rsig::Addr(RVA_TERRAIN_CORNER)) {
        if (g_inWorld.load() && ++misses >= 20) {
            g_terrainHook.store(2);
            Log("vrcam: terrain heights: hook NOT possible (0x6B54C0 / 0xE79000 / 0x7E65A0 / 0x7E7DD0 not as expected - another build?) - the ground's "
                "tiles stay at the hero's floor");
        }
        return;
    }
    const bool in = d2rsig::Hook(RVA_TERRAIN_TILES, kSigTerrainTiles, sizeof kSigTerrainTiles, (void*)&HookTerrainTiles,
                                 (void**)&OrigTerrainTiles) &&
                    d2rsig::Hook(RVA_TERRAIN_ORIGIN, kSigTerrainOrigin, sizeof kSigTerrainOrigin, (void*)&HookTerrainOrigin,
                                 (void**)&OrigTerrainOrigin);
    g_terrainHook.store(in ? 1 : 2);
    Log(in ? "vrcam: terrain heights: hook in - each ground tile at its own floor's height, stitched to its neighbours ([render] terrain_heights)"
           : "vrcam: terrain heights: hook FAILED - the ground's tiles stay at the hero's floor");
}

// [render] rings and model_radius into cleanroom/drawdist (the old
// renderdistance plugin's job); the radius retries until its hook is in.
// big_radius apart (D2R VR Unleashed's Big objects, cleanroom/drawdist/unleashed_big_objects.cpp, MPL): its hook goes in only once it is
// on, and waits for the game to have run that code; off and never on, the game's test is never touched.
std::atomic<bool> g_renderDirty{true};
std::atomic<bool> g_bigDirty{true};
void ApplyRender() {
    // RingsNow / ModelRadiusNow: [render] rings and model_radius, or VR F3's own Draw distance ([table] draw_distance) while F3 is the view -
    // so going into F3 or out of it, or moving F3's slider, applies again (only when the pair changes: SetModelRadius re-sorts for 500 ms)
    static int viewRings = -1;
    static float viewRadius = -1.0f;
    const int rings = RingsNow();
    const float radius = ModelRadiusNow();
    if (rings != viewRings || radius != viewRadius) {
        if (viewRings >= 0) g_renderDirty.store(true);
        viewRings = rings;
        viewRadius = radius;
    }
    if (g_renderDirty.load()) {
        d2rcam::SetRenderRadius(rings);
        if (d2rcam::SetModelRadius(radius)) {
            g_renderDirty.store(false);
            LogF("vrcam: render distance - %d room rings, models out to %.0f%s", d2rcam::GetRenderRadius(), radius,
                 ViewDistanceStop() ? " (VR F3's own Draw distance, [table] draw_distance)" : "");
        }
    }
    const float big = g_set.bigRadius.load();
    static ULONGLONG bigWaitSince = 0;
    static bool toldBigWait = false;
    if (g_bigDirty.load() && big > 0 && g_inWorld.load()) {   // (on, its hook not in yet, in a game)
        if (!bigWaitSince) bigWaitSince = GetTickCount64();
        if (!toldBigWait && GetTickCount64() - bigWaitSince > 60000) {
            toldBigWait = true;
            Log("vrcam: big objects - the game's model distance test not found after a minute in the game (another build?); every model "
                "keeps the model visibility radius. Still looked for twice a second");
        }
    }
    if (g_bigDirty.load() && d2rcam::SetBigModelRadius(big)) {
        g_bigDirty.store(false);
        bigWaitSince = 0;
        static bool wasOn = false;
        if (big > 0)
            LogF("vrcam: big objects - trees, houses and walls drawn out to %.0f by their size (models out to %.0f, %d room rings; D2R VR "
                 "Unleashed's, bearstonem)", big, g_set.modelRadius.load(), g_set.rings.load());
        else if (wasOn) Log("vrcam: big objects - off, every model out to the model visibility radius");
        wasOn = big > 0;
    }
    // what they cost: how many models their size kept, every 10 s
    static ULONGLONG nextCount = 0;
    if (big > 0 && GetTickCount64() >= nextCount) {
        const bool first = nextCount == 0;
        nextCount = GetTickCount64() + 10000;
        if (!first)
            LogF("vrcam: big objects - %u model decisions kept past %.0f by size in 10 s", d2rcam::TakeBigModelsKept(), g_set.modelRadius.load());
        else d2rcam::TakeBigModelsKept();
    }
}

// [render] render_pool_mb and entity_pool_mb: the game's Renderer Pool (12 MB) and Entity
// System Pool (70 MB) grown once the game has made them (cleanroom/pools). With more rooms
// drawn the renderer's ran out in the Act 3 jungle and the game crashed (issue #7). Once per
// game start - a change in the ini counts at the next. One line in the log.
void ApplyPools() {
    static bool done = false, toldWaiting = false;
    static ULONGLONG since = 0;
    if (done) return;
    char line[400];
    done = d2rcam::GrowPools((unsigned)g_set.renderPoolMb.load(), (unsigned)g_set.entityPoolMb.load(), line, sizeof line);
    if (!since) since = GetTickCount64();
    if (done) LogF("vrcam: %s", line);
    else if (!toldWaiting && GetTickCount64() - since > 120000) { toldWaiting = true; LogF("vrcam: %s (still waiting)", line); }
}

// The game's main loop sleeps 10 ms every frame while its window is not in
// front (profiler zone "Background Sleep", D2RLoader layout 0xB6A9F): ~60 fps
// instead of 180 - 30 per eye in AFR stereo - whenever BodyWalk or the headset
// has the focus. Both calls' argument 10 becomes 0 (a yield). In memory only;
// [render] background_full_speed=0 puts the 10 back.
void ApplyBackgroundSleep() {
    static const uint64_t kSites[2] = {0xB6C32, 0xB6C92};
    static const uint8_t kTen[5] = {0xB9, 0x0A, 0x00, 0x00, 0x00}, kZero[5] = {0xB9, 0x00, 0x00, 0x00, 0x00};   // mov ecx, 10 / 0
    static int told = -1;
    if (!g_ctx) return;
    const bool want = g_set.bgFullSpeed.load();
    int done = 0;
    for (uint64_t rva : kSites) {
        if (Matches(rva, want ? kZero : kTen, 5)) { ++done; continue; }
        if (Matches(rva, want ? kTen : kZero, 5) && d2rsig::Patch(rva, want ? kTen : kZero, 5, want ? kZero : kTen, 5)) ++done;
    }
    const int state = done == 2 ? (want ? 1 : 0) : 2;
    if (state != told) {
        told = state;
        Log(state == 1 ? "vrcam: background sleep removed - full frame rate with the game window behind BodyWalk"
            : state == 0 ? "vrcam: background sleep back (10 ms a frame while the window is behind)"
                         : "vrcam: background sleep NOT patched (code not decrypted yet, or another build)");
    }
}

// Walls the game makes see-through (2026-10-07). From its own camera high above, a wall
// between it and the hero is drawn at half alpha so the hero stays in sight; from our
// camera that is any wall we look at. The alpha is data: transparentWallAlpha, 0.5, in
// data/hd/global/excel/translation_settings.json, loaded into GlobalTranslationSettings - a
// static block in the image (RVA 0x2401498 on this build): highlight -0.3, its characters'
// 0.5, item shift 0, attack fx (bool), wind 0.9, 0.45, monster fade (bool), fade in 0.15,
// out 0.45, monster light 1, transition 0.45, then the wall alpha 0.5. Found by those
// values (another build: scanned), set to 1 per [render] solid_walls: 0 the game's,
// 1 every view but F1 (the default: from above the hero is still wanted in sight - the
// user, 2026-10-07: "see-through only for F1"), 2 in every view.
namespace solidwalls {
std::atomic<uintptr_t> g_alpha{0};   // the wall alpha's address, 0 = not found yet
float g_was = 0.5f;                  // the game's own value

bool Fits(const float* f) {   // the block's values, the bools skipped
    return f[0] == -0.3f && f[1] == 0.5f && f[2] == 0.0f && f[4] == 0.9f && f[5] == 0.45f && f[7] == 0.15f &&
           f[8] == 0.45f && f[10] == 0.45f && f[11] > 0.0f && f[11] <= 1.0f;
}

void Find() {
    if (g_alpha.load() || !g_base) return;
    static ULONGLONG last = 0;
    if (GetTickCount64() - last < 5000) return;
    last = GetTickCount64();
    float f[12];
    if (env::SafeRead((void*)(g_base + 0x2401498), f, sizeof f) && Fits(f)) {
        g_was = f[11];
        g_alpha.store(g_base + 0x2401498 + 11 * 4);
        LogF("vrcam: the game's see-through wall alpha found (%.2f) at +0x24014C4", g_was);
        return;
    }
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (!env::SafeRead((void*)g_base, &dos, sizeof dos) || dos.e_magic != IMAGE_DOS_SIGNATURE) return;
    if (!env::SafeRead((void*)(g_base + dos.e_lfanew), &nt, sizeof nt) || nt.Signature != IMAGE_NT_SIGNATURE) return;
    constexpr size_t kChunk = 0x10000;
    static float buf[kChunk / 4 + 12];
    const uintptr_t end = g_base + nt.OptionalHeader.SizeOfImage;
    for (uintptr_t a = g_base + 0x1000; a < end; a += kChunk) {
        const size_t n = (size_t)std::min<uintptr_t>(kChunk + 48, end - a);
        if (!env::PageReadable(a) || !env::SafeRead((void*)a, buf, n)) continue;
        for (size_t i = 0; i + 12 <= n / 4; ++i) {
            if (buf[i] != -0.3f || !Fits(buf + i)) continue;
            g_was = buf[i + 11];
            g_alpha.store(a + (i + 11) * 4);
            LogF("vrcam: the game's see-through wall alpha found (%.2f) at +0x%llX", g_was, (unsigned long long)(a + (i + 11) * 4 - g_base));
            return;
        }
    }
    static bool told = false;
    if (!told) { told = true; Log("vrcam: the game's see-through wall alpha NOT found (another build?) - walls fade as the game has them"); }
}

// On the update thread: the alpha the view wants, written when it changes.
void Tick() {
    Find();
    const uintptr_t a = g_alpha.load();
    if (!a) return;
    const int mode = g_set.solidWalls.load();
    const bool inside = g_enabled.load() && !TopPersp();   // every view but F1 (the game's camera, or ours from above): the floor too
    const float want = mode == 2 || (mode == 1 && inside) ? 1.0f : g_was;
    float now = 0.0f;
    if (!env::SafeRead((void*)a, &now, sizeof now) || now == want) return;
    __try { *(volatile float*)a = want; } __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    LogF("vrcam: walls %s (alpha %.2f)", want >= 1.0f ? "solid" : "see-through as the game has them", want);
}
}  // namespace solidwalls

// [render] keep_roofs (vr/roof_keep.h): the game's roof cut-away - its hide branch asking the opacity animator for alpha 0 -
// gets 1 in VR F4; every other caller of the setter, and every other view, keeps the game's own. The hook goes in once the
// setting asks for it; off again, the call passes straight through.
namespace roofhook {
using namespace ::roofkeep;
using SetFn = void (*)(void* component, float alpha, float seconds);
SetFn g_orig = nullptr;
std::atomic<uintptr_t> g_hideReturn{0};
std::atomic<uint32_t> g_kept{0};
bool g_in = false, g_failed = false;

bool Keep() { return g_set.keepRoofs.load(std::memory_order_relaxed) && FullBody() && g_enabled.load(std::memory_order_relaxed) && g_inWorld.load(std::memory_order_relaxed); }

void HookSet(void* component, float alpha, float seconds) {
    const uintptr_t caller = (uintptr_t)_ReturnAddress();
    const float want = ::roofkeep::TargetAlpha(Keep(), caller, g_hideReturn.load(std::memory_order_relaxed), alpha);
    if (want != alpha) g_kept.fetch_add(1, std::memory_order_relaxed);
    g_orig(component, want, seconds);
}

// The update thread: tried until both are there (their page decrypted), once the setting is on.
void Install() {
    if (g_in || g_failed || !g_ctx || !g_set.keepRoofs.load()) return;
    if (!Matches(kSetterRva, kSetterSig, sizeof kSetterSig) || !Matches(kHideRva, kHideSig, sizeof kHideSig)) return;
    const uintptr_t hide = d2rsig::Addr(kHideRva);
    if (!hide) return;
    g_hideReturn.store(hide + kHideReturnOffset);
    g_in = d2rsig::Hook(kSetterRva, kSetterSig, sizeof kSetterSig, (void*)&HookSet, (void**)&g_orig);
    g_failed = !g_in;
    Log(g_in ? "vrcam: roofs kept in F4 - the game's roof cut-away hook in ([render] keep_roofs)"
             : "vrcam: roof cut-away hook FAILED - roofs fade as the game has them");
}

void Tick() {
    static ULONGLONG last = 0;
    if (!g_in || GetTickCount64() - last < 10000) return;
    last = GetTickCount64();
    if (const uint32_t n = g_kept.exchange(0)) LogF("vrcam: roofs kept - %u of the game's cut-away requests answered with alpha 1 in 10 s", n);
}
}  // namespace roofhook

// [stereo] props_once_a_pair (vr/prop_hold.h): in the right pass of a pair (the game's draw run a second time, not the replay)
// the partial ragdoll's Prepare and Simulate, called from its Step, are skipped: the props move once a pair, both eyes get the
// pose the left pass made. Both hooks go in once the setting asks; until both are in, neither skips anything.
namespace prophook {
using namespace ::prophold;
using PrepareFn = void (*)(void* self, const void* transform, const void* settings, float dt);
using SimulateFn = void (*)(void* self, float dt);
PrepareFn g_origPrepare = nullptr;
SimulateFn g_origSimulate = nullptr;
std::atomic<bool> g_ready{false};
std::atomic<uintptr_t> g_prepareReturn{0}, g_simulateReturn{0};
std::atomic<uint32_t> g_ran{0}, g_held{0};
bool g_prepareIn = false, g_simulateIn = false, g_failed = false;

bool Skip(uintptr_t caller, uintptr_t expected) {
    return ::prophold::Hold(g_ready.load(std::memory_order_relaxed), g_set.propsOncePair.load(std::memory_order_relaxed) && FullBody(),
                PairOfOneFrame(), g_eye.load(std::memory_order_relaxed), caller, expected);
}

void HookPrepare(void* self, const void* transform, const void* settings, float dt) {
    const uintptr_t caller = (uintptr_t)_ReturnAddress(), expected = g_prepareReturn.load(std::memory_order_relaxed);
    if (Skip(caller, expected)) { g_held.fetch_add(1, std::memory_order_relaxed); return; }
    if (caller == expected) g_ran.fetch_add(1, std::memory_order_relaxed);
    g_origPrepare(self, transform, settings, dt);
}
void HookSimulate(void* self, float dt) {
    if (Skip((uintptr_t)_ReturnAddress(), g_simulateReturn.load(std::memory_order_relaxed))) return;
    g_origSimulate(self, dt);
}

// The update thread: tried until all are there (their pages decrypted), once the setting is on.
void Install() {
    if (g_ready.load() || g_failed || !g_ctx || !g_set.propsOncePair.load()) return;
    if (!Matches(kCallsRva, kCallsSig, sizeof kCallsSig) || !Matches(kThunkRva, kThunkSig, sizeof kThunkSig) ||
        !Matches(kPrepareRva, kPrepareSig, sizeof kPrepareSig) || !Matches(kSimulateRva, kSimulateSig, sizeof kSimulateSig)) return;
    const uintptr_t calls = d2rsig::Addr(kCallsRva);
    if (!calls) return;
    g_prepareReturn.store(calls + kPrepareReturnOffset);
    g_simulateReturn.store(calls + kSimulateReturnOffset);
    if (!g_prepareIn) g_prepareIn = d2rsig::Hook(kPrepareRva, kPrepareSig, sizeof kPrepareSig, (void*)&HookPrepare, (void**)&g_origPrepare);
    if (!g_simulateIn) g_simulateIn = d2rsig::Hook(kSimulateRva, kSimulateSig, sizeof kSimulateSig, (void*)&HookSimulate, (void**)&g_origSimulate);
    g_failed = !g_prepareIn || !g_simulateIn;
    g_ready.store(!g_failed);
    Log(!g_failed ? "vrcam: hanging props once a pair - the partial ragdoll's hooks in ([stereo] props_once_a_pair; only where the right eye is drawn a second time)"
                  : "vrcam: hanging props hooks FAILED - the props step in both passes as before");
}

void Tick() {
    static ULONGLONG last = 0;
    if (!g_ready.load() || GetTickCount64() - last < 10000) return;
    last = GetTickCount64();
    const uint32_t ran = g_ran.exchange(0), held = g_held.exchange(0);
    if (held) LogF("vrcam: hanging props - %u steps in the left pass, %u right-pass steps held in 10 s", ran, held);
}
}  // namespace prophook

}  // namespace vrc
