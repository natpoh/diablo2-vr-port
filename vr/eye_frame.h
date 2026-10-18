#pragma once
// eye_frame.h - the frame both eyes of native OpenXR's stereo pair are drawn in, for headsets whose displays are canted ([openxr] canted_eyes).
// Pure: no OpenXR, no Windows (tools/eye_frame_test.cpp).
//
// The runtime gives each eye its own view: a position, a turn and a fov. On most headsets the two turns are the same (parallel displays). On
// some (Pimax, Varjo, Index and others) each eye's view is turned outward by a few degrees. vrcam builds the game's camera from ONE turn - the
// left eye's, for both eyes - and draws the right eye as that camera moved sideways, with the right eye's fov; the layer to the runtime used to
// carry each eye's OWN turn. On a canted headset the right eye was then submitted as turned (say) 10 degrees one way when it was drawn turned 10
// the other: the runtime reprojected it 20 degrees wrong - the eyes read as crossed and the centre as stretched, until a Steam menu over the game
// (SteamVR then shows the layer another way) hid it.
//
// Now, when the two turns differ by more than a quarter of a degree: both eyes are drawn AND submitted in one frame - the mean of the two turns,
// the head's - and each eye's fov is the frustum of its true view as seen from that frame (the bounding frustum of the four corners of its own
// view, turned into the common frame). The runtime reprojects each eye from the frame it was drawn in to the display it is shown on, which is
// what a submitted pose is for. Parallel headsets (the turns the same): nothing changes, bit for bit.

#include <algorithm>
#include <cmath>

namespace eyeframe {

struct Quat { float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f; };
struct Fov { float l = 0.0f, r = 0.0f, u = 0.0f, d = 0.0f; };   // OpenXR's: angles in radians, left and down negative

constexpr float kPi = 3.14159265f;
constexpr float kCantMinRad = 0.25f * kPi / 180.0f;   // two turns closer than this are parallel eyes
constexpr float kMaxTan = 5.671f;                      // tan 80 deg: a bounding frustum is kept narrower than that (a planar frustum cannot reach the horizon)
constexpr float kMinDepth = 0.1f;

inline Quat Mul(const Quat& a, const Quat& b) {   // b first, then a
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
inline Quat Conj(const Quat& q) { return {-q.x, -q.y, -q.z, q.w}; }
inline Quat Normalized(const Quat& q) {
    const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return n > 1e-6f ? Quat{q.x / n, q.y / n, q.z / n, q.w / n} : Quat{};
}
inline void Rotate(const Quat& q, const float v[3], float out[3]) {
    const float cx = q.y * v[2] - q.z * v[1] + q.w * v[0], cy = q.z * v[0] - q.x * v[2] + q.w * v[1], cz = q.x * v[1] - q.y * v[0] + q.w * v[2];
    out[0] = v[0] + 2.0f * (q.y * cz - q.z * cy);
    out[1] = v[1] + 2.0f * (q.z * cx - q.x * cz);
    out[2] = v[2] + 2.0f * (q.x * cy - q.y * cx);
}

// The angle between two turns, radians (0 .. pi).
inline float AngleBetween(const Quat& a, const Quat& b) {
    const float dot = std::min(1.0f, std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w));
    return 2.0f * std::acos(dot);
}
// The turn halfway between two (the head's, for two eyes turned out by the same angle each way).
inline Quat Mean(const Quat& a, const Quat& b) {
    const float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    const float s = dot < 0.0f ? -1.0f : 1.0f;
    const Quat m{a.x + s * b.x, a.y + s * b.y, a.z + s * b.z, a.w + s * b.w};
    const float n2 = m.x * m.x + m.y * m.y + m.z * m.z + m.w * m.w;
    return n2 > 1e-8f ? Normalized(m) : a;
}

// An eye's fov (its own view, turned as `qEye`) as the frustum of the same view seen from the frame `qRef`: the bounding box, in tangents on the
// plane one metre ahead, of the view's four corner rays turned into that frame. Equal turns: the same fov.
inline Fov Reframe(const Fov& f, const Quat& qEye, const Quat& qRef) {
    const Quat R = Mul(Conj(qRef), qEye);
    const float tx[2] = {std::tan(f.l), std::tan(f.r)}, ty[2] = {std::tan(f.d), std::tan(f.u)};
    float lo[2] = {kMaxTan, kMaxTan}, hi[2] = {-kMaxTan, -kMaxTan};
    bool first = true;
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j) {
            const float v[3] = {tx[i], ty[j], -1.0f};
            float w[3];
            Rotate(R, v, w);
            const float depth = std::max(-w[2], kMinDepth);
            const float px = std::clamp(w[0] / depth, -kMaxTan, kMaxTan), py = std::clamp(w[1] / depth, -kMaxTan, kMaxTan);
            if (first) { lo[0] = hi[0] = px; lo[1] = hi[1] = py; first = false; }
            lo[0] = std::min(lo[0], px); hi[0] = std::max(hi[0], px);
            lo[1] = std::min(lo[1], py); hi[1] = std::max(hi[1], py);
        }
    return {std::atan(lo[0]), std::atan(hi[0]), std::atan(hi[1]), std::atan(lo[1])};
}

struct Frame {
    bool canted = false;   // the two turns differ: both eyes are drawn in `ref`
    Quat ref;              // the frame both eyes are drawn and submitted in (the first eye's own turn when not canted - what the camera always used)
    Fov fov[2];            // what each eye is drawn and submitted with
    float cantRad = 0.0f;  // the angle between the eyes' own turns
};

// `q`: each eye's own turn from the runtime, `want`: each eye's fov as it should show (cropped as the player set it), `allowed`: [openxr] canted_eyes.
inline Frame Make(const Quat q[2], const Fov want[2], bool allowed) {
    Frame fr;
    fr.ref = q[0];
    fr.fov[0] = want[0];
    fr.fov[1] = want[1];
    fr.cantRad = AngleBetween(q[0], q[1]);
    if (!allowed || !(fr.cantRad >= kCantMinRad)) return fr;
    fr.canted = true;
    fr.ref = Mean(q[0], q[1]);
    for (int e = 0; e < 2; ++e) fr.fov[e] = Reframe(want[e], q[e], fr.ref);
    return fr;
}

// An eye's own turn seen from the head's frame, for the log: yaw (+ turned left), pitch (+ up), roll (+ the eye's right side up), degrees.
inline void Relative(const Quat& ref, const Quat& eye, float* yawDeg, float* pitchDeg, float* rollDeg) {
    const Quat r = Mul(Conj(ref), eye);
    const float fwd[3] = {0.0f, 0.0f, -1.0f}, side[3] = {1.0f, 0.0f, 0.0f};
    float f[3], s[3];
    Rotate(r, fwd, f);
    Rotate(r, side, s);
    const float k = 180.0f / kPi;
    *yawDeg = std::atan2(-f[0], -f[2]) * k;
    *pitchDeg = std::asin(std::clamp(f[1], -1.0f, 1.0f)) * k;
    *rollDeg = std::atan2(s[1], std::sqrt(s[0] * s[0] + s[2] * s[2])) * k;
}

}  // namespace eyeframe
