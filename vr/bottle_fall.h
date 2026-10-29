// bottle_fall.h - a potion bottle let go of in VR first person (F4) falls with its weight: [hands] belt_empty_bottle (the emptied bottle) and [hands] belt_drop_ground
// (a full one thrown or dropped). Pure: no game, no window, no thread - cleanroom/tests/bottle_fall_test.cpp drives it.
//
// The bottle is a solid glass cylinder: its radius, its height and its mass (the potion's size: a flask is heavier than a vial). It leaves the hand with the hand's own
// motion (Release: the velocity and the spin a Tracker worked out from the hand's last poses), falls under gravity, and meets the ground - a flat floor at a height, the
// hero's ground under him - at the rims of its two ends: sequential impulses at the points under the floor, a bounce (restitution, only for a real hit: a slow touch does
// not bounce, so it does not jitter), Coulomb friction, a little rolling resistance while it touches. A bottle slower than restSpeed and restSpin for restTime is still:
// it lies (or stands) there from then on. Nothing comes from any library: plain rigid-body mechanics.
//
// Frame and units: any right-handed frame with y up, metres, seconds, kilograms. The body's point is its centre of mass, half the height up its own +y from the base; the
// model's origin (the base's centre) is Base(). Quaternions (x, y, z, w), unit.
#pragma once

#include <algorithm>
#include <cmath>

