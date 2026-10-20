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

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
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
#include "thrown_touch.h"
#include "touch_why.h"
#include "unit_scan.h"
#include "melee_bodies.h"
#include "melee_boxes.h"
#include "melee_direct.h"
#include "melee_press.h"
#include "room_pose.h"
#include "melee_touch.h"
#include "view_gate.h"
#include "weight_drive.h"
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
std::atomic<ULONGLONG> g_liveAt{0};   // the last time Tick had a weapon it swings (melee, bare hands, a throwing weapon) posed and striking: GetTickCount64
std::atomic<uint32_t> g_liveKind{0};  // ... and its D2RVR_WeaponType
std::atomic<bool> g_throwWanted{false};   // [hands] throw_autoaim_deg: the monsters near the hero are wanted for a throw's auto-aim
// [gameplay] melee_touch_thrown: a javelin, throwing knife or throwing axe is swung too (a point-blank throw by the game's own attack), and what
// the touch needs of the throw by gesture (vr/throw_gesture_wire.h), by function pointers so this module does not depend on it (vrcam wires them).
std::atomic<bool> g_thrownTouch{true};
std::atomic<GripIsThrowsFn> g_gripIsThrows{nullptr};
std::atomic<GripHeldFn> g_gripHeld{nullptr};
std::atomic<LastThrowFn> g_lastThrow{nullptr};
std::atomic<HoldOffFn> g_holdOff{nullptr};

