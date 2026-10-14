#pragma once
// What any panel hung in the headset needs, kept apart from the settings panel so another one (the skill wheel) can use it:
// small vector / quaternion maths, a ray against a panel's rectangle, the pad values a panel takes from the game, and the
// release gate that keeps what was held when a panel closed away from the game until it is let go. Pure: no Windows, no OpenXR.

#include <cmath>
#include <cstdint>

namespace hset {

struct V3 { float x = 0, y = 0, z = 0; };
struct Q { float x = 0, y = 0, z = 0, w = 1; };
struct Pose { V3 p; Q q; bool valid = false; };

inline V3 Rot(const Q& q, V3 v) {
    const V3 u{q.y * v.z - q.z * v.y + q.w * v.x, q.z * v.x - q.x * v.z + q.w * v.y, q.x * v.y - q.y * v.x + q.w * v.z};
    return {v.x + 2.0f * (q.y * u.z - q.z * u.y), v.y + 2.0f * (q.z * u.x - q.x * u.z), v.z + 2.0f * (q.x * u.y - q.y * u.x)};
}
inline Q YawQ(float a) { return {0.0f, std::sin(0.5f * a), 0.0f, std::cos(0.5f * a)}; }   // about +y, + turns left
inline float YawOf(const Q& q) {
    const V3 f = Rot(q, {0.0f, 0.0f, -1.0f});
    return std::atan2(-f.x, -f.z);
}
inline V3 Add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 Sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 Mul(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
inline float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 Unit(V3 a) {
    const float n = std::sqrt(Dot(a, a));
    return n > 1e-9f ? Mul(a, 1.0f / n) : V3{0, 0, -1};
}

// A ray against the panel's rectangle (its front is +z of `panel`, u, v from the top left).
struct PanelHit { bool hit = false; float u = 0, v = 0, dist = 0; V3 point; };
inline PanelHit IntersectPanel(V3 from, V3 dir, const Pose& panel, float w, float h) {
    PanelHit r;
    if (!panel.valid) return r;
    const V3 n = Rot(panel.q, {0.0f, 0.0f, 1.0f});
    const float dn = Dot(dir, n);
    if (!(dn < -1e-4f)) return r;   // parallel, or pointing away from its front
    const float t = Dot(Sub(panel.p, from), n) / dn;
    if (!(t > 0.0f)) return r;
    const V3 p = Add(from, Mul(dir, t));
    const Q back{-panel.q.x, -panel.q.y, -panel.q.z, panel.q.w};
    const V3 local = Rot(back, Sub(p, panel.p));
    r.u = local.x / w + 0.5f;
    r.v = 0.5f - local.y / h;
    r.hit = r.u >= 0.0f && r.u < 1.0f && r.v >= 0.0f && r.v < 1.0f;
    r.dist = t;
    r.point = p;
    return r;
}

// Buttons as the session reports them (the same bits as Game Link's BW_LINK_XRB_*; xr_headset_settings.h checks).
enum Btn : uint32_t { kPrimary = 1u << 0, kSecondary = 1u << 1, kStick = 1u << 2, kMenu = 1u << 3, kTrigger = 1u << 4, kSqueeze = 1u << 5 };
struct Pad {
    float trigger = 0, squeeze = 0, stickX = 0, stickY = 0;
    uint32_t buttons = 0;
};

constexpr float kPressAt = 0.55f, kReleaseAt = 0.20f;   // a trigger / grip is down past the first, up below the second
constexpr double kGripHoldMs = 350.0;                  // both grips squeezed this long: then a thumbstick click opens / closes the panel
constexpr double kRepeatFirstMs = 450.0, kRepeatMs = 110.0;
constexpr float kBeamM = 1.5f;                         // the beam's length where it hits nothing

// Controls that were held when the panel closed (or when the chord took them) stay at zero until let go.
class ReleaseGate {
    uint32_t buttons_[2] = {0, 0};
    bool trigger_[2] = {false, false}, squeeze_[2] = {false, false}, stick_[2] = {false, false};
public:
    void Arm(const Pad pad[2]) {
        for (int h = 0; h < 2; ++h) {
            buttons_[h] = pad[h].buttons;
            trigger_[h] = pad[h].trigger > kReleaseAt;
            squeeze_[h] = pad[h].squeeze > kReleaseAt;
            stick_[h] = std::fabs(pad[h].stickX) > 0.3f || std::fabs(pad[h].stickY) > 0.3f;
        }
    }
    void Hold(int hand, uint32_t bits) { buttons_[hand] |= bits; }
    void Clear() { *this = ReleaseGate{}; }
    bool Active() const {
        for (int h = 0; h < 2; ++h) if (buttons_[h] || trigger_[h] || squeeze_[h] || stick_[h]) return true;
        return false;
    }
    void Apply(Pad pad[2]) {
        for (int h = 0; h < 2; ++h) {
            Pad& p = pad[h];
            buttons_[h] &= p.buttons;
            p.buttons &= ~buttons_[h];
            if (trigger_[h]) { if (p.trigger > kReleaseAt) p.trigger = 0.0f; else trigger_[h] = false; }
            if (squeeze_[h]) { if (p.squeeze > kReleaseAt) p.squeeze = 0.0f; else squeeze_[h] = false; }
            if (stick_[h]) {
                if (std::fabs(p.stickX) > 0.2f || std::fabs(p.stickY) > 0.2f) p.stickX = p.stickY = 0.0f;
                else stick_[h] = false;
            }
        }
    }
};

}  // namespace hset
