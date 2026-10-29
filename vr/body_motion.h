#pragma once
// body_motion.h - [gameplay] melee_thrust_with_run for [weight] stab_in: the monsters' bodies' velocities in the weight model's room (the hero's run - the world sliding
// past the room - and the monster's own walk), from where each capsule's middle is step by step, smoothed. The stab's speed against the body (the weight library's
// D2rwStab: the point's velocity against the body's) then counts the hero running at it with the weapon held forward, and the wound rides with the body.
// The author (2026-10-22): «если я бегу и держу перед собой оружие оно должно воткнуться». Off (the tick unticked): the bodies have no velocity, as before.
//
// A body is known by its monster and its limb (Key). Its places come from the touch's scans (a few times a frame at most) taken through the view each step, so a
// step's raw velocity is jagged; it is smoothed (kAlpha a step) and a jump (a new scan, a teleport: faster than kMaxSpeed) starts it again from rest. Pure.

#include <cmath>
#include <cstdint>

#include "heft.h"

namespace bodymotion {

constexpr double kAlpha = 0.35;      // the share of a step's raw velocity taken in
constexpr double kMaxSpeed = 15.0;   // m/s: faster than this is a jump, not a motion
constexpr int kSlots = 128;

inline uint64_t Key(uint32_t id, int part) { return (static_cast<uint64_t>(id) << 16) | static_cast<uint16_t>(part + 1); }

class Tracker {
public:
    // A body's middle (room) on weight step `step` (a counter the caller moves by one a step), dt seconds after the last. Its smoothed velocity.
    heft::V3 Feed(uint64_t key, heft::V3 mid, double dt, uint64_t step) {
        Slot* s = nullptr;
        Slot* oldest = &slots_[0];
        for (Slot& x : slots_) {
            if (x.used && x.key == key) { s = &x; break; }
            if (!x.used || x.step < oldest->step) oldest = &x;
        }
        if (!s) {   // new (or forgotten): from rest
            s = oldest;
            *s = Slot {};
            s->used = true;
            s->key = key;
            s->mid = mid;
            s->step = step;
            return {};
        }
        if (s->step + 1 == step && dt > 1e-4) {
            const heft::V3 raw = heft::Mul(heft::Sub(mid, s->mid), 1.0 / dt);
            if (heft::Len(raw) > kMaxSpeed) s->vel = {};
            else s->vel = heft::Add(s->vel, heft::Mul(heft::Sub(raw, s->vel), kAlpha));
        } else if (s->step != step) {
            s->vel = {};   // (a gap: not seen last step)
        }
        s->mid = mid;
        s->step = step;
        return s->vel;
    }
    void Clear() { *this = Tracker {}; }

private:
    struct Slot {
        bool used = false;
        uint64_t key = 0, step = 0;
        heft::V3 mid, vel;
    };
    Slot slots_[kSlots];
};

}  // namespace bodymotion
