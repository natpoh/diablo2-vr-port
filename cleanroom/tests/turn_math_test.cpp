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
// edge      EdgeScale / CutsEdge: a cell over a foreground's edge is cut, one on a surface is kept

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

}  // namespace

int main() {
    AxesCases();
    ImageCases();
    MoveCases();
    TurnCases();
    CarryCases();
    EdgeCases();
    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
