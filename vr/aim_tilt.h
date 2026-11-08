// aim_tilt.h - [body] flat_aim_tilt: flat first person (F3), the arms and what they hold tilt up and down with the crosshair, as a shooter's or Morrowind's
// do - look down at a small monster and the game's swing comes down onto it, look up and it rises. Serg (2026-11-07): "оружие не наклоняется, а всегда
// бьёт поверху". Drawn bones only: the game's attack, its hit, range and direction are untouched (vr/skeletons.cpp PoseBody turns the drawn pose and
// hands it to the skinning, headshadow::PublishOurPose - as [body] attack_lean and flat_weapon_adjust do).
//
// How, from the rig and the camera:
// - what turns: every bone under the chest (attacklean::Chest: the lowest bone both upper arms hang under) but the chest itself and the neck with the
//   head (they stay under the camera, [body] flat_hold_trunk) - the collarbones, the arms, the hands, the weapon and shield attach bones; a weapon or
//   shield attach bone that hangs elsewhere in a rig is taken along with what hangs under it, so a weapon never parts from its hand;
// - about what: the line through the middle of the two shoulders (the upper arms' places in this pose), across the way the body faces;
// - how far, always: the crosshair's own pitch (the camera's forward in the hero's model) times [body] flat_aim_tilt_pct, capped at
//   flat_aim_tilt_max_deg either way;
// - and while the hero's attack plays ([body] flat_aim_tilt_target, the "доводка"): when the crosshair's ray met a monster's body (the [input]
//   flat_body_pick capsules, the point where the ray went into it), the tilt eases from the look's to that point's elevation seen from the shoulders -
//   the blow comes to the body under the crosshair, whatever height the camera looks from. The point is held through the swing (the crosshair may slip
//   off a small monster as the blow comes) and let go when the swing has eased out.
// Positive = down (attacklean's sign: a forward bend about attacklean::Axis). Pure; the test is cleanroom/tests/aim_tilt_test.cpp.
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "attack_lean.h"

namespace aimtilt {

using attacklean::V3;

inline constexpr float kPctDefault = 100.0f;    // the arms follow the look all the way (the weapon stays where it is on the screen)
inline constexpr float kMaxDegDefault = 60.0f;  // past this the arms would come up into the camera (down) or out of the picture (up)
inline constexpr float kFreshMs = 250.0f;       // a body met by the crosshair this long ago still counts as under it

inline float Pct(float raw) { return std::isfinite(raw) ? std::clamp(raw, 0.0f, 150.0f) : kPctDefault; }
inline float MaxDeg(float raw) { return std::isfinite(raw) ? std::clamp(raw, 0.0f, 89.0f) : kMaxDegDefault; }

// The bones that tilt (1) of a rig of n: under the chest, not the chest, not the neck's own (inNeck: the neck's lowest bone and everything under it);
// and each of `extra` (the weapon and shield attach bones, -1 for none) that does not hang under the chest, with all under it. Empty: no chest.
inline std::vector<char> Marks(int n, const int* parent, int chest, const char* inNeck, const int* extra, int extraN) {
    if (n <= 0 || !parent || chest < 0 || chest >= n) return {};
    std::vector<char> m(n, 0);
    int any = 0;
    for (int i = 0; i < n; ++i)
        if (i != chest && !(inNeck && inNeck[i]) && attacklean::Under(parent, n, i, chest)) { m[i] = 1; ++any; }
    for (int e = 0; e < extraN && extra; ++e) {
        const int b = extra[e];
        if (b < 0 || b >= n || attacklean::Under(parent, n, b, chest)) continue;
        for (int i = 0; i < n; ++i)
            if (attacklean::Under(parent, n, i, b)) { m[i] = 1; ++any; }
    }
    return any ? m : std::vector<char>{};
}

// The look's tilt (radians, + down) for the crosshair's forward `fwd` in the hero's model (y up): its pitch times pct / 100, capped at maxDeg.
inline float LookRad(V3 fwd, float pct, float maxDeg) {
    const float l = std::sqrt(fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z);
    if (!(l > 1e-6f) || !std::isfinite(l)) return 0.0f;
    const float pitch = std::asin(std::clamp(fwd.y / l, -1.0f, 1.0f));   // + up
    const float cap = MaxDeg(maxDeg) * attacklean::kPi / 180.0f;
    return std::clamp(-pitch * Pct(pct) / 100.0f, -cap, cap);
}

// The target's tilt (radians, + down): `point`'s elevation seen from `pivot` (the shoulders' middle), capped at maxDeg. False: no usable point.
inline bool TargetRad(V3 pivot, V3 point, float maxDeg, float* out) {
    const float dx = point.x - pivot.x, dy = point.y - pivot.y, dz = point.z - pivot.z;
    if (!std::isfinite(dx + dy + dz)) return false;
    const float ahead = std::sqrt(dx * dx + dz * dz);
    if (!(ahead > 1e-4f) && !(std::fabs(dy) > 1e-4f)) return false;   // (the point at the shoulders themselves: no way to it)
    const float cap = MaxDeg(maxDeg) * attacklean::kPi / 180.0f;
    *out = std::clamp(-std::atan2(dy, ahead), -cap, cap);
    return true;
}

// The target held through a swing. Step once a pose: swinging (the hero's attack mode plays), a fresh body under the crosshair and its tilt, and the
// ease's weight after this pose's step. A fresh body while the swing plays (re)takes the target; it is let go once the swing is over and eased out.
struct Hold {
    bool have = false;
    float rad = 0.0f;
    void Step(bool swinging, bool fresh, float freshRad, float weight) {
        if (swinging && fresh && std::isfinite(freshRad)) { have = true; rad = freshRad; }
        else if (!swinging && !(weight > 0.0f)) have = false;
    }
};

// This pose's tilt: the look's, eased toward the held target's by the ease's weight.
inline float Blend(float look, bool have, float target, float weight) {
    return have ? look + (target - look) * std::clamp(weight, 0.0f, 1.0f) : look;
}

// The shoulders' middle: the two upper arms' places (model space).
inline V3 Mid(V3 a, V3 b) { return {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, (a.z + b.z) * 0.5f}; }

// The turn on a pose's model-space matrices w (n bones, row vectors): each marked bone by rad about the unit axis through pivot. Nothing else moves.
inline void Turn(float (*w)[16], int n, const std::vector<char>& marks, V3 axis, float rad, V3 pivot) {
    if ((int)marks.size() != n || rad == 0.0f || !std::isfinite(rad + pivot.x + pivot.y + pivot.z)) return;
    float t[16];
    attacklean::TurnAbout(axis, rad, pivot, t);
    for (int i = 0; i < n; ++i)
        if (marks[i]) attacklean::MulInto(w[i], t);
}

}  // namespace aimtilt
