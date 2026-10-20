// Eyes by turns, the eye not drawn made from pictures already drawn (vr/turn_math.h): the maths on synthetic cameras and
// points - no GPU, no headset:
//
//   turn_math_test        every case; exit code 0 = all passed
//
// axes      FromAxes: a camera's right / up / back into its turn, and back
// image     ViewToImage / ImageToView: the projection's round trip, reverse-Z (near / distance), u right, v down
// move      MovePoint: a point drawn by one eye, moved with its depth, lands where the other eye sees it directly - the eye
//           gap (the drawn eye laid over the other), a head turn and step (the eye's own last picture), asymmetric fovs
// turn      Homography: rotation only - a far point lands where it is seen; a near one does not (what the depth is for)
// carry     Carry: what the hero holds goes with his step and his body's turn, the world stays
// held      Held: first person - what is near stays in the picture (the doll is posed from the head), the world moves
// edge      EdgeScale / CutsEdge: a cell over a foreground's edge is cut, one on a surface is kept
// overlay   OverlayDepthOf: the eye drawn's overlays by the game's depth, the eye made's by its own moved depth or by nothing -
//           never the other eye's (the doubled medallions, 2026-10-20)
// overlay camera  OtherEyeAt / GapOf: the eye made's overlays from the camera its picture shows - the eye drawn's moved across the
//           gap; unmoved or moved the wrong way the wrist orb lands elsewhere (shown twice)

#include "turn_math.h"

#include <cstdio>

using namespace turnwarp;

namespace {

int g_failed = 0, g_checks = 0;
void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_failed; std::printf("FAIL: %s\n", what); }
}
bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

// A camera at `at`, turned by yaw (about +y) then pitch (about its x).
Camera Cam(Vec at, float yaw, float pitch, Tan t) {
    const Quat q = Multiply(Yaw(yaw), Quat{std::sin(0.5f * pitch), 0, 0, std::cos(0.5f * pitch)});
    return {at, q, t, true};
}
// Where camera c sees a world point: image place and depth.
bool See(const Camera& c, float nearPlane, Vec world, float* u, float* v, float* d) {
    return ViewToImage(c.tan, nearPlane, WorldToView(c, world), u, v, d);
}

const Tan kLeftTan{-1.05f, 0.92f, -1.10f, 0.98f};    // a headset's left eye: wider outwards
const Tan kRightTan{-0.92f, 1.05f, -1.10f, 0.98f};
constexpr float kNear = 1.5f;                       // vrcam's [camera] near, world units
constexpr float kUpm = 3.6f;                        // world units a metre

void AxesCases() {
    for (float yaw : {0.0f, 0.7f, -2.5f, 3.1f})
        for (float pitch : {0.0f, -0.6f, 0.4f}) {
            const Camera c = Cam({}, yaw, pitch, kLeftTan);
            const Quat q = FromAxes(Rotate(c.turn, {1, 0, 0}), Rotate(c.turn, {0, 1, 0}), Rotate(c.turn, {0, 0, 1}));
            const Vec a = Rotate(q, {0.3f, -0.2f, 0.9f}), b = Rotate(c.turn, {0.3f, -0.2f, 0.9f});
            Check(Near(a.x, b.x) && Near(a.y, b.y) && Near(a.z, b.z), "axes: FromAxes gives the camera's turn back");
        }
    // vrcam's camera: right, up and minus ahead are the columns
    const Vec fwd{0.0f, 0.0f, -1.0f}, up{0, 1, 0}, right{1, 0, 0};
    const Quat q = FromAxes(right, up, fwd * -1.0f);
    Check(Near(q.w, 1.0f) && Near(q.x, 0) && Near(q.y, 0) && Near(q.z, 0), "axes: looking down -z upright is no turn");
}

void ImageCases() {
    const Vec p{0.7f, -0.4f, -6.0f};
    float u, v, d;
    Check(ViewToImage(kLeftTan, kNear, p, &u, &v, &d), "image: a point ahead is seen");
    Check(Near(d, kNear / 6.0f), "image: reverse-Z depth is near / distance");
    const Vec back = ImageToView(kLeftTan, kNear, u, v, d);
    Check(Near(back.x, p.x, 1e-4f) && Near(back.y, p.y, 1e-4f) && Near(back.z, p.z, 1e-4f), "image: and back");
    Check(ViewToImage(kLeftTan, kNear, {0, 0, -1}, &u, &v, &d) && Near(u, 1.05f / 1.97f) && Near(v, 0.98f / 2.08f),
          "image: straight ahead is off-centre by the fov's own asymmetry");
    Check(ViewToImage(kLeftTan, kNear, {1.0f, 0, -1}, &u, &v, &d) && u > 0.99f, "image: u grows to the right");
    Check(ViewToImage(kLeftTan, kNear, {0, 0.9f, -1}, &u, &v, &d) && v < 0.05f, "image: v grows downwards");
    Check(!ViewToImage(kLeftTan, kNear, {0, 0, 1}, &u, &v, &d), "image: behind the camera is not seen");
}

