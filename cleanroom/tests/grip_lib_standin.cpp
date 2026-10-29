// grip_lib_standin.cpp - a stand-in for the grip library (shared/d2r_vr_grip.h): the interface implemented in the simplest way that is still a rule, so the loader (vr/grip_lib.cpp),
// the hold's grip engine and the skeleton hook's library path can be tested without the real library. Not a grip anyone plays with.
//
//   LineStep: held by the grip, or by the trigger too for the second hand; loose = the point goes to the controller at once, the turn follows it; tight = both stay; the pulses.
//   HandStep: the palm 3.5 cm from the axis (on the side it came), the turn the hand has, every bone half way from open to closed - or, after StandinSet(echo), exactly as the
//             hand's fingers stand (D2rgHandIn::fingersFrom), which makes the adapter's round trip visible. Given the shaft against the hand (interface version 2: axisInHand, lineInHand)
//             it lays the hand the way that field says: the line along the shaft by the smallest turn, the palm's middle where the vector from it reaches the axis.
//   StandinSet(refuse, echo): not part of the interface; a test finds it by name. StandinLast(out): what the last HandStep was given (size, has-axis, the vector, the line, palmDepth).
// STANDIN_BAD_ABI builds one that speaks another interface version; STANDIN_ABI_1 one that speaks version 1 (it ignores the version-2 fields and lays the palm 3.5 cm from the axis).

#define D2RG_BUILD_DLL 1
#include "d2r_vr_grip.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
int g_refuse = 0, g_echo = 0;
double g_last[12] = {};   // what the last HandStep was given

struct Q4 { double x, y, z, w; };

