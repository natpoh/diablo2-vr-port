// turn_math.h - eyes by turns, the eye not drawn this frame made from pictures already drawn: the maths, no D3D12, no
// OpenXR, no globals. vr/turn_warp.cpp draws with it on the GPU, vr/xr.cpp feeds it the eyes' cameras;
// cleanroom/tests/turn_math_test.cpp checks it on synthetic cameras and points.
//
// The technique is bearstonem/d2r-pcvr's (MIT, steamvr/turn_warp.h and transport.cpp TurnEye, 2026-10-19; v0.3.24's edges and keeping
// the eye's own picture, 2026-10-20, below SplitCell and KeepOwn): native OpenXR
// draws one eye a game frame, and the other one is rebuilt on the GPU - (1) its own last picture, moved with the depth it
// was drawn with to where that eye is now; (2) over it the eye drawn this frame, moved across with its own depth, cut where
// its depth parts at an edge (what it could not see behind the edge: the older picture shows there); (3) where neither
// reaches (the frame's borders), the turn alone. The code is written afresh for our camera (vr/vrcam.cpp VrViewInner).
//
// Frames: a camera's view axes are OpenXR's - x right, y up, -z ahead; its turn is the quaternion that takes them into the
// world (any right-handed world: vrcam's turns the room about +y into the game's). Image coordinates u right, v down, 0..1.
// A field of view is the tangents of its four edges, left and down negative. The game's projection is reverse-Z with the
// far plane at infinity: a pixel's distance along the view is near / depth, in the world's units.
#pragma once

#include <array>
#include <cmath>