// Points of a scene, near and far, seen from `from`, moved to `to`: where `to` sees them directly.
void MoveBetween(const Camera& from, const Camera& to, const char* what) {
    const auto move = Move(from.at, from.turn, to.at, to.turn);
    const Vec pts[] = {{0.5f, 1.2f, -8.0f}, {-2.0f, 0.0f, -30.0f}, {0.2f, 1.6f, -2.2f}, {3.0f, 2.5f, -12.0f}, {-0.6f, 0.4f, -4.0f}};
    int ok = 0, seen = 0;
    for (const Vec& w : pts) {
        float u, v, d, u2, v2, d2, ue, ve, de;
        if (!See(from, kNear, w, &u, &v, &d) || !See(to, kNear, w, &ue, &ve, &de)) continue;
        ++seen;
        if (MovePoint(from.tan, to.tan, kNear, move, move, 0, 0, u, v, d, &u2, &v2, &d2) && Near(u2, ue, 2e-4f) && Near(v2, ve, 2e-4f) &&
            Near(d2, de, 2e-5f))
            ++ok;
    }
    Check(seen >= 4 && ok == seen, what);
}

void MoveCases() {
    // the eye drawn this frame laid over the other: the same head, the eye gap along its right
    const float gap = 0.064f * kUpm;
    const Camera left = Cam({10.0f, 6.1f, 4.0f}, 0.4f, -0.3f, kLeftTan);
    Camera right = left;
    right.at = left.at + Rotate(left.turn, {gap, 0, 0});
    right.tan = kRightTan;
    MoveBetween(left, right, "move: the left eye's picture to the right eye (the eye gap, the fovs mirrored)");
    MoveBetween(right, left, "move: and the right to the left");
    // the eye's own last picture: a frame later the head turned and stepped, the hero walked
    const Camera was = Cam({10.0f, 6.1f, 4.0f}, 0.4f, -0.3f, kRightTan);
    const Camera now = Cam({10.4f, 6.0f, 3.7f}, 0.47f, -0.25f, kRightTan);
    MoveBetween(was, now, "move: the eye's own picture to where the head is a frame later");
    // nothing moved: the identity
    const auto same = Move(was.at, was.turn, was.at, was.turn);
    float u, v, d;
    Check(MovePoint(was.tan, was.tan, kNear, same, same, 0, 0, 0.31f, 0.62f, 0.2f, &u, &v, &d) && Near(u, 0.31f) && Near(v, 0.62f) && Near(d, 0.2f),
          "move: the same camera: every point where it was");
    Check(!MovePoint(was.tan, was.tan, kNear, same, same, 0, 0, 0.5f, 0.5f, 0.0f, &u, &v, &d), "move: the sky (depth 0) is not a point");
}

void TurnCases() {
    const Camera was = Cam({0, 6, 0}, 0.2f, 0.0f, kLeftTan), now = Cam({0, 6, 0}, 0.35f, 0.1f, kLeftTan);
    const auto h = Homography(was.turn, was.tan, now.turn, now.tan);
    const auto map = [&](float u, float v, float* du, float* dv) {
        const float x = h[0] * u + h[1] * v + h[2], y = h[3] * u + h[4] * v + h[5], z = h[6] * u + h[7] * v + h[8];
        *du = x / z; *dv = y / z;
    };
    // a far point (the turn alone is right for it)
    const Vec far = Rotate(now.turn, {0.3f, -0.2f, -1.0f}) * 1e4f + now.at;
    float un, vn, dn, ud, vd, dd, hu, hv;
    See(now, kNear, far, &un, &vn, &dn);
    See(was, kNear, far, &ud, &vd, &dd);
    map(un, vn, &hu, &hv);
    Check(Near(hu, ud, 1e-4f) && Near(hv, vd, 1e-4f), "turn: a far point - where the drawn eye saw it");
    // after a step a near point is off: the depth pass is what moves it
    Camera stepped = now;
    stepped.at = now.at + Vec{0.5f, 0, 0};
    const auto h2 = Homography(was.turn, was.tan, stepped.turn, stepped.tan);
    const Vec nearP = Rotate(stepped.turn, {0.1f, 0.0f, -1.0f}) * 3.0f + stepped.at;
    See(stepped, kNear, nearP, &un, &vn, &dn);
    See(was, kNear, nearP, &ud, &vd, &dd);
    const float x = h2[0] * un + h2[1] * vn + h2[2], z = h2[6] * un + h2[7] * vn + h2[8];
    Check(std::fabs(x / z - ud) > 0.02f, "turn: a near point after a step is not where the turn alone puts it");
}

