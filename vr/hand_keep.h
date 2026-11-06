#pragma once
// hand_keep.h - the hands when the runtime gives no controllers (docs/notes/no_controllers.md). Pure: no OpenXR, no Windows
// (cleanroom/tests/hand_keep_test.cpp).
//
// Serg, 2026-11-06 (0.205.5, Quest 3 over Virtual Desktop): Quest switched to hand tracking or VD stopped sending the controllers -
// "controllers: left none, right none", the grips ----, actions inactive - and the wrist medallions, the map in the hand with the
// allies' portraits under it and the laser vanished while the game played on: every one of them hangs off the hand's pose
// (D2RVR_XrInput hand[h].grip.valid == 1, the shared block's handsValid), and with no pose there is no hand. "надо их отвязать от
// контроллеров".
//
// Three pieces, each behind its own key (xr.cpp Publish):
//   Keeper      [openxr] hands_kept: a hand whose controller is gone (no pose after [openxr] hand_hold_ms, or a bare hand that
//               places nothing) keeps a place - its last place by the head, in the frame of the head's turn, so it comes round
//               with the player; never seen this session: a rest place below and in front of the head. It FOLLOWS that place at
//               most kMaxSpeedMS and kMaxTurnDegS: a kept hand never moves fast, so no strike by touch, no throw, no swing can
//               come of it (the slowest blade speed that hits, [gameplay] melee_min_speed, is 0.8 m/s by default) - it is a
//               controller held still, which the game mechanics already meet every frame. It presses nothing.
//   GripFromPalm / AimFromPalm   [openxr] bare_hand_poses: a bare hand (XR_EXT_hand_tracking, no controller in it) places the
//               arm where the runtime gives no grip pose for it - the palm joint turned into a grip pose by the two conventions
//               (the joints: -Z to the fingertips, +Y out of the back of the hand, X = Y x Z; the grip: +X out of the palm
//               (left) / into it (right), -Z through the curled fingers from the little finger to the thumb).
//   PinchGate   ... and a bare hand's trigger (the runtime's pinch) reaches only the plugin's own panels (the headset settings,
//               the skill panel) while one of them is up - never the game; a pinch still held when a panel closes (or held
//               before it opened) is nothing until let go.
// KeyVk: [openxr] panels_key - a keyboard key that asks for the skill panel / the headset settings as the right stick's long press
// does (the game window in front).

#include <algorithm>
#include <cmath>

namespace handkeep {

constexpr float kMaxSpeedMS = 0.5f;    // a kept hand moves at most this fast (m/s): under every blade speed that hits
constexpr float kMaxTurnDegS = 90.0f;  // and turns at most this fast
constexpr float kPinchOn = 0.8f;       // the pinch (the runtime's trigger value for a bare hand) counts from here
constexpr float kPinchOff = 0.3f;      // and is let go under this

struct Pose {
    float p[3] = {0.0f, 0.0f, 0.0f};
    float q[4] = {0.0f, 0.0f, 0.0f, 1.0f};   // x y z w
};

namespace math {
struct V3 { float x, y, z; };
inline V3 Rot(const float q[4], V3 v) {   // q v q*
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float tx = 2.0f * (y * v.z - z * v.y), ty = 2.0f * (z * v.x - x * v.z), tz = 2.0f * (x * v.y - y * v.x);
    return {v.x + w * tx + (y * tz - z * ty), v.y + w * ty + (z * tx - x * tz), v.z + w * tz + (x * ty - y * tx)};
}
inline void Mul(const float a[4], const float b[4], float o[4]) {   // a then b in a's frame (a * b)
    const float x = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    const float y = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    const float z = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    const float w = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
    o[0] = x; o[1] = y; o[2] = z; o[3] = w;
}
inline bool Normalize(float q[4]) {
    const float n2 = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
    if (!std::isfinite(n2) || n2 < 1e-8f) return false;
    const float k = 1.0f / std::sqrt(n2);
    for (int i = 0; i < 4; ++i) q[i] *= k;
    return true;
}
// The head's turn about the vertical (OpenXR: y up, -z ahead), + to the left
inline float YawOf(const float q[4]) {
    const V3 f = Rot(q, {0.0f, 0.0f, -1.0f});
    return std::atan2(-f.x, -f.z);
}
inline void YawQ(float yaw, float o[4]) { o[0] = 0.0f; o[1] = std::sin(0.5f * yaw); o[2] = 0.0f; o[3] = std::cos(0.5f * yaw); }
// The angle between two unit turns, radians (0 .. pi)
inline float Angle(const float a[4], const float b[4]) {
    const float d = std::fabs(a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]);
    return 2.0f * std::acos(std::min(1.0f, d));
}
// From a toward b by t (0..1), the short way
inline void Slerp(const float a[4], const float b[4], float t, float o[4]) {
    float bb[4] = {b[0], b[1], b[2], b[3]};
    float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    if (d < 0.0f) { d = -d; for (float& c : bb) c = -c; }
    if (d > 0.9995f) {
        for (int i = 0; i < 4; ++i) o[i] = a[i] + t * (bb[i] - a[i]);
    } else {
        const float th = std::acos(d), s = std::sin(th);
        const float ka = std::sin((1.0f - t) * th) / s, kb = std::sin(t * th) / s;
        for (int i = 0; i < 4; ++i) o[i] = ka * a[i] + kb * bb[i];
    }
    Normalize(o);
}
// A rotation matrix's columns (unit, right-handed) as a quaternion
inline void FromColumns(V3 cx, V3 cy, V3 cz, float o[4]) {
    const float m00 = cx.x, m10 = cx.y, m20 = cx.z, m01 = cy.x, m11 = cy.y, m21 = cy.z, m02 = cz.x, m12 = cz.y, m22 = cz.z;
    const float tr = m00 + m11 + m22;
    if (tr > 0.0f) {
        const float s = std::sqrt(tr + 1.0f) * 2.0f;
        o[3] = 0.25f * s; o[0] = (m21 - m12) / s; o[1] = (m02 - m20) / s; o[2] = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        o[3] = (m21 - m12) / s; o[0] = 0.25f * s; o[1] = (m01 + m10) / s; o[2] = (m02 + m20) / s;
    } else if (m11 > m22) {
        const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        o[3] = (m02 - m20) / s; o[0] = (m01 + m10) / s; o[1] = 0.25f * s; o[2] = (m12 + m21) / s;
    } else {
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        o[3] = (m10 - m01) / s; o[0] = (m02 + m20) / s; o[1] = (m12 + m21) / s; o[2] = 0.25f * s;
    }
    Normalize(o);
}
}  // namespace math

