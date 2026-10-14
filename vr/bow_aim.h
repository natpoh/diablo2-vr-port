// bow_aim.h - the bow along the hands: the maths of turning the bow in the hero's hand until its axis lies along the line from
// the string hand to the bow hand. Pure: no game calls, no globals; cleanroom/tests/bow_aim_test.cpp drives it with synthetic poses.
//
// A bow in the hero's hand is held as the game's animation holds it: rigid in the hand, turned the way the controller is. The
// player aims with the two hands - the line from the string hand through the bow hand is what the draw wire hands the game's
// attack - so while the string is taken the bow should lie along that line, as a real one does, with the arrow on it from the
// nock to the bow hand. The bow's frame is two unit vectors in its own cog frame (bow_rig.h: forward = away from the archer along
// the bow, up = from the lower limb tip to the upper); the hero's pose carries them into the model with the attach bone.
//
// The turn this makes is a rotation in the hero's model space, applied after the hand's own: the hand's turn is given by two
// directions (skeletons.cpp Wrist: where the hand points, where its knuckles run), and turning both by it turns the hand - and
// the attach bone, the bow and the arrow with it - by exactly that rotation about the palm.
//
// Roll: Follow keeps the controller's roll (the shortest swing: whatever the bow was turned about its own axis stays), Upright
// keeps the limbs' axis as upright as the aim allows (the bow's up as near the world's up as a plane across the aim has it).
// Blend: the turn comes in over a time when the string is taken and goes out when it is let go or the pull is cancelled.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "bow_rig.h"

