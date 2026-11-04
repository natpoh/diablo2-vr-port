// attack_lean.h - [body] attack_lean: in the 2D views (flat F2 / F3, VR F2) the hero's swing leans toward where the crosshair looks - aim down at a
// small monster and the blade's arc comes down onto it, aim up and it rises. Drawn bones only: the game's attack, its hit check, range and direction
// are untouched (vr/skeletons.cpp PoseBody turns the drawn pose and hands it to the skinning, headshadow::PublishOurPose).
//
// How, all from the rig and the camera, nothing tuned by hand:
// - the chest: the lowest bone both upper arms hang from (spine_03 in D2R's hero rigs); the bend: spine_01 up to it, each joint of that chain
//   turning what hangs from it by an equal share of the lean, about its own place - the spine curves, the shoulders, arms, the weapon attach
//   bones (so the weapon's own tilt), the neck and the head ride on it; the hips and legs stay the game's;
// - the aimed point: where the crosshair's ray (the camera's eye and forward in the hero's model space) first meets either the floor under the
//   hero (his feet's level in this pose) or his reach about the chest (a cylinder whose radius is his own eye height - arm and weapon together
//   are about a body's height); a ray that meets neither (looking up past the reach from inside it) counts by its own pitch;
// - the lean: that point's elevation seen from the chest, capped at [body] attack_lean_max_deg, eased in when the hero's attack mode starts and
//   out when it ends (the game's own mode on its client unit: the attacks, the throw, the kick, the skill and sequence modes - not a cast).
// Pure; the test is cleanroom/tests/attack_lean_test.cpp.
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace attacklean {

struct V3 { float x, y, z; };

inline constexpr float kPi = 3.14159265f;
inline constexpr float kEaseInMs = 120.0f;    // from the attack mode's first pose to the whole lean
inline constexpr float kEaseOutMs = 150.0f;   // from its end back to the game's own pose

// Diablo II's unit modes the lean plays in: 7 / 8 the attacks, 11 the throw, 12 the kick, 13 - 16 the skill modes, 18 the sequence (Zeal, Fend,
// Strafe, Fury). Not 10 (a cast: the spell's own aim), nor walking, standing, a hit, a block or death.
inline bool IsSwingMode(int m) { return m == 7 || m == 8 || (m >= 11 && m <= 16) || m == 18; }

// Is bone i the bone b or hung under it (parents walked up, a broken list ends)?
inline bool Under(const int* parent, int n, int i, int b) {
    for (int k = i, steps = 0; k >= 0 && k < n && steps <= n; k = parent[k], ++steps)
        if (k == b) return true;
    return false;
}

// The chest: the lowest bone that both upper arms hang under; -1 when either is missing or that bone is not spine_01 or above it.
inline int Chest(int n, const int* parent, int spine, int upperR, int upperL) {
    if (n <= 0 || spine < 0 || spine >= n || upperR < 0 || upperR >= n || upperL < 0 || upperL >= n) return -1;
    for (int b = parent[upperR], steps = 0; b >= 0 && b < n && steps <= n; b = parent[b], ++steps)
        if (Under(parent, n, upperL, b)) return Under(parent, n, b, spine) ? b : -1;
    return -1;
}

// spine_01 up to the chest, spine_01 first; empty when the chest is not above spine_01.
inline std::vector<int> Chain(int n, const int* parent, int spine, int chest) {
    std::vector<int> up;
    if (chest < 0 || chest >= n || spine < 0 || spine >= n) return {};
    for (int b = chest, steps = 0; b >= 0 && b < n && steps <= n; b = parent[b], ++steps) {
        up.push_back(b);
        if (b == spine) return std::vector<int>(up.rbegin(), up.rend());
    }
    return {};
}

enum class Met { None, Floor, Reach, Ray };
inline const char* MetName(Met m) {
    return m == Met::Floor ? "the floor" : m == Met::Reach ? "the reach" : m == Met::Ray ? "nothing (the ray's own pitch)" : "-";
}

struct Aim {
    float elevation = 0.0f;   // radians, + up: the aimed point seen from the chest
    float pitch = 0.0f;       // radians, + up: the crosshair's ray itself
    float ahead = 0.0f;       // the aimed point's distance from the chest across the floor (model units; 0 for Met::Ray)
    Met by = Met::None;
};

// The crosshair's ray from eye along fwd (model space, y up), the chest, the floor's height and the reach's radius. False: no usable ray.
inline bool AimAt(V3 eye, V3 fwd, V3 chest, float floorY, float reach, Aim* out) {
    const float l = std::sqrt(fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z);
    if (!(l > 1e-6f) || !std::isfinite(l + eye.x + eye.y + eye.z + chest.x + chest.y + chest.z + floorY + reach)) return false;
    const V3 f{fwd.x / l, fwd.y / l, fwd.z / l};
    Aim a;
    a.pitch = std::asin(std::clamp(f.y, -1.0f, 1.0f));
    float t = -1.0f;
    if (f.y < -1e-4f) {   // down to the floor
        const float tf = (floorY - eye.y) / f.y;
        if (tf > 0.0f) { t = tf; a.by = Met::Floor; }
    }
    const float qa = f.x * f.x + f.z * f.z;
    if (reach > 0.0f && qa > 1e-8f) {   // out through the reach's cylinder about the chest (the far crossing: in front of a camera inside it or behind it)
        const float dx = eye.x - chest.x, dz = eye.z - chest.z;
        const float qb = 2.0f * (dx * f.x + dz * f.z), qc = dx * dx + dz * dz - reach * reach;
        const float disc = qb * qb - 4.0f * qa * qc;
        if (disc >= 0.0f) {
            const float tr = (-qb + std::sqrt(disc)) / (2.0f * qa);
            if (tr > 0.0f && (t < 0.0f || tr < t)) { t = tr; a.by = Met::Reach; }
        }
    }
    if (t < 0.0f) {
        a.by = Met::Ray;
        a.elevation = a.pitch;
    } else {
        const V3 p{eye.x + f.x * t, eye.y + f.y * t, eye.z + f.z * t};
        a.ahead = std::sqrt((p.x - chest.x) * (p.x - chest.x) + (p.z - chest.z) * (p.z - chest.z));
        a.elevation = std::atan2(p.y - chest.y, std::max(a.ahead, 1e-4f));
    }
    *out = a;
    return true;
}

// The bend for an elevation: a point below the chest bends him forward (positive, about Axis below), capped at maxDeg, times the ease's weight.
inline float LeanRad(float elevation, float maxDeg, float weight) {
    const float cap = std::clamp(maxDeg, 0.0f, 90.0f) * kPi / 180.0f;
    return -std::clamp(elevation, -cap, cap) * std::clamp(weight, 0.0f, 1.0f);
}

// The axis a forward bend turns about for a hero facing `facing` (across the floor): up x facing - a positive turn tips up toward the facing.
inline V3 Axis(V3 facing) {
    const float l = std::sqrt(facing.x * facing.x + facing.z * facing.z);
    if (!(l > 1e-6f)) return {1.0f, 0.0f, 0.0f};
    return {facing.z / l, 0.0f, -facing.x / l};
}

// A turn by rad about the unit axis through pivot, as a row-vector 4x4 (p' = p * T; the translation in [12..14]) - the skeleton's matrices' form.
inline void TurnAbout(V3 k, float rad, V3 pivot, float out[16]) {
    const float c = std::cos(rad), s = std::sin(rad), u = 1.0f - c;
    const float rc[3][3] = {   // Rodrigues, column vectors: v' = rc v
        {c + k.x * k.x * u, k.x * k.y * u - k.z * s, k.x * k.z * u + k.y * s},
        {k.y * k.x * u + k.z * s, c + k.y * k.y * u, k.y * k.z * u - k.x * s},
        {k.z * k.x * u - k.y * s, k.z * k.y * u + k.x * s, c + k.z * k.z * u}};
    for (int r = 0; r < 3; ++r) {
        for (int col = 0; col < 3; ++col) out[r * 4 + col] = rc[col][r];   // row vectors: the transpose
        out[r * 4 + 3] = 0.0f;
    }
    const float p[3] = {pivot.x, pivot.y, pivot.z};
    for (int col = 0; col < 3; ++col) out[12 + col] = p[col] - (p[0] * out[col] + p[1] * out[4 + col] + p[2] * out[8 + col]);
    out[15] = 1.0f;
}

inline void MulInto(float a[16], const float b[16]) {   // a = a * b
    float r[16] = {};
    for (int row = 0; row < 4; ++row)
        for (int k = 0; k < 4; ++k)
            for (int col = 0; col < 4; ++col) r[row * 4 + col] += a[row * 4 + k] * b[k * 4 + col];
    for (int i = 0; i < 16; ++i) a[i] = r[i];
}

// The bend itself on a pose's model-space matrices w (n bones, row vectors): for each chain bone, spine_01 first, that bone and all under it turn
// by rad / (chain length) about that bone's own place as it stands after the turns below it. Nothing else moves.
inline void Bend(float (*w)[16], int n, const int* parent, const std::vector<int>& chain, V3 axis, float rad) {
    if (chain.empty() || rad == 0.0f || !std::isfinite(rad)) return;
    const float share = rad / (float)chain.size();
    std::vector<char> under(n > 0 ? n : 0, 0);
    for (const int b : chain) {
        if (b < 0 || b >= n) return;
        float t[16];
        TurnAbout(axis, share, {w[b][12], w[b][13], w[b][14]}, t);
        for (int i = 0; i < n; ++i)
            if (Under(parent, n, i, b)) MulInto(w[i], t);
    }
}

// The lean's weight through an attack: in while the hero is in a swing mode, out after it; smoothed both ways.
struct Ease {
    float t = 0.0f;   // 0..1, linear
    float Step(bool swinging, float dtMs) {
        dtMs = std::clamp(dtMs, 0.0f, 100.0f);   // (a stall is not a whole ease)
        t = swinging ? std::min(1.0f, t + dtMs / kEaseInMs) : std::max(0.0f, t - dtMs / kEaseOutMs);
        return Weight();
    }
    float Weight() const { return t * t * (3.0f - 2.0f * t); }
};

}  // namespace attacklean
