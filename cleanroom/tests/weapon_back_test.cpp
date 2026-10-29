// Offline checks of where a let-go weapon is drawn on the hero's back (vr/weapon_back.h). No game, no headset.
// Run: build/Release/weapon_back_test.exe   (exit code 0 = all passed)

#include "../../vr/weapon_back.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>

using namespace weaponback;

static int g_failures = 0;

static void Expect(bool ok, const char* what, double value = 0.0) {
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s (%g)\n", what, value);
    }
}
static bool Near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }
static bool NearV(V3 a, V3 b, float tol) { return Near(a.x, b.x, tol) && Near(a.y, b.y, tol) && Near(a.z, b.z, tol); }

static Mat Identity() {
    Mat r{};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}
// A bone: rotation about Y by yawDeg (row-vector: v' = v*M), then moved to p.
static Mat BoneAt(V3 p, float yawDeg, float pitchDeg = 0.0f) {
    const float y = yawDeg * 0.0174532925f, x = pitchDeg * 0.0174532925f;
    const float cy = std::cos(y), sy = std::sin(y), cx = std::cos(x), sx = std::sin(x);
    Mat ry = Identity(), rx = Identity();
    ry.m[0] = cy; ry.m[2] = -sy; ry.m[8] = sy; ry.m[10] = cy;
    rx.m[5] = cx; rx.m[6] = sx; rx.m[9] = -sx; rx.m[10] = cx;
    Mat r = Mul(rx, ry);
    r.m[12] = p.x; r.m[13] = p.y; r.m[14] = p.z;
    return r;
}
// The tip the weapon matrix points to, with its attach axis tipAxis.
static V3 Tip(const Mat& w, int tipAxis) {
    const V3 r = Row(w, std::abs(tipAxis) - 1);
    return Norm(Mul(r, tipAxis < 0 ? -1.0f : 1.0f));
}

