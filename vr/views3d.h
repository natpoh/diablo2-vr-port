// views3d.h - native OpenXR, F1 (from above) and F3 (the game on your floor) in real 3D: the pure maths.
//
// Native OpenXR drew only first person (F4) and third person (F2) from the headset's own eyes; F1 and F3 went to the
// headset as a flat picture of the window. [openxr] views_3d draws them with the runtime's eyes too (docs/plan_native_f1_f3.md):
//
//   F3, the room (kViewsRoom): the diorama stands in the room - the hero on a floor point, the world scaled to the
//     diorama - and each eye is the real eye, at the room's scale. The runtime shows the picture with the eyes' own poses:
//     RoomToWorld / TableEye here are the one similarity (turn about the vertical, scale, move) that makes the
//     picture the diorama in the room seen from where the head is.
//   F1, the window (kViewsWindow): the game's view from above in true perspective, through a window of the room (the flat
//     picture's place and width) with the hero on the window's middle. A window sees the world with off-axis frusta:
//     what lies on the window plane is the same in both eyes (zero parallax), the rest has depth, and a head moved
//     sideways sees round things. An eye's picture is the window's own texture, so it is hung on the window as a quad per eye
//     (eyeVisibility LEFT / RIGHT) and the compositor shows it with the head as it is, as it does the flat picture.
//
// Pure: no OpenXR, no game. vr/xr.cpp (the eyes, the layer) and vr/vrcam.cpp (the camera) call it; cleanroom/tests/views3d_test.cpp
// checks it. Conventions as OpenXR's and xr.cpp's: x right, y up, -z ahead; a yaw is about +y and turns LEFT when positive;
// the game's axes (vrcam's Facing): a heading h faces (-sin h, 0, -cos h).
#pragma once

#include <algorithm>
#include <cmath>

