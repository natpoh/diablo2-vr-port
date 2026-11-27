// shield_push.h - [weight] shield_collide_unheld: a weapon the weight model does not hold (an orb in the palm, a claw on the fist) meets the hero's own
// shield too. [weight] shield_collide stops only the weight model's weapon (vr/weight.cpp: the shield's face handed to its contact step); an orb or a claw is
// never that model's (vr/body/arms.cpp kindOf: -1), so it went through the shield whatever shield_collide said (Serg's sorceress, 0.225.0: 'ob2 ' beside a
// 'sml ' - «почему у меня оружие проходит сквозь щит а не бьется об него»). Here the same rules (vr/shield_collide.h) act on the drawn hand alone: the weapon,
// where the game draws it in that hand, is kept out of the shield's face (vr/shield_shape.h, the game's mesh where the game draws it) by moving the drawn hand -
// only along the face's normal, so it slides along it - while the controller goes on; past shieldcollide::kGiveM it goes through until it is clear.
// Only the drawing: no attack, block, command or anything of the game's. Pure maths (cleanroom/tests/shield_push_test.cpp).
//
// The frame is the hands' (metres from the eye, x right, y up, z back: Input::hand / rot). The weapon's capsule and the shield's are each kept in the frame of
// the controller that carries them (Local: taken off the last pose, as drawn), and put back on the controllers of this sample: the shield follows its arm at
// once, the weapon's hand is moved from where it was drawn last toward its controller in short moves (kStepM: never through the face in one), each followed by
// the push out of the face.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "shield_collide.h"