// Where a hand rests when it was never seen this session, in the frame of the head's turn (x right, y up, z back), side 0 left,
// 1 right: 20 cm out to its side, 45 cm under the eyes, 28 cm ahead, the controller pointing ahead and 30 degrees down.
inline Pose RestRel(int side) {
    Pose r;
    r.p[0] = side ? 0.20f : -0.20f;
    r.p[1] = -0.45f;
    r.p[2] = -0.28f;
    const float a = -30.0f * 3.14159265f / 180.0f;   // about +x: a negative turn tips -z down
    r.q[0] = std::sin(0.5f * a); r.q[1] = 0.0f; r.q[2] = 0.0f; r.q[3] = std::cos(0.5f * a);
    return r;
}

// One hand's kept place. Live() every frame its controller (or a bare hand that places it) is there; Keep() every frame it is not.
class Keeper {
public:
    // The hand as the runtime gives it now (grip and aim in the room), the head too: what it is to the head is learned.
    void Live(const Pose& grip, const Pose& aim, const Pose& head) {
        float yq[4];
        math::YawQ(-math::YawOf(head.q), yq);
        const math::V3 d = math::Rot(yq, {grip.p[0] - head.p[0], grip.p[1] - head.p[1], grip.p[2] - head.p[2]});
        rel_.p[0] = d.x; rel_.p[1] = d.y; rel_.p[2] = d.z;
        math::Mul(yq, grip.q, rel_.q);
        math::Normalize(rel_.q);
        // the aim in the grip's frame (the pointer of the same controller)
        float inv[4] = {-grip.q[0], -grip.q[1], -grip.q[2], grip.q[3]};
        const math::V3 a = math::Rot(inv, {aim.p[0] - grip.p[0], aim.p[1] - grip.p[1], aim.p[2] - grip.p[2]});
        aimInGrip_.p[0] = a.x; aimInGrip_.p[1] = a.y; aimInGrip_.p[2] = a.z;
        math::Mul(inv, aim.q, aimInGrip_.q);
        math::Normalize(aimInGrip_.q);
        seen_ = true;
        cur_ = grip;
        haveCur_ = true;
        kept_ = false;
    }

