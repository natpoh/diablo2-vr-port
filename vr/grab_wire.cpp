// grab_wire.cpp - see grab_wire.h.

#include "grab_wire.h"

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
#include <mutex>

#include "grab_bodies.h"
#include "grab_lib.h"
#include "grip_lib.h"   // [weight] grab_wrap: the grip library lays the holding hand on the body
#include "haptics.h"
#include "haptics_pattern.h"
#include "knock_props.h"   // [weight] grab_props: the props world's hands
#include "melee_touch.h"
#include "monster_bodies.h"
#include "player_bodies.h"   // (the other player's body: its id, for the log)

namespace grabwire {

namespace {

namespace gb = grabbodies;

std::atomic<LogFn> g_log{nullptr};
std::atomic<unsigned> g_holding{0};
std::atomic<unsigned> g_overBody{0};
std::mutex g_shiftMutex;
float g_shift[2][3] = {};
ULONGLONG g_shiftAt[2] = {};

void LogF(const char* fmt, ...) {
    const LogFn log = g_log.load();
    if (!log) return;
    char buf[1000];   // (the held grip's line is long)
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    log(buf);
}

double Now() {
    LARGE_INTEGER t, f;
    QueryPerformanceCounter(&t);
    QueryPerformanceFrequency(&f);
    return static_cast<double>(t.QuadPart) / static_cast<double>(f.QuadPart);
}

// The library's pulse (its grab / hover / release haptics), lifted for a weak motor (PS VR2) as the mod's other pulses are.
void Pulse(int side, const D2rgbHandOut& o) {
    if (!(o.hapticAmp > 0.0f)) return;
    const haptics::Request r = haptics::ForMotor(haptics::CurrentMotor(), o.hapticAmp, static_cast<uint32_t>(std::max(1.0f, o.hapticSec * 1000.0f)));
    if (r.amplitude > 0.0f) haptics::Pulse(haptics::FromSkel(side), r.amplitude, static_cast<int>(r.durationMs));
}

const char* SideName(int side) { return side ? "left" : "right"; }
// What a held body is, for the log: a monster (hostile), the other player (vr/player_bodies.h, its pose shared), or an NPC.
const char* WhoName(bool hostile, uint32_t id) { return hostile ? "monster" : playerbodies::IsPlayerId(id) ? "the other player" : "NPC"; }

// The skeleton hook's thread only.
double g_lastAt = 0.0;
bool g_wasOn = false;
bool g_bodiesWere = false;
D2rgbHandOut g_last[2];      // the last step's (a second rig posed along is drawn from it)
double g_since[2] = {};
float g_peakForce[2] = {}, g_peakGap[2] = {};
bool g_hostile[2] = {};
bool g_capOk[2] = {};                        // [weight] grab_wrap: the held body as drawn, in the hands' frame (the last step's)
float g_capA[2][3] = {}, g_capB[2][3] = {}, g_capR[2] = {};
float g_ctrl[2][3] = {};
float g_ctrl0[2][3] = {}, g_held0[2][3] = {};   // the controller and the held point when the hand took hold (the pull against what it moved)
float g_maxBend[2] = {};                        // degrees: the largest turn of a held limb's line this hold
bool g_pastArm[2] = {};                         // [weight] grab_arm_reach: this hand let go past its arm's reach - it takes nothing until its grip opens
D2rgbLink g_links[D2RGB_MAX_SWAYS];
int g_nLinks = 0;                     // the controller (the palm with nothing in it) of the last step, render metres
// A held grip, said once a second (at most kHoldLines): where the controller, the library's hand and the drawn palm are - the headset 0.183 said «рука не
// притягивается к телу нпс», and the next try must tell which of them is off
constexpr double kHoldEvery = 1.0;
constexpr int kHoldLines = 120;
double g_holdToldAt[2] = {-1e9, -1e9};
int g_holdLines = 0;
float Dist3(const float a[3], const float b[3]) { return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2])); }
// p's distance from the surface of the capsule a..b, r round it (< 0 inside)
float OffSurface(const float p[3], const float a[3], const float b[3], float r) {
    float ax[3], l2 = 0.0f, t = 0.0f;
    for (int k = 0; k < 3; ++k) { ax[k] = b[k] - a[k]; l2 += ax[k] * ax[k]; t += (p[k] - a[k]) * ax[k]; }
    t = l2 > 1e-10f ? std::clamp(t / l2, 0.0f, 1.0f) : 0.0f;
    float q[3];
    for (int k = 0; k < 3; ++k) q[k] = a[k] + ax[k] * t;
    return Dist3(p, q) - r;
}