namespace shieldpush {

using shieldcollide::Seg;

constexpr double kStepM = 0.02;     // the drawn hand moves at most this far between two pushes out of the face (the face's capsules are ~4 cm thick, the weapon's ~9)
constexpr int kMaxSteps = 48;       // ... at most this many a sample (about a metre)
constexpr double kTouchM = 0.004;   // a weapon this near the face is on it
constexpr int kPushRounds = 6;

// q: x y z w (Input::rot). v' = q v q*.
inline void Rotate(const float q[4], const double v[3], double out[3]) {
    const double x = q[0], y = q[1], z = q[2], w = q[3];
    const double tx = 2.0 * (y * v[2] - z * v[1]), ty = 2.0 * (z * v[0] - x * v[2]), tz = 2.0 * (x * v[1] - y * v[0]);
    out[0] = v[0] + w * tx + (y * tz - z * ty);
    out[1] = v[1] + w * ty + (z * tx - x * tz);
    out[2] = v[2] + w * tz + (x * ty - y * tx);
}
inline void RotateBack(const float q[4], const double v[3], double out[3]) {
    const float c[4] = {-q[0], -q[1], -q[2], q[3]};
    Rotate(c, v, out);
}
inline bool QuatOk(const float q[4]) {
    const double l = (double)q[0] * q[0] + (double)q[1] * q[1] + (double)q[2] * q[2] + (double)q[3] * q[3];
    return std::isfinite(l) && l > 0.8 && l < 1.2;
}

// A capsule in a controller's frame.
struct Local {
    double a[3] = {}, b[3] = {}, r = 0.0;
};
inline Local ToLocal(const Seg& s, const float pos[3], const float q[4]) {
    Local l;
    double d[3];
    for (int i = 0; i < 3; ++i) d[i] = s.a[i] - pos[i];
    RotateBack(q, d, l.a);
    for (int i = 0; i < 3; ++i) d[i] = s.b[i] - pos[i];
    RotateBack(q, d, l.b);
    l.r = s.r;
    return l;
}
inline Seg FromLocal(const Local& l, const double pos[3], const float q[4]) {
    Seg s{};
    Rotate(q, l.a, s.a);
    Rotate(q, l.b, s.b);
    for (int i = 0; i < 3; ++i) { s.a[i] += pos[i]; s.b[i] += pos[i]; }
    s.r = l.r;
    return s;
}

// The closest points of two capsules' segments: the gap between their surfaces (under 0 they overlap) and the way from q's point to p's (unit; any
// unit way when they cross exactly).
inline double Contact(const Seg& p, const Seg& q, double n[3]) {
    double d1[3], d2[3], w[3];
    for (int i = 0; i < 3; ++i) { d1[i] = p.b[i] - p.a[i]; d2[i] = q.b[i] - q.a[i]; w[i] = p.a[i] - q.a[i]; }
    const auto dot = [](const double* x, const double* y) { return x[0] * y[0] + x[1] * y[1] + x[2] * y[2]; };
    const double a = dot(d1, d1), e = dot(d2, d2), f = dot(d2, w);
    double s = 0, t = 0;
    if (a <= 1e-12 && e <= 1e-12) {
    } else if (a <= 1e-12) {
        t = std::clamp(f / e, 0.0, 1.0);
    } else {
        const double c = dot(d1, w);
        if (e <= 1e-12) {
            s = std::clamp(-c / a, 0.0, 1.0);
        } else {
            const double b = dot(d1, d2), den = a * e - b * b;
            s = den > 1e-12 ? std::clamp((b * f - c * e) / den, 0.0, 1.0) : 0.0;
            t = (b * s + f) / e;
            if (t < 0) { t = 0; s = std::clamp(-c / a, 0.0, 1.0); }
            else if (t > 1) { t = 1; s = std::clamp((b - c) / a, 0.0, 1.0); }
        }
    }
    double x[3], l2 = 0;
    for (int i = 0; i < 3; ++i) { x[i] = (p.a[i] + d1[i] * s) - (q.a[i] + d2[i] * t); l2 += x[i] * x[i]; }
    const double l = std::sqrt(l2);
    if (l > 1e-9) for (int i = 0; i < 3; ++i) n[i] = x[i] / l;
    else { n[0] = 0; n[1] = 1; n[2] = 0; }
    return l - p.r - q.r;
}

// The weapon (its Locals on a hand at `at`, turned by q) against the face: the least gap, the way out of the deepest overlap.
inline double Gap(const Local* w, int nw, const double at[3], const float q[4], const Seg* face, int nf, double nOut[3]) {
    double g = 1e9;
    for (int i = 0; i < nw; ++i) {
        const Seg s = FromLocal(w[i], at, q);
        for (int j = 0; j < nf; ++j) {
            double n[3];
            const double gi = Contact(s, face[j], n);
            if (gi < g) { g = gi; if (nOut) for (int k = 0; k < 3; ++k) nOut[k] = n[k]; }
        }
    }
    return g;
}
// The hand moved out of the face: along the deepest overlap's way, a few rounds. Returns the last way out it was pushed along (or 0 0 0).
inline bool PushOut(const Local* w, int nw, double at[3], const float q[4], const Seg* face, int nf, double nOut[3]) {
    bool pushed = false;
    for (int round = 0; round < kPushRounds; ++round) {
        double n[3];
        const double g = Gap(w, nw, at, q, face, nf, n);
        if (!(g < 0.0)) break;
        for (int k = 0; k < 3; ++k) at[k] += n[k] * (-g + 1e-4);
        if (nOut) for (int k = 0; k < 3; ++k) nOut[k] = n[k];
        pushed = true;
    }
    return pushed;
}

// What a sample did.
struct Result {
    double off[3] = {};        // the drawn hand against its controller (hands' frame, metres): added to Input::hand
    bool touch = false;        // the weapon is on the face now
    bool fresh = false;        // ... and was not the sample before
    bool through = false;      // pressed on through it (the shield left out until the weapon is clear)
    bool wentThrough = false;  // ... this sample
    bool cleared = false;      // ... and clear again this sample
    double lag = 0.0;          // how far the drawn hand is off its controller (m)
    double gap = 1e9;          // the weapon's gap to the face at the drawn hand (m)
    double approach = 0.0;     // how fast the weapon came onto the face, along its normal (m/s; the controller's motion against the shield's)
    double slide = 0.0;        // how fast the drawn hand moves along the face while on it (m/s)
};

// The drawn hand of the weapon, held back by the shield: one Step per sample.
class Hand {
public:
    // w / nw: the weapon's capsules in its controller's frame; hand / q: that controller now; face / nf: the shield's capsules now (hands' frame);
    // dt: seconds since the last Step (<= 0 or > 0.1: a new start, nothing carried).
    Result Step(const Local* w, int nw, const float hand[3], const float q[4], const Seg* face, int nf, double dt) {
        Result r;
        if (nw <= 0 || nf <= 0 || !QuatOk(q) || !std::isfinite(hand[0] + hand[1] + hand[2])) { Reset(); return r; }
        const double target[3] = {hand[0], hand[1], hand[2]};
        double centre[3] = {};
        for (int j = 0; j < nf; ++j)
            for (int k = 0; k < 3; ++k) centre[k] += 0.5 * (face[j].a[k] + face[j].b[k]) / nf;
        const bool carry = had_ && dt > 1e-4 && dt <= 0.1;
        double p[3];
        for (int k = 0; k < 3; ++k) p[k] = carry ? prevTarget_[k] + off_[k] : target[k];   // (where it was drawn last)
        double n[3] = {0, 0, 0};
        if (pass_.Through()) {
            for (int k = 0; k < 3; ++k) p[k] = target[k];
            r.gap = Gap(w, nw, p, q, face, nf, n);
            pass_.Step(false, 0.0f, (float)r.gap);
            r.cleared = !pass_.Through();
        } else {
            const double was[3] = {p[0], p[1], p[2]};
            PushOut(w, nw, p, q, face, nf, n);   // (the shield swung onto the weapon where it was drawn)
            for (int i = 0; i < kMaxSteps; ++i) {   // toward the controller kStepM at a time (never further in one move: no tunnelling), pushed out after each
                double rem[3] = {target[0] - p[0], target[1] - p[1], target[2] - p[2]};
                const double len = std::sqrt(rem[0] * rem[0] + rem[1] * rem[1] + rem[2] * rem[2]);
                if (len < 1e-6) break;
                const double k1 = std::min(1.0, kStepM / len);
                const double before[3] = {p[0], p[1], p[2]};
                for (int k = 0; k < 3; ++k) p[k] += rem[k] * k1;
                PushOut(w, nw, p, q, face, nf, n);
                double m2 = 0;
                for (int k = 0; k < 3; ++k) m2 += (p[k] - before[k]) * (p[k] - before[k]);
                if (m2 < 4e-8) break;   // (held: the face takes the whole move, give or take the push's 0.1 mm)
            }
            double gn[3];
            r.gap = Gap(w, nw, p, q, face, nf, gn);
            r.touch = r.gap <= kTouchM;
            if (r.touch) for (int k = 0; k < 3; ++k) n[k] = gn[k];
            double lag2 = 0;
            for (int k = 0; k < 3; ++k) lag2 += (p[k] - target[k]) * (p[k] - target[k]);
            r.lag = std::sqrt(lag2);
            if (!pass_.Step(r.touch, (float)r.lag, (float)r.gap, never_)) {   // pressed on past kGiveM: through, the hand on its controller
                r.wentThrough = true;
                r.touch = false;
                for (int k = 0; k < 3; ++k) p[k] = target[k];
                r.lag = 0.0;
            }
            if (r.touch && carry) {
                double moved[3], vc[3], vs[3];
                for (int k = 0; k < 3; ++k) {
                    moved[k] = p[k] - was[k];
                    vc[k] = (target[k] - prevTarget_[k]) / dt;   // (the controller's motion)
                    vs[k] = (centre[k] - prevCentre_[k]) / dt;   // (the shield's)
                }
                const double closing = -((vc[0] - vs[0]) * n[0] + (vc[1] - vs[1]) * n[1] + (vc[2] - vs[2]) * n[2]);
                r.approach = std::max(0.0, closing);
                const double mn = moved[0] * n[0] + moved[1] * n[1] + moved[2] * n[2];
                double t2 = 0;
                for (int k = 0; k < 3; ++k) { const double tk = moved[k] - n[k] * mn; t2 += tk * tk; }
                r.slide = std::sqrt(t2) / dt;
            }
        }
        r.through = pass_.Through();
        r.fresh = r.touch && !wasTouch_;
        wasTouch_ = r.touch;
        for (int k = 0; k < 3; ++k) {
            r.off[k] = p[k] - target[k];
            off_[k] = r.off[k];
            prevTarget_[k] = target[k];
            prevCentre_[k] = centre[k];
        }
        had_ = true;
        return r;
    }
    void Reset() {
        had_ = false;
        wasTouch_ = false;
        pass_.Reset();
        for (int k = 0; k < 3; ++k) off_[k] = prevTarget_[k] = prevCentre_[k] = 0.0;
    }
    unsigned Throughs() const { return pass_.Count(); }
    // [weight] shield_never_through: never through - the hand gives way however far its controller goes
    void SetNeverThrough(bool on) { never_ = on; }

private:
    bool had_ = false, wasTouch_ = false, never_ = false;
    double off_[3] = {}, prevTarget_[3] = {}, prevCentre_[3] = {};
    shieldcollide::Pass pass_;
};

// The log's state of the contact, once every few seconds (vr/weight.cpp, vr/skeletons.cpp): what is told and when.
enum class Tell { None, Resting, Sliding, Through };
inline Tell TellOf(bool touch, bool through, double slideMps) {
    return through ? Tell::Through : !touch ? Tell::None : slideMps >= 0.10 ? Tell::Sliding : Tell::Resting;
}
inline const char* TellName(Tell t) {
    return t == Tell::Through ? "gone through" : t == Tell::Sliding ? "sliding along it" : t == Tell::Resting ? "resting on it" : "off it";
}
// A line at most every kTellMs while there is contact to tell (and once more when it ends): each state's samples counted between lines.
constexpr uint64_t kTellMs = 3000;
class Teller {
public:
    // Counts this sample; true when a line is due now (the counts in `out`, then cleared).
    struct Counts {
        unsigned resting = 0, sliding = 0, through = 0, touches = 0;
        double maxLag = 0.0, maxApproach = 0.0;
    };
    bool Step(Tell t, bool freshTouch, double lag, double approach, uint64_t nowMs, Counts* out) {
        if (t == Tell::Resting) ++c_.resting;
        if (t == Tell::Sliding) ++c_.sliding;
        if (t == Tell::Through) ++c_.through;
        if (freshTouch) ++c_.touches;
        c_.maxLag = std::max(c_.maxLag, lag);
        c_.maxApproach = std::max(c_.maxApproach, approach);
        const bool any = c_.resting + c_.sliding + c_.through + c_.touches > 0;
        if (!any) return false;
        if (at_ && nowMs - at_ < kTellMs) return false;
        at_ = nowMs;
        if (out) *out = c_;
        c_ = Counts{};
        return true;
    }

private:
    Counts c_;
    uint64_t at_ = 0;
};

}  // namespace shieldpush
