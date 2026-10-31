// knock_trigger.h - [weight] knock_down: does a push of the held weapon knock a humanoid off its feet (docs/notes/knock_down.md)? Pure
// (cleanroom/tests/knock_trigger_test.cpp).
//
// The weight model hands every push it gives a body (vr/weight.cpp PushBack -> meleetouch::PushBody): a strike's first step carries the weapon and the arm
// behind it - (the steel + 3.5 kg) x the speed in; while it presses on, the contact's own impulse each step. A body is knocked down by the speed a push gives
// it: the impulse over its mass, against `needMs` (the slider):
//   a blow     one strike's impulse, and a RUNNING one more: the hero's own speed across the ground the way of the blow times runKg (his weight behind it - the
//              weight model's speeds are the hands' in the room; the hero's run is not in them) - once a strike, at its first step;
//   a sweep    a blow low at the legs (under kSweepShare of the body's height over its floor) and across, not down: needs sweepShare of it;
//   a shove    pushes that do not knock alone but add up within kShoveS to shoveShare of it, kShoveSteps of them at least - the weapon pressed on and on;
//   a stagger  a strike that gives at least staggerShare of what it would need, but not all: a step or two back, the balance caught, no fall
//              (2026-10-31, the author: «или шаг назад а потом он должен вернуться на свое место»).
// After a knock-down the body is left alone for kQuietS (the library has it down anyway). Only the drawing: the game's mechanics never hear of it.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "d2r_vr_fall.h"