int main() {
    const V3 right{-1, 0, 0}, up{0, 1, 0}, back{0, 0, -1};
    const Mat bind = BoneAt({0.0f, 3.5f, 0.2f}, 0.0f);   // spine_01 in the bind pose
    const float upm = 3.65f, mid = 0.075f;
    Mat out{};

    // 1. in the bind pose: the tip as the default says, the shaft's middle where the default says
    for (const Class cls : {Class::Long, Class::Short}) {
        for (const int axis : {1, 2, 3, -1, -2, -3}) {
            Expect(BackMatrix(bind, bind, Identity(), axis, mid, upm, cls, {}, &out), "bind: computed");
            const Place d = DefaultPlace(cls);
            const V3 wantTip = Norm(Add(Add(Mul(right, d.tip.x), Mul(up, d.tip.y)), Mul(back, d.tip.z)));
            Expect(NearV(Tip(out, axis), wantTip, 1e-4f), "bind: the tip along the default", axis);
            const V3 middle = Add(Pos(out), Mul(Tip(out, axis), mid * upm));
            const V3 wantMid = Add(Pos(bind), Mul(Add(Add(Mul(right, d.at.x), Mul(up, d.at.y)), Mul(back, d.at.z)), upm));
            Expect(NearV(middle, wantMid, 1e-3f), "bind: the shaft's middle at the default place", axis);
            // the rows are square to each other, right-handed, unit length
            const V3 r0 = Row(out, 0), r1 = Row(out, 1), r2 = Row(out, 2);
            Expect(Near(Dot(r0, r1), 0, 1e-4f) && Near(Dot(r1, r2), 0, 1e-4f) && Near(Dot(r0, r2), 0, 1e-4f), "bind: square", axis);
            Expect(Det3(out) > 0.99f, "bind: right-handed", Det3(out));
        }
    }
    // a long weapon lies diagonally with its tip up, a short one across the lower back with its tip to the hero's left
    {
        BackMatrix(bind, bind, Identity(), 2, mid, upm, Class::Long, {}, &out);
        Expect(Tip(out, 2).y > 0.7f && Dot(Tip(out, 2), right) > 0.3f, "long: the tip up over the right shoulder");
        BackMatrix(bind, bind, Identity(), 2, mid, upm, Class::Short, {}, &out);
        Expect(Dot(Tip(out, 2), right) < -0.9f && std::fabs(Tip(out, 2).y) < 0.35f, "short: across the back, the tip to the left");
        // a weapon in each hand: the left one behind the left shoulder - the long place mirrored across the spine, its tuning's x and yaw too
        Mat l{}, r{};
        Expect(BackMatrix(bind, bind, Identity(), 2, mid, upm, Class::Long, {}, &l, true), "mirror: computed");
        Expect(Tip(l, 2).y > 0.7f && Dot(Tip(l, 2), right) < -0.3f, "mirror: the tip up over the LEFT shoulder");
        const Tuning tune{5.0f, 0.0f, 0.0f, 0.0f, 10.0f, 0.0f};
        BackMatrix(bind, bind, Identity(), 2, mid, upm, Class::Long, tune, &r);
        BackMatrix(bind, bind, Identity(), 2, mid, upm, Class::Long, tune, &l, true);
        const V3 tr = Tip(r, 2), tl = Tip(l, 2);
        Expect(Near(Dot(tr, right), -Dot(tl, right), 1e-4f) && Near(tr.y, tl.y, 1e-4f) && Near(tr.z, tl.z, 1e-4f), "mirror: the tuned tip mirrored", Dot(tl, right));
        Expect(Near(Dot(V3{r.m[12], r.m[13], r.m[14]}, right) + Dot(V3{l.m[12], l.m[13], l.m[14]}, right), 2.0f * Dot(V3{bind.m[12], bind.m[13], bind.m[14]}, right), 1e-3f),
               "mirror: its place on the other side of the spine");
    }

    // 2. the body moves (a turn of 40 degrees about the vertical through its origin, a crouch, a lean): the weapon moves with it, rigidly
    {
        const Mat now = BoneAt({0.4f, 3.1f, 0.5f}, 40.0f, 15.0f);
        Mat inBind{}, inNow{};
        BackMatrix(bind, bind, Identity(), 2, mid, upm, Class::Long, {}, &inBind);
        BackMatrix(bind, now, Identity(), 2, mid, upm, Class::Long, {}, &inNow);
        const Mat rel = Mul(InvAffine(bind), now);   // the bone's move
        const Mat wantNow = Mul(inBind, rel);
        bool same = true;
        for (int i = 0; i < 16; ++i) same = same && Near(inNow.m[i], wantNow.m[i], 1e-3f);
        Expect(same, "body: the weapon is carried by the bone's move, rigidly");
        // (the move is a pure rotation and a shift: the weapon's rows stay unit)
        Expect(Near(Len(Row(inNow, 1)), 1.0f, 1e-4f), "body: no scale from the move");
        // two poses of the same bone give the same matrix: it does not shake
        Mat again{};
        BackMatrix(bind, now, Identity(), 2, mid, upm, Class::Long, {}, &again);
        bool equal = true;
        for (int i = 0; i < 16; ++i) equal = equal && inNow.m[i] == again.m[i];
        Expect(equal, "body: the same bone, the same matrix");
    }

    // 3. scale and handedness of the attach bone stay: a bigger weapon, a mirrored (left) attach bone
    {
        Mat big = Identity();
        for (int i = 0; i < 3; ++i) big.m[i * 5] = 1.15f;
        BackMatrix(bind, bind, big, 2, mid, upm, Class::Long, {}, &out);
        Expect(Near(Len(Row(out, 0)), 1.15f, 1e-4f) && Near(Len(Row(out, 1)), 1.15f, 1e-4f) && Near(Len(Row(out, 2)), 1.15f, 1e-4f), "scale kept");
        Expect(NearV(Tip(out, 2), Tip([&] { Mat o2{}; BackMatrix(bind, bind, Identity(), 2, mid, upm, Class::Long, {}, &o2); return o2; }(), 2), 1e-4f), "scale: the tip is the same");
        Mat mirror = Identity();
        mirror.m[0] = -1.0f;   // a mirrored attach bone
        BackMatrix(bind, bind, mirror, 2, mid, upm, Class::Long, {}, &out);
        Expect(Det3(out) < -0.99f, "a mirrored attach bone stays mirrored", Det3(out));
        Expect(Near(Tip(out, 2).y, 0.866f, 1e-3f), "mirrored: the tip is the same");
    }

    // 4. the tuning: cm in the back's axes, degrees about them
    {
        Mat base{}, moved{};
        BackMatrix(bind, bind, Identity(), 2, mid, upm, Class::Long, {}, &base);
        Tuning t;
        t.x = 10;   // 10 cm to the hero's right
        BackMatrix(bind, bind, Identity(), 2, mid, upm, Class::Long, t, &moved);
        Expect(NearV(Add(Pos(moved), Mul(Pos(base), -1)), Mul(right, 0.10f * upm), 1e-3f), "tuning x: 10 cm to the right");
        t = {};
        t.y = -5;
        BackMatrix(bind, bind, Identity(), 2, mid, upm, Class::Long, t, &moved);
        Expect(NearV(Add(Pos(moved), Mul(Pos(base), -1)), Mul(up, -0.05f * upm), 1e-3f), "tuning y: 5 cm down");
        t = {};
        t.z = 3;
        BackMatrix(bind, bind, Identity(), 2, mid, upm, Class::Long, t, &moved);
        Expect(NearV(Add(Pos(moved), Mul(Pos(base), -1)), Mul(back, 0.03f * upm), 1e-3f), "tuning z: 3 cm back");
        t = {};
        t.yawDeg = 90;
        BackMatrix(bind, bind, Identity(), 2, mid, upm, Class::Long, t, &moved);
        Expect(Near(Tip(moved, 2).y, 0.866f, 1e-3f), "tuning yaw: about the vertical the tip's height stays");
        Expect(std::fabs(Dot(Tip(moved, 2), right)) < 0.01f, "tuning yaw 90: the tip, which pointed 30 deg to the right of up, now points back or forward");
        t = {};
        t.rollDeg = 90;
        BackMatrix(bind, bind, Identity(), 2, mid, upm, Class::Long, t, &moved);
        Expect(NearV(Tip(moved, 2), Tip(base, 2), 1e-4f), "tuning roll: the tip stays");
        Expect(!NearV(Row(moved, 0), Row(base, 0), 0.1f), "tuning roll: the other rows turn");
        t = {};
        t.pitchDeg = 90;
        BackMatrix(bind, bind, Identity(), 2, mid, upm, Class::Long, t, &moved);
        // a quarter turn about the hero's right axis: the tip's right component stays, its up goes back
        Expect(Near(Dot(Tip(moved, 2), right), Dot(Tip(base, 2), right), 1e-3f), "tuning pitch: the right component stays");
    }

    // 5a. QuatFromAxes: the quaternion turns x y z into the three columns it was made from, in every branch (small and half turns, all axes)
    {
        const V3 axes[] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, Norm({1, 1, 0}), Norm({-1, 2, 3}), Norm({0.3f, -0.9f, 0.2f}), Norm({1, 1, 1})};
        const float degs[] = {0, 1, 30, 89, 90, 91, 135, 179, 180, 181, 225, 270, 359};
        const V3 e[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        int checked = 0;
        for (const V3 ax : axes)
            for (const float deg : degs) {
                const V3 a0 = Turn(e[0], ax, deg), a1 = Turn(e[1], ax, deg), a2 = Turn(e[2], ax, deg);
                float q[4];
                QuatFromAxes(a0, a1, a2, q);
                Expect(Near(std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]), 1.0f, 1e-4f), "QuatFromAxes: unit", deg);
                const V3 u{q[0], q[1], q[2]};
                bool ok = true;
                for (int i = 0; i < 3; ++i) {   // q v q* = v + 2 w (u x v) + 2 u x (u x v)
                    const V3 t = Mul(Cross(u, e[i]), 2.0f);
                    const V3 r = Add(Add(e[i], Mul(t, q[3])), Cross(u, t));
                    ok = ok && NearV(r, i == 0 ? a0 : i == 1 ? a1 : a2, 1e-3f);
                }
                Expect(ok, "QuatFromAxes: turns x y z into the columns", deg);
                ++checked;
            }
        Expect(checked == 7 * 13, "QuatFromAxes: all cases run");
        // the identity
        float qi[4];
        QuatFromAxes({1, 0, 0}, {0, 1, 0}, {0, 0, 1}, qi);
        Expect(Near(qi[3], 1.0f, 1e-6f) && Near(qi[0], 0, 1e-6f) && Near(qi[1], 0, 1e-6f) && Near(qi[2], 0, 1e-6f), "QuatFromAxes: identity");
    }

    // 5. numbers that are no use: the weapon stays as it is
    {
        Mat w = Identity();
        w.m[12] = 5;
        Expect(!BackMatrix(bind, bind, w, 0, mid, upm, Class::Long, {}, &out) && out.m[12] == 5.0f, "no tip axis");
        Expect(!BackMatrix(bind, bind, w, 2, mid, 0.0f, Class::Long, {}, &out), "no scale");
        Expect(!BackMatrix(bind, bind, w, 2, std::nanf(""), upm, Class::Long, {}, &out), "NaN length");
        Expect(!BackMatrix(bind, BoneAt({std::nanf(""), 0, 0}, 0), w, 2, mid, upm, Class::Long, {}, &out), "NaN bone");
    }

    if (g_failures) {
        std::printf("%d FAILED\n", g_failures);
        return 1;
    }
    std::printf("weapon_back_test: all passed\n");
    return 0;
}
