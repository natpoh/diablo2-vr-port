// knock_trigger.h - [weight] knock_down: does a push of the held weapon knock a humanoid off its feet, stagger it, or nothing (docs/notes/knock_down.md)? Pure
// (cleanroom/tests/knock_trigger_test.cpp).
//
// The weight model hands every push it gives a body (vr/weight.cpp PushBack -> meleetouch::PushBody) with the weapon's OWN speed into the body there - the swing,
// in the room: the hands' motion only, never the body's own (its walk, the hero's run past it, a co-op client's network jump) - and the weight behind it (the
// steel + the arm, heftKg). A blow is judged by both (Serg, 2026-11-01: they fell from a light push; «падать только от сильного и быстрого удара»):
//   a blow     a strike's first step: its swing at least `minSwingMs` (the speed slider) AND the speed its push gives the body at least `needMs` (the force
//              slider) - the push the weapon and the arm behind it at the swing, heftKg x swingMs, and a RUNNING one more: the hero's speed across the ground the
//              way of the blow times runKg (his weight behind it; the run adds force, never speed - a weapon only held out while running swings nothing);
//   a sweep    a blow low at the legs (under kSweepShare of the body's height over its floor) and across, not down: needs sweepShare of the force;
//   a stagger  a strike with at least staggerShare of BOTH (the swing and the force) but not all: a step or two back, the balance caught, no fall
//              (2026-10-31, the author: «или шаг назад а потом он должен вернуться на свое место»); and a shove - pushes pressed on and on, kShoveSteps of them at
//              least, that add up within kShoveS to staggerShare of the force: it staggers too, it never knocks down. A pressed push counts for no more than the
//              hands put into it (heftKg x its swing): a body that jumps onto a resting blade pushes nothing.
// After a knock-down or a stagger the body is left alone for kQuietS (the library has it anyway). Only the drawing: the game's mechanics never hear of it.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "d2r_vr_fall.h"

