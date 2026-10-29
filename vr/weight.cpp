// weight.cpp - see weight.h.
//
// The weapon lives in the room (OpenXR stage axes, metres, y up): the controllers come in BodyWalk's
// frame (metres from the head, the head's turn only) and are put in the room by the head's place and
// turn from the same sample. Walking and turning with the sticks move the game's world, not the room,
// so they do not drag the weapon. One step per new head sample: both eyes of a stereo pair are posed
// from one sample (FreshHands hands over the pair's copy), so both see one weapon.

#include "weight.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "grip_zone.h"
#include "hero_strength.h"
#include "world_anchor.h"
#include "hold.h"
#include "doll_view.h"
#include "shoulder_draw.h"
#include "shoulder_grip.h"
#include "shoulder_swap.h"
#include "slide.h"
#include "slide_kinds.h"
#include "stab_debug.h"
#include "body_motion.h"
#include "strike_direction.h"
#include "parry.h"   // [weight] parry_feel: the knock's ease (Knock)
#include "clash_loud.h"   // [weight] clash_sound: how loud and how hard the knock of the two weapons is
#include "stick_strength.h"
#include "strike_measure.h"
#include "strike_force.h"   // [weight] cleave: the weapon's strike threshold the cleave's numbers are times
#include "weight_flight.h"
#include "weight_floor.h"
#include "weight_throw.h"