void CarryCases() {
    // the hero walks 1 unit along x and his body turns 30 degrees; the head stays on the body
    const Vec heroWas{5, 0, 5}, heroNow{6, 0, 5};
    const float bodyWas = 0.3f, bodyNow = 0.3f + 0.5236f;
    const Vec eyeOff{0.1f, 6.0f, 0.0f};   // the eye over the hero, in the body's frame
    const Camera was{heroWas + Rotate(Yaw(bodyWas), eyeOff), Yaw(bodyWas), kLeftTan, true};
    const Camera now{heroNow + Rotate(Yaw(bodyNow), eyeOff), Yaw(bodyNow), kLeftTan, true};
    const auto move = Move(was.at, was.turn, now.at, now.turn);
    const auto carry = Carry(was.at, was.turn, heroWas, bodyWas, heroNow, bodyNow, now.at, now.turn);
    // a hand held 2 units ahead and below the eye: carried, it stays where it was in the picture
    const Vec hand = was.at + Rotate(was.turn, {0.3f, -1.0f, -2.0f});
    float u, v, d, u2, v2, d2;
    See(was, kNear, hand, &u, &v, &d);
    const float nearM = kCarriedNearM * kUpm, farM = kCarriedFarM * kUpm;
    Check(MovePoint(was.tan, now.tan, kNear, move, carry, nearM, farM, u, v, d, &u2, &v2, &d2) && Near(u2, u, 1e-4f) && Near(v2, v, 1e-4f),
          "carry: a hand within reach goes with the hero: the same place in the picture");
    // a wall 20 units away: the world's, where the moved eye sees it
    const Vec wall = was.at + Rotate(was.turn, {1.0f, 0.5f, -20.0f});
    float ue, ve, de;
    See(was, kNear, wall, &u, &v, &d);
    See(now, kNear, wall, &ue, &ve, &de);
    Check(MovePoint(was.tan, now.tan, kNear, move, carry, nearM, farM, u, v, d, &u2, &v2, &d2) && Near(u2, ue, 2e-4f) && Near(v2, ve, 2e-4f),
          "carry: the world past reach stays the world's");
    // without a carry the hand would smear a frame of the walk behind
    See(was, kNear, hand, &u, &v, &d);
    MovePoint(was.tan, now.tan, kNear, move, move, 0, 0, u, v, d, &u2, &v2, &d2);
    Check(std::fabs(u2 - u) > 0.02f, "carry: (without it the hand is left behind)");
}

// First person: what is near (the doll, the hands) is posed from the head - it stays where it was in the picture, whatever the head did.
void HeldCases() {
    const Camera was = Cam({10.0f, 6.1f, 4.0f}, 0.4f, -0.3f, kRightTan), now = Cam({10.6f, 6.0f, 3.5f}, 0.9f, -0.1f, kRightTan);
    const auto move = Move(was.at, was.turn, now.at, now.turn);
    const auto held = Held();
    const float nearM = kCarriedNearM * kUpm, farM = kCarriedFarM * kUpm;
    float u, v, d, u2, v2, d2, ue, ve, de;
    const Vec hand = was.at + Rotate(was.turn, {0.4f, -1.2f, -2.0f});   // a hand, 0.7 m off the eye
    See(was, kNear, hand, &u, &v, &d);
    Check(MovePoint(was.tan, now.tan, kNear, move, held, nearM, farM, u, v, d, &u2, &v2, &d2) && Near(u2, u) && Near(v2, v) && Near(d2, d),
          "held: the doll's hand stays in the picture through a head turn and step");
    const Vec wall = was.at + Rotate(was.turn, {1.0f, 0.5f, -20.0f});
    See(was, kNear, wall, &u, &v, &d);
    See(now, kNear, wall, &ue, &ve, &de);
    Check(MovePoint(was.tan, now.tan, kNear, move, held, nearM, farM, u, v, d, &u2, &v2, &d2) && Near(u2, ue, 2e-4f) && Near(v2, ve, 2e-4f),
          "held: the world past reach is the world's");
}

void EdgeCases() {
    const float gap = 0.064f * kUpm;
    const float k = EdgeScale(2048, kRightTan, gap, kNear);
    Check(k > 0.0f && Near(k, 2048.0f / 1.97f * gap / kNear, 1e-2f), "edge: the scale is pixels per tangent times gap / near");
    // a monster's edge at 2 m over a wall 10 m away: depth near/w in world units
    const float fg = kNear / (2.0f * kUpm), bg = kNear / (10.0f * kUpm);
    Check(CutsEdge(fg, fg, bg, bg, k), "edge: a cell across a 2 m / 10 m edge is cut");
    // a floor sloping away across a 4-pixel cell: a few cm in depth
    const float a = kNear / (5.0f * kUpm), b = kNear / (5.03f * kUpm);
    Check(!CutsEdge(a, a, b, b, k), "edge: a cell on a surface is kept");
    Check(!CutsEdge(fg, fg, bg, bg, 0.0f), "edge: no scale (the eye's own picture): never cut");
}

