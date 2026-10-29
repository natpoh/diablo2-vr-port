// melee_touch.cpp - hit by touch (melee_touch.h). The swing engine, the capsules and the shapes are pure (stroke_contact.h,
// melee_bodies.h); this file is what touches the game, and all of that is READ-ONLY except the press itself, which is the
// game's own attack on the pad, and one answer given to the game's own target lookups:
//   - the client's unit hash (the game's FindClientUnit table) is read for monsters, SEH-guarded and bounded;
//   - MonStats rows are asked of the game's own getter, once per class;
//   - the server's Relation (hostile?) and Line (wall?) are called read-only on the game's thread, ahead of any touch, so a
//     touch finds its verdict ready (field-tested by bearstonem/d2r-pcvr, MIT, which calls them the same way);
//   - nothing is called that attacks, throws or casts; the reach, the rolls and the speed are the game's - EXCEPT with the user's
//     experimental option [gameplay] melee_direct (vr/melee_direct.cpp, default off), where an accepted touch calls the game's
//     basic attack itself instead of pressing its key (Approve below); unticked, or whenever that is not possible, none of it runs.
// The idea of the whole - a blade swept as a capsule against monster body capsules, a stroke logic that keeps a held blade
// from hitting - is bearstonem/d2r-pcvr's (steamvr/melee_contact.h, vr/melee.cpp, vr/vrcam.cpp MeleeContactTick). Where it
// differs: they call the server's Attack with the target swapped, replace Range and force the rolls; here the touch only
// picks the target and the moment, and the game's own attack does the rest.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>

#include <D2RLPlugin/api.h>

#include "log_async.h"   // [debug] log_async: the plugin's log lines through a ring and a writer thread (vr/log_queue.h); off = ctx->LogInfo as ever
#include "d2r_vr_shared.h"
#include "hit_claim.h"
#include "hit_latency.h"
#include "blade_shape.h"
#include "double_swing.h"
#include "double_swing_game.h"   // [hands] double_swing_by_hands: the second hand's touch may be the game's Double Swing / Frenzy
#include "melee_arm_game.h"   // [gameplay] melee_skill_armed: the touch's press may be read as the armed melee skill
#include "stroke_tuning.h"
#include "strike_direction.h"
#include "metal_head.h"
#include "parry_wire.h"   // [weight] parry_*: the clash with a monster's weapon, a blow on its weapon in its attack   // [gameplay] melee_metal_head: where the head starts, from the game's own model
#include "thrust_stroke.h"   // [gameplay] melee_thrust: a poke along the blade with its own speed and distance
#include "thrust_debug.h"    // [debug] thrust_debug: the last thrust's readings
#include "thrust_debug_wire.h"   // ... shown over the weapon hand
#include "thrown_touch.h"
#include "touch_why.h"
#include "unit_scan.h"
#include "client_verdict.h"   // hostile? a wall? from the client's own units, where the server is not in this process (a TCP/IP client)
#include "melee_bodies.h"
#include "melee_boxes.h"
#include "player_bodies.h"   // [multiplayer] pose_share_bodies: the other players among the bodies (their ids, their capsule)
#include "melee_direct.h"
#include "melee_direct_client.h"   // [gameplay] melee_direct_client: a joined TCP/IP player's touch sent as the game's own command for the monster
#include "melee_press.h"
#include "doll_lib.h"     // [weight] monster_ragdoll: the weight library's monsters' weight (d2r_vr_weight.dll's D2rd* exports) moves the bodies
#include "doll_view.h"
#include "grab_bodies.h"   // [weight] grab_bodies: the held limbs put into the bodies (PutLinks)
#include "item_grab.h"     // [weight] grab_items: the items on the ground near the hero (GroundItems)
#include "barrel_touch.h"  // [gameplay] melee_touch_barrels: a swing through a barrel / pot presses the game's own interact on it
#include "blade_push.h"   // [weight] monster_ragdoll: a blade the weight model does not hold (a weapon in each hand) pushes the doll itself
#include "room_pose.h"
#include "melee_touch.h"
#include "knock_props.h"   // [weight] knock_props: the blades as drawn knock the level's props about
#include "view_gate.h"
#include "weight_drive.h"
#include "weight.h"   // [weight] monster_shields: Reached / Blocked
#include "sigscan.h"
#include "skeletons.h"
#include "stroke_contact.h"
#include "ui_ticket.h"
#include "frame_trace.h"   // [debug] frame_trace: what the touch's hooks and jobs cost a pair, and when a sweep, a press, a hit happened

#if __has_include("direct_input.h")
#include "direct_input.h"   // the direct-input branch is in the tree: the press goes through it
#define MELEE_TOUCH_DIRECT 1
#endif

using namespace D2RL;

