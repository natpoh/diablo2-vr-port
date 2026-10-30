// vr/render/render_switches.cpp - the game's render switched as the ini says: room heights ([render] floors_stay), the draw distance
// ([render] rings, model_radius, big_radius), the memory pools, the background sleep, solid walls, the roofs kept, the props once a
// pair (cut from vr/vrcam.cpp, docs/plan_modules.md M13 - the render group). What it gives: vr/render/render_switches.h.
#include "../vrcam_headers.h"

using namespace D2RL;
using namespace vrc;   // as in vrcam.cpp

namespace vrc {

// [render] floors_stay (vr/room_heights.h): LevelTranslationLayer::CalculateHeightOffset 0x6A2A40(layer, DRLG_ROOM*, const float edges[4])
// -> the height a room is drawn at, stitched to the neighbours drawn at that moment; the record that keeps it is erased when the room is no longer
// drawn, so a way back another way brings the room back a step up or down. A room keeps the height it was first drawn at, for the game - the
// rooms of one preset as one (the game copies a drawn sibling's height to the others without asking), the rooms kept together as one island
// placed where it meets the ground drawn now, a room drawn before its neighbours not kept. Every view (any camera but the game's own from
// above shows it); visual only.
constexpr uint64_t RVA_ROOM_HEIGHT = 0x6A2A40;
// mov rax, rsp; push rbx; push rsi; push rdi; push r13; sub rsp, 58h; movaps [rax-48h], xmm7
const uint8_t kSigRoomHeight[16] = {0x48,0x8B,0xC4,0x53,0x56,0x57,0x41,0x55,0x48,0x83,0xEC,0x58,0x0F,0x29,0x78,0xB8};
using RoomHeightFn = float (*)(void* layer, const void* room, const float* edges);
RoomHeightFn OrigRoomHeight = nullptr;
std::atomic<int> g_roomHeightHook{0};   // 0 not in yet, 1 in, 2 not possible
std::atomic<uintptr_t> g_roomLayer{0};   // the layer the game asked for (its records at +0x2B0)
std::atomic<bool> g_roomHeightsClear{false};   // the main menu was seen: the next game's rooms are other rooms
roomheights::Keeper g_roomKeeper;
SRWLOCK g_roomKeeperLock = SRWLOCK_INIT;
// The log's lines of each kind (kept off the game's / an island placed / an orphan / given a grounded neighbour's), at most 3 a level a
// game - each level its own, so the area looked at is told even after others (game thread; cleared with the keeper).
uint8_t g_roomHeightsTold[1025][4];
bool RoomHeightsTell(int level, int kind) { return level > 0 && level <= 1024 && g_roomHeightsTold[level][kind]++ < 3; }
std::atomic<uint32_t> g_roomTick{0};   // the level layer's ticks (the pending rooms' age)

bool RoomRead(void* d, uintptr_t s, size_t n) { return SafeRead(d, (const void*)s, n); }

float HookRoomHeight(void* layer, const void* room, const float* edges) {
    const float game = OrigRoomHeight ? OrigRoomHeight(layer, room, edges) : 0.0f;
    if (!g_set.keepRoomHeights.load(std::memory_order_relaxed) || !room) return game;
    roomheights::Key k;
    uint64_t uid = 0;
    if (!roomheights::ReadRoom(&RoomRead, (uintptr_t)room, &k, &uid)) return game;
    g_roomLayer.store((uintptr_t)layer, std::memory_order_relaxed);
    roomheights::Side sides[4];
    roomheights::Sides(&RoomRead, (uintptr_t)layer, (uintptr_t)room, edges, sides);
    const bool drawn = roomheights::DrawnCount(&RoomRead, (uintptr_t)layer) > 0;
    roomheights::Keeper::How how;
    size_t joined = 0;
    float kept = NAN;
    AcquireSRWLockExclusive(&g_roomKeeperLock);
    g_roomKeeper.Kept(k, &kept);
    const float h = g_roomKeeper.Ask(k, uid, game, sides, drawn, g_roomTick.load(std::memory_order_relaxed), &how, &joined);
    ReleaseSRWLockExclusive(&g_roomKeeperLock);
    const char* what = k.preset ? "preset" : "room";
    char around[96] = "";   // for the log: what the drawn sides ask of it, west north east south ("-" not drawn)
    const auto Around = [&]() -> const char* {
        int n = 0;
        for (int d = 0; d < 4; ++d)
            n += sides[d].drawn ? snprintf(around + n, sizeof around - n, "%s%.2f", d ? " " : "", sides[d].ask)
                                : snprintf(around + n, sizeof around - n, "%s-", d ? " " : "");
        return around;
    };
    using How = roomheights::Keeper::How;
    // The first few of each a level: where, and what the game would have done.
    if (how == How::Kept && std::fabs(h - game) > 0.01f && RoomHeightsTell(k.level, 0))
        LogF("vrcam: room heights: level %d %s %d,%d asked again - the game worked out %.2f, given %.2f to stand as it did beside the rooms kept "
             "with it; its sides ask W N E S %s ([render] floors_stay)", k.level, what, k.x, k.y, game, h, Around());
    else if (how == How::Joined && RoomHeightsTell(k.level, 1))
        LogF("vrcam: room heights: %zu kept room(s) placed by level %d %s %d,%d - the game worked out %.2f where %.2f was kept: they all move "
             "by %.2f to meet the ground drawn now ([render] floors_stay)", joined, k.level, what, k.x, k.y, game, kept, game - kept);
    else if (how == How::Orphan && RoomHeightsTell(k.level, 2))
        LogF("vrcam: room heights: level %d %s %d,%d drawn before its neighbours or beside an orphan's ground (the game's %.2f; its sides ask "
             "W N E S %s) - not kept ([render] floors_stay)", k.level, what, k.x, k.y, game, Around());
    else if (how == How::Grounded && RoomHeightsTell(k.level, 3))
        LogF("vrcam: room heights: level %d %s %d,%d - the game's %.2f is from an orphan's ground; given %.2f, its grounded neighbour's "
             "(W N E S %s) ([render] floors_stay)", k.level, what, k.x, k.y, game, h, Around());
    return h;
}

// After each tick of the level layer (the game's thread, d2rcam::SetLevelTickCallback): the rooms asked for that are drawn now keep the height
// they were drawn at; a game left for the main menu leaves nothing behind.
void RoomHeightsTick(void*) {
    const uint32_t tick = g_roomTick.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool clear = g_roomHeightsClear.exchange(false);
    AcquireSRWLockExclusive(&g_roomKeeperLock);
    if (clear) g_roomKeeper.Clear();
    const size_t before = g_roomKeeper.KeptCount();
    if (g_set.keepRoomHeights.load(std::memory_order_relaxed))
        g_roomKeeper.Harvest(&RoomRead, g_roomLayer.load(std::memory_order_relaxed), tick);
    const size_t kept = g_roomKeeper.KeptCount();
    ReleaseSRWLockExclusive(&g_roomKeeperLock);
    if (clear) memset(g_roomHeightsTold, 0, sizeof g_roomHeightsTold);
    static bool told = false;
    if (!told && before == 0 && kept > 0) {
        told = true;
        LogF("vrcam: room heights: the first %zu room(s) drawn keep their height for the game ([render] floors_stay)", kept);
    }
}

// Once [render] floors_stay asks for it; tried with the other hooks until the bytes are there.
void InstallRoomHeightHook() {
    static int misses = 0;
    if (!g_ctx || g_roomHeightHook.load() != 0 || !g_set.keepRoomHeights.load()) return;
    if (!d2rcam::GetStatus().roomHooks) return;   // the tick that keeps the heights comes from the render distance's hook
    if (!Matches(RVA_ROOM_HEIGHT, kSigRoomHeight, sizeof kSigRoomHeight)) {
        if (g_inWorld.load() && ++misses >= 20) {
            g_roomHeightHook.store(2);
            Log("vrcam: room heights: hook NOT possible (0x6A2A40 not as expected - another build?) - a room may come back at another height");
        }
        return;
    }
    d2rcam::SetLevelTickCallback(&RoomHeightsTick);
    const bool in = d2rsig::Hook(RVA_ROOM_HEIGHT, kSigRoomHeight, sizeof kSigRoomHeight, (void*)&HookRoomHeight, (void**)&OrigRoomHeight);
    g_roomHeightHook.store(in ? 1 : 2);
    Log(in ? "vrcam: room heights: hook in - a room of the level keeps the height it was first drawn at ([render] floors_stay)"
           : "vrcam: room heights: hook FAILED - a room may come back at another height");
}

// [render] rings and model_radius into cleanroom/drawdist (the old
// renderdistance plugin's job); the radius retries until its hook is in.
// big_radius apart (D2R VR Unleashed's Big objects, cleanroom/drawdist/unleashed_big_objects.cpp, MPL): its hook goes in only once it is
// on, and waits for the game to have run that code; off and never on, the game's test is never touched.
std::atomic<bool> g_renderDirty{true};
std::atomic<bool> g_bigDirty{true};
void ApplyRender() {
    if (g_renderDirty.load()) {
        d2rcam::SetRenderRadius(g_set.rings.load());
        if (d2rcam::SetModelRadius(g_set.modelRadius.load())) {
            g_renderDirty.store(false);
            LogF("vrcam: render distance - %d room rings, models out to %.0f", d2rcam::GetRenderRadius(), g_set.modelRadius.load());
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
