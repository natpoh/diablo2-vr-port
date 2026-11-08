#pragma once
// pose_zones.h - a hand pose's tolerance zone (Serg, 2026-11-08: "the cloud means the size of the space the hand is in, not a copy of the picture; that
// size must be visible from inside while the movement is made, editable, shown or hidden like BodyWalk's spheres"): a sphere round each hand of the pose
// at the spot the take recorded, as big as the zone - inside it the hand counts as in the pose.
//
// Where a zone is: in the body frame the engine compares in (shared/gesture_engine_abi.h BWGE_PoseZone) - the pose's hand joint, metres from the
// pelvis (x right, y up, z back, BW_LinkGesture.joints), the pelvis found from the live head each frame:
//   chest  = head - 0.07 H headForward - 0.04 H headUp - 0.11 H worldUp   (the head's own forward / up, pitch included; H the body's height)
//   pelvis = chest - 0.42 m worldUp, axes the head's forward laid level
// so the ball stands exactly where the engine looks for the hand. One radius for both hands of a two-hand pose (the engine's posTriggerRadius,
// BWGE_GetPoseZone / BWGE_SetPoseZone). While a pose is being taken the zone follows the hand instead (its size seen round the hand).
// Pure: no Windows, no OpenXR (cleanroom/tests/gesture_overlay_test.cpp); vr/xr.cpp HangZones puts them in the room.

#include <cmath>
#include <cstdint>

#include "skill_link_abi.h"

namespace posezones {

// a zone's radius: the engine's default (0.1 arm lengths of BodyWalk's 0.6 m arm), its bounds (BWGE_SetPoseZone's), the +/- step
constexpr float kDefaultM = 0.06f, kMinM = 0.03f, kMaxM = 0.50f, kStepM = 0.01f;
constexpr float kDefaultHeightM = 1.7f;   // the body's height when the engine has not said (BodyWalk's own default)
inline float Clamp(float r) { return std::isfinite(r) ? (r < kMinM ? kMinM : r > kMaxM ? kMaxM : r) : kDefaultM; }
inline float Step(float r, int dir) { return Clamp(Clamp(r) + (dir < 0 ? -kStepM : kStepM)); }

enum class Attach : uint8_t { Body, LeftHand, RightHand };
struct Zone {
    Attach attach = Attach::Body;
    float off[3] = {0, 0, 0};   // Body: metres from the pelvis in the body frame (x right, y up, z back); a hand: none (the hand itself)
    float heightM = kDefaultHeightM;   // Body: the body's height the pelvis is found with
    float radius = kDefaultM;
    uint32_t rgba = 0x60F0C8A0u;   // R in the low byte, alpha = opacity: a pale see-through gold
};
constexpr int kMaxZones = 16;

// The hands a gesture's pose uses (BW_LINK_GH_*): [0] left, [1] right.
inline void HandsOf(uint8_t hand, bool out[2]) {
    out[0] = hand == BW_LINK_GH_LEFT || hand == BW_LINK_GH_BOTH;
    out[1] = hand == BW_LINK_GH_RIGHT || hand == BW_LINK_GH_BOTH;
}

// A recorded pose's zones (its hands' spots from the pelvis), `radius` per hand, `heightM` the body's. How many (0: not a pose, no hand recorded).
inline int TargetsOf(const BW_LinkGesture& g, const float radius[2], uint32_t rgba, Zone* out, int max, float heightM = kDefaultHeightM) {
    if (g.type != BW_LINK_GT_POSE) return 0;
    bool use[2];
    HandsOf(g.hand, use);
    int n = 0;
    for (int h = 0; h < 2 && n < max; ++h) {
        const int j = h ? BW_LINK_J_RHAND : BW_LINK_J_LHAND;
        if (!use[h] || !(g.jointValid & (1u << j))) continue;
        Zone z;
        z.attach = Attach::Body;
        z.heightM = heightM > 0.5f && heightM < 2.6f ? heightM : kDefaultHeightM;
        bool ok = true;
        for (int k = 0; k < 3; ++k) {
            z.off[k] = g.joints[j][k];
            ok = ok && std::isfinite(z.off[k]) && std::fabs(z.off[k]) < 2.5f;
        }
        if (!ok) continue;
        z.radius = Clamp(radius[h]);
        z.rgba = rgba;
        out[n++] = z;
    }
    return n;
}

// While a pose is taken: a zone round each hand of the take, following it, as big as the zone will be.
inline int FollowHands(uint8_t hand, float radius, uint32_t rgba, Zone* out, int max) {
    bool use[2];
    HandsOf(hand, use);
    int n = 0;
    for (int h = 0; h < 2 && n < max; ++h) {
        if (!use[h]) continue;
        Zone z;
        z.attach = h ? Attach::RightHand : Attach::LeftHand;
        z.radius = Clamp(radius);
        z.rgba = rgba;
        out[n++] = z;
    }
    return n;
}

// The head as the body frame needs it: its place, its own forward (-z of its turn) and up (+y), room space (y up).
struct Head {
    float pos[3] = {0, 0, 0};
    float fwd[3] = {0, 0, -1};
    float up[3] = {0, 1, 0};
    bool ok = false;
};
// The pelvis and the level heading (yaw: 0 looks down -z, + turns left) from the head, as the engine finds them.
inline bool PelvisOf(const Head& h, float heightM, float pelvis[3], float* yaw) {
    if (!h.ok) return false;
    const float H = heightM > 0.5f && heightM < 2.6f ? heightM : kDefaultHeightM;
    for (int k = 0; k < 3; ++k) pelvis[k] = h.pos[k] - 0.07f * H * h.fwd[k] - 0.04f * H * h.up[k];
    pelvis[1] -= 0.11f * H + 0.42f;
    float fx = h.fwd[0], fz = h.fwd[2];
    if (fx * fx + fz * fz < 1e-6f) {   // looking straight up / down: the heading from where the top of the head points (the other way)
        fx = -h.up[0] * (h.fwd[1] > 0 ? -1.0f : 1.0f);
        fz = -h.up[2] * (h.fwd[1] > 0 ? -1.0f : 1.0f);
    }
    *yaw = std::atan2(-fx, -fz);
    return true;
}
// A zone's centre in the room: body zones from the pelvis and heading the head gives, hand zones at the hand. False when what it hangs on is not there.
inline bool CentreOf(const Zone& z, const Head& head, const float hands[2][3], const bool handOk[2], float out[3]) {
    if (z.attach == Attach::Body) {
        float p[3], yaw = 0;
        if (!PelvisOf(head, z.heightM, p, &yaw)) return false;
        const float c = std::cos(yaw), s = std::sin(yaw);
        // (about +y: x' = x c + z s, z' = -x s + z c)
        out[0] = p[0] + z.off[0] * c + z.off[2] * s;
        out[1] = p[1] + z.off[1];
        out[2] = p[2] - z.off[0] * s + z.off[2] * c;
        return true;
    }
    const int h = z.attach == Attach::RightHand ? 1 : 0;
    if (!handOk[h]) return false;
    for (int k = 0; k < 3; ++k) out[k] = hands[h][k];
    return true;
}

}  // namespace posezones