// A grip try that took nothing, said once (throttled): the grip pressed from 0.6 - as the mod's other grips go - and its outcome when it reaches the library's 0.85
// (taken, or why not) or when it is let go short of that.
constexpr float kTryFrom = 0.6f, kTryOff = 0.4f, kLibraryGrip = 0.85f;   // (the library's own grip threshold: its controller's input map)
constexpr double kTryEvery = 1.5;   // s between two lines of a hand
constexpr int kTryLines = 60;       // in all
struct Try {
    bool on = false, judged = false;
    float peak = 0.0f;
    double toldAt = -1e9;
};
Try g_try[2];
int g_tryLines = 0;

void TellTry(int side, double now, const char* fmt, ...) {
    Try& t = g_try[side];
    if (g_tryLines >= kTryLines || now - t.toldAt < kTryEvery) return;
    t.toldAt = now;
    ++g_tryLines;
    char buf[300];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    LogF("grab: the %s hand's grip took nothing - %s%s", SideName(side), buf, g_tryLines == kTryLines ? " (no more of these lines this session)" : "");
}

// [weight] grab_props: the same hands in knock_props' world (its physics thread steps them, vr/knock_props.h): this pose's hands handed over, the last step's read
// back (render metres) - a hand that holds a prop is drawn on it and laid round its bounds as a capsule along their longest side.
D2rpHandOut g_prop[2];
bool g_propCapOk[2] = {};
float g_propCapA[2][3] = {}, g_propCapB[2][3] = {}, g_propCapR[2] = {};
double g_propSince[2] = {};
float g_propPeakGap[2] = {}, g_propPeakForce[2] = {}, g_propKg[2] = {};
float g_propOwnKg[2] = {};     // what the prop under the palm weighs (the hover's: once held, the library says what it weighs in the hand)
float g_propTakenY[2] = {}, g_propTopY[2] = {};   // the prop's height when taken, its highest since (physics metres, y up)
double g_propToldAt[2] = {};
constexpr double kPropHoldLineSec = 5.0;   // a line a second for the first seconds of each hold
uint32_t g_propId[2] = {};
std::atomic<uint32_t> g_propShareId[2] = {};   // [multiplayer] pose_share_held (PropOf): the prop each hand holds or held last
std::atomic<bool> g_propShareHeld[2] = {};
bool g_propsWere = false;
constexpr int kPropLines = 400;   // the grab's lines of the props, a session
int g_propLines = 0;

void PropLog(const char* fmt, ...) {
    if (g_propLines >= kPropLines) return;
    ++g_propLines;
    char buf[600];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    LogF("grab: %s%s", buf, g_propLines == kPropLines ? " (no more of the props' lines this session)" : "");
}

void RotateQ(const float q[4], const float v[3], float out[3]) {
    const float ux = q[0], uy = q[1], uz = q[2], w = q[3];
    const float tx = 2.0f * (uy * v[2] - uz * v[1]), ty = 2.0f * (uz * v[0] - ux * v[2]), tz = 2.0f * (ux * v[1] - uy * v[0]);
    out[0] = v[0] + w * tx + (uy * tz - uz * ty);
    out[1] = v[1] + w * ty + (uz * tx - ux * tz);
    out[2] = v[2] + w * tz + (ux * ty - uy * tx);
}

// The prop's bounds as the capsule the hand is laid round: along their longest side through their centre, its radius the mean of the other two half sides.
void PropCapsule(const D2rpHandOut& o, float a[3], float b[3], float* r) {
    int axis = 0;
    for (int k = 1; k < 3; ++k) if (o.half[k] > o.half[axis]) axis = k;
    const float h0 = o.half[(axis + 1) % 3], h1 = o.half[(axis + 2) % 3];
    *r = std::max(0.01f, 0.5f * (h0 + h1));
    const float along = std::max(0.0f, o.half[axis] - *r);
    float la[3] = {o.centre[0], o.centre[1], o.centre[2]}, lb[3] = {o.centre[0], o.centre[1], o.centre[2]};
    la[axis] -= along;
    lb[axis] += along;
    float ra[3], rb[3];
    RotateQ(o.q, la, ra);
    RotateQ(o.q, lb, rb);
    for (int k = 0; k < 3; ++k) { a[k] = o.p[k] + ra[k]; b[k] = o.p[k] + rb[k]; }
}

