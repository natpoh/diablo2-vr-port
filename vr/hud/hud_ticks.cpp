// vr/hud/hud_ticks.cpp - cut from vr/vrcam.cpp (docs/plan_modules.md M9); what it gives: vr/hud/hud_vr.h. It sees vrcam.cpp's headers.
#include "../vrcam_headers.h"

using namespace D2RL;
using namespace vrc;   // as in vrcam.cpp

namespace vrc {
// BodyWalk's walking stick (vrcam.cpp; declared here only: its default is its definition's)
bool BodyWalkWalk(float* px, float* py, bool* own = nullptr);


// F3, the game on the floor: the mouse pointer laid on the game's ground by
// itself, wherever the head looks from. A plane seen from our camera has
// 1/depth linear in the screen's height, so FlatVR's per-eye shift
// (FlatVRGameHudPointerDepth: base + tilt v + curve v^2, v 0 top .. 1 bottom, a
// fraction of one eye's width, + nearer) is exactly a line: with the off-axis
// pair of VrView / VrProj, shift(v) = M[0] ipd / 4 (1/z(v) - 1/conv). Its size at
// the top of the screen follows the depths' ratio (far off it is small, as the
// ground it points at). Sent when it moves, not every tick: hud's lock is the
// render thread's too.
bool FloorPointerAuto() {
    return g_set.platform.load() == 1 && g_enabled.load() && TableView() && g_floorCamOk.load() && g_inWorld.load() &&
           !gamestate::MenuOpen();
}
void FloorPointerTick() {
    static float sent[3] = {1e9f, 1e9f, 1e9f};
    static bool told = false;
    if (!FloorPointerAuto()) { sent[0] = 1e9f; told = false; return; }
    const float h = g_floorEyeH.load(), sx = g_projSx.load(), sy = g_projSy.load();
    if (h < 1e-3f || sx <= 0.0f || sy <= 0.0f) return;
    const float fy = g_floorFwdY.load(), uy = g_floorUpY.load(), conv = g_stereoConv.load();
    auto inv = [&](float v) { return -(fy + uy * (1.0f - 2.0f * v) / sy) / h; };   // 1 / the ground's depth at height v
    const float k = sx * g_stereoIpd.load() * 0.25f, c = conv > 0.0f ? 1.0f / conv : 0.0f;
    const float top = inv(0.0f), bottom = inv(1.0f);
    const float base = k * (top - c), tilt = k * (bottom - top);
    const float topScale = bottom > 1e-6f ? std::clamp(top / bottom, 0.3f, 1.0f) : 1.0f;
    if (fabsf(base - sent[0]) < 2e-4f && fabsf(tilt - sent[1]) < 2e-4f && fabsf(topScale - sent[2]) < 0.005f) return;
    sent[0] = base; sent[1] = tilt; sent[2] = topScale;
    hud::SetPointerDepth(base, tilt, 0.0f, topScale);
    if (!told) {
        told = true;
        LogF("vrcam: mouse pointer for FlatVR laid on the floor's ground: shift %.4f at the top, %.4f at the bottom, size at the top %.2f",
             base, base + tilt, topScale);
    }
}

// [hud] map_direction (vr/map_direction.h), every tick: the arrow on the game's map in our camera's views - flat F2 / F3, VR F2 / F4 -
// not F1 (the game's own camera, or ours from above: the map turns with the picture) nor the floor (F3); and whether the game's map
// is the whole-screen one now (its AutoMap open in mode 0: no box of its own, the arrow in the middle). The way itself comes with each view.
void MapDirectionTick() {
    const bool want = g_set.mapDirection.load() && g_enabled.load() && g_inWorld.load() && ViewNow() != 1 && !TableView();
    hud::SetMapDirection(want, want && gamestate::AutoMapOpen() && gamestate::MapMode() == 0);
}

// [openxr] hand_inventory (vr/hand_inventory.h, "the walk with the inventory up"): the game's walk is locked while its controller inventory
// is up, so the stick's own command is sent for it - 0xFEFE0 "move in this direction" (as the stick's walk sends it, and as
// move_while_attacking does), with the game's own strength, Run toggle, stamina and forced walk; let go, the stick's own stop (the map click
// of type 6 at the hero's own cell). On the game's UI thread (gamestate::QueueHandWalk), every guarded; a fault stops it for the session.
constexpr uint64_t RVA_HAND_WALK_DIR = 0xFEFE0, RVA_HAND_CMD_FLAGS = 0x8AFD0;
std::atomic<uint64_t> g_hwGrid{0};    // the direction on the game's grid (two floats), the timer's
std::atomic<int> g_hwSend{0};         // handinv::Send for the UI thread
std::atomic<uint32_t> g_hwSent{0};    // walks sent (the log's evidence)
std::atomic<bool> g_hwFault{false};
void HandWalkOnUi() noexcept {
    const int what = g_hwSend.exchange(0);
    if (what == 0 || g_hwFault.load()) return;
    const uintptr_t hero = SewerHero();
    const uintptr_t walkFn = d2rsig::Addr(RVA_HAND_WALK_DIR), flagsFn = d2rsig::Addr(RVA_HAND_CMD_FLAGS), mgrFn = d2rsig::Addr(RVA_CTRL_HANDLER);
    const uintptr_t indexFn = d2rsig::Addr(RVA_SEWER_LOCAL_INDEX), clickFn = d2rsig::Addr(RVA_MAP_CLICK);
    if (!hero || !walkFn || !flagsFn || !mgrFn || !indexFn) return;
    __try {
        const uint16_t flags = reinterpret_cast<uint16_t (*)()>(flagsFn)();
        if (what == (int)handinv::Send::Walk) {
            const uint64_t bits = g_hwGrid.load();
            float g[2];
            memcpy(g, &bits, sizeof g);
            const int index = reinterpret_cast<int (*)()>(indexFn)();
            const uintptr_t mgr = reinterpret_cast<uintptr_t (*)()>(mgrFn)();
            if (!mgr || index < 0 || index >= 8) return;
            const uintptr_t rec = mgr + (uintptr_t)index * 0xB8;   // the controller manager's record of the player (docs/move_recon.md section 3)
            const bool stamina = *reinterpret_cast<const volatile uint8_t*>(rec + 0x1951) != 0, forced = *reinterpret_cast<const volatile uint8_t*>(rec + 0x1952) != 0;
            const float threshold = *reinterpret_cast<const volatile float*>(mgr + 0x1F94);
            const moveattack::Walk w = moveattack::MakeWalk(g[0], g[1], (flags >> 8) != 0, stamina, forced, threshold);
            if (!w.ok) return;
            reinterpret_cast<void (*)(uintptr_t, const float*, float, bool)>(walkFn)(hero, w.dir, w.mag, w.run);
            g_hwSent.fetch_add(1);
        } else if (clickFn) {   // the stick's own stop: walk to where the hero stands
            const uintptr_t path = *reinterpret_cast<const volatile uintptr_t*>(hero + 0x38);
            if (path < 0x10000) return;
            const int x = *reinterpret_cast<const volatile uint16_t*>(path + 2), y = *reinterpret_cast<const volatile uint16_t*>(path + 6);
            if (g_mapClick.load() == 1) OrigMapClick(reinterpret_cast<void*>(hero), 6, x, y, flags);
            else if (Matches(RVA_MAP_CLICK, kSigMapClick, sizeof kSigMapClick)) reinterpret_cast<MapClickFn>(clickFn)(reinterpret_cast<void*>(hero), 6, x, y, flags);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_hwFault.store(true);
    }
}

// [openxr] hand_inventory (vr/hand_inventory.h): every tick of the timer - where the inventory hangs and whether the left stick walks with it
// up. The hero's area read 4 times a second (the game's own accessor, as the sewer stairs read it). While the panel hangs on the hand and the
// stick is pushed, once a second: did the game's walk read the stick, and did the hero move (the look-at point) - the game's own answer to
// "may he walk with the inventory open", said in the log.
void HandInventoryTick(ULONGLONG nowMs) {
    static int level = 0;
    static ULONGLONG nextLevel = 0;
    handinv::Facts f;
    f.keyOn = xr::HandInventoryOn();
    if (f.keyOn && nowMs >= nextLevel) { nextLevel = nowMs + 250; level = g_inWorld.load() ? SewerLevel(SewerHero()) : 0; }
    f.f4 = viewgate::F4() && xr::Running() && FullBody() && !ThirdPerson();
    f.panel = MenuAs3d();
    f.laser = xr::LaserOn();
    f.inWorld = g_inWorld.load();
    f.level = level;
    f.menu = gamestate::MenuOpen();
    f.inventoryOnly = gamestate::InventoryOnly();
    f.chat = ChatOpen();
    const handinv::Off away = handinv::Away(f), force = handinv::InForce(f);
    const bool up = force == handinv::Off::None;
    xr::SetHandInventory(up);
    directinput::SetHandInventoryAway(away == handinv::Off::None);
    const bool walk = up && StickHookIn();
    g_handWalk.store(walk);
    if (up) {   // the cursor's item, for the laser's drop off the panel (xr.cpp)
        static ULONGLONG nextCursor = 0;
        if (nowMs >= nextCursor) { nextCursor = nowMs + 100; gamestate::PollCursor(); }
    }
    static handinv::Off told = handinv::Off::Count;
    static int tellings = 0;
    if (f.keyOn && force != told && (f.menu || force == handinv::Off::None || told == handinv::Off::None)) {   // (said with a menu open, not every area change)
        told = force;
        if (++tellings <= 40 || tellings % 50 == 0)
            LogF("vrcam: hand inventory - %s (area %d%s)%s", handinv::OffText(force), level, level > 0 && clientverdict::IsTown(level) ? ", a town" : "",
                 up && !walk ? " - the walking stick hook is not in ([input] direct_walk=0?): the panel on the hand, the stick stays the menu's" : "");
    }
    {   // the stick's own walk while the game's is locked (handinv::WalkStep), once a game frame
        static ULONGLONG nextWalk = 0, lastGameRead = 0;
        static uint32_t readsSeen = 0;
        static bool walking = false, faultTold = false;
        if (const uint32_t r = g_handWalkReads.load(); r != readsSeen) { readsSeen = r; lastGameRead = nowMs; }
        if (g_hwFault.load()) {
            if (!faultTold) { faultTold = true; Log("vrcam: hand inventory - the stick's walk command faulted: off for this session (the game's own lock holds the hero)"); }
        } else if (nowMs >= nextWalk) {
            nextWalk = nowMs + handinv::kWalkEveryMs;
            float x = 0.0f, y = 0.0f;
            const bool pushed = walk && BodyWalkWalk(&x, &y);   // the stick (and BodyWalk's walk) as the game takes it, turned by our camera
            const handinv::Send s = handinv::WalkStep(walk, pushed, lastGameRead && nowMs - lastGameRead < handinv::kWalkEveryMs * 5, walking);
            if (s != handinv::Send::Nothing) {
                if (s == handinv::Send::Walk) {
                    float g[2];
                    handinv::ScreenToGrid(x, y, &g[0], &g[1]);
                    uint64_t bits = 0;
                    memcpy(&bits, g, sizeof bits);
                    g_hwGrid.store(bits);
                }
                g_hwSend.store((int)s);
                if (gamestate::QueueHandWalk(&HandWalkOnUi)) walking = s == handinv::Send::Walk;
            }
        }
    }
    // the evidence: the game's walk with the inventory on the hand
    static ULONGLONG since = 0;
    static uint32_t readsAt = 0;
    static float from[3] = {};
    static bool held = false, fromOk = false;
    static int said = 0;
    if (!walk || !xr::HandInventoryHung()) { since = 0; held = false; return; }
    directinput::PadOut d{};
    BWPadMirror bw{};
    const bool pushed = (directinput::PadRead(&d) && walkpath::PushedRaw(d.lx, d.ly)) ||   // the left stick, or BodyWalk's body walking mixed in
                        (directinput::BodyWalkWalks() && BodyWalkReport(&bw, true) && walkpath::PushedRaw(bw.thumb_lx, bw.thumb_ly));
    float at[3] = {};
    AcquireSRWLockShared(&g_lookLock);
    const bool atOk = g_lookOk;
    memcpy(at, g_lookAt, sizeof at);
    ReleaseSRWLockShared(&g_lookLock);
    static uint32_t sentAt = 0;
    if (!since) { since = nowMs; readsAt = g_handWalkReads.load(); sentAt = g_hwSent.load(); memcpy(from, at, sizeof from); fromOk = atOk; held = false; }
    held = held || pushed;
    if (nowMs - since < 1000) return;
    const uint32_t reads = g_handWalkReads.load() - readsAt;
    const float dx = at[0] - from[0], dz = at[2] - from[2];
    const float cells = atOk && fromOk ? sqrtf(dx * dx + dz * dz) / pointcast::kUnitsPerCell : 0.0f;
    const uint32_t sent = g_hwSent.load() - sentAt;
    const handinv::Walk v = handinv::Verdict(held, reads, cells, sent);
    static handinv::Walk lastV = handinv::Walk::NotAsked;
    if (v != handinv::Walk::NotAsked && (v != lastV || said < 6) && said < 60) {
        ++said;
        LogF("vrcam: hand inventory - %s: the walk read its stick %u times in 1 s, the stick's command sent %u times, the hero went %.1f cells",
             handinv::WalkText(v), reads, sent, cells);
    }
    if (v != handinv::Walk::NotAsked) lastV = v;
    since = 0;   // the next second
}

// VR F2 with mouse look: the crosshair's depth, from [hud_third] crosshair_depth
// looking ahead to crosshair_depth_down looking straight down, by the sine of the
// camera's look down - the ground in the middle of the screen comes nearer as the
// view tips down, and is far off looking ahead. Every tick (the head
// moves), sent only when it changed: hud's lock is the render thread's too.
bool CrosshairDepthNow() {
    return g_set.platform.load() == 1 && ViewNow() == 2 && KeyMoveMode() && ShooterActive() && g_set.crosshairOn.load();
}
void CrosshairDepthTick() {
    static float sent = 1e9f;
    if (!CrosshairDepthNow()) { sent = 1e9f; return; }
    const float down = std::clamp(-g_viewFwdY.load(), 0.0f, 1.0f);
    const float ahead = g_set.crosshairDepth.load();
    const float d = (ahead + (g_set.crosshairDepthDown.load() - ahead) * down) * 0.005f;
    if (fabsf(d - sent) < 2e-4f) return;
    sent = d;
    hud::SetPointerDepth(d, 0.0f, 0.0f, 1.0f);
}

}  // namespace vrc
