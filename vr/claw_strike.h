// claw_strike.h - [shapeshift] claw_hits: a druid's werewolf / werebear strikes with its bare paws by touch (F4, with
// [shapeshift] forms_first_person and [gameplay] melee_touch). The forms hold no weapon - their rigs have no weapon bones - so
// hit by touch had no blade to sweep. Each paw is a blade of its own here, from the form's own skeleton: the wrist (the arm
// chain's hand bone) to the farthest bone under it (a claw's tip), as thick as the paw's bones spread about that line.
//
// What a paw's touch does is what a weapon's does (vr/melee_touch.cpp): the same stroke engine, rules and verdicts, then the game's
// own attack - the key, Direct hit by its rules, the armed skill (Fury, Feral Rage, Maul, Hunger ... on a pad button, melee_arm.h).
// The game decides the hit, the miss, the damage and the speed. The one thing added: the two paws are ONE weapon (the idea of
// D2R VR Unleashed's Sample::claws): each has its own strokes, but a paw's attack starts one shared recovery - the hero's own attack
// time, measured on his unit (melee_direct_client.h AttackClock: the game's speed with its IAS) - so two paws never attack twice as often.
//
// Pure: no game calls (cleanroom/tests/claw_strike_test.cpp). vr/skeletons.cpp makes the paws, vr/melee_touch.cpp judges them.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace clawstrike {

// ---- a paw's blade, from the form's bones -------------------------------------------------------------------------------------
// The capsule's radius bounds, metres: a paw is never thinner than a fist's knuckles nor thicker than a bear's whole paw.
constexpr float kMinRadius = 0.04f, kMaxRadius = 0.15f;
// ... and its length: a paw whose bones end within 3 cm of the wrist (a rig with nothing under the hand) has none.
constexpr float kMinLength = 0.03f, kMaxLength = 0.60f;

struct Paw {
    bool ok = false;
    float dir[3] = {};     // the wrist -> the claw's tip, unit
    float length = 0.0f;   // metres
    float radius = 0.0f;
};

// wrist: the hand bone; ends: the bones under it (n points, packed x y z), all in one frame of metres. The tip is the farthest of
// them; the radius the farthest any of them lies off the wrist -> tip line (the paw's breadth), bounded.
inline Paw PawFrom(const float wrist[3], const float* ends, int n) {
    Paw p;
    float best = 0.0f;
    int tipAt = -1;
    for (int i = 0; i < n; ++i) {
        const float d[3] = {ends[i * 3] - wrist[0], ends[i * 3 + 1] - wrist[1], ends[i * 3 + 2] - wrist[2]};
        const float l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (std::isfinite(l) && l > best) { best = l; tipAt = i; }
    }
    if (tipAt < 0 || best < kMinLength) return p;
    for (int k = 0; k < 3; ++k) p.dir[k] = (ends[tipAt * 3 + k] - wrist[k]) / best;
    float off = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float d[3] = {ends[i * 3] - wrist[0], ends[i * 3 + 1] - wrist[1], ends[i * 3 + 2] - wrist[2]};
        const float along = d[0] * p.dir[0] + d[1] * p.dir[1] + d[2] * p.dir[2];
        const float q[3] = {d[0] - p.dir[0] * along, d[1] - p.dir[1] * along, d[2] - p.dir[2] * along};
        const float o = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
        if (std::isfinite(o)) off = std::max(off, o);
    }
    p.length = std::min(best, kMaxLength);
    p.radius = std::clamp(off, kMinRadius, kMaxRadius);
    p.ok = true;
    return p;
}

// ---- the paw's grip point against the controller ([weight] grab_bodies with a paw) -------------------------------------------
// The palm's middle as the paw is drawn, kept in the CONTROLLER's own frame (q: its rotation, x y z w): Turn(q, v, true) takes a head-frame offset into it,
// Turn(q, local, false) back out, however the controller is turned since. v' = v + w t + u x t, t = 2 u x v (u = q.xyz, or -q.xyz undone).
inline void Turn(const float q[4], const float v[3], bool undo, float out[3]) {
    const float s = undo ? -1.0f : 1.0f, u[3] = {q[0] * s, q[1] * s, q[2] * s};
    const float t[3] = {2.0f * (u[1] * v[2] - u[2] * v[1]), 2.0f * (u[2] * v[0] - u[0] * v[2]), 2.0f * (u[0] * v[1] - u[1] * v[0])};
    out[0] = v[0] + q[3] * t[0] + (u[1] * t[2] - u[2] * t[1]);
    out[1] = v[1] + q[3] * t[1] + (u[2] * t[0] - u[0] * t[2]);
    out[2] = v[2] + q[3] * t[2] + (u[0] * t[1] - u[1] * t[0]);
}