namespace turnwarp {

struct Vec { float x = 0, y = 0, z = 0; };
struct Quat { float x = 0, y = 0, z = 0, w = 1; };
struct Tan { float left = -1, right = 1, down = -1, up = 1; };   // tangents of the edges (left, down < 0)

inline Vec operator+(Vec a, Vec b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec operator-(Vec a, Vec b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec operator*(Vec a, float k) { return {a.x * k, a.y * k, a.z * k}; }
inline float Dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float Length(Vec a) { return std::sqrt(Dot(a, a)); }
inline Vec Cross(Vec a, Vec b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline Quat Conjugate(Quat q) { return {-q.x, -q.y, -q.z, q.w}; }
inline Quat Multiply(Quat a, Quat b) {   // b first, then a
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
inline Vec Rotate(Quat q, Vec v) {
    const Vec u{q.x, q.y, q.z};
    const Vec t = Cross(u, v) * 2.0f;
    return v + t * q.w + Cross(u, t);
}
inline Quat Yaw(float a) { return {0.0f, std::sin(0.5f * a), 0.0f, std::cos(0.5f * a)}; }   // about +y, + turns -z toward -x
inline bool Finite(Vec v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// The turn whose columns are a camera's right, up and back (minus ahead) in the world: orthonormal, right-handed.
inline Quat FromAxes(Vec r, Vec u, Vec b) {
    Quat q;
    const float trace = r.x + u.y + b.z;
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        q = {(u.z - b.y) / s, (b.x - r.z) / s, (r.y - u.x) / s, 0.25f * s};
    } else if (r.x > u.y && r.x > b.z) {
        const float s = std::sqrt(1.0f + r.x - u.y - b.z) * 2.0f;
        q = {0.25f * s, (u.x + r.y) / s, (b.x + r.z) / s, (u.z - b.y) / s};
    } else if (u.y > b.z) {
        const float s = std::sqrt(1.0f + u.y - r.x - b.z) * 2.0f;
        q = {(u.x + r.y) / s, 0.25f * s, (b.y + u.z) / s, (b.x - r.z) / s};
    } else {
        const float s = std::sqrt(1.0f + b.z - r.x - u.y) * 2.0f;
        q = {(b.x + r.z) / s, (b.y + u.z) / s, 0.25f * s, (r.y - u.x) / s};
    }
    const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return n > 1e-6f ? Quat{q.x / n, q.y / n, q.z / n, q.w / n} : Quat{};
}

// One eye's camera in the world as it was drawn (or would be now): where it stands, how it is turned, its field of view.
struct Camera {
    Vec at;
    Quat turn;
    Tan tan;
    bool ok = false;
};

// -- what a camera sees ---------------------------------------------------------------------------------------------

// A point in the camera's view frame to its image (u, v) and its reverse-Z depth (near / distance). False behind it.
inline bool ViewToImage(const Tan& t, float nearPlane, Vec p, float* u, float* v, float* depth) {
    const float w = -p.z;
    if (!(w > 1e-6f)) return false;
    *u = (p.x / w - t.left) / (t.right - t.left);
    *v = (t.up - p.y / w) / (t.up - t.down);
    *depth = nearPlane / w;
    return true;
}
// An image point and its depth back to the camera's view frame.
inline Vec ImageToView(const Tan& t, float nearPlane, float u, float v, float depth) {
    const float away = nearPlane / depth;
    return Vec{t.left + (t.right - t.left) * u, t.up - (t.up - t.down) * v, -1.0f} * away;
}
inline Vec WorldToView(const Camera& c, Vec world) { return Rotate(Conjugate(c.turn), world - c.at); }
inline Vec ViewToWorld(const Camera& c, Vec view) { return Rotate(c.turn, view) + c.at; }

// -- the eye's own last picture, turned only (a homography) --------------------------------------------------------

// This frame's image coordinates to the drawn image's, homogeneous, row-major: (u, v, 1) times the rows, divided by the
// third. Rotation only - what the runtime's own reprojection does; the base under the depth passes and what shows past
// their reach (the frame's borders).
inline std::array<float, 9> Homography(Quat drawn, const Tan& drawnTan, Quat now, const Tan& nowTan) {
    const auto mul = [](const float* a, const float* b) {
        std::array<float, 9> m{};
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) m[r * 3 + c] = a[r * 3] * b[c] + a[r * 3 + 1] * b[3 + c] + a[r * 3 + 2] * b[6 + c];
        return m;
    };
    // this frame's pixel to its ray in its view
    const float ray[9] = {nowTan.right - nowTan.left, 0, nowTan.left, 0, nowTan.down - nowTan.up, nowTan.up, 0, 0, -1};
    // that ray in the drawn view: the columns are this frame's axes there
    const Quat turn = Multiply(Conjugate(drawn), now);
    const Vec x = Rotate(turn, {1, 0, 0}), y = Rotate(turn, {0, 1, 0}), z = Rotate(turn, {0, 0, 1});
    const float axes[9] = {x.x, y.x, z.x, x.y, y.y, z.y, x.z, y.z, z.z};
    // the drawn view's ray to its pixel, times its distance along -z
    const float L = drawnTan.left, R = drawnTan.right, U = drawnTan.up, D = drawnTan.down;
    const float pixel[9] = {1 / (R - L), 0, L / (R - L), 0, -1 / (U - D), -U / (U - D), 0, 0, -1};
    const auto inDrawn = mul(axes, ray);
    return mul(pixel, inDrawn.data());
}

// -- moved with the depth --------------------------------------------------------------------------------------------

// The drawn view's frame to this frame's view: x_now = R x_drawn + t, the rows of [R | t].
inline std::array<float, 12> Move(Vec drawnAt, Quat drawn, Vec nowAt, Quat now) {
    const Quat back = Conjugate(now);
    const Quat turn = Multiply(back, drawn);
    const Vec x = Rotate(turn, {1, 0, 0}), y = Rotate(turn, {0, 1, 0}), z = Rotate(turn, {0, 0, 1});
    const Vec t = Rotate(back, drawnAt - nowAt);
    return {x.x, y.x, z.x, t.x, x.y, y.y, z.y, t.y, x.z, y.z, z.z, t.z};
}
inline Vec Apply(const std::array<float, 12>& m, Vec p) {
    return {m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3], m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7],
            m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11]};
}

// Move for what the hero carries (the hands, the weapon, his body): not fixed in the world but taken along by his step
// (heroWas to heroNow) and his body's turn about +y (bodyWas to bodyNow, radians, as vrcam's room-to-world yaw), then seen
// from this frame's eye. Moved as the world, a hand trailed a frame of the hero's run behind (pcvr, 2026-10-19).
inline std::array<float, 12> Carry(Vec drawnAt, Quat drawn, Vec heroWas, float bodyWas, Vec heroNow, float bodyNow, Vec nowAt, Quat now) {
    const Quat body = Yaw(bodyNow - bodyWas);
    return Move(heroNow + Rotate(body, drawnAt - heroWas), Multiply(body, drawn), nowAt, now);
}

// What the camera carries in first person: the doll is posed from the head every frame (its body turns with the view, the hands hang off
// the eye, a crouch follows the head), so what is near stays where it was in the picture - the identity, in the view's own frame. Moved
// with the hero's step instead (Carry), the doll of the older picture showed beside the one drawn now along its edges, eye after eye:
// "the doll keeps coming off" (the author, 2026-10-20).
inline std::array<float, 12> Held() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}; }

// Nearer than kCarriedNearM metres along the view a point moves with the hero (Carry), farther than kCarriedFarM with the
// world (Move), blended between: the hands and a weapon sit within the first, the ground at the feet - even looking straight
// down - past the second (pcvr's numbers).
constexpr float kCarriedNearM = 1.0f, kCarriedFarM = 1.6f;
// A grid cell of the drawn eye's picture whose corners' depths part by more than this many target pixels, seen from the eye
// gap away, spans an edge: cut, the older picture shows there.
constexpr float kEdgePixels = 2.0f;
// The grid's spacing in target pixels.
constexpr unsigned kGridStep = 4;