namespace bottlefall {

constexpr double kGravity = 9.81;
constexpr int kRim = 12;              // points round each end's rim: the cylinder meets the floor as a 12-sided prism (it rolls, and comes to rest)
constexpr double kMaxStep = 1.0 / 240.0;
constexpr double kSlop = 0.001;       // m: a rim this close over the floor touches it

struct Shape {
    double radius = 0.04;   // m
    double height = 0.12;   // m, base to top
    double mass = 0.35;     // kg
};

struct Params {
    double gravity = kGravity;
    double restitution = 0.3;    // glass on stone
    double bounceSpeed = 0.5;    // m/s: a hit slower than this does not bounce
    double friction = 0.45;
    double rolling = 1.5;        // /s: the spin lost while it touches (rolling resistance)
    double restSpeed = 0.05;     // m/s ...
    double restSpin = 0.8;       // rad/s ...
    double restTime = 0.3;       // ... for this long: still
    double maxSpeed = 12.0;      // m/s: a release faster than this is a tracking jump, held to it
    double maxSpin = 40.0;       // rad/s
    double maxFallS = 10.0;      // s: still not at rest by then - it is laid still where it is
};

enum class Phase { Air, Floor, Rest };

struct Body {
    double p[3] = {0, 0, 0};     // centre of mass
    double q[4] = {0, 0, 0, 1};  // its turn: the bottle's up is q's +y
    double v[3] = {0, 0, 0};
    double w[3] = {0, 0, 0};     // angular velocity, world
};

struct Sim {
    bool valid = false;
    Shape shape;
    Body body;
    Phase phase = Phase::Air;
    double still = 0;            // s it has been slow
    double t = 0;                // s since the release
    bool touched = false;        // it has met the floor
    int bounces = 0;
};

// What a step saw.
enum : unsigned { kTouchedFirst = 1u, kCameToRest = 2u };

namespace detail {
inline void Rot(const double q[4], const double v[3], double o[3]) {   // o = q v q*
    const double tx = 2.0 * (q[1] * v[2] - q[2] * v[1]), ty = 2.0 * (q[2] * v[0] - q[0] * v[2]), tz = 2.0 * (q[0] * v[1] - q[1] * v[0]);
    o[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
    o[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
    o[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
}
inline void RotInv(const double q[4], const double v[3], double o[3]) {
    const double c[4] = {-q[0], -q[1], -q[2], q[3]};
    Rot(c, v, o);
}
inline void Cross(const double a[3], const double b[3], double o[3]) {
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}
inline double Dot(const double a[3], const double b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
inline double Len(const double a[3]) { return std::sqrt(Dot(a, a)); }
inline bool Finite(const double* a, int n) {
    for (int i = 0; i < n; ++i)
        if (!std::isfinite(a[i])) return false;
    return true;
}
inline void Normalise(double q[4]) {
    const double l = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (!(l > 1e-12)) { q[0] = q[1] = q[2] = 0; q[3] = 1; return; }
    for (int i = 0; i < 4; ++i) q[i] /= l;
}
inline void Clamp(double v[3], double max) {
    const double l = Len(v);
    if (l > max && l > 0)
        for (int i = 0; i < 3; ++i) v[i] *= max / l;
}
// The solid cylinder's inertia about its own axes (y along it): the inverse, diagonal.
inline void InvInertia(const Shape& s, double out[3]) {
    const double r2 = s.radius * s.radius, h2 = s.height * s.height;
    const double across = s.mass * (3.0 * r2 + h2) / 12.0, along = s.mass * r2 / 2.0;
    out[0] = out[2] = across > 1e-12 ? 1.0 / across : 0.0;
    out[1] = along > 1e-12 ? 1.0 / along : 0.0;
}
// I^-1 x in the world: R Ib^-1 R^T x
inline void ApplyInvInertia(const Sim& s, const double inv[3], const double x[3], double o[3]) {
    double b[3];
    RotInv(s.body.q, x, b);
    for (int i = 0; i < 3; ++i) b[i] *= inv[i];
    Rot(s.body.q, b, o);
}
}  // namespace detail

// The base's centre (the model's origin) of a body: half the height down its own +y.
inline void Base(const Sim& s, double out[3]) {
    const double down[3] = {0, -0.5 * s.shape.height, 0};
    double d[3];
    detail::Rot(s.body.q, down, d);
    for (int i = 0; i < 3; ++i) out[i] = s.body.p[i] + d[i];
}

// A bottle let go of: its model's origin (the base's centre) and turn, the hand's velocity and spin. Not valid for a shape or a number that is none.
inline Sim Release(const Shape& shape, const double base[3], const double q[4], const double vel[3], const double spin[3], const Params& prm = Params()) {
    Sim s;
    if (!(shape.radius > 0.002 && shape.radius < 1.0 && shape.height > 0.005 && shape.height < 2.0 && shape.mass > 0.001 && shape.mass < 50.0)) return s;
    if (!detail::Finite(base, 3) || !detail::Finite(q, 4) || !detail::Finite(vel, 3) || !detail::Finite(spin, 3)) return s;
    s.shape = shape;
    for (int i = 0; i < 4; ++i) s.body.q[i] = q[i];
    detail::Normalise(s.body.q);
    const double up[3] = {0, 0.5 * shape.height, 0};
    double u[3];
    detail::Rot(s.body.q, up, u);
    for (int i = 0; i < 3; ++i) {
        s.body.p[i] = base[i] + u[i];
        s.body.v[i] = vel[i];
        s.body.w[i] = spin[i];
    }
    detail::Clamp(s.body.v, prm.maxSpeed);
    detail::Clamp(s.body.w, prm.maxSpin);
    s.valid = true;
    return s;
}

// The lowest point of the bottle (its rims), height in the frame.
inline double Lowest(const Sim& s) {
    double low = 1e300;
    for (int end = 0; end < 2; ++end)
        for (int k = 0; k < kRim; ++k) {
            const double a = 6.283185307179586 * k / kRim;
            const double local[3] = {s.shape.radius * std::cos(a), (end ? 0.5 : -0.5) * s.shape.height, s.shape.radius * std::sin(a)};
            double r[3];
            detail::Rot(s.body.q, local, r);
            low = std::min(low, s.body.p[1] + r[1]);
        }
    return low;
}

// One step of dt seconds over a floor at floorY (substepped inside). Returns what happened (kTouchedFirst, kCameToRest).
inline unsigned Step(Sim& s, const Params& prm, double floorY, double dt) {
    using namespace detail;
    unsigned ev = 0;
    if (!s.valid || s.phase == Phase::Rest || !(dt > 0) || !std::isfinite(dt) || !std::isfinite(floorY)) return ev;
    dt = std::min(dt, 0.1);   // a stall does not throw it through the floor
    const int n = std::max(1, (int)std::ceil(dt / kMaxStep));
    const double h = dt / n;
    double inv[3];
    InvInertia(s.shape, inv);
    const double invM = 1.0 / s.shape.mass;
    for (int step = 0; step < n && s.phase != Phase::Rest; ++step) {
        s.t += h;
        s.body.v[1] -= prm.gravity * h;
        // the rim points under the floor
        // the rim points under the floor, or that would be after the step (speculative: a fast fall is caught before it is through, and stopped at the floor)
        double pts[2 * kRim][3], target[2 * kRim];
        double depth = 0;
        int m = 0;
        bool touching = false, bounced = false;
        for (int end = 0; end < 2; ++end)
            for (int k = 0; k < kRim; ++k) {
                const double a = 6.283185307179586 * k / kRim;
                const double local[3] = {s.shape.radius * std::cos(a), (end ? 0.5 : -0.5) * s.shape.height, s.shape.radius * std::sin(a)};
                double r[3], wr[3];
                Rot(s.body.q, local, r);
                Cross(s.body.w, r, wr);
                const double vn = s.body.v[1] + wr[1];
                const double gap = s.body.p[1] + r[1] - floorY;
                if (gap > kSlop && gap + std::min(0.0, vn) * h >= 0) continue;
                for (int i = 0; i < 3; ++i) pts[m][i] = r[i];
                if (gap > kSlop) target[m] = -gap / h;   // not there yet: it may come down as far as the floor in this step
                else {
                    touching = true;
                    depth = std::max(depth, -gap);
                    const bool hit = -vn > prm.bounceSpeed;
                    bounced = bounced || hit;
                    target[m] = hit ? -prm.restitution * vn : gap > 0 ? -gap / h : 0.0;
                }
                ++m;
            }
        if (m) {
            if (touching && !s.touched) { s.touched = true; ev |= kTouchedFirst; }
            if (bounced) ++s.bounces;
            double jn[2 * kRim] = {}, jt[2 * kRim][2] = {};
            const double nrm[3] = {0, 1, 0};
            for (int it = 0; it < 10; ++it)
                for (int c = 0; c < m; ++c) {
                    const double* r = pts[c];
                    double wr[3], vrel[3];
                    Cross(s.body.w, r, wr);
                    for (int i = 0; i < 3; ++i) vrel[i] = s.body.v[i] + wr[i];
                    // normal: k = 1/m + n . ((I^-1 (r x n)) x r)
                    double rn[3], irn[3], t3[3];
                    Cross(r, nrm, rn);
                    ApplyInvInertia(s, inv, rn, irn);
                    Cross(irn, r, t3);
                    const double kn = invM + Dot(nrm, t3);
                    double dj = (target[c] - vrel[1]) / std::max(kn, 1e-9);
                    const double old = jn[c];
                    jn[c] = std::max(0.0, old + dj);
                    dj = jn[c] - old;
                    double imp[3] = {0, dj, 0};
                    // friction, x and z, each held to friction x the normal impulse
                    for (int a = 0; a < 2; ++a) {
                        const int ax = a ? 2 : 0;
                        double tdir[3] = {0, 0, 0};
                        tdir[ax] = 1;
                        double rt[3], irt[3], t4[3];
                        Cross(r, tdir, rt);
                        ApplyInvInertia(s, inv, rt, irt);
                        Cross(irt, r, t4);
                        const double kt = invM + Dot(tdir, t4);
                        double djt = -vrel[ax] / std::max(kt, 1e-9);
                        const double lim = prm.friction * jn[c];
                        const double o2 = jt[c][a];
                        jt[c][a] = std::clamp(o2 + djt, -lim, lim);
                        djt = jt[c][a] - o2;
                        imp[ax] += djt;
                    }
                    for (int i = 0; i < 3; ++i) s.body.v[i] += imp[i] * invM;
                    double ri[3], dw[3];
                    Cross(r, imp, ri);
                    ApplyInvInertia(s, inv, ri, dw);
                    for (int i = 0; i < 3; ++i) s.body.w[i] += dw[i];
                }
            if (touching) {
                const double keep = std::exp(-prm.rolling * h);
                for (int i = 0; i < 3; ++i) s.body.w[i] *= keep;
                s.body.p[1] += depth;   // out of the floor (the impulses keep it from going back in)
                s.phase = Phase::Floor;
            }
        }
        // move and turn
        for (int i = 0; i < 3; ++i) s.body.p[i] += s.body.v[i] * h;
        const double* w = s.body.w;
        const double* q = s.body.q;
        const double dq[4] = {0.5 * h * (w[0] * q[3] + w[1] * q[2] - w[2] * q[1]), 0.5 * h * (-w[0] * q[2] + w[1] * q[3] + w[2] * q[0]),
                              0.5 * h * (w[0] * q[1] - w[1] * q[0] + w[2] * q[3]), 0.5 * h * (-w[0] * q[0] - w[1] * q[1] - w[2] * q[2])};
        for (int i = 0; i < 4; ++i) s.body.q[i] += dq[i];
        Normalise(s.body.q);
        if (!Finite(s.body.p, 3) || !Finite(s.body.v, 3) || !Finite(s.body.w, 3)) {   // never: but a body that went wrong is laid still on the floor
            s.body.v[0] = s.body.v[1] = s.body.v[2] = s.body.w[0] = s.body.w[1] = s.body.w[2] = 0;
            if (!Finite(s.body.p, 3)) { s.valid = false; return ev; }
            s.phase = Phase::Rest;
            ev |= kCameToRest;
            return ev;
        }
        const bool slow = touching && Len(s.body.v) < prm.restSpeed && Len(s.body.w) < prm.restSpin;
        s.still = slow ? s.still + h : 0;
        if ((slow && s.still >= prm.restTime) || s.t >= prm.maxFallS) {
            for (int i = 0; i < 3; ++i) s.body.v[i] = s.body.w[i] = 0;
            const double low = Lowest(s);
            if (low < floorY || s.touched) s.body.p[1] += floorY - low;   // on the floor, not in it, not over it
            s.phase = Phase::Rest;
            ev |= kCameToRest;
        }
    }
    return ev;
}

// ---- The hand's motion at the release: the last poses of the hand that lets go ----
// Velocity: the line through the positions of the last `window` seconds (least squares), so one jittery pose does not throw the bottle; spin: the turn from the oldest of
// them to the newest over the time between. Not enough poses: none (the bottle just drops).
struct Tracker {
    static constexpr int kN = 24;
    double t[kN] = {}, p[kN][3] = {}, q[kN][4] = {};
    int n = 0, head = 0;
    void Clear() { n = 0; head = 0; }
    void Add(double time, const double pos[3], const double rot[4]) {
        if (!std::isfinite(time) || !detail::Finite(pos, 3) || !detail::Finite(rot, 4)) return;
        if (n && time <= t[(head + kN - 1) % kN]) return;   // the same pose twice, or time going back
        t[head] = time;
        for (int i = 0; i < 3; ++i) p[head][i] = pos[i];
        for (int i = 0; i < 4; ++i) q[head][i] = rot[i];
        head = (head + 1) % kN;
        n = std::min(n + 1, kN);
    }
    // false: fewer than three poses in the window (the hand stood still or was not seen) - vel and spin are zero then
    bool Motion(double now, double window, double vel[3], double spin[3]) const {
        vel[0] = vel[1] = vel[2] = spin[0] = spin[1] = spin[2] = 0;
        int idx[kN];
        int m = 0;
        for (int k = 0; k < n; ++k) {
            const int i = (head + kN - 1 - k) % kN;   // newest first
            if (now - t[i] > window) break;
            idx[m++] = i;
        }
        if (m < 3) return false;
        double mt = 0, mp[3] = {0, 0, 0};
        for (int k = 0; k < m; ++k) {
            mt += t[idx[k]];
            for (int a = 0; a < 3; ++a) mp[a] += p[idx[k]][a];
        }
        mt /= m;
        for (int a = 0; a < 3; ++a) mp[a] /= m;
        double stt = 0, stp[3] = {0, 0, 0};
        for (int k = 0; k < m; ++k) {
            const double dt = t[idx[k]] - mt;
            stt += dt * dt;
            for (int a = 0; a < 3; ++a) stp[a] += dt * (p[idx[k]][a] - mp[a]);
        }
        if (!(stt > 1e-12)) return false;
        for (int a = 0; a < 3; ++a) vel[a] = stp[a] / stt;
        // spin: d = qNew * conj(qOld), its axis-angle over the time
        const double* qn = q[idx[0]];
        const double* qo = q[idx[m - 1]];
        const double span = t[idx[0]] - t[idx[m - 1]];
        const double c[4] = {-qo[0], -qo[1], -qo[2], qo[3]};
        double d[4] = {qn[3] * c[0] + qn[0] * c[3] + qn[1] * c[2] - qn[2] * c[1], qn[3] * c[1] - qn[0] * c[2] + qn[1] * c[3] + qn[2] * c[0],
                       qn[3] * c[2] + qn[0] * c[1] - qn[1] * c[0] + qn[2] * c[3], qn[3] * c[3] - qn[0] * c[0] - qn[1] * c[1] - qn[2] * c[2]};
        if (d[3] < 0)
            for (double& x : d) x = -x;   // the short way round
        const double s = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (s > 1e-9 && span > 1e-6) {
            const double angle = 2.0 * std::atan2(s, d[3]);
            for (int a = 0; a < 3; ++a) spin[a] = d[a] / s * angle / span;
        }
        return true;
    }
};

}  // namespace bottlefall
