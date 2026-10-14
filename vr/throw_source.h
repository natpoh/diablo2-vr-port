// throw_source.h - who says where a throw by gesture goes: the weapon-weight model or the hand's own estimate. Pure (no game calls, no
// globals); unit test cleanroom/tests/throw_source_test.cpp.
//
// With [weight] on, the weight model works the throw out itself (vr/weight.h ThrowOutcome / weight::LastThrow): the weapon's mass, the velocity the
// hand really gave it, the real floor - and it has ALREADY scaled the horizontal reach by [weight] throw_reach_pct and held the landing inside the
// game's 50-cell check, so a consumer must not scale it again. When its outcome is valid and belongs to THIS throw (its step is within
// kFreshMs of the release this module saw), the throw's heading, a potion's landing and a javelin's / knife's / axe's line come from it. Otherwise
// (weight off, nothing valid, a stale outcome, a degenerate one) the hand's own estimate stays - peak velocity and ballistics, vr/throw_aim.h.
//
// The outcome's frame is the head frame of the release as the controllers come: x RIGHT, y UP, z BACK, metres, the head's turn only, the origin at the
// head. The throw gesture's own heading is in the same frame as (right, forward): right = x, forward = -z.
#pragma once

#include <cmath>
#include <cstdint>

namespace throwsrc {

// A weight outcome that belongs to a throw: its step within this many ms of the release.
inline constexpr uint64_t kFreshMs = 300;
// The landing must be this far (metres) from the head to say anything; a heading is read from the landing when it is at least kLandingHeadingM
// away, else from the velocity's horizontal part when that is at least kVelocityHeadingMps.
inline constexpr float kMinLandingM = 0.05f;
inline constexpr float kLandingHeadingM = 0.5f;
inline constexpr float kVelocityHeadingMps = 0.3f;

// The part of weight::ThrowOutcome a throw uses (a copy made by the code that registers the source; the field names are the weight model's).
struct Outcome {
    bool valid = false;
    bool preview = false;            // updated every step while the weapon is in the hand (a release now); false = frozen at the grip release
    uint64_t tickMs = 0;             // GetTickCount64 of the step it was worked out on
    uint64_t releaseTickMs = 0;      // when the grip was released (0 while preview)
    float velocity[3] = {};          // m/s, head frame (x right, y up, z back)
    float speed = 0.0f;              // |velocity|
    float landing[3] = {};           // metres, head frame, where the flight first touches the ground: FINAL (scaled and capped)
    float flightS = 0.0f;
    float elevationDeg = 0.0f;       // velocity over its horizontal, + up
    bool capped = false;             // the weight model's cell cap held the landing
};

// What the weight model decided for this throw.
struct Choice {
    bool weight = false;             // false = use the hand's own estimate (every other field is then 0)
    float right = 0.0f, forward = 1.0f;   // the heading on the ground, a unit vector, the head frame (right of the head, forward of it)
    float landingM = 0.0f;           // metres on the ground from the head to the landing, final (not to be scaled again)
    float speed = 0.0f, flightS = 0.0f, elevationDeg = 0.0f;
    bool headingFromVelocity = false;   // the landing was too near to give a heading
    bool capped = false;
};

inline bool Finite3(const float v[3]) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }

// The outcome's age against a release: the nearest of its step and its grip release to `releaseMs`, in ms either way (a step a moment after
// the release is as good as one before it).
inline uint64_t DistanceMs(uint64_t a, uint64_t b) { return a > b ? a - b : b - a; }

inline bool Fresh(const Outcome& o, uint64_t releaseMs) {
    if (releaseMs == 0) return false;   // this module has seen no release
    if (o.tickMs != 0 && DistanceMs(o.tickMs, releaseMs) <= kFreshMs) return true;
    return o.releaseTickMs != 0 && DistanceMs(o.releaseTickMs, releaseMs) <= kFreshMs;
}

// The weight model's answer for the throw released at `releaseMs` (GetTickCount64): true and the heading, the landing and the flight, or false when
// the hand's own estimate has to do.
inline bool FromWeight(const Outcome& o, uint64_t releaseMs, Choice* out) {
    if (!out) return false;
    *out = Choice();
    if (!o.valid || !Fresh(o, releaseMs) || !Finite3(o.landing) || !Finite3(o.velocity)) return false;
    const float landM = std::hypot(o.landing[0], o.landing[2]);
    if (!(landM >= kMinLandingM)) return false;
    float right, forward;
    bool fromVelocity = false;
    if (landM >= kLandingHeadingM) {
        right = o.landing[0] / landM;
        forward = -o.landing[2] / landM;
    } else {
        const float vh = std::hypot(o.velocity[0], o.velocity[2]);
        if (!(vh >= kVelocityHeadingMps)) return false;
        right = o.velocity[0] / vh;
        forward = -o.velocity[2] / vh;
        fromVelocity = true;
    }
    out->weight = true;
    out->right = right;
    out->forward = forward;
    out->landingM = landM;
    out->speed = std::isfinite(o.speed) ? o.speed : 0.0f;
    out->flightS = std::isfinite(o.flightS) ? o.flightS : 0.0f;
    out->elevationDeg = std::isfinite(o.elevationDeg) ? o.elevationDeg : 0.0f;
    out->headingFromVelocity = fromVelocity;
    out->capped = o.capped;
    return true;
}

}  // namespace throwsrc
