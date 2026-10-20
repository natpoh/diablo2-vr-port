// crouch.h - [body] crouch (VR first person, F4): the hero crouches when you crouch in the room. Pure logic, no game, no
// OpenXR, no globals: vrcam (PushArms) steps a Tracker every 10 ms with how far the head stands below its standing height,
// skeletons.cpp (PoseBody) bends the legs with LegRoom / KneePole / Knee; cleanroom/tests/crouch_test.cpp runs
// both on synthetic numbers.
//
// The idea is bearstonem/d2r-pcvr's (MIT, vr/skeletons.cpp PoseBody, its tracked-neck body, and steamvr/body_tracking.h
// Knee): the drawn body goes down with the camera and each leg is bent again, two bones of their own lengths, to keep the
// foot where the game's animation has it. His body follows the head all the time; here it is a crouch with a threshold
// (a slider), hysteresis and an eased way in and out, so a nod or a small dip leaves the hero standing as before. The code
// is written afresh for our skeleton hook and our recentre.
//
// The camera already goes down with the head (native OpenXR: the eyes from their standing height - the floor plus the measured
// eye height with [calibration] use=1 and a STAGE floor, vr/body_calibration.h StandingEyeY, else the recentre's height; FlatVR
// with [flatvr] head_position: the room head since the recentre). Counted from the recentre alone the crouch did nothing in
// the headset (2026-10-19): a recentre taken seated or at the session's first frame sits low, and a crouch from standing barely
// reached under it. How deep the body goes is that same drop at the same scale (world
// units per metre from the hero's bind-pose eyes and the measured / set eye height): no number by hand. Drawn bones only:
// the hero's place, his hit box, his walking and every game state stay the game's.
#pragma once

#include <algorithm>
#include <cmath>

namespace crouch {

// -- when the player crouches ---------------------------------------------------------------------------------------

struct Params {
    float enterM = 0.20f;   // [body] crouch_cm / 100: the head this far below its standing height is a crouch
    float floorM = 0.0f;    // the deepest drop taken, metres: the standing eye height (the head cannot go under the floor); <= 0: none
};

constexpr float kExitFraction = 0.5f;   // standing again once the head is back within half the threshold (hysteresis)
constexpr float kBlendS = 0.15f;        // the body goes into and out of the crouch over this long (no jump of the hips)
constexpr float kLostHoldS = 0.25f;     // the head's height not known: the pose is held this long, then the body stands up
constexpr float kMaxStepS = 0.1f;       // a longer gap between two steps (a hitch, a pause) counts as this long

class Tracker {
 public:
    // valid: the head's height below its standing height is known this step; downM: that drop, metres (+ = below, a head
    // above it is 0); dtS: seconds since the last step. Returns how far the body goes down, metres (>= 0, 0 = standing).
    float Step(bool valid, float downM, float dtS, const Params& p) {
        if (!std::isfinite(dtS) || dtS < 0.0f) dtS = 0.0f;
        dtS = std::min(dtS, kMaxStepS);
        const float enter = std::isfinite(p.enterM) ? std::max(p.enterM, 0.01f) : 0.20f;
        if (valid && std::isfinite(downM)) {
            lost_ = 0.0f;
            depth_ = std::max(0.0f, downM);
            if (std::isfinite(p.floorM) && p.floorM > 0.0f) depth_ = std::min(depth_, p.floorM);
            if (!crouched_ && depth_ >= enter) crouched_ = true;
            else if (crouched_ && depth_ <= enter * kExitFraction) crouched_ = false;
        } else {
            lost_ += dtS;
            if (lost_ > kLostHoldS) crouched_ = false;   // depth_ kept: the body stands up from where it was
        }
        const float step = dtS / kBlendS;
        weight_ = crouched_ ? std::min(1.0f, weight_ + step) : std::max(0.0f, weight_ - step);
        if (weight_ <= 0.0f && !crouched_) depth_ = 0.0f;
        return weight_ * depth_;
    }
    // Off, another view, no game: standing at once, nothing remembered.
    void Reset() { *this = Tracker{}; }
    bool Crouched() const { return crouched_; }
    float Weight() const { return weight_; }