namespace knocktrigger {

constexpr double kGapS = 0.12;       // s without a push on a body: the next push begins a new strike
constexpr double kShoveS = 0.6;      // s the pushes of a shove add up over
constexpr int kShoveSteps = 5;
constexpr double kQuietS = 1.0;
constexpr float kSweepShare = 0.3f;  // of the body's height over its floor: a blow under this is at the legs
constexpr float kAcross = 0.6f;      // a sweep's push is across: its up or down share at most this
constexpr int kMaxUnits = 16;

struct Settings {
    float needMs = 0.7f;        // [weight] knock_down_cms / 100: the speed a push must give the body
    float sweepShare = 0.5f;    // a sweep needs this share of it
    float shoveShare = 1.0f;    // a shove (its pushes added up) needs this share of it
    float runKg = 12.0f;        // the hero's weight behind a running blow
    float staggerShare = 0.45f; // [weight] knock_down_stagger_pct / 100: a strike that gives this share of the need staggers (0: never)
};

// The body as the touch's timer last saw it (render metres): its floor, its height over it, its mass.
struct Body {
    float floorY = 0.0f, height = 1.7f, massKg = 50.0f;
};

struct Verdict {
    bool knock = false;
    int kind = D2RF_BLOW;
    float impulse[3] = {};      // N s: the push as it knocks (a running blow's with the hero's weight in it; a shove's added up)
    float point[3] = {};
    float gaveMs = 0.0f;        // the speed it gives the body
    float needMs = 0.0f;        // ... and what it needed
    float runMs = 0.0f;         // the hero's speed the way of the blow (a strike's first step)
    bool strike = false;        // this push began a strike
};

class Tracker {
public:
    // One push (N s, render world; point: where) on body id at t (s). heroVel: the hero's speed across the ground (render metres a second).
    Verdict Note(uint32_t id, const float impulse[3], const float point[3], double t, const Body& body, const float heroVel[3], const Settings& s) {
        Verdict v;
        if (!id || !impulse || !point || !std::isfinite(impulse[0] + impulse[1] + impulse[2] + point[0] + point[1] + point[2] + t)) return v;
        const float j = std::sqrt(impulse[0] * impulse[0] + impulse[1] * impulse[1] + impulse[2] * impulse[2]);
        if (!(j > 1e-6f) || !(body.massKg > 0.5f) || !(s.needMs > 0.0f)) return v;
        Unit& u = UnitOf(id, t);
        if (t - u.quietUntil < 0.0) return v;
        v.strike = t - u.lastT > kGapS;
        u.lastT = t;
        const float dir[3] = {impulse[0] / j, impulse[1] / j, impulse[2] / j};
        float jj = j;
        if (v.strike && heroVel) {   // a running blow: the hero's weight behind it, the way of the blow across the ground
            const float hx = dir[0], hz = dir[2], hl = std::sqrt(hx * hx + hz * hz);
            if (hl > 1e-4f) v.runMs = std::max(0.0f, (heroVel[0] * hx + heroVel[2] * hz) / hl);
            if (std::isfinite(v.runMs)) jj += s.runKg * v.runMs;
        }
        const bool low = point[1] - body.floorY < kSweepShare * body.height && std::fabs(dir[1]) <= kAcross;
        v.kind = low ? D2RF_SWEEP : D2RF_BLOW;
        v.needMs = s.needMs * (low ? s.sweepShare : 1.0f);
        v.gaveMs = jj / body.massKg;
        for (int k = 0; k < 3; ++k) {
            v.impulse[k] = dir[k] * jj;
            v.point[k] = point[k];
        }
        // the shove's sum: the pushes of the last kShoveS
        if (u.n == kSteps) { for (int i = 1; i < kSteps; ++i) u.p[i - 1] = u.p[i]; --u.n; }
        u.p[u.n++] = {t, {impulse[0], impulse[1], impulse[2]}};
        if (v.gaveMs >= v.needMs) return Knocked(u, t, v);
        if (v.strike && s.staggerShare > 0.0f && v.gaveMs >= v.needMs * s.staggerShare) {   // a stagger: a strike's first step only
            v.kind = D2RF_STAGGER;
            return Knocked(u, t, v);
        }
        float sum[3] = {};
        int steps = 0;
        for (int i = 0; i < u.n; ++i)
            if (t - u.p[i].t <= kShoveS) {
                for (int k = 0; k < 3; ++k) sum[k] += u.p[i].j[k];
                ++steps;
            }
        const float sj = std::sqrt(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]);
        if (steps >= kShoveSteps && sj / body.massKg >= s.needMs * s.shoveShare) {
            v.kind = D2RF_SHOVE;
            v.needMs = s.needMs * s.shoveShare;
            v.gaveMs = sj / body.massKg;
            for (int k = 0; k < 3; ++k) v.impulse[k] = sum[k];
            return Knocked(u, t, v);
        }
        return v;
    }
    void Reset() { n_ = 0; }

private:
    static constexpr int kSteps = 96;   // a weight step is ~11 ms: kShoveS of them and some
    struct Pushed { double t; float j[3]; };
    struct Unit {
        uint32_t id = 0;
        double lastT = -1e9, quietUntil = -1e9, seenT = -1e9;
        int n = 0;
        Pushed p[kSteps];
    };
    Unit u_[kMaxUnits];
    int n_ = 0;
    Unit& UnitOf(uint32_t id, double t) {
        for (int i = 0; i < n_; ++i)
            if (u_[i].id == id) { u_[i].seenT = t; return u_[i]; }
        int slot = n_ < kMaxUnits ? n_++ : 0;
        for (int i = 0; i < n_; ++i)   // full: the one seen longest ago
            if (u_[i].seenT < u_[slot].seenT) slot = i;
        u_[slot] = Unit{};
        u_[slot].id = id;
        u_[slot].seenT = t;
        return u_[slot];
    }
    static Verdict Knocked(Unit& u, double t, Verdict v) {
        v.knock = true;
        u.quietUntil = t + kQuietS;
        u.n = 0;
        return v;
    }
};

// The mass of a humanoid of this height (m) by the author's two numbers (2026-10-22: a fallen of 1.15 m 30 kg, a zombie of 1.8 m 50 kg - the doll library's
// law, the size to the power ~1.14), times scale (the weight slider).
inline float MassOf(float heightM, float scale) {
    const float h = std::clamp(heightM, 0.3f, 6.0f);
    return 30.0f * std::pow(h / 1.15f, 1.1427f) * std::clamp(scale, 0.1f, 10.0f);
}

}  // namespace knocktrigger