struct V { double x, y, z; };
V Sub(V a, V b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V Add(V a, V b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V Mul(V a, double k) { return {a.x * k, a.y * k, a.z * k}; }
double Dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V P(const D2rgV3& v) { return {v.x, v.y, v.z}; }
D2rgV3 C(V v) { return {v.x, v.y, v.z}; }
V Cross(V a, V b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double Len(V a) { return std::sqrt(Dot(a, a)); }
V Unit(V a, V fallback) { const double l = Len(a); return l > 1e-12 ? Mul(a, 1.0 / l) : fallback; }
Q4 QMul(Q4 a, Q4 b) { return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z}; }
Q4 QConj(Q4 a) { return {-a.x, -a.y, -a.z, a.w}; }
Q4 QNorm(Q4 a) { const double n = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z + a.w * a.w); return n > 1e-12 ? Q4{a.x / n, a.y / n, a.z / n, a.w / n} : Q4{0, 0, 0, 1}; }
V Rot(Q4 q, V v) {   // q v q*
    const Q4 p = QMul(QMul(q, Q4{v.x, v.y, v.z, 0}), QConj(q));
    return {p.x, p.y, p.z};
}
Q4 AxisAngle(V axis, double rad) { const V a = Unit(axis, V{1, 0, 0}); const double s = std::sin(rad * 0.5); return {a.x * s, a.y * s, a.z * s, std::cos(rad * 0.5)}; }
Q4 FromTo(V a, V b) {   // the smallest turn taking unit a onto unit b
    const double d = Dot(a, b);
    if (d < -0.999999) { const V p = Unit(std::fabs(a.x) < 0.9 ? Cross(a, V{1, 0, 0}) : Cross(a, V{0, 1, 0}), V{0, 0, 1}); return AxisAngle(p, 3.14159265358979323846); }
    const V c = Cross(a, b);
    return QNorm(Q4{c.x, c.y, c.z, 1.0 + d});
}
double SignedAngle(V a, V b, V k) { return std::atan2(Dot(Cross(a, b), k), Dot(a, b)); }
Q4 Of(const D2rgQ& q) { return {q.x, q.y, q.z, q.w}; }
D2rgQ Out(Q4 q) { return {q.x, q.y, q.z, q.w}; }
D2rgQ Nlerp(D2rgQ a, D2rgQ b, double t) {
    if (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0) b = {-b.x, -b.y, -b.z, -b.w};
    D2rgQ q{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
    const double n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return n > 1e-12 ? D2rgQ{q.x / n, q.y / n, q.z / n, q.w / n} : D2rgQ{0, 0, 0, 1};
}
}  // namespace

extern "C" {

__declspec(dllexport) void StandinSet(int refuse, int echo) {
    g_refuse = refuse;
    g_echo = echo;
}

// what the last HandStep that held a hand was given: [0] size, [1] hasAxisInHand (0 when the struct ends before it), [2..4] axisInHand, [5..7] lineInHand, [8] palmDepth, [9] the palm's middle's
// distance from the axis it answered with (m)
__declspec(dllexport) void StandinLast(double* out) {
    for (int i = 0; i < 12; ++i) out[i] = g_last[i];
}

int32_t D2rgAbi(void) {
#ifdef STANDIN_BAD_ABI
    return 99;
#elif defined(STANDIN_ABI_1)
    return 1;
#else
    return D2RG_ABI_VERSION;
#endif
}

const char* D2rgName(void) { return "grip standin 0.1"; }

int32_t D2rgLineStep(const D2rgLineIn* in, D2rgLineState* st, D2rgLineOut* out) {
    if (!in || !st || !out || in->size < sizeof(D2rgLineIn) || g_refuse) return -1;
    if (st->size == 0) st->size = sizeof(D2rgLineState);
    const bool holds = in->holds != 0, second = in->second != 0;
    const bool grip = in->grip > 0.6, trig = in->trigger > 0.6;
    D2rgLineOut o{};
    o.hold = holds && (grip || (second && trig)) ? 1 : 0;
    o.mode = !holds ? D2RG_MODE_NONE : (second && !grip && trig) ? D2RG_MODE_LOOSE : D2RG_MODE_TIGHT;
    o.t = in->t;
    o.roll = holds ? st->roll : 0.0;
    if (o.mode == D2RG_MODE_LOOSE) {
        o.t = std::clamp(in->raw, std::min(in->butt, in->tip), std::max(in->butt, in->tip));
        o.roll = in->rollRaw;
    }
    if (in->inReach && !holds && !st->wasNear) o.events |= D2RG_EV_HOVER;
    if (holds && !st->wasHolds) o.events |= D2RG_EV_GRAB;
    if (!holds && st->wasHolds) o.events |= D2RG_EV_RELEASE;
    if (o.events) { o.hapticAmp = 0.5; o.hapticMs = 40; }
    st->wasHolds = holds ? 1 : 0;
    st->wasNear = in->inReach && !holds ? 1 : 0;
    st->mode = o.mode;
    st->roll = o.roll;
    *out = o;
    return 0;
}

int32_t D2rgHandStep(const D2rgHandIn* in, const D2rgRig* rig, D2rgHandState* st, D2rgHandOut* out) {
    if (!in || !rig || !st || !out || in->size < sizeof(D2rgHandIn) || rig->size < sizeof(D2rgRig) || g_refuse) return -1;
    if (st->size == 0) st->size = sizeof(D2rgHandState);
    D2rgHandOut o{};
    o.handPos = in->handPos;
    o.handRot = in->handRot;
    for (int i = 0; i < rig->nBones; ++i) o.finger[i] = rig->bones[i].open;
    if (!in->holds) {
        std::memset(st, 0, sizeof *st);
        st->size = sizeof(D2rgHandState);
        *out = o;
        return 0;
    }
    const V at = P(in->shaft.at), d = P(in->shaft.dir);
    const double t = std::clamp(Dot(Sub(P(in->handPos), at), d), in->shaft.butt, in->shaft.tip);
    const V axis = Add(at, Mul(d, t));
    V away = Sub(P(in->handPos), axis);
    const double l = std::sqrt(Dot(away, away));
    away = l > 1e-9 ? Mul(away, 1.0 / l) : V{0, 1, 0};
    st->phase = 2;
    o.onShaft = 1;
    o.fingersOn = 1;
    o.handPos = C(Add(axis, Mul(away, 0.035)));
    o.t = t;
#ifndef STANDIN_ABI_1
    if (in->size >= sizeof(D2rgHandIn) && in->hasAxisInHand) {   // version 2: the way the mod says the shaft lies against the hand
        const bool zeroLine = in->lineInHand.x == 0.0 && in->lineInHand.y == 0.0 && in->lineInHand.z == 0.0;
        const V lineH = zeroLine ? V{0, 1, 0} : Unit(P(in->lineInHand), V{0, 1, 0});
        const Q4 q0 = QNorm(Of(in->handRot));
        const V lv = Rot(q0, lineH);
        const Q4 q1 = QNorm(QMul(FromTo(lv, Dot(d, lv) < 0.0 ? Mul(d, -1.0) : d), q0));
        V c = P(in->axisInHand);
        c = Sub(c, Mul(lineH, Dot(c, lineH)));
        Q4 q2 = q1;
        if ((in->flags & D2RG_HAND_FACES_AXIS) && Len(c) > 1e-3) {
            const V w = Sub(axis, P(in->handPos));
            const V wp = Sub(w, Mul(d, Dot(w, d)));
            if (Len(wp) > 0.02) q2 = QNorm(QMul(AxisAngle(d, SignedAngle(Rot(q1, c), wp, d)), q1));
        }
        o.handPos = C(Sub(axis, Rot(q2, c)));
        o.handRot = Out(q2);
    }
#endif
    {
        const V pp = Sub(P(o.handPos), axis);
        g_last[0] = in->size;
        g_last[1] = in->size >= sizeof(D2rgHandIn) ? in->hasAxisInHand : 0;
        g_last[2] = in->size >= sizeof(D2rgHandIn) ? in->axisInHand.x : 0;
        g_last[3] = in->size >= sizeof(D2rgHandIn) ? in->axisInHand.y : 0;
        g_last[4] = in->size >= sizeof(D2rgHandIn) ? in->axisInHand.z : 0;
        g_last[5] = in->lineInHand.x;
        g_last[6] = in->lineInHand.y;
        g_last[7] = in->lineInHand.z;
        g_last[8] = in->palmDepth;
        g_last[9] = Len(Sub(pp, Mul(d, Dot(pp, d))));
    }
    for (int i = 0; i < rig->nBones; ++i) {
        if (g_echo) o.finger[i] = in->hasFingersFrom ? in->fingersFrom[i] : rig->bones[i].open;
        else o.finger[i] = Nlerp(rig->bones[i].open, rig->bones[i].closed, 0.5);
    }
    *out = o;
    return 0;
}

}  // extern "C"
