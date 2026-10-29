// eye_fov.h - the fov each eye of native OpenXR's pair is drawn and submitted with, and the projection the game's camera gets from it.
//
// The runtime gives each eye its own fov (xrLocateViews), usually asymmetric and mirrored between the eyes: each wider on its outer
// side (a Quest on SteamVR: left -60 / +45, right -45 / +60 deg). What we draw with it (vr/xr.cpp Cropped):
//  - [openxr] symmetric_fov (on): as wide to the left as to the right, the wider side's - the game's interface, drawn once and the same
//    in both eyes, then meets (the names over monsters);
//  - [openxr] fov_crop (on): then narrowed to the game window's aspect about its own middle - square pixels, black above and below;
//    each eye submitted with exactly the fov it was drawn with.
//  - [openxr] runtime_fov (off): each eye submitted as the runtime's own view, exactly, as a typical OpenXR app does - a compositor that
//    takes only the headset's own view right (ALVR into SteamVR: an Apple Vision Pro's world crossed and stretched with our own view,
//    0.179; its driver places each texture over the headset's own projection by the texture bounds alone) then shows it right. It is
//    DRAWN with one centred frustum that holds both eyes' views (Centred: as wide each way as the widest side, as tall each way as the
//    tallest) - no off-axis term in the game's camera, the same in both eyes (drawn off-axis, the light bins missed: a seam down the
//    picture, 2026-10-25), the interface meets - and each eye's own view is the part of it submitted: the picture's sub-rectangle with
//    exactly those pixels' fov (Inside, Pixels), which the runtime hands on as texture bounds.
//
// Pure: tangents in, tangents out (left and down negative). Tested in cleanroom/tests/eye_fov_test.cpp.
#pragma once

#include <algorithm>
#include <cmath>

