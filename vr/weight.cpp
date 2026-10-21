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

#include "grip_zone.h"
#include "hero_strength.h"
#include "world_anchor.h"
#include "hold.h"
#include "shoulder_draw.h"
#include "shoulder_grip.h"
#include "shoulder_swap.h"
#include "slide.h"
#include "slide_kinds.h"
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
bool g_drawnNew = false;
std::atomic<uint32_t> g_holding{0};   // bit per hand on the weapon
std::atomic<int> g_carrying{-1};      // the hand that carries it
std::atomic<unsigned> g_wielding{0};  // the hands that hold it or that it flies into, a bit each
std::atomic<bool> g_placed{false};    // a weapon held that way
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
ContactNote g_contactNote;                         // the last step's (under g_lock)
uint32_t g_contactWas = 0;                         // the body it touched on the step before (0 none): a pulse a strike, not a step
ULONGLONG g_contactPulseAt = 0, g_contactToldAt = 0;
std::array<int, monsterbodies::kMax> g_capBone{};   // the bone of each capsule handed to the hold this step (-1: a monster's own capsule)
struct BodiesCost { double us = 0, worst = 0; unsigned steps = 0, strikes = 0; ULONGLONG since = 0; } g_bodiesCost;   // told once a minute while the bodies are on
std::atomic<ActionFrameFn> g_actionFrame{nullptr};
std::atomic<GripIsThrowsFn> g_gripIsThrows{nullptr};
std::atomic<LastThrowFn> g_lastThrow{nullptr};
// [weight] to_back: where the skeleton drew the stowed weapon's body frame (told by SetBackBody; BodyWalk's frame), the bone's distance along
// the shaft from where the fist passes it (SetShaft), and what the swap gesture asks (BackStowed).
float g_backBody[3] = {}, g_backRot[4] = {0, 0, 0, 1};
bool g_backNew = false;
float g_boneAlong = 0.0f;
std::atomic<bool> g_stowed{false};
std::atomic<ULONGLONG> g_takenUntil{0};
std::atomic<uint32_t> g_takeZone[2] = {};   // per hand, from the last step: where a grip pressed now would take the weapon (hh::Hold::TakeZone): the zone in the low byte, the fist's distance in mm above
std::atomic<HapticFn> g_haptic{nullptr};
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

