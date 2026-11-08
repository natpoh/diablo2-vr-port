// trunk_hold.h - which pose each bone above the pelvis takes when the upper body is held: VR F4's [body] lock (everything above the pelvis
// in the bind pose, the hands' contents the animation's) and flat F3's [body] flat_hold_trunk (only the trunk's axis - spine_01 up to the head -
// in the bind pose, every other upper bone the animation's turn from its parent on that held trunk). Pure; the test is
// cleanroom/tests/trunk_hold_test.cpp, the game side vr/skeletons.cpp PoseBody.
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace trunkhold {

inline constexpr float kTwistMaxRad = 2.0943951f;   // 120 deg: a chest turned further is read as turned this far (a flipped bone, not a swing)

// [body] flat_hold_trunk_twist: how far the animation turns the chest about the model's vertical from its bind pose (radians, capped at maxRad either way)
// - bind and anim are the chest's model-space matrices (row vectors, 16 floats; a scale in them does not count). The held chest drops that turn, and the
// arms hung on it with the animation's own turn from it lost it too: the barbarian's side swing (his attack 2, the chest turned far round) went out to
// the side, past the camera (Serg 2026-11-08: "когда сбоку бьёт руки не видно"). PoseBody turns the arms back by this about the chest's vertical.
inline float TwistYaw(const float bind[16], const float anim[16], float maxRad) {
    // the turn M taking bind to anim in model space (W_anim = W_bind * M): model z goes to row 2 of M = (row 2 of bind's inverse) * anim; for a
    // rotation (times a scale) bind's inverse's row 2 is its column 2 over the scale squared - the scale does not change the angle
    const float iz[3] = {bind[2], bind[6], bind[10]};
    const float x = iz[0] * anim[0] + iz[1] * anim[4] + iz[2] * anim[8];
    const float z = iz[0] * anim[2] + iz[1] * anim[6] + iz[2] * anim[10];
    if (!std::isfinite(x + z) || (std::fabs(x) < 1e-8f && std::fabs(z) < 1e-8f)) return 0.0f;
    const float cap = std::isfinite(maxRad) ? std::max(0.0f, maxRad) : 0.0f;
    return std::clamp(std::atan2(x, z), -cap, cap);
}

// A turn by yaw about the vertical through (px, pz), as a row-vector 4x4 (p' = p * T) - the body's own turn's form (vr/skeletons.cpp PoseBody).
inline void YawAbout(float yaw, float px, float pz, float out[16]) {
    const float c = std::cos(yaw), s = std::sin(yaw);
    for (int i = 0; i < 16; ++i) out[i] = 0.0f;
    out[0] = c; out[2] = -s; out[5] = 1.0f; out[8] = s; out[10] = c; out[15] = 1.0f;
    out[12] = px - (px * c + pz * s);
    out[14] = pz - (-px * s + pz * c);
}

// The trunk's axis: the head, its parents down to spine_01 (both included). Empty when either is missing or the head is not above spine_01.
inline std::vector<char> Axis(int n, const int* parent, int head, int spine) {
    std::vector<char> axis(n > 0 ? n : 0, 0);
    if (n <= 0 || head < 0 || head >= n || spine < 0 || spine >= n) return {};
    for (int b = head, steps = 0; b >= 0 && b < n && steps <= n; b = parent[b], ++steps) {
        axis[b] = 1;
        if (b == spine) return axis;
    }
    return {};   // spine_01 is not under the head: nothing held
}

enum class Pose { Game, Root, Bind, Anim };   // the game's own; spine_01 at its bind place; bind turn from its parent; the animation's turn from its parent

// One bone. upper: spine_01 or above it; root: spine_01 itself (or a bone with no parent); inHand: what a hand holds (and the fingers).
// lock: VR F4's whole upper body held; hold: flat F3's trunk only (with the axis above).
inline Pose For(bool upper, bool root, bool inHand, bool lock, bool hold, bool onAxis) {
    if (!upper || !(lock || hold)) return Pose::Game;
    if (root) return Pose::Root;
    if (inHand) return Pose::Anim;
    if (lock) return Pose::Bind;
    return onAxis ? Pose::Bind : Pose::Anim;
}

}  // namespace trunkhold
