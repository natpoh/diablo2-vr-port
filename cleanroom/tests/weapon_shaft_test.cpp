// Offline checks that the weight model's shaft lies on the hit-by-touch blade's axis: both are made from the SAME weapon bone as drawn, with the same
// ends (vr/weapon_shaft.h, vr/blade_shape.h FromBone / ForItem, cleanroom/heft Hold::SetDrawnShaft). No game, no headset.
// Run: build/Release/weapon_shaft_test.exe   (exit code 0 = all passed)

#include "../../vr/weapon_shaft.h"
#include "../../vr/blade_shape.h"
#include "../../vr/metal_head.h"
#include "hold.h"

#include <cmath>
#include <cstdio>

namespace hh = heft::hold;
using heft::Pose;
using heft::Q;
using heft::V3;

static int g_failures = 0;
static void Expect(bool ok, const char* what, double value = 0.0) {
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s (%g)\n", what, value);
    }
}

// A row-vector 4x4: the rotation of q (rows = the images of x y z), scaled per row, and the position p.
static void MakeMat(const Q& q, const V3& p, float sx, float sy, float sz, float out[16]) {
    const V3 ex = heft::Rotate(q, {1, 0, 0}), ey = heft::Rotate(q, {0, 1, 0}), ez = heft::Rotate(q, {0, 0, 1});
    const float s[3] = {sx, sy, sz};
    const V3 r[3] = {ex, ey, ez};
    for (int i = 0; i < 3; ++i) {
        out[i * 4] = (float)(r[i].x * s[i]); out[i * 4 + 1] = (float)(r[i].y * s[i]); out[i * 4 + 2] = (float)(r[i].z * s[i]); out[i * 4 + 3] = 0.0f;
    }
    out[12] = (float)p.x; out[13] = (float)p.y; out[14] = (float)p.z; out[15] = 1.0f;
}
static double Dist(const V3& a, const V3& b) { return heft::Len(heft::Sub(a, b)); }