void PropsOff() {
    if (g_propsWere) knockprops::NoHands();
    g_propsWere = false;
    for (int side = 0; side < 2; ++side) {
        if (g_prop[side].state >= D2RP_HAND_TAKING) PropLog("the %s hand let go of the %s - the props' grab is off or not F4", SideName(side), knockprops::PropName(g_propId[side]));
        g_prop[side] = D2rpHandOut{};
        g_propCapOk[side] = false;
    }
}

void StepProps(skel::Input* in, const monsterbodies::View& view, const D2rgbHand* hands) {
    g_propsWere = true;
    const double now = Now();
    for (int side = 0; side < 2; ++side) {
        knockprops::GrabHand g;
        g.tracked = hands[side].tracked != 0;
        g.mayTake = in->grabFree[side] && g_last[side].state == D2RGB_EMPTY && !g_pastArm[side];   // (a body under the palm is the body grab's)
        g.grip = g_pastArm[side] ? 0.0f : in->grip[side];   // ([weight] grab_arm_reach: let go past the arm's reach)
        for (int k = 0; k < 3; ++k) g.pos[k] = hands[side].pos[k];
        for (int k = 0; k < 4; ++k) g.rot[k] = hands[side].rot[k];
        g.grabRadius = in->grabReachM;
        g.breakDistance = in->grabBreakM;
        g.strength = in->grabStrength;
        knockprops::NoteHand(side, g);
    }
    for (int side = 0; side < 2; ++side) {
        D2rpHandOut o;
        if (!knockprops::TakeHand(side, &o)) o = D2rpHandOut{};
        if (o.hapticAmp > 0.0f) {   // the library's grab / hover / release pulses, lifted for a weak motor as the bodies' are
            const haptics::Request r = haptics::ForMotor(haptics::CurrentMotor(), o.hapticAmp, static_cast<uint32_t>(std::max(1.0f, o.hapticSec * 1000.0f)));
            if (r.amplitude > 0.0f) haptics::Pulse(haptics::FromSkel(side), r.amplitude, static_cast<int>(r.durationMs));
        }
        if (o.state == D2RP_HAND_HOVER && !(o.events & D2RP_HEV_GRAB)) g_propOwnKg[side] = o.massKg;
        const float pull = D2RP_HAND_MAX_FORCE * std::max(0.05f, in->grabStrength);   // the hand's pull (the library's, at this strength)
        if (o.events & D2RP_HEV_GRAB) {
            g_propSince[side] = now;
            g_propPeakGap[side] = g_propPeakForce[side] = 0.0f;
            g_propId[side] = o.id;
            g_propKg[side] = o.massKg;
            g_propTakenY[side] = g_propTopY[side] = o.p[1];
            g_propToldAt[side] = now;
            const float own = g_propOwnKg[side] > o.massKg ? g_propOwnKg[side] : o.massKg;
            PropLog("the %s hand took a %s (entity %08X, %.0f x %.0f x %.0f cm by its bounds, %.2f kg, %.2f kg in the hand: its pull %.0f N, what it holds weighs at most "
                    "half) at the spot under the palm, %.0f cm from it", SideName(side), knockprops::PropName(o.id), o.id, o.half[0] * 200.0f, o.half[1] * 200.0f,
                    o.half[2] * 200.0f, own, o.massKg, pull, Dist3(hands[side].pos, o.point) * 100.0f);
        }
        if (o.state >= D2RP_HAND_TAKING) {
            g_propPeakGap[side] = std::max(g_propPeakGap[side], o.gap);
            g_propPeakForce[side] = std::max(g_propPeakForce[side], o.force);
            g_propTopY[side] = std::max(g_propTopY[side], o.p[1]);
            if (now - g_propToldAt[side] >= 1.0 && now - g_propSince[side] <= kPropHoldLineSec + 0.5) {   // a hold, second by second: does it come up with the hand
                g_propToldAt[side] = now;
                const float up = (o.p[1] - g_propTakenY[side]) * 100.0f, top = (g_propTopY[side] - g_propTakenY[side]) * 100.0f;
                PropLog("the %s hand holds the %s (%.0f s, %s): %.2f kg in the hand of its pull %.0f N, pulling %.0f N now; the controller %.1f cm from the hand; the %s %+.0f cm "
                        "from where it was taken (highest %+.0f cm) - %s", SideName(side), knockprops::PropName(o.id), now - g_propSince[side],
                        o.state == D2RP_HAND_HOLDING ? "in the hand" : "coming into it", o.massKg, pull, o.force, o.gap * 100.0f, knockprops::PropName(o.id), up, top,
                        top >= 10.0f ? "lifted" : o.force >= 0.95f * pull ? "NOT lifted: the hand pulls all it can" : "not lifted (yet)");
            }
        }
        if (o.events & (D2RP_HEV_RELEASE | D2RP_HEV_TORN | D2RP_HEV_LOST))
            PropLog("the %s hand let go of the %s (%.2f kg) after %.1f s - %s; it pulled at most %.0f N, held back at most %.0f cm", SideName(side), knockprops::PropName(g_propId[side]),
                    g_propKg[side], now - g_propSince[side],
                    (o.events & D2RP_HEV_TORN) ? "torn loose (pulled past the break distance)" : (o.events & D2RP_HEV_LOST) ? "lost (a new area, or the hand's tracking)" : "the grip was let go: thrown on as the hand moved",
                    g_propPeakForce[side], g_propPeakGap[side] * 100.0f);
        g_prop[side] = o;
        if (o.state >= D2RP_HAND_TAKING && o.id) g_propShareId[side].store(o.id);
        g_propShareHeld[side].store(o.state >= D2RP_HAND_TAKING && o.id != 0);
        g_propCapOk[side] = false;
        if (o.state >= D2RP_HAND_TAKING) {
            float a[3], b[3];
            PropCapsule(o, a, b, &g_propCapR[side]);
            g_propCapOk[side] = monsterbodies::RenderToHead(view, a, g_propCapA[side]) && monsterbodies::RenderToHead(view, b, g_propCapB[side]);
        }
    }
    unsigned holding = 0, over = 0;   // both grabs' hands (the body grab's of this pose already in g_last)
    for (int side = 0; side < 2; ++side) {
        if (g_last[side].state >= D2RGB_TAKING || g_prop[side].state >= D2RP_HAND_TAKING) holding |= 1u << side;
        if (g_last[side].state >= D2RGB_HOVER || g_prop[side].state >= D2RP_HAND_HOVER) over |= 1u << side;
    }
    g_holding.store(holding);
    g_overBody.store(over);
}