namespace views3d {

constexpr float kPi = 3.14159265358979f;

struct Vec { float x, y, z; };
struct Quat { float x, y, z, w; };

inline Vec operator+(Vec a, Vec b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec operator-(Vec a, Vec b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec operator*(Vec a, float k) { return {a.x * k, a.y * k, a.z * k}; }
inline float Dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float Length(Vec a) { return std::sqrt(Dot(a, a)); }

inline Vec Rotate(const Quat& q, Vec v) {
    const Vec u{q.y * v.z - q.z * v.y + q.w * v.x, q.z * v.x - q.x * v.z + q.w * v.y, q.x * v.y - q.y * v.x + q.w * v.z};
    return {v.x + 2.0f * (q.y * u.z - q.z * u.y), v.y + 2.0f * (q.z * u.x - q.x * u.z), v.z + 2.0f * (q.x * u.y - q.y * u.x)};
}
inline Quat YawQuat(float a) { return {0.0f, std::sin(0.5f * a), 0.0f, std::cos(0.5f * a)}; }
inline float YawOf(const Quat& q) {
    const Vec f = Rotate(q, {0.0f, 0.0f, -1.0f});
    return std::atan2(-f.x, -f.z);
}
inline float WrapPi(float a) { return std::atan2(std::sin(a), std::cos(a)); }

// -- F3: the room to the game ---------------------------------------------------------------------------------------
// A room vector (x right, y up, z back, metres) turned about the vertical by phi into the game's axes. (vrcam's TableCamera
// and the F4 native branch say the same: world = {x cos + z sin, y, -x sin + z cos}.)
inline Vec RoomToWorldDir(float phi, Vec v) {
    const float c = std::cos(phi), s = std::sin(phi);
    return {v.x * c + v.z * s, v.y, -v.x * s + v.z * c};
}

// The diorama's place in the room. fx, fz: the floor point under the hero; floorY: the floor's height; phi: the room-to-game
// turn (the game's heading of the room's -z); scale: world units per metre; lift: [table] height_m, the diorama's height over
// the floor; minAbove: the lowest an eye goes over the diorama's ground, metres (the head cannot go through the board).
struct Table { float fx, fz, floorY, phi, scale, lift, minAbove; };

// The game's world position of an eye at room position p: the hero's point `hero` plus the head's offset from the hero's floor
// point, turned and scaled; the height over the diorama's ground as it is, scaled.
inline Vec TableEye(const Table& t, Vec hero, Vec p) {
    const Vec d = RoomToWorldDir(t.phi, {p.x - t.fx, 0.0f, p.z - t.fz}) * t.scale;
    return {hero.x + d.x, hero.y + std::max(p.y - t.floorY - t.lift, t.minAbove) * t.scale, hero.z + d.z};
}

// -- F1: the window --------------------------------------------------------------------------------------------------
// The window of the room the game is seen through: metres, width by height at `dist` ahead of the place it was put.
struct Window { float width, height, dist; };

inline Window MakeWindow(float widthM, float distM, float aspect) {
    const float a = std::isfinite(aspect) && aspect > 0.1f && aspect < 10.0f ? aspect : 16.0f / 9.0f;
    return {widthM, widthM / a, distM};
}

// Where the window stands: held to the gaze at first, and brought round in front of it again after more than 60 degrees
// off the gaze for a second - as the flat picture stands (xr.cpp OpenFrame), here for a pair's frames.
class Anchor {
 public:
    static constexpr int kHoldFrames = 45;       // the runtime's first poses may not be the head's yet
    static constexpr float kOffRad = 1.05f;      // 60 degrees
    static constexpr double kOffUs = 1.0e6;      // for a second

    void Reset() { anchored_ = false; frames_ = 0; offSince_ = 0.0; }
    bool Anchored() const { return anchored_; }
    float Yaw() const { return yaw_; }
    Vec Pos() const { return pos_; }

    // Once a frame: the gaze's yaw, the head's place (the eyes' middle) and the time (microseconds). True the frame it was
    // put for the first time or brought round again (for the log); the first half second it follows the gaze quietly.
    bool Step(float gazeYaw, Vec head, double nowUs) {
        const bool was = anchored_;
        frames_ = was ? frames_ + 1 : 0;
        const bool holding = !was || frames_ < kHoldFrames;
        bool brought = false;
        if (!holding && std::fabs(WrapPi(gazeYaw - yaw_)) > kOffRad) {
            if (offSince_ <= 0.0) offSince_ = nowUs;
            else if (nowUs - offSince_ > kOffUs) brought = true;
        } else {
            offSince_ = 0.0;
        }
        if (holding || brought) {
            anchored_ = true;
            offSince_ = 0.0;
            yaw_ = gazeYaw;
            pos_ = head;
        }
        return !was || brought;
    }

 private:
    bool anchored_ = false;
    int frames_ = 0;
    double offSince_ = 0.0;
    float yaw_ = 0.0f;
    Vec pos_{0.0f, 0.0f, 0.0f};
};

// The window in the room: its middle (level with the head it was put from) and its turn (about the vertical only).
struct Placed { Vec centre; float yaw; };
inline Placed Place(const Window& w, float yaw, Vec head) {
    return {head + Rotate(YawQuat(yaw), {0.0f, 0.0f, -w.dist}), yaw};
}

// A room point in the window's frame (x right along the window, y up, z out of the window towards the viewer).
inline Vec ToWindow(const Placed& p, Vec room) { return Rotate(YawQuat(-p.yaw), room - p.centre); }
inline Vec FromWindow(const Placed& p, Vec win) { return p.centre + Rotate(YawQuat(p.yaw), win); }

// What a window makes of the two eyes: the middle between them in the window's frame, the half gap between them ALONG the
// window (the replayed pair shifts the right eye along x only), and each eye's frustum through the window's four edges
// (tangents, left and down negative). The eyes are put level with each other, at the middle's depth, so the two frusta are exactly
// the ones of the cameras that draw. `room[e]` is where each of those eyes stands in the room (the window's turn about it).
struct WindowEyes {
    Vec mid;
    float half;
    float tanL[2], tanR[2], tanU[2], tanD[2];
    Vec room[2];
};

inline WindowEyes ThroughWindow(const Window& w, const Placed& at, Vec eyeLeft, Vec eyeRight, float minZ) {
    const Vec l = ToWindow(at, eyeLeft), r = ToWindow(at, eyeRight);
    WindowEyes o{};
    o.mid = {0.5f * (l.x + r.x), 0.5f * (l.y + r.y), std::max(0.5f * (l.z + r.z), minZ)};
    o.half = std::fabs(0.5f * (r.x - l.x));
    for (int e = 0; e < 2; ++e) {
        const Vec p{o.mid.x + (e == 0 ? -o.half : o.half), o.mid.y, o.mid.z};
        o.tanL[e] = (-0.5f * w.width - p.x) / p.z;
        o.tanR[e] = (0.5f * w.width - p.x) / p.z;
        o.tanD[e] = (-0.5f * w.height - p.y) / p.z;
        o.tanU[e] = (0.5f * w.height - p.y) / p.z;
        o.room[e] = FromWindow(at, p);
    }
    return o;
}

// The game's world position of the middle between the eyes, for the overhead camera: the hero stands on the window's middle,
// the game's axes (right, up, forward of the overhead camera) are the window's x, y and -z, `upm` world units per metre.
inline Vec WindowWorldEye(Vec hero, Vec right, Vec up, Vec fwd, float upm, Vec winMid) {
    return hero + (right * winMid.x + up * winMid.y - fwd * winMid.z) * upm;
}

// How far back the overhead camera stands so that the game's view height fills the window: `half` (world units) is the
// game's half height of view at the hero. Returns the distance (world units) and the world units per metre (dist over the
// window's distance).
inline float OverheadDistance(float half, const Window& w) { return half * 2.0f * w.dist / w.height; }

// A pinhole's normalised device x and y of a point, for a frustum given as tangents: eye at the origin looking down -z.
inline bool Ndc(Vec p, float tanL, float tanR, float tanD, float tanU, float* nx, float* ny) {
    if (!(p.z < -1e-6f)) return false;
    const float x = p.x / -p.z, y = p.y / -p.z;
    *nx = (2.0f * x - (tanR + tanL)) / (tanR - tanL);
    *ny = (2.0f * y - (tanU + tanD)) / (tanU - tanD);
    return true;
}

}  // namespace views3d