void Log(const char* text) {
    frametrace::Scope trace(frametrace::kLog);   // [debug] frame_trace: what the host's log costs the writer
    if (g_ctx) logasync::Info(g_ctx, text);
}
template <class... A> void LogF(const char* fmt, A... a) {
    char b[320];
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

std::mutex g_histMutex;
stroke::PoseHistory g_hist[2];   // 0 right, 1 left
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
    float d2 = 0.0f;       // squared distance to the hero on the grid, game units
    stroke::Body body;
};
struct Scan {
    std::array<Cand, 48> c;
    size_t n = 0;
    uint32_t heroId = 0;
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
    float best = 4.0f * 4.0f;   // the hero is the player within 4 game units of where his model stands
    EachUnit(0, [&](uintptr_t, const UnitBytes& u) {
        float x, z;
        if (!GridOf(u, &x, &z)) return true;
        const float d2 = (x - heroX) * (x - heroX) + (z - heroZ) * (z - heroZ);
        if (d2 < best) { best = d2; out->heroId = U32(u, 8); }
        return true;
    });
    if (!out->heroId) return false;
    unitscan::Near found[std::tuple_size<decltype(Scan::c)>::value];   // the monsters within 24 game units: unit_scan.h, the walk the shield bash shares
    const size_t n = MonstersNear(heroX, heroZ, 24.0f, upm, floorM, found, std::tuple_size<decltype(Scan::c)>::value);
    for (size_t i = 0; i < n; ++i) {
        out->c[i].id = found[i].id;
        out->c[i].cls = found[i].cls;
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

// ---- the server's verdicts: hostile? is a wall between? ----------------------------------------------------------------------
// Asked ahead (every 100 ms, on the game's thread, read-only) for the monsters near the hero, so a touch finds its answer
// ready and the press goes out in the frame of the contact.

struct Verdict { uint32_t id = 0; bool hostile = false, clear = false; ULONGLONG at = 0; };
std::mutex g_verdictMutex;
std::array<Verdict, 64> g_verdicts;
std::array<uint32_t, 48> g_probeIds;
size_t g_probeCount = 0;
std::atomic<uintptr_t> g_serverGame{0};
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
    if (type == 0 && unit && (id == g_heroId.load(std::memory_order_relaxed) || (statusHero && id == statusHero)) && game != g_serverGame.load(std::memory_order_relaxed)) {
        uintptr_t back = 0;
        uint32_t head[3];
        if (Read(head, unit, sizeof head) && head[0] == 0 && head[2] == id && Read(&back, unit + 0xD8, sizeof back) && back == game)
            g_serverGame.store(game);
    }
    return unit;
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

enum class Know { Unknown, No, Yes };
Know Verdicted(uint32_t id, ULONGLONG now) {
    if (g_unchecked.load()) return Know::Yes;
    std::lock_guard<std::mutex> lock(g_verdictMutex);
    for (const Verdict& v : g_verdicts)
        if (v.id == id) return now - v.at > 700 ? Know::Unknown : v.hostile && v.clear ? Know::Yes : Know::No;
    return Know::Unknown;
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
// Per hand (the timer's thread only): how far the head has gone in the stroke that is on (the amplitude), and its way over the last few poses (the debug's look of the blade).
strikedir::StrokeTravel g_travel[2];
strikedir::TipWay g_tipWay[2];
std::atomic<bool> g_tuneFlip{false};

// The blade's shape for the item in a hand as the touch tests it NOW: the kind's and the base item's constants, the general sliders on top, and for a
// thrown kind its own two on top of those.
bladeshape::Shape TunedShape(uint32_t code, uint32_t kind, bool twoHandsOn) {
    const bladeshape::Tuning tune{g_tuneLength.load(), g_tuneRadius.load(), g_tuneFlip.load(), g_tuneButt.load()};
    bladeshape::Shape s = bladeshape::Tuned(bladeshape::ForItem(code, kind, twoHandsOn), tune);
    if (ThrownKind(kind)) s = bladeshape::TunedThrown(s, {g_thrownLength.load(), g_thrownButt.load()});
    return s;
}
uint32_t g_lastPoseStamp = 0;
ULONGLONG g_lastScanAt = 0, g_lastReportAt = 0, g_lastToldAt = 0;
Scan g_scan;
struct Counters { unsigned sweeps = 0, touched = 0, pressed = 0, notHostile = 0, noVerdict = 0, noHero = 0, noHand = 0, slowPart = 0, away = 0, notHead = 0, small = 0; } g_n;

void Cancel() {
    g_engine[0].Reset();
    g_engine[1].Reset();
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
void Approve(uint32_t id, bool allowDirect, int hand) {
    frametrace::Note(frametrace::kPress);   // [debug] frame_trace: a touch was accepted - the game's attack is asked for (key or direct)
    const ULONGLONG now = GetTickCount64();
    // [gameplay] melee_direct (the user's experimental option, vr/melee_direct.h): the touch calls the game's attack itself.
    // Off, or not possible (no local server, a signature missing, a fault): the route is Key and nothing below changes.
    // Never for a thrown weapon (allowDirect false): the server's basic attack is a melee blow, not the throw the game's own attack makes of it.
    if (allowDirect && meleedirect::Wanted()) {
        const meleedirect::Route route = meleedirect::Plan(now, g_serverGame.load() != 0 && g_heroId.load() != 0);
        if (route == meleedirect::Route::Skip) return;
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
    stroke::Sample s;
    bool ball = false;          // [flail] chain: the blade is the flail's ball on its chain (skel::PosedBlade::ball)
};

// [debug] melee_boxes: the log line of a refused contact, throttled: a line each 250 ms for each of the reasons (0 not toward the monster, 1 not the head, 2 a swing too small, 3 too slow where it
// touched, 4 a movement that never became a stroke). `always`: told whatever the debug says (the first dozen, for the log of the first test). False: not now.
bool TellRefusal(int reason, ULONGLONG now, bool always) {
    static ULONGLONG at[5] = {};
    if (reason < 0 || reason > 4) return false;
    if (!always && (!meleeboxes::Wanted() || now - at[reason] < 250)) return false;
    at[reason] = now;
    return true;
}

// [debug] melee_boxes: a contact's marks - a ball on the touching point, an arrow along its velocity, a line to the nearest point of the monster's axis, and for a refusal the monster's capsule
// tinted - in the verdict's colour (meleeboxes::Colour). Read-only drawing.
void MarkContact(const strikedir::Judgement& jd, uint32_t monsterId, const stroke::Body& body, int colour, bool tint, ULONGLONG now) {
    if (!meleeboxes::Wanted() || !jd.haveWay) return;
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
            if (g_engine[side].Counting() || g_engine[side].LastSpeed() < 0.3f) continue;   // (counted - or judged in Tick - or nobody swung into it)
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
    g_threads = Query<ThreadService>(ctx);
    if (!g_threads || !g_threads->runOnGameThread) Log("melee touch: no game-thread service - the hostility check cannot run, nothing will be pressed");
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
void SetUnchecked(bool on) { g_unchecked.store(on); }
void SetAnimNote(AnimNoteFn fn) { g_animNote.store(fn); }
void SetPressTrace(PressNoteFn press, PressOutcomeFn outcome) {
    g_pressOutcome.store(outcome);
    g_pressNote.store(press);
}
void SetThrownTouch(bool on) {
    if (g_thrownTouch.exchange(on) != on && g_wanted.load()) Log(on ? "melee touch: thrown weapons (javelin, throwing knife and axe) hit by touch too" : "melee touch: thrown weapons are not swung");
}
bool ThrownTouch() { return g_thrownTouch.load(); }
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
void SetStrikeRule(const strikedir::Rule& rule) {
    const strikedir::Rule r = strikedir::Clamp(rule);
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(g_ruleMutex);
        changed = g_strikeRule.toward != r.toward || g_strikeRule.towardKinds != r.towardKinds || g_strikeRule.towardDeg != r.towardDeg || g_strikeRule.head != r.head ||
                  g_strikeRule.headKinds != r.headKinds || g_strikeRule.headPct != r.headPct || g_strikeRule.amplitude != r.amplitude || g_strikeRule.pierceCm != r.pierceCm ||
                  g_strikeRule.edgedX != r.edgedX || g_strikeRule.bluntX != r.bluntX || g_strikeRule.staffX != r.staffX || g_strikeRule.kindX != r.kindX;
        g_strikeRule = r;
    }
    if (!changed || !g_wanted.load()) return;
    char kinds[200] = "";
    for (int k = 0, w = 0; k < strikedir::kKinds && w < 170; ++k)
        w += snprintf(kinds + w, sizeof kinds - static_cast<size_t>(w), "%s%s %.0f", w ? ", " : "", strikedir::KindName(k), strikedir::AmplitudeCm(r, k));
    LogF("melee touch: the strike rules - toward the monster %s (within %.0f deg of the direction to its axis), the head only %s (the outer %.0f%% of the blade), the swing's amplitude %s; least cm: %s",
         r.toward ? "on" : "off", r.towardDeg, r.head ? "on" : "off", r.headPct, r.amplitude ? "on" : "off", r.amplitude ? kinds : "-");
}
void SetKindTuning(int kind, float minSpeed, float startCm) {
    if (kind < 0 || kind >= strikedir::kKinds) return;
    const float s = std::isfinite(minSpeed) && minSpeed >= stroke::kMinSpeedFloor ? std::min(minSpeed, stroke::kMinSpeedCeil) : 0.0f, c = std::isfinite(startCm) && startCm >= 2.0f ? std::min(startCm, 30.0f) : 0.0f;
    g_kindSpeed[kind].store(s);
    g_kindStart[kind].store(c);
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
BladeExtent BladeFor(uint32_t code, uint32_t type, bool twoHandsOn) {
    // A thrown weapon has a blade of its own only while the touch swings it (hit by touch and melee_touch_thrown on): [weight]'s shaft follows it
    // then. Otherwise it keeps the shaft it always had (the kind-less default, a sword's).
    if (ThrownKind(type) && !(g_wanted.load() && g_thrownTouch.load())) type = D2RVR_TYPE_SWORD;
    const bladeshape::Shape s = TunedShape(code, type, twoHandsOn);
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
    }
}

void Tick(const Frame& f) {
    const ULONGLONG now = GetTickCount64();
    ReleasePress(now, false);
    LatencyPoll(now);
    doubleswing::Tick();   // [hands] double_swing_by_hands: the hero's mana is read for its decisions (cheap when off)
    meleearm::Tick();      // [gameplay] melee_skill_armed: the same, while a skill is armed
    if (g_wanted.load()) meleedirect::Install();   // only while hit by touch and [gameplay] melee_direct are both on
    // F4 only (vr/view_gate.h). f.ok is vrcam's - a game area, first person on the controllers, no panel - and the view test is
    // here too, so the module does not depend on the caller's being right: another view = not ok, and what a touch had under way
    // ends at once (the press up, the window the game's lookups are answered in closed, a Direct hit request still queued dropped).
    const bool view = viewgate::F4();
    if (!view) {
        g_press.CloseWindow();
        if (g_wanted.load()) meleedirect::Flush();
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
    if (!drive.strikes) { skip(meleetouch::Why::LetGo); Cancel(); return; }
    if (thrown) {   // the touch and the throw by gesture must not both fire for one swing (vr/thrown_touch.h)
        const GripIsThrowsFn owns = g_gripIsThrows.load();
        const GripHeldFn held = g_gripHeld.load();
        const LastThrowFn last = g_lastThrow.load();
        meleetouch::ThrownState st;
        st.gripOwns = owns && owns();
        st.gripHeld = held ? held() : true;   // the gesture's own reading of the squeeze: the shared block's grip is 0 while the gesture owns it
        st.nowMs = now;
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
            const int hand = k == 0 ? drive.hand : side;     // the controller that swings it ([weight]: the hand that carries it)
            const skel::PosedBlade& pb = pose.blade[side];
            if (!pb.ok) continue;
            const uint32_t kind = k == 0 ? f.weaponType : f.kind[side];
            const bool two = k == 0 && f.twoHandsOn;
            // [flail] chain: a flail's ball on its chain strikes, not its handle - a ball as thick as the model's, where the chain has it
            const bool ball = pb.ball && head.ok;
            const bladeshape::Shape shape = ball ? bladeshape::BallShape(pb.ballRadius) : TunedShape(f.code[side], kind, two);   // (the kind's, the general sliders, a thrown kind's own on top)
            SideBlade& sb = blades[nb];
            bladeshape::RoomFrame roomFrame;
            bool haveRoomFrame = false;
            {
                std::lock_guard<std::mutex> lock(g_histMutex);
                if (!g_hist[hand].Find(pose.poseStamp, Seconds(D2RVRStampNow()), sb.s)) { ++g_n.noHand; g_engine[hand].Reset(); continue; }
                for (const RoomFrameAt& fr : g_frames)
                    if (fr.set && fr.stamp == pose.poseStamp) { roomFrame = fr.frame; haveRoomFrame = true; break; }
            }
            if (head.ok) {
                bladeshape::Bone bone;
                for (int i = 0; i < 3; ++i) { bone.origin[i] = ball ? pb.ballCentre[i] : pb.origin[i]; bone.axisY[i] = ball ? pb.ballDir[i] : pb.axis[1][i]; }
                sb.s.blade = bladeshape::FromBone(head, bone, shape);
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
            used[hand] = true;
            ++nb;
        }
        for (int side = 0; side < 2; ++side) if (!used[side]) g_engine[side].Reset();   // a hand with no blade this pose starts afresh
        if (!nb) { skip(meleetouch::Why::NoBlade); return; }
        {   // what blades there are, once per change (for the log of the first test: which hand, which kind, which way the tip lies, how long)
            static uint32_t toldKey = 0;
            uint32_t key = head.ok ? 2166136261u : 1u;
            for (const SideBlade& sb : blades)
                if (sb.ok) for (const uint32_t v : {static_cast<uint32_t>(sb.side), sb.kind, sb.code, static_cast<uint32_t>(sb.shape.tipSign + 2), static_cast<uint32_t>(sb.ball),
                                                     static_cast<uint32_t>(sb.shape.length * 1000.0f), static_cast<uint32_t>(sb.shape.butt * 1000.0f),
                                                     static_cast<uint32_t>(sb.shape.radius * 10000.0f)}) { key ^= v; key *= 16777619u; }
            if (key != toldKey) {
                toldKey = key;
                char b[600];
                int w = snprintf(b, sizeof b, "melee touch: blades (%s) -", head.ok ? "weapon bone as drawn" : "NO VIEW YET - the hero model's matrix");
                for (const SideBlade& sb : blades)
                    if (sb.ok && w > 0 && static_cast<size_t>(w) < sizeof b)
                        w += snprintf(b + w, sizeof b - static_cast<size_t>(w), " %c hand item '%c%c%c%c' kind %u: tip %.2f m and butt %.2f m from the bone, r %.1f cm, tip along the bone's %cY%s;",
                                      sb.side ? 'L' : 'R', sb.code ? static_cast<char>(sb.code & 255) : '?', static_cast<char>((sb.code >> 8) & 255) ? static_cast<char>((sb.code >> 8) & 255) : ' ',
                                      static_cast<char>((sb.code >> 16) & 255) ? static_cast<char>((sb.code >> 16) & 255) : ' ', static_cast<char>((sb.code >> 24) & 255) ? static_cast<char>((sb.code >> 24) & 255) : ' ',
                                      sb.kind, sb.shape.length, sb.shape.butt, sb.shape.radius * 100.0f, sb.shape.tipSign < 0 ? '-' : '+',
                                      sb.ball ? " - the flail's ball on its chain (the 'bone' is the ball's centre), not the handle" : "");
                Log(b);
            }
        }
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
    // The strike rules (vr/strike_direction.h): toward the monster, the head, the amplitude - per kind
    const strikedir::Rule rule = CurrentRule();
    if (now - g_lastScanAt >= 100) {
        g_lastScanAt = now;
        if (ScanUnits(grid.x, grid.z, upm, heroY, &g_scan)) {
            g_heroId.store(g_scan.heroId);
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
        if (g_probeCount && !g_unchecked.load() && g_threads && g_threads->runOnGameThread && g_serverGame.load()) {
            if (const uint32_t token = g_probeTicket.Start(now))
                if (g_threads->runOnGameThread(g_ctx, &Probe, uiticket::ToUser(token)) != Threads::Result::Success) g_probeTicket.Claim(token);
        }
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
    };
    Pending pend[2];
    int npend = 0;
    for (const SideBlade& sb : blades) {
        if (!sb.ok) continue;
        const int side = sb.side;
        stroke::Sweep sweep;
        const bool swept = g_engine[side].Step(sb.s, sweep);
        // the head's travel in the stroke that is on (the amplitude), and the tip's way over the last few poses (the debug's look) - every pose, swept or not. The travel is measured in the room
        // frame like the engine's speed (the hero walking adds nothing), the way in the world frame the monsters are in.
        g_travel[side].Feed(sb.s.time, sb.s.haveRoom ? sb.s.room.tip : sb.s.blade.tip, g_engine[side].StrokeId());
        g_tipWay[side].Feed(sb.s.time, sb.s.blade.tip);
        if (!swept) continue;
        ++g_n.sweeps;
        frametrace::Note(frametrace::kSweep);   // [debug] frame_trace: the blade moved fast enough to be swept
        g_sweepSpeed[side] = sweep.speed;   // (for the [debug] melee_boxes log)
        g_sweepAt[side] = now;
        if (!haveFresh) { haveFresh = true; freshOk = ScanUnits(grid.x, grid.z, upm, heroY, &fresh); }
        if (!freshOk) { ++g_n.noHero; continue; }
        const Cand* first = nullptr;
        bool touchedAny = false;   // [flail] chain: the blade met a body in this sweep (counted as an attack or not): the flail's head is kicked back
        float firstT = 2.0f, firstSpeed = 0.0f;
        strikedir::Judgement firstJd;
        const float least = g_engine[side].GetParams().strokeSpeed;
        for (size_t i = 0; i < fresh.n; ++i) {
            const Cand& c = fresh.c[i];
            const stroke::Contact touch = stroke::TouchOf(sweep, c.body);
            const float t = touch.t;
            if (t >= 0.0f) touchedAny = true;
            if (t < 0.0f || t >= firstT || g_hits[side].Contains(f.session, sweep.stroke, c.id)) continue;
            // The speed that counts is the blade's where it TOUCHES (the two ends' velocities blended at the contact point, stroke::ContactSpeed): the
            // tip of a blade that swings about the hand is fast, its grip end is not. A touch at a part moving slower than the least speed waits - the
            // monster is not used up: a later sweep of the stroke may touch it with a faster part.
            const float at = touch.speed;
            const int kindIdx = strikedir::KindIndex(sb.kind);
            // The strike rules' verdict on this contact (vr/strike_direction.h): toward the monster, the head, the amplitude - and where the touching point was, its way, the angle to the axis (for the
            // debug's marks and the log). A refused contact does not use the monster up: a later contact of the stroke, or the next stroke, counts. Only whether the contact counts as an attack - the
            // game's attack, damage and rolls are not touched.
            const strikedir::Judgement jd = strikedir::Judge(sb.ball ? strikedir::ForBall(rule) : rule, kindIdx, sweep, touch, c.body, g_travel[side].Meters());
            if (at < least) {
                ++g_n.slowPart;
                // [debug] melee_boxes: an amber ball (and the arrow, the line, the tint) where the blade met the monster too slowly, and a line (kind, speed, the way)
                MarkContact(jd, c.id, c.body, meleeboxes::kCAmber, true, now);
                if (TellRefusal(3, now, false))
                    LogF("melee touch: %c hand, a contact with monster %u is not counted - too slow: the %s moves %.1f m/s where it touched, the least is %.1f m/s%s", side ? 'L' : 'R', c.id,
                         strikedir::KindName(kindIdx), at, least, jd.haveWay ? (jd.dir.y >= 0.0f ? " (going up)" : " (going down)") : "");
                continue;
            }
            if (jd.why != strikedir::Why::Counts) {
                const int reason = jd.why == strikedir::Why::Away ? 0 : jd.why == strikedir::Why::NotHead ? 1 : 2;
                (reason == 0 ? g_n.away : reason == 1 ? g_n.notHead : g_n.small)++;
                const int colour = reason == 0 ? meleeboxes::kCBlue : reason == 1 ? meleeboxes::kCGrey : meleeboxes::kCAmber;
                MarkContact(jd, c.id, c.body, colour, true, now);   // [debug] melee_boxes: the marks, the monster tinted
                static unsigned told = 0;
                const bool firstOnes = told < 12;   // (the first twelve are told whatever the debug says; then, with [debug] melee_boxes, one each 250 ms for each reason)
                if (TellRefusal(reason, now, firstOnes)) {
                    if (firstOnes) ++told;
                    char detail[160];
                    if (reason == 0) snprintf(detail, sizeof detail, "its point moves %.0f deg from the direction to the monster's axis (it must be within %.0f)", jd.haveAxis ? jd.angleDeg : 0.0f, rule.towardDeg);
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
            if (g_hits[side].Insert(f.session, sweep.stroke, first->id) && npend < 2) pend[npend++] = {side, first, firstJd, firstSpeed, sweep.stroke, sweep.speed};
        }
    }

    // What each blade touched in this pass becomes the game's attack. A monster that both blades met is pressed once. [hands] double_swing_by_hands: with a weapon in each hand, the strike of the SECOND hand on
    // the monster the first hand struck within the window is the game's Double Swing (or Frenzy) instead of the attack key - vr/double_swing.h decides, the game does the swings. Two blades that met one monster
    // in one pass are the pair at once (the first only noted: the second says); everything else is as it was.
    const bool dualBlades = blades[0].ok && blades[1].ok && !thrown;
    for (int i = 0; i < npend; ++i) {
        const Pending& p = pend[i];
        if (p.c->id == approvedId) continue;   // (the other blade's contact with this monster was pressed in this pass)
        ++g_n.pressed;
        approvedId = p.c->id;
        MarkContact(p.jd, p.c->id, p.c->body, meleeboxes::kCGreen, false, now);   // [debug] melee_boxes: a counted contact: the ball, the arrow and the line in green
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
        else Approve(p.c->id, !thrown, p.side);
        if (thrown)   // one throw per swing: the throw by gesture's release of this swing is dropped (the touch's press is the throw)
            if (const HoldOffFn hold = g_holdOff.load()) hold(meleetouch::HoldOffUntil(now));
        if (g_n.pressed <= 20)
            LogF("melee touch: %c hand, stroke %llu hit monster %u (class %u) - the blade at %.1f m/s (%.1f where it touched) - %s pressed on it%s", p.side ? 'L' : 'R',
                 static_cast<unsigned long long>(p.stroke), p.c->id, p.c->cls, p.sweepSpeed, p.speed, dual.dual ? (dual.skillId == doubleswing::kDoubleSwing ? "the game's Double Swing" : "the game's Frenzy") : "the game's attack",
                 thrown ? " (a thrown weapon: a point-blank throw; the throw by gesture's release of this swing is held off)" : "");
    }

    // [debug] melee_boxes: AFTER the engine's steps, so the colour and the numbers are those of this pose's step (read-only)
    if (meleeboxes::Wanted()) FeedBoxes(now, blades, upm, heroY, grid);

    if (now - g_lastReportAt >= 10000) {
        g_lastReportAt = now;
        if (g_n.sweeps || g_n.touched || g_n.noHand) {
            LogF("melee touch: %u sweeps, %u touches, %u pressed, %u at a part of the blade moving slower than the least speed, %u not toward the monster, %u not the head, %u a swing too small (not counted), %u not hostile or behind a wall, %u without a verdict yet, %u without the hero, %u without a hand sample%s",
                 g_n.sweeps, g_n.touched, g_n.pressed, g_n.slowPart, g_n.away, g_n.notHead, g_n.small, g_n.notHostile, g_n.noVerdict, g_n.noHero, g_n.noHand,
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
    if ((!g_wanted.load() && !g_throwWanted.load() && !g_statusWanted.load()) || g_serverHook.load() == 1 || !g_ctx || GetTickCount64() < retryAt) return;
    retryAt = GetTickCount64() + (tries < 30 ? 2000 : 30000);   // the server's code is decrypted as it runs: tried again
    if (!d2rsig::Addr(RVA_LOOKUP) || !d2rsig::Addr(RVA_RELATION) || !d2rsig::Addr(RVA_LINE) || !d2rsig::Addr(RVA_UNITS) ||
        !d2rsig::Addr(RVA_FIND_UNIT) || !d2rsig::Addr(RVA_MONSTATS))
        return;   // not all found (yet): the game's code is decrypted as it runs
    const bool in = d2rsig::Hook(RVA_LOOKUP, kSigLookup, sizeof kSigLookup, reinterpret_cast<void*>(&HookLookup), reinterpret_cast<void**>(&g_origLookup));
    g_serverHook.store(in ? 1 : 2);
    if (in) Log("melee touch: the server's unit lookup is observed (to learn its game); the hostility check will run on the game's thread");
    else if (++tries == 30) Log("melee touch: the server's unit lookup could not be hooked (another build?) - no touch will be pressed unless [debug] melee_touch_unchecked=1");
}

// ---- a throw's auto-aim ([hands] throw_autoaim_deg): the monsters near the hero and the server's verdict on each ----------------
// The same read-only reads and the same ahead-of-time hostility check as the touch, for a thrown weapon in hand (the touch itself
// stands down for those): ThrowPoll about every 100 ms from vrcam's timer, ThrowTargets from the game's aim hooks at the release.

namespace {
struct ThrowMon { uint32_t id = 0; float x = 0.0f, z = 0.0f, d2 = 0.0f; };
std::mutex g_thrMutex;
std::array<ThrowMon, 48> g_thr;
size_t g_thrN = 0;
std::atomic<ULONGLONG> g_thrFreshAt{0};
ULONGLONG g_thrScanAt = 0;   // the timer's thread only
}  // namespace

void SetThrowWanted(bool on) { g_throwWanted.store(on); }
bool ThrowWanted() { return g_throwWanted.load(); }

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
    if (!g_throwWanted.load() || !viewgate::F4() || !heroUnit || !Read(hero.b, heroUnit, sizeof hero.b) || U32(hero, 0) != 0 || !GridOf(hero, &hx, &hz)) {
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
    if (n && !g_unchecked.load() && g_threads && g_threads->runOnGameThread && g_serverGame.load()) {
        if (const uint32_t token = g_probeTicket.Start(now))
            if (g_threads->runOnGameThread(g_ctx, &Probe, uiticket::ToUser(token)) != Threads::Result::Success) g_probeTicket.Claim(token);
    }
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

}  // namespace meleetouch