// One grid point: its image place and depth in the drawn eye, to this frame's eye. `carry` / `nearM`, `farM`: as above in the
// world's units (0, 0: all by `move`). The GPU's vertex shader does the same (turn_warp.cpp); the test checks this one.
inline bool MovePoint(const Tan& drawnTan, const Tan& nowTan, float nearPlane, const std::array<float, 12>& move,
                      const std::array<float, 12>& carry, float nearM, float farM, float u, float v, float depth,
                      float* outU, float* outV, float* outDepth) {
    if (!(depth > 0.0f)) return false;
    const Vec p = ImageToView(drawnTan, nearPlane, u, v, depth);
    const Vec world = Apply(move, p);
    Vec q = world;
    if (farM > nearM && nearM > 0.0f) {
        const float away = nearPlane / depth;
        const float k = std::fmin(std::fmax((away - nearM) / (farM - nearM), 0.0f), 1.0f);
        q = Apply(carry, p) * (1.0f - k) + world * k;
    }
    return ViewToImage(nowTan, nearPlane, q, outU, outV, outDepth);
}

// The scale the edge test multiplies a depth difference by: target pixels a unit of reverse-Z depth parts by, seen from
// `apart` world units away (the eye gap): a step in depth d1 -> d2 moves `apart` * |1/w1 - 1/w2| in tangent, which is
// apart / near * |d1 - d2|, times the target's pixels per tangent.
inline float EdgeScale(unsigned targetWidth, const Tan& nowTan, float apart, float nearPlane) {
    if (!(apart > 0.0f) || !(nearPlane > 0.0f) || !(nowTan.right > nowTan.left)) return 0.0f;
    return float(targetWidth) / (nowTan.right - nowTan.left) * apart / nearPlane;
}
inline bool CutsEdge(float a, float b, float c, float d, float edgeScale) {
    const float hi = std::fmax(std::fmax(a, b), std::fmax(c, d)), lo = std::fmin(std::fmin(a, b), std::fmin(c, d));
    return edgeScale > 0.0f && (hi - lo) * edgeScale > kEdgePixels;
}

// -- the drawn eye's picture to the pixel along edges (d2r-pcvr v0.3.24, c72b90d) ---------------------------------------
// A cell across an edge was cut whole (CutsEdge): the older picture showed through in 4-pixel steps along every edge - stale where a head,
// a hand or a sword had moved since - and a blade thinner than a cell, missed by the corners, moved with the ground behind it ("ghosting
// on monsters' heads and on swords", pcvr's owner, 2026-10-20). Now such a cell goes as two flat layers, at its nearest depth and its
// farthest, and each pixel keeps to the layer its own depth is nearer: only what the drawn eye could not see keeps the older picture. The
// test reads the cell's corners and its middle (`lo`, `hi`: the farthest and the nearest of the four texels round each), so a thin blade
// in the cell is caught. The GPU's vertex shader does the same (turn_warp.cpp kDepthShader).
struct CellSplit {
    bool split = false;         // across an edge: two layers
    float front = 0, back = 0;  // the nearer layer's depth (reverse-Z: the larger), the farther's
    float mid = 0;              // a pixel nearer than this (its depth larger) is the front layer's
};
inline CellSplit SplitCell(const float lo[5], const float hi[5], float edgeScale) {
    CellSplit s;
    s.back = lo[0];
    s.front = hi[0];
    for (int i = 1; i < 5; ++i) { s.back = std::fmin(s.back, lo[i]); s.front = std::fmax(s.front, hi[i]); }
    s.split = edgeScale > 0.0f && (s.front - s.back) * edgeScale > kEdgePixels;
    s.mid = 0.5f * (s.front + s.back);
    return s;
}
inline bool InFrontLayer(const CellSplit& s, float depth) { return depth > s.mid; }