namespace knocktrigger {

constexpr double kGapS = 0.12;       // s without a push on a body: the next push begins a new strike
constexpr double kShoveS = 0.6;      // s the pushes of a shove add up over
constexpr int kShoveSteps = 5;
constexpr float kMaxSwingMs = 25.0f; // m/s: a weapon faster than this jumped (a pose a frame late, a co-op correction) - no swing (Serg's fastest: 21)
constexpr float kPressInMs = 0.3f;   // m/s the weapon itself goes into the body: slower, it rests on it and its pushes add nothing to a shove
constexpr double kQuietS = 1.0;
constexpr float kSweepShare = 0.3f;  // of the body's height over its floor: a blow under this is at the legs
constexpr float kAcross = 0.6f;      // a sweep's push is across: its up or down share at most this
constexpr int kMaxUnits = 16;

struct Settings {
    float needMs = 0.8f;        // [weight] knock_down_cms / 100: the speed a blow's push must give the body (its force over the body's mass)
    float minSwingMs = 5.0f;    // [weight] knock_down_min_speed: the weapon's own speed into the body a blow needs
    float sweepShare = 0.5f;    // a sweep needs this share of the force
    float runKg = 12.0f;        // the hero's weight behind a running blow
    float staggerShare = 0.45f; // [weight] knock_down_stagger_pct / 100: a strike with this share of both staggers, a shove this share of the force (0: never)
};

// The body as the touch's timer last saw it (render metres): its floor, its height over it, its mass.
struct Body {
    float floorY = 0.0f, height = 1.7f, massKg = 50.0f;
};

struct Verdict {
    bool knock = false;         // the library takes it: kind D2RF_STAGGER a step back, else off its feet
    int kind = D2RF_BLOW;
    bool shove = false;         // a stagger by pushes pressed on (kind D2RF_STAGGER)
    float impulse[3] = {};      // N s: the push as it knocks (the swing's, a running blow's with the hero's weight in it; a shove's added up)
    float point[3] = {};
    float gaveMs = 0.0f;        // the speed it gives the body
    float needMs = 0.0f;        // ... and what it needed
    float swingMs = 0.0f;       // the weapon's own speed into the body
    float needSwingMs = 0.0f;   // ... and what it needed
    float runMs = 0.0f;         // the hero's speed the way of the blow (a strike's first step)
    bool strike = false;        // this push began a strike
};

class Tracker {
public:
    // One push (N s, render world: its way; point: where) on body id at t (s). swingMs: the weapon's own speed into the body there (m/s); heftKg: the steel and
    // the arm behind it. heroVel: the hero's speed across the ground (render metres a second, HeroRun's).
    Verdict Note(uint32_t id, const float impulse[3], const float point[3], float swingMs, float heftKg, double t, const Body& body, const float heroVel[3],
                 const Settings& s) {
        Verdict v;
        if (!id || !impulse || !point || !std::isfinite(impulse[0] + impulse[1] + impulse[2] + point[0] + point[1] + point[2] + t)) return v;
        const float j = std::sqrt(impulse[0] * impulse[0] + impulse[1] * impulse[1] + impulse[2] * impulse[2]);
        if (!(j > 1e-6f) || !(body.massKg > 0.5f) || !(s.needMs > 0.0f)) return v;
        const float swing = std::isfinite(swingMs) && swingMs <= kMaxSwingMs ? std::max(swingMs, 0.0f) : 0.0f;
        const float heft = std::isfinite(heftKg) ? std::clamp(heftKg, 0.0f, 50.0f) : 0.0f;
        Unit& u = UnitOf(id, t);
        if (t - u.quietUntil < 0.0) return v;
        v.strike = t - u.lastT > kGapS;
        u.lastT = t;
        const float dir[3] = {impulse[0] / j, impulse[1] / j, impulse[2] / j};
        const float hands = heft * swing;   // N s the hands put into it
        // the shove's sum: the pushes of the last kShoveS, each no more than the hands put into it - and none while the weapon itself rests (the body moved onto it)
        const float pressed = swing >= kPressInMs ? std::min(j, hands) : 0.0f;
        if (u.n == kSteps) { for (int i = 1; i < kSteps; ++i) u.p[i - 1] = u.p[i]; --u.n; }
        u.p[u.n++] = {t, {dir[0] * pressed, dir[1] * pressed, dir[2] * pressed}};
        v.swingMs = swing;
        v.needSwingMs = std::max(0.0f, s.minSwingMs);
        if (v.strike) {
            float jj = hands;
            if (heroVel) {   // a running blow: the hero's weight behind it, the way of the blow across the ground
                const float hx = dir[0], hz = dir[2], hl = std::sqrt(hx * hx + hz * hz);
                if (hl > 1e-4f) v.runMs = std::max(0.0f, (heroVel[0] * hx + heroVel[2] * hz) / hl);
                if (!std::isfinite(v.runMs)) v.runMs = 0.0f;
                jj += s.runKg * v.runMs;
            }
            const bool low = point[1] - body.floorY < kSweepShare * body.height && std::fabs(dir[1]) <= kAcross;
            v.kind = low ? D2RF_SWEEP : D2RF_BLOW;
            v.needMs = s.needMs * (low ? s.sweepShare : 1.0f);
            v.gaveMs = jj / body.massKg;
            for (int k = 0; k < 3; ++k) {
                v.impulse[k] = dir[k] * jj;
                v.point[k] = point[k];
            }
            if (swing >= v.needSwingMs && v.gaveMs >= v.needMs) return Knocked(u, t, v);
            if (s.staggerShare > 0.0f && swing >= v.needSwingMs * s.staggerShare && v.gaveMs >= v.needMs * s.staggerShare) {
                v.kind = D2RF_STAGGER;
                return Knocked(u, t, v);
            }
        }
        if (s.staggerShare > 0.0f) {   // a shove: pressed on and on, it staggers
            float sum[3] = {};
            int steps = 0;
            for (int i = 0; i < u.n; ++i)
                if (t - u.p[i].t <= kShoveS) {
                    for (int k = 0; k < 3; ++k) sum[k] += u.p[i].j[k];
                    ++steps;
                }
            const float sj = std::sqrt(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]);
            if (steps >= kShoveSteps && sj / body.massKg >= s.needMs * s.staggerShare) {
                Verdict w = v;
                w.kind = D2RF_STAGGER;
                w.shove = true;
                w.needMs = s.needMs * s.staggerShare;
                w.gaveMs = sj / body.massKg;
                w.runMs = 0.0f;
                for (int k = 0; k < 3; ++k) {
                    w.impulse[k] = sum[k];
                    w.point[k] = point[k];
                }
                return Knocked(u, t, w);
            }
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

// The hero's speed across the ground for a running blow, from where he is drawn tick by tick (render metres). Eased over kRunEaseS; a step faster than kRunJumpMs
// is a jump, not a run - a teleport, a co-op client's correction from the host (rivas_10: on a client monsters fell on every hit) - and starts it from rest.
class HeroRun {
public:
    static constexpr float kRunEaseS = 0.08f;
    static constexpr float kRunJumpMs = 12.0f;
    // pos: the hero's place (render metres) dt seconds after the last; null or not finite: not known (from rest). The eased velocity (y 0).
    const float* Feed(const float pos[3], float dt) {
        if (!pos || !std::isfinite(pos[0] + pos[1] + pos[2])) { *this = HeroRun{}; return vel_; }
        if (had_ && dt > 1e-4f && dt < 0.5f) {
            const float rx = (pos[0] - was_[0]) / dt, rz = (pos[2] - was_[2]) / dt;
            if (std::sqrt(rx * rx + rz * rz) > kRunJumpMs) {
                vel_[0] = vel_[2] = 0.0f;
            } else {
                const float a = std::min(1.0f, dt / kRunEaseS);
                vel_[0] += (rx - vel_[0]) * a;
                vel_[2] += (rz - vel_[2]) * a;
            }
        } else if (had_) {
            vel_[0] = vel_[2] = 0.0f;   // (a gap: from rest)
        }
        for (int k = 0; k < 3; ++k) was_[k] = pos[k];
        had_ = true;
        return vel_;
    }
    const float* Vel() const { return vel_; }

private:
    float was_[3] = {}, vel_[3] = {};
    bool had_ = false;
};

// The mass of a humanoid of this height (m) - one law of its size, the doll library's (doll_physics 1.3): a fallen of 1.15 m 30 kg, a man of 1.75 m 63 kg
// (Serg, 2026-10-31: «надо нпс ещё весу добавить, чтобы они не были такие легкие, чем больше тем тяжелее» - a rogue 60 - 65), the size to the power ~1.77
// between and beyond (until then a zombie's 50 kg at 1.8 m, the power ~1.14: a rogue came to 48); times scale (the weight slider).
inline float MassOf(float heightM, float scale) {
    const float h = std::clamp(heightM, 0.3f, 6.0f);
    return 30.0f * std::pow(h / 1.15f, 1.7673f) * std::clamp(scale, 0.1f, 10.0f);
}

}  // namespace knocktrigger
