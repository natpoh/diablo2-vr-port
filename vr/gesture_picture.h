#pragma once
// [openxr] gesture_overlay: the schematic picture of a gesture, as geometry in the window's pixels - what the painter (vr/gesture_tab_paint.inc)
// strokes and fills. Pure: no Windows, no game; points and joints in, pixel points out (cleanroom/tests/gesture_overlay_test.cpp).
//
//   GLYPH  - the glyph's icon: BodyWalk's 48 points (fitted to [-0.5, 0.5]^2, y up), one stroke in the glyph's colour;
//   MOTION - the path the same way, a start dot at the first point and an arrow head at the last;
//   POSE   - a stick figure in the pose (head, chest, pelvis, the two hands BodyWalk took; the elbows bent naturally between the shoulder and
//            the hand, a closed hand a fist), small at the lower left, and a thought cloud in front of it - up and to its right - holding the
//            same pose big;
//   BUTTON - the button's name in a key frame (the painter's own: text only).

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "skill_link_abi.h"

namespace gpic {

struct P2 {
    float x = 0, y = 0;
};
struct Box {
    float l = 0, t = 0, r = 0, b = 0;
    float W() const { return r - l; }
    float H() const { return b - t; }
    float Cx() const { return 0.5f * (l + r); }
    float Cy() const { return 0.5f * (t + b); }
};

inline bool Finite(float v) { return std::isfinite(v) != 0; }

// -- GLYPH / MOTION: BodyWalk's points into a box -------------------------------------------------------------------------------------------

// A point of [-0.5, 0.5]^2 (y up) into the box: one scale for both axes (the shape keeps its proportions), centred, `margin` pixels kept free
// on every side; y turned down (the window's pixels go down).
inline P2 Map(float px, float py, Box b, float margin) {
    const float s = std::max(1.0f, std::min(b.W(), b.H()) - 2.0f * margin);
    return {b.Cx() + px * s, b.Cy() - py * s};
}

// The gesture's points (up to `n`; non-finite ones and those far outside the fitted square skipped) as a polyline in the box. Returns how many.
inline int Polyline(const float (*pts)[2], int n, Box b, float margin, P2* out, int max) {
    int k = 0;
    for (int i = 0; i < n && k < max; ++i) {
        const float x = pts[i][0], y = pts[i][1];
        if (!Finite(x) || !Finite(y) || std::fabs(x) > 0.75f || std::fabs(y) > 0.75f) continue;
        out[k++] = Map(x, y, b, margin);
    }
    return k;
}

// The arrow head at the end of a path: two short strokes back from the last point, `len` long, 28 degrees off the last direction (the last
// point and the first one before it at least 2 px away). False when the path has no such direction.
inline bool ArrowHead(const P2* p, int n, float len, P2* left, P2* right) {
    if (n < 2) return false;
    const P2 end = p[n - 1];
    for (int i = n - 2; i >= 0; --i) {
        const float dx = end.x - p[i].x, dy = end.y - p[i].y, d = std::sqrt(dx * dx + dy * dy);
        if (d < 2.0f) continue;
        const float ux = dx / d, uy = dy / d, c = std::cos(0.49f), s = std::sin(0.49f);   // (28 degrees)
        *left = {end.x - len * (ux * c - uy * s), end.y - len * (uy * c + ux * s)};
        *right = {end.x - len * (ux * c + uy * s), end.y - len * (uy * c - ux * s)};
        return true;
    }
    return false;
}

// -- POSE: the stick figure -------------------------------------------------------------------------------------------------------------------

struct V3 {
    float x = 0, y = 0, z = 0;
};
inline V3 Add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 Sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 Mul(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
inline float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float Len(V3 a) { return std::sqrt(Dot(a, a)); }

// The body's measures (metres): where the shoulders and hips sit, how long the bones are.
constexpr float kShoulderOut = 0.17f, kShoulderUp = 0.05f, kUpperArm = 0.29f, kForearm = 0.27f;
constexpr float kHipOut = 0.10f, kFootOut = 0.15f, kFootDown = 0.92f, kHeadR = 0.11f;
constexpr float kClosed = 0.5f;   // a hand closed this much (grip 0..1) is a fist

// The elbow between a shoulder and a hand: the arm's two bones as they are, bent the way an arm bends - outward, down and back. A hand out of
// reach straightens the arm toward it; one at the shoulder folds it.
inline V3 Elbow(V3 shoulder, V3 hand, float side) {
    V3 d = Sub(hand, shoulder);
    float dist = Len(d);
    if (!(dist > 1e-4f)) {
        d = {0.0f, -1.0f, 0.0f};
        dist = 1e-4f;
    } else {
        d = Mul(d, 1.0f / dist);
    }
    const float reach = kUpperArm + kForearm - 1e-3f;
    const float l = std::clamp(dist, 0.05f, reach);
    const float a = (kUpperArm * kUpperArm - kForearm * kForearm + l * l) / (2.0f * l);
    const float h = std::sqrt(std::max(0.0f, kUpperArm * kUpperArm - a * a));
    V3 pole{side * 0.6f, -1.0f, 0.45f};   // outward, down, back (z is the body's back)
    pole = Sub(pole, Mul(d, Dot(pole, d)));
    float pl = Len(pole);
    if (!(pl > 1e-4f)) {   // the arm points along the hint: any perpendicular
        pole = Sub(V3{0.0f, 0.0f, 1.0f}, Mul(d, d.z));
        pl = Len(pole);
        if (!(pl > 1e-4f)) {
            pole = {1.0f, 0.0f, 0.0f};
            pl = 1.0f;
        }
    }
    pole = Mul(pole, 1.0f / pl);
    return Add(Add(shoulder, Mul(d, a)), Mul(pole, h));
}

// The figure in pixels.
struct Figure {
    P2 head;
    float headR = 0;
    P2 neck, pelvis;
    P2 shoulder[2], elbow[2], hand[2];   // [0] the figure's left, [1] its right
    P2 hip[2], foot[2];
    bool fist[2] = {false, false};
    bool known[2] = {false, false};      // BodyWalk had that hand (an unknown one hangs at the side, drawn dim)
    float scale = 0;                     // pixels per metre
};

// The view: from the front, a little to the figure's right and from a little above, so a hand held forward is seen apart from one held out to
// the side. The figure faces the viewer: its right hand is on the picture's left, as a person facing you.
constexpr float kYaw = 0.42f, kPitch = 0.26f;
inline P2 Project(V3 p) {
    const float cy = std::cos(kYaw), sy = std::sin(kYaw), cp = std::cos(kPitch), sp = std::sin(kPitch);
    // turn about y (the figure turned a little to its left: its front toward the picture's right), then tilt (seen from above)
    const float x = p.x * cy - p.z * sy, z = p.x * sy + p.z * cy;
    return {-x, p.y * cp + z * sp};   // (x right on the picture = the figure's left; z back seen from above sits higher)
}

// The pose BodyWalk took (joints: metres from the pelvis, x body-right, y up, z body-back; valid bits; grip L/R closed 0..1) as a figure filling
// the box (from the head's top to the feet, `margin` free), the pelvis centred across. `legs`: how far the feet are below the pelvis - kFootDown
// standing, less for the cloud's figure (stubs: its arms, the pose, drawn bigger).
inline Figure StickFigure(const float (*joints)[3], uint32_t valid, const float* grip, Box b, float margin, float legs = kFootDown) {
    auto J = [&](int j, V3 fallback) {
        if (!(valid & (1u << j))) return fallback;
        const V3 v{joints[j][0], joints[j][1], joints[j][2]};
        return Finite(v.x) && Finite(v.y) && Finite(v.z) && Len(v) < 3.0f ? v : fallback;
    };
    const V3 pelvis = J(BW_LINK_J_PELVIS, V3{0.0f, 0.0f, 0.0f});
    const V3 chest = J(BW_LINK_J_CHEST, Add(pelvis, V3{0.0f, 0.42f, 0.0f}));
    const V3 head = J(BW_LINK_J_HEAD, Add(pelvis, V3{0.0f, 0.72f, 0.0f}));
    Figure f;
    V3 s3[2], e3[2], h3[2];
    for (int k = 0; k < 2; ++k) {
        const float side = k == 0 ? -1.0f : 1.0f;   // [0] left (x < 0), [1] right
        s3[k] = Add(chest, V3{side * kShoulderOut, kShoulderUp, 0.0f});
        const int j = k == 0 ? BW_LINK_J_LHAND : BW_LINK_J_RHAND;
        f.known[k] = (valid & (1u << j)) != 0;
        h3[k] = J(j, Add(s3[k], V3{side * 0.06f, -(kUpperArm + kForearm - 0.03f), 0.02f}));   // (unknown: hanging at the side)
        e3[k] = Elbow(s3[k], h3[k], side);
        const float g = grip ? grip[k] : 0.0f;
        f.fist[k] = Finite(g) && g >= kClosed;
    }
    const V3 hip3[2] = {Add(pelvis, V3{-kHipOut, -0.02f, 0.0f}), Add(pelvis, V3{kHipOut, -0.02f, 0.0f})};
    const float down = std::clamp(Finite(legs) ? legs : kFootDown, 0.05f, kFootDown);
    const float out = kHipOut + (kFootOut - kHipOut) * down / kFootDown;
    const V3 foot3[2] = {Add(pelvis, V3{-out, -down, 0.0f}), Add(pelvis, V3{out, -down, 0.0f})};

    // the figure's extent in the picture's plane (metres), then the scale that fits it in the box
    const V3 all[] = {head, Add(head, V3{0.0f, kHeadR, 0.0f}), chest, pelvis, s3[0], s3[1], e3[0], e3[1], h3[0], h3[1], foot3[0], foot3[1]};
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    for (const V3& v : all) {
        const P2 q = Project(v);
        x0 = std::min(x0, q.x - kHeadR);
        x1 = std::max(x1, q.x + kHeadR);
        y0 = std::min(y0, q.y);
        y1 = std::max(y1, q.y + 0.02f);
    }
    const float w = std::max(0.3f, x1 - x0), h = std::max(0.3f, y1 - y0);
    const float availW = std::max(1.0f, b.W() - 2.0f * margin), availH = std::max(1.0f, b.H() - 2.0f * margin);
    const float s = std::min(availW / w, availH / h);
    f.scale = s;
    const P2 pc = Project(pelvis);
    const float midX = 0.5f * (x0 + x1);
    // across: the pelvis at the middle unless that would push a part out of the box; up: the extent centred
    float ox = b.Cx() - pc.x * s;
    const float lo = b.l + margin - x0 * s, hi = b.r - margin - x1 * s;
    ox = lo <= hi ? std::clamp(ox, lo, hi) : b.Cx() - midX * s;
    const float oy = b.Cy() + 0.5f * (y0 + y1) * s;
    auto Px = [&](V3 v) {
        const P2 q = Project(v);
        return P2{ox + q.x * s, oy - q.y * s};
    };
    f.head = Px(head);
    f.headR = kHeadR * s;
    f.neck = Px(chest);
    f.pelvis = Px(pelvis);
    for (int k = 0; k < 2; ++k) {
        f.shoulder[k] = Px(s3[k]);
        f.elbow[k] = Px(e3[k]);
        f.hand[k] = Px(h3[k]);
        f.hip[k] = Px(hip3[k]);
        f.foot[k] = Px(foot3[k]);
    }
    return f;
}

// -- the thought cloud ---------------------------------------------------------------------------------------------------------------------------

constexpr int kLobes = 9, kBubbles = 3;
constexpr float kLobeMax = 1.12f;   // the biggest bump's radius, in the cloud's bump unit
struct Cloud {
    Box body;                  // the box the whole cloud keeps inside
    float coreRx = 0, coreRy = 0;   // its middle: an ellipse about the box's centre
    P2 lobe[kLobes];           // the bumps round its rim
    float lobeR[kLobes] = {};
    P2 bubble[kBubbles];       // the little bubbles from the thinker's head to the cloud, smallest first
    float bubbleR[kBubbles] = {};
    Box inner;                 // where the pose goes inside it
};

// A cloud filling `body`, with the bubbles running from `from` (the thinker's head) to its lower left rim.
inline Cloud ThoughtCloud(Box body, P2 from) {
    Cloud c;
    c.body = body;
    const float rx = 0.5f * body.W(), ry = 0.5f * body.H(), cx = body.Cx(), cy = body.Cy();
    const float r = 0.30f * std::min(rx, ry);
    for (int i = 0; i < kLobes; ++i) {   // (each bump inside the box: its centre kLobeMax units in from the rim)
        const float a = 6.2831853f * (i + 0.5f) / kLobes;
        c.lobe[i] = {cx + std::cos(a) * (rx - kLobeMax * r), cy + std::sin(a) * (ry - kLobeMax * r)};
        c.lobeR[i] = r * (0.85f + (kLobeMax - 0.85f) * ((i * 37) % 7) / 6.0f);   // (uneven bumps)
    }
    c.coreRx = rx - 0.9f * r;
    c.coreRy = ry - 0.9f * r;
    const P2 rim{cx - 0.94f * rx, cy + 0.20f * ry};   // its left side, a little below the middle
    for (int i = 0; i < kBubbles; ++i) {
        const float t = (i + 1.0f) / (kBubbles + 1.0f);
        c.bubble[i] = {from.x + (rim.x - from.x) * t, from.y + (rim.y - from.y) * t};
        c.bubbleR[i] = r * (0.18f + 0.16f * i);
    }
    c.inner = {cx - 0.72f * rx, cy - 0.78f * ry, cx + 0.72f * rx, cy + 0.80f * ry};
    return c;
}

// The pose picture's parts in a box: the thinker small at the lower left, the cloud with the pose up and to its right, the bubbles rising from
// just above the thinker's head to the cloud's left side.
struct PoseLayout {
    Box thinker;
    Cloud cloud;
};
inline PoseLayout LayoutPose(Box b) {
    PoseLayout p;
    const float w = b.W(), h = b.H();
    p.thinker = {b.l, b.t + 0.40f * h, b.l + 0.36f * w, b.b};
    const Box body{b.l + 0.38f * w, b.t, b.r, b.t + 0.62f * h};
    const P2 from{p.thinker.Cx() + 0.04f * w, p.thinker.t + 0.07f * h};
    p.cloud = ThoughtCloud(body, from);
    return p;
}

}  // namespace gpic