void OverlayCases() {
    using F = OverlayDepthFrom;
    for (int drawn = 0; drawn < 2; ++drawn) {
        const int made = drawn ^ 1;
        Check(OverlayDepthOf(true, false, drawn, drawn) == F::Game && OverlayDepthOf(true, true, drawn, drawn) == F::Game,
              "overlay: the eye drawn this frame - the game's depth, its own");
        Check(OverlayDepthOf(true, false, drawn, made) == F::Nothing, "overlay: the eye made, its depth off or not bound - nothing hides");
        Check(OverlayDepthOf(true, true, drawn, made) == F::Made, "overlay: the eye made, turn_overlay_depth bound - its own moved depth");
        Check(OverlayDepthOf(true, false, drawn, made) != F::Game && OverlayDepthOf(true, true, drawn, made) != F::Game,
              "overlay: the eye made never takes the game's depth (the other eye's)");
        Check(OverlayDepthOf(false, true, drawn, made) == F::Game, "overlay: not apart (both eyes drawn) - the game's, as always");
    }
}

// The eye made's overlays from its own camera: the eye drawn's moved across the gap is the camera the warp makes that eye for, so an
// orb on the wrist lands where the made picture shows the wrist; the eye drawn's camera unmoved (or moved the wrong way) is off by
// the gap and its orb lands elsewhere - a second one beside the first (the doubled medallions, 2026-10-20).
void OverlayCameraCases() {
    const float half = 0.032f * kUpm;
    const Tan t{-1.0f, 0.9f, -1.0f, 1.0f};
    const Camera head = Cam({1, 50, 2}, 0.4f, -0.3f, t);
    const Vec right = Rotate(head.turn, {1, 0, 0}), up = Rotate(head.turn, {0, 1, 0}), back = Rotate(head.turn, {0, 0, 1});
    const Vec at[2] = {head.at - right * half, head.at + right * half};   // vrcam's TurnView: the warp's two eyes
    const Vec wrist = head.at + Rotate(head.turn, Vec{0.15f, -0.35f, -0.6f} * kUpm);
    for (int drawn = 0; drawn < 2; ++drawn) {
        const int made = drawn ^ 1;
        const Vec madeAt = OtherEyeAt(at[drawn], right, half, drawn);
        Check(Near(madeAt.x, at[made].x, 1e-3f) && Near(madeAt.y, at[made].y, 1e-3f) && Near(madeAt.z, at[made].z, 1e-3f),
              "overlay camera: the eye drawn's moved across the gap is the eye made's");
        const Camera picture{at[made], head.turn, t, true};
        const CameraGap g = GapOf(picture, madeAt, right, up, back);
        Check(g.at < 1e-3f && g.turn < 1e-4f, "overlay camera: the eye made's overlays from the camera its picture shows");
        const CameraGap wrong = GapOf(picture, at[drawn], right, up, back);
        Check(Near(wrong.at, 2.0f * half, 1e-3f), "overlay camera: the eye drawn's camera for the eye made is off by the gap");
        const CameraGap swapped = GapOf(picture, OtherEyeAt(at[drawn], right, half, made), right, up, back);
        Check(Near(swapped.at, 4.0f * half, 1e-3f), "overlay camera: moved the wrong way, off by twice the gap");
        float u0, v0, d0, u1, v1, d1;
        const Camera from{madeAt, head.turn, t, true}, unmoved{at[drawn], head.turn, t, true};
        Check(See(picture, kNear, wrist, &u0, &v0, &d0) && See(from, kNear, wrist, &u1, &v1, &d1) && Near(u0, u1) && Near(v0, v1),
              "overlay camera: the wrist orb lands on the made picture's wrist");
        Check(See(unmoved, kNear, wrist, &u1, &v1, &d1) && std::fabs(u0 - u1) > 0.05f,
              "overlay camera: from the eye drawn's camera it lands a tenth of the picture away - shown twice");
    }
    const CameraGap turned = GapOf(head, head.at, Rotate(Yaw(0.1f), right), Rotate(Yaw(0.1f), up), Rotate(Yaw(0.1f), back));
    Check(turned.at < 1e-4f && Near(turned.turn, 2.0f * std::sin(0.05f), 1e-3f), "overlay camera: a turned camera's miss, as its angle");
}

}  // namespace

int main() {
    AxesCases();
    ImageCases();
    MoveCases();
    TurnCases();
    CarryCases();
    HeldCases();
    EdgeCases();
    OverlayCases();
    OverlayCameraCases();
    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