int main() {
    struct Item { const char* name; uint32_t code, type; bool two; int tipSign; };
    const Item items[] = {
        {"short staff", bladeshape::Code('s', 's', 't', ' '), D2RVR_TYPE_STAFF, true, 1},
        {"long staff", bladeshape::Code('l', 's', 't', ' '), D2RVR_TYPE_STAFF, true, 1},
        {"war staff", bladeshape::Code('w', 's', 't', ' '), D2RVR_TYPE_STAFF, true, 1},
        {"spear (2ht: head along +Y, by its model)", bladeshape::Code('s', 'p', 'r', ' '), D2RVR_TYPE_SPEAR, true, 1},
        {"javelin (1ht: head along -Y, measured)", bladeshape::Code('j', 'a', 'v', ' '), D2RVR_TYPE_JAVELIN, false, -1},
        {"glaive (4-cell javelin)", bladeshape::Code('g', 'l', 'v', ' '), D2RVR_TYPE_JAVELIN, false, -1},
        {"throwing knife (1ht)", bladeshape::Code('t', 'k', 'f', ' '), D2RVR_TYPE_THROWING, false, -1},
        {"throwing axe (1hs)", bladeshape::Code('t', 'a', 'x', ' '), D2RVR_TYPE_THROWING, false, 1},
        {"polearm", 0, D2RVR_TYPE_POLEARM, true, 1},
        {"two-handed sword", 0, D2RVR_TYPE_SWORD, true, 1},
        {"two-handed axe", 0, D2RVR_TYPE_AXE, true, 1},
        {"one-handed sword", 0, D2RVR_TYPE_SWORD, false, 1},
        {"mace", 0, D2RVR_TYPE_MACE, false, 1},
        {"dagger (blade along -Y)", 0, D2RVR_TYPE_DAGGER, false, -1},
    };
    // A few poses of the bone and of the weight model's body, in one frame - the head's, metres (x right, y up, z back), and the same in model units (x upm).
    struct Pose2 { V3 bonePos; double boneAxis[3], boneAng; V3 bodyPos; double bodyAxis[3], bodyAng; };
    const Pose2 poses[] = {
        {{0.20, -0.40, -0.35}, {0.2, 1, 0.3}, 0.7, {0.19, -0.42, -0.30}, {0.1, 0.9, 0.2}, 0.65},
        {{-0.1, -0.2, -0.5}, {1, 0, 0}, 2.2, {-0.1, -0.2, -0.45}, {0, 0, 1}, -0.4},
        {{0.0, 0.1, -0.2}, {0.3, 0.3, 1}, -1.9, {0.05, 0.05, -0.2}, {0.5, 0.5, 0.1}, 3.0},
        {{0.3, -0.6, 0.1}, {0, 1, 0}, 0.0, {0.3, -0.6, 0.1}, {0, 1, 0}, 0.0},
    };
    // (unitM 0: the kind's constants; 0.274: the weapon models' own ends at the hero's 3.65 units per metre - vr/weapon_extents.h)
    for (const float unitM : {0.0f, 1.0f / 3.65f})
    for (const double upm : {1.0, 3.65, 5.3}) {
        for (const Item& it : items) {
            for (const Pose2& ps : poses) {
                const bladeshape::Shape shape = bladeshape::ForItem(it.code, it.type, it.two, unitM);
                Expect(shape.tipSign == it.tipSign, "(the item's tip side as the blade has it)", shape.tipSign);
                const Q qb = heft::QFromAxisAngle(heft::Mul(V3{ps.boneAxis[0], ps.boneAxis[1], ps.boneAxis[2]}, 1.0 / heft::Len(V3{ps.boneAxis[0], ps.boneAxis[1], ps.boneAxis[2]})), ps.boneAng);
                const Q qd = heft::QFromAxisAngle(heft::Mul(V3{ps.bodyAxis[0], ps.bodyAxis[1], ps.bodyAxis[2]}, 1.0 / heft::Len(V3{ps.bodyAxis[0], ps.bodyAxis[1], ps.bodyAxis[2]})), ps.bodyAng);
                // the bone (a scaled weapon: rows of length 1.15) and the body, in the frame whose unit is 1/upm metre
                float bone[16], body[16];
                MakeMat(qb, heft::Mul(ps.bonePos, upm), 1.15f, 1.15f, 1.15f, bone);
                MakeMat(qd, heft::Mul(ps.bodyPos, upm), 1.0f, 1.0f, 1.0f, body);
                const weaponshaft::Line ln = weaponshaft::Relative(bone, body, upm, it.tipSign < 0 ? -2 : 2, shape.length, shape.butt);
                Expect(ln.ok, "the shaft is made");
                if (!ln.ok) continue;
                // THE BLADE: from the same bone in the head frame (metres): its origin and its Y axis, the shape's tipSign, length and butt
                bladeshape::HeadFrame head;
                head.ok = true;
                head.right[0] = 1; head.right[1] = 0; head.right[2] = 0;
                head.fwd[0] = 0; head.fwd[1] = 0; head.fwd[2] = -1;   // (the head frame's own axes: PointToRender is the identity then)
                head.eyeWorld[0] = head.eyeWorld[1] = head.eyeWorld[2] = 0;
                head.upm = 1.0f;
                bladeshape::Bone bn;
                bn.origin[0] = (float)ps.bonePos.x; bn.origin[1] = (float)ps.bonePos.y; bn.origin[2] = (float)ps.bonePos.z;
                const V3 ey = heft::Rotate(qb, {0, 1, 0});
                bn.axisY[0] = (float)ey.x; bn.axisY[1] = (float)ey.y; bn.axisY[2] = (float)ey.z;
                const stroke::Blade cap = bladeshape::FromBone(head, bn, shape);
                // THE WEIGHT MODEL: a hold whose body is at the body pose, given the shaft measured against it
                hh::Hold w;
                hh::Settings s;
                hh::HandIn in[2];
                in[0].tracked = in[1].tracked = true;
                in[0].pose = {ps.bodyPos, qd};
                in[0].grip = 0.0f;
                in[1].pose = {{-0.3, 1.0, 0.0}, {}};
                w.Place(0, hh::Kind::Staff, true, {{0, 0, 0}, {0, 0, -1}}, in, s);   // (a first guess along the fist: not the drawn axis)
                const hh::Line line{{ln.at[0], ln.at[1], ln.at[2]}, {ln.dir[0], ln.dir[1], ln.dir[2]}};
                w.SetDrawnShaft(line, ln.butt, ln.tip, s);
                V3 butt, tip;
                w.Ends(&butt, &tip);
                const double eb = Dist(butt, {cap.base.x, cap.base.y, cap.base.z}), et = Dist(tip, {cap.tip.x, cap.tip.y, cap.tip.z});
                Expect(eb < 2e-4 && et < 2e-4, "the weight model's shaft ends are the blade capsule's ends", std::fmax(eb, et));
                // and it is as long as the blade says: the wood, not a per-kind constant of the weight model's
                Expect(std::fabs(Dist(butt, tip) - (shape.length + shape.butt)) < 2e-4, "the shaft is as long as the blade (tip + butt)", Dist(butt, tip));
                // the axis of the red line is the capsule's axis
                const V3 axW = heft::Mul(heft::Sub(tip, butt), 1.0 / Dist(butt, tip));
                const V3 axB = heft::Mul(heft::Sub({cap.tip.x, cap.tip.y, cap.tip.z}, {cap.base.x, cap.base.y, cap.base.z}), 1.0 / Dist({cap.tip.x, cap.tip.y, cap.tip.z}, {cap.base.x, cap.base.y, cap.base.z}));
                Expect(heft::Dot(axW, axB) > 0.99999, "the shaft's axis is the capsule's axis", heft::Dot(axW, axB));
                // the bone's own origin lies on the line, at the distance along it that the blade has it from the butt
                const double alongBone = ln.boneAlong - ln.butt;
                Expect(std::fabs(alongBone - shape.butt) < 1e-9, "the bone's origin is `butt` from the butt end", alongBone);
                // the grip line (the weight model's shaft, where the hands take the weapon) lies ON the wood and the game's hold is on it: the bone's origin, where the game
                // hangs the item in the hand, is between the butt and the point - not past the butt end, as a spear whose capsule ran the other way had it
                Expect(alongBone >= -1e-9 && alongBone <= Dist(butt, tip) + 1e-9, "the grip line holds the bone's origin: it lies between the butt and the point", alongBone);
            }
        }
    }

    // THE BUG: a stale first guess ~9 degrees off the drawn axis (the line measured on another pose, or from an axis choice): before SetDrawnShaft the weight
    // model's line was off the capsule by decimetres; after it, on it
    {
        const bladeshape::Shape shape = bladeshape::ForItem(bladeshape::Code('s', 's', 't', ' '), D2RVR_TYPE_STAFF, true);
        const Q qb = heft::QFromAxisAngle({0, 0, 1}, 0.8), qd = heft::QFromAxisAngle({0, 0, 1}, 0.8);   // the bone and the body turned the same (as placed)
        float bone[16], body[16];
        const V3 pos{0.2, -0.4, -0.3};
        MakeMat(qb, pos, 1, 1, 1, bone);
        MakeMat(qd, pos, 1, 1, 1, body);
        hh::Hold w;
        hh::Settings s;
        hh::HandIn in[2];
        in[0].tracked = in[1].tracked = true;
        in[0].pose = {pos, qd};
        in[1].pose = {{-0.3, 1.0, 0.0}, {}};
        const double nine = 9.0 * 0.0174532925;
        const hh::Line stale{{0, 0, 0}, {std::sin(nine), std::cos(nine), 0}};   // 9 degrees steeper than the bone's Y, from the fist
        w.Place(0, hh::Kind::Staff, true, stale, in, s);
        V3 b0, t0;
        w.Ends(&b0, &t0);
        const V3 ey = heft::Rotate(qb, {0, 1, 0});
        const V3 capTip = heft::Add(pos, heft::Mul(ey, shape.length));
        Expect(Dist(t0, capTip) > 0.15, "(the bug) a stale line's tip is far off the capsule's", Dist(t0, capTip));
        const weaponshaft::Line ln = weaponshaft::Relative(bone, body, 1.0, 2, shape.length, shape.butt);
        w.SetDrawnShaft({{ln.at[0], ln.at[1], ln.at[2]}, {ln.dir[0], ln.dir[1], ln.dir[2]}}, ln.butt, ln.tip, s);
        V3 b1, t1;
        w.Ends(&b1, &t1);
        Expect(Dist(t1, capTip) < 2e-4, "(the fix) the drawn shaft's tip is the capsule's", Dist(t1, capTip));
        Expect(std::fabs(Dist(b1, t1) - 1.32) < 1e-3, "(the fix) a short staff's wood is 1.32 m (0.66 each side of the bone), not 1.75", Dist(b1, t1));
        // the same shaft again (the next pose, float noise of a tenth of a millimetre): nothing rebuilt, nothing moved
        const V3 keepB = b1, keepT = t1;
        w.SetDrawnShaft({{ln.at[0] + 0.0001, ln.at[1], ln.at[2]}, {ln.dir[0], ln.dir[1], ln.dir[2]}}, ln.butt + 0.0001, ln.tip, s);
        V3 b2, t2;
        w.Ends(&b2, &t2);
        Expect(Dist(b2, keepB) < 1e-12 && Dist(t2, keepT) < 1e-12, "noise under the hysteresis changes nothing");
        // a millimetre the drawn weapon has moved is followed: the model's line is the drawn axis to well under a millimetre, not within three
        w.SetDrawnShaft({{ln.at[0] + 0.001, ln.at[1], ln.at[2]}, {ln.dir[0], ln.dir[1], ln.dir[2]}}, ln.butt, ln.tip, s);
        V3 b3, t3;
        w.Ends(&b3, &t3);
        Expect(std::fabs(Dist(b3, keepB) - 0.001) < 2e-4 && std::fabs(Dist(t3, keepT) - 0.001) < 2e-4, "a millimetre of the drawn weapon is followed", Dist(b3, keepB));
        // the mass is kept when the wood is refitted
        hh::Hold w2;
        w2.Place(0, hh::Kind::Staff, true, stale, in, s);
        const double massBefore = w2.WeaponShape().Mass();
        w2.SetDrawnShaft({{ln.at[0], ln.at[1], ln.at[2]}, {ln.dir[0], ln.dir[1], ln.dir[2]}}, ln.butt, ln.tip, s);
        Expect(std::fabs(w2.WeaponShape().Mass() - massBefore) < 1e-9, "refitted, the staff weighs what it weighed", w2.WeaponShape().Mass());
    }

    // [weight] dual_collide with [gameplay] melee_head_volume: the head's own volume (the model's units, its x y z) against the body is where hit by touch puts it on the
    // same bone (metalhead::ModelPoint: the bone's unit axes times the model's unit in metres) - only in the body's frame; its radius by the model's unit as drawn
    for (const double upm : {1.0, 3.65, 5.3})
        for (const Pose2& ps : poses) {
            const Q qb = heft::QFromAxisAngle(heft::Mul(V3{ps.boneAxis[0], ps.boneAxis[1], ps.boneAxis[2]}, 1.0 / heft::Len(V3{ps.boneAxis[0], ps.boneAxis[1], ps.boneAxis[2]})), ps.boneAng);
            const Q qd = heft::QFromAxisAngle(heft::Mul(V3{ps.bodyAxis[0], ps.bodyAxis[1], ps.bodyAxis[2]}, 1.0 / heft::Len(V3{ps.bodyAxis[0], ps.bodyAxis[1], ps.bodyAxis[2]})), ps.bodyAng);
            float bone[16], body[16];
            MakeMat(qb, heft::Mul(ps.bonePos, upm), 1.15f, 1.15f, 1.15f, bone);
            MakeMat(qd, heft::Mul(ps.bodyPos, upm), 1.0f, 1.0f, 1.0f, body);
            const float va[3] = {0.05f, 1.6f, 0.25f}, vb[3] = {-0.02f, 1.9f, 0.55f}, vr = 0.12f;   // an axe's blade beside the shaft, in model units
            weaponshaft::Head hd;
            Expect(weaponshaft::HeadIn(bone, body, upm, va, vb, vr, &hd), "the head is put on the bone");
            // the touch's way, in the head frame (metres): the bone's origin and unit axes, the unit 1.15 / upm m
            float origin[3] = {(float)ps.bonePos.x, (float)ps.bonePos.y, (float)ps.bonePos.z}, axes[3][3];
            for (int i = 0; i < 3; ++i) {
                const V3 e = heft::Rotate(qb, {i == 0 ? 1.0 : 0.0, i == 1 ? 1.0 : 0.0, i == 2 ? 1.0 : 0.0});
                axes[i][0] = (float)e.x; axes[i][1] = (float)e.y; axes[i][2] = (float)e.z;
            }
            const float unitM = (float)(1.15 / upm);
            float ta[3], tb[3];
            metalhead::ModelPoint(origin, axes, unitM, va, ta);
            metalhead::ModelPoint(origin, axes, unitM, vb, tb);
            const V3 la = heft::Rotate(heft::QConj(qd), heft::Sub({ta[0], ta[1], ta[2]}, ps.bodyPos)), lb = heft::Rotate(heft::QConj(qd), heft::Sub({tb[0], tb[1], tb[2]}, ps.bodyPos));
            const double ea = Dist(la, {hd.a[0], hd.a[1], hd.a[2]}), eb = Dist(lb, {hd.b[0], hd.b[1], hd.b[2]});
            Expect(ea < 2e-5 && eb < 2e-5, "the head against the body is where hit by touch has it on the bone", std::fmax(ea, eb));
            Expect(std::fabs(hd.radius - vr * unitM) < 1e-6, "its radius by the model's unit as drawn", hd.radius);
        }
    {
        float bone[16], body[16], flat[16];
        MakeMat({0, 0, 0, 1}, {0, 0, 0}, 1, 1, 1, bone);
        MakeMat({0, 0, 0, 1}, {0, 0, 0}, 1, 1, 1, body);
        MakeMat({0, 0, 0, 1}, {0, 0, 0}, 0, 0, 0, flat);
        const float va[3] = {0, 1, 0}, vb[3] = {0, 2, 0};
        weaponshaft::Head hd;
        Expect(!weaponshaft::HeadIn(bone, body, 1.0, va, vb, 0.0f, &hd), "no head: no radius");
        Expect(!weaponshaft::HeadIn(bone, flat, 1.0, va, vb, 0.1f, &hd), "a body that is no frame");
        Expect(!weaponshaft::HeadIn(flat, body, 1.0, va, vb, 0.1f, &hd), "a bone that is a point");
    }

    // the numbers that are no use: nothing changes
    {
        const bladeshape::Shape shape = bladeshape::ForItem(0, D2RVR_TYPE_SPEAR, true);
        float bone[16], body[16];
        MakeMat({0, 0, 0, 1}, {0, 0, 0}, 1, 1, 1, bone);
        MakeMat({0, 0, 0, 1}, {0, 0, 0}, 1, 1, 1, body);
        Expect(!weaponshaft::Relative(bone, body, 0.0, 2, shape.length, shape.butt).ok, "no scale");
        Expect(!weaponshaft::Relative(bone, body, 1.0, 0, shape.length, shape.butt).ok, "no axis");
        Expect(!weaponshaft::Relative(bone, body, 1.0, 4, shape.length, shape.butt).ok, "an axis that is none");
        Expect(!weaponshaft::Relative(bone, body, 1.0, 2, 0.0, 0.0).ok, "no wood");
        float flat[16];
        MakeMat({0, 0, 0, 1}, {0, 0, 0}, 0, 0, 0, flat);
        Expect(!weaponshaft::Relative(bone, flat, 1.0, 2, shape.length, shape.butt).ok, "a body that is no frame");
        Expect(!weaponshaft::Relative(flat, body, 1.0, 2, shape.length, shape.butt).ok, "a bone that is a point (the game shrinks one that holds nothing)");
    }
    if (g_failures) {
        std::printf("%d FAILED\n", g_failures);
        return 1;
    }
    std::printf("weapon_shaft_test: all passed\n");
    return 0;
}