void Apply(skel::Input* in, Out* out) {
    *out = Out{};
    const skel::Input::Weight& wt = in->weight;
    const int slot = wt.slot & 1;
    AcquireSRWLockExclusive(&g_lock);
    if (!wt.on || in->mode != 2 || !(in->handsValid & (1u << slot))) {
        if (g_hold.Placed()) LogF("vrcam: weight - off (%s)", !wt.on ? "no weapon held that way" : "no controller for its hand");
        g_hold.Reset();
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
    hh::HandIn hi[2];
    int drawZone[2] = {-1, -1};   // the shoulder each hand is behind when it may draw the weapon (0 right, 1 left)
    bool wearsShieldAt[2] = {false, false};   // the hand is busy with the shield (for the log's reason)
    for (int side = 0; side < 2; ++side) {
        const int at = (in->handsValid & (1u << side)) ? side : slot;   // a hand not tracked: its pose stands in, unused
        hi[side].tracked = (in->handsValid & (1u << side)) != 0;
        hi[side].pose = ToRoom(head, in->hand[at], in->rot[at]);
        hi[side].grip = wt.haveRawGrip ? wt.rawGrip[side] : in->grip[side];   // (a grip taken from the layout reads as let go in in->grip)
        hi[side].trigger = wt.haveRawTrigger ? wt.rawTrigger[side] : -1.0f;
        // the other hand holds something of its own (the shield strapped to it). With [hands] shoulder_draw handling the shield nobody is busy with it: the hand that takes the
        // weapon is the one the shield leaves (it goes to the other forearm as the weapon is drawn)
        // (the hand that wears the shield is busy with it - unless the shield FOLLOWS the sword: it moves to the other arm as the weapon is taken - or is put away)
        const bool wearsShield = wt.shieldArm == 0 || wt.shieldArm == 1 ? (!wt.shieldFollow && side == wt.shieldArm) : wt.shieldArm == 2 ? false : (side != slot && wt.otherBusy);
        wearsShieldAt[side] = wearsShield;
        hi[side].busy = wt.busy[side] || wearsShield;
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
            LogF("vrcam: weight - the hero's strength: Str %d x %.0f %% (his class) = %.0f against the weapon's requirement %d%s -> ratio %.2f, the steel x%.2f, the hands' strength x%.2f (strength matters %.0f %%)",
                 wt.heroStr, (double)wt.strengthClassPct, hstr.capacity, (int)std::lround(hstr.req), hstr.reqFromGame ? " (the game's table)" : " (typical for its kind: the table is not read)", hstr.ratio,
                 hstr.steel, hstr.hands, (double)wt.strengthEffect);
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
            ti.owned = owns && last && thrownKind && owns();
            ti.holds = g_out[0].holds;
            ti.grip = hi[0].grip;
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
            const bool carry = s.drawMode && (g_hold.Away() || g_hold.Flying());
            if (!carry) g_worldTold = false;
            if (carry && !(wt.worldOk && now.ok)) {
                if (!g_worldTold) { g_worldTold = true; LogF("vrcam: weight - the camera's frame in the world is not known: a weapon on the floor moves with the hero"); }
            } else if (carry && g_worldWas.ok) {
                const worldanchor::Move mv = worldanchor::Between(g_worldWas, now);
                const double jump = std::hypot(mv.sx, mv.sz);
                if (mv.ok && jump < 1.5 && std::fabs(mv.delta) < 1.0) {   // (a jump of more than that is a new area or a teleport: the weapon stays in the room's place)
                    if (jump > 1e-4 || std::fabs(mv.delta) > 1e-4) g_hold.Transport(heft::QFromAxisAngle({0, 1, 0}, mv.delta), {mv.sx, 0.0, mv.sz});
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
                monsterbodies::View view;
                for (int i = 0; i < 3; ++i) { view.eye[i] = wt.worldEye[i]; view.camR[i] = wt.worldCamR[i]; view.camF[i] = wt.worldCamF[i]; }
                view.upm = std::clamp(wt.worldUpm, 1.0f, 12.0f);   // (the touch's render metres are over the same, clamped so)
                const float ident[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                for (int i = 0; i < snap.n && nBodies < monsterbodies::kMax; ++i) {
                    float a[3], b[3];
                    if (!monsterbodies::RenderToHead(view, snap.b[i].bottom, a) || !monsterbodies::RenderToHead(view, snap.b[i].top, b)) continue;
                    g_capBone[nBodies] = snap.b[i].rig ? snap.b[i].bone : -1;   // (the debug view tints the limb the weapon is on)
                    heft::contact::Capsule& c = caps[nBodies++];
                    c.a = ToRoom(head, a, ident).pos;
                    c.b = ToRoom(head, b, ident).pos;
                    c.radius = snap.b[i].rig ? snap.b[i].radius : monsterbodies::CoreRadius(snap.b[i].radius);   // (a limb is as thick as its mesh; the touch's capsule less its margin)
                    c.id = snap.b[i].id;
                }
            }
            g_hold.SetBodies(caps, nBodies);
        } else {
            g_hold.SetBodies(nullptr, 0);
        }
        s.bodies = nBodies > 0;
        g_hold.Step(hi, dt, s, g_out);
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
            const ULONGLONG nowMs = GetTickCount64();
            const bool strike = c.bodies > 0 && c.id != g_contactWas;   // a new body met (not every step it rests on one)
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
                    LogF("vrcam: weight - the weapon struck monster %u's body at %.1f m/s (%.1f cm in, put back on it): it %s; %d bod%s near, %.0f us", c.id, c.approach, c.depth * 100.0,
                         c.slide > 0.3 ? "glances off along it" : "stops on it", nBodies, nBodies == 1 ? "y" : "ies", (double)note.costUs);
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
void SetHeroModeSource(HeroModeFn fn) { g_heroMode.store(fn); }
void SetLibrarySource(LibraryFn ensure) { g_libraryEnsure.store(ensure); }
void SetGripLibrarySource(LibraryFn ensure) { g_gripEnsure.store(ensure); }
void SetActionFrameSource(ActionFrameFn fn) { g_actionFrame.store(fn); }
void SetBodiesSource(BodiesFn fn) { g_bodiesSrc.store(fn); }
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
bool LastThrow(ThrowOutcome* out) { if (out) *out = ThrowOutcome{}; return false; }
void SetHaptic(HapticFn) {}
void SetHeroModeSource(HeroModeFn) {}
void SetLibrarySource(LibraryFn) {}
void SetGripLibrarySource(LibraryFn) {}
void SetActionFrameSource(ActionFrameFn) {}
void SetBodiesSource(BodiesFn) {}
bool LastContact(ContactNote* out) { if (out) *out = ContactNote{}; return false; }
void SetBackBody(const float*, const float*) {}
void NoteGripFit(const GripFitNote&) {}
void NoteShaftUnmeasured(const char*) {}
void Say(const char*) {}
bool LastStrength(StrengthNote* out) { if (out) *out = StrengthNote{}; return false; }
bool BackStowed() { return false; }
bool HoldsWeapon(int) { return false; }
int CarryingHand() { return -1; }
unsigned WieldingHands() { return 0u; }
bool Placed() { return false; }
int TakeZone(int, float* cm) { if (cm) *cm = 1e6f; return 0; }
bool ShaftEnds(float*, float*) { return false; }
bool Aim(AimInfo* out) { if (out) *out = AimInfo{}; return false; }
void SetLog(LogFn) {}
void SetThrowSource(GripIsThrowsFn, LastThrowFn) {}
#endif

}  // namespace weight