namespace bowaim {

using bowrig::Mat;
using bowrig::V3;

enum class Roll : int { Follow = 0, Upright = 1 };

// The bow's axes in the model (unit, across each other): forward away from the archer along the bow, up along the limbs, and
// `side` the third one - the cog frame's own x, up x forward: toward the archer's LEFT in the hero's model space (its chirality is the
// head frame's, x right y up z back, which ToModel only turns about y), i.e. toward the bow arm's side, for a bow held upright.
// (Zero when the caller has not given it: FrameOf then makes it from the other two.)
struct Frame { V3 forward, up, side = {0.0f, 0.0f, 0.0f}; };

inline constexpr float kPi = 3.14159265358979f;

// The row-vector turn about the unit axis `axis` by `angle` (rad).
inline Mat RotationAbout(V3 axis, float angle) {
    const V3 k = bowrig::Unit(axis);
    const float c = std::cos(angle), s = std::sin(angle), t = 1.0f - c;
    const float kv[3] = {k.x, k.y, k.z};
    const float cross[3][3] = {{0, -k.z, k.y}, {k.z, 0, -k.x}, {-k.y, k.x, 0}};
    Mat r = bowrig::Identity();
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r.m[i * 4 + j] = (i == j ? c : 0.0f) + s * cross[j][i] + t * kv[i] * kv[j];
    return r;
}

inline float AngleDeg(V3 a, V3 b) {
    const V3 ua = bowrig::Unit(a), ub = bowrig::Unit(b);
    if (bowrig::Length(ua) < 0.5f || bowrig::Length(ub) < 0.5f) return 0.0f;
    return std::acos(std::clamp(bowrig::Dot(ua, ub), -1.0f, 1.0f)) * 57.2957795f;
}

// The aim: from the string hand to the bow hand, unit. False when the hands are nearer than minLen (the line has no direction
// then) or a hand is not a number.
inline bool AimVector(V3 stringHand, V3 bowHand, float minLen, V3* out) {
    if (!out || !bowrig::Finite(stringHand) || !bowrig::Finite(bowHand)) return false;
    const V3 d = bowHand - stringHand;
    const float l = bowrig::Length(d);
    if (!(l > minLen) || !(l > 1e-6f)) return false;
    *out = d * (1.0f / l);
    return true;
}

// The bow's frame in the model: its two axes in the attach bone's frame carried by the attach bone's matrix (a scale in it goes).
// `sideInAttach` (zero: not given) is the third axis the same way; it is made across the other two, with the sign it came with.
inline Frame FrameOf(const Mat& attach, V3 forwardInAttach, V3 upInAttach, V3 sideInAttach = {0.0f, 0.0f, 0.0f}) {
    Frame f;
    f.forward = bowrig::Unit(bowrig::Direction(attach, forwardInAttach));
    const V3 u = bowrig::Unit(bowrig::Direction(attach, upInAttach));
    f.up = bowrig::Unit(u - f.forward * bowrig::Dot(u, f.forward));   // across the forward axis, whatever the matrix did
    const V3 made = bowrig::Unit(bowrig::Cross(f.up, f.forward));     // x = y x z
    if (bowrig::Length(sideInAttach) > 0.5f) {
        const V3 given = bowrig::Unit(bowrig::Direction(attach, sideInAttach));
        f.side = bowrig::Dot(made, given) < 0.0f ? made * -1.0f : made;   // across both, on the side the matrix carried the given axis to
    } else {
        f.side = made;
    }
    return f;
}

// A point's offset from the bow hand's reference point, given in the bow's own frame: `x` along the bow (+ toward the target), `y`
// across it (+ toward the archer's left), `z` up (+ along the limbs, toward the upper one) - in the unit the frame's vectors are in.
inline V3 OffsetIn(const Frame& f, float x, float y, float z) {
    return bowrig::Unit(f.forward) * x + bowrig::Unit(f.side) * y + bowrig::Unit(f.up) * z;
}

namespace detail {
// The turn that carries direction f onto a, `s` of the way (0..1) along the shortest swing. Opposite directions swing about `side`.
inline Mat Swing(V3 f, V3 a, V3 side, float s) {
    const V3 axis = bowrig::Cross(f, a);
    const float sine = bowrig::Length(axis), cosine = bowrig::Dot(f, a);
    if (sine < 1e-6f) {
        if (cosine > 0.0f) return bowrig::Identity();
        V3 about = side - f * bowrig::Dot(side, f);
        if (bowrig::Length(about) < 1e-3f) about = bowrig::Cross(f, std::fabs(f.x) < 0.9f ? V3{1, 0, 0} : V3{0, 1, 0});
        return RotationAbout(about, kPi * s);
    }
    return RotationAbout(axis, std::atan2(sine, cosine) * s);
}
}  // namespace detail

// The turn (a rotation matrix, no translation, applied after the bow's own pose) that takes the bow's frame `cur` toward the aim
// `aim`, `s` of the way (0 = not at all, 1 = its forward axis along the aim). Roll::Follow: the shortest swing, so the bow keeps
// the roll the controller has. Roll::Upright: and a twist about the aim until the limbs' axis is as near `worldUp` as it can
// be (when the aim is itself up or down there is no such thing: Follow). Not numbers or no frame: no turn.
inline Mat AimTurn(const Frame& cur, V3 aim, Roll roll, V3 worldUp, float s) {
    const V3 f = bowrig::Unit(cur.forward), a = bowrig::Unit(aim);
    V3 u = cur.up - f * bowrig::Dot(cur.up, f);
    u = bowrig::Unit(u);
    if (!bowrig::Finite(f) || !bowrig::Finite(a) || !(bowrig::Length(f) > 0.5f) || !(bowrig::Length(a) > 0.5f) || !std::isfinite(s)) return bowrig::Identity();
    s = std::clamp(s, 0.0f, 1.0f);
    if (s <= 0.0f) return bowrig::Identity();
    const Mat swing = detail::Swing(f, a, u, s);
    if (roll != Roll::Upright) return swing;
    // the twist about the swung forward axis, from where the limbs' axis lies after the whole swing to the upright one
    const Mat full = detail::Swing(f, a, u, 1.0f);
    const V3 uFull = bowrig::Unit(bowrig::Direction(full, u));
    V3 upright = worldUp - a * bowrig::Dot(worldUp, a);
    if (!bowrig::Finite(upright) || bowrig::Length(upright) < 0.1f) return swing;   // aimed up or down: no upright to speak of
    upright = bowrig::Unit(upright);
    const float phi = std::atan2(bowrig::Dot(bowrig::Cross(uFull, upright), a), bowrig::Dot(uFull, upright));
    const V3 f1 = bowrig::Unit(bowrig::Direction(swing, f));
    return bowrig::Mul(swing, RotationAbout(f1, phi * s));
}

// A hand's plan - the two directions the wrist is turned by (where the hand points, where its knuckles run) - turned by `turn`.
inline void TurnPlan(V3* point, V3* along, const Mat& turn) {
    if (point) *point = bowrig::Unit(bowrig::Direction(turn, *point));
    if (along) *along = bowrig::Unit(bowrig::Direction(turn, *along));
}

// The arrow's rest point. The arrow lies on the string, along the line from the string hand through the bow hand - and what the player
// sees is the arrow passing over the hand at a place that slid about with the hands' places, "now on the hand as it should be, now
// through the middle of the palm": the line passed through the controller's reference point, and where that is in the fist is
// not the same from one pose to the next. Here the point the arrow passes the bow hand at is FIXED in the bow hand's own frame: an
// offset (x along the bow, y across it, z up) from the reference point, carried with the hand as the bow is displayed, and the
// aim is the line from the string hand THROUGH that point.
//
// The bow as displayed is turned along the aim (about the hand, AimTurn), and the aim passes through a point that is fixed in the frame
// of the bow as turned - the aim is on both sides of the equation. It is solved by iteration (the offset is small against the hands'
// distance, so each round moves the aim by a fraction of the last): aim -> the bow's turn for it -> the point -> the aim through it.
// `turnsBow` false: the bow is not turned (the controller's own frame is displayed) and the first round is the answer. When it does
// not settle - the hands nearly on one another with a big offset - the controller-held frame's point is used, which is still fixed
// to the hand, just not to the turn it is displayed with. `offset` is in the unit of the hands (what OffsetIn gave, in the
// controller-held frame `cur`).
struct RestSolution {
    bool ok = false;           // there is a line (the hands are far enough apart)
    V3 aim{0.0f, 0.0f, 0.0f};  // unit, from the string hand through the point
    V3 point{0.0f, 0.0f, 0.0f};  // the rest point, where the aim passes the bow hand
    int steps = 0;
    float residualDeg = 0.0f;  // the aim's change in the last round: how far it is from settled
    bool settled = true;
    bool damped = false;       // the rounds taken whole swung about and it was the half-rounds that settled it
};
inline RestSolution SolveRest(V3 stringHand, V3 bowHand, const Frame& cur, V3 offset, bool turnsBow, Roll roll, V3 worldUp, float blend, float minLen) {
    RestSolution r;
    V3 a;
    if (!AimVector(stringHand, bowHand, minLen, &a) || !bowrig::Finite(offset)) return r;
    r.aim = a;
    r.point = bowHand;
    r.ok = true;
    if (!(bowrig::Length(offset) > 1e-9f)) return r;   // nothing to rest on: the line through the hand's own point, as it always was
    const bool turned = turnsBow && std::isfinite(blend) && blend > 0.0f;
    V3 p = bowHand + offset;
    if (!turned) {   // the controller-held frame is the one displayed: the point is where the offset puts it, no more
        if (!AimVector(stringHand, p, minLen, &a)) { r.ok = false; return r; }
        r.aim = a; r.point = p; r.steps = 1;
        return r;
    }
    // Round by round: the aim -> the turn it makes of the bow -> the point -> the aim through it. A first try with each round taken whole; if that
    // does not settle (the bow far from the line, the hands near, the offset big: the rounds overshoot and swing about), another with each round
    // taken half - slower, but it settles where the whole step swings.
    float diff = 0.0f;
    int steps = 0;
    bool done = false;
    for (const float weight : {1.0f, 0.5f}) {
        V3 x;
        if (!AimVector(stringHand, bowHand, minLen, &x)) { r.ok = false; return r; }
        steps = 0;
        for (const int limit = weight < 1.0f ? 96 : 32; steps < limit;) {
            const Mat t = AimTurn(cur, x, roll, worldUp, blend);
            p = bowHand + bowrig::Direction(t, offset);
            V3 next;
            if (!AimVector(stringHand, p, minLen, &next)) { r.ok = false; return r; }
            ++steps;
            diff = AngleDeg(x, next) / weight;   // (a half round moves half as far: the distance still to go is the round's over its weight)
            const V3 mix = weight < 1.0f ? bowrig::Unit(x * (1.0f - weight) + next * weight) : next;
            x = bowrig::Length(mix) > 0.5f ? mix : next;
            if (diff < 0.003f) break;
        }
        a = x;
        r.damped = weight < 1.0f;
        if (diff <= 0.2f) { done = true; break; }   // (the last round changed the aim by under a fifth of a degree: under a millimetre at the hand)
    }
    if (done && !AimVector(stringHand, p, minLen, &a)) { r.ok = false; return r; }   // the aim exactly through the point it came to
    r.steps = steps;
    r.residualDeg = diff;
    if (!done) {   // it did not settle either way: the controller-held frame's point, which is still fixed to the hand
        r.settled = false;
        p = bowHand + offset;
        if (!AimVector(stringHand, p, minLen, &a)) { r.ok = false; return r; }
    }
    r.aim = a;
    r.point = p;
    return r;
}

// ---- the arrow clear of the hand and the forearm ----
// The arrow lies on the line from the string hand through the rest point. The bow hand (the fist, the back of the hand, the wrist) and the
// forearm behind it are in the way: with the string hand low or far to one side, or the hand turned out, the line goes through the wrist
// or the forearm however well the rest point sits over the knuckle ("the shaft cuts through the middle of the wrist and the medallion",
// "its rear half crosses the forearm"). The bow arm is a few capsules (an axis and a radius at each end) and the arrow a segment with a
// thickness; when the arrow is nearer to an axis than the capsule's radius plus the arrow's, it is lifted - the nock and the arrow together,
// parallel to the line, up the bow's limb axis - by as little as clears it, within a limit. When nothing within the limit clears it, the
// lift that comes nearest is taken, never one that is worse than none.
struct Capsule {
    V3 a, b;          // the axis
    float ra, rb;     // the radius at a and at b
};

// The distance between the segments p0-p1 and q0-q1, and where along each (0..1) the nearest points are.
inline float SegmentDistance(V3 p0, V3 p1, V3 q0, V3 q1, float* sOut = nullptr, float* tOut = nullptr) {
    const V3 d1 = p1 - p0, d2 = q1 - q0, r = p0 - q0;
    const float a = bowrig::Dot(d1, d1), e = bowrig::Dot(d2, d2), f = bowrig::Dot(d2, r);
    float s = 0.0f, t = 0.0f;
    const float eps = 1e-12f;
    if (a <= eps && e <= eps) {
        s = t = 0.0f;
    } else if (a <= eps) {
        t = std::clamp(f / e, 0.0f, 1.0f);
    } else {
        const float c = bowrig::Dot(d1, r);
        if (e <= eps) {
            s = std::clamp(-c / a, 0.0f, 1.0f);
        } else {
            const float b = bowrig::Dot(d1, d2), denom = a * e - b * b;
            s = denom > eps ? std::clamp((b * f - c * e) / denom, 0.0f, 1.0f) : 0.0f;
            t = (b * s + f) / e;
            if (t < 0.0f) { t = 0.0f; s = std::clamp(-c / a, 0.0f, 1.0f); }
            else if (t > 1.0f) { t = 1.0f; s = std::clamp((b - c) / a, 0.0f, 1.0f); }
        }
    }
    if (sOut) *sOut = s;
    if (tOut) *tOut = t;
    return bowrig::Length((p0 + d1 * s) - (q0 + d2 * t));
}

// How far the arrow segment a0-a1 (thickness `shaft` as a radius) is from touching the capsules, with `margin` to spare: the least over them
// of (the distance between the axes - the capsule's radius where they are nearest - the shaft - the margin). Negative: through one.
inline float ArmClearance(V3 a0, V3 a1, const Capsule* caps, int n, float shaft, float margin) {
    float least = 1e9f;
    for (int i = 0; i < n; ++i) {
        float s, t;
        const float d = SegmentDistance(a0, a1, caps[i].a, caps[i].b, &s, &t);
        least = std::min(least, d - (caps[i].ra + (caps[i].rb - caps[i].ra) * t) - shaft - margin);
    }
    return least;
}

struct ClearLift {
    float lift = 0.0f;        // how far the arrow is lifted along `up`, in the unit of the points
    float before = 0.0f;      // the clearance with no lift
    float after = 0.0f;       // and with it
    bool cleared = true;      // the lift clears the arm (else it is the nearest to doing so)
};
// The arrow runs from the string hand (the nock) through `restPoint` and `tipLength` on beyond it. Lifted by h along `up` it is parallel to that line.
// The least h in [0, maxLift] (tried every `step`) that clears; failing that the h with the most clearance (the least of them on a tie).
inline ClearLift ClearanceLift(V3 stringHand, V3 restPoint, V3 up, float tipLength, const Capsule* caps, int n, float shaft, float margin, float maxLift, float step) {
    ClearLift r;
    const V3 line = restPoint - stringHand;
    const float len = bowrig::Length(line);
    if (!(len > 1e-6f) || !bowrig::Finite(up) || !(bowrig::Length(up) > 0.5f) || !(maxLift >= 0.0f) || !(step > 0.0f) || n <= 0) return r;
    const V3 u = bowrig::Unit(up), tip = restPoint + line * (tipLength / len);
    auto at = [&](float h) { return ArmClearance(stringHand + u * h, tip + u * h, caps, n, shaft, margin); };
    r.before = r.after = at(0.0f);
    if (r.before >= 0.0f) return r;
    float best = r.before, bestH = 0.0f;
    for (float h = step; h <= maxLift + 1e-6f; h += step) {
        const float c = at(h);
        if (c >= 0.0f) { r.lift = h; r.after = c; r.cleared = true; return r; }
        if (c > best + 1e-7f) { best = c; bestH = h; }
    }
    r.lift = bestH;
    r.after = best;
    r.cleared = false;
    return r;
}

// An exponential approach to a moving target, for what is found frame by frame and must not flicker between two answers: `value` goes to
// the target at the rate 1/tauMs per ms. A gap longer than `idleMs` starts from the target.
struct Smooth {
    float value = 0.0f;
    uint64_t at = 0;
    bool started = false;
    float Step(float target, uint64_t nowMs, float tauMs, uint64_t idleMs = 300) {
        if (!std::isfinite(target)) target = 0.0f;
        if (!started || nowMs < at || nowMs - at > idleMs) { value = target; started = true; at = nowMs; return value; }
        const float k = 1.0f - std::exp(-(float)(nowMs - at) / std::max(tauMs, 1.0f));
        value += (target - value) * k;
        at = nowMs;
        return value;
    }
    void Reset() { value = 0.0f; at = 0; started = false; }
};

// How far the turn is in: 0 while the bow is as the controller holds it, 1 along the hands. Moves at 1/blendMs per ms toward the
// target - 1 while the string is taken (or drawn), 0 otherwise - and returns the eased value, so take, release and cancel all
// come and go over the same time, and a change of mind half way turns back from where it is, with no jump.
struct Blend {
    float value = 0.0f;
    uint64_t at = 0;
    bool started = false;
    float Step(bool engaged, uint64_t nowMs, float blendMs) {
        blendMs = std::isfinite(blendMs) ? std::clamp(blendMs, 10.0f, 2000.0f) : 120.0f;
        if (started && nowMs > at) {
            const float dt = (float)std::min<uint64_t>(nowMs - at, 500);   // a stall of the game is not a longer blend
            const float step = dt / blendMs;
            value = engaged ? std::min(1.0f, value + step) : std::max(0.0f, value - step);
        } else if (!started) {
            value = 0.0f;
        }
        started = true;
        at = nowMs;
        return Eased();
    }
    // For a caller that is not run every frame - the bow's own hook runs only while there is something to pose: after a gap longer than
    // `idleMs` it starts from nothing, instead of counting the stall clamp as time that passed (which would take it in at once).
    float StepSparse(bool engaged, uint64_t nowMs, float blendMs, uint64_t idleMs = 300) {
        if (started && nowMs > at && nowMs - at > idleMs) Reset();
        return Step(engaged, nowMs, blendMs);
    }
    float Eased() const { return value * value * (3.0f - 2.0f * value); }
    void Reset() { value = 0.0f; started = false; at = 0; }
};

// Which wrist the bow hangs on. The game shows the attach bone of the wrist it hangs the bow on and shrinks the other one - unless the
// other slot holds a quiver (an Amazon's: the bow in the right slot, the arrows in the left): both bones are shown then ("R 1.00
// L 1.00" in the log) and the bones cannot tell. The bow is held in the hand the player aims with, which the draw detector names
// as the opposite of the string hand, so that decides; vrcam's aim hand does when the detector says nothing yet.
// `size*` are the attach bones' sizes against their hands in the game's own pose (negative or NaN: not read).
enum class SideBy : int { Nothing = 0, AttachBones = 1, DrawFeed = 2, AimHand = 3 };
struct BowWrist {
    int side = -1;   // 0 right, 1 left, -1 not known
    SideBy by = SideBy::Nothing;
};
inline BowWrist ChooseBowWrist(float sizeRight, float sizeLeft, int stringHandFed, int aimHand) {
    if (sizeLeft > 0.7f && sizeRight < 0.3f) return {1, SideBy::AttachBones};
    if (sizeRight > 0.7f && sizeLeft < 0.3f) return {0, SideBy::AttachBones};
    if (stringHandFed == 0 || stringHandFed == 1) return {stringHandFed ^ 1, SideBy::DrawFeed};
    if (aimHand == 0 || aimHand == 1) return {aimHand, SideBy::AimHand};
    return {};
}
inline const char* SideByName(SideBy by) {
    switch (by) {
        case SideBy::AttachBones: return "the attach bones";
        case SideBy::DrawFeed: return "the draw's feed";
        case SideBy::AimHand: return "the aim hand";
        default: return "nothing";
    }
}

}  // namespace bowaim
