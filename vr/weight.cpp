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
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "hold.h"
#include "shoulder_grip.h"
#include "shoulder_swap.h"
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
std::atomic<bool> g_placed{false};    // a weapon held that way
struct EndsOut { float butt[3], tip[3]; ULONGLONG at; };
EndsOut g_ends{};
std::atomic<LogFn> g_log{nullptr};
ThrowRelease g_throw;                              // a grip release that is a throw (vr/weight_throw.h); under g_lock
ThrownFly g_thrownFly;                             // a thrown weapon's visit, until it is done
bool g_thrownFlies = false;                        // ... and it is a potion whose model flies to its landing (wt.thrownPotion && wt.potionFlies, latched at the release)
EmptyHand g_emptyHand;                             // the hand stays empty while the game throws
std::atomic<HeroModeFn> g_heroMode{nullptr};
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
        g_holding.store(0);
        g_carrying.store(-1);
        g_placed.store(false);
        g_stowed.store(false);
        g_takenUntil.store(0);
        g_zoneTick[0].Reset();
        g_zoneTick[1].Reset();
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
    if (wt.gravityFeel) hh::UseGravityFeel(s);
    {   // the feel's sliders, told when they change (the log shows what a test ran with)
        static float lastG = -1.0f, lastD = -1.0f, lastS = -1.0f;
        static int lastF = -1;
        if (wt.gravity != lastG || wt.damping != lastD || wt.strength != lastS || (int)wt.gravityFeel != lastF) {
            lastG = wt.gravity; lastD = wt.damping; lastS = wt.strength; lastF = wt.gravityFeel ? 1 : 0;
            LogF("vrcam: weight - gravity %.0f %%, damping %.0f %%, hands' strength %.0f %%, %s wrists", wt.gravity * 100.0f, wt.damping * 100.0f,
                 wt.strength * 100.0f, wt.gravityFeel ? "soft (gravity feel)" : "stiff (the old)");
        }
    }
    s.grabDistance = wt.grabM;
    s.letGo = wt.letGoM;
    s.fist = {wt.fist[0], wt.fist[1], wt.fist[2]};
    s.stowAfter = wt.afterDrop >= 1 ? (double)std::max(0.0f, wt.stowAfterS) : -1.0;   // 0: it lies on the floor as ever
    s.recallAny = wt.recallAny;
    s.slide = wt.slide;
    s.backTake = std::clamp((double)wt.backTakeM, 0.0, 0.8);
    s.floor = weightfloor::RoomFloorY(in->headRoom[1], wt.headAboveGroundM);   // the ground the player sees, not the room's floor

    const Head head = HeadOf(*in);
    hh::HandIn hi[2];
    for (int side = 0; side < 2; ++side) {
        const int at = (in->handsValid & (1u << side)) ? side : slot;   // a hand not tracked: its pose stands in, unused
        hi[side].tracked = (in->handsValid & (1u << side)) != 0;
        hi[side].pose = ToRoom(head, in->hand[at], in->rot[at]);
        hi[side].grip = wt.haveRawGrip ? wt.rawGrip[side] : in->grip[side];   // (a grip taken from the layout reads as let go in in->grip)
        hi[side].busy = wt.busy[side] || (side != slot && wt.otherBusy);
        hi[side].claimed = wt.claimed[side];
        // behind a shoulder: the swap gesture's volume (vr/shoulder_swap.h; the hands are in the head's turn frame already)
        hi[side].behind = wt.afterDrop >= 1 && hi[side].tracked && shoulderswap::Near(in->hand[side]);
        hi[side].fistLine = FistLine(*in, side);
    }
    const uint64_t key = ((uint64_t)(uint32_t)in->weaponType << 32) | ((uint64_t)wt.kind << 8) | (wt.twoHanded ? 2u : 0u) | (uint64_t)slot;
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
        LogF("vrcam: weight - %s%s in the %s hand, %.2f kg (x %.2f), the other hand %s%s", wt.twoHanded ? "two-handed " : "",
             hh::KindName((hh::Kind)wt.kind), HandName(slot), hh::ShapeOf((hh::Kind)wt.kind, wt.twoHanded).Mass(), s.weight,
             wt.otherBusy ? "holds something of its own" : "may take it with its grip", why);
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
        const int mainWas = g_hold.Main();
        const bool heldWas = g_hold.Held(), flyWas = g_hold.Flying(), stowWas = g_hold.Stowed();
        // on the back: where the skeleton drew it last pose (a frame late is fine); the weapon flies from there when taken
        if (stowWas && g_backNew) g_hold.SetStowPose(ToRoom(head, g_backBody, g_backRot));
        g_backNew = false;
        g_hold.Step(hi, dt, s, g_out);
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
            LogF("vrcam: weight - the %s grip calls it back", HandName(slot));
        }
        {   // [weight] take_haptic: a hand enters the take zone behind a shoulder while the weapon is away - a short pulse, so the hand feels where to grab
            const HapticFn pulse = g_haptic.load();
            const ULONGLONG nowMs = GetTickCount64();
            for (int side = 0; side < 2; ++side) {
                // the take zone: behind a shoulder, or at the weapon where it hangs (a fist within back_take_cm of its drawn shaft)
                const double dist = g_hold.StowDistance(side, hi[side], s);
                const bool atBack = s.backTake > 0.0 && dist <= s.backTake;
                const bool tick = g_zoneTick[side].Step(g_hold.Stowed(), hi[side].behind || atBack, hi[side].busy, nowMs);
                if (tick && pulse && wt.takeHaptic > 0.0f) pulse(side, std::min(1.0f, wt.takeHaptic), kTickMs);
                // a grip squeezed while the weapon is away and still not taken: say why, so the next test shows it
                const float gv = hi[side].grip;
                const bool down = gv > 0.6f ? true : gv < 0.4f ? false : g_gripWas[side];
                if (down && !g_gripWas[side] && stowWas && g_hold.Stowed()) {
                    const char* why = !hi[side].tracked ? "the hand is not tracked"
                                    : hi[side].busy ? "the hand holds something of its own (a belt potion, or the shield's hand)"
                                    : hi[side].behind || atBack ? "(inside the zone: it should have been taken)"
                                    : hi[side].claimed ? "the grip is the weapon swap's claim, and the hand is outside the zone"
                                    : (side == g_hold.First() || s.recallAny) ? "(a call-back: it should have been called)"
                                    : "outside the zone, and only the hand the game holds it in calls it from afar (recall_any_hand is off)";
                    LogF("vrcam: stowed weapon: the %s hand is %.0f cm from it, zone %.0f cm, behind a shoulder %s - not taken: %s", HandName(side), dist * 100.0,
                         s.backTake * 100.0, hi[side].behind ? "yes" : "no", why);
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
    out->stowed = g_hold.Stowed();
    out->hidden = g_emptyHand.Active();
    {   // the middle of the shaft from the weapon bone: the shape counts from where the fist passes the shaft, the bone stands boneAlong ahead of that
        const hh::Shape& sh = g_hold.WeaponShape();
        out->midM = (float)(0.5 * (sh.butt + sh.tip)) - (g_shaftKey == g_key ? g_boneAlong : 0.0f);
    }
    g_stowed.store(out->stowed);
    g_holding.store(holding);
    g_carrying.store(g_free ? -1 : out->main);
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
void SetActionFrameSource(ActionFrameFn fn) { g_actionFrame.store(fn); }

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

bool BackStowed() { return g_stowed.load() || GetTickCount64() < g_takenUntil.load(); }

bool HoldsWeapon(int side) { return side >= 0 && side < 2 && (g_holding.load() & (1u << side)) != 0; }
int CarryingHand() { return g_carrying.load(); }
bool Placed() { return g_placed.load(); }

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
void SetActionFrameSource(ActionFrameFn) {}
void SetBackBody(const float*, const float*) {}
void NoteGripFit(const GripFitNote&) {}
void NoteShaftUnmeasured(const char*) {}
void Say(const char*) {}
bool BackStowed() { return false; }
bool HoldsWeapon(int) { return false; }
int CarryingHand() { return -1; }
bool Placed() { return false; }
bool ShaftEnds(float*, float*) { return false; }
bool Aim(AimInfo* out) { if (out) *out = AimInfo{}; return false; }
void SetLog(LogFn) {}
void SetThrowSource(GripIsThrowsFn, LastThrowFn) {}
#endif

}  // namespace weight