namespace meleetouch {
namespace {

// Addresses in 3.3.93787 (the keys of game_sigs.h).
constexpr uint64_t RVA_FIND_UNIT = 0x9A5D0, RVA_UNITS = 0x2A23910, RVA_MONSTATS = 0x975B0;
constexpr uint64_t RVA_LOOKUP = 0x48FE80, RVA_RELATION = 0x48E460, RVA_LINE = 0x3502A0;
// mov [rsp+8], rbx; mov [rsp+18h], rsi; push rdi; sub rsp, 20h
const uint8_t kSigLookup[15] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20};

using FindUnitFn = uintptr_t (*)(uint32_t id, int type);
using StatsFn = uintptr_t (*)(uint8_t bank, int cls);
using LookupFn = uintptr_t (*)(uintptr_t game, int type, uint32_t id);
using RelationFn = int (*)(uintptr_t game, uintptr_t player, uintptr_t unit);
using LineFn = int (*)(uintptr_t a, uintptr_t b, int flags);
LookupFn g_origLookup = nullptr;

const PluginContext* g_ctx = nullptr;
const ThreadService* g_threads = nullptr;
std::atomic<bool> g_wanted{false}, g_unchecked{false};
std::atomic<bool> g_noAttack{false};   // [debug] melee_touch_no_attack: a touch that would attack presses nothing (the weapon's weight, its stab and the monsters' push go on)
std::atomic<ULONGLONG> g_liveAt{0};   // the last time Tick had a weapon it swings (melee, bare hands, a throwing weapon) posed and striking: GetTickCount64
std::atomic<uint32_t> g_liveKind{0};  // ... and its D2RVR_WeaponType
std::atomic<bool> g_throwWanted{false};   // [hands] throw_autoaim_deg: the monsters near the hero are wanted for a throw's auto-aim
std::atomic<bool> g_bowWanted{false};     // [bow] pitch_aim: ... and for a bow's or crossbow's shot aimed by its line (vr/bow_pitch_wire.cpp)
std::atomic<bool> g_leapWanted{false};    // [gameplay] leap_by_gaze: ... and for the monster a Barbarian's Leap Attack looks at
// [gameplay] melee_touch_thrown: a javelin, throwing knife or throwing axe is swung too (a point-blank throw by the game's own attack), and what
// the touch needs of the throw by gesture (vr/throw_gesture_wire.h), by function pointers so this module does not depend on it (vrcam wires them).
std::atomic<bool> g_thrownTouch{true};
std::atomic<bool> g_thrownHeld{false};   // [gameplay] melee_touch_thrown_held: a thrown weapon is in the hand from the equip, like a sword (vr/thrown_touch.h)
std::atomic<GripIsThrowsFn> g_gripIsThrows{nullptr};
std::atomic<GripHeldFn> g_gripHeld{nullptr};
std::atomic<GripAtFn> g_gripIsThrowsAt{nullptr}, g_gripHeldAt{nullptr};   // (SetThrowGripAt)
std::atomic<LastThrowFn> g_lastThrow{nullptr};
std::atomic<HoldOffFn> g_holdOff{nullptr};

void Log(const char* text) {
    frametrace::Scope trace(frametrace::kLog);   // [debug] frame_trace: what the host's log costs the writer
    if (g_ctx) logasync::Info(g_ctx, text);
}
template <class... A> void LogF(const char* fmt, A... a) {
    char b[640];   // (a monster's held parts, each with its space, run long)
    snprintf(b, sizeof b, fmt, a...);
    Log(b);
}
template <class S> const S* Query(const PluginContext* ctx) {
    const S* s = nullptr;
    return ctx->QueryService(&s) == ServiceQueryResult::Success ? s : nullptr;
}

bool Read(void* d, uintptr_t s, size_t n) noexcept {
    if (s < 0x10000) return false;
    __try { memcpy(d, reinterpret_cast<const void*>(s), n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// The game's functions, each behind its own guard: a fault is a "no", never a crash of the game.
bool CallFindUnit(uintptr_t fn, uint32_t id, int type, uintptr_t* out) noexcept {
    __try { *out = reinterpret_cast<FindUnitFn>(fn)(id, type); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallStats(uintptr_t fn, uint8_t bank, int cls, uintptr_t* out) noexcept {
    __try { *out = reinterpret_cast<StatsFn>(fn)(bank, cls); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallLookup(uintptr_t fn, uintptr_t game, int type, uint32_t id, uintptr_t* out) noexcept {
    __try { *out = reinterpret_cast<LookupFn>(fn)(game, type, id); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallRelation(uintptr_t fn, uintptr_t game, uintptr_t player, uintptr_t unit, int* out) noexcept {
    __try { *out = reinterpret_cast<RelationFn>(fn)(game, player, unit); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallLine(uintptr_t fn, uintptr_t a, uintptr_t b, int* out) noexcept {
    __try { *out = reinterpret_cast<LineFn>(fn)(a, b, 0x804); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// ---- time: the stamps of the shared block (0.1 ms, the low 32 bits of the counter) as seconds ----------------------------

std::mutex g_timeMutex;
uint32_t g_stampBase = 0;
double g_stampOffset = 0.0;
double Seconds(uint32_t stamp) {
    std::lock_guard<std::mutex> lock(g_timeMutex);
    if (!g_stampBase) g_stampBase = stamp ? stamp : 1;
    int32_t d = static_cast<int32_t>(stamp - g_stampBase);
    if (d > (1 << 30) || d < -(1 << 30)) {   // about 30 hours: take a new base without a jump in the seconds
        g_stampOffset += static_cast<double>(d) * 1e-4;
        g_stampBase = stamp;
        d = 0;
    }
    return g_stampOffset + static_cast<double>(d) * 1e-4;
}

// ---- the hand samples, in the room frame, by the stamp of the pose they belong to --------------------------------------

// [multiplayer] pose_share_bodies: the blades of the last pose swept (meleetouch::OwnBlades)
std::mutex g_ownBladesMutex;
meleetouch::OwnBlade g_ownBlades[2];
size_t g_ownBladesN = 0;
ULONGLONG g_ownBladesAt = 0;

std::mutex g_histMutex;
stroke::PoseHistory g_hist[2];   // 0 right, 1 left
stroke::HandPath g_handPath[2];  // [gameplay] melee_direct_client: each controller's own positions in the room (its path in a swing), under g_histMutex
uint32_t g_lastStamp = 0;
// Where the head was in the room at each sample (the shared block's headRoom and yaw), by the same stamp: the weapon bone, which the skeleton hook keeps
// in the head's turn-only frame, is put back in the room with it, as the hand is - so the BLADE's own motion is measured in the room too.
struct RoomFrameAt {
    uint32_t stamp = 0;
    bool set = false;
    bladeshape::RoomFrame frame;
};
RoomFrameAt g_frames[16];
unsigned g_frameNext = 0;

stroke::Pose RoomPose(const D2RVR_Shared& sh, const float* pos, const float* rot) {
    return meleetouch::RoomPose(sh.headRoom, sh.headYawRoomDeg * 0.0174532925f, pos, rot);
}

// ---- the game's monsters ----------------------------------------------------------------------------------------------------

struct Cand {
    uint32_t id = 0, cls = 0;
    int mode = -1;         // its unit's mode at the scan (unitscan::Near::mode; [weight] parry_*)
    float d2 = 0.0f;       // squared distance to the hero on the grid, game units
    stroke::Body body;
};
struct Scan {
    std::array<Cand, 48> c;
    size_t n = 0;
    uint32_t heroId = 0;
    // [multiplayer] pose_share_bodies: the other players within the scan's reach (their unit ids and places on the grid, game WORLD units)
    struct Player { uint32_t id = 0; float x = 0.0f, z = 0.0f; };
    std::array<Player, playerbodies::kTouchPlayers> p;
    size_t np = 0;
};

unitscan::RowCache g_rows;   // (unit_scan.h) the timer's thread only: hit by touch's scan and the shield bash's are both made on it

// A class's MonStats row as the game's own getter gives it, read now (any thread; the reads and the call guarded).
bool RowNow(uint8_t bank, uint32_t cls, meleetouch::MonsterRow* out) {
    uintptr_t row = 0;
    uint8_t f[16] = {};
    const uintptr_t fn = d2rsig::Addr(RVA_MONSTATS);
    if (!fn || cls > 4095 || bank > 3 || !CallStats(fn, bank, static_cast<int>(cls), &row) || !Read(f, row, sizeof f)) return false;
    *out = {f[8], f[9], f[13]};
    return f[8] >= 1 && f[8] <= 8 && f[9] >= 1 && f[9] <= 8;
}

bool RowFor(uint8_t bank, uint32_t cls, meleetouch::MonsterRow* out) {   // (a row that did not read is asked again after 5 s)
    return g_rows.Get(bank, cls, GetTickCount64(), [](uint8_t b, uint32_t c, meleetouch::MonsterRow* r) { return RowNow(b, c, r); }, out);
}

// The client's unit hash is walked by unit_scan.h (pure, tested) - the one walk of everything that looks at the monsters round the hero.
constexpr size_t kUnitBytes = unitscan::kUnitBytes;
using UnitBytes = unitscan::Unit;
uint32_t U32(const UnitBytes& u, size_t o) { return unitscan::Field32(u, o); }
uintptr_t U64(const UnitBytes& u, size_t o) { return unitscan::Field64(u, o); }

// A unit's place on the grid in game world units (twice the cell): the 16.16 coordinates of its path.
bool GridOf(const UnitBytes& u, float* x, float* z) {
    return unitscan::Place([](void* d, uintptr_t s, size_t n) { return Read(d, s, n); }, u, x, z);
}

// The units of one type in the client's hash, one after the other: calls fn(unit address, bytes) until it says stop.
template <class F> void EachUnit(int type, F&& fn) {
    unitscan::EachUnit([](void* d, uintptr_t s, size_t n) { return Read(d, s, n); }, d2rsig::Addr(RVA_UNITS), type, fn);
}

// The hero's own client unit (the player near the hero's feet on the grid) and the monsters within reach of the swing.
bool ScanUnits(float heroX, float heroZ, float upm, float floorM, Scan* out) {
    out->n = 0;
    out->heroId = 0;
    out->np = 0;
    float best = 4.0f * 4.0f;   // the hero is the player within 4 game units of where his model stands
    Scan::Player all[16];
    size_t na = 0;
    EachUnit(0, [&](uintptr_t, const UnitBytes& u) {
        float x, z;
        if (!GridOf(u, &x, &z)) return true;
        const float d2 = (x - heroX) * (x - heroX) + (z - heroZ) * (z - heroZ);
        if (d2 < best) { best = d2; out->heroId = U32(u, 8); }
        const uint32_t mode = U32(u, 0xC);
        if (na < std::size(all) && d2 <= 24.0f * 24.0f && mode != 0 && mode != 17) all[na++] = {U32(u, 8), x, z};   // (not a dead one: mode 0 death, 17 dead)
        return true;
    });
    if (!out->heroId) return false;
    for (size_t i = 0; i < na && out->np < out->p.size(); ++i)   // [multiplayer] pose_share_bodies: the others, our hero not among them
        if (all[i].id && all[i].id != out->heroId) out->p[out->np++] = all[i];
    unitscan::Near found[std::tuple_size<decltype(Scan::c)>::value];   // the monsters within 24 game units: unit_scan.h, the walk the shield bash shares
    const size_t n = MonstersNear(heroX, heroZ, 24.0f, upm, floorM, found, std::tuple_size<decltype(Scan::c)>::value);
    for (size_t i = 0; i < n; ++i) {
        out->c[i].id = found[i].id;
        out->c[i].cls = found[i].cls;
        out->c[i].mode = static_cast<int>(found[i].mode);
        out->c[i].d2 = found[i].d2;
        out->c[i].body = found[i].body;
    }
    out->n = n;
    return true;
}

uintptr_t ClientUnit(uint32_t id, int type) {
    const uintptr_t fn = d2rsig::Addr(RVA_FIND_UNIT);
    uintptr_t u = 0;
    if (!fn || !id || !CallFindUnit(fn, id, type, &u) || !u) return 0;
    uint32_t head[3];
    if (!Read(head, u, sizeof head) || head[0] != static_cast<uint32_t>(type) || head[2] != id) return 0;
    return u;
}

// ---- [gameplay] melee_touch_barrels (vr/barrel_touch.h) --------------------------------------------------------------------------
// The barrels, pots, urns and baskets near the hero: the client's hash, type 2, whole (mode 0), their Objects row (unit +0x10 -> its first
// pointer, as the game's own read at 0x34ADB1) of the three kinds, each at its static path's cell (+0x38 -> +0x10 / +0x14, the items'
// way). Read only, vrcam's timer (Tick, every 50 ms at most). A counting stroke's sweep through one asks the press (BarrelTouch);
// BarrelsStep holds it until the object breaks; vrcam presses the game's interact with its target answered with the object.
std::atomic<bool> g_barrelsWanted{false};
barreltouch::Barrel g_barrels[barreltouch::kMax];
int g_barrelsN = 0;
ULONGLONG g_barrelsAt = 0;
barreltouch::Presser g_barrelPress;           // vrcam's timer thread (Tick, BarrelsStep)
std::atomic<uintptr_t> g_barrelTarget{0};     // the press in flight, for vrcam's pad and target hooks
std::atomic<ULONGLONG> g_barrelTargetAt{0};

bool ObjectRow(const UnitBytes& u, barreltouch::Row* r) {
    const uintptr_t data = U64(u, 0x10);
    uintptr_t row = 0;
    if (data < 0x10000 || !Read(&row, data, sizeof row) || row < 0x10000) return false;
    uint8_t bytes[barreltouch::kRowBytes];
    if (!Read(bytes, row, sizeof bytes)) return false;
    *r = barreltouch::RowOf(bytes);
    return true;
}

void ScanBarrels(const stroke::Vec& grid, float upm, ULONGLONG now) {
    if (now - g_barrelsAt < 50 && now >= g_barrelsAt) return;
    g_barrelsAt = now;
    const float reach = barreltouch::kScanM * upm;
    int n = 0;
    EachUnit(2, [&](uintptr_t at, const UnitBytes& u) {
        if (n >= barreltouch::kMax) return false;
        barreltouch::Row row;
        if (!ObjectRow(u, &row) || !barreltouch::Breakable(row, U32(u, 0xC))) return true;
        const uintptr_t path = U64(u, 0x38);
        uint32_t cell[2] = {};
        if (path < 0x10000 || !Read(cell, path + 0x10, sizeof cell)) return true;
        const float x = 2.0f * (float)cell[0] + 1.0f, z = 2.0f * (float)cell[1] + 1.0f;
        if ((x - grid.x) * (x - grid.x) + (z - grid.z) * (z - grid.z) > reach * reach) return true;
        barreltouch::Barrel& b = g_barrels[n++];
        b.id = U32(u, 8);
        b.cls = U32(u, 4);
        b.unit = at;
        b.op = row.operateFn;
        b.sizeX = row.sizeX;
        b.sizeY = row.sizeY;
        b.gridX = x;
        b.gridZ = z;
        return true;
    });
    g_barrelsN = n;
}

void PublishBarrelPress() {
    g_barrelTarget.store(g_barrelPress.Target());
    g_barrelTargetAt.store(GetTickCount64());
}

// A sweep of hand `side` (a counting stroke's) against the barrels near: the first it goes through is asked - one press at a time, none
// while the touch's own attack press is down (a monster in the same swing has it).
void BarrelTouch(int side, const stroke::Sweep& sw, const stroke::Vec& grid, float upm, float floorM, ULONGLONG now) {
    ScanBarrels(grid, upm, now);
    if (g_barrelPress.Pressing() || PressDown() || !sw.stroke) return;
    for (int i = 0; i < g_barrelsN; ++i) {
        stroke::Body body;
        if (!barreltouch::Volume(g_barrels[i], floorM, upm, body) || !stroke::Touches(sw.before, sw.after, body)) continue;
        if (g_barrelPress.Ask(side, sw.stroke, g_barrels[i], (double)now / 1000.0)) { PublishBarrelPress(); return; }
    }
}

// ---- the server's verdicts: hostile? is a wall between? ----------------------------------------------------------------------
// Asked ahead (every 100 ms, on the game's thread, read-only) for the monsters near the hero, so a touch finds its answer
// ready and the press goes out in the frame of the contact.

struct Verdict { uint32_t id = 0; bool hostile = false, clear = false; ULONGLONG at = 0; };
std::mutex g_verdictMutex;
std::array<Verdict, 64> g_verdicts;
std::array<uint32_t, 48> g_probeIds;
size_t g_probeCount = 0;
std::atomic<uintptr_t> g_serverGame{0};
std::atomic<bool> g_learnGame{true};   // false while the world camera is down: a game being torn down is not learned again (SetInWorld)
std::atomic<uint32_t> g_heroId{0};
std::atomic<bool> g_statusWanted{false};      // [hud] wrist_status (vr/wrist_status.h): the lookup is observed for it too (the server's game, for the read-only reads of the hero's pets and states)
std::atomic<uint32_t> g_statusHeroId{0};      // ... and the hero's unit id it learns the game by (the client's own read of it, vr/wrist_status_game.cpp)
uiticket::Ticket g_probeTicket;
std::atomic<int> g_serverHook{0};   // 0 not tried, 1 in, 2 not possible

uintptr_t HookLookup(uintptr_t game, int type, uint32_t id) {
    frametrace::Scope trace(frametrace::kUnitLookup);   // [debug] frame_trace: ours, the server's lookup left out (it is called for every unit the server asks for)
    trace.Pause();
    const uintptr_t unit = g_origLookup(game, type, id);
    trace.Resume();
    const uint32_t statusHero = g_statusHeroId.load(std::memory_order_relaxed);
    if (type == 0 && unit && (id == g_heroId.load(std::memory_order_relaxed) || (statusHero && id == statusHero)) && game != g_serverGame.load(std::memory_order_relaxed) &&
        g_learnGame.load(std::memory_order_relaxed)) {
        uintptr_t back = 0;
        uint32_t head[3];
        if (Read(head, unit, sizeof head) && head[0] == 0 && head[2] == id && Read(&back, unit + 0xD8, sizeof back) && back == game)
            g_serverGame.store(game);
    }
    return unit;
}

// The verdicts in, each to its monster's slot (or the oldest slot).
void StoreVerdicts(const Verdict* out, size_t m) {
    std::lock_guard<std::mutex> lock(g_verdictMutex);
    for (size_t i = 0; i < m; ++i) {
        size_t slot = 0;
        for (size_t k = 0; k < g_verdicts.size(); ++k) {
            if (g_verdicts[k].id == out[i].id) { slot = k; break; }
            if (g_verdicts[k].at < g_verdicts[slot].at) slot = k;   // the oldest goes
        }
        g_verdicts[slot] = out[i];
    }
}

void __cdecl Probe(const PluginContext*, void* user) noexcept {
    frametrace::Scope trace(frametrace::kProbe);   // [debug] frame_trace: this job runs on the game's thread, 10 times a second
    if (!g_probeTicket.Claim(uiticket::FromUser(user))) return;
    const uintptr_t game = g_serverGame.load();
    const uint32_t heroId = g_heroId.load();
    if (!game || !heroId) return;
    uint32_t ids[48];
    size_t n;
    {
        std::lock_guard<std::mutex> lock(g_verdictMutex);
        n = g_probeCount;
        memcpy(ids, g_probeIds.data(), n * sizeof(uint32_t));
    }
    const uintptr_t lookup = g_origLookup ? reinterpret_cast<uintptr_t>(g_origLookup) : d2rsig::Addr(RVA_LOOKUP);
    const uintptr_t relation = d2rsig::Addr(RVA_RELATION), line = d2rsig::Addr(RVA_LINE);
    uintptr_t player = 0;
    uint32_t mode = 0;
    if (!lookup || !relation || !line || !CallLookup(lookup, game, 0, heroId, &player) || !player || !Read(&mode, player + 0xC, 4) ||
        mode == 0 || mode == 17 || mode == 5 || mode == 6)   // dead, in the town portal, dying... (pcvr's gates)
        return;
    const ULONGLONG now = GetTickCount64();
    Verdict out[48];
    size_t m = 0;
    for (size_t i = 0; i < n; ++i) {
        uintptr_t unit = 0;
        uint32_t head[4] = {};
        if (!CallLookup(lookup, game, 1, ids[i], &unit) || !unit || !Read(head, unit, sizeof head) || head[0] != 1 || head[2] != ids[i]) continue;
        Verdict v;
        v.id = ids[i];
        v.at = now;
        int rel = 0, wall = 1;
        v.hostile = head[3] != 0 && head[3] != 12 && CallRelation(relation, game, player, unit, &rel) && rel != 0;
        v.clear = v.hostile && CallLine(line, player, unit, &wall) && wall == 0;
        out[m++] = v;
    }
    StoreVerdicts(out, m);
}

// ---- the client's verdicts: where the server is not in this process --------------------------------------------------------------
// A TCP/IP client's game server runs on the host's PC: no server game is ever learned here, Probe never runs, and without a
// verdict a touch presses nothing. Then the client's own units answer (vr/client_verdict.h): the monster's owner (a pet is not
// hostile), the hero's area (nobody fights in town) and the game's line test on the client's units for a wall. Asked on the UI
// thread, where the client's units are kept, every 100 ms like Probe; read-only. The host's server still decides every press.

uiticket::Ticket g_clientTicket;
std::atomic<bool> g_clientVerdict{false};   // [debug] melee_touch_client_verdict: the client's units answer even with the server here

// The server's game, unless [debug] melee_touch_client_verdict asks for the client's answers anyway.
uintptr_t ServerToAsk() { return g_clientVerdict.load() ? 0 : g_serverGame.load(); }

D2RL::Reimplementations::GetLevelIdFromUnitFn LevelGetter() noexcept {   // apart from HeroLevel: a static's guard and __try don't mix (C2712)
    static const auto getLevel = []() -> D2RL::Reimplementations::GetLevelIdFromUnitFn {
        const HMODULE core = GetModuleHandleW(L"D2RCore.dll");
        return core ? reinterpret_cast<D2RL::Reimplementations::GetLevelIdFromUnitFn>(
                          GetProcAddress(core, MAKEINTRESOURCEA(D2RL::Reimplementations::GetLevelIdFromUnitInfo.ordinal)))
                    : nullptr;
    }();
    return getLevel;
}
int HeroLevel(uintptr_t hero) noexcept {
    const auto getLevel = LevelGetter();
    if (!getLevel || !hero) return 0;
    __try { return getLevel(reinterpret_cast<const void*>(hero)); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

void __cdecl ClientProbe(const PluginContext*, void* user) noexcept {
    if (!g_clientTicket.Claim(uiticket::FromUser(user))) return;
    if (ServerToAsk()) return;   // the server is here after all: Probe answers
    const uint32_t heroId = g_heroId.load();
    if (!heroId) return;
    uint32_t ids[48];
    size_t n;
    {
        std::lock_guard<std::mutex> lock(g_verdictMutex);
        n = g_probeCount;
        memcpy(ids, g_probeIds.data(), n * sizeof(uint32_t));
    }
    const uintptr_t hero = ClientUnit(heroId, 0);
    if (!hero || !n) return;
    uint32_t players[8];
    size_t np = 0;
    EachUnit(0, [&](uintptr_t, const UnitBytes& u) {
        players[np++] = U32(u, 8);
        return np < 8;
    });
    const int level = HeroLevel(hero);
    const uintptr_t line = d2rsig::Addr(RVA_LINE);
    const ULONGLONG now = GetTickCount64();
    Verdict out[48];
    size_t m = 0, hostile = 0, pets = 0, walls = 0, unknown = 0, gone = 0, lineFailed = 0;
    for (size_t i = 0; i < n; ++i) {
        clientverdict::Facts f;
        f.heroLevel = level;
        const uintptr_t unit = ClientUnit(ids[i], 1);
        uintptr_t data = 0;
        if (unit && Read(&f.mode, unit + 0xC, 4) && Read(&data, unit + clientverdict::kMonsterData, sizeof data) &&
            Read(&f.owner, data + clientverdict::kOwnerId, 4))
            f.unitRead = true;
        if (f.unitRead && line) {
            int wall = 1;
            if (CallLine(line, hero, unit, &wall)) f.line = wall;
            else ++lineFailed;
        }
        const clientverdict::Verdict cv = clientverdict::Decide(f, players, np);
        if (!cv.known) {
            if (!f.unitRead || f.mode == 0 || f.mode == 12) ++gone;   // dying, dead, or out of the client's hash
            else ++unknown;                                           // the area or the owner field not understood
            continue;
        }
        Verdict v;
        v.id = ids[i];
        v.at = now;
        v.hostile = cv.hostile;
        v.clear = cv.clear;
        out[m++] = v;
        hostile += cv.hostile && cv.clear;
        walls += cv.hostile && !cv.clear;
        pets += !cv.hostile && f.owner != clientverdict::kNoOwner;
    }
    StoreVerdicts(out, m);
    // For the first test as a TCP/IP client: what the client's units said, whenever that changes (at most once a second).
    static ULONGLONG toldAt = 0;
    static size_t told[6] = {};
    const size_t now6[6] = {hostile, walls, pets, unknown, gone, lineFailed};
    if (memcmp(told, now6, sizeof told) != 0 && now - toldAt >= 1000) {
        toldAt = now;
        memcpy(told, now6, sizeof told);
        LogF("melee touch: no server here - the client's units answer: area %d%s, %u hostile, %u behind a wall, %u pets, %u dead or gone, %u not understood%s",
             level, clientverdict::IsTown(level) ? " (town)" : "", static_cast<unsigned>(hostile), static_cast<unsigned>(walls),
             static_cast<unsigned>(pets), static_cast<unsigned>(gone), static_cast<unsigned>(unknown),
             lineFailed ? " - the line test faulted, walls not checked" : "");
    }
}

// Asks for the verdicts of the monsters in g_probeIds: the server where it is in this process, the client's units where not.
void AskVerdicts(ULONGLONG now) {
    if (g_unchecked.load() || !g_threads) return;
    if (ServerToAsk()) {
        if (g_threads->runOnGameThread)
            if (const uint32_t token = g_probeTicket.Start(now))
                if (g_threads->runOnGameThread(g_ctx, &Probe, uiticket::ToUser(token)) != Threads::Result::Success) g_probeTicket.Claim(token);
        return;
    }
    if (g_threads->runOnUiThread)
        if (const uint32_t token = g_clientTicket.Start(now))
            if (g_threads->runOnUiThread(g_ctx, &ClientProbe, uiticket::ToUser(token)) != Threads::Result::Success) g_clientTicket.Claim(token);
}

enum class Know { Unknown, No, Yes };
Know Verdicted(uint32_t id, ULONGLONG now) {
    if (g_unchecked.load()) return Know::Yes;
    std::lock_guard<std::mutex> lock(g_verdictMutex);
    for (const Verdict& v : g_verdicts)
        if (v.id == id) return now - v.at > 700 ? Know::Unknown : v.hostile && v.clear ? Know::Yes : Know::No;
    return Know::Unknown;
}

// ---- [weight] monster_bodies: the monsters as bodies the held weapon meets (vr/monster_bodies.h) ------------------------------------------
// Every scan of the touch keeps the hostile monsters (the server called them hostile and unwalled), the nearest first; every 10 ms their own
// skeletons are read (vr/monster_rig.h: the models standing near the hero, each a capsule on each limb, matched to the monsters by where they
// stand) and the bodies published: the weight model takes them on its own thread (Bodies), [debug] melee_boxes draws them (DrawBodies). Wanted by
// either - the drawing does not need the blocking on, so the fit can be looked at first. While wanted the monsters are read every 30 ms.

std::atomic<bool> g_bodiesWanted{false};
std::atomic<bool> g_shieldsWanted{false};   // [weight] monster_shields (SetShieldsWanted)
std::atomic<bool> g_headVolume{true};        // [gameplay] melee_head_volume (SetHeadVolume)
std::mutex g_bodiesMutex;
monsterbodies::Snapshot g_bodies;
ULONGLONG g_bodiesScanAt = 0, g_rigsAt = 0;
ULONGLONG g_touchBodiesAt = 0;   // [weight] grab_bodies: when the touch's own tick last kept the bodies (GrabTick scans for the grab when it does not)
monsterbodies::Body g_hostile[monsterbodies::kMax];   // the last scan's hostile monsters (the touch's capsules of them), the timer's thread only
int g_nHostile = 0;

std::atomic<bool> g_grabWanted{false};      // [weight] grab_bodies (SetGrabWanted): the bodies made for it too, the non-hostile ones kept for it alone

bool BodiesWanted() { return g_bodiesWanted.load() || meleeboxes::Wanted() || g_grabWanted.load(); }

// [weight] monster_ragdoll (probe): the bodies' sway - the doll library's (vr/doll_lib.h; its state ours, under g_swayMutex), stepped by the clock whenever
// the bodies are handed over (either thread). No library: nothing sways.
std::atomic<bool> g_swayWanted{false};
std::mutex g_swayMutex;
D2rdState g_doll{};
D2rdParams g_dollParams{sizeof(D2rdParams), 1.8f, 0.6f, 0.8f, 0.25f, 0.20f, 1.0f, 1.0f};   // (softer since 0.188.7 - a 6-11 N s push turned a limb a few degrees: "нпс не шевелятся"; 0.8 of critical, no bounce worth the name; the sliders set the last two)
LARGE_INTEGER g_swayAt{};
ULONGLONG g_swayToldAt = 0;
// The doll stepped by the clock since it was last stepped (under g_swayMutex): by whichever thread hands it the bodies or pushes it - the weight model's
// (Bodies), or the blades' when the weight model holds no weapon (PushWithBlades: a weapon in each hand).
void StepSwayLocked() {
    LARGE_INTEGER t, f;
    QueryPerformanceCounter(&t);
    QueryPerformanceFrequency(&f);
    if (g_swayAt.QuadPart) dolllib::Step(&g_doll, &g_dollParams, static_cast<float>(static_cast<double>(t.QuadPart - g_swayAt.QuadPart) / static_cast<double>(f.QuadPart)));
    g_swayAt = t;
}
std::atomic<bool> g_swayView{false};               // [debug] monster_ragdoll_view
// [weight] monster_ragdoll_pose: each swaying humanoid's bone turns (vr/doll_pose.h), made on the timer's thread with the bodies, read by the skeleton hook
std::atomic<bool> g_poseWanted{false};
SRWLOCK g_posesLock = SRWLOCK_INIT;
dollpose::Pose g_poses[16];
int g_nPoses = 0;
std::atomic<int> g_nPosesNow{0};   // (g_nPoses, read without the lock by the skinning's look)
constexpr ULONGLONG kPoseFreshMs = 150;   // a pose older than this is not applied (the timer stopped: another view, no weapon)
constexpr ULONGLONG kPoseEaseMs = 300;    // a monster let go of (it died, it went out of reach, its doll came to rest) eases back to its animation over this
                                          // (bearstonem/d2r-pcvr's ease back, feat/monster-limb-push e865def, MIT)

// The poses of the hostile humanoids whose dolls sway now: their limbs' and bodies' offsets (the doll's state) as turns of their own bones.
void PublishPoses(const skel::MonsterRig* rigs, size_t nr, const int* rigOf, ULONGLONG now, float upm) {
    static dollpose::Pose made[16];
    int n = 0;
    if (g_poseWanted.load() && g_swayWanted.load() && viewgate::F4() && dolllib::Loaded()) {
        D2rdState st;
        {
            std::lock_guard<std::mutex> lock(g_swayMutex);
            st = g_doll;
        }
        for (int m = 0; m < g_nHostile && n < 16; ++m) {
            const int r = rigOf[m];
            if (r < 0 || static_cast<size_t>(r) >= nr || rigs[r].doll <= 0 || rigs[r].n <= 0) continue;
            const uint32_t id = g_hostile[m].id;
            const float* body = nullptr;
            for (int i = 0; i < st.nMonsters && i < D2RD_MAX_MONSTERS; ++i)
                if (st.monsters[i].id == id) body = st.monsters[i].s.off;
            dollpose::LimbOff limbs[dollpose::kMaxTurns];
            int nl = 0;
            for (int i = 0; i < st.nLimbs && i < D2RD_MAX_LIMBS && nl < dollpose::kMaxTurns - 1; ++i)
                if (st.limbs[i].id == id) limbs[nl++] = {st.limbs[i].bone, {st.limbs[i].s.off[0], st.limbs[i].s.off[1], st.limbs[i].s.off[2]}};
            if (!body && !nl) continue;
            const skel::MonsterRig& g = rigs[r];
            dollpose::Rig::Cap caps[monsterrig::kMaxPerRig];
            for (int i = 0; i < g.n; ++i) {
                std::memcpy(caps[i].a, g.c[i].a, sizeof caps[i].a);
                std::memcpy(caps[i].b, g.c[i].b, sizeof caps[i].b);
                caps[i].bone = g.c[i].bone;
            }
            dollpose::Rig rig;
            std::memcpy(rig.m3, g.m3, sizeof rig.m3);
            std::memcpy(rig.root, g.root, sizeof rig.root);
            rig.bones = g.bones;
            rig.parent = g.parent;
            rig.caps = caps;
            rig.nCaps = g.n;
            dollpose::Pose& p = made[n];
            if (!dollpose::Build(rig, upm, body, limbs, nl, &p)) continue;
            p.self = g.self;
            p.id = id;
            p.tickMs = now;
            skel::WorldPoses(g.self, &p.poseCur, &p.posePrev, &p.skel);   // (the skinning finds it by them: headshadow's composite)
            ++n;
        }
    }
    AcquireSRWLockExclusive(&g_posesLock);
    n = dollpose::Carry(made, n, 16, g_poses, g_nPoses, now, kPoseEaseMs);   // the ones let go of this time: kept, easing back (dollpose::Faded), for kPoseEaseMs
    for (int i = 0; i < n; ++i) g_poses[i] = made[i];
    g_nPoses = n;
    g_nPosesNow.store(n, std::memory_order_relaxed);
    {   // the turns, once a second while a monster is turned: the strongest pose's, each limb's bone and degrees and the lean (0.188.7: "нпс не шевелятся")
        static ULONGLONG toldAt = 0;
        int s = -1;
        for (int i = 0; i < n; ++i)
            if (!made[i].fadeFromMs && (s < 0 || dollpose::Strongest(made[i]) > dollpose::Strongest(made[s]))) s = i;
        if (s >= 0 && now - toldAt >= 1000) {
            toldAt = now;
            const dollpose::Pose& p = made[s];
            char turns[320];
            int w = 0;
            for (int t = 0; t < p.n && w < (int)sizeof turns - 40; ++t)
                w += snprintf(turns + w, sizeof turns - w, "%s%s %.1f deg", t ? ", " : "", p.t[t].pivot < 0 ? "lean" : ("bone " + std::to_string(p.t[t].pivot)).c_str(),
                              std::fabs(p.t[t].angle) * 57.2957795f);
            LogF("melee touch: monster ragdoll pose - monster %u: %d turn(s) - %s (%d posed now)", p.id, p.n, turns, n);
        }
    }
    ReleaseSRWLockExclusive(&g_posesLock);
}
dollview::Arrow g_swayArrows[4];                   // (under g_swayMutex) the last pushes, for the view
int g_swayArrowAt = 0;

// The bodies of the hostile monsters of the last scan, from their skeletons as they stand now.
std::atomic<bool> g_playerBodies{false};   // [multiplayer] pose_share_bodies: the other players among the bodies (SetPlayerBodies)
playerbodies::PlayerSlots g_playerSlots;    // (the timer's thread only)
void RepublishBodies(ULONGLONG now, const float heroRender[3], float upm) {
    g_rigsAt = now;
    LARGE_INTEGER t0, t1, qf;
    QueryPerformanceCounter(&t0);
    static skel::MonsterRig rigs[16];   // (the timer's thread only)
    const size_t nr = g_nHostile ? skel::MonsterRigs(heroRender[0], heroRender[2], 6.0f * upm, rigs, std::size(rigs)) : 0;
    monsterbodies::Snapshot snap;
    snap.tickMs = now;
    snap.upm = upm;
    int rigged = 0;
    int rigOf[monsterbodies::kMax];
    monsterbodies::Assemble(g_hostile, g_nHostile, rigs, static_cast<int>(nr), upm, &snap, &rigged, rigOf);
    {   // [multiplayer] pose_share_bodies: the other players' bodies, every 10 s while asked - how many, from their skeletons or the whole capsule only
        static ULONGLONG toldAt = 0;
        static unsigned frames = 0, players = 0, limbed = 0, limbs = 0;
        if (g_playerBodies.load(std::memory_order_relaxed)) {
            ++frames;
            for (int m = 0; m < g_nHostile; ++m) {
                if (!playerbodies::IsPlayerId(g_hostile[m].id)) continue;
                ++players;
                if (rigOf[m] >= 0 && static_cast<size_t>(rigOf[m]) < nr) { ++limbed; limbs += static_cast<unsigned>(rigs[rigOf[m]].n); }
            }
            if (now - toldAt >= 10000) {
                if (toldAt) {
                    if (players)
                        LogF("melee touch: player bodies (10 s): %.1f other players a scan - %.0f %% from their own skeletons (%.1f limbs each), the rest the whole capsule only; "
                             "not hostile, ids no game unit has", (double)players / std::max(1u, frames), 100.0 * limbed / players, limbed ? (double)limbs / limbed : 0.0);
                    else
                        Log("melee touch: player bodies (10 s): none - no other player within 24 units (or none alive)");
                }
                toldAt = now;
                frames = players = limbed = limbs = 0;
            }
        }
    }
    for (int m = 0; m < g_nHostile; ++m)   // [weight] grab_bodies: a humanoid's limbs linked up its chain (its skeleton's parents are read for the humanoids only)
        if (rigOf[m] >= 0 && static_cast<size_t>(rigOf[m]) < nr && rigs[rigOf[m]].doll > 0)
            monsterbodies::LinkParents(&snap, g_hostile[m].id, rigs[rigOf[m]].parent, std::min(rigs[rigOf[m]].bones, dollpose::kMaxBones));
    PublishPoses(rigs, nr, rigOf, now, upm);   // [weight] monster_ragdoll_pose
    QueryPerformanceCounter(&t1);
    QueryPerformanceFrequency(&qf);
    const double us = static_cast<double>(t1.QuadPart - t0.QuadPart) * 1e6 / static_cast<double>(qf.QuadPart);
    {   // each model once a verdict: did its skeleton make a body, and of what (the first headset test reads this; a model first met before its parts were there says so again once it has them)
        static std::set<std::string> told;
        for (size_t r = 0; r < nr && told.size() < 128; ++r) {
            const skel::MonsterRig& g = rigs[r];
            if (!g.model[0] || !told.insert(std::string(g.model) + (g.n > 0 ? "|limbs" : "|none")).second) continue;
            if (g.n > 0) {
                float thick = 0.0f, thin = 1e9f;
                for (int i = 0; i < g.n; ++i) { thick = std::max(thick, g.c[i].radius); thin = std::min(thin, g.c[i].radius); }
                LogF("melee touch: monster bodies - '%s': its skeleton, %d limbs (of %d bones, %d with a box%s): %.0f to %.0f cm thick; %d of %d hostile monsters near have their skeletons (%.0f us)",
                     g.model, g.n, g.bones, g.boxes, g.alongBone ? (g.alongBone == 1 ? "; 1 limb bone's box off it: along the bone" : "; limb bones' boxes off them: along the bones") : "",
                     thin / upm * 100.0f, thick / upm * 100.0f, rigged, g_nHostile, us);
                if (g.offBone + g.notBody > 0) {   // (the boxes that are no limb: off their bone, or a rope / a strip of cloth off a humanoid's body - vr/monster_rig.h; every kind named)
                    char b[640];
                    snprintf(b, sizeof b, "melee touch: monster bodies - '%s': %d boxes left out - %d not at their bone, %d not the body: %s", g.model, g.offBone + g.notBody,
                             g.offBone, g.notBody, g.leftOut);
                    Log(b);
                }
            } else {
                LogF("melee touch: monster bodies - '%s': no limbs (%s, %d bones, %d boxes) - a monster of it keeps the game's capsule", g.model, g.why, g.bones, g.boxes);
            }
        }
    }
    if (g_shieldsWanted.load()) {   // [weight] monster_shields: what each model holds, once (and once more when it first gives capsules)
        static std::set<std::string> told;
        for (size_t r = 0; r < nr && told.size() < 256; ++r) {
            const skel::MonsterRig& g = rigs[r];
            if (!g.model[0] || g.n <= 0 || !told.insert(std::string(g.model) + (g.nHeld > 0 ? "|held" : "|none")).second) continue;
            int shields = 0, weapons = 0;
            for (int i = 0; i < g.nHeld; ++i) (g.heldKind[i] == 2 ? shields : weapons) += 1;
            if (g.nHeld > 0) LogF("melee touch: monster shields - '%s' holds: %d shield capsule(s), %d weapon capsule(s) - they stop the weapon (%s)", g.model, shields, weapons, g.heldSaid);
            else LogF("melee touch: monster shields - '%s': nothing it holds made a capsule - %d held part(s)%s%s%s", g.model, g.heldParts,
                      g.heldParts ? ": " : " (no RH / LH / SH child with a model)", g.heldSaid, g.heldOff ? " - boxes on no bone of its skeleton" : "");
        }
    }
    if (g_swayWanted.load()) {   // [weight] monster_ragdoll: each model's verdict once (and again when it changes): a humanoid gets the doll, nothing else does
        static std::set<std::string> told;
        static std::unordered_map<std::string, ULONGLONG> waiting;   // a humanoid by its bones not seen standing: since when (told again once, 5 s on, with why)
        for (size_t r = 0; r < nr && told.size() < 256; ++r) {
            const skel::MonsterRig& g = rigs[r];
            if (!g.model[0] || g.n <= 0) continue;
            if (g.doll == 0) {
                const auto w = waiting.try_emplace(g.model, now).first;
                if (now - w->second >= 5000 && told.insert(std::string(g.model) + "|late").second)
                    LogF("melee touch: monster ragdoll - '%s': still not seen standing (%s) - none", g.model, g.dollPose);
            }
            if (told.insert(std::string(g.model) + "|" + std::to_string(g.doll)).second) {
                if (g.doll > 0 && g.dollWhy && std::strncmp(g.dollWhy, "every monster", 13) == 0) LogF("melee touch: monster ragdoll - '%s': ragdoll (%s)", g.model, g.dollWhy);
                else if (g.doll > 0) LogF("melee touch: monster ragdoll - '%s': humanoid - ragdoll (%s, seen standing)", g.model, g.dollWhy);
                else if (g.doll == 0 && g.still) LogF("melee touch: monster ragdoll - '%s': %s - no doll while it lies so", g.model, g.dollWhy);
                else if (g.doll == 0) LogF("melee touch: monster ragdoll - '%s': humanoid by its bones, not seen standing yet (%s) - none until it is", g.model, g.dollPose);
                else LogF("melee touch: monster ragdoll - '%s': not humanoid (%s) - none", g.model, g.dollWhy);
            }
            if (told.insert(std::string(g.model) + "|bones").second) LogF("melee touch: monster ragdoll - '%s' limbs: %s", g.model, g.dollSaid);
            if (g.doll > 0 && dolllib::Loaded() && upm > 1e-3f && told.insert(std::string(g.model) + "|mass").second) {   // its size and the mass the library gives it (the author: a fallen 30 kg, a zombie 50)
                D2rdBody bodies[monsterrig::kMaxPerRig];
                float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
                for (int i = 0; i < g.n; ++i) {
                    D2rdBody& d = bodies[i];
                    d = D2rdBody{};
                    d.id = 1;
                    d.bone = g.c[i].bone;
                    d.radius = g.c[i].radius / upm;
                    d.rig = d.doll = 1;
                    for (int k = 0; k < 3; ++k) {
                        d.a[k] = g.c[i].a[k] / upm;
                        d.b[k] = g.c[i].b[k] / upm;
                        lo[k] = std::min(lo[k], std::min(d.a[k], d.b[k]) - d.radius);
                        hi[k] = std::max(hi[k], std::max(d.a[k], d.b[k]) + d.radius);
                    }
                }
                D2rdParams p;
                {
                    std::lock_guard<std::mutex> lock(g_swayMutex);
                    p = g_dollParams;
                }
                LogF("melee touch: monster ragdoll - '%s' weighs %.0f kg (%.2f m by %.2f by %.2f; the weight slider %.0f %%)", g.model, dolllib::Mass(&p, bodies, g.n, 1),
                     hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2], p.massScale * 100.0f);
            }
        }
    }
    {   // [debug] monster_rig_dump: one model's reads, once (skel::SetRigDump)
        const std::string dump = skel::RigDumpTakeIn();
        if (!dump.empty()) Log(dump.c_str());
    }
    std::lock_guard<std::mutex> lock(g_bodiesMutex);
    g_bodies = snap;
}

// A scan's hostile monsters kept, and the bodies published from them at once. [weight] npc_bodies: the scan's other units too (town NPCs, hirelings -
// every monster-type unit near that is not dying or dead), marked not hostile - a body for the weapon to meet and stick into, never a target: the
// press asks its own verdict at the touch (Verdicted below, in the strike path), which this does not change.
std::atomic<bool> g_npcBodies{false};
void PublishBodies(const Scan& s, ULONGLONG now, const float heroRender[3], float upm) {
    if (!BodiesWanted()) return;
    const bool npcs = g_npcBodies.load() || g_grabWanted.load();   // ([weight] grab_bodies: an ally is grabbed too - Bodies leaves them out for the rest)
    size_t order[std::tuple_size<decltype(Scan::c)>::value];
    bool hostile[std::tuple_size<decltype(Scan::c)>::value];
    size_t n = 0;
    for (size_t i = 0; i < s.n && n < std::size(order); ++i) {
        const bool yes = Verdicted(s.c[i].id, now) == Know::Yes;
        if (!yes && !npcs) continue;
        hostile[i] = yes;
        order[n++] = i;
    }
    std::sort(order, order + n, [&](size_t a, size_t b) { return s.c[a].d2 < s.c[b].d2; });
    g_nHostile = 0;
    for (size_t k = 0; k < n && g_nHostile < monsterbodies::kMax; ++k) {
        const Cand& c = s.c[order[k]];
        monsterbodies::Body& b = g_hostile[g_nHostile++];
        b = monsterbodies::Body{};
        b.id = c.id;
        b.hostile = hostile[order[k]];
        b.mode = c.mode;
        b.bottom[0] = c.body.bottom.x; b.bottom[1] = c.body.bottom.y; b.bottom[2] = c.body.bottom.z;
        b.top[0] = c.body.top.x; b.top[1] = c.body.top.y; b.top[2] = c.body.top.z;
        b.radius = c.body.radius;
    }
    if (g_playerBodies.load(std::memory_order_relaxed)) {   // the other players, as NPCs: never hostile, an id no game unit has; their limbs come with Assemble
        const float floorM = heroRender[1] / std::max(upm, 1e-3f);
        for (size_t k = 0; k < s.np && g_nHostile < monsterbodies::kMax; ++k) {
            const int slot = g_playerSlots.Slot(s.p[k].id, now);
            stroke::Body body;
            if (slot < 0 || !BodyFor(playerbodies::kPlayerRow, s.p[k].x, s.p[k].z, floorM, upm, body)) continue;
            monsterbodies::Body& b = g_hostile[g_nHostile++];
            b = monsterbodies::Body{};
            b.id = playerbodies::PlayerBodyId(slot);
            b.hostile = false;
            b.mode = -1;
            b.bottom[0] = body.bottom.x; b.bottom[1] = body.bottom.y; b.bottom[2] = body.bottom.z;
            b.top[0] = body.top.x; b.top[1] = body.top.y; b.top[2] = body.top.z;
            b.radius = body.radius;
        }
    }
    RepublishBodies(now, heroRender, upm);
}
// ---- the press and the window the game's target lookups answer in --------------------------------------------------------------

Press g_press;                       // the press and the window (vr/melee_press.h)
#ifdef MELEE_TOUCH_DIRECT
std::atomic<bool> g_keyDown{false};   // the touch's attack is held (PressDown: vrcam's Stand Still reads the press as the touch's)
void SendPress(bool down) {
    g_keyDown.store(down);
    directinput::Action(D2RVR_ACT_ATTACK, down, directinput::kSrcMelee);
}
#else
std::atomic<bool> g_padA{false};
void SendPress(bool down) { g_padA.store(down); }
#endif

// ---- the detector's state: the timer's thread only ------------------------------------------------------------------------------

// One engine and one hit set per hand's slot: with a melee weapon in each hand (a Barbarian's two blades, an Assassin's two claws) each
// blade is its own stroke of its own hand, and a monster answers once per stroke of each.
stroke::Engine g_engine[2];
stroke::HitSet g_hits[2];
// The touch's blade tuned live ([debug] melee_blade_length_pct / melee_blade_radius_cm / melee_blade_flip, bladeshape::Tuning).
std::atomic<float> g_tuneLength{100.0f}, g_tuneRadius{0.0f}, g_tuneButt{100.0f};
// ... and the thrown weapons' own, on top ([debug] melee_thrown_length_pct / melee_thrown_butt_pct, bladeshape::ThrownTuning).
std::atomic<float> g_thrownLength{100.0f}, g_thrownButt{100.0f};
// The engine's two numbers the user sets live ([gameplay] melee_min_speed, melee_stroke_start_cm, stroke::ParamsFor).
std::atomic<float> g_minSpeed{stroke::kDefaultMinSpeed}, g_startCm{stroke::kDefaultStartCm};
// ... per weapon kind, 0 = the general ones ([gameplay] melee_min_speed_<kind>, melee_stroke_start_cm_<kind>); and the strike rules (toward the monster, the head, the amplitude: [gameplay] melee_toward*, melee_head_only*, melee_amp*, vr/strike_direction.h).
std::atomic<float> g_kindSpeed[strikedir::kKinds], g_kindStart[strikedir::kKinds];
std::mutex g_ruleMutex;
strikedir::Rule g_strikeRule;   // under g_ruleMutex
strikedir::Rule CurrentRule() { std::lock_guard<std::mutex> lock(g_ruleMutex); return g_strikeRule; }
// [weight] strike_by_force (vr/strike_force.h): its settings (under g_ruleMutex), and per slot the strikes already pressed (the timer's thread only): a monster
// once per strike - again only with strike_force_stuck 1, when RepeatDue says - and the strikes already told as refused (the log)
strikeforce::Settings g_force;
std::atomic<bool> g_forceDebug{false};   // [debug] strike_force_debug
strikeforce::Settings CurrentForce() { std::lock_guard<std::mutex> lock(g_ruleMutex); return g_force; }
struct ForcePressed {
    struct E { uint32_t serial = 0, id = 0; ULONGLONG at = 0; int presses = 0; };
    E e[16];
    int next = 0;
    E* Find(uint32_t serial, uint32_t id) {
        for (E& x : e) if (x.serial == serial && x.id == id) return &x;
        return nullptr;
    }
    void Note(uint32_t serial, uint32_t id, ULONGLONG at) {
        E* x = Find(serial, id);
        if (!x) { x = &e[next++ % 16]; *x = E{}; x->serial = serial; x->id = id; }
        x->at = at;
        ++x->presses;
    }
};
ForcePressed g_forcePressed[2];
uint32_t g_forceTold[16] = {};
int g_forceToldNext = 0;
bool ForceToldOnce(uint32_t serial) {
    for (uint32_t s : g_forceTold) if (s == serial) return false;
    g_forceTold[g_forceToldNext++ % 16] = serial;
    return true;
}
// Per hand (the timer's thread only): how far the head has gone in the stroke that is on (the amplitude), and its way over the last few poses (the debug's look of the blade).
strikedir::StrokeTravel g_travel[2];
strikedir::TipWay g_tipWay[2];
std::atomic<bool> g_tuneFlip{false};
// [gameplay] melee_thrust (vr/thrust_stroke.h), per hand (the timer's thread only): a second engine on the same samples with the thrust's numbers, its own hit set (a monster once
// per thrust; one pressed by either engine is marked in the other's set for the stroke that is on there, so one movement both count is one press), and the thrust's path along
// the blade. The rule under g_ruleMutex.
stroke::Engine g_thrustEngine[2];
stroke::HitSet g_thrustHits[2];
thrust::Path g_thrustPath[2];
std::atomic<ULONGLONG> g_stabbingAt[2] = {};   // [weight] stab_in: when each hand's pointed weapon last moved point first (StabbingNow)
thrust::PointFirst g_pointFirst[2];             // ... the stab's own judge of it, every pointed blade (the timer's thread only)
thrust::Rule g_thrustRule;   // under g_ruleMutex
thrust::Rule CurrentThrust() { std::lock_guard<std::mutex> lock(g_ruleMutex); return g_thrustRule; }

// [debug] thrust_debug (vr/thrust_debug.h, the timer's thread only): per hand the last thrust's contact with a monster (TOUCH) and the last jab, touching or not (JAB), the jab being
// made, and the blade's tint - green for a counted thrust, red for a refused one, for a moment. Shown over the hand last read (vr/thrust_debug_wire.h); a drawing and a log only.
thrustdbg::Reading g_dbgTouch[2], g_dbgJab[2];
thrustdbg::JabTracker g_dbgJabs[2];
int g_dbgSide = 0;
int g_dbgTint[2] = {-1, -1};
ULONGLONG g_dbgTintUntil[2] = {};
uint64_t g_dbgTouchStroke[2] = {};   // the thrust stroke the last TOUCH was of (its jab is not told again as a jab with no monster)
uint64_t g_dbgLiveStroke[2] = {};    // the jab already tinted blue while it was made (a touch would have counted): its end is not tinted again
constexpr ULONGLONG kDbgTouchTintMs = 1000, kDbgJabTintMs = 500, kDbgLiveTintMs = 150;   // live: renewed every pose while a touch would count
void DbgTint(int side, bool ok, ULONGLONG now, ULONGLONG ms) {
    g_dbgTint[side & 1] = ok ? meleeboxes::kCThrustOk : meleeboxes::kCThrustNo;   // the thrust's own tint: blue / orange, apart from the swing's green / red
    g_dbgTintUntil[side & 1] = now + ms;
}
int DbgTintNow(int side, ULONGLONG now) { return thrustdebugwire::Wanted() && now < g_dbgTintUntil[side & 1] ? g_dbgTint[side & 1] : -1; }
// A contact of a thrust with a monster: kept, the blade tinted, and one line in the log for each thrust and monster.
void DbgTouch(int side, const thrustdbg::Reading& r, ULONGLONG now, bool tell) {
    if (!thrustdbg::Shown(r, thrustdebugwire::Only())) return;   // [debug] thrust_debug_only: a movement that is not a thrust is not shown
    g_dbgTouch[side & 1] = r;
    g_dbgTouchStroke[side & 1] = r.stroke;
    g_dbgSide = side & 1;
    DbgTint(side, r.counted, now, kDbgTouchTintMs);
    if (!tell) return;
    char accel[60];
    if (r.needAccel > 0.0f) snprintf(accel, sizeof accel, "%.1f / %.0f m/s^2", r.accel, r.needAccel);
    else snprintf(accel, sizeof accel, "%.1f m/s^2 (not asked)", r.accel);
    LogF("thrust debug: %c hand, %s thrust %llu on monster %u - %s%s | speed %.2f / %.2f m/s%s, along %.1f / %.0f cm%s, straight %.0f / %.0f %%, turn %.0f / %.0f deg, accel %s%s", side ? 'L' : 'R',
         r.kind >= 0 ? strikedir::KindName(r.kind) : "weapon", static_cast<unsigned long long>(r.stroke), r.monster, r.counted ? "COUNTED" : "REJECTED: ", r.counted ? "" : thrustdbg::FailText(r.fail),
         r.speed, r.needSpeed, r.ownSpeed ? " (own)" : "", r.cm, r.needCm, r.ownCm ? " (own)" : "", r.alongPct, r.needAlongPct, r.turnDeg, r.needTurnDeg, accel, r.ownAccel ? " (own)" : "");
}
// The lines over the weapon hand: the hand last read - its last touch and its last jab - or why that blade does not thrust.
void DbgPublish(const char* why) {
    if (!thrustdebugwire::Wanted()) return;
    thrustdbg::Line lines[thrustdebugwire::kMaxLines];
    size_t n = 0;
    if (why) {
        std::snprintf(lines[0].text, thrustdbg::kLineChars, "%s", why);
        lines[0].colour = thrustdbg::kGrey;
        n = 1;
    } else {
        n += thrustdbg::Lines(g_dbgTouch[g_dbgSide], "TOUCH", lines + n, thrustdebugwire::kMaxLines - n);
        n += thrustdbg::Lines(g_dbgJab[g_dbgSide], "JAB", lines + n, thrustdebugwire::kMaxLines - n);
    }
    thrustdebugwire::SetLines(lines, n, g_dbgSide);
}

// The blade's shape for the item in a hand as the touch tests it NOW: the weapon model's own ends where the pose has a unit (unitM, metres per model unit,
// vr/weapon_extents.h), else the kind's and the base item's constants, the general sliders on top, and for a thrown kind its own two on top of those.
bladeshape::Shape TunedShape(uint32_t code, uint32_t kind, bool twoHandsOn, float unitM) {
    const bladeshape::Tuning tune{g_tuneLength.load(), g_tuneRadius.load(), g_tuneFlip.load(), g_tuneButt.load()};
    bladeshape::Shape base = bladeshape::ForItem(code, kind, twoHandsOn, unitM);
    metalhead::Volume vol;   // [gameplay] melee_head_volume: a headed weapon's shaft as thin as its model's (its head is a volume of its own)
    if (g_headVolume.load() && unitM > 0.0f && metalhead::VolumeOf(code, strikedir::KindIndex(kind), &vol)) metalhead::ThinShaft(vol.shaftR, unitM, &base.length, &base.butt, &base.radius);
    bladeshape::Shape s = bladeshape::Tuned(base, tune);
    if (ThrownKind(kind)) s = bladeshape::TunedThrown(s, {g_thrownLength.load(), g_thrownButt.load()});
    return s;
}
uint32_t g_lastPoseStamp = 0;
ULONGLONG g_lastScanAt = 0, g_lastReportAt = 0, g_lastToldAt = 0;
Scan g_scan;
struct Counters { unsigned sweeps = 0, touched = 0, pressed = 0, notHostile = 0, noVerdict = 0, noHero = 0, noHand = 0, slowPart = 0, away = 0, notHead = 0, small = 0, thrustSweeps = 0, thrusts = 0, thrustShort = 0, thrustWeak = 0; } g_n;

// [gameplay] melee_thrust: one line per thrust in the log - each counted or refused thrust (the stroke of the thrust engine, the monster) is told once, with its speed where it touched,
// the cm its point went along the blade, the part of its path along it and its peak acceleration (to tune melee_thrust_* from the log). The first twelve refusals always (as the swing's),
// every one with [debug] melee_boxes. The timer's thread only.
bool ThrustToldOnce(int side, uint64_t stroke, uint32_t monster) {
    static uint64_t told[2][8] = {};
    static uint32_t toldId[2][8] = {};
    static unsigned next[2] = {};
    const int s = side & 1;
    for (int i = 0; i < 8; ++i)
        if (told[s][i] == stroke && toldId[s][i] == monster) return false;
    told[s][next[s] % 8] = stroke;
    toldId[s][next[s] % 8] = monster;
    ++next[s];
    return true;
}
// "peak 31.0 m/s^2 (asked 12)" / "(not asked)"
void AccelText(char* out, size_t n, const thrust::Verdict& v) {
    if (v.needAccel > 0.0f) snprintf(out, n, "peak acceleration %.1f m/s^2 (needs %.0f)", v.accel, v.needAccel);
    else snprintf(out, n, "peak acceleration %.1f m/s^2 (not asked)", v.accel);
}

void ResetThrust(int hand) {
    g_thrustEngine[hand & 1].Reset();
    g_thrustPath[hand & 1].Reset();
}

void Cancel() {
    g_engine[0].Reset();
    g_engine[1].Reset();
    ResetThrust(0);
    ResetThrust(1);
    g_lastPoseStamp = 0;
    doubleswing::Forget();   // the touch stands down (another view, a menu, another weapon): the pairs of strikes are forgotten
}

void ReleasePress(ULONGLONG now, bool force) {
    if (g_press.Release(now, force)) SendPress(false);
}

// The press -> hit times (vr/hit_latency.h): the moment we ask the game to attack, against the moment its hit-flags function
// answers "landed" for a player (the observer in vr/haptics.cpp, shared). Counting only - nothing here changes what is pressed.
std::atomic<AnimNoteFn> g_animNote{nullptr};   // [debug] bow_latency: the hero's animation row for the press->hit line (vrcam)
std::atomic<PressNoteFn> g_pressNote{nullptr};          // [debug] melee_press_trace (vrcam): the key route's press ...
std::atomic<PressOutcomeFn> g_pressOutcome{nullptr};    // ... and the hit counter's verdict on it
hitlatency::Stats g_lat;             // the timer's thread only
hitlatency::Stats g_latDirect;       // the same for the direct hit's route (call -> hit), [gameplay] melee_direct
ULONGLONG g_latPollAt = 0, g_latReportAt = 0;
unsigned g_latTold = 0, g_latToldDirect = 0;

void LatencyPoll(ULONGLONG now) {
    if (now - g_latPollAt < 25) return;
    g_latPollAt = now;
    hitlatency::Tracker::Outcome o[8];
    for (int n; (n = hitlatency::Collect(hitlatency::kMelee, o, 8)) > 0;)
        for (int i = 0; i < n; ++i) {
            g_lat.Add(o[i]);
            if (const PressOutcomeFn fn = g_pressOutcome.load()) fn(o[i].hit, o[i].ms);   // [debug] melee_press_trace
            if (o[i].hit && ++g_latTold <= 200) {   // (the first 200; the 10 s line goes on)
                char note[200] = {};
                const AnimNoteFn fn = g_animNote.load();
                if (fn && fn(note, sizeof note) > 0) LogF("melee touch: press->hit %.0f ms | the hero's animation row: %s", o[i].ms, note);   // [debug] bow_latency
                else LogF("melee touch: press->hit %.0f ms", o[i].ms);
            }
        }
    for (int n; (n = hitlatency::Collect(hitlatency::kMeleeDirect, o, 8)) > 0;)
        for (int i = 0; i < n; ++i) {
            g_latDirect.Add(o[i]);
            if (o[i].hit && ++g_latToldDirect <= 200) LogF("melee touch (direct): call->hit %.0f ms", o[i].ms);
        }
    if (now - g_latReportAt >= 10000) {
        g_latReportAt = now;
        if (g_lat.Any()) {
            char b[300];
            g_lat.Line(b, sizeof b, "melee touch:");
            Log(b);
        }
        if (g_latDirect.Any()) {
            char b[300];
            g_latDirect.Line(b, sizeof b, "melee touch (direct, call->hit):");
            Log(b);
        }
    }
}

// `hand`: the controller whose blade touched (skeleton side, 0 right / 1 left) - it claims the blow the game is about to roll (vr/hit_claim.h), so the buzz of the hit is that hand's, whichever hand
// swung: a Barbarian's second blade, a weapon drawn into the left hand.
// [gameplay] melee_direct_client: the force of the weight model's live strike on monster `id` (vr/strike_force.h Score, the strongest of its strikes on it now), N s;
// -1 when it measures none (the weight model off, no monster bodies, no strike on that monster yet).
float ForceNow(uint32_t id) {
    weight::LiveStrike ls[8];
    const int n = weight::LiveStrikes(ls, 8);
    const strikeforce::Settings s = CurrentForce();
    float best = -1.0f;
    for (int i = 0; i < n; ++i) {
        if (ls[i].s.id != id) continue;
        const strikeforce::Strike k{ls[i].s.id, ls[i].s.serial, ls[i].s.hands, ls[i].s.window, ls[i].s.cut, ls[i].s.recent, ls[i].s.startS, ls[i].s.along};
        best = std::max(best, strikeforce::Score(s, strikeforce::FamilyOf(ls[i].kind), k));
    }
    return best;
}

// [debug] melee_touch_no_attack: the touch counted, but the game's attack is not asked for - the drawing says where it struck, the log tells the first few
bool NoAttack(uint32_t id, ULONGLONG now) {
    if (!g_noAttack.load()) return false;
    meleeboxes::Struck(id, meleeboxes::kStruckKey, now);   // [debug] melee_boxes: the drawing only
    static unsigned told = 0;
    if (told < 12) { ++told; LogF("melee touch: monster %u would be attacked - not pressed ([debug] melee_touch_no_attack)", id); }
    return true;
}

void PressKey(uint32_t id, bool allowDirect, int hand, ULONGLONG now);
// `touch`: the touch as measured ([gameplay] melee_direct_client's rules: speed, force, the swing's length); `twoHanded` the weapon.
void Approve(uint32_t id, bool allowDirect, int hand, const meleedirectclient::Touch& touch = {}, bool twoHanded = false, bool twoHandsOn = false) {
    frametrace::Note(frametrace::kPress);   // [debug] frame_trace: a touch was accepted - the game's attack is asked for (key or direct)
    const ULONGLONG now = GetTickCount64();
    if (NoAttack(id, now)) return;
    // [gameplay] melee_direct (the user's experimental option, vr/melee_direct.h): the touch calls the game's attack itself.
    // Off, or not possible (no local server, a signature missing, a fault): the route is Key and nothing below changes.
    // Never for a thrown weapon (allowDirect false): the server's basic attack is a melee blow, not the throw the game's own attack makes of it.
    if (allowDirect && meleedirect::Wanted()) {
        const meleedirect::Route route = meleedirect::Plan(now, g_serverGame.load() != 0 && g_heroId.load() != 0);
        if (route == meleedirect::Route::Skip) return;
        if (route == meleedirect::Route::Direct && meleedirect::ByRules()) {   // [gameplay] melee_direct 2: the touch by the rules (melee_direct_client.h)
            static unsigned toldRules = 0;
            const bool tell = toldRules < 20 || meleeboxes::Wanted();
            if (tell) ++toldRules;
            bool ready = false, extra = false;
            if (!meleedirectclient::HostRules(id, touch, twoHanded, twoHandsOn, meleearm::Armed(&ready) >= 0, tell, now, &extra)) {
                PressKey(id, allowDirect, hand, now);   // a rule failed: the normal attack
                return;
            }
        }
        if (route == meleedirect::Route::Direct) {
            hitlatency::Press(hitlatency::kMeleeDirect);   // the clock starts before the request can run
            hitclaim::Press(hand, static_cast<int64_t>(now));
            g_latDirect.Press();
            meleedirect::Request req;
            req.game = g_serverGame.load();
            req.heroId = g_heroId.load();
            req.monsterId = id;
            req.lookup = g_origLookup ? reinterpret_cast<uintptr_t>(g_origLookup) : d2rsig::Addr(RVA_LOOKUP);
            meleedirect::Submit(req, now);
            meleeboxes::Struck(id, meleeboxes::kStruckDirect, now);   // [debug] melee_boxes: the drawing only
            return;
        }
    }
    // [gameplay] melee_direct_client (vr/melee_direct_client.h): a joined TCP/IP player (no server of ours here) - the touch is the game's own command
    // "left skill on this monster", sent through the funnel every command of the player goes through; the host's server does the rest. Off, not a
    // joined game, another skill on the left, a skill armed, a thrown weapon: false, and the key path below as before.
    if (meleedirectclient::Wanted() && g_serverGame.load() == 0) {
        bool ready = false;
        const bool armed = meleearm::Armed(&ready) >= 0;
        static unsigned told = 0;   // each touch's verdict on one line: the first twenty, then with [debug] melee_boxes (a log line costs a frame on a slow PC)
        const bool tell = told < 20 || meleeboxes::Wanted();
        if (tell) ++told;
        if (meleedirectclient::Send(g_heroId.load(), id, hand, allowDirect, armed, touch, twoHanded, twoHandsOn, tell, now)) {
            hitclaim::Press(hand, static_cast<int64_t>(now));
            meleeboxes::Struck(id, meleeboxes::kStruckDirect, now);   // [debug] melee_boxes: the drawing only
            return;
        }
    }
    PressKey(id, allowDirect, hand, now);
}

// The touch's attack on the game's own key: the press, and the window in which the game's target lookups answer with monster `id`.
void PressKey(uint32_t id, bool allowDirect, int hand, ULONGLONG now) {
    meleeboxes::Struck(id, meleeboxes::kStruckKey, now);   // [debug] melee_boxes: the drawing only
    if (g_press.Begin(id, now)) {
        meleearm::OnPress(allowDirect);   // [gameplay] melee_skill_armed: this press is the armed skill or the plain attack (never for a thrown weapon: allowDirect false)
        if (const PressNoteFn fn = g_pressNote.load()) fn(id, hand);   // [debug] melee_press_trace: what the plugin holds now, before the press goes out
        SendPress(true);
        hitclaim::Press(hand, static_cast<int64_t>(now));
        hitlatency::Press(hitlatency::kMelee);
        g_lat.Press();
    }
}

// [hands] double_swing_by_hands (vr/double_swing.h): the second hand of a pair touched the monster the first hand struck - the game's own Double Swing (or Frenzy) instead of its attack key. The pad button of
// the slot the skill sits in is pressed (a finger on it: 65 ms, 180 on the left trigger's row), as the shield bash's Smite is, and the window in which the game's target lookups answer with the touched
// monster is opened just the same, so the cast goes to the monster the blade touched. The swings, the mana, the damage and the animation are the game's; nothing is called that attacks.
std::atomic<uint32_t> g_dualPad{0};   // the pad press of the last dual cast (buttons | lt << 16), let go when the game's click has asked for the target
void ApproveDual(uint32_t id, int hand, const doubleswing::Press& p) {
    frametrace::Note(frametrace::kPress);
    const ULONGLONG now = GetTickCount64();
    if (NoAttack(id, now)) return;
    meleeboxes::Struck(id, meleeboxes::kStruckKey, now);   // [debug] melee_boxes: the drawing only
    if (g_press.Down()) ReleasePress(now, true);   // the first hand's attack key is let go before the skill's button goes down: two pad buttons at once are not a press the game reads
    g_press.OpenWindow(id, now);
    hitclaim::Press(hand, static_cast<int64_t>(now));   // the blow is the second hand's
    hitlatency::Press(hitlatency::kMelee);
    g_lat.Press();
#ifdef MELEE_TOUCH_DIRECT
    g_dualPad.store(static_cast<uint32_t>(p.buttons) | static_cast<uint32_t>(p.lt) << 16);
    directinput::PadPulse({p.buttons, p.lt, 0}, p.holdMs);
#else
    (void)p;
#endif
}

// ---- [debug] melee_boxes (vr/melee_boxes.h): what is handed over for drawing and for the log ------------------------------------
// READ-ONLY on everything above: it has its own scan (a fresh one every 20 ms, so the capsules follow the monsters) and its own
// copies; nothing of it feeds back into the detector. With the key off none of this is called and nothing is scanned.
float g_sweepSpeed[2] = {};          // the last sweep each hand's engine made, for the log
ULONGLONG g_sweepAt[2] = {};

// One blade of this pose, with the hand sample it was made from: [0] the primary (the hand slot PosedWeapon names), [1] the other hand's.
struct SideBlade {
    bool ok = false;
    int side = 0;
    uint32_t kind = 0;
    uint32_t code = 0;          // the item's code (bladeshape::Code)
    bladeshape::Shape shape;
    float unitM = 0.0f;         // metres per unit of the weapon model as drawn (skel::PosedBlade::unitM); 0 = the blade is the kind's constants
    int zSign = 0;              // a fist weapon's blade along the bone's Z: the sign the game's own pose gives that axis (skel::PosedBlade::zSign)
    stroke::Sample s;
    bool ball = false;          // [flail] chain: the blade is the flail's ball on its chain (skel::PosedBlade::ball)
    int thrustClass = -1;       // the item's class in the game's data: 1 a thrust (1ht, 2ht), 0 another, -1 not read (Frame::thrustClass)
    int slot = 0;               // the game's slot of the item (0 right, 1 left): [weight] strike_by_force's strikes are the weight model's by slot
    bool thrusts = false;       // [gameplay] melee_thrust: this blade's thrusts count (thrust::Applies; never a flail's ball, never without the blade in the room)
    float headAlong = -1.0f;    // [gameplay] melee_metal_head: where its head starts along it (0 the butt end, 1 the tip; metalhead::Along), -1 nothing to refuse
    metalhead::Source headFrom = metalhead::Source::None;   // ... and where that came from (the log)
    int nHeads = 0;             // [gameplay] melee_head_volume: the head's own volume (metalhead::Volume) where the pose puts it - or ([flail] chain) a three-ball
    stroke::Blade heads[meleeboxes::kHeads];   // flail's other balls, each its own sphere; render metres, as s.blade. Each strikes as the head.
};
// [gameplay] melee_head_volume: each hand's head volume the pose before (its sweep runs from there to where it is now). The timer's thread only.
struct HeadPrev {
    bool ok = false;
    stroke::Blade b;
    double t = 0.0;
    uint64_t epoch = 0, context = 0;
};
HeadPrev g_headPrev[2][meleeboxes::kHeads];

// [debug] melee_boxes: the log line of a refused contact, throttled: a line each 250 ms for each of the reasons (0 not toward the monster, 1 not the head, 2 a swing too small, 3 too slow where it
// touched, 4 a movement that never became a stroke). `always`: told whatever the debug says (the first dozen, for the log of the first test). False: not now.
bool TellRefusal(int reason, ULONGLONG now, bool always) {
    static ULONGLONG at[5] = {};
    if (thrustdebugwire::Only()) return false;   // (the swing's lines: not with [debug] thrust_debug_only)
    if (reason < 0 || reason > 4) return false;
    if (!always && (!meleeboxes::Wanted() || now - at[reason] < 250)) return false;
    at[reason] = now;
    return true;
}

// [debug] melee_boxes: a contact's marks - a ball on the touching point, an arrow along its velocity, a line to the nearest point of the monster's axis, and for a refusal the monster's capsule
// tinted - in the verdict's colour (meleeboxes::Colour). Read-only drawing.
// thrustMark: a thrust's contact ([debug] thrust_debug_only leaves out every other).
void MarkContact(const strikedir::Judgement& jd, uint32_t monsterId, const stroke::Body& body, int colour, bool tint, ULONGLONG now, bool thrustMark = false) {
    if (!meleeboxes::Wanted() || !jd.haveWay) return;
    if (!thrustMark && thrustdebugwire::Only()) return;
    meleeboxes::ContactMark m;
    m.at = jd.at;
    m.dir = jd.dir;
    m.onAxis = jd.onAxis;
    m.haveAxis = jd.haveAxis;
    m.colour = colour;
    m.tint = tint;
    m.monsterId = monsterId;
    m.body = body;
    m.haveBody = true;
    m.ms = now;
    meleeboxes::Touched(m);
}

// [weight] monster_ragdoll: the blades the weight model does not hold - a weapon in each hand (the weight model holds a weapon held alone, in either hand) - push
// the doll themselves, each hand's blade the bodies it enters (vr/blade_push.h: the steel and the arm times the speed in, as the weight model's strike). The
// headset 2026-10-23: «оружие в двух руках - кукла прозрачная». weightHolds: the weight model holds this pose's weapon (it pushes the doll itself).
bladepush::Tracker g_bladePush[2];
// [weight] monster_shields: on a monster with a shield or a weapon, the weight model's weapon must have reached its BODY - a blow its shield or its weapon
// stopped is no hit (the author: «при этом удар не засчитывался»); a blow that lands goes to the game's own attack and its own block roll as ever. Asked
// AFTER every rule a hit is judged by (the least speed, the stroke's or the thrust's own, toward the monster, the head of the blade, the verdict): a contact
// that gets here would have counted. So a blow the shield (or the weapon) stopped is a BLOCKED BLOW - told once per stroke per monster, kept for the
// shield-hit sound (LastBlockedBlow); a touch, or a blade resting on the shield, never gets this far. True: the hit is held back.
bool HoldsSomething(uint32_t id) {   // has this monster a shield or a weapon among the bodies now?
    std::lock_guard<std::mutex> lock(g_bodiesMutex);
    for (int i = 0; i < g_bodies.n; ++i)
        if (g_bodies.b[i].id == id && g_bodies.b[i].held) return true;
    return false;
}
// [weight] parry_counts_hit: is this monster in an attack now (its unit's mode at the last scan)?
bool AttackingNow(uint32_t id) {
    std::lock_guard<std::mutex> lock(g_bodiesMutex);
    return parrywire::Attacking(g_bodies, id);
}
// ... the weapons of the monsters in an attack, this pose (the timer's thread only): a blow on one is a blow on its monster.
struct AttackWeapon { uint32_t id = 0; stroke::Body body; };
AttackWeapon g_attackWeapons[monsterbodies::kMax];
int g_nAttackWeapons = 0;
void GatherAttackWeapons() {
    g_nAttackWeapons = 0;
    if (!parrywire::CountsHitWanted()) return;
    std::lock_guard<std::mutex> lock(g_bodiesMutex);
    for (int i = 0; i < g_bodies.n && g_nAttackWeapons < monsterbodies::kMax; ++i) {
        const monsterbodies::Body& b = g_bodies.b[i];
        if (b.held != 1 || !parry::MonsterAttacking(b.mode)) continue;
        AttackWeapon& w = g_attackWeapons[g_nAttackWeapons++];
        w.id = b.id;
        w.body = {{b.bottom[0], b.bottom[1], b.bottom[2]}, {b.top[0], b.top[1], b.top[2]}, b.radius};
    }
}
// The contact of a sweep with a monster: its body's, or - [weight] parry_counts_hit, the monster in an attack - its weapon's where that comes first
// (*on: the capsule touched; *viaWeapon: it was the weapon).
stroke::Contact TouchOrWeapon(const stroke::Sweep& sweep, uint32_t id, const stroke::Body& body, stroke::Body* on, bool* viaWeapon) {
    stroke::Contact touch = stroke::TouchOf(sweep, body);
    *on = body;
    *viaWeapon = false;
    for (int i = 0; i < g_nAttackWeapons; ++i) {
        if (g_attackWeapons[i].id != id) continue;
        const stroke::Contact w = stroke::TouchOf(sweep, g_attackWeapons[i].body);
        if (w.t >= 0.0f && (touch.t < 0.0f || w.t < touch.t)) { touch = w; *on = g_attackWeapons[i].body; *viaWeapon = true; }
    }
    return touch;
}
void TellWeaponHit(int side, uint32_t id, float speed, bool thrust) {   // (once a second at most)
    static ULONGLONG at = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - at < 1000) return;
    at = now;
    LogF("melee touch: %c hand, a %s on monster %u's weapon in its attack at %.1f m/s - counts as a hit on it ([weight] parry_counts_hit; the game's own rolls decide)", side ? 'L' : 'R',
         thrust ? "thrust" : "blow", id, speed);
}

stroke::HitSet g_blockedBlows[2];
std::mutex g_blockedMutex;
BlockedBlow g_lastBlocked;
bool ShieldStops(const Frame& f, int side, uint32_t id, uint64_t session, uint64_t strokeId, float speed, bool thrust, ULONGLONG now) {
    if (!g_shieldsWanted.load() || !f.weightPlaced || !HoldsSomething(id) || weight::Reached(id, 250)) return false;
    const int by = weight::Blocked(id, 250);
    if (by == 1 && parrywire::CountsHitWanted() && AttackingNow(id)) {   // [weight] parry_counts_hit: its weapon in its attack took the blow - a blow on it
        TellWeaponHit(side, id, speed, thrust);
        return false;
    }
    if (by && g_blockedBlows[side & 1].Insert(session, strokeId, id)) {
        {
            std::lock_guard<std::mutex> lock(g_blockedMutex);
            g_lastBlocked.id = id;
            g_lastBlocked.side = side & 1;
            g_lastBlocked.held = by;
            g_lastBlocked.speed = speed;
            g_lastBlocked.thrust = thrust;
            g_lastBlocked.ms = now;
            ++g_lastBlocked.count;
        }
        LogF("melee touch: %c hand, a %s on monster %u at %.1f m/s - its %s stopped it: a blow, no hit", side ? 'L' : 'R', thrust ? "thrust" : "blow", id, speed,
             by == 2 ? "shield" : "weapon");
    } else if (!by) {
        static ULONGLONG toldAt = 0;
        if (now - toldAt >= 500) {
            toldAt = now;
            LogF("melee touch: %c hand, monster %u is not hit yet - the weapon has not reached its body (it holds a shield or a weapon)", side ? 'L' : 'R', id);
        }
    }
    return true;
}

bool LastBlockedBlowLocked(BlockedBlow* out) {
    std::lock_guard<std::mutex> lock(g_blockedMutex);
    if (out) *out = g_lastBlocked;
    return g_lastBlocked.count > 0;
}

void PushWithBlades(ULONGLONG now, const SideBlade (&sb)[2], bool weightHolds) {
    bool have[2] = {};
    static monsterbodies::Snapshot snap;   // (the timer's thread only: too big for its stack every pose)
    static bladepush::Body bodies[monsterbodies::kMax];
    int n = 0;
    if (g_swayWanted.load() && !weightHolds && dolllib::Loaded()) {
        {
            std::lock_guard<std::mutex> lock(g_bodiesMutex);
            snap = g_bodies;
        }
        {   // (the weight model holds no weapon: nobody else steps the doll - it springs back by this clock)
            std::lock_guard<std::mutex> lock(g_swayMutex);
            StepSwayLocked();
        }
        for (int i = 0; i < snap.n && n < monsterbodies::kMax; ++i) {
            const monsterbodies::Body& b = snap.b[i];
            bladepush::Body& d = bodies[n++];
            d.bottom = {b.bottom[0], b.bottom[1], b.bottom[2]};
            d.top = {b.top[0], b.top[1], b.top[2]};
            d.radius = dollview::MeetRadius(b);   // (the weight model's own capsule of it)
            d.id = b.id;
            d.bone = b.rig ? b.bone : -1;
        }
        for (const SideBlade& s : sb) {
            if (!s.ok) continue;
            const int side = s.side & 1;
            have[side] = true;
            const stroke::Blade& bl = s.s.blade;
            const bladepush::Blade blade{{bl.base.x, bl.base.y, bl.base.z}, {bl.tip.x, bl.tip.y, bl.tip.z}, bl.radius};
            bladepush::Push p[4];
            const int np = g_bladePush[side].Step(true, blade, (double)now * 0.001, bladepush::SteelKg(strikedir::KindIndex(s.kind)), bodies, n, p, 4);
            for (int k = 0; k < np; ++k) {
                const float imp[3] = {p[k].impulse.x, p[k].impulse.y, p[k].impulse.z}, at[3] = {p[k].point.x, p[k].point.y, p[k].point.z};
                PushBody(snap, p[k].body, imp, at);
            }
        }
    }
    for (int side = 0; side < 2; ++side)
        if (!have[side]) g_bladePush[side].Step(false, {}, (double)now * 0.001, 0.0f, nullptr, 0, nullptr, 0);   // (no blade, or the weight model's: starts afresh)
}

// [gameplay] melee_not_butt: the front hand's place along this blade (0 its butt end, 1 its tip; vr/strike_direction.h Rule::frontAlong) - the blade runs
// from the weapon bone's origin less its butt to the origin plus its length (bladeshape::FromBone; both from the model's own mesh, weapon_extents.h). The
// hand: the weight model's foremost holding hand on the shaft (weight::HandOnShaft - a poleaxe held in the middle: the middle) when it holds this blade,
// else the game's own grip, the bone's origin. -1: no butt to speak of (a fist weapon's blade starts ahead of the hand; a flail's ball).
// ... and the log, once a session, the first time a butt touch is refused (then with the other refusals, throttled)
void TellButtOnce(int side, uint32_t id, float along, float front, float speed, const char* kind) {
    static bool told = false;
    if (told) return;
    told = true;
    LogF("melee touch: %c hand, the %s's butt touched monster %u at %.1f m/s - no hit (at %.0f%% of its length, behind the front hand at %.0f%%: [gameplay] melee_not_butt)", side ? 'L' : 'R',
         kind, id, speed, along * 100.0f, front * 100.0f);
}
// [gameplay] melee_metal_head: the log, once a session, the first time a touch with the shaft below a weapon's head is refused (then with the other refusals, throttled)
void TellShaftOnce(int side, uint32_t id, float along, float head, float speed, const char* kind, metalhead::Source from) {
    static bool told = false;
    if (told) return;
    told = true;
    LogF("melee touch: %c hand, the %s's shaft touched monster %u at %.1f m/s - a push, no hit (at %.0f%% of its length, the head starts at %.0f%%: %s; [gameplay] melee_metal_head)",
         side ? 'L' : 'R', kind, id, speed, along * 100.0f, head * 100.0f, metalhead::SourceText(from));
}
float FrontAlong(const SideBlade& sb, bool weightHolds) {
    const float len = sb.shape.butt + sb.shape.length;
    if (sb.ball || !(sb.shape.butt > 0.0f) || !(len > 1e-3f)) return -1.0f;
    float ahead = 0.0f;
    if (weightHolds) {
        bool any = false;
        for (int side = 0; side < 2; ++side) {
            float a = 0.0f;
            if (weight::HandOnShaft(side, &a) && a > -sb.shape.butt && a < sb.shape.length) { ahead = any ? std::max(ahead, a) : a; any = true; }   // (a hand off the weapon: not taken)
        }
    }
    return std::clamp((sb.shape.butt + ahead) / len, 0.0f, 0.8f);   // (the outer fifth always strikes, whatever the hands' numbers say)
}

void FeedBoxes(ULONGLONG now, const SideBlade (&sb)[2], float upm, float floorM, const stroke::Vec& grid) {
    static ULONGLONG scanAt = 0;
    static Scan scan;
    static meleeboxes::Monster monsters[meleeboxes::kMaxMonsters];
    static size_t count = 0;
    if (now - scanAt >= 20) {
        scanAt = now;
        count = 0;
        if (ScanUnits(grid.x, grid.z, upm, floorM, &scan))
            for (size_t i = 0; i < scan.n && count < meleeboxes::kMaxMonsters; ++i) {
                const Cand& c = scan.c[i];
                const Know k = Verdicted(c.id, now);
                monsters[count++] = {c.id, c.cls, c.d2, c.body,
                                     k == Know::No ? meleeboxes::Verdict::Refused : k == Know::Yes ? meleeboxes::Verdict::Hostile : meleeboxes::Verdict::Unknown};
            }
    }
    const strikedir::Rule rule = CurrentRule();
    const thrust::Rule thrustRule = CurrentThrust();
    meleeboxes::Frame f;
    f.nowMs = now;
    f.upm = upm;
    f.floorY = floorM;
    f.minSpeed = g_engine[0].GetParams().strokeSpeed;
    for (int i = 0; i < 2; ++i) {
        meleeboxes::BladeIn& b = f.blades[i];
        b.have = sb[i].ok;
        if (!b.have) continue;
        const int side = sb[i].side & 1;
        b.side = sb[i].side;
        b.blade = sb[i].s.blade;
        b.nHeads = sb[i].nHeads;   // [gameplay] melee_head_volume, a flail's balls: drawn with the blade
        for (int k = 0; k < sb[i].nHeads; ++k) b.heads[k] = sb[i].heads[k];
        b.inStroke = g_engine[sb[i].side].InStroke();
        b.speed = g_engine[sb[i].side].LastSpeed();     // the engine's own speed of this pose's step - the value its gate is on
        b.counting = g_engine[sb[i].side].Counting();   // ... and whether that step swept something: a contact would count now
        // What that movement is under the strike rules, against the monster nearest the tip (within kTargetReachM, the game's own checks not refusing it): green only while a contact would count
        const stroke::Blade& bl = sb[i].s.blade;
        const meleeboxes::Monster* nearest = nullptr;
        float best = meleeboxes::kTargetReachM;
        for (size_t k = 0; k < count; ++k) {
            if (monsters[k].verdict == meleeboxes::Verdict::Refused) continue;
            const float d = meleeboxes::Scene::PointSegmentDistance(bl.tip, monsters[k].body.bottom, monsters[k].body.top) - monsters[k].body.radius;
            if (d < best) { best = d; nearest = &monsters[k]; }
        }
        b.look = strikedir::LookOf(rule, strikedir::KindIndex(sb[i].kind), b.counting, bl.tip, g_tipWay[side].Way(), nearest ? &nearest->body : nullptr, g_travel[side].Meters());
        b.tint = DbgTintNow(side, now);   // [debug] thrust_debug: blue / orange for a moment after a thrust
        if (thrustdebugwire::Only()) {   // [debug] thrust_debug_only: no swing colour on the blade - cyan, or the thrust's tint
            b.counting = false;
            b.look = strikedir::Look::Cold;
            continue;
        }
        // [gameplay] melee_thrust: a poke the thrust engine counts as a thrust looks as its contact would be judged (green, or what the strike rules / its distance make of it)
        if (sb[i].thrusts && b.look != strikedir::Look::Hot && g_thrustEngine[side].Counting()) {
            const strikedir::Look tl = thrust::LookOf(thrustRule, rule, strikedir::KindIndex(sb[i].kind), true, g_thrustPath[side], bl.tip, g_tipWay[side].Way(), nearest ? &nearest->body : nullptr);
            if (tl != strikedir::Look::Cold) {
                b.look = tl;
                b.counting = true;
                b.speed = std::max(b.speed, g_thrustEngine[side].LastSpeed());
            }
        }
        b.sweepSpeed = g_sweepSpeed[sb[i].side];
        b.sweepMs = g_sweepAt[sb[i].side];
    }
    f.monsters = monsters;
    f.count = count;
    meleeboxes::Update(f);
    // A touch that never became a stroke: the blade ENTERS a monster's body in a movement the engine does not count - shorter than the stroke's start distance, or slower than the least
    // speed (a blade at rest that a monster walks into does not count as a swing at all). An amber ball where it met the monster, and a line in the log; the monsters the game's own
    // checks refuse are left out. (A touch with a stroke on is told where it is judged, in Tick: too slow where it touched, or one of the strike rules.)
    static uint32_t inside[2][8] = {};
    static size_t insideN[2] = {};
    for (int i = 0; i < 2; ++i) {
        if (!sb[i].ok) { insideN[i] = 0; continue; }
        const int side = sb[i].side & 1;
        const stroke::Blade& bl = sb[i].s.blade;
        const stroke::Vec way = g_tipWay[side].Way();
        uint32_t nowInside[8];
        size_t nn = 0;
        for (size_t k = 0; k < count && nn < 8; ++k) {
            const meleeboxes::Monster& mo = monsters[k];
            float s = 0.0f;
            const float reach = mo.body.radius + bl.radius;
            if (stroke::SegmentDistanceSquared(bl.base, bl.tip, mo.body.bottom, mo.body.top, &s) > reach * reach) continue;
            nowInside[nn++] = mo.id;
            bool was = false;
            for (size_t q = 0; q < insideN[i]; ++q) was = was || inside[i][q] == mo.id;
            if (was || mo.verdict == meleeboxes::Verdict::Refused) continue;
            if (g_engine[side].Counting() || g_engine[side].LastSpeed() < 0.3f || (sb[i].thrusts && g_thrustEngine[side].Counting())) continue;   // (counted - or judged in Tick, a thrust too - or nobody swung into it)
            strikedir::Judgement jd;
            jd.haveWay = true;
            jd.at = bl.base + (bl.tip - bl.base) * s;
            jd.dir = way;
            jd.onAxis = strikedir::NearestOnAxis(mo.body, jd.at);
            jd.haveAxis = true;
            MarkContact(jd, mo.id, mo.body, meleeboxes::kCAmber, false, now);
            if (TellRefusal(4, now, false)) {
                const stroke::Params& p = g_engine[side].GetParams();
                LogF("melee touch: %c hand, the %s entered monster %u without a stroke - not counted: the blade moves %.1f m/s (a stroke needs %.1f m/s and %.0f cm of movement first)", side ? 'L' : 'R',
                     strikedir::KindName(strikedir::KindIndex(sb[i].kind)), mo.id, g_engine[side].LastSpeed(), p.strokeSpeed, p.onsetExcursion * 100.0f);
            }
        }
        for (size_t q = 0; q < nn; ++q) inside[i][q] = nowInside[q];
        insideN[i] = nn;
    }
}
uint32_t KeyOf(uint32_t type, bool two, int side, uint32_t heroGen) {
    uint32_t h = 2166136261u;
    for (const uint32_t v : {type, two ? 1u : 0u, static_cast<uint32_t>(side), heroGen}) { h ^= v; h *= 16777619u; }
    return h ? h : 1u;
}

}  // namespace

void Init(const PluginContext* ctx) {
    g_ctx = ctx;
    meleeboxes::SetLogSink(&Log);   // [debug] melee_boxes writes its numbers through the same log
    meleedirect::Init(ctx);
    meleedirectclient::Init(ctx);
    g_threads = Query<ThreadService>(ctx);
    if (!g_threads || (!g_threads->runOnGameThread && !g_threads->runOnUiThread))
        Log("melee touch: no thread service - the hostility check cannot run, nothing will be pressed");
}

void SetWanted(bool on) {
    if (g_wanted.exchange(on) != on) Log(on ? "melee touch: on" : "melee touch: off");
    hitlatency::Want(hitlatency::kMelee, on);   // the hit observer (vr/haptics.cpp) is put in for the press -> hit log too
}
bool Wanted() { return g_wanted.load(); }
void SetStatusWanted(bool on) { g_statusWanted.store(on); }
bool StatusWanted() { return g_statusWanted.load(); }
void SetStatusHero(uint32_t id) { g_statusHeroId.store(id); }
bool ServerLookup(uintptr_t* game, uintptr_t* lookup) {
    const uintptr_t g = g_serverGame.load();
    const uintptr_t fn = g_origLookup ? reinterpret_cast<uintptr_t>(g_origLookup) : d2rsig::Addr(RVA_LOOKUP);
    if (!g || !fn) return false;
    *game = g;
    *lookup = fn;
    return true;
}
// A game kept over a game's end is freed memory: 2026-10-25 a character switched without a restart, the wrist status asked the old game for the
// new hero, and the game died in its own lookup (D2R.exe+0x48FECF).
void ForgetServerGame(const char* why) {
    if (g_serverGame.exchange(0)) LogF("melee touch: the server's game forgotten - %s; the next game's is learned from its own lookups", why);
}
void SetInWorld(bool world) {
    g_learnGame.store(world);
    if (!world) ForgetServerGame("no world camera (the title, the character screen or a load)");
}
void SetUnchecked(bool on) { g_unchecked.store(on); }
void SetNoAttack(bool on) {
    if (g_noAttack.exchange(on) != on) Log(on ? "melee touch: no attack ([debug] melee_touch_no_attack) - a touch that would attack presses nothing; the weapon's weight, stab and push go on"
                                              : "melee touch: no attack off - a touch presses the game's attack again");
}
void SetClientVerdict(bool on) { g_clientVerdict.store(on); }
void SetPlayerBodies(bool on) {
    if (g_playerBodies.exchange(on) != on)
        Log(on ? "melee touch: player bodies on ([multiplayer] pose_share_bodies: the other players near are bodies as NPCs are - their own skeletons' limbs; the weapon meets them, "
                 "grab_bodies takes them, nothing of them is ever hit)"
               : "melee touch: player bodies off");
}
void SetNpcBodies(bool on) {
    if (g_npcBodies.exchange(on) != on)
        Log(on ? "melee touch: npc bodies on ([weight] npc_bodies: town NPCs and the other non-hostile units near get bodies too - the weapon meets them, never attacks them)"
               : "melee touch: npc bodies off");
}
// [weight] monster_shields: what the monsters hold - their shields and weapons - are bodies of their own kind (vr/monster_rig.h HeldCapsules): the held weapon
// stops on them and glances along them, they push no doll, and a blow the weapon met only them with is no hit (the press below waits for the body: Reached).
bool LastBlockedBlow(BlockedBlow* out) { return LastBlockedBlowLocked(out); }
void SetShieldsWanted(bool on) {
    skel::SetHeldWanted(on);
    if (g_shieldsWanted.exchange(on) != on)
        Log(on ? "melee touch: monster shields on - a monster's shield and its weapons stop the held weapon; a blow they stop is no hit (the game's own block roll is for the blows that land)"
               : "melee touch: monster shields off");
}
void SetHeadVolume(bool on) {
    if (g_headVolume.exchange(on) != on) Log(on ? "melee touch: head volumes on - a headed weapon's head is a volume of its own where its model has it, its shaft as thin as the model's"
                                                : "melee touch: head volumes off - one capsule along the weapon, as before");
}
void SetBodiesWanted(bool on) {
    if (g_bodiesWanted.exchange(on) != on) Log(on ? "melee touch: monster bodies on - the hostile monsters' capsules go to the weight model (the held weapon stops on them)" : "melee touch: monster bodies off");
}
bool StabbingNow(int side) {
    const ULONGLONG at = g_stabbingAt[side & 1].load();
    return at && GetTickCount64() - at < 150 && viewgate::F4();
}
void PutGrabLinks(monsterbodies::Snapshot* s);   // [weight] grab_bodies (below)
bool Bodies(monsterbodies::Snapshot* out) {
    if (!out || !BodiesWanted() || !viewgate::F4()) return false;
    {
        std::lock_guard<std::mutex> lock(g_bodiesMutex);
        *out = g_bodies;
    }
    if (!g_npcBodies.load() && g_grabWanted.load()) {   // [weight] grab_bodies keeps the non-hostile units for the grab alone: not bodies for the weapon
        int k = 0;
        for (int i = 0; i < out->n; ++i)
            if (out->b[i].hostile) out->b[k++] = out->b[i];
        out->n = k;
    }
    const ULONGLONG now = GetTickCount64();
    const bool fresh = out->n > 0 && now >= out->tickMs && now - out->tickMs <= monsterbodies::kFreshMs;
    if (fresh && g_swayWanted.load() && dolllib::Ensure()) {
        std::lock_guard<std::mutex> lock(g_swayMutex);
        StepSwayLocked();
        if (g_doll.nLimbs + g_doll.nMonsters > 0) {
            D2rdBody bodies[monsterbodies::kMax];
            const int n = dollview::ToDoll(*out, bodies);
            if (dolllib::Apply(&g_doll, &g_dollParams, bodies, n) > 0) dollview::FromDoll(bodies, n, out);
        }
    }
    if (fresh) PutGrabLinks(out);   // [weight] grab_bodies: the held limbs where the library holds them
    return fresh;
}
// [weight] grab_bodies: every body of the last scan (the non-hostile ones too), fresh and in F4, as it stands swayed (the doll's state in, as Bodies hands
// it over) - or as the game has it (unswayed: the library's grab works on those and the doll's state). False: none fresh.
bool GrabBodiesNow(monsterbodies::Snapshot* out, bool swayed) {
    if (!g_grabWanted.load() || !viewgate::F4()) return false;
    {
        std::lock_guard<std::mutex> lock(g_bodiesMutex);
        *out = g_bodies;
    }
    const ULONGLONG now = GetTickCount64();
    if (!(out->n > 0 && now >= out->tickMs && now - out->tickMs <= monsterbodies::kFreshMs)) return false;
    if (swayed && g_swayWanted.load() && dolllib::Loaded()) {
        std::lock_guard<std::mutex> lock(g_swayMutex);
        if (g_doll.nLimbs + g_doll.nMonsters > 0) {
            D2rdBody bodies[monsterbodies::kMax];
            const int n = dollview::ToDoll(*out, bodies);
            if (dolllib::Apply(&g_doll, &g_dollParams, bodies, n) > 0) dollview::FromDoll(bodies, n, out);
        }
    }
    if (swayed) PutGrabLinks(out);   // (the held limbs where the library holds them)
    return true;
}
void GrabTick(bool ok, const float heroRender[3], float upm) {
    if (!g_grabWanted.load() || !ok || !viewgate::F4() || !heroRender || !std::isfinite(heroRender[0] + heroRender[1] + heroRender[2])) return;
    const ULONGLONG now = GetTickCount64();
    if (now >= g_touchBodiesAt && now - g_touchBodiesAt < 60) return;   // the touch's tick keeps them (a melee weapon swung)
    upm = std::clamp(upm, 1.0f, 12.0f);
    if (now - g_bodiesScanAt >= 30 || now < g_bodiesScanAt) {   // who is near (the same read-only scan as the touch's), every 30 ms ...
        g_bodiesScanAt = now;
        const stroke::Vec grid = meleetouch::GridPlane({heroRender[0], heroRender[1], heroRender[2]});
        Scan s;
        if (ScanUnits(grid.x, grid.z, upm, heroRender[1] / upm, &s)) PublishBodies(s, now, heroRender, upm);
    } else if (now - g_rigsAt >= 10 || now < g_rigsAt) {         // ... and between, their limbs as the game poses them (every 10 ms)
        RepublishBodies(now, heroRender, upm);
    }
}
void SetGrabWanted(bool on) {
    if (g_grabWanted.exchange(on) != on)
        Log(on ? "melee touch: grab bodies on ([weight] grab_bodies: an empty hand's grip holds a monster's or an NPC's body - the doll follows the hand by its weight, the game's units stay where they are)"
               : "melee touch: grab bodies off");
}
bool GrabBodies(monsterbodies::Snapshot* out, bool swayed) { return out && GrabBodiesNow(out, swayed); }
bool GrabDollOn() { return g_swayWanted.load() && dolllib::Loaded(); }
// [weight] grab_bodies: the held limbs as the library holds them (GrabLinks), put into every snapshot handed round while fresh
std::mutex g_grabLinksMutex;
D2rgbLink g_grabLinks[D2RGB_MAX_SWAYS];
int g_nGrabLinks = 0;
ULONGLONG g_grabLinksAt = 0;
void GrabLinks(const D2rgbLink* links, int n) {
    std::lock_guard<std::mutex> lock(g_grabLinksMutex);
    g_nGrabLinks = links ? std::clamp(n, 0, static_cast<int>(D2RGB_MAX_SWAYS)) : 0;
    for (int i = 0; i < g_nGrabLinks; ++i) g_grabLinks[i] = links[i];
    g_grabLinksAt = GetTickCount64();
}
void PutGrabLinks(monsterbodies::Snapshot* s) {
    std::lock_guard<std::mutex> lock(g_grabLinksMutex);
    if (g_nGrabLinks > 0 && GetTickCount64() - g_grabLinksAt <= 150) grabbodies::PutLinks(s, g_grabLinks, g_nGrabLinks);
}
void GrabDollTuning(float* limbHz, float* bodyHz, float* damping, float* maxLimbM, float* maxBodyM, float* massScale) {
    std::lock_guard<std::mutex> lock(g_swayMutex);
    *limbHz = g_dollParams.limbHz;
    *bodyHz = g_dollParams.bodyHz;
    *damping = g_dollParams.damping;
    *maxLimbM = g_dollParams.maxLimbM;
    *maxBodyM = g_dollParams.maxBodyM;
    *massScale = g_dollParams.massScale;
}
void GrabSways(const D2rgbSway* sways, int n) {
    if (!sways || n <= 0 || !GrabDollOn()) return;
    std::lock_guard<std::mutex> lock(g_swayMutex);
    StepSwayLocked();   // (the doll's clock goes on; the held ones are set below each step)
    D2rdState& s = g_doll;
    s.size = sizeof(D2rdState);
    for (int i = 0; i < n; ++i) {
        const D2rgbSway& w = sways[i];
        D2rdSpring* sp = nullptr;
        if (w.bone < 0) {
            for (int k = 0; k < s.nMonsters && !sp; ++k)
                if (s.monsters[k].id == w.id) sp = &s.monsters[k].s;
            if (!sp && s.nMonsters < D2RD_MAX_MONSTERS) {
                D2rdMonster& m = s.monsters[s.nMonsters++];
                m = D2rdMonster{};
                m.id = w.id;
                sp = &m.s;
            }
        } else {
            for (int k = 0; k < s.nLimbs && !sp; ++k)
                if (s.limbs[k].id == w.id && s.limbs[k].bone == w.bone) sp = &s.limbs[k].s;
            if (!sp && s.nLimbs < D2RD_MAX_LIMBS) {
                D2rdLimb& l = s.limbs[s.nLimbs++];
                l = D2rdLimb{};
                l.id = w.id;
                l.bone = w.bone;
                sp = &l.s;
            }
        }
        if (!sp) continue;
        for (int k = 0; k < 3; ++k) { sp->off[k] = w.off[k]; sp->vel[k] = w.vel[k]; }
    }
}
void SetSwayWanted(bool on) {
    if (g_swayWanted.exchange(on) == on) return;
    {
        std::lock_guard<std::mutex> lock(g_swayMutex);
        g_doll = D2rdState{};
        g_swayAt.QuadPart = 0;
    }
    Log(on ? "melee touch: monster ragdoll (a probe) on - the humanoid monsters' bodies give way to the weapon by their weight and spring back (the doll library; drawn with [debug] melee_boxes)"
           : "melee touch: monster ragdoll off");
    if (on) dolllib::Ensure();   // (says in the log when there is no d2r_vr_weight.dll, or one without the monsters' weight)
}
void SetSwayTuning(float weightPct, float pushPct) {
    std::lock_guard<std::mutex> lock(g_swayMutex);
    g_dollParams.massScale = std::clamp(weightPct, 10.0f, 1000.0f) * 0.01f;
    g_dollParams.pushScale = std::clamp(pushPct, 0.0f, 1000.0f) * 0.01f;
}
void SetSwayView(bool on) { g_swayView.store(on); }
void SetPoseWanted(bool on) {
    if (g_poseWanted.exchange(on) != on) Log(on ? "melee touch: monster ragdoll pose on - a struck humanoid gives way as the game draws it (its bones turned with its doll)" : "melee touch: monster ragdoll pose off");
}
bool MonsterPose(uintptr_t self, dollpose::Pose* out) {
    if (!g_poseWanted.load() || !out) return false;
    bool found = false;
    AcquireSRWLockShared(&g_posesLock);
    for (int i = 0; i < g_nPoses && !found; ++i)
        if (g_poses[i].self == self) {
            *out = g_poses[i];
            found = true;
        }
    ReleaseSRWLockShared(&g_posesLock);
    const ULONGLONG now = GetTickCount64();
    if (!found || now - out->tickMs > kPoseFreshMs) return false;
    if (out->fadeFromMs) {   // let go of: eased back to the animation
        const float w = 1.0f - static_cast<float>(now - out->fadeFromMs) / static_cast<float>(kPoseEaseMs);
        if (!(w > 0.0f)) return false;
        *out = dollpose::Faded(*out, w);
    }
    return true;
}
int MonsterPosesForSkin(uintptr_t skeleton, uintptr_t worldPose, dollpose::Pose* out, int max, bool* exact) {
    if (exact) *exact = false;
    if (!g_poseWanted.load(std::memory_order_relaxed) || !out || max <= 0 || g_nPosesNow.load(std::memory_order_relaxed) == 0) return 0;   // (every skinning asks)
    int n = 0;
    AcquireSRWLockShared(&g_posesLock);
    for (int i = 0; i < g_nPoses; ++i)   // the instance's own pose first
        if (worldPose && (g_poses[i].poseCur == worldPose || g_poses[i].posePrev == worldPose)) {
            out[0] = g_poses[i];
            n = 1;
            if (exact) *exact = true;
            break;
        }
    if (!n)   // ... else every pose of a monster of this skeleton (its model): the caller tells them apart by their matrices
        for (int i = 0; i < g_nPoses && n < max; ++i)
            if (skeleton && g_poses[i].skel == skeleton) out[n++] = g_poses[i];
    ReleaseSRWLockShared(&g_posesLock);
    const ULONGLONG now = GetTickCount64();
    int k = 0;
    for (int i = 0; i < n; ++i) {
        dollpose::Pose p = out[i];
        if (now - p.tickMs > kPoseFreshMs) continue;
        if (p.fadeFromMs) {
            const float w = 1.0f - static_cast<float>(now - p.fadeFromMs) / static_cast<float>(kPoseEaseMs);
            if (!(w > 0.0f)) continue;
            p = dollpose::Faded(p, w);
        }
        out[k++] = p;
    }
    if (!k && exact) *exact = false;
    return k;
}
int PosedTurns(uint32_t id) {
    if (!g_poseWanted.load()) return -1;
    int n = 0;
    const ULONGLONG now = GetTickCount64();
    AcquireSRWLockShared(&g_posesLock);
    for (int i = 0; i < g_nPoses; ++i)
        if (g_poses[i].id == id && now - g_poses[i].tickMs <= kPoseFreshMs) n = g_poses[i].n;
    ReleaseSRWLockShared(&g_posesLock);
    return n;
}
bool SwayView() { return g_swayView.load() && g_swayWanted.load(); }
void PushBody(const monsterbodies::Snapshot& snap, int i, const float impulse[3], const float point[3]) {
    if (!g_swayWanted.load() || i < 0 || i >= snap.n || !snap.b[i].doll || !dolllib::Loaded()) return;   // (humanoids only)
    D2rdBody bodies[monsterbodies::kMax];
    const int n = dollview::ToDoll(snap, bodies);
    std::lock_guard<std::mutex> lock(g_swayMutex);
    if (!dolllib::Push(&g_doll, &g_dollParams, bodies, n, i, impulse, point)) return;
    const ULONGLONG now = GetTickCount64();
    if (g_swayView.load()) {   // the view's arrow: the push as the doll took it (the slider in it)
        dollview::Arrow& r = g_swayArrows[g_swayArrowAt];
        if (now - r.ms > 60) {   // (one a few steps: the weight model steps at 90 Hz and a rest on a body pushes every step)
            g_swayArrowAt = (g_swayArrowAt + 1) % 4;
            dollview::Arrow& w = g_swayArrows[g_swayArrowAt];
            for (int k = 0; k < 3; ++k) { w.at[k] = point[k]; w.imp[k] = impulse[k] * g_dollParams.pushScale; }
            w.ms = now;
        }
    }
    if (now - g_swayToldAt >= 1000) {   // (once a second at most: a line costs a millisecond)
        g_swayToldAt = now;
        const float j = std::sqrt(impulse[0] * impulse[0] + impulse[1] * impulse[1] + impulse[2] * impulse[2]);
        const monsterbodies::Body& b = snap.b[i];
        LogF("melee touch: monster ragdoll - monster %u (%.0f kg) pushed on limb %d with %.2f N s; balance %.0f %%, %d limbs and %d monsters swaying", b.id,
             dolllib::Mass(&g_dollParams, bodies, n, b.id), b.rig ? b.bone : -1, j, dolllib::Balance(&g_doll, &g_dollParams, b.id, nullptr) * 100.0f, g_doll.nLimbs, g_doll.nMonsters);
    }
}
int DrawBodies(uint64_t nowMs, const float eyeWorld[3], uint32_t touchedId, int touchedBone, float (*a)[4], float (*b)[4], int max) {
    if (!meleeboxes::Wanted() || max <= 0) return 0;
    monsterbodies::Snapshot snap;
    if (!Bodies(&snap) || nowMs < snap.tickMs) return 0;
    int k = monsterbodies::Pack(snap, eyeWorld, touchedId, touchedBone, a, b, max);
    if (SwayView() && k < max) {   // [debug] monster_ragdoll_view after the bodies
        D2rdBody bodies[monsterbodies::kMax];
        const int n = dollview::ToDoll(snap, bodies);
        std::lock_guard<std::mutex> lock(g_swayMutex);
        k += dollview::PackDebug(snap, g_swayArrows, 4, nowMs, eyeWorld, [&](uint32_t id) { return dolllib::Mass(&g_dollParams, bodies, n, id); },
                                 [&](uint32_t id, float off[3]) { return dolllib::Balance(&g_doll, &g_dollParams, id, off); }, a + k, b + k, max - k);
    }
    return k;
}
void SetAnimNote(AnimNoteFn fn) { g_animNote.store(fn); }
void SetPressTrace(PressNoteFn press, PressOutcomeFn outcome) {
    g_pressOutcome.store(outcome);
    g_pressNote.store(press);
}
void SetThrownTouch(bool on) {
    if (g_thrownTouch.exchange(on) != on && g_wanted.load()) Log(on ? "melee touch: thrown weapons (javelin, throwing knife and axe) hit by touch too" : "melee touch: thrown weapons are not swung");
}
bool ThrownTouch() { return g_thrownTouch.load(); }
void SetThrownHeld(bool on) {
    if (g_thrownHeld.exchange(on) != on && g_wanted.load())
        Log(on ? "melee touch: a thrown weapon is in the hand from the equip, like a sword (melee_touch_thrown_held) - an open grip does not stop its touch"
               : "melee touch: with the grip throw on, a thrown weapon touches only while its grip is squeezed (melee_touch_thrown_held off)");
}
void SetThrowGripAt(GripAtFn gripIsThrowsAt, GripAtFn gripHeldAt) {
    g_gripIsThrowsAt.store(gripIsThrowsAt);
    g_gripHeldAt.store(gripHeldAt);
}

void SetThrowLink(GripIsThrowsFn gripIsThrows, GripHeldFn gripHeld, LastThrowFn lastThrowTickMs, HoldOffFn holdOff) {
    g_gripIsThrows.store(gripIsThrows);
    g_gripHeld.store(gripHeld);
    g_lastThrow.store(lastThrowTickMs);
    g_holdOff.store(holdOff);
}
void SetStrokeTuning(float minSpeed, float startCm) {
    const stroke::StrokeTuning t = stroke::Clamp({minSpeed, startCm});
    g_minSpeed.store(t.minSpeed);
    g_startCm.store(t.startCm);
}
void SetStrikeForce(const strikeforce::Settings& in) {
    const strikeforce::Settings s = strikeforce::Clamp(in);
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(g_ruleMutex);
        changed = g_force.on != s.on || g_force.general != s.general || g_force.blunt != s.blunt || g_force.edged != s.edged || g_force.piercing != s.piercing ||
                  g_force.massExp != s.massExp || g_force.cutWeight != s.cutWeight || g_force.floorNs != s.floorNs || g_force.shared != s.shared || g_force.stuck != s.stuck;
        g_force = s;
    }
    if (!changed) return;
    if (!s.on) { Log("melee touch: strike by force off - the blade's speed judges a touch, as before"); return; }
    LogF("melee touch: strike by force on - a weapon the weight model holds counts by what its strike puts into the body: the impulse in the first 150 ms%s, at least %.2f x %.2f "
         "(blunt) / %.2f (edged) / %.2f (piercing) x steel kg ^ %.2f, never under %.2f N s (a 1 kg weapon: %.2f / %.2f / %.2f N s); %s; %s", s.cutWeight > 0.0f ? " + the cut x the cut weight (edged)" : "",
         s.general, s.blunt, s.edged, s.piercing, s.massExp, s.floorNs, strikeforce::Threshold(s, strikeforce::Family::Blunt, 1.0f), strikeforce::Threshold(s, strikeforce::Family::Edged, 1.0f),
         strikeforce::Threshold(s, strikeforce::Family::Piercing, 1.0f), s.shared ? "a blow shared between monsters counts on each with a fair share" : "each monster's strike alone",
         s.stuck == 1 ? "stuck in and pushed: the hit repeats at the game's attack timing" : "one hit a strike");
}
void SetStrikeForceDebug(bool on) { g_forceDebug.store(on); }
void SetStrikeRule(const strikedir::Rule& rule) {
    const strikedir::Rule r = strikedir::Clamp(rule);
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(g_ruleMutex);
        changed = g_strikeRule.toward != r.toward || g_strikeRule.towardKinds != r.towardKinds || g_strikeRule.towardDeg != r.towardDeg || g_strikeRule.head != r.head ||
                  g_strikeRule.headKinds != r.headKinds || g_strikeRule.headPct != r.headPct || g_strikeRule.amplitude != r.amplitude || g_strikeRule.pierceCm != r.pierceCm ||
                  g_strikeRule.edgedX != r.edgedX || g_strikeRule.bluntX != r.bluntX || g_strikeRule.staffX != r.staffX || g_strikeRule.kindX != r.kindX || g_strikeRule.backward != r.backward;
        g_strikeRule = r;
    }
    if (!changed || !g_wanted.load()) return;
    char kinds[200] = "";
    for (int k = 0, w = 0; k < strikedir::kKinds && w < 170; ++k)
        w += snprintf(kinds + w, sizeof kinds - static_cast<size_t>(w), "%s%s %.0f", w ? ", " : "", strikedir::KindName(k), strikedir::AmplitudeCm(r, k));
    LogF("melee touch: the strike rules - toward the monster %s (within %.0f deg of the direction to its axis), the head only %s (the outer %.0f%% of the blade), the swing's amplitude %s, "
         "point-to-handle refused %s (weapons that thrust); least cm: %s",
         r.toward ? "on" : "off", r.towardDeg, r.head ? "on" : "off", r.headPct, r.amplitude ? "on" : "off", r.backward ? "on" : "off", r.amplitude ? kinds : "-");
}
void SetKindTuning(int kind, float minSpeed, float startCm) {
    if (kind < 0 || kind >= strikedir::kKinds) return;
    const float s = std::isfinite(minSpeed) && minSpeed >= stroke::kMinSpeedFloor ? std::min(minSpeed, stroke::kMinSpeedCeil) : 0.0f, c = std::isfinite(startCm) && startCm >= 2.0f ? std::min(startCm, 30.0f) : 0.0f;
    g_kindSpeed[kind].store(s);
    g_kindStart[kind].store(c);
}
void SetThrustRule(const thrust::Rule& rule) {
    const thrust::Rule r = thrust::Clamp(rule);
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(g_ruleMutex);
        changed = g_thrustRule.on != r.on || g_thrustRule.minSpeed != r.minSpeed || g_thrustRule.cm != r.cm || g_thrustRule.alongPct != r.alongPct || g_thrustRule.mode != r.mode ||
                  g_thrustRule.kindSpeed != r.kindSpeed || g_thrustRule.kindCm != r.kindCm || g_thrustRule.minAccel != r.minAccel || g_thrustRule.kindAccel != r.kindAccel ||
                  g_thrustRule.withRun != r.withRun;
        g_thrustRule = r;
    }
    if (!changed || !g_wanted.load()) return;
    // which kinds thrust: ticked (melee_thrust_on_<kind>), and those left to the game's class of the item (an ini with neither the tick nor the older number)
    char ticked[200] = "", byGame[200] = "";
    for (int k = 0, wt = 0, wg = 0; k < strikedir::kKinds; ++k) {
        const int m = r.mode[static_cast<size_t>(k)];
        if (m == thrust::kAlways && wt < 180) wt += snprintf(ticked + wt, sizeof ticked - static_cast<size_t>(wt), "%s%s", wt ? ", " : "", strikedir::KindName(k));
        if (m == thrust::kByGame && wg < 180) wg += snprintf(byGame + wg, sizeof byGame - static_cast<size_t>(wg), "%s%s", wg ? ", " : "", strikedir::KindName(k));
    }
    LogF("melee touch: thrusts %s - a poke along the blade (%.0f%% of the tip's path, point first) counts from %.2f m/s after %.0f cm, the peak acceleration %s%.0f m/s^2; the kinds that "
         "thrust: %s%s%s%s",
         r.on ? "on" : "off", r.alongPct, r.minSpeed, r.cm, r.minAccel > 0.0f ? "at least " : "not asked - ", r.minAccel, ticked[0] ? ticked : "none ticked",
         byGame[0] ? "; by the game's class of the item (1ht, 2ht - no tick in the ini): " : "", byGame,
         r.withRun ? "; running counts (measured in the world: the hero's run is the thrust's movement too)" : "");
}
void SetBladeTuning(float lengthPct, float radiusCm, bool flip, float buttPct) {
    const bladeshape::Tuning t = bladeshape::Clamp({lengthPct, radiusCm, flip, buttPct});
    g_tuneLength.store(t.lengthPct);
    g_tuneRadius.store(t.radiusCm);
    g_tuneFlip.store(t.flip);
    g_tuneButt.store(t.buttPct);
}
void SetThrownTuning(float lengthPct, float buttPct) {
    const bladeshape::ThrownTuning t = bladeshape::Clamp(bladeshape::ThrownTuning{lengthPct, buttPct});
    g_thrownLength.store(t.lengthPct);
    g_thrownButt.store(t.buttPct);
}
BladeExtent BladeFor(uint32_t code, uint32_t type, bool twoHandsOn, float unitM) {
    // A thrown weapon has a blade of its own only while the touch swings it (hit by touch and melee_touch_thrown on): [weight]'s shaft follows it
    // then. Otherwise it keeps the shaft it always had (the kind-less default, a sword's).
    if (ThrownKind(type) && !(g_wanted.load() && g_thrownTouch.load())) { type = D2RVR_TYPE_SWORD; unitM = 0.0f; }   // (the sword's constants: no model of a javelin on that shaft)
    const bladeshape::Shape s = TunedShape(code, type, twoHandsOn, unitM);
    return {s.length, s.butt, s.tipSign};
}

void NoteHands(const D2RVR_Shared* sh, uint32_t epoch) {
    if (!g_wanted.load() || !sh || sh->sampleStampMagic != D2RVR_SAMPLE_STAMP_MAGIC || sh->roomMagic != D2RVR_ROOM_MAGIC || !sh->handsValid) return;
    if (sh->sampleStamp == g_lastStamp) return;   // one sample per headset frame: the second eye's pose is the same stamp
    g_lastStamp = sh->sampleStamp;
    const double t = Seconds(sh->sampleStamp);
    std::lock_guard<std::mutex> lock(g_histMutex);
    {
        RoomFrameAt& fr = g_frames[g_frameNext++ % std::size(g_frames)];
        fr.stamp = sh->sampleStamp;
        fr.set = true;
        for (int i = 0; i < 3; ++i) fr.frame.head[i] = sh->headRoom[i];
        fr.frame.yawRad = sh->headYawRoomDeg * 0.0174532925f;
    }
    for (int side = 0; side < 2; ++side) {
        if (!(sh->handsValid & (1u << side))) continue;
        stroke::Sample s;
        s.time = t;
        s.active = true;
        s.epoch = epoch;
        s.hand = side == 0 ? RoomPose(*sh, sh->rightHand, sh->rightRot) : RoomPose(*sh, sh->leftHand, sh->leftRot);
        g_hist[side].Push(sh->sampleStamp, s);
        g_handPath[side].Push(t, s.hand.p);
    }
}

void Tick(const Frame& f) {
    const ULONGLONG now = GetTickCount64();
    ReleasePress(now, false);
    LatencyPoll(now);
    doubleswing::Tick();   // [hands] double_swing_by_hands: the hero's mana is read for its decisions (cheap when off)
    meleearm::Tick();      // [gameplay] melee_skill_armed: the same, while a skill is armed
    if (g_wanted.load()) meleedirect::Install();   // only while hit by touch and [gameplay] melee_direct are both on
    if (g_wanted.load()) {   // [gameplay] melee_direct_client: the proofs, the role, the look at the skills; a command handed back is pressed on the key
        uint32_t heroMode = 0;   // the hero's own client unit's mode: his attack time is measured from it
        const uintptr_t heroUnit = meleedirectclient::Wanted() || meleedirect::ByRules() ? ClientUnit(g_heroId.load(), 0) : 0;
        const bool modeKnown = heroUnit && Read(&heroMode, heroUnit + 0xC, 4);
        meleedirectclient::Tick(now, g_serverGame.load() != 0, viewgate::F4(), modeKnown, heroMode);
        uint32_t backId = 0;
        int backHand = 0;
        if (meleedirectclient::TakeFallback(&backId, &backHand) && viewgate::F4()) PressKey(backId, true, backHand, now);
        // [gameplay] melee_direct_client_cleave, the host: a joined player's extra hit - the server's attack for his hero (vr/melee_direct.cpp)
        for (uint32_t rh = 0, rm = 0; meleedirectclient::TakeRemote(&rh, &rm);) {
            const uintptr_t game = g_serverGame.load();
            if (!game || rh == g_heroId.load()) continue;   // not hosting (no server here), or our own hero named: nothing
            meleedirect::Request req;
            req.game = game;
            req.heroId = rh;
            req.monsterId = rm;
            req.lookup = g_origLookup ? reinterpret_cast<uintptr_t>(g_origLookup) : d2rsig::Addr(RVA_LOOKUP);
            req.remote = true;
            meleedirect::Submit(req, now);
        }
    }
    // F4 only (vr/view_gate.h). f.ok is vrcam's - a game area, first person on the controllers, no panel - and the view test is
    // here too, so the module does not depend on the caller's being right: another view = not ok, and what a touch had under way
    // ends at once (the press up, the window the game's lookups are answered in closed, a Direct hit request still queued dropped).
    const bool view = viewgate::F4();
    if (!view) {
        g_press.CloseWindow();
        if (g_wanted.load()) meleedirect::Flush();
        if (g_wanted.load()) meleedirectclient::Flush();
    }
    // A javelin, throwing knife or axe is swung too ([gameplay] melee_touch_thrown), a throwing potion (the same kind) never: its item code is checked below
    const bool thrown = meleetouch::ThrownKind(f.weaponType);
    // Why the touch does not start, said once per weapon and reason (vr/touch_why.h): "the spear does not work" has to leave a line in the log
    static meleetouch::WhyTold whyTold;
    const uint32_t weaponKey = meleetouch::WeaponKey(f.weaponType, f.code[0], f.code[1]);
    auto skip = [&](meleetouch::Why why) {
        if (!g_wanted.load() || !whyTold.First(weaponKey, why)) return;
        const uint32_t code = f.kind[0] == f.weaponType ? f.code[0] : f.code[1];
        const auto ch = [&](int i) { const char c = static_cast<char>((code >> (8 * i)) & 255); return c ? c : ' '; };
        LogF("melee touch: not started for '%c%c%c%c' (kind %u) - %s", code ? ch(0) : '?', ch(1), ch(2), ch(3), f.weaponType, meleetouch::WhyText(why));
    };
    if (!g_wanted.load() || !f.ok || !view || !(meleetouch::MeleeKind(f.weaponType) || (thrown && g_thrownTouch.load()))) {
        if (g_wanted.load())
            skip(!view ? meleetouch::Why::NotF4 : !f.ok ? meleetouch::Why::NotOk : thrown ? meleetouch::Why::ThrownOff : meleetouch::Why::KindNotSwung);
        if (g_press.Down()) ReleasePress(now, true);
        Cancel();
        return;
    }
#if D2RVR_FIRST_PERSON
    skel::PosedWeaponPose pose;
    if (!skel::PosedWeapon(pose) || pose.worldEpoch != f.epoch) { skip(meleetouch::Why::NoPose); Cancel(); return; }
    if (thrown && !bladeshape::TouchKind(f.weaponType, f.code[pose.side & 1], true)) { skip(meleetouch::Why::Potion); Cancel(); return; }   // a throwing potion (or an item not read yet)
#else
    Cancel();   // the public build has no first-person body: no weapon to swing
    return;
#endif
#if D2RVR_FIRST_PERSON
    // [weight]: the hand that carries the weapon drives the stroke; a weapon no hand holds (falling, lying, flying back) strikes nothing.
    const Drive drive = DrivingHand(f.weightPlaced, f.weightCarrying, pose.side);
    // A weapon in each hand: the one let go strikes nothing, the other strikes on (the user, 2026-10-24: «если я грипом выбросил первое оружие то со второго пропадает
    // хитбокс») - the pose goes on with only the blade still in a hand
    const bool dualHeld = bladeshape::DualWield(f.kind[0], f.kind[1]) && (drive.strikes || !f.secondLetGo);
    if (!drive.strikes && !dualHeld) { skip(meleetouch::Why::LetGo); Cancel(); return; }
    if (thrown) {   // the touch and the throw by gesture must not both fire for one swing (vr/thrown_touch.h)
        const GripIsThrowsFn owns = g_gripIsThrows.load();
        const GripHeldFn held = g_gripHeld.load();
        const LastThrowFn last = g_lastThrow.load();
        meleetouch::ThrownState st;
        const GripAtFn ownsAt = g_gripIsThrowsAt.load(), heldAt = g_gripHeldAt.load();
        if (ownsAt && heldAt) {   // the hand that swings it: with a thrown weapon in each hand each grip is its own weapon's
            st.gripOwns = ownsAt(drive.hand);
            st.gripHeld = heldAt(drive.hand);
        } else {
            st.gripOwns = owns && owns();
            st.gripHeld = held ? held() : true;   // the gesture's own reading of the squeeze: the shared block's grip is 0 while the gesture owns it
        }
        st.nowMs = now;
        st.heldAtOnce = g_thrownHeld.load();
        st.lastThrowMs = last ? last() : 0;
        const meleetouch::ThrownWhy why = meleetouch::ThrownWhyNot(st);
        if (why != meleetouch::ThrownWhy::None) {   // (a stroke started in the follow-through, or with the hand open, is dropped: it starts afresh)
            skip(why == meleetouch::ThrownWhy::AfterThrow ? meleetouch::Why::AfterThrow : meleetouch::Why::GripOpen);
            Cancel();
            return;
        }
    }
    g_liveKind.store(f.weaponType);     // [gameplay] melee_touch_only: a weapon the touch swings (melee, bare hands, a throwing weapon with its touch on) is in a hand, posed and striking -
    g_liveAt.store(now);                // the trigger need not attack (vr/melee_trigger_wire.cpp asks Live; a bow or a crossbow is never here)
    if (pose.poseStamp == g_lastPoseStamp) return;   // nothing new since the last time
    g_lastPoseStamp = pose.poseStamp;
    const float upm = std::clamp(f.upm, 1.0f, 12.0f);
    // The blades of this pose: the weapon bone AS DRAWN (vr/blade_shape.h - the red line's frame, not the hero model's matrix), [0] of the
    // hand slot PosedWeapon names, [1] the other hand's when a melee weapon is held in each (a Barbarian's two blades, an Assassin's two
    // claws). The length and thickness are the weapon kind's (bladeshape::ForKind), tuned live by the user's sliders.
    SideBlade blades[2];
    bool used[2] = {false, false};
    {
        bladeshape::HeadFrame head;
        head.ok = f.headOk;
        for (int i = 0; i < 3; ++i) { head.right[i] = f.camR[i]; head.fwd[i] = f.camF[i]; head.eyeWorld[i] = f.eyeWorld[i]; }
        head.upm = upm;
        const bool dual = bladeshape::DualWield(f.kind[0], f.kind[1]);
        int nb = 0;
        for (int k = 0; k < (dual ? 2 : 1); ++k) {
            const int side = (pose.side & 1) ^ k;            // the wrist slot the bone is on
            const int hand = k == 0 ? drive.hand : f.secondHand >= 0 ? f.secondHand : side;     // the controller that swings it ([weight]: the hand that carries it)
            const skel::PosedBlade& pb = pose.blade[side];
            if (!pb.ok) continue;
            if ((k == 0 && !drive.strikes) || (k == 1 && f.secondLetGo)) continue;   // (let go: falling, lying, flying back - it strikes nothing)
            const uint32_t kind = k == 0 ? f.weaponType : f.kind[side];
            const bool two = k == 0 && f.twoHandsOn;
            // [flail] chain: a flail's ball on its chain strikes, not its handle - a ball as thick as the model's, where the chain has it
            const bool ball = pb.ball && head.ok;
            const bladeshape::Shape shape = ball ? bladeshape::BallShape(pb.ballRadius) : TunedShape(f.code[side], kind, two, pb.unitM);   // (the model's own ends or the kind's, the general sliders, a thrown kind's own on top)
            SideBlade& sb = blades[nb];
            bladeshape::RoomFrame roomFrame;
            bool haveRoomFrame = false;
            {
                std::lock_guard<std::mutex> lock(g_histMutex);
                if (!g_hist[hand].Find(pose.poseStamp, Seconds(D2RVRStampNow()), sb.s)) { ++g_n.noHand; g_engine[hand].Reset(); ResetThrust(hand); continue; }
                for (const RoomFrameAt& fr : g_frames)
                    if (fr.set && fr.stamp == pose.poseStamp) { roomFrame = fr.frame; haveRoomFrame = true; break; }
            }
            if (head.ok) {
                bladeshape::Bone bone;
                float line[3];   // the axis the blade lies along: the bone's Y, or for the claws and katars its Z with the sign the game's own pose gives it
                bladeshape::LineOf(shape, pb.axis[1], pb.axis[2], pb.zSign, line);
                for (int i = 0; i < 3; ++i) { bone.origin[i] = ball ? pb.ballCentre[i] : pb.origin[i]; bone.axisY[i] = ball ? pb.ballDir[i] : line[i]; }
                sb.s.blade = bladeshape::FromBone(head, bone, shape);
                // [gameplay] melee_head_volume: the head's own volume, where the model has it on the posed bone (its axes, the model's units in metres)
                sb.nHeads = 0;
                // [flail] chain: Khalim's Flail and Will - three chains, three balls, each strikes (the user, 2026-10-24: «все три должны быть ударными и они не должны
                // сливаться в один»): the balls beside the one this blade is, each a sphere of its own where the chain has it
                for (int m = 0; ball && m < pb.moreBalls && sb.nHeads < meleeboxes::kHeads; ++m) {
                    float a[3], b[3];
                    for (int i = 0; i < 3; ++i) { a[i] = pb.moreCentre[m][i] - pb.moreDir[m][i] * bladeshape::kBallHalf; b[i] = pb.moreCentre[m][i] + pb.moreDir[m][i] * bladeshape::kBallHalf; }
                    sb.heads[sb.nHeads++] = {bladeshape::PointToRender(head, a), bladeshape::PointToRender(head, b), pb.ballRadius};
                }
                metalhead::Volume vol;
                if (!ball && g_headVolume.load() && pb.unitM > 0.0f && shape.axis != 2 && metalhead::VolumeOf(f.code[side], strikedir::KindIndex(kind), &vol)) {
                    float ha[3], hb[3];
                    if (metalhead::ModelPoint(pb.origin, pb.axis, pb.unitM, vol.a, ha) && metalhead::ModelPoint(pb.origin, pb.axis, pb.unitM, vol.b, hb)) {
                        sb.heads[0] = {bladeshape::PointToRender(head, ha), bladeshape::PointToRender(head, hb), vol.r * pb.unitM};
                        sb.nHeads = 1;
                    }
                }
                // the same blade in the room, for the stroke's speed and onset: the BLADE's motion as drawn, not the controller's (stroke::Sample::room)
                if (haveRoomFrame) { sb.s.room = bladeshape::RoomFromBone(roomFrame, bone, shape); sb.s.haveRoom = true; }
                sb.s.anchor = {head.eyeWorld[0] / upm, head.eyeWorld[1] / upm, head.eyeWorld[2] / upm};
            } else if (k == 0) {   // no view yet: the hero model's matrix gives the weapon in hand (the first version's way)
                const stroke::Vec base{pose.origin[0] / upm, pose.origin[1] / upm, pose.origin[2] / upm};
                const stroke::Vec axis{pose.axis[0], pose.axis[1], pose.axis[2]};
                sb.s.blade = {base, base + axis * (shape.length * static_cast<float>(shape.tipSign)), shape.radius};
                sb.s.anchor = {pose.hero[0] / upm, pose.hero[1] / upm, pose.hero[2] / upm};
            } else {
                continue;
            }
            sb.s.epoch = (static_cast<uint64_t>(pose.heroGeneration) << 32) | pose.worldEpoch;
            sb.s.context = KeyOf(kind, two, hand, pose.heroGeneration);
            sb.s.scale = upm;
            sb.s.lever = shape.length;   // the tip side: a butt behind the grip does not make the stroke's turn count for more
            if (ball) {   // the ball swings as far from the hand as the handle and the chain hold it
                const float d[3] = {pb.ballCentre[0] - pb.origin[0], pb.ballCentre[1] - pb.origin[1], pb.ballCentre[2] - pb.origin[2]};
                sb.s.lever = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) + pb.ballRadius;
            }
            sb.ball = ball;
            sb.ok = true;
            sb.side = hand;
            sb.kind = kind;
            sb.code = f.code[side];
            sb.shape = shape;
            sb.unitM = ball ? 0.0f : pb.unitM;
            sb.zSign = pb.zSign;
            sb.thrustClass = f.thrustClass[side];
            sb.slot = side;
            sb.headFrom = metalhead::Source::None;
            sb.headAlong = ball ? -1.0f : metalhead::Along(sb.code, strikedir::KindIndex(kind), shape.butt, shape.length, sb.unitM, &sb.headFrom);
            used[hand] = true;
            ++nb;
        }
        for (int side = 0; side < 2; ++side) if (!used[side]) { g_engine[side].Reset(); ResetThrust(side); }   // a hand with no blade this pose starts afresh
        if (!nb) { skip(meleetouch::Why::NoBlade); return; }
        if (head.ok) {   // [multiplayer] pose_share_bodies: the blades as drawn, for the other players
            std::lock_guard<std::mutex> lock(g_ownBladesMutex);
            g_ownBladesN = 0;
            for (const SideBlade& sb : blades) {
                if (!sb.ok || g_ownBladesN >= 2) continue;
                meleetouch::OwnBlade& o = g_ownBlades[g_ownBladesN++];
                const stroke::Blade& bl = sb.s.blade;
                o.butt[0] = bl.base.x; o.butt[1] = bl.base.y; o.butt[2] = bl.base.z;
                o.tip[0] = bl.tip.x; o.tip[1] = bl.tip.y; o.tip[2] = bl.tip.z;
                o.radius = bl.radius;
                o.hand = sb.side & 1;
                o.kind = sb.kind;
            }
            g_ownBladesAt = now;
        }
        {   // what blades there are, once per change (for the log of the first test: which hand, which kind, which way the tip lies, how long)
            static uint32_t toldKey = 0;
            uint32_t key = head.ok ? 2166136261u : 1u;
            for (const SideBlade& sb : blades)
                if (sb.ok) for (const uint32_t v : {static_cast<uint32_t>(sb.side), sb.kind, sb.code, static_cast<uint32_t>(sb.shape.tipSign + 2), static_cast<uint32_t>(sb.ball),
                                                     // (coarse: the pose's metres per unit wobble in their third digit, the lengths and thicknesses with them - a line every pose; a change of 2 % or 5 cm is told)
                                                     static_cast<uint32_t>(sb.shape.length * 20.0f + 0.5f), static_cast<uint32_t>(sb.shape.butt * 20.0f + 0.5f),
                                                     static_cast<uint32_t>(sb.shape.radius * 200.0f + 0.5f), static_cast<uint32_t>(std::log(std::max(sb.unitM, 1e-3f)) * 50.0f + 1000.0f),
                                                     static_cast<uint32_t>(sb.shape.axis * 4 + sb.zSign + 1), static_cast<uint32_t>((sb.headAlong + 1.0f) * 1000.0f), static_cast<uint32_t>(sb.nHeads ? sb.heads[0].radius * 100.0f + 1.5f + 1000.0f * sb.nHeads : 0.0f)}) { key ^= v; key *= 16777619u; }
            if (key != toldKey) {
                toldKey = key;
                char b[1300];
                int w = snprintf(b, sizeof b, "melee touch: blades (%s) -", head.ok ? "weapon bone as drawn" : "NO VIEW YET - the hero model's matrix");
                for (const SideBlade& sb : blades)
                    if (sb.ok && w > 0 && static_cast<size_t>(w) < sizeof b) {
                        char src[360] = "";   // where the two ends come from: the weapon model's own bounds (and the metres of its unit), or the kind's constants
                        if (!sb.ball) {
                            weaponextent::Reach rr;
                            if (sb.unitM > 0.0f && weaponextent::ReachOf(sb.code, &rr)) snprintf(src, sizeof src, " (the model's own: tip %.2f + butt %.2f units of %.3f m)", rr.tip, rr.butt, sb.unitM);
                            else snprintf(src, sizeof src, " (the kind's constants%s)", sb.unitM > 0.0f ? ": no model row for the item" : ": no unit measured");
                            if (sb.nHeads && !sb.ball) {   // [gameplay] melee_head_volume: the head's own volume, how far out from the shaft and how thick
                                const size_t at = strlen(src);
                                const float d = std::sqrt(stroke::SegmentDistanceSquared(sb.heads[0].base, sb.heads[0].tip, sb.s.blade.base, sb.s.blade.tip));
                                snprintf(src + at, sizeof src - at, " [head volume %.0f cm long, r %.1f cm, %.0f cm off the shaft's line]", stroke::Length(sb.heads[0].tip - sb.heads[0].base) * 100.0f,
                                         sb.heads[0].radius * 100.0f, d * 100.0f);
                            }
                            if (sb.headFrom != metalhead::Source::None) {   // [gameplay] melee_metal_head: where its head starts, and from what
                                const size_t at = strlen(src);
                                if (sb.headAlong > 0.0f) snprintf(src + at, sizeof src - at, " [head from %.0f%% of its length: %s]", sb.headAlong * 100.0f, metalhead::SourceText(sb.headFrom));
                                else snprintf(src + at, sizeof src - at, " [head: %s]", metalhead::SourceText(sb.headFrom));
                            }
                            if (sb.shape.axis == 2) {   // a fist weapon's blade: along the bone's Z, which way the game's pose has it against the forearm
                                const size_t at = strlen(src);
                                snprintf(src + at, sizeof src - at, " [Z %s the forearm in the game's pose]", sb.zSign > 0 ? "along" : sb.zSign < 0 ? "against" : "not told against");
                            }
                        }
                        w += snprintf(b + w, sizeof b - static_cast<size_t>(w), " %c hand item '%c%c%c%c' kind %u: tip %.2f m and butt %.2f m from the bone, r %.1f cm, tip along the bone's %c%c%s%s;",
                                      sb.side ? 'L' : 'R', sb.code ? static_cast<char>(sb.code & 255) : '?', static_cast<char>((sb.code >> 8) & 255) ? static_cast<char>((sb.code >> 8) & 255) : ' ',
                                      static_cast<char>((sb.code >> 16) & 255) ? static_cast<char>((sb.code >> 16) & 255) : ' ', static_cast<char>((sb.code >> 24) & 255) ? static_cast<char>((sb.code >> 24) & 255) : ' ',
                                      sb.kind, sb.shape.length, sb.shape.butt, sb.shape.radius * 100.0f, sb.shape.tipSign < 0 ? '-' : '+', sb.shape.axis == 2 ? 'Z' : 'Y', src,
                                      sb.ball ? (sb.nHeads ? " - the flail's balls on their chains (each its own sphere, each strikes), not the handle" : " - the flail's ball on its chain (the 'bone' is the ball's centre), not the handle") : "");
                    }
                Log(b);
            }
        }
    }

    // [weight] knock_props (vr/knock_props.h): these blades as drawn are what knocks the level's props about (only drawn; nothing of the touch's own changes)
    if (knockprops::Wanted()) {
        bool noted[2] = {false, false};
        for (const SideBlade& sb : blades) {
            if (!sb.ok) continue;
            const int hand = sb.side & 1;
            const float a[3] = {sb.s.blade.base.x, sb.s.blade.base.y, sb.s.blade.base.z}, b[3] = {sb.s.blade.tip.x, sb.s.blade.tip.y, sb.s.blade.tip.z};
            knockprops::NoteBlade(hand, true, sb.kind == D2RVR_TYPE_UNARMED ? 1 : 0, a, b, sb.s.blade.radius);
            noted[hand] = true;
        }
        for (int side = 0; side < 2; ++side) if (!noted[side]) knockprops::NoteBlade(side, false, 0, nullptr, nullptr, 0.0f);
    }
    {   // [weight] parry_*: the hands' blades against the monsters' weapons in their attacks (vr/parry_wire.h) - the pulse and the knock of a clash; and the weapons a blow on counts
        const parrywire::Settings ps = parrywire::Get();
        if (ps.feel || ps.blockShow || ps.clawBlock) {
            static monsterbodies::Snapshot snap;   // (the timer's thread only: too big for its stack every pose)
            if (ps.shields) {   // (without the monsters' weapons only the hands' kinds go: the block shown)
                std::lock_guard<std::mutex> lock(g_bodiesMutex);
                snap = g_bodies;
            } else {
                snap.n = 0;
            }
            parrywire::Blade pb[2];
            int np = 0;
            for (const SideBlade& sb : blades)
                if (sb.ok && np < 2) {
                    parrywire::Blade& b = pb[np++];
                    b.c = {sb.s.blade.base, sb.s.blade.tip, sb.s.blade.radius};
                    b.hand = sb.side;
                    b.weighted = &sb == &blades[0] && f.weightPlaced;
                    b.kind = sb.kind;
                }
            parrywire::Blades(pb, np, snap, now);
        }
        GatherAttackWeapons();
    }

    // Ahead of any touch: who is near, and what the server says of each (hostile? a wall between?).
    const float heroY = pose.hero[1] / upm;
    const stroke::Vec grid = meleetouch::GridPlane({pose.hero[0], pose.hero[1], pose.hero[2]});   // game units
    {   // the user's two numbers of the engine ([gameplay] melee_min_speed, melee_stroke_start_cm - and, for the kind in a hand, melee_min_speed_<kind>, melee_stroke_start_cm_<kind>),
        // live: each hand's engine has the numbers of the weapon in that hand, applied when they change
        static float appliedSpeed[2] = {-1.0f, -1.0f}, appliedStart[2] = {-1.0f, -1.0f};
        const stroke::StrokeTuning general{g_minSpeed.load(), g_startCm.load()};
        for (const SideBlade& sb : blades) {
            if (!sb.ok) continue;
            const int ki = strikedir::KindIndex(sb.kind);
            strikedir::KindTuning own;
            if (ki >= 0) own = {g_kindSpeed[ki].load(), g_kindStart[ki].load()};
            const stroke::StrokeTuning t = strikedir::Effective(general, own);
            const int hand = sb.side & 1;
            if (t.minSpeed == appliedSpeed[hand] && t.startCm == appliedStart[hand]) continue;
            appliedSpeed[hand] = t.minSpeed;
            appliedStart[hand] = t.startCm;
            const stroke::Params p = stroke::ParamsFor(t);
            g_engine[hand].SetParams(p);
            LogF("melee touch: the stroke's gate, %c hand (%s%s) - a contact counts from %.2f m/s (a stroke may begin at %.2f, it ends at rest under %.2f), after %.0f cm of movement",
                 hand ? 'L' : 'R', ki >= 0 ? strikedir::KindName(ki) : "thrown", own.minSpeed >= 0.3f || own.startCm >= 2.0f ? ", its own numbers" : "", p.strokeSpeed, p.beginSpeed, p.quietSpeed,
                 p.onsetExcursion * 100.0f);
        }
    }
    const thrust::Rule thrustRule = CurrentThrust();
    {   // [gameplay] melee_thrust (vr/thrust_stroke.h): which blades thrust - by the game's class of the item (1ht, 2ht) or the kind's own setting; never a flail's ball, never
        // without the blade in the room (the thrust is the blade's own axis, measured where the hero walking adds nothing) - and each hand's thrust engine has its numbers
        static float appliedSpeed[2] = {-1.0f, -1.0f}, appliedCm[2] = {-1.0f, -1.0f}, appliedAccel[2] = {-1.0f, -1.0f};
        static int toldClass[2] = {-2, -2};
        static bool toldThrusts[2] = {false, false};
        for (SideBlade& sb : blades) {
            if (!sb.ok) continue;
            const int ki = strikedir::KindIndex(sb.kind), hand = sb.side & 1;
            sb.thrusts = !sb.ball && sb.s.haveRoom && thrust::Applies(thrustRule, ki, sb.thrustClass);
            if (sb.thrusts != toldThrusts[hand] || sb.thrustClass != toldClass[hand]) {
                toldThrusts[hand] = sb.thrusts;
                toldClass[hand] = sb.thrustClass;
                if (thrustRule.on)
                    LogF("melee touch: %c hand, the %s %s by a thrust (the game's class of the item: %s%s)", hand ? 'L' : 'R', ki >= 0 ? strikedir::KindName(ki) : "weapon",
                         sb.thrusts ? "hits" : "does not hit", sb.thrustClass > 0 ? "a thrust, 1ht / 2ht" : sb.thrustClass == 0 ? "a swing" : "not read - by kind",
                         ki >= 0 && thrustRule.mode[static_cast<size_t>(ki)] != thrust::kByGame ? (thrustRule.mode[static_cast<size_t>(ki)] == thrust::kAlways ? "; the kind's Thrust tick is on" : "; the kind's Thrust tick is off") : "; the kind has no Thrust tick in the ini - by the game's class");
            }
            if (!sb.thrusts) { ResetThrust(hand); appliedSpeed[hand] = appliedCm[hand] = -1.0f; continue; }
            const thrust::Numbers n = thrust::NumbersFor(thrustRule, ki);
            if (n.minSpeed == appliedSpeed[hand] && n.cm == appliedCm[hand] && n.minAccel == appliedAccel[hand]) continue;
            appliedSpeed[hand] = n.minSpeed;
            appliedCm[hand] = n.cm;
            appliedAccel[hand] = n.minAccel;
            const stroke::Params p = thrust::ParamsFor(n);
            g_thrustEngine[hand].SetParams(p);
            LogF("melee touch: the thrust's gate, %c hand (%s) - a poke counts from %.2f m/s once the tip has gone %.0f cm along the blade, point first (%.0f%% of its path), %s%.0f m/s^2 of peak acceleration; a thrust may begin at %.2f after %.1f cm",
                 hand ? 'L' : 'R', ki >= 0 ? strikedir::KindName(ki) : "weapon", p.strokeSpeed, n.cm, thrustRule.alongPct, n.minAccel > 0.0f ? "with at least " : "the acceleration not asked - ", n.minAccel,
                 p.beginSpeed, p.onsetExcursion * 100.0f);
        }
    }
    // The strike rules (vr/strike_direction.h): toward the monster, the head, the amplitude - per kind
    const strikedir::Rule rule = CurrentRule();
    g_touchBodiesAt = now;   // ([weight] grab_bodies: the touch keeps the bodies itself)
    if (BodiesWanted() && now - g_bodiesScanAt >= 30 && now - g_lastScanAt < 100) {   // [weight] monster_bodies / [debug] melee_boxes: the bodies where the monsters are now
        g_bodiesScanAt = now;
        Scan bodies;
        if (ScanUnits(grid.x, grid.z, upm, heroY, &bodies)) PublishBodies(bodies, now, pose.hero, upm);
    } else if (BodiesWanted() && (now - g_rigsAt >= 10 || now < g_rigsAt)) {   // ... and between the scans the limbs as the game poses them (every 10 ms)
        RepublishBodies(now, pose.hero, upm);
    }
    if (now - g_lastScanAt >= 100) {
        g_lastScanAt = now;
        if (ScanUnits(grid.x, grid.z, upm, heroY, &g_scan)) {
            g_heroId.store(g_scan.heroId);
            PublishBodies(g_scan, now, pose.hero, upm);
            g_bodiesScanAt = now;
            std::lock_guard<std::mutex> lock(g_verdictMutex);
            g_probeCount = 0;
            for (size_t i = 0; i < g_scan.n && g_probeCount < g_probeIds.size(); ++i)
                if (g_scan.c[i].d2 <= 14.0f * 14.0f) g_probeIds[g_probeCount++] = g_scan.c[i].id;
        } else {
            g_scan.n = 0;
            g_heroId.store(0);
        }
        {   // for the first test: what the scan makes of the game (hero found? monsters read? their classes, bodies, the server's game)
            static unsigned told = 0;
            static size_t toldN = 0;
            if (told < 8 && g_scan.n != toldN && g_scan.n > 0) {
                ++told;
                toldN = g_scan.n;
                size_t nearest = 0;
                for (size_t i = 1; i < g_scan.n; ++i) if (g_scan.c[i].d2 < g_scan.c[nearest].d2) nearest = i;
                const Cand& c = g_scan.c[nearest];
                LogF("melee touch: scan - hero id %u, %u monsters within 24 units; the nearest: class %u, %.1f units away, body radius %.2f m, %.2f to %.2f m up; the server's game %s",
                     g_scan.heroId, static_cast<unsigned>(g_scan.n), c.cls, sqrtf(c.d2), c.body.radius, c.body.bottom.y, c.body.top.y,
                     g_serverGame.load() ? "learned" : "not learned yet");
            }
        }
        if (g_probeCount) AskVerdicts(now);
    }

    // Each blade its own stroke: the monsters' places are read afresh once a pass (not 100 ms old: the hash is small), for the first
    // blade that sweeps; a monster both blades touch in one pass is approved once.
    Scan fresh;
    bool haveFresh = false, freshOk = false;
    uint32_t approvedId = 0;
    struct Pending {   // what a blade touched in this pass, to be pressed once every blade has said (below the loop)
        int side;
        const Cand* c;
        strikedir::Judgement jd;
        float speed;
        uint64_t stroke;
        float sweepSpeed;
        bool thrust;    // [gameplay] melee_thrust: the contact of a thrust (the thrust engine's stroke)
        thrust::Verdict tv;   // ... and the thrust's numbers, for its log line
        bool force = false;   // [weight] strike_by_force: a strike judged by its force (speed = its score, N s; need its threshold)
        float need = 0.0f;
        float approach = -1.0f;   // ... and its arrival's closing speed, m/s ([gameplay] melee_direct_client's speed rule)
        const char* forceWhat = "";
    };
    Pending pend[2];
    int npend = 0;
    const strikeforce::Settings force = CurrentForce();                // [weight] strike_by_force
    const unsigned forceSlots = force.on ? weight::ForceSlots() : 0u;   // ... the slots whose weapon a hand holds
    for (const SideBlade& sb : blades) {
        if (!sb.ok) continue;
        const int side = sb.side;
        stroke::Sweep sweep;
        const bool swept = g_engine[side].Step(sb.s, sweep);
        // [gameplay] melee_head_volume: the head's own volume swept with the blade - from where it was the pose before to where it is now, the stroke's own
        stroke::Sweep hsweep[meleeboxes::kHeads];
        bool hswept[meleeboxes::kHeads] = {};
        for (int hk = 0; hk < meleeboxes::kHeads; ++hk) {
            HeadPrev& hp = g_headPrev[side & 1][hk];
            const bool have = hk < sb.nHeads;
            if (have && hp.ok && hp.epoch == sb.s.epoch && hp.context == sb.s.context && sb.s.time > hp.t && sb.s.time - hp.t < 0.1) {
                const float k = static_cast<float>(1.0 / (sb.s.time - hp.t));
                hsweep[hk].before = hp.b;
                hsweep[hk].after = sb.heads[hk];
                hsweep[hk].stroke = sweep.stroke;
                hsweep[hk].speed = sweep.speed;
                hsweep[hk].baseVel = (sb.heads[hk].base - hp.b.base) * k;
                hsweep[hk].tipVel = (sb.heads[hk].tip - hp.b.tip) * k;
                hsweep[hk].haveVel = true;
                hswept[hk] = swept;
            }
            if (sb.s.time != hp.t || !have) {
                hp.ok = have;
                if (have) hp.b = sb.heads[hk];
                hp.t = sb.s.time;
                hp.epoch = sb.s.epoch;
                hp.context = sb.s.context;
            }
        }
        // the head's travel in the stroke that is on (the amplitude), and the tip's way over the last few poses (the debug's look) - every pose, swept or not. The travel is measured in the room
        // frame like the engine's speed (the hero walking adds nothing), the way in the world frame the monsters are in.
        g_travel[side].Feed(sb.s.time, sb.s.haveRoom ? sb.s.room.tip : sb.s.blade.tip, g_engine[side].StrokeId());
        g_tipWay[side].Feed(sb.s.time, sb.s.blade.tip);
        // [weight] stab_in: every pointed blade (a sword too, whether it thrusts for hit by touch or not) moving point first now - the stab's own judge (vr/thrust_stroke.h
        // PointFirst), on the grip and a point ahead of it along the weapon in the room. Only the stab reads it (StabbingNow): what hit by touch counts is not changed.
        if (sb.s.haveRoom && !sb.ball && thrust::PointedKind(strikedir::KindIndex(sb.kind))) {
            // [gameplay] melee_thrust_with_run: as the thrust, measured in the WORLD - the hero running with the weapon held forward is the stab's movement too
            // (the author: «если я бегу и держу перед собой оружие оно должно воткнуться»)
            const float at = thrust::GripAt(sb.s.room, sb.s.hand.p);
            const stroke::Blade gb = thrustRule.withRun ? thrust::GripBladeAt(sb.s.blade, at) : thrust::GripBladeAt(sb.s.room, at);
            g_pointFirst[side & 1].Feed(sb.s.time, gb.base, gb.tip);
            if (g_pointFirst[side & 1].Now(thrustRule.alongPct)) g_stabbingAt[side & 1].store(now);
        } else {
            g_pointFirst[side & 1].Reset();
        }
        // [gameplay] melee_thrust (vr/thrust_stroke.h): the thrust engine on the same sample, with the thrust's numbers, and the tip's path along the blade in the room - every pose
        stroke::Sweep tsweep;
        bool tswept = false;
        if (sb.thrusts) {
            // The whole weapon, not its tip (thrust::GripBlade): the engine steps on the grip and a point kGripAheadM out, so a turn of the hand on a 2 m spear
            // neither powers a "poke" nor keeps one from ending; Path follows the grip against the weapon's axis and how far that axis turns.
            // [gameplay] melee_thrust_with_run: the same grip blade in the WORLD (metres), so the hero running with the weapon forward is the thrust's movement too.
            stroke::Sample ts = sb.s;
            stroke::Vec pathTip = sb.s.room.tip;
            if (ts.haveRoom) {
                const float at = thrust::GripAt(sb.s.room, sb.s.hand.p);
                ts.room = thrustRule.withRun ? thrust::GripBladeAt(sb.s.blade, at) : thrust::GripBladeAt(sb.s.room, at);
                ts.lever = stroke::Length(ts.room.tip - ts.room.base);
                if (thrustRule.withRun) pathTip = sb.s.blade.tip;
            }
            tswept = g_thrustEngine[side].Step(ts, tsweep);
            g_thrustPath[side].Feed(sb.s.time, ts.room.base, pathTip, g_thrustEngine[side].StrokeId());
            if (thrust::ThrustingNow(thrustRule, g_thrustPath[side])) g_stabbingAt[side & 1].store(now);   // [weight] stab_in: a weapon that thrusts, thrusting now (StabbingNow)
            if (thrustdebugwire::Wanted()) {   // [debug] thrust_debug: the jab being made, and the one just ended (touching a monster or not) with its peaks against the thresholds
                const int ki = strikedir::KindIndex(sb.kind);
                const thrust::Verdict pv = thrust::Judge(thrustRule, ki, g_thrustPath[side]);
                thrustdbg::JabTracker::Peaks done;
                if (g_dbgJabs[side].Feed(g_thrustEngine[side].StrokeId(), g_thrustEngine[side].LastSpeed(), pv.alongCm, pv.share * 100.0f, pv.accel, &done, pv.turnDeg)) {
                    const thrustdbg::Reading jab = thrustdbg::JabReading(thrustRule, ki, done.speed, done.cm, done.alongPct, done.accel, done.stroke, now, done.turnDeg);
                    const bool shown = thrustdbg::Shown(jab, thrustdebugwire::Only());   // ([debug] thrust_debug_only: a swing's movement is not shown)
                    if (shown) {
                        g_dbgJab[side] = jab;
                        g_dbgSide = side;
                    }
                    if (shown && g_dbgTouchStroke[side] != done.stroke) {   // (a jab that touched a monster was told as its TOUCH)
                        const thrustdbg::Reading& j = g_dbgJab[side];
                        // (a swing, far from straight, leaves the blade's own colour; a jab tinted blue while it was made is not tinted again at its end)
                        if (j.alongPct >= 0.5f * j.needAlongPct && g_dbgLiveStroke[side] != done.stroke) DbgTint(side, j.counted, now, kDbgJabTintMs);
                        LogF("thrust debug: %c hand, %s jab %llu, no monster - %s%s | peak speed %.2f / %.2f m/s, along %.1f / %.0f cm, straight %.0f / %.0f %%, turn %.0f / %.0f deg, peak accel %.1f%s m/s^2",
                             side ? 'L' : 'R', ki >= 0 ? strikedir::KindName(ki) : "weapon", static_cast<unsigned long long>(j.stroke), j.counted ? "would count" : "would fail: ",
                             j.counted ? "" : thrustdbg::FailText(j.fail), j.speed, j.needSpeed, j.cm, j.needCm, j.alongPct, j.needAlongPct, j.turnDeg, j.needTurnDeg, j.accel,
                             j.needAccel > 0.0f ? (j.accel >= j.needAccel ? " (enough)" : " (too little)") : " (not asked)");
                    }
                }
                // While a touch NOW would be a counted thrust - the engine moving at the thrust's speed, the path a thrust (straight, long enough, not turned) -
                // the blade is blue, held for as long as that lasts: a poke the moment it qualifies (not at its end, the pull-back ~0.5 s later), a run with
                // the weapon ([gameplay] melee_thrust_with_run) for the whole charge (it lit only at the run's start: "it lights at the start, then goes out").
                // The readout and the log line still come at the jab's end.
                const uint64_t liveStroke = g_thrustEngine[side].StrokeId();
                if (liveStroke && g_thrustEngine[side].Counting() && pv.why == thrust::Why::Counts) {
                    g_dbgLiveStroke[side] = liveStroke;
                    DbgTint(side, true, now, kDbgLiveTintMs);
                }
            }
        } else {
            g_dbgJabs[side].Reset();
        }
        // [weight] strike_by_force (vr/strike_force.h): a weapon the weight model holds in a hand is judged by what its strikes put into the bodies - the impulse in the
        // first 150 ms, an edge's cut - against a threshold from its own steel, not by the blade's speed (its sweeps are not judged). The rules in front stay: the head
        // (melee_metal_head) and the butt (melee_not_butt) by where along the weapon it met the body, the verdict (hostile, no wall), the monster's shield or weapon
        // (ShieldStops); the press is the game's own attack, once per strike per monster.
        if (force.on && f.weightPlaced && (forceSlots & (1u << (sb.slot & 1)))) {
            weight::LiveStrike ls[8];
            const int nl = weight::LiveStrikes(ls, 8);
            strikeforce::Strike ks[8];
            double dur[8] = {};
            float appr[8] = {};   // the arrival's closing speed, m/s ([gameplay] melee_direct_client)
            float steel = 0.0f;
            int holdKind = 0, nk = 0;
            for (int i = 0; i < nl && nk < 8; ++i) {
                if (ls[i].slot != (sb.slot & 1)) continue;
                const strikemeasure::Strike& m = ls[i].s;
                ks[nk] = {m.id, m.serial, m.hands, m.window, m.cut, m.recent, m.startS, m.along};
                dur[nk] = m.durS;
                appr[nk] = static_cast<float>(m.approach);
                steel = ls[i].steelKg;
                holdKind = ls[i].kind;
                ++nk;
            }
            if (nk && !haveFresh) {
                haveFresh = true;
                freshOk = ScanUnits(grid.x, grid.z, upm, heroY, &fresh);
                if (freshOk) { PublishBodies(fresh, now, pose.hero, upm); g_bodiesScanAt = now; }
            }
            if (nk && freshOk) {
                const strikeforce::Family fam = strikeforce::FamilyOf(holdKind);
                strikeforce::Verdict v[8];
                strikeforce::JudgeAll(force, fam, steel, ks, nk, v);
                const float front = FrontAlong(sb, &sb == &blades[0]);
                for (int k = 0; k < nk; ++k) {
                    const Cand* c = nullptr;
                    for (size_t i = 0; i < fresh.n; ++i) if (fresh.c[i].id == ks[k].id) { c = &fresh.c[i]; break; }
                    if (!c) continue;
                    ForcePressed::E* was = g_forcePressed[sb.slot & 1].Find(ks[k].serial, ks[k].id);
                    const bool due = !was ? v[k].Counts()
                                          : strikeforce::RepeatDue(force, fam, steel, ks[k], strikeforce::HeroAttacking(f.heroMode), (double)(now - was->at) * 0.001);
                    static unsigned told = 0;
                    const bool tell = told < 12 || meleeboxes::Wanted() || g_forceDebug.load();
                    if (!due) {
                        if (!was && dur[k] >= strikemeasure::kWindowS && tell && ForceToldOnce(ks[k].serial)) {
                            ++told;
                            LogF("melee touch: %c hand, strike %u on monster %u NOT counted by force - %s: %.2f N s (%.2f in the first 150 ms%s), needs %.2f (%s, steel %.2f kg)%s", side ? 'L' : 'R',
                                 ks[k].serial, ks[k].id, strikeforce::WhyText(v[k].why), v[k].score, ks[k].window,
                                 fam == strikeforce::Family::Edged ? (std::string(" + cut ") + std::to_string((int)ks[k].cut) + " J").c_str() : "", v[k].need, strikeforce::FamilyName(fam), steel,
                                 told == 12 ? " (the first twelve are told always; more with [debug] melee_boxes or strike_force_debug)" : "");
                        }
                        continue;
                    }
                    // the head and the butt: where along the weapon it met the body (the weight model's contact) - a shaft's push is no hit
                    const bool shaft = rule.metalHead && sb.headAlong > 0.0f && ks[k].along >= 0.0f && ks[k].along < sb.headAlong;
                    const bool butt = rule.notButt && front > 0.0f && ks[k].along >= 0.0f && ks[k].along < front;
                    if (shaft || butt) {
                        if (tell && ForceToldOnce(ks[k].serial ^ 0x80000000u)) {
                            ++told;
                            LogF("melee touch: %c hand, strike %u on monster %u by force - %s: it met the body at %.0f%% of the weapon's length (%s at %.0f%%) - a push, no hit", side ? 'L' : 'R',
                                 ks[k].serial, ks[k].id, shaft ? "the shaft" : "the butt", ks[k].along * 100.0f, shaft ? "the head starts" : "the front hand", (shaft ? sb.headAlong : front) * 100.0f);
                        }
                        continue;
                    }
                    const Know kn = Verdicted(c->id, now);
                    if (kn == Know::No) { ++g_n.notHostile; continue; }
                    if (kn == Know::Unknown) { ++g_n.noVerdict; continue; }
                    if (ShieldStops(f, side, c->id, f.session, ks[k].serial, v[k].score, false, now)) continue;
                    g_forcePressed[sb.slot & 1].Note(ks[k].serial, ks[k].id, now);
                    if (npend < 2) {
                        Pending& p = pend[npend++];
                        p = Pending{side, c, strikedir::Judgement{}, v[k].score, ks[k].serial, 0.0f, false, thrust::Verdict{}};
                        p.force = true;
                        p.need = v[k].need;
                        p.approach = appr[k];
                        p.forceWhat = was ? "again, pushed in" : v[k].why == strikeforce::Why::Shared ? "a blow shared between monsters" : strikeforce::FamilyName(fam);
                    }
                    break;   // (one press a blade a pass)
                }
            }
            continue;
        }
        if (!swept && !tswept) continue;
        frametrace::Note(frametrace::kSweep);   // [debug] frame_trace: the blade moved fast enough to be swept
        if (swept) {
            ++g_n.sweeps;
            g_sweepSpeed[side] = sweep.speed;   // (for the [debug] melee_boxes log)
            g_sweepAt[side] = now;
        }
        if (tswept) ++g_n.thrustSweeps;
        if (g_barrelsWanted.load() && viewgate::F4()) BarrelTouch(side, swept ? sweep : tsweep, grid, upm, heroY, now);   // [gameplay] melee_touch_barrels
        if (!haveFresh) {
            haveFresh = true;
            freshOk = ScanUnits(grid.x, grid.z, upm, heroY, &fresh);
            if (freshOk) { PublishBodies(fresh, now, pose.hero, upm); g_bodiesScanAt = now; }
        }
        if (!freshOk) { ++g_n.noHero; continue; }
        const Cand* first = nullptr;
        bool touchedAny = false;   // [flail] chain: the blade met a body in this sweep (counted as an attack or not): the flail's head is kicked back
        float firstT = 2.0f, firstSpeed = 0.0f;
        strikedir::Judgement firstJd;
        bool byThrust = false;
        thrust::Verdict firstVerdict;   // (a thrust's: what the log line of the counted thrust says)
        strikedir::Rule bladeRule = rule;   // [gameplay] melee_not_backward: only a weapon that thrusts refuses a movement from its point toward its handle (its swing too)
        bladeRule.backward = rule.backward && sb.thrusts;
        bladeRule.frontAlong = FrontAlong(sb, &sb == &blades[0] && f.weightPlaced);   // [gameplay] melee_not_butt: behind the front hand is no strike
        bladeRule.headAlong = sb.headAlong;   // [gameplay] melee_metal_head: below the head (the game's model's) is a push, no strike
        // A thrust is judged FIRST: a poke the thrust counts is not judged again as a swing (the swing's amplitude would call a short stab too small). Its own least speed where it
        // touched, the path along the blade (a movement that is not a thrust is left to the swing, quietly - nearly every swing of a spear is one), the strike rules without the
        // swing's amplitude (toward the monster, the head) and the thrust's own distance. A refused thrust does not use the monster up, as a refused swing does not.
        if (tswept) {
            const int kindIdx = strikedir::KindIndex(sb.kind);
            const thrust::Verdict v = thrust::Judge(thrustRule, kindIdx, g_thrustPath[side]);
            const float least = g_thrustEngine[side].GetParams().strokeSpeed;
            const strikedir::Rule strike = thrust::StrikeRule(bladeRule);
            if (v.why == thrust::Why::NotAlong && thrustdebugwire::Wanted())   // [debug] thrust_debug: a monster touched by a movement that is not a thrust - told as "not straight" (the swing judges it)
                for (size_t i = 0; i < fresh.n; ++i) {
                    const Cand& c = fresh.c[i];
                    const stroke::Contact touch = stroke::TouchOf(tsweep, c.body);
                    if (touch.t < 0.0f || g_thrustHits[side].Contains(f.session, tsweep.stroke, c.id)) continue;
                    DbgTouch(side, thrustdbg::ContactReading(thrustRule, kindIdx, v, touch.speed, strikedir::Why::Counts, c.id, tsweep.stroke, now), now, ThrustToldOnce(side, tsweep.stroke, c.id));
                    break;
                }
            for (size_t i = 0; v.why != thrust::Why::NotAlong && i < fresh.n; ++i) {
                const Cand& c = fresh.c[i];
                stroke::Body onBody;
                bool viaWeapon = false;
                const stroke::Contact touch = TouchOrWeapon(tsweep, c.id, c.body, &onBody, &viaWeapon);   // ([weight] parry_counts_hit: or its weapon in its attack)
                const float t = touch.t;
                if (t < 0.0f || t >= firstT || g_thrustHits[side].Contains(f.session, tsweep.stroke, c.id)) continue;
                const float at = touch.speed;
                const strikedir::Judgement jd = strikedir::Judge(strike, kindIdx, tsweep, touch, onBody, 0.0f);
                static unsigned told = 0;
                const bool firstOnes = told < 12;   // (as the swing's: the first twelve told whatever the debug says, then with [debug] melee_boxes - one line per thrust and monster)
                if (at >= least && jd.why == strikedir::Why::Butt) TellButtOnce(side, c.id, jd.along, strike.frontAlong, at, strikedir::KindName(kindIdx));
                if (at >= least && jd.why == strikedir::Why::Shaft) TellShaftOnce(side, c.id, jd.along, strike.headAlong, at, strikedir::KindName(kindIdx), sb.headFrom);
                const int reason = at < least ? 3 : jd.why == strikedir::Why::Away || jd.why == strikedir::Why::Backward ? 0 : jd.why == strikedir::Why::NotHead || jd.why == strikedir::Why::Butt || jd.why == strikedir::Why::Shaft ? 1
                                 : v.why == thrust::Why::Short || v.why == thrust::Why::Weak ? 2 : -1;
                if (reason >= 0 && thrustdebugwire::Wanted())   // [debug] thrust_debug: the refused contact's numbers, the blade red (its log line below)
                    DbgTouch(side, thrustdbg::ContactReading(thrustRule, kindIdx, v, at, jd.why, c.id, tsweep.stroke, now), now, false);
                if (reason >= 0) {
                    (reason == 3 ? g_n.slowPart : reason == 0 ? g_n.away : reason == 1 ? g_n.notHead : v.why == thrust::Why::Weak ? g_n.thrustWeak : g_n.thrustShort)++;
                    MarkContact(jd, c.id, c.body, thrustdebugwire::Wanted() ? meleeboxes::kCThrustNo : reason == 0 ? meleeboxes::kCBlue : reason == 1 ? meleeboxes::kCGrey : meleeboxes::kCAmber, true, now,
                                true);   // [debug] melee_boxes (with thrust_debug: the thrust's own orange)
                    if ((firstOnes || meleeboxes::Wanted()) && ThrustToldOnce(side, tsweep.stroke, c.id)) {
                        if (firstOnes) ++told;
                        char detail[200], accel[80];
                        if (reason == 3) snprintf(detail, sizeof detail, "too slow: the %s's point moves %.1f m/s where it touched, a thrust needs %.1f", strikedir::KindName(kindIdx), at, least);
                        else if (jd.why == strikedir::Why::Backward) snprintf(detail, sizeof detail, "backward: the touching point moves %.0f deg from the handle's direction along the blade (refused within %.0f)", strikedir::DegOf(-jd.axisCos), strikedir::kBackwardDeg);
                        else if (reason == 0) snprintf(detail, sizeof detail, "not toward the monster: its point moves %.0f deg from the direction to its axis (it must be within %.0f)", jd.haveAxis ? jd.angleDeg : 0.0f, rule.towardDeg);
                        else if (jd.why == strikedir::Why::Butt) snprintf(detail, sizeof detail, "the butt: it touched at %.0f%% of the weapon's length, behind the front hand at %.0f%%", jd.along * 100.0f, strike.frontAlong * 100.0f);
                        else if (jd.why == strikedir::Why::Shaft) snprintf(detail, sizeof detail, "the shaft: it touched at %.0f%% of the weapon's length, the head starts at %.0f%%", jd.along * 100.0f, strike.headAlong * 100.0f);
                        else if (reason == 1) snprintf(detail, sizeof detail, "not the head: it touched at %.0f%% of the blade's length, the head is the outer %.0f%%", jd.along * 100.0f, rule.headPct);
                        else if (v.why == thrust::Why::Weak) snprintf(detail, sizeof detail, "a push, not a jab: the %s's point reached %.1f m/s^2 along the blade, the least is %.0f", strikedir::KindName(kindIdx), v.accel, v.needAccel);
                        else snprintf(detail, sizeof detail, "too short: the tip has gone %.1f cm along the blade, the %s's thrust needs %.0f", v.alongCm, strikedir::KindName(kindIdx), v.needCm);
                        AccelText(accel, sizeof accel, v);
                        LogF("melee touch: %c hand, thrust %llu on monster %u NOT counted - %s | %.1f m/s where it touched, %.1f cm along the blade, %.0f%% of the path along it, %s%s", side ? 'L' : 'R',
                             static_cast<unsigned long long>(tsweep.stroke), c.id, detail, at, v.alongCm, v.share * 100.0f, accel,
                             firstOnes && told == 12 ? " (the first twelve are told always; more with [debug] melee_boxes)" : "");
                    }
                    continue;
                }
                const Know k = Verdicted(c.id, now);
                if (k == Know::No) { ++g_n.notHostile; continue; }
                if (k == Know::Unknown) { ++g_n.noVerdict; continue; }
                if (viaWeapon) TellWeaponHit(side, c.id, at, true);
                else if (ShieldStops(f, side, c.id, f.session, tsweep.stroke, at, true, now)) continue;   // [weight] monster_shields: a thrust into a shield is no hit either
                first = &c;
                firstT = t;
                firstSpeed = at;
                firstJd = jd;
                byThrust = true;
                firstVerdict = v;
                if (thrustdebugwire::Wanted()) DbgTouch(side, thrustdbg::ContactReading(thrustRule, kindIdx, v, at, jd.why, c.id, tsweep.stroke, now), now, false);   // (its log line is the counted one below)
            }
        }
        const float least = g_engine[side].GetParams().strokeSpeed;
        const bool swingJudged = swept && !byThrust;   // (the swing's contacts, as they always were - unless a thrust already took this pass)
        for (size_t i = 0; swingJudged && i < fresh.n; ++i) {
            const Cand& c = fresh.c[i];
            stroke::Body onBody;
            bool viaWeapon = false;
            stroke::Contact touch = TouchOrWeapon(sweep, c.id, c.body, &onBody, &viaWeapon);   // ([weight] parry_counts_hit: or its weapon in its attack)
            const int kindIdx = strikedir::KindIndex(sb.kind);
            // The strike rules' verdict on this contact (vr/strike_direction.h): toward the monster, the head, the amplitude - and where the touching point was, its way, the angle to the axis (for the
            // debug's marks and the log). A refused contact does not use the monster up: a later contact of the stroke, or the next stroke, counts. Only whether the contact counts as an attack - the
            // game's attack, damage and rolls are not touched.
            strikedir::Judgement jd = touch.t >= 0.0f ? strikedir::Judge(sb.ball ? strikedir::ForBall(bladeRule) : bladeRule, kindIdx, sweep, touch, onBody, g_travel[side].Meters())
                                                      : strikedir::Judgement{};
            for (int hk = 0; hk < meleeboxes::kHeads; ++hk) {   // [gameplay] melee_head_volume, a flail's other balls: that volume met it - the head's contact (judged by its own motion,
                if (!hswept[hk]) continue;                       // nothing of the shaft to refuse); two balls on one monster are still one hit (the earliest counted)
                const stroke::Contact ht = stroke::TouchOf(hsweep[hk], c.body);
                if (ht.t >= 0.0f) {
                    const strikedir::Judgement hj = strikedir::Judge(strikedir::ForHeadVolume(bladeRule), kindIdx, hsweep[hk], ht, c.body, g_travel[side].Meters());
                    const bool mainOk = touch.t >= 0.0f && jd.why == strikedir::Why::Counts && touch.speed >= least;
                    const bool headOk = hj.why == strikedir::Why::Counts && ht.speed >= least;
                    if (touch.t < 0.0f || (headOk && (!mainOk || ht.t < touch.t)) || (!mainOk && !headOk && ht.t < touch.t)) {
                        touch = ht;
                        jd = hj;
                        onBody = c.body;
                        viaWeapon = false;
                    }
                }
            }
            const float t = touch.t;
            if (t >= 0.0f) touchedAny = true;
            if (t < 0.0f || t >= firstT || g_hits[side].Contains(f.session, sweep.stroke, c.id)) continue;
            // The speed that counts is the blade's where it TOUCHES (the two ends' velocities blended at the contact point, stroke::ContactSpeed): the
            // tip of a blade that swings about the hand is fast, its grip end is not. A touch at a part moving slower than the least speed waits - the
            // monster is not used up: a later sweep of the stroke may touch it with a faster part.
            const float at = touch.speed;
            if (at < least) {
                ++g_n.slowPart;
                // [debug] melee_boxes: an amber ball (and the arrow, the line, the tint) where the blade met the monster too slowly, and a line (kind, speed, the way)
                MarkContact(jd, c.id, c.body, meleeboxes::kCAmber, true, now);
                if (TellRefusal(3, now, false))
                    LogF("melee touch: %c hand, a contact with monster %u is not counted - too slow: the %s moves %.1f m/s where it touched, the least is %.1f m/s%s", side ? 'L' : 'R', c.id,
                         strikedir::KindName(kindIdx), at, least, jd.haveWay ? (jd.dir.y >= 0.0f ? " (going up)" : " (going down)") : "");
                continue;
            }
            if (jd.why == strikedir::Why::Butt) TellButtOnce(side, c.id, jd.along, bladeRule.frontAlong, at, strikedir::KindName(kindIdx));
            if (jd.why == strikedir::Why::Shaft) TellShaftOnce(side, c.id, jd.along, bladeRule.headAlong, at, strikedir::KindName(kindIdx), sb.headFrom);
            if (jd.why != strikedir::Why::Counts) {
                const int reason = jd.why == strikedir::Why::Away || jd.why == strikedir::Why::Backward ? 0 : jd.why == strikedir::Why::NotHead || jd.why == strikedir::Why::Butt || jd.why == strikedir::Why::Shaft ? 1 : 2;
                (reason == 0 ? g_n.away : reason == 1 ? g_n.notHead : g_n.small)++;
                const int colour = reason == 0 ? meleeboxes::kCBlue : reason == 1 ? meleeboxes::kCGrey : meleeboxes::kCAmber;
                MarkContact(jd, c.id, c.body, colour, true, now);   // [debug] melee_boxes: the marks, the monster tinted
                static unsigned told = 0;
                const bool firstOnes = told < 12;   // (the first twelve are told whatever the debug says; then, with [debug] melee_boxes, one each 250 ms for each reason)
                if (TellRefusal(reason, now, firstOnes)) {
                    if (firstOnes) ++told;
                    char detail[160];
                    if (jd.why == strikedir::Why::Backward) snprintf(detail, sizeof detail, "the touching point moves %.0f deg from the handle's direction along the blade (refused within %.0f)", strikedir::DegOf(-jd.axisCos), strikedir::kBackwardDeg);
                    else if (reason == 0) snprintf(detail, sizeof detail, "its point moves %.0f deg from the direction to the monster's axis (it must be within %.0f)", jd.haveAxis ? jd.angleDeg : 0.0f, rule.towardDeg);
                    else if (jd.why == strikedir::Why::Butt) snprintf(detail, sizeof detail, "it touched at %.0f%% of the weapon's length, behind the front hand at %.0f%%", jd.along * 100.0f, bladeRule.frontAlong * 100.0f);
                    else if (jd.why == strikedir::Why::Shaft) snprintf(detail, sizeof detail, "it touched at %.0f%% of the weapon's length, the head starts at %.0f%%", jd.along * 100.0f, bladeRule.headAlong * 100.0f);
                    else if (reason == 1) snprintf(detail, sizeof detail, "it touched at %.0f%% of the blade's length, the head is the outer %.0f%%", jd.along * 100.0f, rule.headPct);
                    else snprintf(detail, sizeof detail, "the head has travelled %.0f cm in this stroke, the %s needs %.0f", jd.travelCm, strikedir::KindName(kindIdx), jd.needCm);
                    LogF("melee touch: %c hand, a contact with monster %u is not counted - %s: %s; %.1f m/s where it touched%s", side ? 'L' : 'R', c.id, strikedir::WhyText(jd.why), detail, at,
                         firstOnes && told == 12 ? " (the first twelve are told always; more with [debug] melee_boxes, throttled)" : "");
                }
                continue;
            }
            const Know k = Verdicted(c.id, now);
            if (k == Know::No) { ++g_n.notHostile; continue; }
            if (k == Know::Unknown) { ++g_n.noVerdict; continue; }
            if (viaWeapon) TellWeaponHit(side, c.id, at, false);
            else if (ShieldStops(f, side, c.id, f.session, sweep.stroke, at, false, now)) continue;   // [weight] monster_shields
            first = &c;
            firstT = t;
            firstSpeed = at;
            firstJd = jd;
        }
        if (touchedAny) {   // [flail] chain: once a stroke - the look only (vr/flail_chain.h); nothing of the attack
            static uint64_t kickedStroke[2] = {~0ull, ~0ull};
            if (kickedStroke[side] != sweep.stroke) { kickedStroke[side] = sweep.stroke; skel::FlailHit(side); }
        }
        if (first) {
            ++g_n.touched;
            // (the press is made below, once every blade of this pass has said what it touched: two blades on one monster in one pass are a pair for [hands] double_swing_by_hands)
            // The monster answers once per stroke of the engine that found it - and once for the stroke the other engine has on: one movement both count is one press.
            const uint64_t strokeId = byThrust ? tsweep.stroke : sweep.stroke;
            if ((byThrust ? g_thrustHits[side] : g_hits[side]).Insert(f.session, strokeId, first->id)) {
                if (byThrust && g_engine[side].StrokeId()) g_hits[side].Insert(f.session, g_engine[side].StrokeId(), first->id);
                if (!byThrust && g_thrustEngine[side].StrokeId()) g_thrustHits[side].Insert(f.session, g_thrustEngine[side].StrokeId(), first->id);
                if (npend < 2) pend[npend++] = {side, first, firstJd, firstSpeed, strokeId, byThrust ? tsweep.speed : sweep.speed, byThrust, firstVerdict};
            }
        }
    }

    // What each blade touched in this pass becomes the game's attack. A monster that both blades met is pressed once. [hands] double_swing_by_hands: with a weapon in each hand, the strike of the SECOND hand on
    // the monster the first hand struck within the window is the game's Double Swing (or Frenzy) instead of the attack key - vr/double_swing.h decides, the game does the swings. Two blades that met one monster
    // in one pass are the pair at once (the first only noted: the second says); everything else is as it was.
    const bool dualBlades = blades[0].ok && blades[1].ok && !thrown;
    // [gameplay] melee_direct_client: a touch as its rules measure it - the speed where it touched (a strike by force: its arrival's closing speed), the
    // force (a strike by force: its score; else the weight model's live strike on that monster, if it measures one) and the swing's length: the
    // CONTROLLERS' own paths in this swing (since the stroke began, its onset's wind-up included; no stroke on - a thrust, a strike by force: the
    // last half second) - the weapon hand's, a two-handed weapon's the longer of the two. Only asked while the option is on; -1 for what is not known.
    const auto TouchOf = [&](const Pending& p) {
        meleedirectclient::Touch t;
        if (!meleedirectclient::Wanted() && !meleedirect::ByRules()) return t;
        t.speed = p.force ? p.approach : p.speed;
        t.force = p.force ? p.speed : ForceNow(p.c->id);
        const int side = p.side & 1;
        const double start = g_engine[side].StrokeStart();
        const double since = start > 0.0 ? start - g_engine[side].GetParams().onsetWindow : Seconds(D2RVRStampNow()) - 0.5;
        {
            std::lock_guard<std::mutex> lock(g_histMutex);
            for (int h = 0; h < 2; ++h) t.hand[h] = g_handPath[h].Since(since);
        }
        t.length = f.twoHanded ? std::max(t.hand[0], t.hand[1]) : t.hand[side];
        return t;
    };
    for (int i = 0; i < npend; ++i) {
        const Pending& p = pend[i];
        if (p.c->id == approvedId) continue;   // (the other blade's contact with this monster was pressed in this pass)
        ++g_n.pressed;
        if (p.thrust) ++g_n.thrusts;
        approvedId = p.c->id;
        MarkContact(p.jd, p.c->id, p.c->body, p.thrust && thrustdebugwire::Wanted() ? meleeboxes::kCThrustOk : meleeboxes::kCGreen, false, now, p.thrust);   // [debug] melee_boxes: a counted contact: the ball, the arrow and the line in green (a thrust's blue with thrust_debug)
        doubleswing::Press dual;
        int hand = p.side;
#ifdef MELEE_TOUCH_DIRECT
        if (doubleswing::On()) {
            const Pending* next = i + 1 < npend && pend[i + 1].c->id == p.c->id ? &pend[i + 1] : nullptr;
            if (next && dualBlades) {
                doubleswing::Note(p.side, p.c->id, now);
                dual = doubleswing::Strike(next->side, p.c->id, now, true);
                if (dual.dual) hand = next->side;
            } else {
                dual = doubleswing::Strike(p.side, p.c->id, now, dualBlades);
            }
        }
#endif
        if (dual.dual) ApproveDual(p.c->id, hand, dual);
        else Approve(p.c->id, !thrown, p.side, TouchOf(p), f.twoHanded, f.twoHandsOn);   // ([gameplay] melee_direct_client: its speed, force and the hands' paths)
        if (thrown)   // one throw per swing: the throw by gesture's release of this swing is dropped (the touch's press is the throw)
            if (const HoldOffFn hold = g_holdOff.load()) hold(meleetouch::HoldOffUntil(now));
        if (p.thrust && (g_n.thrusts <= 20 || meleeboxes::Wanted())) {   // [gameplay] melee_thrust: the counted thrust's numbers (the first twenty, every one with [debug] melee_boxes)
            char accel[80];
            AccelText(accel, sizeof accel, p.tv);
            LogF("melee touch: %c hand, thrust %llu on monster %u counted | %.1f m/s where it touched, %.1f cm along the blade (needs %.0f), %.0f%% of the path along it, %s", p.side ? 'L' : 'R',
                 static_cast<unsigned long long>(p.stroke), p.c->id, p.speed, p.tv.alongCm, p.tv.needCm, p.tv.share * 100.0f, accel);
        }
        if (p.force && (g_n.pressed <= 40 || meleeboxes::Wanted() || g_forceDebug.load()))   // [weight] strike_by_force: the counted strike's numbers
            LogF("melee touch: %c hand, strike %llu by force hit monster %u (class %u) - %.2f N s, needs %.2f (%s) - %s pressed on it", p.side ? 'L' : 'R', static_cast<unsigned long long>(p.stroke),
                 p.c->id, p.c->cls, p.speed, p.need, p.forceWhat, dual.dual ? (dual.skillId == doubleswing::kDoubleSwing ? "the game's Double Swing" : "the game's Frenzy") : "the game's attack");
        else if (g_n.pressed <= 20 && (p.thrust || !thrustdebugwire::Only()))   // ([debug] thrust_debug_only: the swing's press lines left out)
            LogF("melee touch: %c hand, %s %llu hit monster %u (class %u) - the blade at %.1f m/s (%.1f where it touched) - %s pressed on it%s", p.side ? 'L' : 'R', p.thrust ? "thrust" : "stroke",
                 static_cast<unsigned long long>(p.stroke), p.c->id, p.c->cls, p.sweepSpeed, p.speed, dual.dual ? (dual.skillId == doubleswing::kDoubleSwing ? "the game's Double Swing" : "the game's Frenzy") : "the game's attack",
                 thrown ? " (a thrown weapon: a point-blank throw; the throw by gesture's release of this swing is held off)" : "");
    }

    // [debug] melee_boxes: AFTER the engine's steps, so the colour and the numbers are those of this pose's step (read-only)
    if (meleeboxes::Wanted()) FeedBoxes(now, blades, upm, heroY, grid);
    PushWithBlades(now, blades, f.weightPlaced);   // [weight] monster_ragdoll: a weapon in each hand pushes the doll blade by blade (the weight model's own pushes it itself)
    if (thrustdebugwire::Wanted()) {   // [debug] thrust_debug: the readout over the weapon hand - the last touch and jab, or why the blade in hand does not thrust
        const SideBlade* thrusting = nullptr;
        for (const SideBlade& sb : blades) if (sb.ok && sb.thrusts && (!thrusting || (sb.side & 1) == g_dbgSide)) thrusting = &sb;
        if (thrusting) {
            if (!g_dbgTouch[g_dbgSide].have && !g_dbgJab[g_dbgSide].have) g_dbgSide = thrusting->side & 1;
            DbgPublish(nullptr);
        } else {
            char why[thrustdbg::kLineChars];
            const SideBlade& b0 = blades[0].ok ? blades[0] : blades[1];
            const int ki = strikedir::KindIndex(b0.kind);
            g_dbgSide = b0.side & 1;
            snprintf(why, sizeof why, "%s: no thrust - %s", ki >= 0 ? strikedir::KindName(ki) : "weapon",
                     !thrustRule.on ? "thrusts are off" : b0.ball ? "a flail's ball" : !b0.s.haveRoom ? "no room frame" : "its Thrust tick is off");
            DbgPublish(why);
        }
    }

    if (now - g_lastReportAt >= 10000) {
        g_lastReportAt = now;
        if (g_n.sweeps || g_n.thrustSweeps || g_n.touched || g_n.noHand) {
            LogF("melee touch: %u sweeps, %u touches, %u pressed (%u by a thrust, of %u thrust sweeps), %u at a part of the blade moving slower than the least speed, %u not toward the monster, %u not the head, %u a swing too small, %u a thrust too short, %u a push not a jab (not counted), %u not hostile or behind a wall, %u without a verdict yet, %u without the hero, %u without a hand sample%s",
                 g_n.sweeps, g_n.touched, g_n.pressed, g_n.thrusts, g_n.thrustSweeps, g_n.slowPart, g_n.away, g_n.notHead, g_n.small, g_n.thrustShort, g_n.thrustWeak, g_n.notHostile, g_n.noVerdict, g_n.noHero, g_n.noHand,
                 g_serverGame.load() ? "" : " (the server's game not learned yet)");
            g_n = {};
        }
    }
#endif  // D2RVR_FIRST_PERSON
}

int HostileVerdict(uint32_t id) {
    switch (Verdicted(id, GetTickCount64())) {
        case Know::Yes: return 1;
        case Know::No: return 0;
        default: return -1;
    }
}

bool TargetPending() { return viewgate::F4() && g_press.Pending(GetTickCount64()); }

bool Live(uint64_t maxAgeMs, uint32_t* weaponType) {
    if (!g_wanted.load() || !viewgate::F4()) return false;
    const ULONGLONG at = g_liveAt.load();
    if (at == 0 || GetTickCount64() - at > maxAgeMs) return false;
    if (weaponType) *weaponType = g_liveKind.load();
    return true;
}

uintptr_t TargetUnit(int where) {
    const uint32_t id = viewgate::F4() ? g_press.Target(GetTickCount64()) : 0;
    if (!id) return 0;
    const uintptr_t unit = ClientUnit(id, 1);
#ifdef MELEE_TOUCH_DIRECT
    if (unit && where == 2)   // the map click has asked for the target: a dual cast's button need not be held longer (the cast is on its way)
        if (const uint32_t pad = g_dualPad.exchange(0)) directinput::PadPulseEnd({static_cast<uint16_t>(pad & 0xFFFF), static_cast<uint8_t>(pad >> 16), 0});
#else
    (void)where;
#endif
    return unit;
}

bool PadPressed() {
#ifdef MELEE_TOUCH_DIRECT
    return false;
#else
    return g_padA.load();
#endif
}

bool PressDown() {
#ifdef MELEE_TOUCH_DIRECT
    return g_keyDown.load();
#else
    return g_padA.load();
#endif
}

void Install() {
    static ULONGLONG retryAt = 0;
    static unsigned tries = 0;
    if ((!g_wanted.load() && !g_throwWanted.load() && !g_bowWanted.load() && !g_leapWanted.load() && !g_statusWanted.load()) || g_serverHook.load() == 1 || !g_ctx || GetTickCount64() < retryAt) return;
    retryAt = GetTickCount64() + (tries < 30 ? 2000 : 30000);   // the server's code is decrypted as it runs: tried again
    if (!d2rsig::Addr(RVA_LOOKUP) || !d2rsig::Addr(RVA_RELATION) || !d2rsig::Addr(RVA_LINE) || !d2rsig::Addr(RVA_UNITS) ||
        !d2rsig::Addr(RVA_FIND_UNIT) || !d2rsig::Addr(RVA_MONSTATS))
        return;   // not all found (yet): the game's code is decrypted as it runs
    const bool in = d2rsig::Hook(RVA_LOOKUP, kSigLookup, sizeof kSigLookup, reinterpret_cast<void*>(&HookLookup), reinterpret_cast<void**>(&g_origLookup));
    g_serverHook.store(in ? 1 : 2);
    if (in) Log("melee touch: the server's unit lookup is observed (to learn its game); the hostility check will run on the game's thread");
    else if (++tries == 30) Log("melee touch: the server's unit lookup could not be hooked (another build?) - the client's own units answer instead");
}

// ---- a throw's auto-aim ([hands] throw_autoaim_deg): the monsters near the hero and the server's verdict on each ----------------
// The same read-only reads and the same ahead-of-time hostility check as the touch, for a thrown weapon in hand (the touch itself
// stands down for those): ThrowPoll about every 100 ms from vrcam's timer, ThrowTargets from the game's aim hooks at the release.

namespace {
struct ThrowMon { uint32_t id = 0; float x = 0.0f, z = 0.0f, d2 = 0.0f; MonsterRow row; bool rowOk = false; };
std::mutex g_thrMutex;
std::array<ThrowMon, 48> g_thr;
size_t g_thrN = 0;
std::atomic<ULONGLONG> g_thrFreshAt{0};
ULONGLONG g_thrScanAt = 0;   // the timer's thread only
}  // namespace

void SetThrowWanted(bool on) { g_throwWanted.store(on); }
bool ThrowWanted() { return g_throwWanted.load(); }
void SetBowWanted(bool on) { g_bowWanted.store(on); }
bool BowWanted() { return g_bowWanted.load(); }
void SetLeapWanted(bool on) { g_leapWanted.store(on); }
uintptr_t MonsterUnit(uint32_t id) { return id ? ClientUnit(id, 1) : 0; }

void ThrowPoll(uintptr_t heroUnit) {
    const ULONGLONG now = GetTickCount64();
    if (now - g_thrScanAt < 100) return;
    g_thrScanAt = now;
    auto clear = [&]() {
        std::lock_guard<std::mutex> lock(g_thrMutex);
        g_thrN = 0;
        g_thrFreshAt.store(0);
    };
    UnitBytes hero;
    float hx = 0.0f, hz = 0.0f;
    if (!(g_throwWanted.load() || g_bowWanted.load() || g_leapWanted.load()) || !viewgate::F4() || !heroUnit || !Read(hero.b, heroUnit, sizeof hero.b) || U32(hero, 0) != 0 || !GridOf(hero, &hx, &hz)) {
        clear();
        return;
    }
    const uint32_t heroId = U32(hero, 8);
    constexpr float kReach = 96.0f;   // game units: 48 cells, the farthest a throw's point goes (throwaim::kMaxCells)
    std::array<ThrowMon, 48> found;
    size_t n = 0;
    EachUnit(1, [&](uintptr_t, const UnitBytes& u) {
        const uint32_t mode = U32(u, 0xC);
        if (mode == 0 || mode == 12) return true;   // dying, dead
        float x, z;
        if (!GridOf(u, &x, &z)) return true;
        const float d2 = (x - hx) * (x - hx) + (z - hz) * (z - hz);
        if (d2 > kReach * kReach) return true;
        ThrowMon c;
        c.id = U32(u, 8);
        c.x = x;
        c.z = z;
        c.d2 = d2;
        c.rowOk = RowFor(u.b[unitscan::kOffBank], U32(u, 4), &c.row);   // (its size, for a bow's shot: [bow] pitch_aim)
        if (n < found.size()) { found[n++] = c; return true; }
        size_t farthest = 0;   // full: the farthest goes
        for (size_t i = 1; i < n; ++i) if (found[i].d2 > found[farthest].d2) farthest = i;
        if (c.d2 < found[farthest].d2) found[farthest] = c;
        return true;
    });
    {
        std::lock_guard<std::mutex> lock(g_thrMutex);
        g_thr = found;
        g_thrN = n;
        g_thrFreshAt.store(now);
    }
    g_heroId.store(heroId);
    {
        std::lock_guard<std::mutex> lock(g_verdictMutex);
        g_probeCount = 0;
        for (size_t i = 0; i < n && g_probeCount < g_probeIds.size(); ++i) g_probeIds[g_probeCount++] = found[i].id;
    }
    if (n) AskVerdicts(now);
}

size_t MonstersNear(float heroX, float heroZ, float reachUnits, float upm, float floorM, unitscan::Near* out, size_t cap) {
    return unitscan::Monsters([](void* d, uintptr_t s, size_t n) { return Read(d, s, n); }, d2rsig::Addr(RVA_UNITS), heroX, heroZ, reachUnits, upm, floorM,
                              [](uint8_t bank, uint32_t cls, MonsterRow* row) { return RowFor(bank, cls, row); }, out, cap);
}

size_t MissileScan(MissileView* out, size_t max) {
    size_t n = 0;
    if (!out || !max) return 0;
    EachUnit(3, [&](uintptr_t, const UnitBytes& u) {
        if (n >= max) return false;
        MissileView v;
        v.id = U32(u, 8);
        v.cls = U32(u, 4);
        v.mode = U32(u, 0xC);
        v.flags = U32(u, 0x124);
        if (!GridOf(u, &v.x, &v.z)) return true;
        float ms = 0.0f;
        if (const uintptr_t data = U64(u, 0x10); data > 0x10000 && Read(&ms, data + 0x14, sizeof ms) && std::isfinite(ms)) v.life = ms / 40.0f;   // ms -> frames
        out[n++] = v;
        return true;
    });
    return n;
}

bool MissileInHash(uintptr_t unit, unsigned* seen) {
    unsigned n = 0;
    bool found = false;
    if (unit) EachUnit(3, [&](uintptr_t at, const UnitBytes&) {
        ++n;
        if (at == unit) { found = true; return false; }
        return true;
    });
    if (seen) *seen = n;
    return found;
}

bool UnitPlace(uintptr_t unit, float* x, float* z) {
    UnitBytes u;
    return unit && x && z && Read(u.b, unit, sizeof u.b) && GridOf(u, x, z);
}

bool FindHireling(const uint32_t* classes, size_t count, HirelingView* out) {
    if (!out || !classes || !count) return false;
    bool found = false;
    EachUnit(1, [&](uintptr_t at, const UnitBytes& u) {
        const uint32_t mode = U32(u, 0xC), cls = U32(u, 4);
        if (mode == 0 || mode == 12) return true;   // dying, dead
        bool hireling = false;
        for (size_t i = 0; i < count; ++i) hireling = hireling || classes[i] == cls;
        if (!hireling) return true;
        HirelingView v;
        v.unit = at;
        v.id = U32(u, 8);
        v.cls = cls;
        if (!GridOf(u, &v.x, &v.z) || !RowNow(u.b[0x1BD], cls, &v.row)) return true;
        *out = v;
        found = true;
        return false;
    });
    return found;
}

bool HirelingPlace(uintptr_t unit, uint32_t id, float* x, float* z) {
    UnitBytes u;
    if (!unit || !x || !z || !Read(u.b, unit, sizeof u.b) || U32(u, 0) != 1 || U32(u, 8) != id) return false;
    const uint32_t mode = U32(u, 0xC);
    return mode != 0 && mode != 12 && GridOf(u, x, z);
}

size_t OwnBlades(OwnBlade out[2], uint64_t maxAgeMs) {
    if (!out) return 0;
    const ULONGLONG now = GetTickCount64();
    std::lock_guard<std::mutex> lock(g_ownBladesMutex);
    if (!g_ownBladesAt || now < g_ownBladesAt || now - g_ownBladesAt > maxAgeMs) return 0;
    for (size_t i = 0; i < g_ownBladesN; ++i) out[i] = g_ownBlades[i];
    return g_ownBladesN;
}

size_t PlayersNow(PlayerSpot* out, size_t max) {
    if (!out || !max) return 0;
    size_t n = 0;
    EachUnit(0, [&](uintptr_t, const UnitBytes& u) {
        PlayerSpot s;
        s.id = U32(u, 8);
        if (s.id && GridOf(u, &s.x, &s.z)) out[n++] = s;
        return n < max;
    });
    return n;
}

size_t ThrowTargets(ThrowMonster* out, size_t max, bool* polled) {
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG at = g_thrFreshAt.load();
    if (polled) *polled = at && now - at < 500;
    if (!out || !max || !at || now - at > 500) return 0;
    std::array<ThrowMon, 48> snap;
    size_t n;
    {
        std::lock_guard<std::mutex> lock(g_thrMutex);
        snap = g_thr;
        n = g_thrN;
    }
    size_t k = 0;
    for (size_t i = 0; i < n && k < max; ++i)
        if (Verdicted(snap[i].id, now) == Know::Yes) out[k++] = {snap[i].id, snap[i].x, snap[i].z};
    return k;
}

size_t PolledMonsters(PolledMonster* out, size_t max, bool* polled) {
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG at = g_thrFreshAt.load();
    if (polled) *polled = at && now - at < 500;
    if (!out || !max || !at || now - at > 500) return 0;
    std::array<ThrowMon, 48> snap;
    size_t n;
    {
        std::lock_guard<std::mutex> lock(g_thrMutex);
        snap = g_thr;
        n = g_thrN;
    }
    size_t k = 0;
    for (size_t i = 0; i < n && k < max; ++i) {
        PolledMonster& m = out[k++];
        m.id = snap[i].id;
        m.x = snap[i].x;
        m.z = snap[i].z;
        m.row = snap[i].row;
        m.rowOk = snap[i].rowOk;
        m.hostile = Verdicted(snap[i].id, now) == Know::Yes;
    }
    return k;
}

// [weight] grab_items (vr/item_grab.h): the items lying on the ground near the hero - the client's hash, type 4, mode 3 (on the ground; 5, still dropping, is left
// out) - each at its static path's cell (+0x38 -> +0x10 / +0x14, as the floor labels read it: the cell's middle, 2 * cell + 1 world units), on the hero's floor, in
// render metres (RenderGround: elevated ground sheared as the renderer does). Read only, vrcam's timer.
namespace {
std::atomic<bool> g_itemsWanted{false};
std::mutex g_itemsMutex;
itemgrab::Item g_items[itemgrab::kMaxItems];
int g_itemsN = 0;
ULONGLONG g_itemsAt = 0;
}  // namespace
void SetItemsWanted(bool on) {
    if (g_itemsWanted.exchange(on) != on)
        Log(on ? "melee touch: ground items on ([weight] grab_items: an empty hand's grip on an item lying on the ground picks it up through the game's own pick-up)"
               : "melee touch: ground items off");
}
void ItemsTick(bool ok, const float heroRender[3], float upm) {
    if (!g_itemsWanted.load() || !ok || !viewgate::F4() || !heroRender || !std::isfinite(heroRender[0] + heroRender[1] + heroRender[2])) return;
    const ULONGLONG now = GetTickCount64();
    if (now - g_itemsAt < 50 && now >= g_itemsAt) return;
    upm = std::clamp(upm, 1.0f, 12.0f);
    const stroke::Vec grid = GridPlane({heroRender[0], heroRender[1], heroRender[2]});
    const float floorM = heroRender[1] / upm, reach = 3.0f * upm;   // world units: the items within 3 m of the hero
    itemgrab::Item found[itemgrab::kMaxItems];
    int n = 0;
    EachUnit(4, [&](uintptr_t at, const UnitBytes& u) {
        if (U32(u, 0xC) != 3) return true;   // not on the ground
        const uintptr_t path = U64(u, 0x38);
        uint32_t cell[2] = {};
        if (path < 0x10000 || !Read(cell, path + 0x10, sizeof cell)) return true;
        const float x = 2.0f * (float)cell[0] + 1.0f, z = 2.0f * (float)cell[1] + 1.0f;
        if ((x - grid.x) * (x - grid.x) + (z - grid.z) * (z - grid.z) > reach * reach) return true;
        if (n >= itemgrab::kMaxItems) return false;
        itemgrab::Item& it = found[n++];
        it.id = U32(u, 8);
        it.cls = U32(u, 4);
        it.unit = at;
        const stroke::Vec g = RenderGround({x / upm, 0.0f, z / upm}, floorM);
        it.p[0] = g.x; it.p[1] = g.y; it.p[2] = g.z;
        return true;
    });
    std::lock_guard<std::mutex> lock(g_itemsMutex);
    for (int i = 0; i < n; ++i) g_items[i] = found[i];
    g_itemsN = n;
    g_itemsAt = now;
}
// [gameplay] melee_touch_barrels
void SetBarrelsWanted(bool on) {
    if (g_barrelsWanted.exchange(on) != on)
        Log(on ? "melee touch: barrels on ([gameplay] melee_touch_barrels: a swing of the held weapon through a barrel, pot, urn or basket presses the game's own "
                 "interact on it - the hero breaks it his own way, as with A; F4, with hit by touch)"
               : "melee touch: barrels off");
}
void BarrelsStep() {
    const bool on = g_barrelsWanted.load() && viewgate::F4();
    if (!g_barrelPress.Pressing()) {
        const unsigned ev = g_barrelPress.Step((double)GetTickCount64() / 1000.0, 0);   // (a press asked and ended in the same step is not left untold)
        (void)ev;
        if (g_barrelTarget.load()) PublishBarrelPress();
        return;
    }
    const barreltouch::Barrel b = g_barrelPress.Object();
    const int side = g_barrelPress.Side();
    if (!on) {
        g_barrelPress.Reset();
        PublishBarrelPress();
        LogF("melee touch: barrels - the press on object %u ends (the option off or not F4)", b.id);
        return;
    }
    int mode = -1;   // gone from the client's list: broken (an exploding barrel), or out of reach
    if (const uintptr_t u = ClientUnit(b.id, 2)) {
        UnitBytes ub;
        if (Read(ub.b, u, sizeof ub.b)) mode = (int)U32(ub, 0xC);
    }
    const double now = (double)GetTickCount64() / 1000.0;
    const unsigned ev = g_barrelPress.Step(now, mode);
    static unsigned told = 0;
    const bool tell = ++told <= 60 || told % 50 == 0;   // (the first 60 presses' lines, then every 50th)
    if ((ev & barreltouch::kAsked) && tell)
        LogF("melee touch: barrels - the %s hand's swing went through a %s (object %u, class %u, its footprint %dx%d): the game's interact pressed on it",
             side ? "left" : "right", barreltouch::KindName(b.op), b.id, b.cls, b.sizeX, b.sizeY);
    if ((ev & barreltouch::kBroken) && tell)
        LogF("melee touch: barrels - object %u %s after %.2f s: the press let go", b.id, mode < 0 ? "gone from the list" : "broken (its mode left 0)", now - g_barrelPress.Since());
    if ((ev & barreltouch::kNotBroken) && tell)
        LogF("melee touch: barrels - the game did not break object %u in %.1f s (out of the hero's reach? another action in the way?): the press let go", b.id, barreltouch::kHoldS);
    PublishBarrelPress();
}
uintptr_t BarrelPressTarget() {
    const ULONGLONG now = GetTickCount64(), at = g_barrelTargetAt.load();
    if (!g_barrelsWanted.load() || now < at || now - at > 300) return 0;   // (the timer stopped: no press held for ever)
    return g_barrelTarget.load();
}

int GroundItems(itemgrab::Item* out, int max, bool* fresh) {
    std::lock_guard<std::mutex> lock(g_itemsMutex);
    const ULONGLONG now = GetTickCount64();
    if (fresh) *fresh = g_itemsWanted.load() && g_itemsAt && now >= g_itemsAt && now - g_itemsAt <= 200;
    const int n = out ? std::min(max, g_itemsN) : 0;
    for (int i = 0; i < n; ++i) out[i] = g_items[i];
    return n;
}

}  // namespace meleetouch