 private:
    bool crouched_ = false;
    float weight_ = 0.0f;   // 0 standing .. 1 the whole drop
    float depth_ = 0.0f;    // the last known drop, metres
    float lost_ = 0.0f;     // seconds without a known head height
};

// -- the legs (model space: y up; any units) ------------------------------------------------------------------------

struct V { float x = 0, y = 0, z = 0; };
inline V operator+(V a, V b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V operator-(V a, V b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V operator*(V a, float k) { return {a.x * k, a.y * k, a.z * k}; }
inline float Dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float Length(V a) { return std::sqrt(Dot(a, a)); }
inline bool Finite(V a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }

// The shortest a leg can be folded to: the knee shut, the two bones back to back (a hair more, so the knee has a side).
inline float FoldedReach(float thigh, float shin) { return std::fabs(thigh - shin) * 1.001f + 1e-4f; }

// How far the hip can come straight down with the foot kept where it is before the leg is folded shut. 0: not at all
// (a bad leg, the hip already at or under that).
inline float LegRoom(V hip, V foot, float thigh, float shin) {
    if (!Finite(hip) || !Finite(foot) || !std::isfinite(thigh + shin) || thigh <= 0.0f || shin <= 0.0f) return 0.0f;
    const float dy = hip.y - foot.y, dx = hip.x - foot.x, dz = hip.z - foot.z;
    const float folded = FoldedReach(thigh, shin);
    const float across2 = dx * dx + dz * dz;
    const float keep = std::sqrt(std::max(0.0f, folded * folded - across2));   // the height over the foot the hip must keep
    return std::max(0.0f, dy - keep);
}

// Where the knee bends to: the side the animation's knee stands off the hip-foot line, when that is ahead; else straight
// ahead (`ahead`, the way the legs face) - knees bend forward. Not normalised.
inline V KneePole(V hip, V knee, V foot, V ahead) {
    const V axis = foot - hip;
    const float l2 = Dot(axis, axis);
    V side = knee - hip;
    if (l2 > 1e-12f) side = side - axis * (Dot(side, axis) / l2);
    const float thigh = Length(knee - hip);
    if (Finite(side) && Length(side) > 1e-3f * std::max(thigh, 1e-3f) && Dot(side, ahead) > 0.0f) return side;
    return ahead;
}

// Two-bone leg: the hip at `hip`, bones `thigh` and `shin` long, the foot wanted at `foot`, bending toward `pole`. Puts
// the knee in `knee`; a foot out of reach is pulled into reach along the same line (`foot` is changed then). The bones keep
// their lengths. False: no leg (bad lengths, NaN).
inline bool Knee(V hip, float thigh, float shin, V& foot, V pole, V& knee) {
    if (!Finite(hip) || !Finite(foot) || !Finite(pole) || !std::isfinite(thigh + shin) || thigh <= 0.01f * (thigh + shin) ||
        shin <= 0.01f * (thigh + shin))
        return false;
    const V delta = foot - hip;
    float d = Length(delta);
    const float low = FoldedReach(thigh, shin), high = (thigh + shin) * 0.9999f;
    if (!(high > low)) return false;
    const V axis = d > 1e-6f ? delta * (1.0f / d) : V{0, -1, 0};
    d = std::clamp(d, low, high);
    foot = hip + axis * d;
    V p = pole - axis * Dot(pole, axis);
    if (Dot(p, p) < 1e-10f) {   // the pole along the leg: any side across it
        const V across = std::fabs(axis.z) < 0.9f ? V{0, 0, 1} : V{1, 0, 0};
        p = across - axis * Dot(across, axis);
    }
    p = p * (1.0f / Length(p));
    const float along = (thigh * thigh - shin * shin + d * d) / (2.0f * d);
    knee = hip + axis * along + p * std::sqrt(std::max(0.0f, thigh * thigh - along * along));
    return Finite(knee);
}

}  // namespace crouch