// [weight] grab_arm_reach: a hand whose held point (the last step's: a body's, or a prop's) went past its arm's reach lets go - its grip is handed to the grabs as
// open until the real grip opens (gb::PastArm says why). The shoulder and the arm as the last pose drew them (skel::ArmReach), the hands' frame. Before each step.
void CheckArmReach(const skel::Input* in, const monsterbodies::View& view) {
    for (int side = 0; side < 2; ++side) {
        if (g_pastArm[side] && !(in->grip[side] >= kTryOff)) g_pastArm[side] = false;
#if D2RVR_FIRST_PERSON
        const bool body = g_last[side].state >= D2RGB_TAKING, prop = !body && g_prop[side].state >= D2RP_HAND_TAKING;
        if (!in->grabArmReach || g_pastArm[side] || !(body || prop) || !(in->handsValid & (1u << side))) continue;
        float shoulder[3], arm = 0.0f, held[3];
        if (!skel::ArmReach(side, shoulder, &arm) || !monsterbodies::RenderToHead(view, body ? g_last[side].pos : g_prop[side].pos, held)) continue;
        const float ctrl[3] = {in->hand[side][0] + in->handOffset[0], in->hand[side][1] + in->handOffset[1], in->hand[side][2] + in->handOffset[2]};
        if (!gb::PastArm(shoulder, arm, held, ctrl, in->grabReachM)) continue;
        g_pastArm[side] = true;
        LogF("grab: the %s hand lets go of %s - the point held is %.0f cm from the shoulder, the arm reaches %.0f cm (the controller %.0f cm): an arm never lengthens "
             "past the grab's reach %.0f cm ([weight] grab_arm_reach); it takes nothing until the grip opens", SideName(side),
             body ? WhoName(g_hostile[side], g_last[side].id) : knockprops::PropName(g_propId[side]), Dist3(held, shoulder) * 100.0f, arm * 100.0f, Dist3(ctrl, shoulder) * 100.0f,
             in->grabReachM * 100.0f);
#else
        (void)view;
#endif
    }
}

