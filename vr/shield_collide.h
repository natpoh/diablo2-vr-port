// shield_collide.h - [weight] shield_collide: the weapon in one hand meets the shield on the other arm - it stops on the shield's face and slides along it, the
// weapon's hand drawn where it holds the weapon (the arm gives way while the controller goes on), a knock in the hands, the game's block sound for a hard
// blow. Only what is drawn, felt and heard: no attack, block or command of the game's is changed. F4 only (the weight model is). vr/weight.cpp does it with
// these rules; the shield's capsules are vr/shield_shape.h's (the game's own mesh). Pure: cleanroom/tests/shield_collide_test.cpp.
//
// The shield's face is a few capsules (vr/shield_parts.h) handed to the weight model with the monsters' bodies, as the other weapon of a pair is
// ([weight] dual_collide): held = 1 (a held thing - the weapon stops on it and glances along it, never stabs in, pushes no doll, strikes no one), firm (a blade
// found inside is eased out, never left there), its velocity from where it was the step before (the shield swung into a resting sword is a knock too).
//
//   Through  The controllers cannot be stopped, so the weapon's hand is drawn off its controller while the weapon rests on the shield. Pressed on further than
//            kGiveM (about 60 cm, asked 2026-11-13 - as far as Unleashed 0.3.34's weapon gives way at its shield) the weapon goes through: the shield is left out until the weapon is
//            clear of it (kClearM) and the hand back near its controller (kBackM) - then it meets the shield again, whichever side it is on.
//   Knock    A new touch (after they had parted) at kFeltMps or faster: a pulse in the weapon's hand and the shield's, harder the faster (the PS VR2's motor
//            shows little under 0.6). A fast one - kSoundMps and up - the game's own block sound too, as loud as clashloud says for that speed. Resting on
//            the shield or sliding along it is silent. One knock at most every kRearmMs.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "clash_loud.h"

namespace shieldcollide {

constexpr float kGiveM = 0.60f;     // the most the weapon's hand is drawn off its controller by the shield; further, the weapon goes through
constexpr float kClearM = 0.03f;    // ... and the shield is met again once the weapon is this far clear of it
constexpr float kBackM = 0.15f;     // ... and its hand within this of its controller
constexpr float kFeltMps = 0.25f;   // a touch this fast and up is felt
constexpr float kSoundMps = 1.0f;   // ... this fast and up is heard too: the game's block sound
constexpr uint64_t kRearmMs = 120;  // a knock at most this often
constexpr uint32_t kId = 0xFFFFFFF1u;   // the shield's capsules among the weight model's bodies (no monster has it; kDualId is the pair's other weapon)

// Through: whether the shield is met this step. touching: the weapon was on the shield after the last step; lagM: how far the weapon's hand is drawn off
// its controller now; gapM: how far the weapon is from the shield (its pieces' surfaces; under 0 inside).
class Pass {
public:
    bool Step(bool touching, float lagM, float gapM) {
        if (!through_) {
            if (touching && lagM >= kGiveM) {
                through_ = true;
                ++count_;
            }
        } else if (gapM > kClearM && lagM <= kBackM) {
            through_ = false;
        }
        return !through_;
    }
    bool Through() const { return through_; }
    unsigned Count() const { return count_; }
    void Reset() { through_ = false; }

private:
    bool through_ = false;
    unsigned count_ = 0;
};

// The knock of a touch at `mps` (the closing speed, m/s).
struct Knock {
    bool felt = false, heard = false;
    float amplitude = 0.0f;   // 0.6 .. 1
    int ms = 0;               // 35 .. 70
    uint32_t gameVolume = 0;  // the game's block sound's volume (0 .. 255) when heard
    float strength = 0.0f;    // 0 .. 1
};
inline Knock KnockOf(float mps) {
    Knock k;
    if (!std::isfinite(mps) || mps < kFeltMps) return k;
    k.felt = true;
    k.strength = std::clamp((mps - kFeltMps) / (clashloud::kFullMps - kFeltMps), 0.0f, 1.0f);
    k.amplitude = 0.6f + 0.4f * k.strength;
    k.ms = static_cast<int>(std::lround(35.0f + 35.0f * k.strength));
    if (mps >= kSoundMps) {
        k.heard = true;
        k.gameVolume = clashloud::Of(mps).gameVolume;
    }
    return k;
}

// A new touch: true once each time the weapon comes onto the shield (after a step off it) and the last knock was kRearmMs ago or more.
class Touches {
public:
    bool Step(bool touch, uint64_t nowMs) {
        const bool fresh = touch && !was_ && (!at_ || nowMs - at_ >= kRearmMs);
        if (fresh) at_ = nowMs;
        was_ = touch;
        return fresh;
    }
    void Reset() { was_ = false; }

private:
    bool was_ = false;
    uint64_t at_ = 0;
};

// The gap between two capsules' surfaces (metres; under 0 they overlap): closest points of the two segments.
struct Seg {
    double a[3], b[3], r;
};
inline double SegGap(const Seg& p, const Seg& q) {
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
    double d = 0;
    for (int i = 0; i < 3; ++i) {
        const double x = (p.a[i] + d1[i] * s) - (q.a[i] + d2[i] * t);
        d += x * x;
    }
    return std::sqrt(d) - p.r - q.r;
}
// The least gap between any piece of one set and any of the other (a very large number when either is empty).
inline double Gap(const Seg* p, int np, const Seg* q, int nq) {
    double g = 1e9;
    for (int i = 0; i < np; ++i)
        for (int j = 0; j < nq; ++j) g = std::min(g, SegGap(p[i], q[j]));
    return g;
}

}  // namespace shieldcollide
