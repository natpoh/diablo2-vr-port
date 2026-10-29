// staff_upright.h - [hands] staff_upright_arm: a staff held upright aims along the arm, shoulder through hand, blended with its own axis as it tilts. Pure.
//
// D2R aims a spell at a point on the ground: only the line's heading counts. A staff held the usual way, upright, has almost no part on the ground,
// so its heading is a few degrees of tilt and the tracking's noise, and the spell went wherever that wobble pointed (or, within 5 degrees of
// vertical, to the hand's fingers: vr/spell_aim_model.h). Here, near upright, the ARM aims - the drawn shoulder to the drawn palm - and as the staff
// tilts away from upright its own axis takes over, smoothly:
//
//   within fullDeg of vertical (either end up)   the arm alone
//   fullDeg .. noneDeg                           a smoothstep from the arm to the staff, by the angle off vertical
//   noneDeg or more                              the staff alone, as before
//
// The heading is turned from the arm's toward the staff's by that share (the shorter way round), and the slope likewise, so a staff tipped toward a
// side sends the cast that way and an upright one follows the arm. An arm shorter than kMinArmM, or one pointing within 5 degrees of straight up or
// down, has no heading: no blend, the staff's line as before.
//
// Idea after bearstonem/d2r-pcvr (MIT), commit 787f820 "staff aims along the arm": vr/vrcam.cpp StaffByHand / HandRay - a staff whose shaft is more
// than ~35 degrees from flat casts from the right shoulder (a fixed 17 cm right of and 22 cm below the eyes) through the hand, a hard switch. Here the
// shoulder is the drawn hero's own (skel::ArmReach, fitted to the player's span), the hand the drawn palm, and the switch is a blend.
#pragma once

#include <cmath>

namespace staffupright {

constexpr float kPi = 3.14159265358979f;
// an arm (shoulder -> palm) shorter than this gives no line, metres (d2r-pcvr's 15 cm)
constexpr float kMinArmM = 0.15f;
// sin 5 degrees: an arm steeper than this has no heading on the ground (vr/spell_aim_model.h kSteepSin)
constexpr float kSteepSin = 0.0871557f;
// defaults of [hands] staff_upright_full_deg / staff_upright_none_deg, degrees off vertical
constexpr float kFullDeg = 10.0f, kNoneDeg = 30.0f;

inline float Len3(const float v[3]) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

// How far the line d lies from vertical, degrees 0..90 (either end up); -1 when d is no direction.
inline float OffVerticalDeg(const float d[3]) {
    const float l = Len3(d);
    if (!std::isfinite(l) || l < 1e-6f) return -1.0f;
    const float c = std::fmin(1.0f, std::fabs(d[1]) / l);
    return std::acos(c) * 180.0f / kPi;
}

// The arm's share of the aim for a staff along d: 1 within fullDeg of vertical, 0 from noneDeg, a smoothstep between (noneDeg <= fullDeg: a step at
// fullDeg). 0 when d is no direction or the numbers are not.
inline float ArmWeight(const float d[3], float fullDeg, float noneDeg) {
    const float t = OffVerticalDeg(d);
    if (t < 0.0f || !std::isfinite(fullDeg) || !std::isfinite(noneDeg)) return 0.0f;
    if (t <= fullDeg) return 1.0f;
    if (t >= noneDeg || noneDeg <= fullDeg) return 0.0f;
    const float x = (noneDeg - t) / (noneDeg - fullDeg);   // 1 at fullDeg, 0 at noneDeg
    return x * x * (3.0f - 2.0f * x);
}

// The arm as a unit line from the shoulder to the hand (both in one frame, metres); false when it is shorter than kMinArmM, not finite, or has no
// heading (within 5 degrees of straight up or down).
inline bool Arm(const float shoulder[3], const float hand[3], float out[3]) {
    float a[3];
    for (int i = 0; i < 3; ++i) a[i] = hand[i] - shoulder[i];
    const float l = Len3(a);
    if (!std::isfinite(l) || l < kMinArmM) return false;
    for (int i = 0; i < 3; ++i) out[i] = a[i] / l;
    return std::sqrt(out[0] * out[0] + out[2] * out[2]) >= kSteepSin;
}

// The line between the staff's (any length) and the arm's (unit, from Arm) by the arm's share w (0..1): the heading on the ground turned from the arm's
// toward the staff's by 1 - w the shorter way round, the slope likewise. y is up; the ground is x, z. out is unit. False when the inputs are not lines.
inline bool Blend(const float staff[3], const float arm[3], float w, float out[3]) {
    const float sl = Len3(staff), al = Len3(arm);
    if (!std::isfinite(sl) || !std::isfinite(al) || !std::isfinite(w) || sl < 1e-6f || al < 1e-6f) return false;
    w = std::fmin(1.0f, std::fmax(0.0f, w));
    const float ah = std::sqrt(arm[0] * arm[0] + arm[2] * arm[2]);
    if (ah < 1e-6f) return false;
    const float sh = std::sqrt(staff[0] * staff[0] + staff[2] * staff[2]);
    const float armYaw = std::atan2(arm[0], arm[2]), armPitch = std::atan2(arm[1], ah);
    float yaw = armYaw, pitch = armPitch;
    if (w < 1.0f) {
        const float staffPitch = std::atan2(staff[1], sh);
        if (sh >= 1e-6f) {   // (a staff exactly upright has no heading: the arm's alone)
            float delta = std::atan2(staff[0], staff[2]) - armYaw;
            while (delta > kPi) delta -= 2.0f * kPi;
            while (delta < -kPi) delta += 2.0f * kPi;
            yaw = armYaw + (1.0f - w) * delta;
        }
        pitch = w * armPitch + (1.0f - w) * staffPitch;
    }
    const float c = std::cos(pitch);
    out[0] = c * std::sin(yaw);
    out[1] = std::sin(pitch);
    out[2] = c * std::cos(yaw);
    return true;
}

}  // namespace staffupright