// The body grab's step ([weight] grab_bodies): the library stepped with the bodies and the hands, its sways and links into the doll, the pulses, the log.
void StepBodies(skel::Input* in, const monsterbodies::View& view, const monsterbodies::Snapshot& snap, const D2rgbBody* bodies, int n, const D2rgbHand* hands) {
    D2rgbParams p{};
    p.size = sizeof p;
    meleetouch::GrabDollTuning(&p.limbHz, &p.bodyHz, &p.damping, &p.maxLimbM, &p.maxBodyM, &p.massScale);
    p.strength = in->grabStrength;
    p.grabRadius = in->grabReachM;
    p.breakDistance = in->grabBreakM;
    p.gripThreshold = 0.0f;   // the library's own (its controller's input map)
    p.gripRelease = 0.0f;
    p.muscle = in->grabMuscle;   // [weight] grab_muscle_pct: the held monster's joints (the library's chain of limbs)
    const double now = Now();
    const float dt = g_lastAt > 0.0 ? static_cast<float>(std::min(0.1, now - g_lastAt)) : 0.0f;
    g_lastAt = now;
    D2rgbHandOut out[2];
    if (grablib::Step(&p, bodies, n, hands, dt, out) < 0) return;
    D2rgbSway sways[D2RGB_MAX_SWAYS];
    meleetouch::GrabSways(sways, grablib::Sways(sways, D2RGB_MAX_SWAYS));
    // the held limbs as the library holds them: every body list puts them there (the debug view, the weapon's contact, the hand's capsule)
    g_nLinks = grablib::Links(g_links, D2RGB_MAX_SWAYS);
    meleetouch::GrabLinks(g_links, g_nLinks);
    for (int side = 0; side < 2; ++side) {   // the hold's largest bend: a held limb's line against the game's
        const D2rgbHandOut& o = out[side];
        if (o.state < D2RGB_TAKING) continue;
        for (int l = 0; l < g_nLinks; ++l) {
            if (g_links[l].id != o.id) continue;
            const int i = gb::FindHeld(snap, g_links[l].id, g_links[l].bone);
            if (i < 0) continue;
            float a[3], b[3], la = 0.0f, lb = 0.0f, d = 0.0f;
            for (int k = 0; k < 3; ++k) {
                a[k] = g_links[l].tip[k] - g_links[l].joint[k];
                b[k] = snap.b[i].top[k] - snap.b[i].bottom[k];
                la += a[k] * a[k];
                lb += b[k] * b[k];
                d += a[k] * b[k];
            }
            if (la > 1e-10f && lb > 1e-10f) g_maxBend[side] = std::max(g_maxBend[side], std::acos(std::clamp(std::fabs(d) / std::sqrt(la * lb), 0.0f, 1.0f)) * 57.2958f);
        }
    }
    unsigned holding = 0, over = 0;
    for (int side = 0; side < 2; ++side) {
        if (out[side].state >= D2RGB_HOVER) over |= 1u << side;
        const D2rgbHandOut& o = out[side];
        Pulse(side, o);
        if (o.events & D2RGB_EV_GRAB) {
            g_since[side] = now;
            g_peakForce[side] = g_peakGap[side] = 0.0f;
            g_maxBend[side] = 0.0f;
            for (int k = 0; k < 3; ++k) { g_ctrl0[side][k] = hands[side].pos[k]; g_held0[side][k] = o.pos[k]; }
            g_hostile[side] = gb::Hostile(snap, o.id);
            LogF("grab: the %s hand took %s %u by %s (%s)", SideName(side), WhoName(g_hostile[side], o.id), o.id, o.bone >= 0 ? "a limb" : "its body",
                 o.gives ? "it gives way by its weight, bending at its joints against its muscles" : "it does not give way");
        }
        if (o.state >= D2RGB_TAKING) {
            g_peakForce[side] = std::max(g_peakForce[side], o.force);
            g_peakGap[side] = std::max(g_peakGap[side], o.gap);
            holding |= 1u << side;
        }
        if (o.events & (D2RGB_EV_RELEASE | D2RGB_EV_TORN | D2RGB_EV_LOST))
            LogF("grab: the %s hand let go after %.1f s - %s; it pulled at most %.0f N, held back at most %.0f cm", SideName(side), now - g_since[side],
                 (o.events & D2RGB_EV_TORN) ? "torn loose (pulled past the break distance)" : (o.events & D2RGB_EV_LOST) ? "the body is gone (dead, or out of the scan)" : "the grip was let go",
                 g_peakForce[side], g_peakGap[side] * 100.0f);
        g_last[side] = o;
        // a grip try that took nothing: why
        Try& t = g_try[side];
        const float grip = g_pastArm[side] ? 0.0f : in->grip[side];   // ([weight] grab_arm_reach: let go - no try until it opens)
        if (!t.on && grip >= kTryFrom && o.state < D2RGB_TAKING && !(o.events & D2RGB_EV_GRAB)) { t.on = true; t.judged = false; t.peak = grip; }
        if (t.on) {
            t.peak = std::max(t.peak, grip);
            if (o.events & D2RGB_EV_GRAB) t.judged = true;
            if (!t.judged && grip >= kLibraryGrip) {
                t.judged = true;
                if (o.state < D2RGB_TAKING && g_prop[side].state == D2RP_HAND_EMPTY) {   // ([weight] grab_props: a prop under the palm is taken by its grab)
                    int which = -1;
                    const float d = gb::Nearest(bodies, n, hands[side].pos, &which);
                    if (!hands[side].tracked) TellTry(side, now, "the hand is not tracked");
                    else if (!in->grabFree[side]) TellTry(side, now, "the hand may take nothing now: %s", gb::BusyText(in->grabBusy[side]));
                    else if (n == 0) TellTry(side, now, "no bodies near (none in the scan: nobody within 24 units, or the scan has not run)");
                    else if (d > in->grabReachM)
                        TellTry(side, now, "the nearest body (%s %u, %s) is %.0f cm from the palm, the reach is %.0f cm (%d bodies near)", gb::Hostile(snap, bodies[which].id) ? "monster" : "NPC",
                                bodies[which].id, bodies[which].bone >= 0 ? "a limb" : "its capsule", d * 100.0f, in->grabReachM * 100.0f, n);
                    else TellTry(side, now, "a body within reach (%.0f cm), but the library did not take it (state %d)", std::max(0.0f, d) * 100.0f, o.state);
                }
            }
            if (grip < kTryOff) {
                if (!t.judged) TellTry(side, now, "the grip reached only %.2f, the grab takes from %.2f (squeeze harder)", t.peak, kLibraryGrip);
                t.on = false;
            }
        }
    }
    g_holding.store(holding);
    g_overBody.store(over);
    // [weight] grab_wrap: the held bodies as they are drawn (the doll's sway in), for the hook to lay the hand on
    // (what the hand holds as the library has it now - the limb as its chain has moved it, not the doll's sway on the game's capsules (headset 0.183: the hand
    // was laid on the capsule where the game had the arm while the library held it elsewhere: «рука не притягивается к телу нпс»))
    for (int side = 0; side < 2; ++side) {
        g_capOk[side] = false;
        const D2rgbHandOut& o = g_last[side];
        if (o.state < D2RGB_TAKING || !o.holdOk) continue;
        g_capOk[side] = monsterbodies::RenderToHead(view, o.holdA, g_capA[side]) && monsterbodies::RenderToHead(view, o.holdB, g_capB[side]);
        g_capR[side] = o.holdR;
    }
}

}  // namespace

