// vr/input/flat_click.cpp - cut from vr/vrcam.cpp (docs/plan_modules.md M10); what it gives: vr/input/input.h. It sees vrcam.cpp's headers.
#include "../vrcam_headers.h"

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
namespace flatobj {
#include "../flat_pick_obj.inc"
}
struct FlatBodies { flatbody::Cap c[flatbody::kBodyCap + flatbody::kObjCap]; int n = 0; ULONGLONG at = 0; };
FlatBodies g_flatBodies;   // the game's thread only
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
    auto read = [](void* d, uintptr_t src, size_t n) { return src >= 0x10000 && SafeRead(d, (const void*)src, n); };
    struct RowMemo { uint32_t key = ~0u; uint8_t f[3] = {}; bool ok = false; };
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
    unitscan::EachUnit(read, table, unitscan::kMonster, [&](uintptr_t, const unitscan::Unit& u) {
        const uint32_t mode = unitscan::Field32(u, unitscan::kOffMode);
        if (mode == 0 || mode == 12) return true;   // dying, dead
        flatbody::Cap c;
        if (!unitscan::Place(read, u, &c.x, &c.z)) return true;
        c.cls = unitscan::Field32(u, unitscan::kOffClass);
        const uint8_t bank = u.b[unitscan::kOffBank];
        RowMemo& m = rows[(c.cls * 3u + bank) & 511u];
        const uint32_t key = (uint32_t)bank << 16 | (c.cls & 0xFFFF);
        if (m.key != key) {
            uint8_t f[16];
            m.key = key;
            m.ok = FlatStatsRow(bank, c.cls, f);
            if (m.ok) { m.f[0] = f[8]; m.f[1] = f[9]; m.f[2] = f[13]; }
        }
        flatbody::BodySize(m.f[0], m.f[1], m.f[2], m.ok, &c.height, &c.radius);
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
        keep(flatbody::kBodyCap, objects, flatbody::kObjCap, c);
        return true;
    });
    // one list: the bodies, then the objects
    for (int i = 0; i < objects; ++i) s.c[bodies + i] = s.c[flatbody::kBodyCap + i];
    s.n = bodies + objects;
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

bool HookMouseGround(void* mgr, float* x, float* y, uint64_t pixel) {
    const bool gameOk = OrigMouseGround(mgr, x, y, pixel);
    if (g_portalForce.load(std::memory_order_acquire) && x && y && g_set.platform.load() == 0 && g_enabled.load()) {
        if (PortalUnderPointer()) g_portalHover.store(true, std::memory_order_release);
        *x = g_portalForceX.load() / flatground::kRenderPerCell;
        *y = g_portalForceZ.load() / flatground::kRenderPerCell;
        return true;
    }
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
        FlatBodyScan();
        if (g_flatBodies.n > 0 && d2rcam::PickRay(px[0], px[1], o, d)) {
            const float tFloor = floorOk ? (floor[0] - o[0]) * d[0] + (floor[1] - o[1]) * d[1] + (floor[2] - o[2]) * d[2] : INFINITY;
            flatbody::Hit hit;
            if (flatbody::Pick(g_flatBodies.c, g_flatBodies.n, o, d, tFloor, &hit)) {
                *x = hit.cellX;
                *y = hit.cellZ;
                static bool told = false;
                if (!told) {
                    told = true;
                    const flatbody::Cap& c = g_flatBodies.c[hit.index];
                    LogF("vrcam: flat: a %s (class %u) picked by its body, not its feet ([input] flat_body_pick; %d bodies and objects round the hero)",
                         c.body ? "monster or NPC" : "object", c.cls, g_flatBodies.n);
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
    if (!g_ctx || g_mouseGround.load() != 0 || g_set.platform.load() != 0) return;
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
    const bool mouseType = type == 0 || type == 1 || type == 3 || type == 4;
    // nothing under the pointer? the game's own lookup (HoverUnit), asked once for whoever needs it
    int bareKnown = -1;
    auto bare = [&]() {
        if (bareKnown < 0) {
            const int idx = ((ClientIndexFn)d2rsig::Addr(RVA_CLIENT_INDEX))(unit);
            bareKnown = idx >= 0 && idx < 8 && !((HoverUnitFn)d2rsig::Addr(RVA_HOVER_UNIT))(idx) ? 1 : 0;
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
