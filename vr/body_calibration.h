// body_calibration.h - the T-pose measurement: the player's standing eye height and the span between the two
// controllers, and what the world scale and the hero's arms make of them. Pure maths, no game and no OpenXR calls,
// no globals: vr/calibration.cpp feeds it a Sample each headset frame; cleanroom/tests/body_calibration_test.cpp runs
// it on synthetic poses.
//
// The idea, the pose limits and the acceptance ranges are bearstonem/d2r-pcvr's (MIT, steamvr/body_calibration.h and
// body_tracking.h, a fork of D2R VR 0.152): stand upright, arms out level, let both triggers go, then hold both for
// 1.2 s without moving; the eye height sets the world scale, the span fits the hero's arms. The code here is written
// afresh for our own frame (OpenXR axes, the controller indexes of vr/direct_input.h: 0 left, 1 right).
//
// Scale and proportion only: nothing here touches a mechanic - not a hit, a speed, a reach the game checks.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace bodycal {

// -- a little maths (OpenXR axes: x right, y up, z back; unit quaternions x y z w) ---------------------------------

struct Vec { float x = 0, y = 0, z = 0; };
struct Quat { float x = 0, y = 0, z = 0, w = 1; };

inline Vec operator+(Vec a, Vec b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec operator-(Vec a, Vec b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec operator*(Vec a, float k) { return {a.x * k, a.y * k, a.z * k}; }
inline float Dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float Length(Vec v) { return std::sqrt(Dot(v, v)); }
inline bool Finite(Vec v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
inline Vec Cross(Vec a, Vec b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

// q v q*
inline Vec Rotate(Quat q, Vec v) {
    const Vec u{q.x, q.y, q.z};
    const Vec t = Cross(u, v) * 2.0f;
    return v + t * q.w + Cross(u, t);
}

// The turn about the vertical that brings the head's forward (-z) round to where it looks, in the horizontal plane.
inline float Heading(Quat q) {
    const Vec f = Rotate(q, {0, 0, -1});
    return std::atan2(-f.x, -f.z);   // 0 = straight ahead (-z), positive to the left (xr.cpp's Yaw)
}

// v turned by a yaw (about +y); Yaw(a) rotates -z toward -x for positive a, as Heading reads it.
inline Vec TurnAboutY(float yaw, Vec v) {
    const float c = std::cos(yaw), s = std::sin(yaw);
    return {c * v.x + s * v.z, v.y, -s * v.x + c * v.z};
}

// -- what counts as a measurement ----------------------------------------------------------------------------------

struct Measurement {
    float eyeHeight = 0;   // metres, the eyes over the floor
    float armSpan = 0;     // metres, grip to grip, arms out level
};

inline bool ValidEyeHeight(float h) { return std::isfinite(h) && h >= 0.8f && h <= 2.3f; }
inline bool ValidSpan(float s) { return std::isfinite(s) && s >= 0.85f && s <= 2.6f; }
inline bool Valid(Measurement m) {
    if (!ValidEyeHeight(m.eyeHeight) || !ValidSpan(m.armSpan)) return false;
    const float ratio = m.armSpan / m.eyeHeight;
    return ratio >= 0.65f && ratio <= 1.45f;
}

// [calibration] seated=1 - played seated (the author, 2026-10-20: "if I play seated, the recenter must set the hero's height ... the
// calibration takes only the distance between the hands, not the headset's height above the floor"). A seated measurement reads the
// arm span alone (the head over the floor is a seated head, not an eye height) and keeps the eye height stored by a standing one.
// What of the stored numbers is in force: standing, both or nothing (Valid); seated, the span by itself, and the stored eye height
// with it when the two are a person's (the world's scale; without one, the height from BodyWalk / [stereo] user_height_m, as before).
inline Measurement InForce(bool seated, Measurement stored) {
    if (!seated) return Valid(stored) ? stored : Measurement{};
    if (!ValidSpan(stored.armSpan)) return {};
    return {Valid(stored) ? stored.eyeHeight : 0.0f, stored.armSpan};
}

// -- one headset frame ---------------------------------------------------------------------------------------------

enum Hand { kLeft = 0, kRight = 1 };

struct Sample {
    double time = 0;                       // seconds, monotonic
    bool floor = false;                    // the poses are in a STAGE space (the floor at y = 0), not LOCAL
    bool headTracked = false;
    bool handTracked[2] = {};              // a controller really located (not a held place)
    bool cancel = false;                   // the cancel button is down (read as an edge)
    bool seated = false;                   // [calibration] seated: the arm span only - no floor, no eye height asked for
    Vec head{};                            // between the eyes
    Quat headRot{};
    Vec hand[2]{};                         // grip positions
    float trigger[2] = {};                 // 0..1
};

enum class Phase { Idle, Release, Ready, Hold, Saving, Saved, Failed };
enum class Hint {
    Pose,        // as it should be
    Release,     // let go of both triggers
    Floor,       // no STAGE floor
    Height,      // the head's height is not a standing eye height
    Tracking,    // the head or a controller is not tracked
    Upright,     // the head is tilted
    Arms,        // the arms are not stretched out level at the shoulders
    Still,       // the head or a hand moved while the triggers were held
    Saved,
    Failed,      // the result could not be written
};

// Is this frame a T-pose, whatever the triggers do.
inline Hint CheckPose(const Sample& s) {
    if (!s.headTracked || !s.handTracked[kLeft] || !s.handTracked[kRight] || !Finite(s.head) || !Finite(s.hand[kLeft]) ||
        !Finite(s.hand[kRight]))
        return Hint::Tracking;
    // a LOCAL space has no floor: the eye height would be a guess (seated, no eye height is read: any space, any head height)
    if (!s.seated && !s.floor) return Hint::Floor;
    if (!s.seated && !ValidEyeHeight(s.head.y)) return Hint::Height;
    const Quat q = s.headRot;
    const float n2 = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    if (!std::isfinite(n2) || std::fabs(n2 - 1.0f) > 0.05f || Rotate(q, {0, 1, 0}).y < 0.96f) return Hint::Upright;
    // the hands in the head's frame, turned by its heading only (the room's axes mean nothing here)
    const float yaw = Heading(q);
    const Vec r = TurnAboutY(-yaw, s.hand[kRight] - s.head), l = TurnAboutY(-yaw, s.hand[kLeft] - s.head);
    const float span = Length(s.hand[kRight] - s.hand[kLeft]);
    if (s.seated ? !ValidSpan(span) : !Valid({s.head.y, span})) return Hint::Arms;
    // a hand each side, level, near the shoulder line (little forward reach), under the eyes by a shoulder's drop
    // (grip centres measure the arm's reach, not fingertip to fingertip)
    const float drop = -(r.y + l.y) * 0.5f;
    if (r.x < 0.35f || l.x > -0.35f || std::fabs(r.x + l.x) > 0.18f || std::fabs(r.y - l.y) > 0.09f ||
        std::fabs(r.z) > 0.22f || std::fabs(l.z) > 0.22f || drop < 0.10f || drop > 0.50f)
        return Hint::Arms;
    return Hint::Pose;
}

// The flow: release both triggers in the pose, then hold both still; Step says true once, when a full stable capture
// is in. The caller writes the result and tells Saved() or SaveFailed(); the flow ends after the triggers are let go.
class Flow {
public:
    Phase phase = Phase::Idle;
    Hint hint = Hint::Release;
    float progress = 0;   // 0..1 of the hold

    static constexpr double kHoldS = 1.2;
    static constexpr unsigned kMinSamples = 30;

    bool Active() const { return phase != Phase::Idle; }
    Measurement Get() const { return measured_; }
    // Triggers the flow owns (it must not let a held trigger become an attack on the way in or out).
    bool OwnsTriggers() const { return phase != Phase::Idle; }

    void Begin() { *this = Flow{}; phase = Phase::Release; hint = Hint::Release; }
    void Cancel() { phase = Phase::Idle; progress = 0; }
    // Tracking or the session paused: what was under way starts over.
    void Suspend() {
        if (phase == Phase::Ready || phase == Phase::Hold) { phase = Phase::Release; hint = Hint::Tracking; progress = 0; }
        previous_ = 0;
        cancelWas_ = true;
    }
    void Saved(double now) {
        if (phase != Phase::Saving) return;
        phase = Phase::Saved; hint = Hint::Saved; completed_ = now; progress = 1;
    }
    void SaveFailed() {
        if (phase != Phase::Saving) return;
        phase = Phase::Failed; hint = Hint::Failed; progress = 0;
    }

    bool Step(const Sample& s) {
        if (!Active()) return false;
        const bool cancel = s.cancel && !cancelWas_;
        cancelWas_ = s.cancel;
        if (cancel && phase != Phase::Saving) { Cancel(); return false; }
        const bool released = s.trigger[kLeft] < 0.2f && s.trigger[kRight] < 0.2f;
        const bool both = s.trigger[kLeft] > 0.65f && s.trigger[kRight] > 0.65f;
        if (phase == Phase::Saving) return false;
        if (phase == Phase::Saved) {
            if (released && s.time - completed_ >= 1.0) Cancel();
            return false;
        }
        if (!std::isfinite(s.time) || s.time <= 0) { Suspend(); return false; }
        const bool gap = previous_ != 0 && (s.time <= previous_ || s.time - previous_ > 0.15);
        previous_ = s.time;
        if (gap) Suspend();
        const Hint pose = CheckPose(s);
        if (pose != Hint::Pose) { hint = pose; phase = Phase::Release; progress = 0; return false; }
        if (phase == Phase::Release || phase == Phase::Failed) {
            if (released) { phase = Phase::Ready; hint = Hint::Pose; }
            else if (phase == Phase::Release) hint = Hint::Release;
            return false;
        }
        if (phase == Phase::Ready) {
            if (!both) return false;
            anchor_ = s; started_ = s.time; heightSum_ = spanSum_ = 0; samples_ = 0; phase = Phase::Hold;
        }
        if (!both) { phase = Phase::Release; hint = Hint::Release; progress = 0; return false; }
        if (Length(s.head - anchor_.head) > 0.03f || Length(s.hand[kLeft] - anchor_.hand[kLeft]) > 0.045f ||
            Length(s.hand[kRight] - anchor_.hand[kRight]) > 0.045f) {
            phase = Phase::Release; hint = Hint::Still; progress = 0;
            return false;
        }
        heightSum_ += s.head.y;
        spanSum_ += Length(s.hand[kLeft] - s.hand[kRight]);
        ++samples_;
        progress = std::clamp(float((s.time - started_) / kHoldS), 0.0f, 1.0f);
        if (progress < 1.0f || samples_ < kMinSamples) return false;
        // seated: the span only (eyeHeight 0 = not measured - the stored one stays)
        measured_ = {s.seated ? 0.0f : float(heightSum_ / samples_), float(spanSum_ / samples_)};
        if (s.seated ? !ValidSpan(measured_.armSpan) : !Valid(measured_)) { phase = Phase::Release; hint = Hint::Arms; progress = 0; return false; }
        phase = Phase::Saving; hint = Hint::Pose;
        return true;
    }

private:
    double previous_ = 0, started_ = 0, completed_ = 0;
    Sample anchor_{};
    double heightSum_ = 0, spanSum_ = 0;
    unsigned samples_ = 0;
    bool cancelWas_ = true;   // a cancel already held when the flow begins is not a press
    Measurement measured_{};
};

// The controls the measurement held on purpose - both triggers squeezed, B pressed to cancel - must not turn into an attack,
// a skill or a menu command when it ends, on a cancel least of all (the triggers are still down): they stay the
// measurement's until they have been let go once. Step() every frame: `active` = the measurement owns the controls now,
// the rest what the player's hands do (physically, before anything took them). True = take them from the layout this
// frame. (The idea of pcvr's ReleaseGate, same rule: not given back until let go.)
struct ReleaseGate {
    bool blocked = false;
    bool Step(bool active, float leftTrigger, float rightTrigger, bool buttonHeld) {
        const bool owned = active || blocked;
        if (active) blocked = true;
        else if (leftTrigger < 0.2f && rightTrigger < 0.2f && !buttonHeld) blocked = false;
        return owned;
    }
};

// -- where the eyes stand: the F4 camera's height ----------------------------------------------------------------------

// The camera puts the hero's bind-pose eyes at the height the player's eyes have standing, and moves from there with the head
// (a crouch takes it down, at the world's scale). The recentre's head height was that height always - but a recentre is taken
// wherever the head is at the moment (the session's first frame, F11, the runtime's own): seated, or with the headset still in
// the hands, it is too low, and the player standing sees the hero's eyes as far under their own - the hero small, under them.
// With a measurement, the standing eyes are known where the T-pose read them: the measured height over the floor. True when
// that is in force: a floor seen in the space the eyes are in, and a measured eye height (0 = none: not used, not F4).
// Played seated ([calibration] seated=1) never: the hero's eyes are where the head is at the recentre (F11, the runtime's, the
// panel's Recentre view) - sit and recentre, and the view is back at his eyes; the crouch counts from there too.
inline bool StandsOnFloor(bool floorKnown, float floorY, float measuredEye, bool seated = false) {
    return !seated && floorKnown && std::isfinite(floorY) && ValidEyeHeight(measuredEye);
}
// The standing eyes' height in the space the eyes are in: the floor plus the measured eye height, else (seated, or nothing to
// stand on) the recentre's.
inline float StandingEyeY(float recentreY, bool floorKnown, float floorY, float measuredEye, bool seated = false) {
    return StandsOnFloor(floorKnown, floorY, measuredEye, seated) ? floorY + measuredEye : recentreY;
}

// -- what the measurement does to the hero ---------------------------------------------------------------------------

// Model units per metre: the hero's eye height in the model over the player's, kept in the range skeletons.cpp has
// always used; `reach` is the old manual arm scale on top ([arms] scale).
inline float UnitsPerMetre(float modelEye, float userEye, float reach = 1.0f) {
    return std::clamp(modelEye / std::max(0.8f, userEye), 2.0f, 8.0f) * reach;
}

// The factor for a shoulder-to-wrist chain (upper arm + forearm, `armLength` in model units) so the wrist stands
// where the controller's grip does, minus the palm (a wrist is that far behind the grip): half the span, less half
// the shoulder width, at the world's scale. 1 for anything not measured. Bounded: a wild span cannot make a
// giant's or a child's arm.
inline float ArmScale(float modelEye, float userEye, float spanM, float shoulderWidth, float armLength, float palm) {
    if (!std::isfinite(modelEye + userEye + spanM + shoulderWidth + armLength + palm) || modelEye <= 0 || userEye < 0.8f ||
        spanM < 0.85f || spanM > 2.6f || shoulderWidth < 0 || armLength < 0.01f || palm < 0)
        return 1.0f;
    const float reach = (spanM * UnitsPerMetre(modelEye, userEye) - shoulderWidth) * 0.5f - palm;
    return std::clamp(reach / armLength, 0.65f, 1.6f);
}

// A row-vector affine matrix (skeletons.cpp's Mat, 16 floats; the position in m[12..14]) scaled about a pivot: its
// axes by k, its place moved along the line from the pivot by k. The caller puts the hand's own axes back after.
inline void ScaleAbout(float* m, Vec pivot, float k) {
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col) m[row * 4 + col] *= k;
    m[12] = pivot.x + (m[12] - pivot.x) * k;
    m[13] = pivot.y + (m[13] - pivot.y) * k;
    m[14] = pivot.z + (m[14] - pivot.z) * k;
}

}  // namespace bodycal