namespace weight {

#if D2RVR_FIRST_PERSON
namespace {

namespace hh = heft::hold;
using heft::Pose;
using heft::Q;
using heft::V3;

SRWLOCK g_lock = SRWLOCK_INIT;
hh::Hold g_hold;
uint64_t g_key = 0;                 // what it was placed for: the hands' items, the kind, the slot
bool g_haveSample = false;
uint32_t g_sample = 0;              // the head sample it was last stepped on
LARGE_INTEGER g_stepAt{};
hh::HandOut g_out[2];
StrengthNote g_strengthNote;         // what the hero's strength did on the last step (under g_lock)
uint64_t g_strKey = 0;               // ... the weapon and the steel factor it was last told for (< 0: told that the Strength was not read); forgotten with the weapon
double g_strSteel = -1.0;
float g_outHand[2][3] = {}, g_outRot[2][4] = {};   // that step's hands in BodyWalk's frame (the pair's second eye)
float g_outBody[3] = {}, g_outBodyRot[4] = {0, 0, 0, 1};   // and the body's frame
bool g_free = false;                // no hand held it at that step
hh::Line g_shaft;                   // measured off the pose (SetShaft, SetDrawnShaft)
uint64_t g_shaftKey = 0;
bool g_shaftNew = false;
double g_drawnButt = -0.1, g_drawnTip = 0.9;   // the wood's ends along it (SetDrawnShaft)
std::mutex g_knockMutex;   // [weight] parry_feel: the knock going on (Knock; read by Apply)
float g_knockDir[3] = {}, g_knockM = 0.0f;
int g_knockMs = 0;
uint64_t g_knockAt = 0;
std::atomic<float> g_handAhead[2] = {};         // [gameplay] melee_not_butt: each holding hand, metres ahead of the bone's origin along the shaft (HandOnShaft)
std::atomic<bool> g_handOn[2] = {};
bool g_drawnNew = false;
std::atomic<uint32_t> g_holding{0};   // bit per hand on the weapon
std::atomic<int> g_carrying{-1};      // the hand that carries it
std::atomic<unsigned> g_wielding{0};  // the hands that hold it or that it flies into, a bit each
std::atomic<bool> g_placed{false};    // a weapon held that way
std::atomic<float> g_steelKg{0.0f};   // [weight] knock_props: its steel on the last step
struct EndsOut { float butt[3], tip[3]; ULONGLONG at; };
EndsOut g_ends{};
std::atomic<LogFn> g_log{nullptr};
ThrowRelease g_throw;                              // a grip release that is a throw (vr/weight_throw.h); under g_lock
ThrownFly g_thrownFly;                             // a thrown weapon's visit, until it is done
bool g_thrownFlies = false;                        // ... and it is a potion whose model flies to its landing (wt.thrownPotion && wt.potionFlies, latched at the release)
EmptyHand g_emptyHand;                             // the hand stays empty while the game throws
std::atomic<HeroModeFn> g_heroMode{nullptr};
std::atomic<LibraryFn> g_libraryEnsure{nullptr};   // weightlib::Ensure (vrcam)
std::atomic<LibraryFn> g_gripEnsure{nullptr};      // griplib::Ensure (vrcam)
std::atomic<BodiesFn> g_bodiesSrc{nullptr};        // [weight] monster_bodies: meleetouch::Bodies (vrcam)
std::atomic<StabFn> g_stabSrc{nullptr};            // [weight] stab_in: meleetouch::StabbingNow (vrcam)
unsigned g_stabTold = 0;                           // [weight] stab_in: the log's lines of wounds so far (the first few dozen told)
std::atomic<StabNoteFn> g_stabNoteSink{nullptr};   // [debug] stab_debug: vrcam's
monsterbodies::View g_stabView;                    // ... the view the bodies came through (the room's points drawn back in the render world)
bool g_stabViewOk = false;
bodymotion::Tracker g_bodyMotion;                  // [gameplay] melee_thrust_with_run: the bodies' velocities in the room (the stab counts the hero's run)
uint64_t g_bodyStep = 0;                           // ... the weight steps that had bodies, counted
strikemeasure::Strikes g_strikeMeasure;            // [debug] strike_measure: the strikes on bodies, measured, each monster its own (vr/strike_measure.h)
// A weapon in each hand (Input::Weight::second): the other hand's weapon is a body of its own - one hand on it, never let go (the game holds both), its own mass,
// its own contacts with the bodies, the doll's push and the strike measure; the hand is drawn where it holds it (under g_lock). The headset 2026-10-23: «когда два
// оружия в двух руках, они проходят сквозь куклу, а когда одно оружие - оно сопротивляется».
hh::Hold g_hold2;
uint64_t g_key2 = 0;
hh::HandOut g_out2[2];
float g_out2Hand[3] = {}, g_out2Rot[4] = {0, 0, 0, 1};
float g_out2Body[3] = {}, g_out2BodyRot[4] = {0, 0, 0, 1};   // [weight] second_lets_go: its body's frame, for drawing it let go
bool g_free2 = false;                                         // ... no hand held it at that step
std::atomic<bool> g_secondLetGo{false};                       // (SecondLetGo)
std::atomic<int> g_secondHand{-1};                            // (SecondHand)
std::atomic<uint64_t> g_secondThrownAt{0};                    // [hands] throw_left_hand: the game threw the second weapon at this GetTickCount64 (SecondThrown), not seen yet
EmptyHand g_emptyHand2;                                       // ... its hand stays empty while the game throws, as the first's (g_emptyHand)
std::atomic<unsigned> g_holders[2] = {};                      // (HoldersOf) by the game's slot: the hands on that slot's weapon, a bit each (1 right, 2 left)
std::atomic<int> g_firstSlot{-1};                             // (FirstSlot)
std::atomic<bool> g_secondPlaced{false};                      // (SecondPlaced)
float g_out2HandOther[3] = {}, g_out2RotOther[4] = {0, 0, 0, 1};   // the hand-over: the first's hand where it holds the second
strikemeasure::Strikes g_strikeMeasure2;
// [weight] strike_by_force: each hold's strikes on now (MeasureStrikes, under g_lock), [0] the first weapon's, [1] the second's
LiveStrike g_live[2][strikemeasure::Strikes::kMax];
int g_nLive[2] = {};
ULONGLONG g_liveAt[2] = {};
std::atomic<unsigned> g_forceSlots{0};
uint32_t g_contactWas2 = 0;
ContactNote g_contactNote;                         // the last step's (under g_lock)
uint32_t g_contactWas = 0;                         // the body it touched on the step before (0 none): a pulse a strike, not a step
ULONGLONG g_contactPulseAt = 0, g_contactToldAt = 0;
std::array<int, monsterbodies::kMax> g_capBone{};   // the bone of each capsule handed to the hold this step (-1: a monster's own capsule)
std::array<int, monsterbodies::kMax> g_capHeld{};   // ... and what it is: 0 a body's, 1 a weapon the monster holds, 2 its shield ([weight] monster_shields)
// [weight] monster_shields: the monsters whose shield or weapon a held weapon was on (and not on their body) - hit by touch presses nothing on them then (Blocked)
struct BlockNote { std::atomic<uint32_t> id{0}; std::atomic<uint64_t> ms{0}; std::atomic<int> held{0}; };
BlockNote g_blocks[8], g_reached[8];   // ... and the monsters whose BODY it was on (Reached)
std::atomic<unsigned> g_blockAt{0}, g_reachAt{0};
void Note(BlockNote* table, std::atomic<unsigned>& next, uint32_t id, int held) {
    int slot = -1;
    for (int k = 0; k < 8 && slot < 0; ++k)
        if (table[k].id.load() == id) slot = k;
    if (slot < 0) slot = (int)(next.fetch_add(1) % 8u);
    table[slot].held.store(held);
    table[slot].ms.store(GetTickCount64());
    table[slot].id.store(id);
}
void NoteBlocks(const heft::contact::Report& c) {
    for (int i = 0; c.bodies > 0 && i < c.nMet; ++i) {
        const heft::contact::Report::Met& e = c.met[i];
        if (e.onBody) Note(g_reached, g_reachAt, e.id, 0);
        else if (e.onHeld) Note(g_blocks, g_blockAt, e.id, e.body >= 0 && e.body < monsterbodies::kMax && g_capHeld[e.body] ? g_capHeld[e.body] : 1);
    }
}
int Within(const BlockNote* table, uint32_t id, uint64_t withinMs) {
    if (!id) return 0;
    const uint64_t now = GetTickCount64();
    for (int k = 0; k < 8; ++k)
        if (table[k].id.load() == id) {
            const uint64_t at = table[k].ms.load();
            return now >= at && now - at <= withinMs ? std::max(1, table[k].held.load()) : 0;
        }
    return 0;
}
std::atomic<BodyPushFn> g_bodyPush{nullptr};        // [weight] monster_ragdoll: meleetouch::PushBody (vrcam)
monsterbodies::Snapshot g_capSnap;                  // ... the snapshot the step's capsules came from, the view they were taken through, and
monsterbodies::View g_capView;                      // which body of it each capsule is (the weight thread only)
std::array<int, monsterbodies::kMax> g_capOf{};
struct BodiesCost { double us = 0, worst = 0; unsigned steps = 0, strikes = 0; ULONGLONG since = 0; } g_bodiesCost;   // told once a minute while the bodies are on
std::atomic<ActionFrameFn> g_actionFrame{nullptr};
std::atomic<GripIsThrowsFn> g_gripIsThrows{nullptr};
std::atomic<LastThrowFn> g_lastThrow{nullptr};
std::atomic<ThrowSideFn> g_throwSide{nullptr};   // [hands] throw_left_hand: the hand whose grip the throw is (none: the right)
// [weight] to_back: where the skeleton drew the stowed weapon's body frame (told by SetBackBody; BodyWalk's frame), the bone's distance along
// the shaft from where the fist passes it (SetShaft), and what the swap gesture asks (BackStowed).
float g_backBody[3] = {}, g_backRot[4] = {0, 0, 0, 1};
bool g_backNew = false;
hh::Line g_shaft2;                                         // [weight] second_lets_go: the second weapon's shaft as drawn (SetDrawnShaft2), against its body
uint64_t g_shaft2Key = 0;
double g_drawn2Butt = -0.1, g_drawn2Tip = 0.9;
bool g_drawn2New = false;
float g_back2Body[3] = {}, g_back2Rot[4] = {0, 0, 0, 1};   // [weight] second_lets_go: the second weapon's place on the back (SetBackBody2)
bool g_back2New = false;
float g_boneAlong = 0.0f;
std::atomic<bool> g_stowed{false};
std::atomic<ULONGLONG> g_takenUntil{0};
std::atomic<uint32_t> g_takeZone[2] = {};   // per hand, from the last step: where a grip pressed now would take the weapon (hh::Hold::TakeZone): the zone in the low byte, the fist's distance in mm above
std::atomic<HapticFn> g_haptic{nullptr};
std::atomic<ClashSoundFn> g_clashSound{nullptr};
SRWLOCK g_cleaveLock = SRWLOCK_INIT;   // [weight] cleave: vrcam's timer writes, the weight thread reads
CleaveSettings g_cleave;
int g_cleaveSeen[2] = {}, g_cleaveTold = 0;
shouldergrip::ZoneTick g_zoneTick[2];   // a hand entering the take zone while a weapon is away (under g_lock)
std::atomic<const char*> g_unmeasured{nullptr};   // why the drawn shaft was not measured, once told (NoteShaftUnmeasured)
std::atomic<uint32_t> g_fitLogged[2] = {0xFFFFFFFFu, 0xFFFFFFFFu};   // the take each hand's grip fit was last logged for (NoteGripFit); reset with the weapon
bool g_gripWas[2] = {};                 // each grip as a button (0.6 / 0.4), for saying why a squeeze took nothing
constexpr int kTickMs = 30;
constexpr ULONGLONG kTakenLatchMs = 400;   // the press that took it from the back is not also a swap, whichever of the two sees it first

void LogF(const char* fmt, ...) {
    const LogFn fn = g_log.load();
    if (!fn) return;
    char b[300];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    fn(b);
}

Q Turn(V3 axis, float deg) { return heft::QFromAxisAngle(axis, deg * 0.0174532925199); }

// The head of this sample in the room: where the hands' frame stands and how it is turned.
struct Head { V3 pos; Q yaw; };
Head HeadOf(const skel::Input& in) {
    return {{in.headRoom[0], in.headRoom[1], in.headRoom[2]}, Turn({0, 1, 0}, in.headYawDeg)};
}
Pose ToRoom(const Head& h, const float p[3], const float q[4]) {
    const Q r = heft::QNorm({q[0], q[1], q[2], q[3]});
    return {heft::Add(h.pos, heft::Rotate(h.yaw, {p[0], p[1], p[2]})), heft::QNorm(heft::QMul(h.yaw, r))};
}
void FromRoom(const Head& h, const Pose& p, float out[3], float q[4]) {
    const Q inv = heft::QConj(h.yaw);
    const V3 v = heft::Rotate(inv, heft::Sub(p.pos, h.pos));
    const Q r = heft::QNorm(heft::QMul(inv, p.rot));
    out[0] = (float)v.x; out[1] = (float)v.y; out[2] = (float)v.z;
    q[0] = (float)r.x; q[1] = (float)r.y; q[2] = (float)r.z; q[3] = (float)r.w;
}

// The line through a closed fist (little finger to index) in the controller's frame: its -Z, turned
// by the [hands] wrist offsets as skeletons' PlanWrist turns the hand (yaw and roll mirrored on the left).
V3 FistLine(const skel::Input& in, int side) {
    if (!in.wrist) return {0, 0, -1};
    const float mir = side == 0 ? 1.0f : -1.0f;
    const Q w = heft::QMul(Turn({1, 0, 0}, in.wristDeg[0]), heft::QMul(Turn({0, 1, 0}, mir * in.wristDeg[1]), Turn({0, 0, 1}, mir * in.wristDeg[2])));
    return heft::Rotate(w, {0, 0, -1});
}

const char* HandName(int side) { return side == 0 ? "right" : "left"; }

// ---- The throw as the model works it out (weight::LastThrow, vr/weight_flight.h) ----
// The weapon's centre and velocity on the last steps (the room's axes), for the PEAK velocity of the 150 ms before a release: a swing's end-zone slowing,
// or a grip seen open a moment late, does not shorten the throw.
struct TrailSample { uint64_t t = 0; V3 com, vel; };
constexpr int kTrail = 32;
constexpr uint64_t kPeakWindowMs = 150;
TrailSample g_trail[kTrail];
int g_trailN = 0, g_trailAt = 0;
ThrowOutcome g_outcome;            // under g_lock
bool g_flightPending = false;      // a classified throw's model is to be put on the worked-out flight once it is out of the hands
uint64_t g_flightRelease = 0;
struct OutcomeRoom { V3 com, vel; Q yaw; } g_outRoom;   // the same throw in the room: where the model's own flight starts, and the head's turn it was worked out in

void TrailClear() { g_trailN = 0; g_trailAt = 0; }
void TrailPush(uint64_t t, const V3& com, const V3& vel) {
    g_trail[g_trailAt] = {t, com, vel};
    g_trailAt = (g_trailAt + 1) % kTrail;
    if (g_trailN < kTrail) ++g_trailN;
}
// The fastest sample of the window, and the latest centre. False with nothing in the window.
bool TrailPeak(uint64_t now, V3* com, V3* peak) {
    bool any = false;
    double best = -1.0;
    uint64_t latest = 0;
    for (int i = 0; i < g_trailN; ++i) {
        const TrailSample& s = g_trail[i];
        if (s.t > now || now - s.t > kPeakWindowMs) continue;
        const double sp = heft::Len(s.vel);
        if (sp > best) { best = sp; *peak = s.vel; }
        if (!any || s.t >= latest) { latest = s.t; *com = s.com; }
        any = true;
    }
    return any;
}

// Works the throw out from the trail: the head frame at this step, the floor (room y + the weapon's lie height), the reach and the cap. Fills g_outcome.
// [weight] stab_in: its sliders as the stab's numbers (cleanroom/heft/stab.h FromSliders).
heft::stab::Params StabParamsOf(const skel::Input::Weight& wt) {
    heft::stab::Sliders sl;
    sl.minSpeed = wt.stabMinSpeed;
    sl.maxAngleDeg = wt.stabMaxAngleDeg;
    sl.resistPct = wt.stabResistPct;
    sl.depthCm = wt.stabDepthCm;
    sl.tearCm = wt.stabTearCm;
    sl.unlockN = wt.stabUnlockN;
    return heft::stab::FromSliders(sl);
}

// [weight] stab_in: what the stab did this step - in the log (in and out, the first few dozen) and, with [debug] stab_debug, to vrcam's readout and marks
// (vr/stab_debug.h) with its points drawn back in the render world through the view the bodies came through.
void TellStab(const Head& head, const hh::Settings& s, bool debug) {
    const heft::stab::Report& st = g_hold.LastStab();
    if ((st.entered || st.left) && g_stabTold < 60) {
        ++g_stabTold;
        if (st.entered) LogF("vrcam: weight - stab: the point went into monster %u's body at %.1f m/s (%.1f cm in at once)", st.id, st.speed, st.depth * 100.0);
        else LogF("vrcam: weight - stab: out of monster %u's body, %.1f cm in at most - %s", st.id, st.deepest * 100.0, st.why);
    }
    const StabNoteFn sink = g_stabNoteSink.load();
    if (!debug || !sink || !(st.judged || st.in || st.left)) return;
    stabdbg::Note n;
    n.state = st.left ? stabdbg::State::Out : st.in ? (st.locked ? stabdbg::State::Locked : stabdbg::State::In) : stabdbg::State::Refused;
    std::snprintf(n.why, sizeof n.why, "%s", st.why);
    n.id = st.id;
    n.side = std::max(0, g_hold.Main()) & 1;
    n.atMs = GetTickCount64();
    const heft::stab::Params& p = s.stabParams;
    n.judged = st.judged;
    n.thrusting = st.thrusting;
    n.speed = (float)st.speed;
    n.minSpeed = (float)p.minSpeed;
    n.angleDeg = (float)(std::acos(std::clamp(st.cosInto, -1.0, 1.0)) * 57.29577951308232);
    n.maxAngleDeg = (float)(std::acos(std::clamp(p.angleCos, -1.0, 1.0)) * 57.29577951308232);
    n.fromTip = (float)st.fromTip;
    n.tipM = (float)p.tipM;
    n.depth = (float)st.depth;
    n.limit = (float)st.limit;
    n.deepest = (float)st.deepest;
    n.across = (float)st.pull;
    n.tearM = (float)p.breakM;
    n.forceN = (float)st.force;
    n.unlockN = (float)p.unlockForce;
    const auto toRender = [&](const V3& room, float out[3]) {
        float h[3], q[4];
        FromRoom(head, Pose{room, {}}, h, q);
        return g_stabViewOk && monsterbodies::HeadToRender(g_stabView, h, out);
    };
    const bool wound = st.in || st.left || st.entered;
    if (wound && heft::Len(st.axis) > 0.5) {
        float tip[3];
        n.have3d = toRender(st.entry, n.entry) && toRender(heft::Add(st.entry, st.axis), tip);
        for (int k = 0; k < 3; ++k) n.axis[k] = tip[k] - n.entry[k];
    }
    if (st.judged) n.havePoint = toRender(st.point, n.point);
    n.upm = g_stabView.upm;
    sink(n);
}

void WorkOutThrow(const skel::Input::Weight& wt, const Head& head, double floorY, double lie, double steelKg, uint64_t nowMs) {
    V3 com, peak;
    ThrowOutcome o;
    o.preview = true;
    o.tickMs = nowMs;
    o.massKg = (float)steelKg;
    if (TrailPeak(nowMs, &com, &peak)) {
        if (wt.handsModel == 1) {   // the weight-physics library's launch velocity from the last steps (oldest first), when it gives one
            if (const hh::Engine* engine = hh::GetEngine()) {
                uint64_t ts[kTrail];
                V3 vs[kTrail];
                int n = 0;
                const int first = (g_trailAt - g_trailN + kTrail) % kTrail;
                for (int k = 0; k < g_trailN; ++k) {
                    const TrailSample& sm = g_trail[(first + k) % kTrail];
                    if (sm.t > nowMs) continue;
                    ts[n] = sm.t;
                    vs[n] = sm.vel;
                    ++n;
                }
                V3 v2;
                if (engine->ThrowVelocity(ts, vs, n, nowMs, &v2)) peak = v2;
            }
        }
        const Q inv = heft::QConj(head.yaw);
        weightflight::In fi;
        const V3 p = heft::Rotate(inv, heft::Sub(com, head.pos)), v = heft::Rotate(inv, peak);
        fi.pos[0] = p.x; fi.pos[1] = p.y; fi.pos[2] = p.z;
        fi.vel[0] = v.x; fi.vel[1] = v.y; fi.vel[2] = v.z;
        fi.floorY = floorY + lie - head.pos.y;
        const double capM = weightflight::CapMetres(wt.throwMaxCells, wt.unitsPerMetre);
        const weightflight::Out r = weightflight::SimulateThrow(fi, wt.throwReachPct, capM, steelKg);
        if (r.valid) {
            o.valid = true;
            for (int i = 0; i < 3; ++i) { o.velocity[i] = (float)r.vel[i]; o.landing[i] = (float)r.landing[i]; }
            o.rawVelocity[0] = (float)v.x; o.rawVelocity[1] = (float)v.y; o.rawVelocity[2] = (float)v.z;
            o.speed = (float)r.speed;
            o.flightS = (float)r.flightS;
            o.elevationDeg = (float)r.elevationDeg;
            o.reachScale = (float)r.hScale;
            o.massFactor = (float)r.massFactor;
            o.capped = r.capped;
            g_outRoom = {com, heft::Rotate(head.yaw, {r.vel[0], r.vel[1], r.vel[2]}), head.yaw};
        }
    }
    g_outcome = o;
}

}  // namespace

// The room's axes against the game's world, of the last pose: a weapon that lies on the floor is carried by how they moved since (vr/world_anchor.h).
namespace {
worldanchor::Snap g_worldWas;
bool g_worldTold = false;
}  // namespace

void SetLog(LogFn fn) { g_log.store(fn); }
void SetThrowSource(GripIsThrowsFn gripIsThrows, LastThrowFn lastThrowTickMs) {
    g_gripIsThrows.store(gripIsThrows);
    g_lastThrow.store(lastThrowTickMs);
}
void SetThrowSide(ThrowSideFn side) { g_throwSide.store(side); }

// [weight] monster_ragdoll: the body a held weapon met takes the push back. The surface pushed the weapon out along its normal: the body is pushed the other way
// (room -> head's frame -> render world). A strike (the step it first meets this body; `was` the body it touched the step before) carries the weapon AND the arm
// behind it into the body: their momentum along the way in, (the steel + kStrikeArmKg) x the speed in - the contact's own impulse takes only what the weapon's step
// loses at the point, a few tenths of a newton-second (the first headset test: 0.2 - 2.5 N s at 1 - 7 m/s). While it rests on the body the steady push is the
// contact's - only while the weapon still goes INTO it (kPushInMs): a weapon resting in or on a body (the second test: "the capsules rattle") pushes nothing, so
// the body's sway does not fight the weapon it just moved.
void PushBack(const hh::Hold& h, const Head& head, uint32_t was, int nBodies) {
    constexpr double kStrikeArmKg = 3.5;   // a man's forearm and hand and part of his upper arm (an arm is about 5 % of him)
    constexpr double kPushInMs = 0.3;      // m/s into the body: slower is resting on it
    const BodyPushFn push = g_bodyPush.load();
    const heft::contact::Report& c = h.LastContact();
    if (!push || c.bodies <= 0 || c.body < 0 || c.body >= nBodies) return;
    if (c.body < monsterbodies::kMax && g_capHeld[c.body]) return;   // ([weight] monster_shields: a shield or a weapon it holds pushes no doll)
    const bool strike = c.id != was;
    const double j = c.approach < kPushInMs ? 0.0 : strike ? std::max(c.impulse, (h.SteelKg() + kStrikeArmKg) * c.approach) : c.impulse;
    if (!(j > 0.0)) return;
    const Q inv = heft::QConj(head.yaw);
    const V3 n = heft::Rotate(inv, c.normal), p = heft::Rotate(inv, heft::Sub(c.point, head.pos));
    const float nh[3] = {(float)(-n.x * j), (float)(-n.y * j), (float)(-n.z * j)}, ph[3] = {(float)p.x, (float)p.y, (float)p.z};
    float imp[3], at[3];
    dollview::HeadDirToRender(g_capView, nh, imp);
    dollview::HeadToRender(g_capView, ph, at);
    push(g_capSnap, g_capOf[c.body], imp, at);
}

// [debug] strike_measure: what each strike of a held weapon delivered into a body (vr/strike_measure.h; docs/notes/strike_force.md) - every monster it touched this
// step, each its own strike (a blade caught between two strikes both); a weapon in a wound, that monster's joint force. slot: the game's slot the weapon is in.
void MeasureStrikes(strikemeasure::Strikes& tracker, const hh::Hold& h, const hh::HandOut out[2], double t, double dt, int kind, int slot, bool log = true, int live = -1) {
    strikemeasure::Step ms[strikemeasure::Strikes::kMax];
    int nm = 0;
    const unsigned handMask = (out[0].holds ? 1u : 0u) | (out[1].holds ? 2u : 0u);
    const heft::contact::Report& c = h.LastContact();
    const auto add = [&](uint32_t id) -> strikemeasure::Step* {
        if (id == 0 || nm >= strikemeasure::Strikes::kMax) return nullptr;
        strikemeasure::Step& m = ms[nm++];
        m.t = t;
        m.dt = dt;
        m.id = id;
        m.handMask = handMask;
        m.hands = (handMask & 1u ? 1 : 0) + (handMask & 2u ? 1 : 0);
        return &m;
    };
    if (h.Stuck())
        if (strikemeasure::Step* m = add(h.LastStab().id)) m->stabForce = h.LastStab().force;
    for (int i = 0; c.bodies > 0 && i < c.nMet; ++i) {
        const heft::contact::Report::Met& e = c.met[i];
        if (!e.onBody) continue;   // ([weight] monster_shields: on its shield or its weapon only - nothing into its body)
        strikemeasure::Step* m = nullptr;
        for (int j = 0; j < nm; ++j)
            if (ms[j].id == e.id) m = &ms[j];
        if (!m) m = add(e.id);
        if (!m) continue;
        m->bone = e.body >= 0 && e.body < monsterbodies::kMax ? g_capBone[e.body] : -1;
        m->impulse = e.impulse;
        m->friction = e.friction;
        m->slid = e.slid;
        m->push = e.push;
        m->approach = e.approach;
        if (e.id == c.id && c.blade >= 0 && c.blade < h.WeaponShape().nPieces) {   // (where along the weapon it met the hardest one: the head rule of strike_by_force)
            const hh::Shape& sh = h.WeaponShape();
            const heft::Piece& pc = sh.pieces[c.blade];
            const double y = pc.a.y + (pc.b.y - pc.a.y) * std::clamp(c.along, 0.0, 1.0), len = sh.tip - sh.butt;
            if (len > 1e-3) m->along = (float)std::clamp((y - sh.butt) / len, 0.0, 1.0);
        }
    }
    for (int j = 0; j < nm; ++j) ms[j].crowd = nm;
    strikemeasure::Strike ended[strikemeasure::Strikes::kMax];
    const int ne = tracker.Feed(ms, nm, t, dt, ended);
    if (live == 0 || live == 1) {   // [weight] strike_by_force: the strikes on now, for hit by touch (LiveStrikes)
        strikemeasure::Strike on[strikemeasure::Strikes::kMax];
        const int n = tracker.Live(on, strikemeasure::Strikes::kMax);
        for (int j = 0; j < n; ++j) g_live[live][j] = {on[j], slot, kind, (float)h.SteelKg()};
        g_nLive[live] = n;
        g_liveAt[live] = GetTickCount64();
    }
    for (int j = 0; log && j < ne; ++j) {
        const strikemeasure::Strike& k = ended[j];
        const char* hands = k.handMask == 3u ? "both hands" : k.handMask == 1u ? "the right hand" : k.handMask == 2u ? "the left hand" : "no hand";
        char crowd[40] = "";
        if (k.crowd > 1) snprintf(crowd, sizeof crowd, ", among %d at once", k.crowd);
        LogF("vrcam: strike measure - monster %u limb %d%s, %s (%s in the game's %s slot, steel %.2f kg): arrival %.2f N s at %.1f m/s, drive %.2f N s, stab %.2f N s - total %.2f "
             "(%.2f in the first %.0f ms); cut %.1f J over %.0f cm slid; peak %.0f N; %.0f ms, %d steps",
             k.id, k.bone, crowd, hands, hh::KindName((hh::Kind)kind), slot == 0 ? "right" : "left", h.SteelKg(), k.arrival,
             k.approach, k.drive, k.stab, k.Total(), k.window, strikemeasure::kWindowS * 1000.0, k.cut, k.slid * 100.0, k.peakForce, k.durS * 1000.0, k.steps);
    }
}

// A weapon in each hand: is this hand at that hold's weapon - its shaft where it is held or lies, or where it hangs on the back - within twice the grab distance
// (25 cm at least)? Then its grip press is meant for THAT weapon: the other calls nothing back by it (the user, 2026-10-24: «если я выбросил левое оружие и взял левой
// рукой двуручный меч например у меня сейчас появляется оружие левой руки параллельно этого не должно быть»).
bool NearShaft(const hh::Hold& h, int side, const hh::HandIn& in, const hh::Settings& s) {
    if (!h.Placed()) return false;
    double m = 1e9;
    h.TakeZone(side, in, s, &m);
    return m <= std::max(0.25, 2.0 * s.grabDistance);
}

// A weapon in each hand: a grip pressed (its rising edge) by a hand that holds one weapon, at the other - behind a shoulder, at its shaft or where it hangs - takes
// nothing; told once a press (the user, 2026-10-24: «левая рука по прежнему держит два оружия»). which: 0 the first weapon reached for, 1 the second.
bool g_fullPress[2][2] = {};
void TellFullHand(int side, int which, bool reaching, bool pressed) {
    bool& was = g_fullPress[side & 1][which & 1];
    if (pressed && reaching && !was)
        LogF("vrcam: weight - not taken: the %s hand holds its own weapon (one weapon a hand: the %s stays where it is)", HandName(side), which ? "second weapon" : "first weapon");
    was = pressed;
}

// [weight] dual_collide: a weapon in each hand - each weapon's pieces are capsules the OTHER meets, marked as a held weapon (Capsule::held 1: it stops on it and
// glances along it as on a monster's weapon, never goes in, pushes no doll, is no strike) under an id no monster has. Their velocity from where they were the
// step before (a knock's closing speed is the two's). The user, 2026-10-24: «для варвара оружия когда в двух руках не должны проходить друг сквозь друга должны
// стукаться сталью».
constexpr uint32_t kDualId = 0xFFFFFFF0u;
constexpr int kDualPieces = 3 + hh::Hold::kHeadParts;   // (its steel's pieces and its head's own volume, Hold::RoomPieces)
struct DualPieces {
    heft::contact::Capsule c[kDualPieces];
    V3 was[kDualPieces][2];
    int n = 0, nWas = 0;
    double t = 0.0, tWas = -1.0;
};
DualPieces g_dualOf[2];   // [0] the first weapon's pieces, [1] the second's
bool g_dualTouch = false;
heft::contact::Capsule g_monsterCaps[monsterbodies::kMax];   // this step's monsters' capsules (the weight thread), the second's own bodies start from them
int g_nMonsterCaps = 0;
ULONGLONG g_dualPulseAt = 0, g_dualToldAt = 0;
unsigned g_dualTold = 0;
// The pieces of a hold where they are now, with their velocity (t: the step's clock).
void NoteDualPieces(DualPieces& d, const hh::Hold& h, double t) {
    heft::contact::Capsule c[kDualPieces];
    const int n = h.Held() ? h.RoomPieces(c, kDualPieces) : 0;
    const double dt = t - d.t;
    for (int i = 0; i < n; ++i) {
        c[i].id = kDualId;
        c[i].held = 1;
        c[i].firm = true;   // (never left inside the other: two weapons put into each other come apart - contact.h Capsule::firm)
        c[i].part = -1;
        if (i < d.n && dt > 1e-4 && dt < 0.1) c[i].vel = heft::Mul(heft::Sub(heft::Mul(heft::Add(c[i].a, c[i].b), 0.5), heft::Mul(heft::Add(d.c[i].a, d.c[i].b), 0.5)), 1.0 / dt);
        d.c[i] = c[i];
    }
    d.n = n;
    d.t = t;
}
// The bodies of this step (the monsters', n of them, in caps) with the other weapon's pieces after them, into `out` (room for monsterbodies::kMax): the monsters'
// cut to leave room. Returns how many.
// [weight] dual_collide with [gameplay] melee_head_volume: each weapon's head as drawn (SetDrawnHead, SetDrawnHead2: the body's frame), for the weapon it was told for
// (key: g_key / g_key2 then) - a head of its own the other weapon stops on, and that stops on the other (Hold::SetHeads): one capsule, or the parts of a flat
// head (vr/weapon_head_parts.h - an axe's blade a thin slab where its mesh is). Under g_lock.
static_assert(kHeadParts == hh::Hold::kHeadParts, "weight.h's head parts are the hold's");
struct DrawnHead {
    V3 a[kHeadParts], b[kHeadParts];
    double r[kHeadParts] = {};
    int n = 0;
    uint64_t key = 0;
};
DrawnHead g_head[2];   // [0] the first weapon's, [1] the second's
void NoteDrawnHead(DrawnHead& h, const float* parts, int n, uint64_t key) {
    h.key = key;
    h.n = 0;
    for (int i = 0; parts && i < n && i < kHeadParts; ++i) {
        const float* p = parts + 7 * i;
        if (!(p[6] > 0.0f) || !std::isfinite(p[0] + p[1] + p[2] + p[3] + p[4] + p[5] + p[6])) { h.n = 0; return; }
        h.a[h.n] = V3{p[0], p[1], p[2]};
        h.b[h.n] = V3{p[3], p[4], p[5]};
        h.r[h.n] = p[6];
        ++h.n;
    }
}
// The hold's head from what was told for its weapon (key), while the pair meets; none otherwise.
void HeadFor(hh::Hold& h, const DrawnHead& d, uint64_t key, bool on) {
    const int had = h.HeadParts();
    if (on && d.key == key && d.n > 0) h.SetHeads(d.a, d.b, d.r, d.n, kDualId);
    else h.ClearHead();
    if (h.HasHead() && h.HeadParts() != had) {   // told when a head comes in: its parts, how thick, how far along and out of the shaft it reaches
        const hh::Line& l = h.ShaftLine();
        V3 out{};   // the way the head reaches out of the shaft: its farthest part's
        double reach = -1.0, rLo = 1e9, rHi = 0.0, lo = 1e9, hi = -1e9;
        for (int i = 0; i < d.n; ++i)
            for (const V3& p : {d.a[i], d.b[i]}) {
                const V3 q = heft::Sub(p, l.at);
                const double t = heft::Dot(q, l.dir);
                const V3 o = heft::Sub(q, heft::Mul(l.dir, t));
                if (heft::Len(o) + d.r[i] > reach) { reach = heft::Len(o) + d.r[i]; out = o; }
                lo = std::min(lo, t - d.r[i]);
                hi = std::max(hi, t + d.r[i]);
            }
        const double ol = heft::Len(out);
        const V3 across = ol > 1e-6 ? heft::Cross(l.dir, heft::Mul(out, 1.0 / ol)) : V3{};
        double thick = 0.0;
        for (int i = 0; i < d.n; ++i) {
            rLo = std::min(rLo, d.r[i]);
            rHi = std::max(rHi, d.r[i]);
            for (const V3& p : {d.a[i], d.b[i]}) thick = std::max(thick, std::fabs(heft::Dot(heft::Sub(p, l.at), across)) + d.r[i]);
        }
        LogF("vrcam: weight - the %s's head is a volume of its own against the other weapon of the pair: %d capsule(s), r %.1f - %.1f cm, %.0f cm along the shaft, out to %.0f cm "
             "off its line, %.1f cm thick (where the model's mesh is)", hh::KindName(h.WeaponKind()), d.n, rLo * 100.0, rHi * 100.0, (hi - lo) * 100.0, reach * 100.0, 2.0 * thick * 100.0);
    }
}
int WithDual(const heft::contact::Capsule* caps, int n, const DualPieces& other, heft::contact::Capsule* out) {
    const int keep = std::min(n, monsterbodies::kMax - other.n);
    for (int i = 0; i < keep; ++i) out[i] = caps[i];
    for (int i = 0; i < other.n; ++i) {
        out[keep + i] = other.c[i];
        g_capHeld[keep + i] = 1;
        g_capBone[keep + i] = -1;
        g_capOf[keep + i] = -1;
    }
    return keep + other.n;
}
// A knock of the two: a new touch of the one hold's weapon on the other's pieces at least clashloud::kQuietMps fast (the closing speed) - the game's block sound
// as loud as the knock was hard ([weight] clash_sound), a pulse as hard in every hand on either weapon, and a line (the first ten each, then once a second);
// resting on it (or sliding along it slowly) is silent. The user, 2026-10-24: «со стальным звуком, чем сильней стукнулись, тем сильней звук».
void DualKnock(const hh::Hold& h, const char* who) {
    const heft::contact::Report& c = h.LastContact();
    double approach = 0.0;
    bool touch = false;
    for (int i = 0; i < c.nMet; ++i)
        if (c.met[i].id == kDualId) { touch = true; approach = std::max(approach, c.met[i].approach); }
    const ULONGLONG nowMs = GetTickCount64();
    const clashloud::Clash k = clashloud::Of((float)approach);
    if (touch && !g_dualTouch && k.heard && nowMs - g_dualPulseAt >= 120) {
        g_dualPulseAt = nowMs;
        if (const HapticFn pulse = g_haptic.load())
            for (int side = 0; side < 2; ++side)
                if (g_out[side].holds || g_out2[side].holds) pulse(side, k.pulse, k.pulseMs);   // (0.6 and up: PS VR2's weak motor shows little under it)
        const ClashSoundFn sound = g_clashSound.load();
        if (sound) sound((float)approach, k.gameVolume, "the two weapons of a pair");
        if (++g_dualTold <= 10 || nowMs - g_dualToldAt >= 1000) {
            g_dualToldAt = nowMs;
            LogF("vrcam: weight - the two weapons struck each other at %.1f m/s (%s against the other): steel on steel - strength %.2f, sound %u/255 (%.0f %%)%s, a pulse %.2f for %d ms in both hands",
                 approach, who, k.strength, k.gameVolume, k.volume * 100.0f, sound ? "" : " not played (no sound registered)", k.pulse, k.pulseMs);
        }
    }
    g_dualTouch = touch;
}

// [weight] cleave: the hold's numbers for this weapon - its strike threshold (vr/strike_force.h) times the settings' over and cost.
void CleaveFor(hh::Settings& s, const hh::Hold& h) {
    AcquireSRWLockShared(&g_cleaveLock);
    const CleaveSettings c = g_cleave;
    ReleaseSRWLockShared(&g_cleaveLock);
    s.cleave = c.on && h.Placed();
    if (!s.cleave) return;
    strikeforce::Settings f;
    f.general = c.general;
    f.blunt = c.blunt;
    f.edged = c.edged;
    f.piercing = c.piercing;
    f.massExp = c.massExp;
    f.floorNs = c.floorNs;
    f = strikeforce::Clamp(f);
    const float need = strikeforce::Threshold(f, strikeforce::FamilyOf((int)h.WeaponKind()), (float)h.SteelKg());
    s.cleaveImpulse = std::clamp(c.over, 0.5f, 20.0f) * need;
    s.cleaveCost = std::clamp(c.cost, 0.0f, 20.0f) * need;
    s.cleaveMinSpeed = std::clamp(c.minSpeed, 0.0f, 20.0f);
}
// ... a body the stroke went through, told (the first 30, then one in 20).
void TellCleave(const hh::Hold& h, int which, const char* who) {
    if (h.Cleaves() == g_cleaveSeen[which]) return;
    g_cleaveSeen[which] = h.Cleaves();
    const hh::Hold::Cleave& k = h.LastCleave();
    if (++g_cleaveTold <= 30 || g_cleaveTold % 20 == 0)
        LogF("vrcam: weight - %s went through monster %u (%.1f N s at %.1f m/s, a cleave needs %.1f): on into the next with %.0f %% of its motion, %.1f N s of the stroke left "
             "(%d this stroke) - [weight] cleave", who, k.id, k.impulse, k.approach, k.need, k.keep * 100.0, k.left, k.inStroke);
}

void ResetSecond() {
    if (g_hold2.Placed()) LogF("vrcam: weight - the second weapon is no longer a body of its own (not a weapon in each hand)");
    g_hold2.Reset();
    g_key2 = 0;
    g_out2[0] = g_out2[1] = {};
    g_contactWas2 = 0;
    g_free2 = false;
    g_secondLetGo.store(false);
    g_secondHand.store(-1);
    g_secondPlaced.store(false);
    g_secondThrownAt.store(0);
    g_emptyHand2.Reset();
}

// A weapon in each hand: the other hand's weapon (the slot the weight model's first weapon is not in) a body of its own - placed in that hand's fist, held by
// it alone (the first weapon's hand never takes it, it never takes the first - each is busy with its own), never let go (the game holds both: no throw, no
// floor, no back), its own kind and mass, the same hands' strength and feel; stopped by the bodies, pushing the doll, measured, its hand drawn where it holds
// it (Apply). [weight] stab_in as the first's (its hand's weapon moving point first, hit by touch says). t: the step's clock (s).
void StepSecond(const skel::Input& in, const hh::HandIn hi[2], const int drawZone[2], const hh::Settings& s, const Head& head, double dt, int nBodies, double t) {
    const skel::Input::Weight& wt = in.weight;
    const int slot = wt.slot & 1, side2 = slot ^ 1;
    if (!(in.handsValid & (1u << side2))) {
        ResetSecond();
        return;
    }
    hh::HandIn h2[2] = {hi[0], hi[1]};
    h2[slot].grip = 0.0f;   // (the first weapon's hand: never on this one)
    h2[slot].busy = true;
    // [weight] second_lets_go: the HAND-OVER - the first weapon went over to the second's hand (it holds it, the first's own hand holds nothing): the first's hand may
    // have the second, drawn from behind a shoulder (whichever) or taken where it lies, so each hand holds one again (the user, 2026-10-24: «если я передал правое оружие
    // в левую руку то я должен иметь возможность из-за спины достать левое оружие в правую руку»). Its own hand, holding the first, takes nothing of it.
    // ... and either hand, empty, may have it: drawn from behind the shoulder it hangs behind (its own hand's), taken where it lies; called back from afar by its own hand
    // only. A hand with a weapon takes it not; a hand at the first's shaft is pressing for the first - nothing of the second's by that press.
    for (int h = 0; wt.secondLetsGo && h < 2; ++h) {   // (a hand with the first reaching for the second: told - it takes nothing)
        if (g_out[h].holds) {
            hh::HandIn free = hi[h];
            free.busy = false;
            TellFullHand(h, 1, (!g_hold2.Held() && (drawZone[h] == side2 || hi[h].behind)) || NearShaft(g_hold2, h, free, s), hi[h].grip > 0.6f);
        } else {
            g_fullPress[h][1] = hi[h].grip > 0.6f;
        }
    }
    for (int h = 0; wt.secondLetsGo && h < 2; ++h) {
        h2[h].grip = hi[h].grip;
        h2[h].busy = wt.busy[h] || g_out[h].holds || NearShaft(g_hold, h, hi[h], s);
        h2[h].claimed = wt.claimed[h];
        h2[h].draw = drawZone[h] == side2;
        h2[h].behind = h == side2 && hi[h].behind;
        h2[h].callable = h == side2;
    }
    if (!wt.secondLetsGo) {   // [weight] second_lets_go off: held for good by its own hand, as before
        h2[side2].grip = 1.0f;
        h2[side2].busy = false;
        h2[side2].claimed = false;
        h2[side2].draw = false;
        h2[side2].behind = false;
        h2[side2].callable = true;
    }
    hh::Settings s2 = s;
    s2.stab = s.stab;   // [weight] stab_in: the second sticks in as the first (the user, 2026-10-24: «меч когда во второй руке не хочет втыкаться»)
    s2.gripModel = 0;
    s2.drawMode = false;
    s2.stowAfter = -1.0;
    s2.recallAny = false;
    s2.recallFloorAny = false;
    s2.slide = false;
    s2.hiltOnly = false;
    s2.backTake = 0.0;
    if (wt.secondLetsGo) {   // [weight] second_lets_go: let go it is as the first - [weight] after_drop (the floor, the back), [hands] shoulder_draw, the calls back
        s2.drawMode = s.drawMode;
        s2.stowAfter = s.stowAfter;
        s2.recallAny = s.recallAny;
        s2.recallFloorAny = s.recallFloorAny;
        s2.backTake = s.backTake;
    }
    const uint64_t key2 = ((uint64_t)(uint32_t)wt.secondKind << 8) | (uint64_t)side2 | (1ull << 40);
    // [hands] throw_left_hand: the game threw the second weapon (a javelin in the other hand) - as the first's: its hand stays EMPTY (its model is not drawn,
    // Out::hidden2) until the game's throw animation is over (the hero out of the action modes, or [weight] thrown_hand_empty_ms), then a fresh one is in it.
    // Nothing of it lies on the floor while the game's own missile flies.
    if (const uint64_t thrownAt = g_secondThrownAt.exchange(0)) {
        g_emptyHand2.Start(thrownAt);
        LogF("vrcam: weight - the %s hand's %s (the second weapon) is thrown by the game: the hand stays empty until the game's throw animation is over (cap %.0f ms)",
             HandName(side2), hh::KindName((hh::Kind)wt.secondKind), (double)wt.thrownHandEmptyMs);
    }
    if (g_emptyHand2.Active()) {
        const uint64_t nowMs = GetTickCount64();
        const HeroModeFn modeFn = g_heroMode.load();
        const EmptyHand::End e = g_emptyHand2.Step(nowMs, modeFn ? modeFn() : -1, (uint64_t)std::max(0.0f, wt.thrownHandEmptyMs));
        if (e != EmptyHand::End::None) {
            g_key2 = 0;   // (a fresh one: placed below, in its own hand)
            LogF("vrcam: weight - the %s hand was empty %.0f ms (%s): the next %s (the second weapon) is drawn in it", HandName(side2),
                 (double)(nowMs - g_emptyHand2.StartedAt()), EmptyHand::Why(e), hh::KindName((hh::Kind)wt.secondKind));
        }
    }
    if (!g_hold2.Placed() || key2 != g_key2) {
        g_hold2.Place(side2, (hh::Kind)wt.secondKind, false, g_shaft2Key == key2 ? g_shaft2 : hh::Line{{}, h2[side2].fistLine}, h2, s2);
        g_key2 = key2;
        if (g_shaft2Key == key2 && g_drawn2Tip > g_drawn2Butt) g_hold2.SetDrawnShaft(g_shaft2, g_drawn2Butt, g_drawn2Tip, s2);
        g_out2[0] = g_out2[1] = {};
        g_contactWas2 = 0;
        const hh::Shape shape = hh::ShapeOf((hh::Kind)wt.secondKind, false);
        LogF("vrcam: weight - a weapon in each hand: the %s hand's %s is a body of its own too, %.2f kg (x %.2f) - one hand on it, stopped by the bodies, pushing them",
             HandName(side2), hh::KindName((hh::Kind)wt.secondKind), shape.Mass(), s2.weight);
    }
    if (g_drawn2New && g_shaft2Key == key2) {   // the second's shaft as drawn: the grips (a hand taking it), its ends and its lever follow the drawn weapon, as the first's
        g_hold2.SetDrawnShaft(g_shaft2, g_drawn2Butt, g_drawn2Tip, s2);
        g_drawn2New = false;
    }
    HeadFor(g_hold2, g_head[1], key2, wt.dualCollide);   // [weight] dual_collide: its head's own volume, as drawn
    if (g_hold2.Stowed() && g_back2New) g_hold2.SetStowPose(ToRoom(head, g_back2Body, g_back2Rot));   // (where the skeleton hangs it on the back)
    g_back2New = false;
    {
        const StabFn src = g_stabSrc.load();
        g_hold2.SetStabbing(s2.stab && src && src(g_out2[slot].holds ? slot : side2));   // (hit by touch: the weapon in the hand that holds it moving point first now - the hand-over's too)
    }
    if (wt.dualCollide && g_hold.Held() && g_hold2.Held() && g_dualOf[0].n > 0) {   // [weight] dual_collide: the second meets the first's pieces where its step left them
        static heft::contact::Capsule withDual[monsterbodies::kMax];
        const int n2 = WithDual(g_monsterCaps, g_nMonsterCaps, g_dualOf[0], withDual);
        g_hold2.SetBodies(withDual, n2);
        s2.bodies = true;
    }
    const bool heldWas2 = g_hold2.Held(), flyWas2 = g_hold2.Flying(), stowWas2 = g_hold2.Stowed();
    CleaveFor(s2, g_hold2);   // [weight] cleave
    g_hold2.Step(h2, dt, s2, g_out2);
    TellCleave(g_hold2, 1, "the second weapon's stroke");
    if (wt.dualCollide) {   // [weight] dual_collide: the second's pieces where it is now (the first meets them next step), a knock on the first's told
        NoteDualPieces(g_dualOf[1], g_hold2, t);
        DualKnock(g_hold2, "the second");
    }
    FromRoom(head, g_out2[side2].pose, g_out2Hand, g_out2Rot);
    {   // [weight] stab_in: its stab, told as the first weapon's
        const heft::stab::Report& st = g_hold2.LastStab();
        if ((st.entered || st.left) && g_stabTold < 60) {
            ++g_stabTold;
            if (st.entered) LogF("vrcam: weight - stab (the %s hand's, the second weapon): the point went into monster %u's body at %.1f m/s (%.1f cm in at once)", HandName(side2), st.id, st.speed, st.depth * 100.0);
            else LogF("vrcam: weight - stab (the %s hand's, the second weapon): out of monster %u's body, %.1f cm in at most - %s", HandName(side2), st.id, st.deepest * 100.0, st.why);
        }
    }
    g_free2 = !g_hold2.Held();
    FromRoom(head, g_hold2.BodyPose(), g_out2Body, g_out2BodyRot);
    g_secondLetGo.store(g_free2);
    {
        const int hand2 = g_out2[side2].holds ? side2 : g_out2[slot].holds ? slot : -1;
        if (hand2 == slot && g_secondHand.load() != slot)   // the hand-over: told once each time
            LogF("vrcam: weight - the second weapon taken by the %s hand: laid as the game's own grip has a weapon in that hand (its shaft measured off it)", HandName(slot));
        g_secondHand.store(hand2);
    }
    if (g_out2[slot].holds) FromRoom(head, g_out2[slot].pose, g_out2HandOther, g_out2RotOther);   // (the hand-over: the first's hand on it)
    if (g_hold2.Held() != heldWas2 || g_hold2.Flying() != flyWas2 || g_hold2.Stowed() != stowWas2)
        LogF("vrcam: weight - the %s hand's %s (the second weapon): %s", HandName(side2), hh::KindName((hh::Kind)wt.secondKind),
             g_hold2.Held() ? "in its hand again" : g_hold2.Flying() ? "called back: it flies into its hand" : g_hold2.Stowed() ? "on the back, behind its own shoulder"
             : "let go: it falls (the first weapon stays a weapon in its hand)");
    const heft::contact::Report& c = g_hold2.LastContact();
    if (wt.monsterBodies && wt.worldOk) {
        if (wt.monsterRagdoll) PushBack(g_hold2, head, g_contactWas2, nBodies);
        const bool strike = c.bodies > 0 && c.id != g_contactWas2 && c.id != kDualId;
        if (strike && c.approach >= 0.5) {   // the pulse in its hand, as the first weapon's
            const HapticFn pulse = g_haptic.load();
            static ULONGLONG pulseAt = 0, toldAt = 0;
            const ULONGLONG nowMs = GetTickCount64();
            if (pulse && nowMs - pulseAt >= 150) {
                pulseAt = nowMs;
                pulse(side2, (float)std::clamp(0.55 + 0.45 * (c.approach - 0.5) / 3.5, 0.55, 1.0), 45);
            }
            if (nowMs - toldAt >= 1000) {
                toldAt = nowMs;
                LogF("vrcam: weight - the %s hand's %s struck monster %u's body at %.1f m/s (%.1f cm in, put back on it): it %s", HandName(side2), hh::KindName((hh::Kind)wt.secondKind),
                     c.id, c.approach, c.depth * 100.0, c.slide > 0.3 ? "glances off along it" : "stops on it");
            }
        }
        g_contactWas2 = c.bodies > 0 ? c.id : 0;
        NoteBlocks(c);
        if (wt.strikeMeasure || wt.strikeByForce) MeasureStrikes(g_strikeMeasure2, g_hold2, g_out2, t, dt, wt.secondKind, side2, wt.strikeMeasure, 1);
    } else {
        g_contactWas2 = 0;
    }
}

void Apply(skel::Input* in, Out* out) {
    *out = Out{};
    const skel::Input::Weight& wt = in->weight;
    const int slot = wt.slot & 1;
    AcquireSRWLockExclusive(&g_lock);
    if (!wt.on || in->mode != 2 || !(in->handsValid & (1u << slot))) {
        if (g_hold.Placed()) LogF("vrcam: weight - off (%s)", !wt.on ? "no weapon held that way" : "no controller for its hand");
        g_hold.Reset();
        ResetSecond();
        g_throw.Reset();
        g_thrownFly.Reset();
        g_emptyHand.Reset();
        TrailClear();
        g_flightPending = false;
        g_key = 0;
        g_strKey = 0;
        g_strSteel = -1.0;
        g_strengthNote = StrengthNote{};
        g_holding.store(0);
        g_carrying.store(-1);
        g_wielding.store(0);
        g_holders[0].store(0);
        g_holders[1].store(0);
        g_firstSlot.store(-1);
        g_secondPlaced.store(false);
        g_placed.store(false);
        g_stowed.store(false);
        g_takenUntil.store(0);
        g_takeZone[0].store(0);
        g_takeZone[1].store(0);
        g_zoneTick[0].Reset();
        g_zoneTick[1].Reset();
        g_worldWas = worldanchor::Snap{};
        g_contactNote = ContactNote{};
        g_contactWas = 0;
        g_gripWas[0] = g_gripWas[1] = false;
        g_fitLogged[0].store(0xFFFFFFFFu);
        g_fitLogged[1].store(0xFFFFFFFFu);
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }
    hh::Settings s;
    s.weight = std::max(0.0f, wt.feel);
    s.strength = std::max(0.05f, wt.strength);
    s.gravity = std::max(0.0f, wt.gravity);
    s.damping = std::max(0.0f, wt.damping);
    s.twoHandsDouble = wt.twoHandsDouble;
    // [weight] hero_strength (vr/hero_strength.h): the hero's Strength against the weapon's requirement scales the steel's mass and the hands' strength - the two numbers every model
    // takes the same way (the library gets them as the masses of the steel and its `strength`). A Strength that was not read leaves both as they are.
    herostr::Out hstr;
    if (wt.heroStrength) {
        herostr::In hsIn;
        hsIn.str = wt.heroStr;
        hsIn.req = wt.reqStr;
        hsIn.classPct = wt.strengthClassPct;
        hsIn.effectPct = wt.strengthEffect;
        hsIn.kind = wt.kind;
        hsIn.twoHanded = wt.twoHanded;
        hsIn.honest = wt.strengthHonest;
        hstr = herostr::Factor(hsIn);
        if (hstr.known) {
            s.weight *= (float)hstr.steel;
            s.strength = std::max(0.05f, wt.strength * (float)hstr.hands);
        }
    }
    if (wt.gravityFeel) hh::UseGravityFeel(s);
    // [weight] hands_model 1: the weight-physics library moves the held body, when it is loaded (vr/weight_lib.h; asked for now - it is loaded when first wanted); without it ours
    if (wt.handsModel == 1 && !hh::GetEngine()) {
        const LibraryFn ensure = g_libraryEnsure.load();
        if (ensure) ensure();
    }
    const bool library = wt.handsModel == 1 && hh::GetEngine() != nullptr;
    if (library) {
        s.model = 1;
        s.modelFlags = wt.handsModelFlags;
    }
    // [weight] grip_model 1: the grip library holds the hands (vr/grip_lib.h), when it is loaded - else ours
    if (wt.gripModel == 1 && !hh::GetGripEngine()) {
        const LibraryFn ensure = g_gripEnsure.load();
        if (ensure) ensure();
    }
    const bool gripLib = wt.gripModel == 1 && hh::GetGripEngine() != nullptr;
    s.gripModel = gripLib ? 1 : 0;
    {
        static int lastGrip = -1;
        const int now = gripLib ? 1 : wt.gripModel == 1 ? 2 : 0;
        if (now != lastGrip) {
            lastGrip = now;
            LogF("vrcam: weight - %s", now == 1 ? "the grip library holds the hands (grip or trigger holds the second hand, tight or loose; its pose of the fingers; its pulses)"
                 : now == 2 ? "grip_model 1 asked, there is no grip library: our own way of holding" : "our own way of holding the weapon");
        }
    }
    {   // the feel's sliders, told when they change (the log shows what a test ran with)
        static float lastG = -1.0f, lastD = -1.0f, lastS = -1.0f;
        static int lastF = -1;
        const int now = (wt.gravityFeel ? 1 : 0) | (library ? 2 : 0) | (wt.handsModel == 1 && !library ? 4 : 0) | (wt.handsModelFlags ? 8 : 0);
        if (wt.gravity != lastG || wt.damping != lastD || wt.strength != lastS || now != lastF) {
            lastG = wt.gravity; lastD = wt.damping; lastS = wt.strength; lastF = now;
            LogF("vrcam: weight - gravity %.0f %%, damping %.0f %%, hands' strength %.0f %%, %s%s%s", wt.gravity * 100.0f, wt.damping * 100.0f,
                 wt.strength * 100.0f, library ? "the weight-physics library moves the weapon" : wt.gravityFeel ? "soft wrists (gravity feel)" : "stiff wrists (the old)",
                 library && wt.handsModelFlags ? ", its weight in the palm" : "",
                 wt.handsModel == 1 && !library ? " - hands_model 1 asked, there is no library: our own physics" : "");
        }
    }
    s.grabDistance = wt.grabM;
    s.letGo = wt.letGoM;
    s.fist = {wt.fist[0], wt.fist[1], wt.fist[2]};
    s.stowAfter = wt.afterDrop >= 1 ? (double)std::max(0.0f, wt.stowAfterS) : -1.0;   // 0: it lies on the floor as ever
    s.recallAny = wt.recallAny;
    s.drawMode = wt.drawMode;   // [hands] shoulder_draw: it lies where it fell (in the world), the grip of either hand calls it back, after the delay it goes to the back, a shoulder draws it
    if (s.drawMode) {
        s.stowAfter = (double)std::max(0.0f, wt.drawStowS);
        s.recallFloorAny = true;
        s.recallAny = false;
    }
    static_assert(gripzone::kCount == 5, "Input::Weight::hiltCm has a place for each of the zone's kinds");
    s.hiltOnly = wt.hiltOnly;   // [weight] grip_hilt_only: the second hand takes a sword (an axe, a mace ...) only on its hilt
    {
        const int zone = gripzone::IndexOf((uint32_t)in->weaponType);
        s.hiltLen = zone >= 0 && zone < 5 ? std::clamp((double)wt.hiltCm[zone], 0.0, (double)gripzone::kMaxCm) * 0.01 : 0.0;
    }
    s.slide = wt.slide && slidekinds::Allows(wt.slideKinds, (uint32_t)in->weaponType);   // [weight] slide_second_hand_<kind>: a staff's second hand slides, a sword's holds where it closed
    s.backTake = std::clamp((double)wt.backTakeM, 0.0, 0.8);
    s.floor = weightfloor::RoomFloorY(in->headRoom[1], wt.headAboveGroundM);   // the ground the player sees, not the room's floor

    const Head head = HeadOf(*in);
    V3 knock{};   // [weight] parry_feel: the knock going on, in the room (Knock)
    {
        float dir[3], metres = 0.0f;
        int ms = 0;
        uint64_t at = 0;
        {
            std::lock_guard<std::mutex> lock(g_knockMutex);
            for (int k = 0; k < 3; ++k) dir[k] = g_knockDir[k];
            metres = g_knockM;
            ms = g_knockMs;
            at = g_knockAt;
        }
        const uint64_t nowMs = GetTickCount64();
        const float share = at && nowMs >= at && nowMs - at < (uint64_t)ms ? parry::KnockShare((int)((nowMs - at) * (uint64_t)parry::kKnockMs / (uint64_t)std::max(ms, 1))) : 0.0f;
        if (share > 0.0f && wt.worldOk) {   // the render world's direction into the head's frame (two points through monsterbodies::RenderToHead) and on into the room
            monsterbodies::View view;
            for (int k = 0; k < 3; ++k) { view.eye[k] = wt.worldEye[k]; view.camR[k] = wt.worldCamR[k]; view.camF[k] = wt.worldCamF[k]; }
            view.upm = std::clamp(wt.worldUpm, 1.0f, 12.0f);
            float p0[3], p1[3], h0[3], h1[3];
            for (int k = 0; k < 3; ++k) { p0[k] = view.eye[k] / view.upm; p1[k] = p0[k] + dir[k]; }
            const float ident[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            if (monsterbodies::RenderToHead(view, p0, h0) && monsterbodies::RenderToHead(view, p1, h1))
                knock = heft::Mul(heft::Sub(ToRoom(head, h1, ident).pos, ToRoom(head, h0, ident).pos), (double)(metres * share));
        }
    }
    hh::HandIn hi[2];
    int drawZone[2] = {-1, -1};   // the shoulder each hand is behind when it may draw the weapon (0 right, 1 left)
    bool wearsShieldAt[2] = {false, false};   // the hand is busy with the shield (for the log's reason)
    // A weapon in each hand: the other hand is busy with its own (StepSecond) - while it has it. [weight] second_lets_go: the second let go (falling, lying, on the
    // back) leaves its hand free - empty, it takes the first weapon as its second hand the usual way (the user, 2026-10-24: «если я второе оружие выбрасываю рука не
    // расслабляется я не могу взять первое оружие», «второй рукой»); flying back into it, the hand is its again.
    // ... and whichever hand has it: the user, 2026-10-24: «если в левой руке есть оружие то я не могу взять второе» - a hand with the second weapon takes no other,
    // the right one too once the first went over to the left («из-за спины достать левое оружие в правую руку»).
    bool secondBusy[2] = {false, false};
    for (int side = 0; side < 2; ++side) {
        if (!wt.second) continue;
        if (!wt.secondLetsGo || !g_hold2.Placed()) secondBusy[side] = side != slot;   // (held for good, or not placed yet: its own hand's, as before)
        else secondBusy[side] = g_out2[side].holds || (g_hold2.Flying() && g_hold2.FlyHand() == side);
    }
    for (int side = 0; side < 2; ++side) {
        const int at = (in->handsValid & (1u << side)) ? side : slot;   // a hand not tracked: its pose stands in, unused
        hi[side].tracked = (in->handsValid & (1u << side)) != 0;
        hi[side].pose = ToRoom(head, in->hand[at], in->rot[at]);
        if (g_out[side].holds) hi[side].pose.pos = heft::Add(hi[side].pose.pos, knock);   // [weight] parry_feel: a hand on the weapon knocked aside with it
        hi[side].grip = wt.haveRawGrip ? wt.rawGrip[side] : in->grip[side];   // (a grip taken from the layout reads as let go in in->grip)
        hi[side].trigger = wt.haveRawTrigger ? wt.rawTrigger[side] : -1.0f;
        // the other hand holds something of its own (the shield strapped to it). With [hands] shoulder_draw handling the shield nobody is busy with it: the hand that takes the
        // weapon is the one the shield leaves (it goes to the other forearm as the weapon is drawn)
        // (the hand that wears the shield is busy with it - unless the shield FOLLOWS the sword: it moves to the other arm as the weapon is taken - or is put away)
        const bool wearsShield = wt.shieldArm == 0 || wt.shieldArm == 1 ? (!wt.shieldFollow && side == wt.shieldArm) : wt.shieldArm == 2 ? false : (side != slot && !wt.second && wt.otherBusy);
        wearsShieldAt[side] = wearsShield;
        hi[side].busy = wt.busy[side] || wearsShield || secondBusy[side];   // (a weapon in each hand: the hand that has the second takes not the first - StepSecond)
        hi[side].claimed = wt.claimed[side];
        // behind a shoulder: the swap gesture's volume (vr/shoulder_swap.h; the hands are in the head's turn frame already)
        hi[side].behind = !wt.drawMode && wt.afterDrop >= 1 && hi[side].tracked && shoulderswap::Near(in->hand[side]);   // (drawMode: the draw flag below says it)
        hi[side].fistLine = FistLine(*in, side);
        // [hands] shoulder_draw: behind either shoulder (whichever hand it is - a cross draw): its grip press draws the weapon from there
        if (wt.drawMode && hi[side].tracked) {
            const shoulderdraw::Side zone = shoulderdraw::ZoneSide(in->hand[side]);
            // (a javelin or a throwing weapon is thrown from the game's hand only - the throw gesture reads that controller - so only that hand draws one)
            const bool throwsKind = wt.kind == (int)hh::Kind::Javelin || wt.kind == (int)hh::Kind::Throwing;
            // (shield_stow: the left shoulder is the shield's, the weapon is drawn from the right one only)
            hi[side].callable = side == slot || !throwsKind;   // (a javelin or a throwing weapon is called and drawn into the game's hand only)
            if (zone != shoulderdraw::Side::None && !(wt.shieldZone && zone == shoulderdraw::Side::Left) && (side == slot || !throwsKind)) {
                float from[3];
                const float ident[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                shoulderdraw::DrawPoint(zone, from);
                hi[side].draw = true;
                drawZone[side] = zone == shoulderdraw::Side::Right ? 0 : 1;
                hi[side].drawFrom = ToRoom(head, from, ident).pos;
            }
        }
    }
    for (int side = 0; side < 2; ++side) {   // a hand with the second reaching for the first: told (it takes nothing - busy)
        if (!wt.second || !secondBusy[side]) { g_fullPress[side][0] = hi[side].grip > 0.6f; continue; }
        hh::HandIn free = hi[side];
        free.busy = false;
        const bool reaching = (!g_hold.Held() && (hi[side].behind || drawZone[side] >= 0)) || NearShaft(g_hold, side, free, s);
        TellFullHand(side, 0, reaching, hi[side].grip > 0.6f);
    }
    const int drawRaw[2] = {drawZone[0], drawZone[1]};   // (the shoulders as the hands are behind them, before the first's own-shoulder rule: the second's draw reads these)
    if (wt.second && wt.secondLetsGo) {   // a weapon in each hand: each shoulder is ITS weapon's - the first hangs behind its slot's shoulder, the second behind the other's -
        // and either hand, empty, draws the one behind the shoulder it reaches into itself (the user, 2026-10-24: «правой рукой я не могу достать оружие из-за левого
        // плеча левое почему-то это надо исправить»); a hand with a weapon takes none (busy). Called back from afar by the first's own hand only.
        for (int side = 0; side < 2; ++side) {
            if (hi[side].draw && drawZone[side] != slot) {
                hi[side].draw = false;
                drawZone[side] = -1;
            }
            if (NearShaft(g_hold2, side, hi[side], s)) hi[side].claimed = true;   // (a press at the second weapon is the second's: nothing of the first's by it)
        }
        hi[slot ^ 1].callable = false;
    }
    const uint64_t key = ((uint64_t)(uint32_t)in->weaponType << 32) | ((uint64_t)wt.kind << 8) | (wt.twoHanded ? 2u : 0u) | (uint64_t)slot;
    {   // what the hero's strength did, told once for each weapon and again when the factors move by more than 3 % (a level, a point, another piece of gear)
        uint64_t& lastKey = g_strKey;
        double& lastSteel = g_strSteel;
        StrengthNote n;
        n.on = hstr.known;
        n.reqFromGame = hstr.reqFromGame;
        n.str = wt.heroStr;
        n.req = (int)std::lround(hstr.req);
        n.classPct = wt.strengthClassPct;
        n.effectPct = wt.strengthEffect;
        n.ratio = (float)hstr.ratio;
        n.steel = (float)hstr.steel;
        n.hands = (float)hstr.hands;
        g_strengthNote = n;
        if (hstr.known && (key != lastKey || std::fabs(hstr.steel - lastSteel) > 0.03)) {
            lastKey = key;
            lastSteel = hstr.steel;
            LogF("vrcam: weight - the hero's strength: Str %d x %.0f %% (his class) = %.0f against the weapon's requirement %d%s -> ratio %.2f%s, the steel x%.2f, the hands' strength x%.2f%s (strength matters %.0f %%)",
                 wt.heroStr, (double)wt.strengthClassPct, hstr.capacity, (int)std::lround(hstr.req), hstr.reqFromGame ? " (the game's table)" : " (typical for its kind: the table is not read)", hstr.ratio,
                 wt.strengthHonest ? " (as it is: strength_honest)" : " (+50 each side)", hstr.steel, hstr.hands, wt.twoHandsDouble ? ", x2 with both hands on it" : "",
                 (double)wt.strengthEffect);
        } else if (!hstr.known && wt.heroStrength && (key != lastKey || lastSteel >= 0.0)) {
            lastKey = key;
            lastSteel = -1.0;
            LogF("vrcam: weight - hero_strength is ticked but the hero's Strength was not read: the weapon weighs as ever");
        }
    }
    // The weapon put in the slot's fist, where the game holds it (a new weapon, the arms back on the controllers, a thrown one's successor);
    // the shaft along the fist until it is measured off the pose.
    const auto place = [&](const char* why) {
        const hh::Line line = g_shaftKey == key ? g_shaft : hh::Line{{}, hi[slot].fistLine};
        g_hold.Place(slot, (hh::Kind)wt.kind, wt.twoHanded, line, hi, s);
        g_key = key;
        g_shaftNew = false;
        if (g_shaftKey == key && g_drawnTip > g_drawnButt) g_hold.SetDrawnShaft(g_shaft, g_drawnButt, g_drawnTip, s);   // (the same weapon: its drawn ends at once)
        g_haveSample = false;
        TrailClear();
        g_flightPending = false;
        g_fitLogged[0].store(0xFFFFFFFFu);   // a new weapon (or a fresh one in the hand): its takes are told afresh
        g_fitLogged[1].store(0xFFFFFFFFu);
        for (int side = 0; side < 2; ++side) g_out[side] = {};
        const hh::Shape shape = hh::ShapeOf((hh::Kind)wt.kind, wt.twoHanded);
        const bool longShaft = shape.tip - shape.butt >= heft::slide::kMinShaft;
        char zoneText[120] = "";   // [weight] grip_hilt_only: where the second hand takes it
        const hh::Shape& cur = g_hold.WeaponShape();   // (as the hold has it: scaled onto the drawn weapon once that is known)
        if (s.hiltOnly && g_hold.HiltEnd(s) < cur.tip - 1e-9)
            snprintf(zoneText, sizeof zoneText, "; its second hand takes it only on the hilt (%.0f cm from the pommel, %s)", (g_hold.HiltEnd(s) - cur.butt) * 100.0, s.hiltLen > 0.0 ? "the Settings' length" : "the model's");
        LogF("vrcam: weight - %s%s in the %s hand, %.2f kg (x %.2f), the other hand %s%s%s%s", wt.twoHanded ? "two-handed " : "",
             hh::KindName((hh::Kind)wt.kind), HandName(slot), shape.Mass(), s.weight,
             wt.otherBusy ? "holds something of its own" : "may take it with its grip", why,
             !longShaft ? "" : s.slide ? "; its second hand slides along the shaft" : "; its second hand holds where it closed (no slide for this kind)", zoneText);
    };
    if (!g_hold.Placed() || key != g_key) {
        g_throw.Reset();
        g_thrownFly.Reset();
        g_emptyHand.Reset();
        place("");
    }
    if (g_shaftNew && g_shaftKey == key) {
        g_hold.SetLine(g_shaft, s);
        g_shaftNew = false;
    }
    if (g_drawnNew && g_shaftKey == key) {   // the shaft as drawn, against the body's frame: the grips, the ends and the lever follow it
        {   // how far the model's line was from the drawn one (the line is the drawn one from now on): told when it is more than a hand's width, at most every 2 s
            const hh::Line& was = g_hold.ShaftLine();
            const double mid = 0.5 * (g_drawnButt + g_drawnTip);
            const V3 pm = heft::Add(g_shaft.at, heft::Mul(g_shaft.dir, mid));
            const V3 off = heft::Sub(pm, was.at);
            const double apart = heft::Len(heft::Sub(off, heft::Mul(was.dir, heft::Dot(off, was.dir))));
            const double turned = std::acos(std::clamp(heft::Dot(was.dir, g_shaft.dir), -1.0, 1.0)) * 57.29578;
            static ULONGLONG toldAt = 0;
            const ULONGLONG nowTick = GetTickCount64();
            if ((apart > 0.015 || turned > 1.5) && nowTick - toldAt > 2000 && g_hold.Held()) {
                toldAt = nowTick;
                LogF("vrcam: weight - the model's shaft was %.1f cm and %.1f deg off the drawn weapon's axis (the grips, the red line and the capsule's line are the drawn one now; %s hands on it)", apart * 100.0,
                     turned, g_out[0].holds && g_out[1].holds ? "both" : "one");
            }
        }
        g_hold.SetDrawnShaft(g_shaft, g_drawnButt, g_drawnTip, s);
        g_drawnNew = false;
    }
    HeadFor(g_hold, g_head[0], key, wt.second && wt.dualCollide);   // [weight] dual_collide: its head's own volume, as drawn
    // One step per new head sample; the pair's second eye gets the same hands.
    if (!g_haveSample || in->sample != g_sample) {
        // [hands] throw_by_grip: the right grip of a thrown weapon, released as a throw (vr/weight_throw.h). The weapon is let go AT the release, as any
        // is, and flies out of the hand and falls with the motion it had; the throw gesture's word comes within a moment after. A throw: it is
        // not for the floor, the back or taking - it ends by itself (it landed, the game's own throw animation is over, or thrown_fly_ms) and a
        // fresh one stands in the hand. No throw by the end of the wait: the release was a drop, which is what has been happening all along.
        {
            const GripIsThrowsFn owns = g_gripIsThrows.load();
            const LastThrowFn last = g_lastThrow.load();
            const uint64_t nowMs = GetTickCount64();
            const bool thrownKind = wt.kind == (int)hh::Kind::Javelin || wt.kind == (int)hh::Kind::Throwing;
            const HeroModeFn modeFn = g_heroMode.load();
            const int heroMode = modeFn ? modeFn() : -1;
            if (thrownKind && modeFn) g_thrownFly.Note(nowMs, heroMode);   // the hero's mode on every step: the game may take the press before the throw gesture says "thrown"
            ThrowRelease::In ti;
            const ThrowSideFn sideFn = g_throwSide.load();
            const int ts = sideFn ? sideFn() & 1 : 0;   // the hand whose grip the throw is: the right, or the left with [hands] throw_left_hand
            ti.owned = owns && last && thrownKind && owns();
            ti.holds = g_out[ts].holds;
            ti.grip = hi[ts].grip;
            ti.now = nowMs;
            ti.lastThrow = ti.owned ? last() : 0;
            const ThrowRelease::Act act = g_throw.Step(ti);
            g_hold.SetStowHold(act == ThrowRelease::Act::Pending);   // (the landed weapon is not put on the back before the verdict is in)
            // The grip is open: the throw as worked out on the last step is FROZEN here (the gesture reads it at its release, before the model has seen
            // the grip open - weight::LastThrow).
            if (g_throw.JustReleased() && g_outcome.valid && g_outcome.preview) {
                g_outcome.preview = false;
                g_outcome.releaseTickMs = nowMs;
            }
            if (act == ThrowRelease::Act::Thrown) {
                g_thrownFly.Start(g_throw.ReleasedAt());
                g_thrownFlies = wt.thrownPotion && wt.potionFlies;
                g_hold.MarkThrown();
                if (g_outcome.valid && !g_outcome.preview && g_outcome.releaseTickMs == g_throw.ReleasedAt()) {
                    // The model's own flight is the one worked out: the same horizontal scale and cap, so it lands where the outcome says. It has been
                    // (or is about to be, on this very step) falling with the raw velocity since the release: put it on the worked-out flight once it is out
                    // of the hands (after the hold's step below).
                    g_flightPending = true;
                    g_flightRelease = g_outcome.releaseTickMs;
                    LogF("vrcam: weight - the throw, worked out: the weapon's peak %.1f m/s, %.2f kg (the arm's push x%.2f), horizontal x%.1f%s -> %.1f m/s at %.0f deg, lands %.1f m out "
                         "after %.2f s (the cap of %.0f cells is %.1f m at %.2f units per metre)",
                         (double)heft::Len({g_outcome.rawVelocity[0], g_outcome.rawVelocity[1], g_outcome.rawVelocity[2]}), (double)g_outcome.massKg,
                         (double)g_outcome.massFactor, (double)g_outcome.reachScale, g_outcome.capped ? " (held by the cap)" : "", (double)g_outcome.speed,
                         (double)g_outcome.elevationDeg, std::hypot((double)g_outcome.landing[0], (double)g_outcome.landing[2]), (double)g_outcome.flightS,
                         (double)wt.throwMaxCells, weightflight::CapMetres(wt.throwMaxCells, wt.unitsPerMetre), (double)wt.unitsPerMetre);
                }
                LogF("vrcam: weight - the grip release was a throw: the model leaves the hand and falls until %s", g_thrownFlies
                     ? "it lands (potion_model_flies: the game taking the throw does not end it - the game's own potion is not drawn) or thrown_fly_ms - then it breaks and the next one is in the hand"
                     : wt.thrownModel == 0
                     ? "the game takes the throw (its own missile flies), it lands, or thrown_fly_ms - then a fresh one stands there"
                     : "it lands, the game's throw animation is over, or thrown_fly_ms - then a fresh one stands there");
            }
            if (g_thrownFly.Active()) {
                if (g_hold.Held() && nowMs - g_throw.ReleasedAt() > 600) {   // it never left the hands (the other hand holds it): not a thrown one
                    g_thrownFly.Reset();
                    g_hold.ClearThrown();
                } else if (g_hold.Thrown()) {
                    const ActionFrameFn framefn = g_actionFrame.load();
                    const ThrownFly::End e = g_thrownFly.Step(nowMs, g_hold.Landed(), wt.thrownModel == 0, (uint64_t)std::max(0.0f, wt.thrownFlyMs),
                                                              (uint64_t)std::max(0.0f, wt.thrownHandoffMs), g_thrownFly.Taken() && framefn && framefn(), g_thrownFlies);
                    if (e != ThrownFly::End::None) {
                        const double sinceRelease = (double)(nowMs - g_throw.ReleasedAt());
                        // The game took the throw: its own missile flies and its animation plays. Our model flew out of the hand along the throw until the game's
                        // missile was up (or the handoff time), and now hands over to it: the fresh weapon is there for the model but NOT DRAWN until the game's
                        // animation is over - else the player sees the spear still in his hand while the game's flies (vr/weight_throw.h EmptyHand). A model that
                        // landed or ran out AFTER the game took the throw hands over too.
                        const bool handedOver = wt.thrownModel == 0 && (ThrownFly::IsHandoff(e) || g_thrownFly.Taken());
                        if (ThrownFly::IsHandoff(e) && e != ThrownFly::End::Taken)
                            LogF("vrcam: weight - the model handed over to the game's missile after %.0f ms: %s", sinceRelease, ThrownFly::Why(e));
                        else if (g_thrownFlies)
                            LogF("vrcam: weight - the thrown potion's model is done (%s, %.0f ms after the release; the game %s the throw): it breaks, the next one is in the hand",
                                 ThrownFly::Why(e), sinceRelease, g_thrownFly.Taken() ? "took" : "did not take");
                        else
                            LogF("vrcam: weight - the thrown weapon is done (%s, %.0f ms after the release): %s", ThrownFly::Why(e), sinceRelease,
                                 handedOver ? "its own missile is the game's now" : "a fresh one in the hand");
                        g_throw.Reset();
                        place(" - thrown: the next one");
                        if (handedOver && wt.thrownHandEmptyMs > 0.0f) {
                            g_emptyHand.Start(nowMs);
                            LogF("vrcam: weight - the hand stays empty until the game's throw animation is over (cap %.0f ms)", (double)wt.thrownHandEmptyMs);
                        }
                    }
                }
            }
            if (g_emptyHand.Active()) {
                const EmptyHand::End e = g_emptyHand.Step(nowMs, heroMode, (uint64_t)std::max(0.0f, wt.thrownHandEmptyMs));
                if (e != EmptyHand::End::None)
                    LogF("vrcam: weight - the hand was empty %.0f ms (%s): the next one is drawn in it", (double)(nowMs - g_emptyHand.StartedAt()), EmptyHand::Why(e));
            }
        }
        LARGE_INTEGER now, f;
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&f);
        const double dt = g_haveSample ? std::clamp((double)(now.QuadPart - g_stepAt.QuadPart) / (double)f.QuadPart, 0.0, 0.05) : 0.0;
        g_stepAt = now;
        g_sample = in->sample;
        g_haveSample = true;
        const hh::HandOut was[2] = {g_out[0], g_out[1]};
        const bool awayWas = g_hold.Away();
        const int mainWas = g_hold.Main();
        const bool heldWas = g_hold.Held(), flyWas = g_hold.Flying(), stowWas = g_hold.Stowed();
        // on the back: where the skeleton drew it last pose (a frame late is fine); the weapon flies from there when taken
        if (stowWas && g_backNew) g_hold.SetStowPose(ToRoom(head, g_backBody, g_backRot));
        g_backNew = false;
        {   // the game's world moves under the room (the hero walks, the sticks turn him): a weapon that lies on the floor stays where it is in the WORLD
            const worldanchor::Snap now = worldanchor::Make(in->headRoom, in->headYawDeg, wt.worldEye, wt.worldCamR, wt.worldCamF, wt.worldUpm);
            const bool carry2 = s.drawMode && wt.second && wt.secondLetsGo && g_hold2.Placed() && (g_hold2.Away() || g_hold2.Flying());   // (the second weapon, let go, too)
            const bool carry = (s.drawMode && (g_hold.Away() || g_hold.Flying())) || carry2;
            if (!carry) g_worldTold = false;
            if (carry && !(wt.worldOk && now.ok)) {
                if (!g_worldTold) { g_worldTold = true; LogF("vrcam: weight - the camera's frame in the world is not known: a weapon on the floor moves with the hero"); }
            } else if (carry && g_worldWas.ok) {
                const worldanchor::Move mv = worldanchor::Between(g_worldWas, now);
                const double jump = std::hypot(mv.sx, mv.sz);
                if (mv.ok && jump < 1.5 && std::fabs(mv.delta) < 1.0) {   // (a jump of more than that is a new area or a teleport: the weapon stays in the room's place)
                    if (jump > 1e-4 || std::fabs(mv.delta) > 1e-4) {
                        if (s.drawMode && (g_hold.Away() || g_hold.Flying())) g_hold.Transport(heft::QFromAxisAngle({0, 1, 0}, mv.delta), {mv.sx, 0.0, mv.sz});
                        if (carry2) g_hold2.Transport(heft::QFromAxisAngle({0, 1, 0}, mv.delta), {mv.sx, 0.0, mv.sz});
                    }
                }
            }
            g_worldWas = wt.worldOk ? now : worldanchor::Snap{};
        }
        // [weight] monster_bodies: the hostile monsters near the hero as bodies, from the touch's scan (render metres) into the room through the head's frame - the very
        // view the drawn weapon is put in the world with, run back (vr/monster_bodies.h). The weapon is put back on them after the step (cleanroom/heft/contact.h).
        LARGE_INTEGER bodiesAt{};
        int nBodies = 0;
        if (wt.monsterBodies && wt.worldOk) {
            QueryPerformanceCounter(&bodiesAt);
            const BodiesFn src = g_bodiesSrc.load();
            monsterbodies::Snapshot snap;
            heft::contact::Capsule caps[monsterbodies::kMax];
            if (src && src(&snap)) {
                g_capBone.fill(-1);
                g_capHeld.fill(0);
                ++g_bodyStep;
                monsterbodies::View view;
                for (int i = 0; i < 3; ++i) { view.eye[i] = wt.worldEye[i]; view.camR[i] = wt.worldCamR[i]; view.camF[i] = wt.worldCamF[i]; }
                view.upm = std::clamp(wt.worldUpm, 1.0f, 12.0f);   // (the touch's render metres are over the same, clamped so)
                const float ident[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                for (int i = 0; i < snap.n && nBodies < monsterbodies::kMax; ++i) {
                    float a[3], b[3];
                    if (!monsterbodies::RenderToHead(view, snap.b[i].bottom, a) || !monsterbodies::RenderToHead(view, snap.b[i].top, b)) continue;
                    g_capBone[nBodies] = snap.b[i].rig ? snap.b[i].bone : -1;   // (the debug view tints the limb the weapon is on)
                    g_capOf[nBodies] = i;
                    heft::contact::Capsule& c = caps[nBodies++];
                    c.a = ToRoom(head, a, ident).pos;
                    c.b = ToRoom(head, b, ident).pos;
                    c.radius = snap.b[i].rig ? snap.b[i].radius : monsterbodies::CoreRadius(snap.b[i].radius);   // (a limb is as thick as its mesh; the touch's capsule less its margin)
                    c.id = snap.b[i].id;
                    c.part = snap.b[i].rig ? snap.b[i].bone : -1;   // ([weight] stab_in: a wound follows its limb)
                    c.held = snap.b[i].held;                        // ([weight] monster_shields: a shield or a weapon it holds - stops the weapon, no hit, no doll)
                    g_capHeld[nBodies - 1] = c.held;
                    if (wt.stabWithRun)   // [gameplay] melee_thrust_with_run: its velocity in the room - the hero's run counts against it (vr/body_motion.h)
                        c.vel = g_bodyMotion.Feed(bodymotion::Key(c.id, c.part), heft::Mul(heft::Add(c.a, c.b), 0.5), dt, g_bodyStep);
                }
                g_stabView = view;   // ([debug] stab_debug: the room's points drawn back in the render world)
                g_stabViewOk = true;
                if (wt.monsterRagdoll) { g_capSnap = snap; g_capView = view; }
            }
            for (int i = 0; i < nBodies; ++i) g_monsterCaps[i] = caps[i];
        }
        g_nMonsterCaps = nBodies;
        // [weight] dual_collide: the first weapon meets the second's pieces (where they were after its last step) as it meets a monster's weapon
        const bool dual = wt.second && wt.dualCollide && g_hold2.Placed() && g_hold2.Held() && g_hold.Held();
        if (!dual) { g_dualOf[0].n = g_dualOf[1].n = 0; g_dualTouch = false; }
        {
            static heft::contact::Capsule withDual[monsterbodies::kMax];
            const int n1 = dual ? WithDual(g_monsterCaps, nBodies, g_dualOf[1], withDual) : nBodies;
            if (n1 > 0) g_hold.SetBodies(dual ? withDual : g_monsterCaps, n1);
            else g_hold.SetBodies(nullptr, 0);
            g_hold2.SetBodies(nBodies > 0 ? g_monsterCaps : nullptr, nBodies);   // (a weapon in each hand: the other one meets the same bodies - and the first's pieces, StepSecond)
            s.bodies = n1 > 0;
        }
        // [weight] stab_in: a point-first strike goes into the body - while hit by touch says the weapon in the hand that holds it (or the other hand on it) is thrusting now
        s.stab = wt.stabIn && wt.monsterBodies;
        s.stabParams = stickstr::Apply(StabParamsOf(wt), wt.stick);   // (its sliders, live; [weight] stick_by_strength / stick_pct: as hard as the hero's strength says)
        // (a body's own surface stops and holds the weapon the same for every class - the hit and the weight are felt; only caught between several does the class count)
        s.wedgeFriction = stickstr::Friction(1.0, wt.stick);
        {   // the sticking's factor, told when it changes (a class, a slider)
            static double toldStick = -1.0;
            if (std::fabs(wt.stick - toldStick) > 0.01) {
                toldStick = wt.stick;
                LogF("vrcam: weight - sticking: x%.2f (%s, stick_pct %.0f %%) - the stab's resistance %.0f %% of the toolkit's, the pull that frees it %.0f N, the tear %.0f cm; caught "
                     "between monsters the friction x%.2f (on one body its own %.2f, as for every class)",
                     wt.stick, wt.stickByStrength ? "the strength table's class multiplier, as much as strength matters" : "not by strength", (double)wt.stickPct,
                     100.0 * s.stabParams.fleshDamper / heft::stab::Params{}.fleshDamper, s.stabParams.unlockForce, s.stabParams.breakM * 100.0, s.wedgeFriction, hh::BodyFriction((hh::Kind)wt.kind));
            }
        }
        if (s.stab && !heft::stab::GetEngine()) {   // the stab is the weight library's (D2rwStab): asked for now - without it every weapon stops at the surface
            const LibraryFn ensure = g_libraryEnsure.load();
            if (ensure) ensure();
            static bool told = false;
            if (!heft::stab::GetEngine() && !told) {
                told = true;
                LogF("vrcam: weight - stab_in: no stab in a weight library (d2r_vr_weight.dll 1.1 or newer, with D2rwStab, in d2rloader\\d2r_vr_libs): every weapon stops at the surface");
            }
        }
        {
            const StabFn src = g_stabSrc.load();
            const int m = g_hold.Main();
            g_hold.SetStabbing(s.stab && src && m >= 0 && (src(m) || (g_out[m ^ 1].holds && src(m ^ 1))));
        }
        CleaveFor(s, g_hold);   // [weight] cleave
        g_hold.Step(hi, dt, s, g_out);
        TellCleave(g_hold, 0, wt.second ? "the first weapon's stroke" : "the stroke");
        if (wt.second && wt.dualCollide) {   // [weight] dual_collide: the first's pieces where it is now (the second meets them), a knock on the second's told
            NoteDualPieces(g_dualOf[0], g_hold, (double)now.QuadPart / (double)f.QuadPart);
            DualKnock(g_hold, "the first");
        }
        g_steelKg.store(g_hold.Placed() ? (float)g_hold.SteelKg() : 0.0f);
        if (wt.monsterBodies && wt.worldOk) {   // what the weapon met: a pulse in the hands that hold it when it strikes a body, a line for the log, the cost
            LARGE_INTEGER end;
            QueryPerformanceCounter(&end);
            const heft::contact::Report& c = g_hold.LastContact();
            ContactNote note;
            note.bodies = c.bodies;
            note.id = c.id;
            note.bone = c.bodies > 0 && c.body >= 0 && c.body < monsterbodies::kMax ? g_capBone[c.body] : -1;
            note.approachMs = (float)c.approach;
            note.depthCm = (float)(c.depth * 100.0);
            note.slideMs = (float)c.slide;
            // (the step with the bodies against what it costs without: the hold's step itself is a few microseconds - the whole is told, an upper bound)
            note.costUs = (float)((double)(end.QuadPart - bodiesAt.QuadPart) * 1e6 / (double)f.QuadPart);
            g_contactNote = note;
            if (wt.monsterRagdoll) PushBack(g_hold, head, g_contactWas, nBodies);   // [weight] monster_ragdoll: the body takes the push back
            const ULONGLONG nowMs = GetTickCount64();
            const bool strike = c.bodies > 0 && c.id != g_contactWas && c.id != kDualId;   // a new body met (not every step it rests on one; the other weapon of a pair is its own knock)
            if (strike && c.approach >= 0.5) {
                const HapticFn pulse = g_haptic.load();
                if (pulse && nowMs - g_contactPulseAt >= 150) {   // (PS VR2 feels 0.7 / 45 ms; a gentler one is lost - vr/haptics.h)
                    g_contactPulseAt = nowMs;
                    const float amp = (float)std::clamp(0.55 + 0.45 * (c.approach - 0.5) / 3.5, 0.55, 1.0);
                    for (int side = 0; side < 2; ++side)
                        if (g_out[side].holds) pulse(side, amp, 45);
                }
                ++g_bodiesCost.strikes;
                if (nowMs - g_contactToldAt >= 1000) {
                    g_contactToldAt = nowMs;
                    LogF("vrcam: weight - the weapon struck monster %u's body at %.1f m/s (%.1f cm in, put back on it): it %s; %d bod%s near, %.0f us%s%s", c.id, c.approach, c.depth * 100.0,
                         g_hold.Stuck() ? "goes in (stab_in)" : c.slide > 0.3 ? "glances off along it" : "stops on it", nBodies, nBodies == 1 ? "y" : "ies", (double)note.costUs,
                         s.stab && !g_hold.Stuck() ? "; no stab: " : "", s.stab && !g_hold.Stuck() ? g_hold.LastStab().why : "");
                }
            }
            g_contactWas = c.bodies > 0 ? c.id : 0;
            BodiesCost& bc = g_bodiesCost;
            if (!bc.since) bc.since = nowMs;
            bc.us += note.costUs;
            bc.worst = std::max(bc.worst, (double)note.costUs);
            ++bc.steps;
            if (nowMs - bc.since >= 60000) {
                LogF("vrcam: weight - monster bodies: %u steps in the last minute, %.1f us a step on average (at most %.0f), %u strikes", bc.steps, bc.us / std::max(1u, bc.steps), bc.worst, bc.strikes);
                bc = BodiesCost{};
                bc.since = nowMs;
            }
        } else {
            g_contactNote = ContactNote{};
            g_contactWas = 0;
        }
        if (s.stab || g_hold.LastStab().left) TellStab(head, s, wt.stabDebug);   // [weight] stab_in: the log, [debug] stab_debug (after the cost above: a log line is a millisecond)
        NoteBlocks(g_hold.LastContact());   // [weight] monster_shields: on a shield or a weapon the monster holds, not on its body - no hit by touch on it
        if ((wt.strikeMeasure || wt.strikeByForce) && wt.monsterBodies)   // [debug] strike_measure: what each strike delivered into a body (vr/strike_measure.h; docs/notes/strike_force.md);
            MeasureStrikes(g_strikeMeasure, g_hold, g_out, (double)now.QuadPart / (double)f.QuadPart, dt, wt.kind, slot, wt.strikeMeasure, 0);   // [weight] strike_by_force: the live ones
        // A weapon in each hand: the other hand's weapon is a body of its own (StepSecond) - stopped by the bodies, pushing them, measured, drawn where it is
        if (wt.second && wt.secondKind >= 0) StepSecond(*in, hi, drawRaw, s, head, dt, nBodies, (double)now.QuadPart / (double)f.QuadPart);
        else if (g_hold2.Placed()) ResetSecond();
        if (gripLib) {   // the pulses the grip library gives a hand: coming within reach of the shaft, closing on it, letting go
            const HapticFn pulse = g_haptic.load();
            for (int side = 0; side < 2; ++side)
                if (pulse && g_out[side].events && g_out[side].hapticAmp > 0.0 && g_out[side].hapticMs > 0)
                    pulse(side, (float)std::min(1.0, g_out[side].hapticAmp), g_out[side].hapticMs);
        }
        g_free = !g_hold.Held();
        FromRoom(head, g_hold.BodyPose(), g_outBody, g_outBodyRot);
        {   // the weapon's centre and velocity on this step; a thrown kind in the hand: the throw it would make if let go now (weight::LastThrow)
            const uint64_t stepMs = GetTickCount64();
            if (g_flightPending) {   // a throw was classified: the model's flight is put on the worked-out one as soon as the weapon is out of the hands
                if (g_hold.Falling()) {
                    const double te = std::clamp((double)(stepMs - g_flightRelease) * 0.001, 0.0, 0.3), g = weightflight::kGravity;
                    const V3& c = g_outRoom.com;
                    const V3& v = g_outRoom.vel;
                    g_hold.SetFlight({c.x + v.x * te, c.y + v.y * te - 0.5 * g * te * te, c.z + v.z * te}, {v.x, v.y - g * te, v.z});
                    g_flightPending = false;
                } else if (stepMs - g_flightRelease > 400) {
                    g_flightPending = false;
                }
            }
            TrailPush(stepMs, g_hold.Com(), g_hold.ComVel());
            if ((wt.kind == (int)hh::Kind::Javelin || wt.kind == (int)hh::Kind::Throwing) && g_hold.Held())
                WorkOutThrow(wt, head, s.floor, g_hold.WeaponShape().Lie(), g_hold.SteelKg(), stepMs);
        }
        if (heldWas && !g_hold.Held() && !g_hold.Flying()) LogF("vrcam: weight - both grips let go: it falls");
        if (!stowWas && g_hold.Stowed()) LogF("vrcam: weight - it lay %.1f s: now on the back", (double)s.stowAfter);
        if (stowWas && !g_hold.Stowed()) {
            g_takenUntil.store(GetTickCount64() + kTakenLatchMs);
            if (g_hold.Flying())
                LogF("vrcam: weight - the %s hand %s from the back", HandName(g_hold.FlyHand()),
                     g_hold.FlyHand() == g_hold.First() ? "calls it back" : "takes it behind the shoulder");
        } else if (!flyWas && g_hold.Flying()) {
            if (s.drawMode && hi[g_hold.FlyHand()].draw)
                LogF("vrcam: weight - the %s hand draws it from behind the %s shoulder%s", HandName(g_hold.FlyHand()), HandName(drawZone[g_hold.FlyHand()] == 0 ? 0 : 1),
                     drawZone[g_hold.FlyHand()] == g_hold.FlyHand() ? "" : " (a cross draw)");
            else if (s.drawMode)
                LogF("vrcam: weight - the %s grip calls it back from where it lay", HandName(g_hold.FlyHand()));
            else
                LogF("vrcam: weight - the %s grip calls it back", HandName(slot));
        }
        for (int side = 0; side < 2; ++side) {   // where a grip pressed now would take the weapon: the belt potions ask (weight::TakeZone) so that a hand in the weapon's zone takes the weapon
            double metres = 1e9;
            const hh::Hold::TakeAt at = g_hold.TakeZone(side, hi[side], s, &metres);
            const uint32_t mm = metres >= 65.0 ? 0xFFFFFFu : (uint32_t)std::lround(std::max(0.0, metres) * 1000.0);
            g_takeZone[side].store((uint32_t)at | (mm << 8));
        }
        {   // [weight] take_haptic: a hand enters the take zone behind a shoulder while the weapon is away - a short pulse, so the hand feels where to grab
            const HapticFn pulse = g_haptic.load();
            const ULONGLONG nowMs = GetTickCount64();
            for (int side = 0; side < 2; ++side) {
                // the take zone: behind a shoulder, or at the weapon where it hangs (a fist within back_take_cm of its drawn shaft)
                const double dist = g_hold.StowDistance(side, hi[side], s);
                const bool atBack = s.backTake > 0.0 && dist <= s.backTake;
                // shoulder_draw: the weapon is away on the floor (or still falling) and the hand enters its shoulder's zone; otherwise the weapon on the back and either shoulder
                const bool away = s.drawMode ? (g_hold.Away() || g_hold.Stowed()) : g_hold.Stowed();
                const bool tick = g_zoneTick[side].Step(away, s.drawMode ? (hi[side].draw || atBack) : (hi[side].behind || atBack), hi[side].busy, nowMs);
                if (tick && pulse && wt.takeHaptic > 0.0f) pulse(side, std::min(1.0f, wt.takeHaptic), kTickMs);
                // a grip squeezed while the weapon is away and still not taken: say why, so the next test shows it
                const float gv = hi[side].grip;
                const bool down = gv > 0.6f ? true : gv < 0.4f ? false : g_gripWas[side];
                if (down && !g_gripWas[side] && stowWas && g_hold.Stowed()) {
                    const char* why = !hi[side].tracked ? "the hand is not tracked"
                                    : hi[side].busy ? (wearsShieldAt[side] ? "the hand wears the shield and the shield does not follow the sword (shoulder_draw_shield is off)" : "the hand holds something of its own (a belt potion)")
                                    : hi[side].behind || atBack ? "(inside the zone: it should have been taken)"
                                    : hi[side].claimed ? "the grip is the weapon swap's claim, and the hand is outside the zone"
                                    : (side == g_hold.First() || s.recallAny) ? "(a call-back: it should have been called)"
                                    : "outside the zone, and only the hand the game holds it in calls it from afar (recall_any_hand is off)";
                    LogF("vrcam: stowed weapon: the %s hand is %.0f cm from it, zone %.0f cm, behind a shoulder %s - not taken: %s", HandName(side), dist * 100.0,
                         s.backTake * 100.0, hi[side].behind ? "yes" : "no", why);
                }
                // shoulder_draw: a grip squeezed while the weapon is away and not drawn - say why (the next test shows it)
                if (s.drawMode && down && !g_gripWas[side] && awayWas && !g_hold.Flying() && !g_hold.Held()) {
                    const shoulderdraw::Side zone = hi[side].tracked ? shoulderdraw::ZoneSide(in->hand[side]) : shoulderdraw::Side::None;
                    const char* why = !hi[side].tracked ? "the hand is not tracked"
                                    : hi[side].busy ? (wearsShieldAt[side] ? "the hand wears the shield and the shield does not follow the sword (shoulder_draw_shield is off)" : "the hand holds something of its own (a belt potion)")
                                    : zone == shoulderdraw::Side::None ? "the hand is not behind a shoulder"
                                    : wt.shieldZone && zone == shoulderdraw::Side::Left ? "the left shoulder is the shield's (shoulder_draw_shield_stow): the weapon is drawn from the right one"
                                    : !hi[side].draw ? "a javelin or a throwing weapon is drawn into the game's hand only (the throw reads that controller)"
                                    : g_hold.Thrown() ? "it was thrown: it ends by itself"
                                    : "(inside its zone: it should have been drawn)";
                    LogF("vrcam: shoulder draw: the %s hand squeezed its grip, the weapon (away) was not drawn: %s", HandName(side), why);
                }
                g_gripWas[side] = down;
            }
        }
        for (int side = 0; side < 2; ++side) {
            FromRoom(head, g_out[side].pose, g_outHand[side], g_outRot[side]);
            if (g_out[side].holds && !was[side].holds && g_out[side].grabs > was[side].grabs) {   // not as placed
                const Pose b = g_hold.BodyPose(), on = g_hold.OnWeapon(side);
                const double along = heft::Dot(heft::Sub(on.pos, heft::Add(b.pos, heft::Rotate(b.rot, g_hold.ShaftLine().at))),
                                               heft::Rotate(b.rot, g_hold.ShaftLine().dir));
                LogF("vrcam: weight - the %s hand takes it %.0f cm %s where the game holds it", HandName(side), fabs(along) * 100.0,
                     along >= 0.0 ? "ahead of" : "behind");
            } else if (!g_out[side].holds && was[side].holds) {
                LogF("vrcam: weight - the %s hand lets go", HandName(side));
            }
        }
        if (g_hold.Held() && g_hold.Main() != mainWas) LogF("vrcam: weight - now in the %s hand", HandName(g_hold.Main()));
        {   // [gameplay] melee_not_butt: where each holding hand is on the shaft - metres ahead (toward the tip) of the weapon bone's origin, the game's own grip
            const Pose b = g_hold.BodyPose();
            const bool measured = g_shaftKey == g_key;
            for (int side = 0; side < 2; ++side) {
                float ahead = 0.0f;
                bool on = false;
                if (measured && g_out[side].holds) {
                    const Pose w = g_hold.OnWeapon(side);
                    const V3 rel = heft::Rotate(heft::QConj(b.rot), heft::Sub(w.pos, b.pos));
                    ahead = (float)(heft::Dot(heft::Sub(rel, g_shaft.at), g_shaft.dir) - g_boneAlong);
                    on = std::isfinite(ahead);
                }
                g_handAhead[side].store(on ? ahead : 0.0f);
                g_handOn[side].store(on);
            }
        }
        // [debug] the shaft's ends, BodyWalk's frame
        V3 butt, tip;
        g_hold.Ends(&butt, &tip);
        float q[4];
        EndsOut e{};
        FromRoom(head, {butt, {}}, e.butt, q);
        FromRoom(head, {tip, {}}, e.tip, q);
        for (int i = 0; i < 3; ++i) { e.butt[i] += in->handOffset[i]; e.tip[i] += in->handOffset[i]; }   // drawn beside the controllers as the hands
        e.at = GetTickCount64();
        g_ends = e;
    }
    uint32_t holding = 0;
    for (int side = 0; side < 2; ++side) {
        if (g_out[side].on > 0.0) {
            memcpy(in->hand[side], g_outHand[side], sizeof g_outHand[side]);
            memcpy(in->rot[side], g_outRot[side], sizeof g_outRot[side]);
        }
        out->holds[side] = g_out[side].holds;
        out->on01[side] = (float)g_out[side].on;
        out->grabs[side] = g_out[side].grabs;
        if (g_out[side].holds) holding |= 1u << side;
    }
    if (wt.second && g_hold2.Placed() && g_hold2.Held()) {   // a weapon in each hand: the other hand drawn where its own weapon is (stopped on a body, lagging its weight)
        const int side2 = slot ^ 1;
        if (g_out2[side2].on > 0.0) {
            memcpy(in->hand[side2], g_out2Hand, sizeof g_out2Hand);
            memcpy(in->rot[side2], g_out2Rot, sizeof g_out2Rot);
        }
        if (g_out2[side2].holds) holding |= 1u << side2;   // (its grip is the weapon's, not the layout's; the belt takes no bottle with it)
        if (g_out2[slot].holds && !g_out[slot].holds) {     // the hand-over: the first's hand drawn where it holds the second
            if (g_out2[slot].on > 0.0) {
                memcpy(in->hand[slot], g_out2HandOther, sizeof g_out2HandOther);
                memcpy(in->rot[slot], g_out2RotOther, sizeof g_out2RotOther);
            }
            holding |= 1u << slot;
        }
    }
    if (wt.second && g_hold2.Placed()) {   // [weight] second_lets_go: where the skeleton draws it let go
        out->second = true;
        out->slot2 = slot ^ 1;
        out->free2 = !g_out2[slot ^ 1].holds;   // (not in its own hand - let go, or the hand-over's other hand has it: drawn where its body is)
        memcpy(out->body2, g_out2Body, sizeof out->body2);
        memcpy(out->body2Rot, g_out2BodyRot, sizeof out->body2Rot);
        out->stowed2 = g_hold2.Stowed();
        out->hidden2 = g_emptyHand2.Active();   // [hands] throw_left_hand: thrown by the game, its hand empty
        out->hand2 = g_out2[slot ^ 1].holds ? (slot ^ 1) : g_out2[slot].holds ? slot : -1;
        const hh::Shape& sh2 = g_hold2.WeaponShape();
        out->midM2 = (float)(0.5 * (sh2.butt + sh2.tip));
    }
    out->on = true;
    out->slot = slot;
    out->main = g_hold.Main();
    out->free = g_free;
    memcpy(out->body, g_outBody, sizeof out->body);
    memcpy(out->bodyRot, g_outBodyRot, sizeof out->bodyRot);
    out->asPlaced = g_hold.AsPlaced() && !g_out[slot ^ 1].holds;
    out->gripLib = gripLib;
    for (int side = 0; side < 2; ++side) {
        out->gripMode[side] = g_out[side].mode;
        out->gripRoll[side] = (float)g_out[side].roll;
    }
    {   // the wood as the weight model has it, from the weapon bone's origin
        const hh::Shape& sh = g_hold.WeaponShape();
        const double along = g_shaftKey == g_key ? g_boneAlong : 0.0;
        out->nPieces = std::min(3, sh.nPieces);
        for (int i = 0; i < out->nPieces; ++i) {
            out->pieceT0[i] = (float)(std::min(sh.pieces[i].a.y, sh.pieces[i].b.y) - along);
            out->pieceT1[i] = (float)(std::max(sh.pieces[i].a.y, sh.pieces[i].b.y) - along);
            out->pieceR[i] = (float)sh.pieces[i].radius;
        }
        out->shaftButtT = (float)(sh.butt - along);
        out->shaftTipT = (float)(sh.tip - along);
    }
    out->stowed = g_hold.Stowed();
    out->hidden = g_emptyHand.Active();
    {   // the middle of the shaft from the weapon bone: the shape counts from where the fist passes the shaft, the bone stands boneAlong ahead of that
        const hh::Shape& sh = g_hold.WeaponShape();
        out->midM = (float)(0.5 * (sh.butt + sh.tip)) - (g_shaftKey == g_key ? g_boneAlong : 0.0f);
    }
    g_stowed.store(out->stowed);
    g_holding.store(holding);
    {   // by the game's slot: who holds which weapon (the throw gesture tells a hand-over from a throw by it)
        const bool second = wt.second && g_hold2.Placed();
        g_holders[slot].store((g_out[0].holds ? 1u : 0u) | (g_out[1].holds ? 2u : 0u));
        g_holders[slot ^ 1].store(second ? (g_out2[0].holds ? 1u : 0u) | (g_out2[1].holds ? 2u : 0u) : 0u);
        g_firstSlot.store(slot);
        g_secondPlaced.store(second);
    }
    {   // [weight] strike_by_force: the slots whose weapon a hand holds now (hit by touch judges those by force)
        unsigned fs = g_hold.Held() ? 1u << slot : 0u;
        if (wt.second && g_hold2.Placed() && g_hold2.Held()) fs |= 1u << (slot ^ 1);   // (by the item's slot: hit by touch's blade of it, whichever hand holds it)
        g_forceSlots.store(fs);
    }
    g_carrying.store(g_free ? -1 : out->main);
    g_wielding.store(g_hold.Flying() ? 1u << g_hold.FlyHand() : g_hold.Held() ? (g_out[0].holds ? 1u : 0u) | (g_out[1].holds ? 2u : 0u) : 0u);   // the shield keeps to the other forearm ([hands] shoulder_draw, vr/shield_arm.h)
    g_placed.store(true);
    ReleaseSRWLockExclusive(&g_lock);
}

void SetShaft(const float at[3], const float dir[3]) {
    const V3 a{at[0], at[1], at[2]}, d{dir[0], dir[1], dir[2]};
    const double l = heft::Len(d);
    if (!(l > 0.5 && l < 2.0) || !(heft::Len(a) < 1.0)) return;   // not a measurement
    // Where the game holds it is where the shaft passes the fist, not the bone's origin: a staff's bone
    // stood 20 cm up the shaft from the palm (2026-10-13), and the whole shape with it.
    const V3 u = heft::Mul(d, 1.0 / l);
    const V3 onShaft = heft::Sub(a, heft::Mul(u, heft::Dot(a, u)));
    AcquireSRWLockExclusive(&g_lock);
    const hh::Line line{onShaft, u};
    if (g_shaftKey != g_key || heft::Len(heft::Sub(line.at, g_shaft.at)) > 1e-4 || heft::Dot(line.dir, g_shaft.dir) < 0.999999) {
        if (g_shaftKey != g_key)
            LogF("vrcam: weight - its shaft passes %.1f cm from the palm (its bone %.1f cm along it), toward the tip %.2f %.2f %.2f in the "
                 "controller's frame", heft::Len(onShaft) * 100.0, heft::Dot(a, u) * 100.0, line.dir.x, line.dir.y, line.dir.z);
        g_shaft = line;
        g_shaftKey = g_key;
        g_shaftNew = true;
        g_boneAlong = (float)heft::Dot(a, u);
    }
    ReleaseSRWLockExclusive(&g_lock);
}

bool LastThrow(ThrowOutcome* out) {
    AcquireSRWLockShared(&g_lock);
    const ThrowOutcome o = g_outcome;
    ReleaseSRWLockShared(&g_lock);
    const bool fresh = o.valid && GetTickCount64() - o.tickMs <= 10000;
    if (out) *out = o;
    return fresh;
}

void SetHaptic(HapticFn fn) { g_haptic.store(fn); }
void SetClashSound(ClashSoundFn fn) { g_clashSound.store(fn); }
void SetCleave(const CleaveSettings& c) {
    AcquireSRWLockExclusive(&g_cleaveLock);
    const CleaveSettings was = g_cleave;
    g_cleave = c;
    ReleaseSRWLockExclusive(&g_cleaveLock);
    if (c.on == was.on && c.over == was.over && c.cost == was.cost && c.minSpeed == was.minSpeed) return;
    if (c.on)
        LogF("vrcam: weight - cleave on: a body that takes %.1fx the weapon's strike threshold at the arrival, the blade coming in at %.1f m/s or faster, is cut "
             "through, each one spends %.1fx of the stroke's force (blades only - a staff or a maul stops; never a thrust, a shield or a weapon)", c.over, c.minSpeed, c.cost);
    else
        LogF("vrcam: weight - cleave off: every body stops the weapon");
}
void SetHeroModeSource(HeroModeFn fn) { g_heroMode.store(fn); }
void SetLibrarySource(LibraryFn ensure) { g_libraryEnsure.store(ensure); }
void SetGripLibrarySource(LibraryFn ensure) { g_gripEnsure.store(ensure); }
void SetActionFrameSource(ActionFrameFn fn) { g_actionFrame.store(fn); }
void SetBodiesSource(BodiesFn fn) { g_bodiesSrc.store(fn); }
void SetStabSource(StabFn fn) { g_stabSrc.store(fn); }
void SetStabNoteSink(StabNoteFn fn) { g_stabNoteSink.store(fn); }
void SetBodyPushSink(BodyPushFn fn) { g_bodyPush.store(fn); }
bool LastContact(ContactNote* out) {
    AcquireSRWLockShared(&g_lock);
    const ContactNote n = g_contactNote;
    ReleaseSRWLockShared(&g_lock);
    if (out) *out = n;
    return n.bodies > 0;
}

void SetDrawnShaft(const float at[3], const float dir[3], float butt, float tip, float boneAlong) {
    const V3 a{at[0], at[1], at[2]}, d{dir[0], dir[1], dir[2]};
    const double l = heft::Len(d);
    if (!(l > 0.5 && l < 2.0) || !(heft::Len(a) < 1.5) || !std::isfinite(butt + tip + boneAlong) || !(tip - butt > 0.05f && tip - butt < 6.0f)) return;   // not a measurement
    if (g_unmeasured.exchange(nullptr)) LogF("vrcam: weight - the drawn shaft is measured again");
    AcquireSRWLockExclusive(&g_lock);
    g_shaft = hh::Line{a, heft::Mul(d, 1.0 / l)};
    g_shaftKey = g_key;
    g_drawnButt = butt;
    g_drawnTip = tip;
    g_boneAlong = boneAlong;
    g_drawnNew = true;
    ReleaseSRWLockExclusive(&g_lock);
}

void SetDrawnShaft2(const float at[3], const float dir[3], float butt, float tip, float boneAlong) {
    const V3 a{at[0], at[1], at[2]}, d{dir[0], dir[1], dir[2]};
    const double l = heft::Len(d);
    if (!(l > 0.5 && l < 2.0) || !(heft::Len(a) < 1.5) || !std::isfinite(butt + tip + boneAlong) || !(tip - butt > 0.05f && tip - butt < 6.0f)) return;   // not a measurement
    AcquireSRWLockExclusive(&g_lock);
    const bool first = g_shaft2Key != g_key2;
    g_shaft2 = hh::Line{a, heft::Mul(d, 1.0 / l)};
    g_shaft2Key = g_key2;
    g_drawn2Butt = butt;
    g_drawn2Tip = tip;
    g_drawn2New = true;
    ReleaseSRWLockExclusive(&g_lock);
    if (first && g_key2) LogF("vrcam: weight - the second weapon's shaft is measured off its drawn bone (a hand that takes it closes on the drawn weapon)");
}

void SetDrawnHeadParts(const float* parts, int n) {
    AcquireSRWLockExclusive(&g_lock);
    NoteDrawnHead(g_head[0], parts, n, g_key);
    ReleaseSRWLockExclusive(&g_lock);
}

void SetDrawnHeadParts2(const float* parts, int n) {
    AcquireSRWLockExclusive(&g_lock);
    NoteDrawnHead(g_head[1], parts, n, g_key2);
    ReleaseSRWLockExclusive(&g_lock);
}

// (one capsule: a, b, radius; none for null ends or no radius)
static int OnePart(const float a[3], const float b[3], float radius, float out[7]) {
    if (!a || !b || !(radius > 0.0f)) return 0;
    for (int i = 0; i < 3; ++i) { out[i] = a[i]; out[3 + i] = b[i]; }
    out[6] = radius;
    return 1;
}
void SetDrawnHead(const float a[3], const float b[3], float radius) {
    float p[7];
    SetDrawnHeadParts(p, OnePart(a, b, radius, p));
}
void SetDrawnHead2(const float a[3], const float b[3], float radius) {
    float p[7];
    SetDrawnHeadParts2(p, OnePart(a, b, radius, p));
}

void Say(const char* text) { LogF("vrcam: weight - %s", text ? text : "?"); }

void NoteShaftUnmeasured(const char* why) {
    if (g_unmeasured.exchange(why) != why) LogF("vrcam: weight - the drawn shaft is NOT measured: %s - the model's line stands where it was (not the drawn weapon's)", why ? why : "?");
}

void SetBackBody(const float body[3], const float rot[4]) {
    if (!std::isfinite(body[0] + body[1] + body[2] + rot[0] + rot[1] + rot[2] + rot[3])) return;
    AcquireSRWLockExclusive(&g_lock);
    memcpy(g_backBody, body, sizeof g_backBody);
    memcpy(g_backRot, rot, sizeof g_backRot);
    g_backNew = true;
    ReleaseSRWLockExclusive(&g_lock);
}
void SetBackBody2(const float body[3], const float rot[4]) {
    if (!std::isfinite(body[0] + body[1] + body[2] + rot[0] + rot[1] + rot[2] + rot[3])) return;
    AcquireSRWLockExclusive(&g_lock);
    memcpy(g_back2Body, body, sizeof g_back2Body);
    memcpy(g_back2Rot, rot, sizeof g_back2Rot);
    g_back2New = true;
    ReleaseSRWLockExclusive(&g_lock);
}

void NoteGripFit(const GripFitNote& n) {
    if (n.side < 0 || n.side > 1 || !n.settled || g_fitLogged[n.side].load() == n.grabs) return;
    g_fitLogged[n.side].store(n.grabs);
    const bool tuned = n.tune[0] != 0.0f || n.tune[1] != 0.0f || n.tune[2] != 0.0f || n.tune[3] != 0.0f || n.tune[4] != 0.0f || n.tune[5] != 0.0f;
    char t[120] = "", miss[96] = "", like[160] = "";
    if (tuned) snprintf(t, sizeof t, "; its own corrections %.1f %.1f %.1f cm, %.0f %.0f %.0f deg", n.tune[0], n.tune[1], n.tune[2], n.tune[3], n.tune[4], n.tune[5]);
    if (n.missCm > 1.0f) snprintf(miss, sizeof miss, "; the palm ends %.1f cm short of it (the arm's reach)", (double)n.missCm);
    if (n.likeBase) snprintf(like, sizeof like, ", sitting on it as the game's own grip has the base hand (the palm %.1f cm across and %.1f cm up from the axis, the knuckles leaning %.0f %.0f %.0f deg)",
                             n.like[1], n.like[2], n.like[3], n.like[4], n.like[5]);
    if (n.snapped || n.aligned)
        LogF("vrcam: weight - grip fit, the %s hand takes the shaft: its palm was %.1f cm off the shaft's axis and its knuckles %.0f deg off the shaft -> laid on it%s%s%s", HandName(n.side),
             (double)n.offAxisCm, (double)n.alignDeg, like, t, miss);
    else
        LogF("vrcam: weight - grip fit, the %s hand (the game's grip, kept)%s%s", HandName(n.side), t, miss);
}

bool LastStrength(StrengthNote* out) {
    AcquireSRWLockShared(&g_lock);
    const StrengthNote n = g_strengthNote;
    ReleaseSRWLockShared(&g_lock);
    if (out) *out = n;
    return n.on;
}

bool BackStowed() { return g_stowed.load() || GetTickCount64() < g_takenUntil.load(); }

bool HoldsWeapon(int side) { return side >= 0 && side < 2 && (g_holding.load() & (1u << side)) != 0; }
int CarryingHand() { return g_carrying.load(); }
unsigned WieldingHands() { return g_wielding.load(); }
bool Placed() { return g_placed.load(); }
float SteelKg() { return g_placed.load() ? g_steelKg.load() : 0.0f; }
bool SecondLetGo() { return g_placed.load() && g_secondLetGo.load(); }
unsigned ForceSlots() { return g_placed.load() ? g_forceSlots.load() : 0u; }
int SecondHand() { return g_placed.load() ? g_secondHand.load() : -1; }
unsigned HoldersOf(int slot) { return g_placed.load() && slot >= 0 && slot < 2 ? g_holders[slot].load() : 0u; }
int FirstSlot() { return g_placed.load() ? g_firstSlot.load() : -1; }
bool SecondPlaced() { return g_placed.load() && g_secondPlaced.load(); }
void SecondThrown() { g_secondThrownAt.store(GetTickCount64()); }
int LiveStrikes(LiveStrike* out, int cap) {
    int n = 0;
    AcquireSRWLockShared(&g_lock);
    const ULONGLONG nowMs = GetTickCount64();
    for (int w = 0; w < 2; ++w) {
        if (!g_liveAt[w] || nowMs - g_liveAt[w] > 100) continue;
        for (int j = 0; j < g_nLive[w] && n < cap; ++j) out[n++] = g_live[w][j];
    }
    ReleaseSRWLockShared(&g_lock);
    return n;
}
bool HandOnShaft(int side, float* aheadM) {
    if (side < 0 || side > 1 || !g_placed.load() || !g_handOn[side].load()) return false;
    if (aheadM) *aheadM = g_handAhead[side].load();
    return true;
}
int Blocked(uint32_t id, uint64_t withinMs) { return Within(g_blocks, id, withinMs); }
bool Reached(uint32_t id, uint64_t withinMs) { return Within(g_reached, id, withinMs) != 0; }
void Knock(const float dirRender[3], float metres, int ms) {
    if (!dirRender || !std::isfinite(dirRender[0] + dirRender[1] + dirRender[2] + metres) || !(metres > 0.0f) || ms <= 0) return;
    std::lock_guard<std::mutex> lock(g_knockMutex);
    for (int k = 0; k < 3; ++k) g_knockDir[k] = dirRender[k];
    g_knockM = std::min(metres, 0.2f);
    g_knockMs = std::min(ms, 1000);
    g_knockAt = GetTickCount64();
}
int TakeZone(int side, float* cm) {
    const uint32_t v = g_takeZone[side & 1].load();
    if (cm) *cm = (v >> 8) >= 0xFFFFFFu ? 1e6f : (float)(v >> 8) * 0.1f;
    return (int)(v & 0xFF);
}

bool Aim(AimInfo* out) {
    AimInfo a;
    AcquireSRWLockShared(&g_lock);
    const bool fresh = g_hold.Placed() && GetTickCount64() - g_ends.at < 500;   // g_ends is made on every step
    const int carrying = g_carrying.load();
    if (fresh && (carrying == 0 || carrying == 1)) {
        a.hand = carrying;
        const V3 d = g_hold.ShaftInHand(carrying);
        a.shaft[0] = (float)d.x; a.shaft[1] = (float)d.y; a.shaft[2] = (float)d.z;
    }
    const uint32_t holding = fresh ? g_holding.load() : 0u;
    a.holds[0] = (holding & 1u) != 0;
    a.holds[1] = (holding & 2u) != 0;
    ReleaseSRWLockShared(&g_lock);
    if (out) *out = a;
    return a.hand >= 0 && std::isfinite(a.shaft[0] + a.shaft[1] + a.shaft[2]);
}

bool ShaftEnds(float butt[3], float tip[3]) {
    AcquireSRWLockShared(&g_lock);
    const EndsOut e = g_ends;
    const bool on = g_hold.Placed();
    ReleaseSRWLockShared(&g_lock);
    if (!on || GetTickCount64() - e.at > 300) return false;
    memcpy(butt, e.butt, sizeof e.butt);
    memcpy(tip, e.tip, sizeof e.tip);
    return true;
}

#else
void Apply(skel::Input*, Out* out) { *out = Out{}; }
void SetShaft(const float*, const float*) {}
void SetDrawnShaft(const float*, const float*, float, float, float) {}
void SetDrawnShaft2(const float*, const float*, float, float, float) {}
void SetDrawnHead(const float*, const float*, float) {}
void SetDrawnHead2(const float*, const float*, float) {}
void SetDrawnHeadParts(const float*, int) {}
void SetDrawnHeadParts2(const float*, int) {}
bool LastThrow(ThrowOutcome* out) { if (out) *out = ThrowOutcome{}; return false; }
void SetHaptic(HapticFn) {}
void SetClashSound(ClashSoundFn) {}
void SetCleave(const CleaveSettings&) {}
void SetHeroModeSource(HeroModeFn) {}
void SetLibrarySource(LibraryFn) {}
void SetGripLibrarySource(LibraryFn) {}
void SetActionFrameSource(ActionFrameFn) {}
void SetBodiesSource(BodiesFn) {}
void SetStabSource(StabFn) {}
void SetStabNoteSink(StabNoteFn) {}
void SetBodyPushSink(BodyPushFn) {}
bool LastContact(ContactNote* out) { if (out) *out = ContactNote{}; return false; }
void SetBackBody(const float*, const float*) {}
void SetBackBody2(const float*, const float*) {}
void NoteGripFit(const GripFitNote&) {}
void NoteShaftUnmeasured(const char*) {}
void Say(const char*) {}
bool LastStrength(StrengthNote* out) { if (out) *out = StrengthNote{}; return false; }
bool BackStowed() { return false; }
bool HoldsWeapon(int) { return false; }
int CarryingHand() { return -1; }
unsigned WieldingHands() { return 0u; }
bool Placed() { return false; }
float SteelKg() { return 0.0f; }
bool SecondLetGo() { return false; }
unsigned ForceSlots() { return 0u; }
int SecondHand() { return -1; }
unsigned HoldersOf(int) { return 0u; }
int FirstSlot() { return -1; }
bool SecondPlaced() { return false; }
void SecondThrown() {}
int LiveStrikes(LiveStrike*, int) { return 0; }
bool HandOnShaft(int, float*) { return false; }
int Blocked(uint32_t, uint64_t) { return 0; }
bool Reached(uint32_t, uint64_t) { return false; }
void Knock(const float*, float, int) {}
int TakeZone(int, float* cm) { if (cm) *cm = 1e6f; return 0; }
bool ShaftEnds(float*, float*) { return false; }
bool Aim(AimInfo* out) { if (out) *out = AimInfo{}; return false; }
void SetLog(LogFn) {}
void SetThrowSource(GripIsThrowsFn, LastThrowFn) {}
void SetThrowSide(ThrowSideFn) {}
#endif

}  // namespace weight