// -- the eye's own picture kept where it shows the same surface (d2r-pcvr v0.3.24, f4c181f / 6726947 / 931d820 / 9302744) -------
// The game's glow round a bright blade, a monster's hair and the effects layer (spells, flames, Tyrael's wings) have no depth of their
// own: moved across from the drawn eye with the depth behind them they stood where the drawn eye saw them - a ghost blade, a second
// outline of hair, the scene through the wings. The eye's own older picture, moved with its own depth, shows them from this eye: where
// it shows the same surface as the drawn eye's (within kEdgePixels of parallax) it stays. Kept everywhere, the whole eye was a frame
// behind the other (grass, flames, monsters' motion), which pcvr's owner liked less; so only where it matters, by [stereo] turns_keep_own:
enum KeepOwn { kKeepOff = 0, kKeepNear = 1, kKeepAll = 2, kKeepEffects = 3, kKeepNearEffects = 4 };
// The default: where the game's effects are (pcvr's owner chose it at Tyrael, 2026-10-20: the wings right everywhere, no ghost blade).
constexpr int kKeepDefault = kKeepEffects;
// Near things: within keepReach target pixels of something nearer by more than keepNear pixels of parallax (as either eye sees it), the
// nearest depth looked for per kNearBlock-pixel block. Effects: where either eye's effects layer has kKeepEffects' level or more, its
// strongest channel spread over its blocks by a reach. The pixel counts are a 4096-wide eye's (pcvr's numbers), scaled to the target.
constexpr unsigned kNearBlock = 16;
constexpr float kKeepGapPx = 48.0f, kKeepReachPx = 96.0f, kEffectsLevel = 0.02f, kEffectsReachPx = 32.0f, kKeepWidthPx = 4096.0f;
struct Keep { float keep = 0, keepNear = 0, keepReach = 0, keepEffects = 0; };   // turn_warp.h Grid's; all 0: the drawn eye's wherever it reaches
inline Keep KeepOf(int mode, unsigned targetWidth, float gapPx, float reachPx, float level) {
    Keep k;
    if (mode <= kKeepOff || mode > kKeepNearEffects) return k;
    k.keep = kEdgePixels;
    const float wide = float(targetWidth) / kKeepWidthPx;
    if (mode == kKeepNear || mode == kKeepNearEffects) { k.keepNear = gapPx * wide; k.keepReach = reachPx * wide; }
    if (mode >= kKeepEffects) k.keepEffects = level;
    return k;
}
// The grids' own depth test holds kAhead / distance (the clip z the vertex shader writes): parallax pixels a unit of it parts by, for
// the drawn eye's EdgeScale (pixels a unit of reverse-Z depth, near / distance, parts by).
constexpr float kAhead = 1e-3f;
inline float KeepScale(float edgeScale, float nearPlane) { return edgeScale * nearPlane / kAhead; }
// The same surface: the drawn eye's pixel, moved here, and this eye's own there part by no more than `within` pixels. ownTest 0: its own
// grid drew nothing there.
inline bool SameSurface(float drawnTest, float ownTest, float keepScale, float within) {
    return ownTest > 0.0f && keepScale > 0.0f && std::fabs(drawnTest - ownTest) * keepScale <= within;
}

// -- the overlays drawn after it (D2R_DepthFog.fx OverlayDepth, vrcam OnBeginEffects) ------------------------------------------

// Which depth hides `eye`'s overlays (the wrist medallions, the bow's orbs) when they are drawn apart, after the warp: the game's for
// the eye drawn this frame; for the eye made, its own moved depth when that is on and bound ([stereo] turn_overlay_hide), else
// nothing - never the game's, which is the other eye's (an orb on the wrist set against the other eye's hand showed twice).
enum class OverlayDepthFrom { Game, Nothing, Made };
inline OverlayDepthFrom OverlayDepthOf(bool apart, bool madeBound, int drawnEye, int eye) {
    if (!apart || eye == drawnEye) return OverlayDepthFrom::Game;
    return madeBound ? OverlayDepthFrom::Made : OverlayDepthFrom::Nothing;
}

// The eye not drawn's camera for its overlays (vrcam VrViewInner: g_skyView of that eye): the eye drawn's moved across the eyes'
// gap - `half` of it each side of the head, along the head's `right`; the left eye (0) is the one at -right.
inline Vec OtherEyeAt(Vec drawnAt, Vec right, float half, int drawnEye) { return drawnAt + right * (drawnEye == 0 ? 2.0f * half : -2.0f * half); }

// How far the camera an eye's overlays are drawn from (the effect's PhantomEye / CamRight / CamUp / CamBack of that eye) is off the
// camera its picture shows (the eye drawn's, or the one the warp made the other for): `at` in world units, `turn` the largest of the
// three axes' misses (unit vectors, about the angle in radians). 0, 0: one camera - the overlays lie on the picture; off by the eyes'
// gap, they lie where the other eye sees them and show twice.
struct CameraGap { float at = 0, turn = 0; };
inline CameraGap GapOf(const Camera& picture, Vec at, Vec r, Vec u, Vec b) {
    CameraGap g;
    g.at = Length(at - picture.at);
    const Vec ax[3] = {Rotate(picture.turn, {1, 0, 0}), Rotate(picture.turn, {0, 1, 0}), Rotate(picture.turn, {0, 0, 1})};
    const Vec ov[3] = {r, u, b};
    for (int i = 0; i < 3; ++i) g.turn = std::fmax(g.turn, Length(ax[i] - ov[i]));
    return g;
}

}  // namespace turnwarp