// ---- [shapeshift] wolf_min_speed / bear_min_speed: the paws' own least speed -----------------------------------------------------
// A bare hand is hard to swing as fast as a sword (Serg 2026-11-03: «рукой сложно быстро махать голой» - his melee_min_speed is 6 m/s): each form its own least
// speed where a paw touches, m/s. 0 (or under kFormMinSpeedFloor, or a key not there) = none of its own: the unarmed kind's, else melee_min_speed, as before.
// The players' ini ships kFormMinSpeedShipped. Only whether a contact counts - the game's attack, its speed and rolls stay the game's.
constexpr float kFormMinSpeedFloor = 0.3f, kFormMinSpeedCeil = 20.0f, kFormMinSpeedShipped = 1.0f;
// form: 1 the werewolf, 2 the werebear (unleashed::shapeshift::Form), anything else none. The form's own least speed, 0 = none.
inline float FormMinSpeed(int form, float wolfMs, float bearMs) {
    const float v = form == 1 ? wolfMs : form == 2 ? bearMs : 0.0f;
    return std::isfinite(v) && v >= kFormMinSpeedFloor ? std::min(v, kFormMinSpeedCeil) : 0.0f;
}
// The least speed a paw's engine is given in place of the unarmed kind's own (unarmedOwn: [gameplay] melee_min_speed_unarmed, 0 = none): the form's where it has
// one, else the unarmed kind's - which strikedir::Effective then takes over the general melee_min_speed when it is set.
inline float PawOwnSpeed(int form, float wolfMs, float bearMs, float unarmedOwn) {
    const float f = FormMinSpeed(form, wolfMs, bearMs);
    return f > 0.0f ? f : unarmedOwn;
}

// ---- the two paws, one weapon -------------------------------------------------------------------------------------------------
enum class Why {
    Counts,       // the game's attack is asked for
    Slow,         // the paw moved slower than the least speed where it touched
    Recovering,   // a paw (this one or the other) attacked less than the recovery ago
    Struck,       // this paw's stroke has already struck this monster
};
inline const char* WhyText(Why w) {
    switch (w) {
        case Why::Counts: return "struck";
        case Why::Slow: return "too slow";
        case Why::Recovering: return "recovering from the last paw's attack";
        case Why::Struck: return "this stroke has struck it already";
    }
    return "?";
}

// The recovery: the hero's attack time as the game plays it (measured: AttackClock), never under kMinRecoveryMs.
constexpr uint64_t kMinRecoveryMs = 150;
inline uint64_t RecoveryMs(uint64_t attackMs) { return std::max(attackMs, kMinRecoveryMs); }

struct Contact {
    int paw = 0;            // 0 right, 1 left
    uint64_t stroke = 0;    // the paw's own stroke (its engine's id)
    uint32_t monster = 0;
    float speed = 0.0f;     // m/s where it touched
    uint64_t nowMs = 0;
};

class Lanes {
 public:
    // A contact a paw made: is it an attack? *sinceMs: how long since the last paw's attack (when Recovering).
    Why Judge(const Contact& c, float leastSpeed, uint64_t recoveryMs, uint64_t* sinceMs = nullptr) const {
        const Lane& l = lane_[c.paw & 1];
        if (l.any && l.stroke == c.stroke && l.monster == c.monster) return Why::Struck;
        if (!(c.speed >= leastSpeed)) return Why::Slow;
        if (lastAt_ && c.nowMs >= lastAt_ && c.nowMs - lastAt_ < recoveryMs) {
            if (sinceMs) *sinceMs = c.nowMs - lastAt_;
            return Why::Recovering;
        }
        return Why::Counts;
    }
    // The game's attack was asked for: the shared recovery starts, and this paw's stroke has struck this monster.
    void Struck(const Contact& c) {
        Lane& l = lane_[c.paw & 1];
        l.any = true;
        l.stroke = c.stroke;
        l.monster = c.monster;
        lastAt_ = c.nowMs ? c.nowMs : 1;
        lastPaw_ = c.paw & 1;
    }
    // A shift back, another game, another view: nothing carries over.
    void Reset() { *this = Lanes{}; }
    int LastPaw() const { return lastPaw_; }
    uint64_t LastAt() const { return lastAt_; }

 private:
    struct Lane { bool any = false; uint64_t stroke = 0; uint32_t monster = 0; };
    Lane lane_[2];
    uint64_t lastAt_ = 0;
    int lastPaw_ = -1;
};

}  // namespace clawstrike