void SetLog(LogFn fn) {
    g_log.store(fn);
    grablib::SetLog(fn);
}

unsigned Holding() { return g_holding.load(); }
uint32_t PropOf(int side, bool* holding) {
    if (side < 0 || side > 1) return 0;
    if (holding) *holding = g_propShareHeld[side].load();
    return g_propShareId[side].load();
}
unsigned OverBody() { return g_overBody.load(); }
bool DrawnShift(int side, float out[3]) {
    std::lock_guard<std::mutex> lock(g_shiftMutex);
    const int s = side & 1;
    if (!g_shiftAt[s] || GetTickCount64() - g_shiftAt[s] > 200) return false;
    for (int k = 0; k < 3; ++k) out[k] = g_shift[s][k];
    return true;
}

void Apply(skel::Input* in) {
    if (!in) return;
    if (!in->grabOn || !in->grabWorldOk) {
        if (g_wasOn && grablib::Loaded() && grablib::ReleaseAll() > 0) LogF("grab: every hand let go - %s", !in->grabOn ? "the grab is off or not F4" : "no world");
        g_wasOn = false;
        g_holding.store(0);
        g_overBody.store(0);
        g_lastAt = 0.0;
        g_last[0] = g_last[1] = D2rgbHandOut{};
        g_capOk[0] = g_capOk[1] = false;
        g_nLinks = 0;
        meleetouch::GrabLinks(nullptr, 0);
        PropsOff();
        in->grabHolding[0] = in->grabHolding[1] = false;
        return;
    }
    g_wasOn = true;
    const bool bodiesOn = in->grabBodies && grablib::Ensure();   // (no library 1.4: the log has said so; no body is grabbed)
    if (!bodiesOn && g_bodiesWere) {   // [weight] grab_bodies unticked while the props' grab goes on
        if (grablib::Loaded() && grablib::ReleaseAll() > 0) LogF("grab: every hand let go of the bodies - [weight] grab_bodies is off");
        g_last[0] = g_last[1] = D2rgbHandOut{};
        g_capOk[0] = g_capOk[1] = false;
        g_nLinks = 0;
        meleetouch::GrabLinks(nullptr, 0);
        g_lastAt = 0.0;
    }
    g_bodiesWere = bodiesOn;
    if (in->grabWrap) griplib::Ensure();   // [weight] grab_wrap: the hook lays the hand with its hand step (it says in the log when there is none)
    monsterbodies::View view;
    for (int k = 0; k < 3; ++k) { view.eye[k] = in->grabEye[k]; view.camR[k] = in->grabCamR[k]; view.camF[k] = in->grabCamF[k]; }
    view.upm = std::clamp(in->grabUpm, 1.0f, 12.0f);

    // A pose posed along with the hero's (a second rig): drawn as the last step had it, nothing stepped
    if (!in->along) {
        // the bodies as the game poses them (the library adds the sway it makes itself)
        monsterbodies::Snapshot snap;
        D2rgbBody bodies[D2RGB_MAX_BODIES];
        const int n = bodiesOn && meleetouch::GrabBodies(&snap) ? gb::ToGrab(snap, meleetouch::GrabDollOn(), bodies) : 0;
        // the hands: the palm (the drawn hand's place with nothing in it), its turn, the grip, whether it may take anything
        CheckArmReach(in, view);
        D2rgbHand hands[2] = {};
        for (int side = 0; side < 2; ++side) {
            D2rgbHand& h = hands[side];
            float head[3];
            for (int k = 0; k < 3; ++k) head[k] = in->hand[side][k] + in->handOffset[k];
            h.tracked = (in->handsValid & (1u << side)) && monsterbodies::HeadToRender(view, head, h.pos) ? 1 : 0;
            h.mayTake = in->grabFree[side] && g_prop[side].state < D2RP_HAND_TAKING && !g_pastArm[side] ? 1 : 0;   // (a hand that holds a prop takes no body)
            h.grip = g_pastArm[side] ? 0.0f : in->grip[side];   // ([weight] grab_arm_reach: let go past the arm's reach)
            gb::HeadRotToRender(view, in->rot[side], h.rot);
            for (int k = 0; k < 3; ++k) g_ctrl[side][k] = h.pos[k];
        }
        if (bodiesOn) StepBodies(in, view, snap, bodies, n, hands);
        if (in->grabProps) StepProps(in, view, hands);
        else PropsOff();
    }
    // the hands that hold something drawn where the library has them - and laid on what they hold ([weight] grab_wrap, in the hook)
    for (int side = 0; side < 2; ++side) {
        const D2rgbHandOut& o = g_last[side];
        if (!in->along && o.state >= D2RGB_TAKING && in->grabBodies && g_holdLines < kHoldLines && Now() - g_holdToldAt[side] >= kHoldEvery) {   // a held grip, once a second
            g_holdToldAt[side] = Now();
            ++g_holdLines;
            float palmHead[3], palm[3];
            const char* how = "";
            const bool drawn = skel::GrabPalmDrawn(side, palmHead, &how) && monsterbodies::HeadToRender(view, palmHead, palm);
            char d[200] = "";
            if (drawn)
                snprintf(d, sizeof d, "the drawn palm (%.2f %.2f %.2f) %.1f cm from the library's hand, %.1f cm off the held surface (laid by %s)", palm[0], palm[1], palm[2],
                         Dist3(palm, o.pos) * 100.0f, o.holdOk ? OffSurface(palm, o.holdA, o.holdB, o.holdR) * 100.0f : -1.0f, how);
            else
                snprintf(d, sizeof d, "the palm NOT laid by the hook (%s)", !in->grabWrap ? "[weight] grab_wrap off" : !o.holdOk ? "no capsule from the library" : "no hand frame or capsule this pose");
            int chain = 0;
            for (int l = 0; l < g_nLinks; ++l) chain += g_links[l].id == o.id;
            float pull[3], moved[3], lp = 0.0f, lm = 0.0f, dpm = 0.0f;
            for (int k = 0; k < 3; ++k) {
                pull[k] = g_ctrl[side][k] - g_ctrl0[side][k];
                moved[k] = o.pos[k] - g_held0[side][k];
                lp += pull[k] * pull[k];
                lm += moved[k] * moved[k];
                dpm += pull[k] * moved[k];
            }
            lp = std::sqrt(lp);
            lm = std::sqrt(lm);
            char bones[96] = "";   // the held chain from its root, by bone (the skeleton's numbers)
            for (int l = 0, w = 0; l < g_nLinks && w < (int)sizeof bones - 8; ++l)
                if (g_links[l].id == o.id) w += snprintf(bones + w, sizeof bones - w, "%s%d", w ? ">" : " (bones ", g_links[l].bone);
            if (bones[0]) { const size_t at = strlen(bones); snprintf(bones + at, sizeof bones - at, ")"); }
            char m[260];
            snprintf(m, sizeof m, "%d held limbs%s, muscles %s, the largest bend %.0f deg; pulled %.0f cm, the held point came %.0f cm %s", chain, bones,
                     chain > 1 ? "on" : o.gives ? "on (one limb)" : "off (a body that stands)", g_maxBend[side], lp * 100.0f, lm * 100.0f,
                     lp > 0.02f && lm > 0.02f ? (dpm / (lp * lm) > 0.5f ? "with the pull" : dpm / (lp * lm) < -0.5f ? "AGAINST the pull" : "across the pull") : "");
            LogF("grab: the %s hand holds %s %u (%s, %s): the controller (%.2f %.2f %.2f), the library's hand (%.2f %.2f %.2f) %.1f cm from it and %.1f cm off the held "
                 "surface (r %.1f cm); %s; %s; its drawn pose %d turns (-1: monster_ragdoll_pose off) - render metres%s", SideName(side), WhoName(g_hostile[side], o.id), o.id,
                 o.bone >= 0 ? "a limb" : "its capsule", o.state == D2RGB_HOLDING ? "joined" : "taking", g_ctrl[side][0], g_ctrl[side][1], g_ctrl[side][2], o.pos[0], o.pos[1],
                 o.pos[2], o.gap * 100.0f, o.holdOk ? OffSurface(o.pos, o.holdA, o.holdB, o.holdR) * 100.0f : -1.0f, o.holdR * 100.0f, d, m, meleetouch::PosedTurns(o.id),
                 g_holdLines == kHoldLines ? " (no more of these lines this session)" : "");
        }
        float head[3];
        in->grabHolding[side] = false;
        const bool propHeld = o.state < D2RGB_TAKING && g_prop[side].state >= D2RP_HAND_TAKING;   // ([weight] grab_props: a prop in this hand)
        if ((o.state >= D2RGB_TAKING || propHeld) && monsterbodies::RenderToHead(view, propHeld ? g_prop[side].pos : o.pos, head)) {
            {   // [hud] wrist_status: the drawn palm (the last pose's) against the controller's, into the room - the wrist's panel rides the drawn hand
                float palmHead[3];
                const char* how = nullptr;
                if (skel::GrabPalmDrawn(side, palmHead, &how)) {
                    float d[3];   // (in->hand is still the controller's here: it is moved below)
                    for (int k = 0; k < 3; ++k) d[k] = palmHead[k] - (in->hand[side][k] + in->handOffset[k]);
                    const float a = in->headYawDeg * 0.0174532925f, c = std::cos(a), sn = std::sin(a);   // the hands' frame's turn in the room (+ left: about +y)
                    std::lock_guard<std::mutex> lock(g_shiftMutex);
                    g_shift[side][0] = d[0] * c + d[2] * sn;
                    g_shift[side][1] = d[1];
                    g_shift[side][2] = -d[0] * sn + d[2] * c;
                    g_shiftAt[side] = GetTickCount64();
                }
            }
            for (int k = 0; k < 3; ++k) in->hand[side][k] = head[k] - in->handOffset[k];
            if (propHeld ? g_propCapOk[side] : g_capOk[side]) {
                in->grabHolding[side] = true;
                for (int k = 0; k < 3; ++k) {
                    in->grabCapA[side][k] = propHeld ? g_propCapA[side][k] : g_capA[side][k];
                    in->grabCapB[side][k] = propHeld ? g_propCapB[side][k] : g_capB[side][k];
                }
                in->grabCapR[side] = propHeld ? g_propCapR[side] : g_capR[side];
            }
        }
    }
}

}  // namespace grabwire