    // No controller this frame: the kept grip and aim, followed toward the place by the head at most kMaxSpeedMS / kMaxTurnDegS over
    // `dtS`. False: nothing to keep it by (no head yet, and never a place).
    bool Keep(const Pose& head, bool headOk, float dtS, int side, Pose* grip, Pose* aim) {
        if (headOk) {
            const Pose rel = seen_ ? rel_ : RestRel(side);
            float yq[4];
            math::YawQ(math::YawOf(head.q), yq);
            const math::V3 d = math::Rot(yq, {rel.p[0], rel.p[1], rel.p[2]});
            Pose target;
            target.p[0] = head.p[0] + d.x; target.p[1] = head.p[1] + d.y; target.p[2] = head.p[2] + d.z;
            math::Mul(yq, rel.q, target.q);
            math::Normalize(target.q);
            if (!haveCur_) {
                cur_ = target;   // the first place: put there, nothing to come from
                haveCur_ = true;
            } else {
                const float dt = std::clamp(std::isfinite(dtS) ? dtS : 0.0f, 0.0f, 0.1f);
                const float step = kMaxSpeedMS * dt;
                const float dx = target.p[0] - cur_.p[0], dy = target.p[1] - cur_.p[1], dz = target.p[2] - cur_.p[2];
                const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                const float k = dist > step && dist > 0.0f ? step / dist : 1.0f;
                cur_.p[0] += dx * k; cur_.p[1] += dy * k; cur_.p[2] += dz * k;
                const float ang = math::Angle(cur_.q, target.q), turn = kMaxTurnDegS * 3.14159265f / 180.0f * dt;
                float q[4];
                math::Slerp(cur_.q, target.q, ang > turn && ang > 0.0f ? turn / ang : 1.0f, q);
                for (int i = 0; i < 4; ++i) cur_.q[i] = q[i];
            }
        }
        if (!haveCur_) return false;   // (no head ever: nothing)
        *grip = cur_;
        const math::V3 a = math::Rot(cur_.q, {aimInGrip_.p[0], aimInGrip_.p[1], aimInGrip_.p[2]});
        aim->p[0] = cur_.p[0] + a.x; aim->p[1] = cur_.p[1] + a.y; aim->p[2] = cur_.p[2] + a.z;
        math::Mul(cur_.q, aimInGrip_.q, aim->q);
        math::Normalize(aim->q);
        kept_ = true;
        return true;
    }

    // The session ended (or the key went off): nothing known
    void Reset() { *this = Keeper{}; }

    bool Kept() const { return kept_; }
    bool Seen() const { return seen_; }

private:
    Pose rel_;          // the grip in the frame of the head's turn, at its last live frame
    Pose aimInGrip_;    // the aim in the grip's frame (none known: the grip's own)
    Pose cur_;          // where the hand is now (the room)
    bool seen_ = false, haveCur_ = false, kept_ = false;
};

// [openxr] bare_hand_poses: the palm joint (XR_HAND_JOINT_PALM_EXT) of a bare hand as a grip pose, side 0 left, 1 right. The grip's
// axes in the palm's frame - right: +X = +Y (into the palm from below a hand held palm down), -Z = -X (little finger to thumb),
// +Y = +Z; left: +X = -Y (out of the palm), -Z = +X, +Y = +Z.
inline Pose GripFromPalm(const Pose& palm, int side) {
    float m[4];
    if (side) math::FromColumns({0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, m);
    else math::FromColumns({0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}, m);
    Pose g;
    for (int i = 0; i < 3; ++i) g.p[i] = palm.p[i];
    math::Mul(palm.q, m, g.q);
    math::Normalize(g.q);
    return g;
}
// ... and as the aim: the palm joint already points as an aim pose does (-Z ahead along the fingers, +Y up out of the back of the hand)
inline Pose AimFromPalm(const Pose& palm) { return palm; }

// A bare hand's pinch for the plugin's own panels only: passed while a panel is up, never the game's. One that was down before the panel
// came up, or is still down when it goes, is nothing until let go.
class PinchGate {
public:
    float Step(bool panelUp, float pinch) {
        const bool down = held_ ? pinch > kPinchOff : pinch >= kPinchOn;
        if (!down) { held_ = false; blocked_ = false; return 0.0f; }
        if (!held_) { held_ = true; blocked_ = !panelUp; }   // a fresh pinch: the panel's if one is up
        if (!panelUp) blocked_ = true;                        // the panel went with the pinch down: nothing until it is let go
        return blocked_ ? 0.0f : std::clamp(pinch, 0.0f, 1.0f);
    }
    void Reset() { held_ = blocked_ = false; }

private:
    bool held_ = false, blocked_ = false;
};

// [openxr] panels_key: 0 none, 1 Insert, 2 Home, 3 End (Windows virtual-key codes)
inline int KeyVk(int choice) { return choice == 1 ? 0x2D : choice == 2 ? 0x24 : choice == 3 ? 0x23 : 0; }
inline const char* KeyName(int choice) { return choice == 1 ? "Insert" : choice == 2 ? "Home" : choice == 3 ? "End" : "none"; }

}  // namespace handkeep
