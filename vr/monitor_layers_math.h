#pragma once
// [render] monitor_panels (vr/monitor_layers.h): where a quad layer the headset shows lies in the eye's picture on the monitor - apart from
// D3D12 and OpenXR, so that it can be tested (tools/monitor_layers_test.cpp).
//
// The headset puts each quad (a panel: the menu with the inventory, the trader, the stash; the toolbar, the map, the laser...) over the eye's
// picture where it is seen from that eye. The window shows the eye's picture (the back buffer: the game's whole picture), maybe its middle
// cropped to the window's shape (monitor_eye_fit). Here: the quad's four corners as clip-space positions for that picture, so the GPU draws
// it where the headset shows it (perspective-correct: w is the depth along the view).

#include <cmath>
#include <cstdint>

namespace monitorlayers {

struct V3 { float x, y, z; };
struct Quat { float x, y, z, w; };   // OpenXR's order
struct Pose { V3 p; Quat q; };

// The tangents of a picture's edges as seen from its eye: left and down below zero, right and up above (OpenXR's fov, tan of each angle).
struct Tan { float l, r, u, d; };

// A part of the eye's picture the target shows (0..1 of its width and height: top left, size) - monitor_eye's crop, or the whole.
struct Part { float u, v, w, h; };

inline V3 Rot(const Quat& q, const V3& v) {
    const V3 u{q.y * v.z - q.z * v.y + q.w * v.x, q.z * v.x - q.x * v.z + q.w * v.y, q.x * v.y - q.y * v.x + q.w * v.z};
    return {v.x + 2.0f * (q.y * u.z - q.z * u.y), v.y + 2.0f * (q.z * u.x - q.x * u.z), v.z + 2.0f * (q.x * u.y - q.y * u.x)};
}
inline Quat Conj(const Quat& q) { return {-q.x, -q.y, -q.z, q.w}; }
inline Quat Mul(const Quat& a, const Quat& b) {   // b's turn first, then a's: Rot(Mul(a, b), v) = Rot(a, Rot(b, v))
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

// A quad hung in a space of its own (OpenXR: its layer's `space` - the room's, the stage, while the eyes' views are in the session's local
// space) in the eyes' space: `space` is where that space lies in the eyes' (xrLocateSpace(its space, the projection layer's space)).
// The runtime does this for the headset; the monitor took the pose as if it were the eyes' space - the map's orb in the hand, hung in the
// stage, drawn ~1.1 m over the eyes and turned by the local space's recentre (Serg, 0.201.2: "the map not in the sphere in the hand but
// somewhere up on the screen", a thin strip in the sky).
inline Pose InSpace(const Pose& space, const Pose& p) {
    const V3 r = Rot(space.q, p.p);
    return {{space.p.x + r.x, space.p.y + r.y, space.p.z + r.z}, Mul(space.q, p.q)};
}

// The whole picture's tangents from a part of it submitted with its own fov (OpenXR's projection view: `sub` the fov of the image rect at
// rx, ry, rw x rh pixels; [openxr] runtime_fov submits only the runtime's own view of the picture), the picture pw x ph pixels. A tangent
// is linear in the pixels across a flat picture.
inline Tan WholePicture(const Tan& sub, int rx, int ry, int rw, int rh, int pw, int ph) {
    if (rw <= 0 || rh <= 0 || pw <= 0 || ph <= 0) return sub;
    const float kx = (sub.r - sub.l) / (float)rw, ky = (sub.d - sub.u) / (float)rh;
    return {sub.l - kx * (float)rx, sub.l + kx * (float)(pw - rx), sub.u - ky * (float)ry, sub.u + ky * (float)(ph - ry)};
}

struct Clip { float x, y, z, w; };
// A quad as drawn: its corners top left, top right, bottom left, bottom right (a triangle strip), in clip space for the target.
struct Corners {
    Clip c[4];
    bool seen;   // some of it may be in the picture (false: all behind the eye, or all past one edge - not drawn)
};

// The point `e` in the eye's space (OpenXR: x right, y up, looking down -z) in clip space for a target showing `part` of the picture:
// x and y -w..w across the target, z half of w (in 0..w: clipped behind the eye only), w the depth.
inline Clip ToClip(const V3& e, const Tan& t, const Part& part) {
    const float w = -e.z;
    // 0..1 across the picture times w (still linear in the point): u from the left edge, v from the top
    const float uw = (e.x - t.l * w) / (t.r - t.l), vw = (t.u * w - e.y) / (t.u - t.d);
    const float pu = (uw - part.u * w) / part.w, pv = (vw - part.v * w) / part.h;   // 0..1 across the part, times w
    return {2.0f * pu - w, w - 2.0f * pv, 0.5f * w, w};
}

// The quad at `quad` (its plane x right, y up; seen from +z), `width` x `height` m, seen from the eye at `eye` with the picture's `t`.
inline Corners Project(const Pose& eye, const Tan& t, const Part& part, const Pose& quad, float width, float height) {
    Corners out{};
    const float hw = 0.5f * width, hh = 0.5f * height;
    const V3 local[4] = {{-hw, hh, 0.0f}, {hw, hh, 0.0f}, {-hw, -hh, 0.0f}, {hw, -hh, 0.0f}};
    const Quat inv = Conj(eye.q);
    for (int i = 0; i < 4; ++i) {
        const V3 r = Rot(quad.q, local[i]);
        const V3 world{quad.p.x + r.x - eye.p.x, quad.p.y + r.y - eye.p.y, quad.p.z + r.z - eye.p.z};
        out.c[i] = ToClip(Rot(inv, world), t, part);
    }
    // left out only when every corner is past the same edge (or behind the eye): a quad across an edge is clipped by the GPU
    auto allPast = [&](auto past) { for (const Clip& c : out.c) if (!past(c)) return false; return true; };
    out.seen = !(allPast([](const Clip& c) { return c.w <= 1e-4f; }) || allPast([](const Clip& c) { return c.x < -c.w; }) ||
                 allPast([](const Clip& c) { return c.x > c.w; }) || allPast([](const Clip& c) { return c.y < -c.w; }) ||
                 allPast([](const Clip& c) { return c.y > c.w; }));
    return out;
}

// A quad's eye visibility (XrEyeVisibility: 0 both, 1 left, 2 right) for the eye whose picture it goes over (0 left, 1 right).
inline bool ForEye(int visibility, int eye) { return visibility == 0 || visibility == eye + 1; }

// What the target shows of the picture: the whole, or monitor_eye's crop of it.
inline Part Whole() { return {0.0f, 0.0f, 1.0f, 1.0f}; }

// The quad's colour scale on the monitor: the layer's own (a fade: XrCompositionLayerColorScaleBiasKHR) without [openxr] brightness, which
// the runtime puts over every layer - the eye's picture under it on the monitor has none.
inline void ScaleWithoutBrightness(const float layer[4], float bright, float out[4]) {
    const float k = bright > 1e-3f ? 1.0f / bright : 1.0f;
    out[0] = layer[0] * k;
    out[1] = layer[1] * k;
    out[2] = layer[2] * k;
    out[3] = layer[3];
}

}  // namespace monitorlayers