namespace eyefov {

struct Tan {
    float l, r, u, d;   // tangents of the half-angles: left and down negative
};
inline Tan FromAngles(float l, float r, float u, float d) { return {std::tan(l), std::tan(r), std::tan(u), std::tan(d)}; }

struct Options {
    bool runtime = false;     // [openxr] runtime_fov: drawn Enclosing, submitted as the runtime's own (the two below do nothing)
    bool symmetric = true;    // [openxr] symmetric_fov
    bool crop = true;         // [openxr] fov_crop
    float aspect = 0.0f;      // the game window's width over height (0 = not known yet: no crop)
};

// The symmetric frustum that holds the runtime's: as wide to the left as to the right (the wider side's), its height as it is.
inline Tan Enclosing(Tan t) {
    const float h = std::max(-t.l, t.r);
    t.l = -h;
    t.r = h;
    return t;
}

// [openxr] runtime_fov: the one centred frustum both eyes are drawn with - every ray of both runtime views, no off-axis term on either
// axis (the game's light binning takes the projection as centred: bearstonem/d2r-pcvr's finding, MIT, steamvr/frame_math.h
// EnclosingSymmetricFov - his whole picture submitted with this fov; ours submits each eye's own part of it, Inside / Pixels).
inline Tan Centred(const Tan& a, const Tan& b) {
    const float h = std::max({-a.l, a.r, -b.l, b.r}), v = std::max({a.u, -a.d, b.u, -b.d});
    return {-h, h, v, -v};
}

// The frustum an eye is drawn with, from the runtime's (runtime_fov: from this eye alone - xr.cpp takes Centred of both).
inline Tan Drawn(Tan t, const Options& o) {
    if (o.runtime) return Centred(t, t);
    if (o.symmetric) t = Enclosing(t);
    const float a = o.aspect;
    if (!o.crop || !(a > 0.1f)) return t;
    if ((t.r - t.l) / (t.u - t.d) > a) { const float cx = 0.5f * (t.r + t.l), hw = 0.5f * a * (t.u - t.d); t.l = cx - hw; t.r = cx + hw; }
    else { const float cy = 0.5f * (t.u + t.d), hh = 0.5f * (t.r - t.l) / a; t.u = cy + hh; t.d = cy - hh; }
    return t;
}

// Where frustum `want` lies in a picture drawn with frustum `drawn`: fractions of the picture, x from the left, y from the TOP (an
// image's rows), clamped to the picture.
struct Window {
    float x0 = 0.0f, x1 = 1.0f, y0 = 0.0f, y1 = 1.0f;
    bool Whole() const { return x0 <= 0.0f && x1 >= 1.0f && y0 <= 0.0f && y1 >= 1.0f; }
};
inline Window Inside(const Tan& drawn, const Tan& want) {
    auto clamp01 = [](float v) { return std::min(1.0f, std::max(0.0f, v)); };
    const float w = drawn.r - drawn.l, h = drawn.u - drawn.d;
    Window o;
    if (!(w > 0.0f) || !(h > 0.0f)) return o;
    o.x0 = clamp01((want.l - drawn.l) / w);
    o.x1 = clamp01((want.r - drawn.l) / w);
    o.y0 = clamp01((drawn.u - want.u) / h);
    o.y1 = clamp01((drawn.u - want.d) / h);
    if (!(o.x1 > o.x0) || !(o.y1 > o.y0)) return {};
    return o;
}
// That window in a W x H picture's whole pixels (to the nearest edge, at least one pixel), and the frustum of exactly those pixels -
// what is submitted: the rectangle and its fov agree to the pixel, and differ from `want` by under half a pixel at each edge.
struct Sub {
    int x = 0, y = 0, w = 0, h = 0;
    Tan tan{};
};
inline Sub Pixels(const Tan& drawn, const Window& win, int W, int H) {
    Sub s;
    if (W <= 0 || H <= 0) return s;
    const int x0 = std::clamp((int)std::lround(win.x0 * W), 0, W - 1), x1 = std::clamp((int)std::lround(win.x1 * W), x0 + 1, W);
    const int y0 = std::clamp((int)std::lround(win.y0 * H), 0, H - 1), y1 = std::clamp((int)std::lround(win.y1 * H), y0 + 1, H);
    s.x = x0; s.y = y0; s.w = x1 - x0; s.h = y1 - y0;
    const float tw = drawn.r - drawn.l, th = drawn.u - drawn.d;
    s.tan = {drawn.l + tw * x0 / W, drawn.l + tw * x1 / W, drawn.u - th * y0 / H, drawn.u - th * y1 / H};
    return s;
}

// [openxr] fov_override (off): each eye's angles set by hand - a stopgap for a headset or streamer whose picture stays distorted (an Apple
// Vision Pro on ALVR, 2026-10-26): the player turns them until the world looks right, and those are, by construction, the angles his
// runtime assumes (each eye is then drawn with exactly them and submitted whole with exactly them). Given for the left eye in degrees -
// outer (its left), inner (its right), up, down, each a positive half-angle - and mirrored for the right eye. Clamped to 10 .. 85 degrees.
struct HandAngles {
    float outer = 50.0f, inner = 45.0f, up = 50.0f, down = 50.0f;   // degrees
};
constexpr float kHandMinDeg = 10.0f, kHandMaxDeg = 85.0f;
// The eye's fov in radians (left, right, up, down; left and down negative): eye 0 = left, 1 = right.
struct Angles {
    float l, r, u, d;
};
inline Angles FromHand(const HandAngles& h, int eye) {
    constexpr float k = 3.14159265f / 180.0f;
    auto c = [](float v) { return std::isfinite(v) ? std::clamp(v, kHandMinDeg, kHandMaxDeg) : 45.0f; };
    const float outer = c(h.outer) * k, inner = c(h.inner) * k;
    return eye == 0 ? Angles{-outer, inner, c(h.up) * k, -c(h.down) * k} : Angles{-inner, outer, c(h.up) * k, -c(h.down) * k};
}
// The other way: the runtime's left eye's fov (radians) as the sliders' numbers (degrees), for D2R VR Settings to show and to copy.
inline HandAngles ToHand(float l, float r, float u, float d) {
    constexpr float k = 180.0f / 3.14159265f;
    return {-l * k, r * k, u * k, -d * k};
}

// The camera's projection for a frustum: the x and y scales and the off-axis terms - M[0], M[5], M[8], M[9] of vrcam's VrProj
// (right-handed view, looking down -z: a direction of tangent t across lands at sx * t - offX, Ndc). The frustum's edges land on
// the picture's: tan l -> -1, tan r -> +1, tan d -> -1, tan u -> +1.
struct Proj {
    float sx, sy, offX, offY;
};
inline Proj Projection(const Tan& t) {
    return {2.0f / (t.r - t.l), 2.0f / (t.u - t.d), (t.r + t.l) / (t.r - t.l), (t.u + t.d) / (t.u - t.d)};
}
// Where a direction's tangent lands across the picture, -1 (left / bottom) to +1, for the projection's x (or y) terms -
// what a frustum edge must give (the test's check that Projection is the frustum).
inline float Ndc(float tanX, float scale, float off) { return scale * tanX - off; }

// Both eyes from one game frame: the right eye's pass is the left's replayed, so the game's interface layer is laid out for the left eye's
// projection. How far (uv, + right) the right eye's layer must move for a label to be in the same direction in both eyes: half the off-axis
// terms apart - vrcam's projection shift, the D2R_DepthFog.fx LabelAxisR. 0 for centred (or identical) eyes.
inline float RightLayerShift(const Tan& left, const Tan& right) { return 0.5f * (Projection(left).offX - Projection(right).offX); }

// The two eyes are mirror images (each the other's flipped left for right) - what the replayed right eye needs (vrcam: the right
// eye made from the left pass's projection with its off-axis term moved by twice the shift). Within `tol` of the scales.
inline bool Mirrored(const Tan& left, const Tan& right, float tol = 0.01f) {
    const Proj a = Projection(left), b = Projection(right);
    return std::fabs(a.sx - b.sx) <= tol * a.sx && std::fabs(a.sy - b.sy) <= tol * a.sy && std::fabs(a.offY - b.offY) <= tol;
}

}  // namespace eyefov
