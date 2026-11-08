// vr/input/flat_click.cpp - cut from vr/vrcam.cpp (docs/plan_modules.md M10); what it gives: vr/input/input.h. It sees vrcam.cpp's headers.
#include "../vrcam_headers.h"
#include "../death_ragdoll.h"   // UnitRowFlags: a monster's MonStats row flags (an NPC: [input] vr_body_pick_hand)

using namespace D2RL;
using namespace vrc;   // as in vrcam.cpp

namespace vrc {

// 0xFE3B0(unit, type, x, y, flags) - the game's click on the map (move_recon.md
// §5): type 0/1 the left button pressed/held, 3/4 the right; the 5th argument
// is 0x8AFD0's word, its low byte "Stand Still" (-> 0x20, attack in place),
// the high byte Run. Inside, the target is 0xF1900(0x9A820(unit)) - the unit
// under the pointer, null on bare ground - and a click with no target and no
// Stand Still is a walk there (0x101EF0). Flat with mouse look: the pointer
// is the crosshair, so with no target the click gets Stand Still - the skill
// goes off at the crosshair (an arrow, a swing, a spell) and the hero stays;
// with a target, the game's own click (attack, pick up, door, NPC).
// mov [rsp+0x10], rbx; push rbp/rsi/rdi/r12..r15 (no rip, no rel32)
const uint8_t kSigMapClick[16] = {0x48,0x89,0x5C,0x24,0x10,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57};
const uint8_t kSigClientIndex[15] = {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x20,0x57,0x48,0x83,0xEC,0x20};
const uint8_t kSigHoverUnit[12] = {0x40,0x53,0x55,0x56,0x48,0x83,0xEC,0x30,0x8B,0xF1,0x8B,0xD9};
MapClickFn OrigMapClick;
std::atomic<int> g_mapClick{0};   // 0 not in yet, 1 in, 2 not possible
std::atomic<bool> g_hoverHooked{false};   // spell_aim's hook is on HoverUnit (its bytes checked then): no longer as they were

// [input] flat_aim_ground (vr/flat_ground.h, docs/notes/flat_targeting.md section 5): 0x7E7CE0(physics manager, float* x, float* y, bcVec2 pixel) is
// the game's mouse pixel -> the ground's cell: RayCastMouseClosest 0x7E7E50(manager, out, filter, pixel, world 1) along the pointer's ray (our
// camera's, through d2rcam's ScreenToRay), then hit.x / 2, hit.z / 2 - no height taken off, exact only along the game's own view (raised ground is
// drawn sheared along it). Its two callers are the game's mouse point in iso pixels (0x8F270: the hover, the click) and on the grid (0x8F3D0: a
// skill's point), so everything the mouse aims follows it. Flat with our camera: the same ray goes into the game's floor world (world 0, with the
// filter of its "floor under a cell" query 0x6B0350: the qword at [0x685750() + 0x18] + 0x1E60) and the cell under that point is the answer. Nothing else.
constexpr uint64_t RVA_MOUSE_GROUND = 0x7E7CE0, RVA_RAYCAST_MOUSE = 0x7E7E50, RVA_SCENE = 0x685750;
// push rbx; push rdi; sub rsp, 98h (then a rip load: not in the pattern)
const uint8_t kSigMouseGround[10] = {0x40,0x53,0x57,0x48,0x81,0xEC,0x98,0x00,0x00,0x00};
// mov r11, rsp; push rbp/rbx/rsi/rdi/r14/r15; lea rbp, [r11-38h]; sub rsp, 108h
const uint8_t kSigRaycastMouse[22] = {0x4C,0x8B,0xDC,0x55,0x53,0x56,0x57,0x41,0x56,0x41,0x57,0x49,0x8D,0x6B,0xC8,0x48,0x81,0xEC,0x08,0x01,0x00,0x00};
// sub rsp, 28h; mov rax, gs:[58h] (the scene's thread-safe static)
const uint8_t kSigScene[13] = {0x48,0x83,0xEC,0x28,0x65,0x48,0x8B,0x04,0x25,0x58,0x00,0x00,0x00};
using MouseGroundFn = bool (*)(void* mgr, float* x, float* y, uint64_t pixel);
using RaycastMouseFn = bool (*)(void* mgr, void* out, const uint64_t* filter, uint64_t pixel, uint64_t world);
using SceneFn = uint8_t* (*)();
MouseGroundFn OrigMouseGround = nullptr;
std::atomic<int> g_mouseGround{0};   // 0 not in yet, 1 in, 2 not possible
constexpr size_t kMouseFilter = 0x128, kSceneObj = 0x18, kFloorFilter = 0x1E60;   // the manager's mouse filter, the scene, its floor query's filter (world 0)

// The last answer, for [debug] flat_pick's click lines (the game's thread writes, the click hook on it reads).
struct MouseGroundSeen { flatground::Pick pick; float floorY = 0.0f; bool world1 = false, world1Hit = false; float world1Y = 0.0f; ULONGLONG at = 0; };
MouseGroundSeen g_mouseGroundSeen;
SRWLOCK g_mouseGroundLock = SRWLOCK_INIT;

// The game's RayCastMouseClosest along the pointer's ray in one world, the hit's point; false: none, or it faulted.
bool MouseRayHit(void* mgr, const uint64_t* filter, uint64_t pixel, uint64_t world, float hit[3]) noexcept {
    const uintptr_t fn = d2rsig::Addr(RVA_RAYCAST_MOUSE);
    if (!fn) return false;
    alignas(16) uint8_t out[0x80] = {};   // dmWorldRayCastOutput: the point first (0x7E7CE0 keeps 0x40 for it)
    __try {
        if (!((RaycastMouseFn)fn)(mgr, out, filter, pixel, world)) return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    memcpy(hit, out, 3 * sizeof(float));
    return flatground::Finite3(hit);
}
// The filter of the game's "floor under a cell" query (0x6B0350 through 0x79F010: the scene's +0x1E60, world 0).
bool FloorFilter(uint64_t* f) noexcept {
    const uintptr_t fn = d2rsig::Addr(RVA_SCENE);
    if (!fn) return false;
    uint8_t* s = nullptr;
    __try { s = ((SceneFn)fn)(); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    uintptr_t scene = 0;
    return s && SafeRead(&scene, s + kSceneObj, sizeof scene) && scene && SafeRead(f, (const void*)(scene + kFloorFilter), sizeof *f);
}

// [input] flat_body_pick (vr/flat_body.h, from sajmonsoft's flat fork): the monsters, NPCs and objects round the hero as upright capsules,
// read again at most ten times a second on the game's thread (the mouse's point asks every frame). The client's unit hash (unit_scan.h,
// the walk hit by touch uses), a body's size from its MonStats row (0x975B0), an object's from its Objects.txt id (flat_pick_obj.inc).
// [input] vr_body_pick / vr_body_pick_hand use the same list in VR (vr/vr_body_pick.h), from the mouse's point and the controller's lookups.
namespace flatobj {
#include "../flat_pick_obj.inc"
}
struct FlatBodies { flatbody::Cap c[flatbody::kBodyCap + flatbody::kObjCap]; int n = 0; ULONGLONG at = 0; float floorY = 0.0f; };
FlatBodies g_flatBodies;   // under g_flatBodiesLock (the mouse's point and the controller's lookups ask it)
SRWLOCK g_flatBodiesLock = SRWLOCK_INIT;
bool FlatStatsRow(uint8_t bank, uint32_t cls, uint8_t f[16]) noexcept {
    const uintptr_t fn = d2rsig::Addr(RVA_FLAT_MONSTATS);
    uintptr_t row = 0;
    if (!fn || cls > 4095 || bank > 3) return false;
    __try { row = ((uintptr_t (*)(uint8_t, int))fn)(bank, (int)cls); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return row >= 0x10000 && SafeRead(f, (const void*)row, 16);
}
void FlatBodyScan() {
    FlatBodies& s = g_flatBodies;
    const ULONGLONG now = GetTickCount64();
    if (now - s.at < 100) return;
    s.at = now;
    s.n = 0;
    float look[3];
    const uintptr_t table = d2rsig::Addr(RVA_FLAT_UNITS);
    if (!table || !LookAtNow(look)) return;
    // the look-at is the hero as drawn: his place on the grid is it less its height's push (flat_ground.h); every unit on his floor
    const float floorY = look[1], gx = look[0] - floorY * flatground::kShear, gz = look[2] - floorY * flatground::kShear;
    s.floorY = floorY;
    auto read = [](void* d, uintptr_t src, size_t n) { return src >= 0x10000 && SafeRead(d, (const void*)src, n); };
    auto readv = [](uintptr_t src, auto& v) { return src >= 0x10000 && SafeRead(&v, (const void*)src, sizeof v); };
    struct RowMemo { uint32_t key = ~0u; uint8_t f[3] = {}; bool ok = false, npc = false; };
    static RowMemo rows[512];
    // keep the nearest `cap` of a kind in [base, base + count)
    auto keep = [&](int base, int& count, int cap, const flatbody::Cap& c) {
        const float d2 = (c.x - gx) * (c.x - gx) + (c.z - gz) * (c.z - gz);
        if (d2 > flatbody::kReach * flatbody::kReach) return;
        if (count < cap) { s.c[base + count++] = c; return; }
        int farthest = base;
        float farD = -1.0f;
        for (int i = base; i < base + count; ++i) {
            const float e = (s.c[i].x - gx) * (s.c[i].x - gx) + (s.c[i].z - gz) * (s.c[i].z - gz);
            if (e > farD) { farD = e; farthest = i; }
        }
        if (d2 < farD) s.c[farthest] = c;
    };
    int bodies = 0, objects = 0;
    unitscan::EachUnit(read, table, unitscan::kMonster, [&](uintptr_t at, const unitscan::Unit& u) {
        const uint32_t mode = unitscan::Field32(u, unitscan::kOffMode);
        if (mode == 0 || mode == 12) return true;   // dying, dead
        flatbody::Cap c;
        if (!unitscan::Place(read, u, &c.x, &c.z)) return true;
        c.cls = unitscan::Field32(u, unitscan::kOffClass);
        c.id = unitscan::Field32(u, unitscan::kOffId);
        const uint8_t bank = u.b[unitscan::kOffBank];
        RowMemo& m = rows[(c.cls * 3u + bank) & 511u];
        const uint32_t key = (uint32_t)bank << 16 | (c.cls & 0xFFFF);
        if (m.key != key) {
            uint8_t f[16];
            m.key = key;
            m.ok = FlatStatsRow(bank, c.cls, f);
            if (m.ok) { m.f[0] = f[8]; m.f[1] = f[9]; m.f[2] = f[13]; }
            uint32_t flags = 0;   // its MonStats row's flags (not the size row above, death_ragdoll.h): an NPC is one to talk to
            m.npc = deathragdoll::UnitRowFlags(at, readv, &flags) && (flags & deathragdoll::kRowNpc);
        }
        flatbody::BodySize(m.f[0], m.f[1], m.f[2], m.ok, &c.height, &c.radius);
        c.npc = m.npc;
        c.floorY = floorY;
        keep(0, bodies, flatbody::kBodyCap, c);
        return true;
    });
    unitscan::EachUnit(read, table, 2, [&](uintptr_t, const unitscan::Unit& u) {
        const uint32_t cls = unitscan::Field32(u, unitscan::kOffClass);
        if (cls >= (uint32_t)flatobj::kObjN) return true;
        const uint8_t packed = flatobj::kObj[cls];
        const int kind = flatbody::ObjKind(packed);
        if (!flatbody::ObjTarget(kind, unitscan::Field32(u, unitscan::kOffMode))) return true;
        uint32_t tile[2] = {};   // an object's place: its static path's tile (as the torches' lights)
        if (!read(tile, unitscan::Field64(u, unitscan::kOffPath) + 0x10, sizeof tile) || tile[0] > 200000u || tile[1] > 200000u) return true;
        flatbody::Cap c;
        c.x = 2.0f * tile[0] + 1.0f;
        c.z = 2.0f * tile[1] + 1.0f;
        c.floorY = floorY;
        c.height = flatbody::ObjHeight(kind);
        c.radius = flatbody::ObjRadius(packed);
        c.body = false;
        c.cls = cls;
        c.id = unitscan::Field32(u, unitscan::kOffId);
        keep(flatbody::kBodyCap, objects, flatbody::kObjCap, c);
        return true;
    });
    // one list: the bodies, then the objects
    for (int i = 0; i < objects; ++i) s.c[bodies + i] = s.c[flatbody::kBodyCap + i];
    s.n = bodies + objects;
}

// The first body or object the ray meets before tFloor (flat_body.h Pick: the cell its body stands for), the list read again first when old.
// *met: that capsule; *n: how many stood round the hero.
bool BodyCell(const float o[3], const float d[3], float tFloor, flatbody::Hit* hit, flatbody::Cap* met, int* n) {
    AcquireSRWLockExclusive(&g_flatBodiesLock);
    FlatBodyScan();
    const bool ok = g_flatBodies.n > 0 && flatbody::Pick(g_flatBodies.c, g_flatBodies.n, o, d, tFloor, hit);
    if (ok) *met = g_flatBodies.c[hit->index];
    *n = g_flatBodies.n;
    ReleaseSRWLockExclusive(&g_flatBodiesLock);
    return ok;
}

// [input] portal_auto (vr/portal_auto.h, from sajmonsoft's flat fork): while FlatPortalFrame aims at a portal, the game's mouse point is its place
// (the cell, half its world units), and the game's own hover - what the point before this one found - is read here, on the mouse code's
// thread: a portal under it lets the click go down. The local player's hover: HoverUnit(ClientIndex(hero)), as the map click reads it.
std::atomic<bool> g_portalForce{false}, g_portalHover{false};
std::atomic<float> g_portalForceX{0.0f}, g_portalForceZ{0.0f};
void* PortalHeroUnit();   // (below, with HeroAnimMode's unit)
bool PortalClass(uint32_t cls) { return cls < (uint32_t)flatobj::kObjN && flatbody::ObjKind(flatobj::kObj[cls]) == 4; }
bool PortalUnderPointer() {
    void* hero = PortalHeroUnit();
    if (!hero || g_mapClick.load() != 1) return false;   // (the map click's hook in: ClientIndex's and HoverUnit's bytes checked)
    const int idx = ((ClientIndexFn)d2rsig::Addr(RVA_CLIENT_INDEX))(hero);
    void* hover = idx >= 0 && idx < 8 ? ((HoverUnitFn)d2rsig::Addr(RVA_HOVER_UNIT))(idx) : nullptr;
    uint32_t tc[2] = {};
    return hover && SafeRead(tc, hover, sizeof tc) && tc[0] == 2 && PortalClass(tc[1]);
}

// [input] vr_body_pick (vr/vr_body_pick.h): VR, the game's mouse point - F1 - F3's pointer and crosshair, F4's shift + click along the hand ([bow]
// mode 2) - by a body too, along the very ray the game's mouse code was just handed (d2rcam's LastPick says whose: our camera through the pixel, or
// the aim's ray override; the game's own camera, nothing). The ground the ray meets: the game's floor world, as flat's. Nothing else of flat's here.
bool VrMouseGround(void* mgr, float* x, float* y, uint64_t pixel, bool gameOk) {
    if (!x || !y || !mgr || !g_enabled.load() || !g_set.vrBodyPick.load()) return gameOk;
    float px[2];
    memcpy(px, &pixel, sizeof px);
    vrbody::MouseAsk a;
    a.platform = 1;
    a.view = viewgate::Number();
    a.vrKey = true;
    a.menu = gamestate::MenuOpen();
    d2rcam::LastPickInfo li{};
    if (d2rcam::LastPick(&li)) {
        a.pickSeen = vrbody::SamePick(px[0], px[1], li.px, li.py, GetTickCount64() - li.at);
        a.pickBy = li.by;
    }
    const vrbody::Ray ray = vrbody::MouseRay(a);
    float o[3], d[3];
    if (ray == vrbody::Ray::Aim) {
        for (int i = 0; i < 3; ++i) { o[i] = g_aimO[i].load(); d[i] = g_aimD[i].load(); }
    } else if (ray != vrbody::Ray::Pixel || !d2rcam::PickRay(px[0], px[1], o, d)) {
        return gameOk;
    }
    uint64_t filter = 0;
    float floor[3] = {NAN, NAN, NAN};
    const bool floorOk = FloorFilter(&filter) && MouseRayHit(mgr, &filter, pixel, 0, floor);
    const float tFloor = floorOk ? (floor[0] - o[0]) * d[0] + (floor[1] - o[1]) * d[1] + (floor[2] - o[2]) * d[2] : INFINITY;
    flatbody::Hit hit;
    flatbody::Cap c;
    int n = 0;
    if (!BodyCell(o, d, tFloor, &hit, &c, &n)) return gameOk;
    *x = hit.cellX;
    *y = hit.cellZ;
    static ULONGLONG told = 0;
    if (GetTickCount64() - told > 10000) {
        told = GetTickCount64();
        LogF("vrcam: VR F%d: a %s (class %u) under the %s picked by its body, not its feet ([input] vr_body_pick; %d bodies and objects round the hero)",
             a.view, c.body ? (c.npc ? "NPC" : "monster") : "object", c.cls, ray == vrbody::Ray::Aim ? "hand's aim" : "pointer", n);
    }
    return true;
}

// [input] vr_body_pick_hand (vr/vr_body_pick.h): F4, what the aiming hand points at (HandRay) - the first body or object along it before the hero's
// floor as drawn. One pick every 30 ms at most (the interact target is asked every frame); the server's verdict asked of the one met only.
bool HandRay(float* o, float* d, float* dRaw = nullptr);   // (vr/input/aim.cpp)
uintptr_t VrHandTarget(vrbody::Ask ask) {
    vrbody::HandAsk h;
    h.key = g_set.vrBodyPickHand.load();
    h.f4 = h.key && viewgate::F4();
    h.on = g_enabled.load();
    h.menu = h.f4 && gamestate::MenuOpen();
    if (!vrbody::HandWanted(h)) return 0;
    struct Last { ULONGLONG at = 0; bool met = false; flatbody::Cap cap; int n = 0; };
    static Last last;   // the game's UI thread
    const ULONGLONG now = GetTickCount64();
    if (now - last.at >= 30) {
        last.at = now;
        last.met = false;
        float o[3], d[3];
        if (HandRay(o, d)) {
            AcquireSRWLockExclusive(&g_flatBodiesLock);
            FlatBodyScan();
            const int i = vrbody::HandMet(g_flatBodies.c, g_flatBodies.n, o, d, g_flatBodies.floorY);
            if (i >= 0) { last.cap = g_flatBodies.c[i]; last.met = true; }
            last.n = g_flatBodies.n;
            ReleaseSRWLockExclusive(&g_flatBodiesLock);
        }
    }
    if (!last.met) return 0;
    const flatbody::Cap& c = last.cap;
    const int verdict = c.body ? meleetouch::Hostile(c.id) : -1;
    if (!vrbody::Answers(ask, c, verdict)) return 0;
    const uintptr_t unit = c.body ? meleetouch::MonsterUnit(c.id) : meleetouch::ObjectUnit(c.id);   // (gone since: none - the game's own answer)
    static ULONGLONG told[2] = {};
    if (unit && now - told[(int)ask] > 3000) {
        told[(int)ask] = now;
        LogF("vrcam: F4: the %s goes to the %s the hand points at (class %u, id %u; [input] vr_body_pick_hand; %d bodies and objects round the hero)",
             ask == vrbody::Ask::Interact ? "interact" : "attack", c.body ? (c.npc ? "NPC" : "monster") : "object", c.cls, c.id, last.n);
    }
    return unit;
}

// [input] flat_body_target (vr/vr_body_pick.h): flat, what the crosshair's ray meets first answers the map click's own target lookup - the unit
// itself, as F4's hand answers the controller's (VrHandTarget), not flat_body_pick's cell, which left the choice to the game's hover boxes made for
// its view from above (up close: none found, the swing went at the floor behind the monster). The ray: the game's mouse pixel through our camera
// (d2rcam's last pick, ours and fresh; else the crosshair, the viewport's middle), against the bodies and objects round the hero (FlatBodyScan),
// before the hero's floor as drawn. One pick every 30 ms at most (the click and its lookup ask in the same moment).
uintptr_t FlatClickTarget(uintptr_t game) {
    vrbody::FlatAsk a;
    a.flat = g_set.platform.load() == 0;
    a.key = g_set.flatBodyTarget.load() && g_set.flatBodyPick.load();
    a.on = g_enabled.load() && g_inWorld.load();
    a.menu = a.flat && a.key && gamestate::MenuOpen();
    if (!vrbody::FlatWanted(a)) return 0;
    struct Last { ULONGLONG at = 0; bool met = false; flatbody::Cap cap; int n = 0; };
    static Last last;   // the game's thread (its map click)
    const ULONGLONG now = GetTickCount64();
    if (now - last.at >= 30) {
        last.at = now;
        last.met = false;
        d2rcam::LastPickInfo li{};
        const bool pickOk = d2rcam::LastPick(&li);
        float px[2], o[3], d[3];
        if (vrbody::FlatPixel(pickOk, li.by, pickOk ? now - li.at : ~0ull, li.px, li.py, li.vpW, li.vpH, ShooterActive(), px) &&
            d2rcam::PickRay(px[0], px[1], o, d)) {
            AcquireSRWLockExclusive(&g_flatBodiesLock);
            FlatBodyScan();
            const int i = vrbody::ClickMet(g_flatBodies.c, g_flatBodies.n, o, d, g_flatBodies.floorY);
            if (i >= 0) { last.cap = g_flatBodies.c[i]; last.met = true; }
            last.n = g_flatBodies.n;
            ReleaseSRWLockExclusive(&g_flatBodiesLock);
        }
    }
    if (!last.met) return 0;
    const flatbody::Cap& c = last.cap;
    uint32_t gameType = ~0u;
    const bool gameIsItem = game && SafeRead(&gameType, (const void*)game, sizeof gameType) && gameType == 4;
    const int verdict = c.body && !c.npc ? meleetouch::Hostile(c.id) : -1;
    if (!vrbody::ClickAnswers(c, verdict, gameIsItem)) return 0;
    const uintptr_t unit = c.body ? meleetouch::MonsterUnit(c.id) : meleetouch::ObjectUnit(c.id);   // (gone since: none - the game's own answer)
    static ULONGLONG told = 0;
    if (unit && now - told > 3000) {
        told = now;
        uint32_t gameHead[3] = {};
        const bool gameRead = game && SafeRead(gameHead, (const void*)game, sizeof gameHead);
        LogF("vrcam: flat: the click goes to the %s the crosshair's ray meets (class %u, id %u; the game's own hover: %s; [input] flat_body_target; %d bodies and objects round the hero)",
             c.body ? (c.npc ? "NPC" : "monster") : "object", c.cls, c.id,
             !game ? "none" : !gameRead ? "?" : gameHead[0] == (c.body ? 1u : 2u) && gameHead[2] == c.id ? "the same" : "another unit", last.n);
    }
    return unit;
}

// [input] point_pickup (vr/unleashed_loot_aim.h, MPL 2.0, D2R VR Unleashed's loot pick): F4, the dropped item the aiming hand points at (HandRay, the arm
// held out from the head) is the one interact takes - the game's own interact lookups are answered with it (InteractTarget and the Interact skill's
// target), then the game's own pick-up: its walk up, its checks, its command. The items on the floor round the hero: the client's unit hash, type 4, on
// the ground or dropping, each at its static path's cell (the middle, as the objects' and grab_items'), on the hero's floor as drawn; read again at most
// ten times a second. Handed over: the live unit of the chosen id now (the game's FindUnit, still on the floor), never a kept address.
struct FlatItems { unleashed::lootaim::Item it[unleashed::lootaim::MaxItems]; int n = 0; ULONGLONG at = 0; float floorY = 0.0f; };
FlatItems g_flatItems;   // the game's UI thread (its interact lookups)
void FlatItemScan() {
    namespace la = unleashed::lootaim;
    FlatItems& s = g_flatItems;
    const ULONGLONG now = GetTickCount64();
    if (now - s.at < 100) return;
    s.at = now;
    s.n = 0;
    float look[3];
    const uintptr_t table = d2rsig::Addr(RVA_FLAT_UNITS);
    if (!table || !LookAtNow(look)) return;
    const float floorY = look[1], gx = look[0] - floorY * flatground::kShear, gz = look[2] - floorY * flatground::kShear;
    s.floorY = floorY;
    auto read = [](void* d, uintptr_t src, size_t n) { return src >= 0x10000 && SafeRead(d, (const void*)src, n); };
    unitscan::EachUnit(read, table, 4, [&](uintptr_t, const unitscan::Unit& u) {
        if (!la::OnFloor(unitscan::Field32(u, unitscan::kOffType), unitscan::Field32(u, unitscan::kOffMode))) return true;
        uint32_t cell[2] = {};
        if (!read(cell, unitscan::Field64(u, unitscan::kOffPath) + 0x10, sizeof cell) || !cell[0] || !cell[1] || cell[0] > 200000u || cell[1] > 200000u) return true;
        const float x = la::CellUnits * cell[0] + 1.0f, z = la::CellUnits * cell[1] + 1.0f;
        const float heroCells = std::hypot(x - gx, z - gz) / la::CellUnits;
        if (!(heroCells <= la::ReachCells)) return true;
        la::Item& it = s.it[s.n++];
        it.id = unitscan::Field32(u, unitscan::kOffId);
        it.at = {x + floorY * flatground::kShear, floorY, z + floorY * flatground::kShear};
        it.heroCells = heroCells;
        return s.n < la::MaxItems;
    });
}

// [debug] point_pickup_ray (vr/render/effects/effects.cpp): the ray the pick used last, world units - the game's UI thread writes, the render thread reads
PointRayDebug g_pointRayDbg;
SRWLOCK g_pointRayDbgLock = SRWLOCK_INIT;
bool PointRayDebugNow(PointRayDebug* out) {
    AcquireSRWLockShared(&g_pointRayDbgLock);
    *out = g_pointRayDbg;
    ReleaseSRWLockShared(&g_pointRayDbgLock);
    return (out->hand[0].valid || out->hand[1].valid) && GetTickCount64() - out->at < 300;
}
uintptr_t PointPickupAnswer(uintptr_t game) {
    namespace la = unleashed::lootaim;
    if (!g_set.pointPickup.load() || !viewgate::F4() || !g_enabled.load() || gamestate::MenuOpen()) return game;
    if (itemgrabwire::Pressing() || meleetouch::BarrelPressTarget()) return game;   // a grip's pick-up or a swing through a barrel in flight: theirs
    // the trigger's pick-up (vr/point_pickup_wire.cpp: the pointing hand's grip held + its trigger) answers its item until it has left the floor
    if (const uint32_t press = pointpickup::PressId()) {
        if (const uintptr_t u = meleetouch::GroundItemUnit(press)) return u;
        pointpickup::PressEnd("the item left the floor (taken)");
    }
    struct Last { ULONGLONG at = 0; uint32_t id = 0; float cells = -1.0f, off = 0.0f; bool pointing = false; int n = 0, hand = -1; };
    static Last last;   // the game's UI thread
    const ULONGLONG now = GetTickCount64();
    // While A is held the press's pick stays (still on the floor): the hand moves as the button goes down, and a pick lost in the press's first
    // frames let A go ([input] interact_one_press: "the target changed during the press" - Serg's 0.198.1 log).
    const bool pressKept = g_interactHeld.load() && last.id && meleetouch::GroundItemUnit(last.id);
    if (!pressKept && now - last.at >= 30) {   // (the interact target is asked every frame)
        last.at = now;
        // Either hand points (Serg: "from either hand"): each its own ray, its own arm out, its own item kept (lootaim::Select); the lit pick is the
        // hand that has one - with both, the one that had it keeps it (pointtrigger::PickOwner). The arm held out from the head: [input]
        // point_pickup_arm_cm (Unleashed's 12 cm; let go 6 cm nearer).
        struct HandPick { bool pointing = false; uint32_t id = 0; float off = 0.0f; };
        static HandPick hp[2];   // 0 right, 1 left
        static int owner = -1;
        const float armM = g_set.pointPickupArmCm.load() * 0.01f;
        FlatItemScan();
        last.n = g_flatItems.n;
        PointRayDebug dbg;
        uint32_t ids[2] = {};
        float offs[2] = {};
        bool rayOk[2] = {};      // (for the log of a trigger that lit nothing: pointpickup::NoteScan, direct_input's order)
        float armOut[2] = {};
        for (int h = 0; h < 2; ++h) {
            la::Ray ray, raw;
            float o[3], d[3], reachM = 0.0f;
            int src = kRayNone;
            if (PointRay(h, o, d, &reachM, &src)) {
                rayOk[h == 0 ? 1 : 0] = true;
                armOut[h == 0 ? 1 : 0] = std::isfinite(reachM) ? reachM : 0.0f;
                hp[h].pointing = std::isfinite(reachM) && reachM >= (hp[h].pointing ? std::max(0.0f, armM - la::ArmReleaseM) : armM);
                raw = {{o[0], o[1], o[2]}, {d[0], d[1], d[2]}, true};
                if (hp[h].pointing) ray = raw;
                PointRayDebug::Hand& dh = dbg.hand[h];
                dh.valid = true; dh.pointing = hp[h].pointing; dh.src = src;
                for (int i = 0; i < 3; ++i) { dh.o[i] = o[i]; dh.d[i] = d[i]; }
            } else {
                hp[h].pointing = false;
            }
            if (raw.valid && !ray.valid) {   // the ray is on an item but the arm is not held out: said, so a test tells why nothing lights
                static ULONGLONG toldArm = 0;
                if (now - toldArm >= 3000)
                    if (const la::Choice r = la::Select(g_flatItems.it, g_flatItems.n, raw, 0); r.id) {
                        toldArm = now;
                        LogF("vrcam: F4: the %s hand's ray is on item %u but the arm is not held out (%.0f cm from the head, %.0f needed: [input] "
                             "point_pickup_arm_cm) - nothing highlighted", h == 0 ? "right" : "left", r.id, reachM * 100.0f, armM * 100.0f);
                    }
            }
            const la::Choice c = la::Select(g_flatItems.it, g_flatItems.n, ray, hp[h].id);
            hp[h].id = c.id;
            hp[h].off = c.distance;
            ids[h] = c.id;
            offs[h] = c.distance;
        }
        pointpickup::NoteScan(g_flatItems.n, rayOk, armOut, armM);
        owner = pointtrigger::PickOwner(ids, offs, owner);
        const uint32_t was = last.id;
        last.id = owner >= 0 ? ids[owner] : 0;
        last.off = owner >= 0 ? offs[owner] : 0.0f;
        last.pointing = owner >= 0;
        last.hand = owner == 0 ? 1 : owner == 1 ? 0 : -1;   // (direct_input's / haptics' order: 0 left, 1 right)
        last.cells = -1.0f;
        for (int i = 0; i < g_flatItems.n && last.id; ++i)
            if (g_flatItems.it[i].id == last.id) {
                last.cells = g_flatItems.it[i].heroCells;
                dbg.item = true; dbg.itemAt[0] = g_flatItems.it[i].at.x; dbg.itemAt[1] = g_flatItems.it[i].at.y; dbg.itemAt[2] = g_flatItems.it[i].at.z;
            }
        if (g_set.pointPickupRay.load()) {   // [debug] point_pickup_ray: what this choice used, for the drawing
            dbg.at = now;
            dbg.owner = owner;
            dbg.floorY = g_flatItems.floorY;
            AcquireSRWLockExclusive(&g_pointRayDbgLock);
            g_pointRayDbg = dbg;
            ReleaseSRWLockExclusive(&g_pointRayDbgLock);
        }
        static ULONGLONG told = 0;
        if (last.id && last.id != was && now - told >= 1000) {
            told = now;
            LogF("vrcam: F4: the %s hand points at item %u - highlighted; grip + trigger or A picks it up (%d items round the hero, %.2f cells off the "
                 "pointed spot, %.1f cells from the hero%s; [input] point_pickup)", owner == 0 ? "right" : "left", last.id, last.n, last.off / la::CellUnits,
                 last.cells, la::InReach(last.cells, g_set.pointPickupReach.load()) ? "" : ", past point_pickup_reach_cells: nothing is taken");
        }
    }
    const uintptr_t pick = last.id ? meleetouch::GroundItemUnit(last.id) : 0;   // (taken or gone since: none - the game's own answer)
    const bool inReach = la::InReach(last.cells, g_set.pointPickupReach.load());
    {   // the highlight (vr/point_pickup_wire.h): its name's rim in its rarity colour (item data +0x10: quality +0x00, flags +0x18), the tick, the trigger
        static uintptr_t toned = 0;
        static uint32_t tonedId = 0;
        static float rgb[3] = {1.0f, 1.0f, 1.0f};
        if (pick && (pick != toned || last.id != tonedId)) {
            toned = pick;
            tonedId = last.id;
            uintptr_t data = 0;
            uint32_t quality = 0, flags = 0;
            rgb[0] = rgb[1] = rgb[2] = 1.0f;
            if (SafeRead(&data, (const void*)(pick + 0x10), sizeof data) && data >= 0x10000 && SafeRead(&quality, (const void*)data, sizeof quality) &&
                SafeRead(&flags, (const void*)(data + 0x18), sizeof flags))
                la::RarityColor(quality, flags, rgb);
        }
        // a crossbow's hand (Serg: "the hand that holds the crossbow picks up only by A"): its trigger shoots (AimHand: the hand that holds it)
        const bool xbowHand = gamestate::WeaponClass() == D2RVR_WEAPON_CROSSBOW && last.hand == (AimHand() == 0 ? 1 : 0);
        pointpickup::Publish(pick ? last.id : 0, pick, last.hand, inReach, rgb, g_labelPaintIn.load() && g_labelSizeIn.load(), !xbowHand);
    }
    uint32_t type = ~0u;
    const bool gameIsItem = game && SafeRead(&type, (const void*)game, sizeof type) && type == 4;
    return la::Answer(game, gameIsItem, pick, inReach);
}

// [body] flat_aim_tilt_target (vr/aim_tilt.h): the point where the crosshair's ray went into a monster's body, and when - written by the mouse's point
// (the game's thread), read by PushArms (the update thread).
std::atomic<float> g_aimBodyW[3];
std::atomic<ULONGLONG> g_aimBodyAt{0};
void NoteFlatAimBody(const float o[3], const float d[3], float t) {
    const float p[3] = {o[0] + d[0] * t, o[1] + d[1] * t, o[2] + d[2] * t};
    if (!std::isfinite(p[0] + p[1] + p[2])) return;
    for (int i = 0; i < 3; ++i) g_aimBodyW[i].store(p[i], std::memory_order_relaxed);
    g_aimBodyAt.store(GetTickCount64(), std::memory_order_release);
}
bool FlatAimBody(float world[3], ULONGLONG maxAgeMs, int* ageMs) {
    const ULONGLONG at = g_aimBodyAt.load(std::memory_order_acquire), now = GetTickCount64();
    if (ageMs) *ageMs = at ? (int)std::min<ULONGLONG>(now - std::min(now, at), 999999) : -1;
    if (!at || now - std::min(now, at) > maxAgeMs) return false;
    for (int i = 0; i < 3; ++i) world[i] = g_aimBodyW[i].load(std::memory_order_relaxed);
    return true;
}

bool HookMouseGround(void* mgr, float* x, float* y, uint64_t pixel) {
    const bool gameOk = OrigMouseGround(mgr, x, y, pixel);
    if (g_portalForce.load(std::memory_order_acquire) && x && y && g_set.platform.load() == 0 && g_enabled.load()) {
        if (PortalUnderPointer()) g_portalHover.store(true, std::memory_order_release);
        *x = g_portalForceX.load() / flatground::kRenderPerCell;
        *y = g_portalForceZ.load() / flatground::kRenderPerCell;
        return true;
    }
    if (g_set.platform.load() == 1) return VrMouseGround(mgr, x, y, pixel, gameOk);   // [input] vr_body_pick
    // (unticked, [debug] flat_pick still works it out for its lines; only the tick hands it to the game)
    const bool apply = g_set.flatAimGround.load();
    const bool bodyPick = g_set.flatBodyPick.load() && !gamestate::MenuOpen();
    if (!x || !y || !mgr || !(apply || bodyPick || g_set.flatPickLog.load()) || g_set.platform.load() != 0 || !g_enabled.load()) return gameOk;
    uint64_t filter = 0;
    float floor[3] = {NAN, NAN, NAN};
    const bool floorOk = FloorFilter(&filter) && MouseRayHit(mgr, &filter, pixel, 0, floor);
    const float game[3] = {gameOk ? *x * flatground::kRenderPerCell : NAN, 0.0f, gameOk ? *y * flatground::kRenderPerCell : NAN};
    const flatground::Pick p = flatground::Choose(gameOk, game, floorOk, floor);
    if (apply && p.ours) { *x = p.gx; *y = p.gz; }
    if (bodyPick) {   // [input] flat_body_pick: a body or an object met before the ground - the cell its body stands for goes to the game
        float px[2], o[3], d[3];
        memcpy(px, &pixel, sizeof px);
        if (d2rcam::PickRay(px[0], px[1], o, d)) {
            const float tFloor = floorOk ? (floor[0] - o[0]) * d[0] + (floor[1] - o[1]) * d[1] + (floor[2] - o[2]) * d[2] : INFINITY;
            flatbody::Hit hit;
            flatbody::Cap c;
            int n = 0;
            if (BodyCell(o, d, tFloor, &hit, &c, &n)) {
                *x = hit.cellX;
                *y = hit.cellZ;
                if (c.body && !c.npc) NoteFlatAimBody(o, d, hit.t);   // [body] flat_aim_tilt_target: the swing's arms come to this point
                static bool told = false;
                if (!told) {
                    told = true;
                    LogF("vrcam: flat: a %s (class %u) picked by its body, not its feet ([input] flat_body_pick; %d bodies and objects round the hero)",
                         c.body ? "monster or NPC" : "object", c.cls, n);
                }
                return true;
            }
        }
    }
    {   // once: that it acts, and the first time it moves the point by over a cell
        static int told = 0;
        if (told == 0 && floorOk && apply) { told = 1; Log("vrcam: flat: the mouse's point is the ground drawn under it - raised floors too ([input] flat_aim_ground)"); }
        if (told == 1 && p.ours && p.offCells > 1.0f) {
            told = 2;
            LogF("vrcam: flat: on a floor %.1f up the game's own point was %.1f cells off - the drawn ground's cell went to the game instead", floor[1], p.offCells);
        }
    }
    // [debug] flat_pick: and where the game's own world (1) met the ray - at the base height (0) if the shear is what its own view needs
    MouseGroundSeen s;
    s.pick = p;
    s.floorY = floorOk ? floor[1] : NAN;
    s.at = GetTickCount64();
    if (g_set.flatPickLog.load()) {
        uint64_t mouseFilter = 0;
        float w1[3];
        s.world1 = SafeRead(&mouseFilter, (const uint8_t*)mgr + kMouseFilter, sizeof mouseFilter);
        s.world1Hit = s.world1 && MouseRayHit(mgr, &mouseFilter, pixel, 1, w1);
        s.world1Y = s.world1Hit ? w1[1] : NAN;
    }
    AcquireSRWLockExclusive(&g_mouseGroundLock);
    g_mouseGroundSeen = s;
    ReleaseSRWLockExclusive(&g_mouseGroundLock);
    return apply ? p.ok : gameOk;
}

// Flat only, with the camera's own hooks; tried twice a second until the bytes are there (the physics code may decrypt late).
void InstallMouseGroundHook() {
    static int misses = 0;
    // flat: [input] flat_aim_ground; VR: [input] vr_body_pick, once it is ticked
    if (!g_ctx || g_mouseGround.load() != 0 || g_set.platform.load() > 1 || (g_set.platform.load() == 1 && !g_set.vrBodyPick.load())) return;
    if (!Matches(RVA_MOUSE_GROUND, kSigMouseGround, sizeof kSigMouseGround) || !Matches(RVA_RAYCAST_MOUSE, kSigRaycastMouse, sizeof kSigRaycastMouse) ||
        !Matches(RVA_SCENE, kSigScene, sizeof kSigScene)) {
        if (g_inWorld.load() && ++misses >= 20) {
            g_mouseGround.store(2);
            Log("vrcam: flat: mouse ground hook NOT possible (0x7E7CE0 / 0x7E7E50 / 0x685750 not as expected - another build?) - on raised ground the aim "
                "is the game's own (off from first person)");
        }
        return;
    }
    const bool in = d2rsig::Hook(RVA_MOUSE_GROUND, kSigMouseGround, sizeof kSigMouseGround, (void*)&HookMouseGround, (void**)&OrigMouseGround);
    g_mouseGround.store(in ? 1 : 2);
    if (g_set.platform.load() == 1)
        Log(in ? "vrcam: VR: mouse ground hook in - the pointer, the crosshair and the hand's shift + click aim pick a monster, an NPC or an object by its body ([input] vr_body_pick)"
               : "vrcam: VR: mouse ground hook FAILED - units are picked by their feet, as the game has it");
    else
        Log(in ? "vrcam: flat: mouse ground hook in - the mouse's point follows the ground drawn under the crosshair ([input] flat_aim_ground)"
               : "vrcam: flat: mouse ground hook FAILED - on raised ground the aim is the game's own (off from first person)");
}

// [input] flat_portal_no_oval / [render] portal_no_oval (vr/portal_oval.h, from sajmonsoft's flat fork): 0xFA1700(render mesh, model path, file,
// Granny mesh, pass) builds a render mesh's materials, once, when its model loads. A town portal's black oval seen there is remembered; its
// counts are 0 while the view wants it hidden - every view through our own camera but F1 (from above, the game's own camera there), flat
// with its tick, VR with its own (the author's cave, 2026-10-28: the oval stood beside the turning ring in VR too) - and its own again
// otherwise (PortalOvalTick). The hook goes in only once the tick of this platform is on.
constexpr uint64_t RVA_MESH_MATERIALS = 0xFA1700;
// mov rax, rsp; mov [rax+18h], r8; mov [rax+10h], rdx; push rbp; push r12; push r14; sub rsp, 70h
const uint8_t kSigMeshMaterials[20] = {0x48,0x8B,0xC4,0x4C,0x89,0x40,0x18,0x48,0x89,0x50,0x10,0x55,0x41,0x54,0x41,0x56,0x48,0x83,0xEC,0x70};
using MeshMaterialsFn = void (*)(void* rm, const char* model, void* file, void* mesh, uint32_t pass);
MeshMaterialsFn OrigMeshMaterials = nullptr;
std::atomic<int> g_meshMaterials{0};   // 0 not in yet, 1 in, 2 not possible
std::atomic<bool> g_ovalHidden{false};   // what the remembered ovals are to be now
SRWLOCK g_ovalLock = SRWLOCK_INIT;
portaloval::Entry g_ovals[16];
int g_ovalN = 0;

// The three counts and the record's head of a render mesh now; false: they do not read.
bool OvalRead(uintptr_t rm, portaloval::Counts* c, uint8_t head[portaloval::kHeadBytes]) {
    return SafeRead(&c->tri, (const void*)(rm + portaloval::kOffTri), 4) && SafeRead(&c->vert, (const void*)(rm + portaloval::kOffVert), 4) &&
           SafeRead(&c->sub, (const void*)(rm + portaloval::kOffSub), 4) && SafeRead(head, (const void*)rm, portaloval::kHeadBytes);
}
// One remembered oval put as `hide` asks, if it is still ours (portal_oval.h WriteNow). Under g_ovalLock.
bool OvalApply(const portaloval::Entry& e, bool hide) {
    portaloval::Counts now, out;
    uint8_t head[portaloval::kHeadBytes];
    if (!OvalRead(e.rm, &now, head) || !portaloval::WriteNow(e, now, head, hide, &out)) return false;
    return SafeWriteMem((void*)(e.rm + portaloval::kOffTri), &out.tri, 4) && SafeWriteMem((void*)(e.rm + portaloval::kOffVert), &out.vert, 4) &&
           SafeWriteMem((void*)(e.rm + portaloval::kOffSub), &out.sub, 4);
}

void HookMeshMaterials(void* rm, const char* model, void* file, void* mesh, uint32_t pass) {
    OrigMeshMaterials(rm, model, file, mesh, pass);
    char path[300] = {};
    for (size_t i = 0; i + 1 < sizeof path; ++i)   // the model's path, a byte at a time (never past its end into the next page)
        if (!SafeRead(&path[i], model + i, 1) || !path[i]) break;
    if (!rm || !portaloval::IsOvalPath(path)) return;
    portaloval::Entry e;
    e.rm = (uintptr_t)rm;
    if (!OvalRead(e.rm, &e.own, e.head) || !portaloval::Plausible(e.own)) {
        LogF("vrcam: the town portal's oval %s - its render mesh's counts are not a mesh's (%u, %u, %u): left as the game draws it", path,
             e.own.tri, e.own.vert, e.own.sub);
        return;
    }
    AcquireSRWLockExclusive(&g_ovalLock);
    int at = 0;
    while (at < g_ovalN && g_ovals[at].rm != e.rm) ++at;
    if (at == g_ovalN && g_ovalN < (int)std::size(g_ovals)) ++g_ovalN;
    if (at < (int)std::size(g_ovals)) {
        g_ovals[at] = e;
        OvalApply(e, g_ovalHidden.load());
    }
    ReleaseSRWLockExclusive(&g_ovalLock);
    static int told = 0;
    if (told++ < 6) LogF("vrcam: the town portal's oval %s (%u triangles) %s (%s)", path, e.own.tri,
                         g_ovalHidden.load() ? "not drawn" : "seen, drawn while it is not wanted hidden",
                         g_set.platform.load() == 0 ? "[input] flat_portal_no_oval" : "[render] portal_no_oval");
}

// The update thread: the hook in (once this platform's tick is on), and the remembered ovals hidden or shown as the view asks - F2 and up
// with the tick hide them (flat F2 / F3, VR F2 / F3 / F4); F1, the tick off show them.
void PortalOvalTick() {
    const bool wanted = g_set.platform.load() == 0 ? g_set.flatPortalNoOval.load() : g_set.portalNoOval.load();
    if (g_ctx && wanted && g_meshMaterials.load() == 0) {
        static int misses = 0;
        if (Matches(RVA_MESH_MATERIALS, kSigMeshMaterials, sizeof kSigMeshMaterials)) {
            const bool in = d2rsig::Hook(RVA_MESH_MATERIALS, kSigMeshMaterials, sizeof kSigMeshMaterials, (void*)&HookMeshMaterials, (void**)&OrigMeshMaterials);
            g_meshMaterials.store(in ? 1 : 2);
            Log(in ? "vrcam: render mesh hook in - the town portal's black oval is not drawn from F2 on (portals seen from now on; [input] flat_portal_no_oval, [render] portal_no_oval)"
                   : "vrcam: render mesh hook FAILED - the town portal's oval stays");
        } else if (g_inWorld.load() && ++misses >= 20) {
            g_meshMaterials.store(2);
            Log("vrcam: render mesh hook NOT possible (0xFA1700 not as expected - another build?) - the town portal's oval stays");
        }
    }
    const bool hide = wanted && g_enabled.load() && ViewNow() != 1;
    if (g_ovalHidden.exchange(hide) == hide) return;
    AcquireSRWLockExclusive(&g_ovalLock);
    int done = 0;
    for (int i = 0; i < g_ovalN; ++i) done += OvalApply(g_ovals[i], hide) ? 1 : 0;
    const int n = g_ovalN;
    ReleaseSRWLockExclusive(&g_ovalLock);
    if (n) LogF("vrcam: the town portal's oval %s (%d of %d remembered)", hide ? "hidden" : "drawn again", done, n);
}

// [debug] flat_pick (docs/notes/flat_targeting.md): a press of a mouse button with the flat mouse look on - what the game has under the
// crosshair (its own HoverUnit, the click's target), the pixel its mouse code last asked a ray for against its viewport's middle, where
// that ray meets the hero's ground and how steeply, and the window's state. Read only; at most 4 lines a second.
extern HWND g_gameWnd;
void FlatPickClick(void* unit, int type, int x, int y, uint64_t flags) {
    static ULONGLONG lastLog = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - lastLog < 250) return;
    lastLog = now;
    const int idx = ((ClientIndexFn)d2rsig::Addr(RVA_CLIENT_INDEX))(unit);
    void* hover = idx >= 0 && idx < 8 ? ((HoverUnitFn)d2rsig::Addr(RVA_HOVER_UNIT))(idx) : nullptr;
    char under[220];
    shottrace::UnitText(hover, g_heroUnit.load(), under, sizeof under);
    char pick[200] = "the game asked no mouse ray yet", ground[120] = "the ray through it: not known (no camera this frame)";
    d2rcam::LastPickInfo lp{};
    if (d2rcam::LastPick(&lp)) {
        float o[3], d[3], look[3];
        if (d2rcam::PickRay(lp.px, lp.py, o, d) && d2rcam::LookAtNow(look)) {
            const flatpick::Ahead a = flatpick::GroundAhead(o, d, look[1]);
            if (a.hit) snprintf(ground, sizeof ground, "its ray %.0f deg down meets the hero's ground %.1f ahead (%.1f per unit of height)", a.downDeg, a.along, flatpick::SlipPerUnit(a.downDeg));
            else snprintf(ground, sizeof ground, "its ray (%.0f deg down) never meets the hero's ground", a.downDeg);
        }
        snprintf(pick, sizeof pick, "the game's pixel %.0f,%.0f of %.0fx%.0f (%s, %.0f%% off, %llu ms ago, from 0x%llX, %s)", lp.px, lp.py, lp.vpW, lp.vpH,
                 flatpick::AtCentre(lp.px, lp.py, lp.vpW, lp.vpH) ? "the middle" : "NOT the middle", 100.0f * flatpick::OffCentre(lp.px, lp.py, lp.vpW, lp.vpH),
                 (unsigned long long)(now - lp.at), lp.ret, lp.by == 0 ? "the game's camera" : lp.by == 2 ? "the ray override" : "our camera");
    }
    POINT c{}, mid{};
    RECT rc{};
    const bool win = g_gameWnd && GetCursorPos(&c) && GetClientRect(g_gameWnd, &rc);
    if (win) { mid = {rc.right / 2, rc.bottom / 2}; ClientToScreen(g_gameWnd, &mid); }
    LogF("vrcam: flat pick: click type %d (%d, %d, flags 0x%llX), under the crosshair: %s", type, x, y, (unsigned long long)flags, under);
    LogF("vrcam: flat pick:   %s", pick);
    LogF("vrcam: flat pick:   %s", ground);
    LogF("vrcam: flat pick:   the cursor %+ld,%+ld from the window's middle (%ldx%ld), focus %d, Alt %d, Shift %d, menu %d", win ? c.x - mid.x : 0L, win ? c.y - mid.y : 0L,
         (long)rc.right, (long)rc.bottom, GameFocused() ? 1 : 0, (GetAsyncKeyState(VK_MENU) & 0x8000) ? 1 : 0, (GetAsyncKeyState(VK_SHIFT) & 0x8000) ? 1 : 0,
         gamestate::MenuOpen() ? 1 : 0);
    // [input] flat_aim_ground: the ground the ray met as drawn and its cell, against the game's own (and where its world 1 met the ray)
    AcquireSRWLockShared(&g_mouseGroundLock);
    const MouseGroundSeen s = g_mouseGroundSeen;
    ReleaseSRWLockShared(&g_mouseGroundLock);
    float hero[3] = {NAN, NAN, NAN};
    LookAtNow(hero);
    char w1[80] = "";
    if (s.world1) snprintf(w1, sizeof w1, s.world1Hit ? "; its world 1 met the ray %.2f up" : "; its world 1 met nothing", s.world1Y);
    if (g_mouseGround.load() != 1 || !s.at || now - s.at > 1000)
        LogF("vrcam: flat pick:   the ground: %s", g_mouseGround.load() == 2 ? "the hook is not in (another build?) - the game's own point" : "not asked lately");
    else if (s.pick.ours)
        LogF("vrcam: flat pick:   the ground drawn under the crosshair %.2f up (the hero's %.2f), cell %.1f, %.1f; the game's own %.1f, %.1f - %.1f cells off%s; %s",
             s.floorY, hero[1], s.pick.gx, s.pick.gz, s.pick.gameGx, s.pick.gameGz, s.pick.offCells, w1,
             g_set.flatAimGround.load() ? "the drawn ground's cell went to the game" : "the game's own went to it ([input] flat_aim_ground=0)");
    else
        LogF("vrcam: flat pick:   the ray met no floor of the game's (the sky, the void) - the game's own point %.1f, %.1f%s", s.pick.gameGx, s.pick.gameGz, w1);
}

uint64_t HookMapClick(void* unit, int type, int x, int y, uint64_t flags) {
    // [gameplay] flat_move_while_attacking: after W A S D cut an attack's rest, the mouse's next attack waits until that animation would have
    // ended (the game's attack rate) - answered 1 as the game answers a click it took, so a held button goes on repeating (vr/move_attack.h ClickHold)
    if (moveattack::HoldClick(unit, type, x, y, flags)) return 1;
    const bool mouseType = type == 0 || type == 1 || type == 3 || type == 4;
    // nothing under the pointer? the game's own lookup (HoverUnit), asked once for whoever needs it
    int bareKnown = -1;
    auto bare = [&]() {
        if (bareKnown < 0) {
            const int idx = ((ClientIndexFn)d2rsig::Addr(RVA_CLIENT_INDEX))(unit);
            bareKnown = idx >= 0 && idx < 8 && !((HoverUnitFn)d2rsig::Addr(RVA_HOVER_UNIT))(idx) ? 1 : 0;
            // [input] flat_body_target: the click's own lookup will be answered with what the crosshair's ray meets - not bare then, no shot at the floor
            // (only while that answer can be given: the lookup's hook in, its return address known)
            if (bareKnown == 1 && ClickLookupHooked() && FlatClickTarget(0)) bareKnown = 0;
        }
        return bareKnown == 1;
    };
    if (unit && mouseType && !(flags & 0xFF)) {
        const bool shooter = g_set.flatClickShoot.load() && KeyMoveMode() && ShooterActive() && !g_portalForce.load();   // ([input] portal_auto's click is no shot)
        // VR first person ([gameplay] attack_in_place): a click on bare ground - from a mouse, the laser or a program - is a skill in
        // place, never a walk (vr/stand_still.h); on a monster, an item, a door or an NPC it stays the game's own click
        const bool still = standstill::ClickCandidate(standstill::Wanted(), viewgate::F4(), gamestate::MenuOpen(), true, type, flags);
        if ((shooter || still) && bare()) {
            flags |= 1;
            if (!shooter) standstill::NoteClick(type);
            else {
                static ULONGLONG lastLog = 0;
                if (const ULONGLONG now = GetTickCount64(); now - lastLog >= 2000) {
                    lastLog = now;
                    LogF("vrcam: flat click type %d, no target - a shot at the crosshair (Stand Still)", type);
                }
            }
        }
    }
    if (g_set.flatPickLog.load() && unit && (type == 0 || type == 3) && KeyMoveMode() && ShooterActive()) FlatPickClick(unit, type, x, y, flags);
    shottrace::ClickTrace shotTrace(unit, type, x, y, flags);   // [debug] shot_trace: the click, and when it returns (nothing when no shot is watched)
    // [gameplay] block_stray_walk (F4): a walk or run to a point that comes out of this click is told by the click's type and by
    // whether anything is under the pointer (vr/stray_walk.h); the guard itself sits on the game's command funnel
    if (unit && straywalk::WalkWanted() && viewgate::F4()) {
        straywalk::ClickScope scope(type, mouseType && bare());
        return OrigMapClick(unit, type, x, y, flags);
    }
    return OrigMapClick(unit, type, x, y, flags);
}

// Like the keyboard move: flat only, tried twice a second until the pages are
// decrypted (the first click in a game area), 10 s in a game area without the
// bytes = another build, and clicks stay the game's own.
void InstallMapClickHook() {
    static int misses = 0;
    // Flat with flat_keyboard_move, or VR with vr_keys_walk (F2's mouse look shoots the same way).
    const bool flat = g_set.platform.load() == 0;
    // ... or VR with [gameplay] attack_in_place (F4: a click on bare ground never walks; the hook asks the view where it decides)
    const bool forShooter = flat ? g_set.flatKeyMove.load() : g_set.vrKeyWalk.load();
    const bool forF4 = !flat && (standstill::Wanted() || straywalk::WalkWanted() || shottrace::Wanted());
    if (!g_ctx || g_mapClick.load() != 0 || !(forShooter || forF4)) return;
    if (!Matches(RVA_MAP_CLICK, kSigMapClick, sizeof kSigMapClick) || !Matches(RVA_CLIENT_INDEX, kSigClientIndex, sizeof kSigClientIndex) ||
        !(g_hoverHooked.load() || Matches(RVA_HOVER_UNIT, kSigHoverUnit, sizeof kSigHoverUnit))) {
        if (flat && g_inWorld.load() && ++misses >= 20) {   // VR: no verdict, the code may run (and decrypt) much later
            g_mapClick.store(2);
            Log("vrcam: flat: click hook NOT possible (0xFE3B0 / 0x9A820 / 0xF1900 not as expected - another build?) - a click on the ground walks");
        }
        return;
    }
    const bool in = d2rsig::Hook(RVA_MAP_CLICK, kSigMapClick, sizeof kSigMapClick, (void*)&HookMapClick, (void**)&OrigMapClick);
    g_mapClick.store(in ? 1 : 2);
    if (forShooter)
        Log(in ? "vrcam: flat: click hook in - with no target under the crosshair a click is a shot there, never a walk"
               : "vrcam: flat: click hook FAILED - a click on the ground walks");
    else
        Log(in ? "vrcam: VR: click hook in - in first person (F4) a click on bare ground is an attack in place, never a walk ([gameplay] attack_in_place, block_stray_walk)"
               : "vrcam: VR: click hook FAILED - a click on the ground walks in first person");
}

}  // namespace vrc
