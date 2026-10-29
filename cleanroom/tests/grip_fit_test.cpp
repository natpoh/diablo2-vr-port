// Offline checks of the grip fit's frame maths (vr/grip_fit.h): the hand that takes a weapon is laid on the shaft as drawn - the palm's middle onto the axis at its
// grasp point, the knuckle line along it - and the per-hand corrections (along / across / up, the roll and the two tilts) act in the shaft's own frame at the grasp.
// Pure maths, no game, no headset.
// Run: build/Release/grip_fit_test.exe   (exit code 0 = all passed)

#include "../../vr/grip_fit.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

using namespace gripfit;

static int g_failures = 0;
static void Expect(bool ok, const char* what, double value = 0.0) {
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s (%g)\n", what, value);
    }
}
static bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
static bool Near(V3 a, V3 b, float eps = 1e-4f) { return Len(Sub(a, b)) <= eps; }
static float Deg(V3 a, V3 b) { return std::acos(std::clamp(Dot(a, b) / (Len(a) * Len(b)), -1.0f, 1.0f)) * 57.2957795f; }
static V3 Mirror(V3 v) { return {-v.x, v.y, v.z}; }   // the hero's left and right (x), as the sagittal plane has them
static V3 Axis(float x, float y, float z) { V3 u; Unit({x, y, z}, &u); return u; }   // About() takes a unit axis

// A right hand holding a vertical shaft: the wrist behind the palm, the fingers pointing ahead (+z), the knuckle line along the shaft (+y), the palm's way out +x.
// (point x knuckles = (0,0,1) x (0,1,0) = (-1,0,0); on the right hand `out` is its negative = +x, as the hook has it.)
struct Rig { Hand hand; Shaft shaft; };
static Rig RightOnShaft(V3 palmOffAxis = {0.03f, 0.0f, 0.02f}) {
    Rig r;
    r.shaft = {{0.30f, 0.0f, 0.10f}, {0.0f, 1.0f, 0.0f}};
    r.hand.side = 0;
    r.hand.point = {0, 0, 1};
    r.hand.knuckles = {0, 1, 0};
    r.hand.out = Mul(Cross(r.hand.point, r.hand.knuckles), -1.0f);
    r.hand.palm = Add({0.30f, 0.45f, 0.10f}, palmOffAxis);
    r.hand.wrist = Sub(r.hand.palm, Mul(r.hand.point, 0.045f));
    return r;
}
// The same hand turned by r about its palm's middle (the shaft where it is: the hand is what turns).
static Hand Turned(Hand h, const Rot& r) {
    h.point = Apply(h.point, r);
    h.knuckles = Apply(h.knuckles, r);
    h.out = Apply(h.out, r);
    h.wrist = Add(h.palm, Apply(Sub(h.wrist, h.palm), r));
    return h;
}
// The hand's reflection across x = 0, as a left hand (the same grip, mirrored).
static Rig Mirrored(const Rig& a) {
    Rig m;
    m.shaft = {Mirror(a.shaft.at), Mirror(a.shaft.dir)};
    m.hand.side = 1 - a.hand.side;
    m.hand.point = Mirror(a.hand.point);
    m.hand.knuckles = Mirror(a.hand.knuckles);
    m.hand.palm = Mirror(a.hand.palm);
    m.hand.wrist = Mirror(a.hand.wrist);
    // `out` is defined by the cross of the other two with the side's sign: mirrored hand, mirrored out
    m.hand.out = Mirror(a.hand.out);
    return m;
}
static V3 Move(const Out& o, V3 p) { return Add(o.pivot, Add(Apply(Sub(p, o.pivot), o.r), o.shift)); }
static bool Orthonormal(const Rot& r) {
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            const float d = r.m[i * 3] * r.m[j * 3] + r.m[i * 3 + 1] * r.m[j * 3 + 1] + r.m[i * 3 + 2] * r.m[j * 3 + 2];
            if (!Near(d, i == j ? 1.0f : 0.0f, 1e-4f)) return false;
        }
    const float det = r.m[0] * (r.m[4] * r.m[8] - r.m[5] * r.m[7]) - r.m[1] * (r.m[3] * r.m[8] - r.m[5] * r.m[6]) + r.m[2] * (r.m[3] * r.m[7] - r.m[4] * r.m[6]);
    return Near(det, 1.0f, 1e-4f);
}

int main() {
    const float cm = 0.01f;   // metres per centimetre: the test space is metres
    const Mode both{true, true}, none{false, false};

    // ---- the base fit: nothing tuned ----
    {
        const Rig a = RightOnShaft();
        const Out o = Solve(a.hand, a.shaft, Tuning{}, both, cm);
        Expect(o.ok, "base fit ok");
        Expect(Orthonormal(o.r), "base fit: a rotation");
        // the palm's middle is on the axis, where it stood along it
        Expect(Near(Len(Sub(o.anchor, Add(a.shaft.at, Mul(a.shaft.dir, Dot(Sub(o.anchor, a.shaft.at), a.shaft.dir))))), 0.0f), "palm's middle on the axis");
        Expect(Near(Dot(Sub(o.anchor, a.shaft.at), a.shaft.dir), Dot(Sub(a.hand.palm, a.shaft.at), a.shaft.dir)), "the grasp point stays where the palm stood along the shaft");
        Expect(Near(o.grasp, o.anchor), "no offsets: the anchor is the grasp point");
        Expect(Near(o.offAxis, std::sqrt(0.03f * 0.03f + 0.02f * 0.02f)), "it says how far off the axis the palm was", o.offAxis);
        Expect(Near(Move(o, a.hand.palm), o.anchor), "moving the palm gives the anchor");
        // the hand is a rigid body: wrist and palm keep their distance
        Expect(Near(Len(Sub(Move(o, a.hand.wrist), Move(o, a.hand.palm))), Len(Sub(a.hand.wrist, a.hand.palm))), "rigid");
        // already aligned: no turn at all
        Expect(Near(o.alignDeg, 0.0f), "aligned knuckles");
        for (int i = 0; i < 9; ++i) Expect(Near(o.r.m[i], i % 4 == 0 ? 1.0f : 0.0f), "an aligned hand is not turned", i);
    }
    // ---- a hand whose knuckle line lies askew is laid along the shaft by the smallest turn; its roll about the shaft stays ----
    {
        const Rig a = RightOnShaft();
        for (const float tilt : {10.0f, 35.0f, 80.0f, -50.0f, 130.0f}) {
            const Rot skew = About({1, 0, 0}, tilt);
            const Hand h = Turned(a.hand, skew);
            const Out o = Solve(h, a.shaft, Tuning{}, both, cm);
            Expect(o.ok && Orthonormal(o.r), "askew hand: a rotation", tilt);
            const V3 k = Apply(h.knuckles, o.r);
            Expect(Near(std::fabs(Dot(k, a.shaft.dir)), 1.0f, 1e-4f), "the knuckle line lies along the shaft", tilt);
            Expect(Near(o.alignDeg, std::min(std::fabs(tilt), 180.0f - std::fabs(tilt))), "alignDeg tells how far off it was", o.alignDeg);
            // the roll is what the hand had: the palm's way out, taken square to the shaft, keeps its direction (smallest turn about an axis square to the shaft)
            const V3 n0 = Sub(h.out, Mul(a.shaft.dir, Dot(h.out, a.shaft.dir))), n1 = Apply(h.out, o.r);
            Expect(Dot(n1, n0) >= -1e-4f, "the roll stays the controller's", Dot(n1, n0));
        }
        // a hand upside down (its knuckles run against the shaft): turned the shorter way, along the shaft either way
        Hand up = Turned(a.hand, About({0, 0, 1}, 180.0f));
        const Out o = Solve(up, a.shaft, Tuning{}, both, cm);
        Expect(Near(std::fabs(Dot(Apply(up.knuckles, o.r), a.shaft.dir)), 1.0f), "a reversed hand lies along the shaft too");
        Expect(Near(o.alignDeg, 0.0f), "reversed knuckles are along the shaft");
    }
    // ---- the hand the game holds it in: not snapped, not aligned - nothing tuned is no change at all ----
    {
        const Rig a = RightOnShaft();
        const Out o = Solve(a.hand, a.shaft, Tuning{}, none, cm);
        Expect(o.ok, "base grip ok");
        for (int i = 0; i < 9; ++i) Expect(o.r.m[i] == (i % 4 == 0 ? 1.0f : 0.0f), "the base grip is not turned", i);
        Expect(Near(o.shift, V3{}, 1e-6f), "the base grip is not moved", Len(o.shift));
        Expect(Near(o.anchor, a.hand.palm), "the base grip's anchor is where the palm is");
        // corrections alone, about the grasp point: along the shaft moves it along it
        Tuning t;
        t.off[0] = 3.0f;
        const Out m = Solve(a.hand, a.shaft, t, none, cm);
        Expect(Near(Sub(m.anchor, a.hand.palm), Mul(a.shaft.dir, 0.03f)), "base grip: 3 cm along the shaft");
        // a tilt turns the hand about the grasp point (the palm's foot on the axis): its distance from it is kept
        Tuning r;
        r.rot[1] = 90.0f;
        const Out q = Solve(a.hand, a.shaft, r, none, cm);
        Expect(Near(Len(Sub(q.anchor, q.grasp)), q.offAxis), "a tilt of a base grip turns it about the grasp point", Len(Sub(q.anchor, q.grasp)));
        // a roll of a base grip turns the palm round the shaft, at the same height along it
        Tuning ro;
        ro.rot[0] = 90.0f;
        const Out w = Solve(a.hand, a.shaft, ro, none, cm);
        Expect(Near(Dot(Sub(w.anchor, w.grasp), a.shaft.dir), 0.0f), "a roll of a base grip keeps the palm's height along the shaft");
        Expect(Near(Sub(w.anchor, w.grasp), Apply(Sub(a.hand.palm, w.grasp), About(a.shaft.dir, 90.0f))), "... and carries it round the shaft");
    }
    // ---- the offsets are along / across / up, in the hand's own frame at the grasp ----
    {
        const Rig a = RightOnShaft();
        const Out zero = Solve(a.hand, a.shaft, Tuning{}, both, cm);
        // the frame by hand: along the shaft, across = the fingers' way (+z), up = out of the palm (+x)
        const V3 along = a.shaft.dir, across = {0, 0, 1}, up = a.hand.out;
        Expect(Near(up, V3{1, 0, 0}), "test setup: out of the palm is +x");
        struct Case { int axis; V3 dir; };
        const Case cases[] = {{0, along}, {1, across}, {2, up}};
        for (const Case& c : cases) {
            Tuning t;
            t.off[c.axis] = 2.5f;
            const Out o = Solve(a.hand, a.shaft, t, both, cm);
            Expect(Near(Sub(o.anchor, zero.anchor), Mul(c.dir, 0.025f)), "an offset is along its own axis", (double)c.axis);
            t.off[c.axis] = -4.0f;
            const Out n = Solve(a.hand, a.shaft, t, both, cm);
            Expect(Near(Sub(n.anchor, zero.anchor), Mul(c.dir, -0.04f)), "a negative offset the other way", (double)c.axis);
            // the offset is a move of the hand: the hand turns with nothing
            Expect(Near(o.axes[0], along) && Near(o.axes[1], across) && Near(o.axes[2], up), "the axes of an offset-only fit", (double)c.axis);
        }
        // the frame is TIED TO THE HAND: roll the hand 90 degrees about the shaft and "up" is where its palm now faces
        for (const float roll : {90.0f, -60.0f, 180.0f}) {
            const Rot r = About(a.shaft.dir, roll);
            Hand h = Turned(a.hand, r);
            h.palm = Add(a.shaft.at, Mul(a.shaft.dir, 0.45f));   // on the axis, so rolling about the shaft through it is a pure roll
            h.wrist = Sub(h.palm, Mul(h.point, 0.045f));
            Tuning t;
            t.off[2] = 3.0f;
            const Out o = Solve(h, a.shaft, t, both, cm);
            const Out z = Solve(h, a.shaft, Tuning{}, both, cm);
            Expect(Near(Sub(o.anchor, z.anchor), Mul(h.out, 0.03f)), "up follows the palm round the shaft", roll);
            Expect(Near(Apply(up, r), h.out), "test setup: the rolled palm's way", roll);
            Tuning u;
            u.off[1] = 3.0f;
            const Out p = Solve(h, a.shaft, u, both, cm);
            Expect(Near(Sub(p.anchor, z.anchor), Mul(h.point, 0.03f)), "across follows the fingers round the shaft", roll);
        }
    }
    // ---- the roll and the tilts ----
    {
        const Rig a = RightOnShaft();
        const V3 along = a.shaft.dir, across = {0, 0, 1}, up = {1, 0, 0};
        // the roll: about the shaft, the palm's middle stays where it is, the way out turns right-handed about the tip's direction
        for (const float deg : {15.0f, 90.0f, -40.0f}) {
            Tuning t;
            t.rot[0] = deg;
            const Out o = Solve(a.hand, a.shaft, t, both, cm);
            const Out z = Solve(a.hand, a.shaft, Tuning{}, both, cm);
            Expect(Near(o.anchor, z.anchor), "a roll keeps the palm where it is", deg);
            Expect(Near(Apply(a.hand.out, o.r), Apply(up, About(along, deg))), "a roll turns the palm about the shaft", deg);
            Expect(Near(Apply(a.hand.knuckles, o.r), a.hand.knuckles), "a roll leaves the knuckle line on the shaft", deg);
            Expect(Orthonormal(o.r), "a roll is a rotation", deg);
        }
        // the tilts: the knuckle line leaves the shaft by exactly the angle, the palm stays at the grasp point
        for (const float deg : {5.0f, 20.0f, -30.0f, 75.0f}) {
            Tuning t;
            t.rot[1] = deg;   // about `across`
            const Out o = Solve(a.hand, a.shaft, t, both, cm);
            Expect(Near(Deg(Apply(a.hand.knuckles, o.r), along), std::fabs(deg), 1e-2f), "a tilt about across leaves the shaft by its angle", deg);
            Expect(Near(Apply(across, o.r), across), "... about its own axis", deg);
            Expect(Near(o.anchor, Solve(a.hand, a.shaft, Tuning{}, both, cm).anchor), "a tilt keeps the palm at the grasp", deg);
            Tuning u;
            u.rot[2] = deg;   // about `up`
            const Out p = Solve(a.hand, a.shaft, u, both, cm);
            Expect(Near(Deg(Apply(a.hand.knuckles, p.r), along), std::fabs(deg), 1e-2f), "a tilt about up leaves the shaft by its angle", deg);
            Expect(Near(Apply(up, p.r), up), "... about its own axis", deg);
            // the two tilts leave the shaft in different planes
            if (std::fabs(deg) > 1.0f) Expect(Deg(Apply(a.hand.knuckles, o.r), Apply(a.hand.knuckles, p.r)) > 0.5f * std::fabs(deg), "the two tilts are different turns", deg);
        }
        // all six at once: still a rigid, proper rotation; the anchor is the grasp plus the offsets
        Tuning all;
        all.off[0] = 1.5f; all.off[1] = -2.0f; all.off[2] = 0.7f;
        all.rot[0] = 25.0f; all.rot[1] = -15.0f; all.rot[2] = 40.0f;
        const Out o = Solve(a.hand, a.shaft, all, both, cm);
        Expect(o.ok && Orthonormal(o.r), "six corrections: a rotation");
        Expect(Near(o.anchor, Add(o.grasp, Add(Add(Mul(along, 0.015f), Mul(across, -0.02f)), Mul(up, 0.007f)))), "the anchor is the grasp plus the offsets");
    }
    // ---- the left hand is the right's mirror image: the same numbers, mirrored geometry ----
    {
        const Rig r = RightOnShaft();
        const Rig l = Mirrored(r);
        Tuning t;
        t.off[0] = 1.2f; t.off[1] = 2.2f; t.off[2] = -1.7f;
        t.rot[0] = 21.0f; t.rot[1] = -13.0f; t.rot[2] = 33.0f;
        const Out a = Solve(r.hand, r.shaft, t, both, cm), b = Solve(l.hand, l.shaft, t, both, cm);
        Expect(a.ok && b.ok, "mirror: both fit");
        const V3 probes[] = {r.hand.palm, r.hand.wrist, Add(r.hand.palm, {0.05f, 0.02f, 0.07f}), {0.1f, 0.2f, 0.3f}};
        for (const V3& p : probes) Expect(Near(Move(b, Mirror(p)), Mirror(Move(a, p)), 2e-4f), "the left hand's fit is the right's, mirrored");
        Expect(Near(b.anchor, Mirror(a.anchor)), "mirror: the anchor");
        // and a left hand with a mirrored roll goes the other way about the shaft
        Tuning roll;
        roll.rot[0] = 30.0f;
        const Out ar = Solve(r.hand, r.shaft, roll, both, cm), br = Solve(l.hand, l.shaft, roll, both, cm);
        Expect(Near(Deg(Apply(r.hand.out, ar.r), r.hand.out), 30.0f, 1e-2f) && Near(Deg(Apply(l.hand.out, br.r), l.hand.out), 30.0f, 1e-2f), "the same roll angle on both hands");
        // about the shaft the right hand turns right-handed (toward tip x palm-out), the left the other way: the mirror of a turn is the reverse turn
        const V3 turnedR = Apply(r.hand.out, ar.r), turnedL = Apply(l.hand.out, br.r);
        Expect(Near(turnedR, Apply(r.hand.out, About(r.shaft.dir, 30.0f))), "the right hand rolls right-handed about the shaft");
        Expect(Near(turnedL, Apply(l.hand.out, About(l.shaft.dir, -30.0f))), "the left hand rolls the other way about the shaft");
    }
    // ---- the way the skeleton hook applies it: the bone's matrix rows turned by r, its position moved as a point - a child bone (the middle knuckle) goes where a point of the
    //      rigid hand goes ----
    {
        const Rig a = RightOnShaft();
        const Hand h = Turned(a.hand, About(Axis(0.3f, 0.5f, 0.8f), 41.0f));
        Tuning t;
        t.off[0] = 1.0f; t.off[1] = -1.5f; t.off[2] = 2.0f;
        t.rot[0] = 12.0f; t.rot[1] = 9.0f; t.rot[2] = -20.0f;
        const Out o = Solve(h, a.shaft, t, both, cm);
        // the wrist bone: rows = its axes (an arbitrary proper rotation), position = the wrist; the middle knuckle hangs 9 cm ahead of it along its own z
        const Rot bone = About(Axis(0.6f, -0.2f, 0.4f), 63.0f);
        const V3 local = {0.0f, 0.0f, 0.09f};
        const V3 knuckleBefore = Add(h.wrist, Apply(local, bone));
        // the hook: row_i' = row_i * r ; the position through the pivot
        const Rot turned = Then(bone, o.r);
        const V3 wristAfter = Add(o.pivot, Add(Apply(Sub(h.wrist, o.pivot), o.r), o.shift));
        const V3 knuckleAfter = Add(wristAfter, Apply(local, turned));
        Expect(Near(knuckleAfter, Add(o.pivot, Add(Apply(Sub(knuckleBefore, o.pivot), o.r), o.shift)), 2e-4f), "a child bone goes where a point of the rigid hand goes");
        Expect(Orthonormal(turned), "the bone's rows stay a rotation");
        // and the palm's middle - halfway wrist -> knuckle - ends at the anchor
        Expect(Near(Add(Mul(Add(wristAfter, knuckleAfter), 0.5f), V3{}), Move(o, Mul(Add(h.wrist, knuckleBefore), 0.5f)), 2e-4f), "the palm moves like the rest");
    }
    // ---- the base grip's relation to the shaft, measured, and given to a hand that takes the weapon: both sit on the shaft the same way ----
    {
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> u(-1.0f, 1.0f);
        auto cmOf = [&](Tuning t) { return InCm(t, cm); };   // the test space is metres: the Relation's offsets in cm
        // a base grip like the game's: the shaft passes a couple of cm from the palm's middle, out of the palm and a little to the fingers' side, the knuckle line 18 degrees off it
        auto baseHand = [&](int side, float rollDeg, float tiltDeg) {
            Rig a = RightOnShaft({0.0f, 0.0f, 0.0f});
            const V3 d = a.shaft.dir;
            Hand h = Turned(a.hand, Then(About({0, 0, 1}, tiltDeg), About(d, rollDeg)));
            h.palm = Add(Add(a.shaft.at, Mul(d, 0.42f)), Add(Mul(h.out, -0.022f), Mul(h.point, -0.008f)));   // the axis passes 2.2 cm out of the palm, 0.8 cm behind the fingers' line
            h.wrist = Sub(h.palm, Mul(h.point, 0.045f));
            Rig out{a.hand, a.shaft};
            out.hand = h;
            if (side == 1) {
                out = Mirrored(out);
            }
            return out;
        };
        for (int side = 0; side < 2; ++side) {
            for (const float roll : {0.0f, 40.0f, -110.0f}) {
                for (const float tilt : {0.0f, 18.0f, -25.0f}) {
                    const Rig b = baseHand(side, roll, tilt);
                    bool ok = false;
                    const Tuning rel = Relation(b.hand, b.shaft, &ok);
                    Expect(ok, "a relation is measured");
                    Expect(Near(rel.off[0], 0.0f), "the relation has no part along the shaft");
                    // the hand carried onto the shaft by its own relation is the hand it was
                    const Out o = Solve(b.hand, b.shaft, cmOf(rel), both, cm);
                    Expect(o.ok, "the relation is applied");
                    Expect(Near(o.anchor, b.hand.palm, 2e-4f), "the base hand, given its own relation, stays where it is", Len(Sub(o.anchor, b.hand.palm)));
                    for (int i = 0; i < 9; ++i) Expect(Near(o.r.m[i], i % 4 == 0 ? 1.0f : 0.0f, 2e-3f), "... and as it is turned", (double)side * 100 + roll + tilt);
                    // another hand: a random roll about the shaft, a random lean, either side - given the relation it sits on the shaft as the base hand does
                    for (int other = 0; other < 2; ++other) {
                        Rig a = RightOnShaft({u(rng) * 0.04f, u(rng) * 0.04f, u(rng) * 0.04f});
                        Hand t = Turned(a.hand, Then(About(Axis(u(rng), u(rng), u(rng)), 85.0f * u(rng)), About(a.shaft.dir, 180.0f * u(rng))));
                        t.palm = Add(Add(a.shaft.at, Mul(a.shaft.dir, 0.9f * (u(rng) * 0.5f + 0.5f))), {u(rng) * 0.03f, u(rng) * 0.03f, u(rng) * 0.03f});
                        t.wrist = Sub(t.palm, Mul(t.point, 0.045f));
                        Rig taker{t, a.shaft};
                        if (other == 1) taker = Mirrored(taker);
                        const Out f = Solve(taker.hand, taker.shaft, cmOf(rel), both, cm);
                        Expect(f.ok, "the taking hand is fitted");
                        // the taking hand as it is drawn now: its vectors turned, its palm at the anchor
                        Hand after = taker.hand;
                        after.point = Apply(after.point, f.r);
                        after.knuckles = Apply(after.knuckles, f.r);
                        after.out = Apply(after.out, f.r);
                        after.palm = f.anchor;
                        after.wrist = Add(f.anchor, Apply(Sub(taker.hand.wrist, taker.hand.palm), f.r));
                        bool ok2 = false;
                        const Tuning got = Relation(after, taker.shaft, &ok2);
                        Expect(ok2, "the fitted hand's relation is measured");
                        const Tuning want = rel;
                        bool same = true;
                        for (int i = 1; i < 3; ++i) same = same && std::fabs(got.off[i] - want.off[i]) < 2e-4f;
                        for (int i = 0; i < 3; ++i) same = same && std::fabs(got.rot[i] - want.rot[i]) < 0.05f;
                        if (!same)
                            std::printf("    base side %d roll %.0f tilt %.0f, taker side %d: off %.4f %.4f vs %.4f %.4f, rot %.2f %.2f %.2f vs %.2f %.2f %.2f\n", side, roll, tilt, other, got.off[1], got.off[2], want.off[1],
                                        want.off[2], got.rot[0], got.rot[1], got.rot[2], want.rot[0], want.rot[1], want.rot[2]);
                        Expect(same, "both hands sit on the shaft the same way");
                    }
                }
            }
        }
        // and the measured numbers are what the base grip was built with: the axis passes 2.2 cm out of the palm and 0.8 cm behind the fingers' line, so the palm stands
        // 2.2 cm behind it (up -2.2) and 0.8 cm behind it across (across -0.8) - on the right hand
        const Rig b = baseHand(0, 0.0f, 0.0f);
        const Tuning rel = InCm(Relation(b.hand, b.shaft), cm);
        Expect(std::fabs(rel.off[1] + 0.8f) < 0.05f && std::fabs(rel.off[2] + 2.2f) < 0.05f, "the offset the base grip has, in cm: across and up", rel.off[1]);
        const Rig lb = baseHand(1, 0.0f, 0.0f);
        const Tuning lrel = InCm(Relation(lb.hand, lb.shaft), cm);
        Expect(std::fabs(lrel.off[1] - rel.off[1]) < 0.05f && std::fabs(lrel.off[2] - rel.off[2]) < 0.05f, "the left hand's mirror image has the same numbers");
    }
    // ---- what is done to a hand on the weapon, in every state it can be in ----
    {
        Tuning user, like;
        user.off[0] = 1.0f; user.off[1] = -2.0f; user.off[2] = 0.5f; user.rot[0] = 10.0f; user.rot[1] = -5.0f; user.rot[2] = 20.0f;
        like.off[1] = 0.8f; like.off[2] = 2.2f; like.rot[1] = 12.0f; like.rot[2] = -18.0f;
        // the base grip (the game's hand, as placed): nothing asked of it = nothing done; the user's corrections alone otherwise, never snapped or aligned
        const Plan quiet = PlanFor(0, Tuning{}, true, &like);
        Expect(!quiet.act, "the base grip with no corrections is left as the game has it");
        const Plan base = PlanFor(0, user, true, &like);
        Expect(base.act && !base.mode.snap && !base.mode.align && !base.likeBase, "the base grip: only the user's corrections, not snapped or aligned");
        for (int i = 0; i < 3; ++i) Expect(base.tune.off[i] == user.off[i] && base.tune.rot[i] == user.rot[i], "... exactly the user's numbers", i);
        // a hand that took it (any hand, carrying or not): snapped and aligned, the base's relation plus the user's corrections
        for (const unsigned grabs : {1u, 2u, 7u}) {
            const Plan taker = PlanFor(grabs, user, true, &like);
            Expect(taker.act && taker.mode.snap && taker.mode.align && taker.likeBase, "a hand that took it is laid on the shaft like the base grip");
            for (int i = 0; i < 3; ++i) Expect(Near(taker.tune.off[i], user.off[i] + like.off[i]) && Near(taker.tune.rot[i], user.rot[i] + like.rot[i]), "... base relation plus its own corrections", i);
            const Plan quiet2 = PlanFor(grabs, Tuning{}, true, &like);
            Expect(quiet2.act && quiet2.likeBase && Near(quiet2.tune.off[2], like.off[2]) && Near(quiet2.tune.rot[2], like.rot[2]), "... with no corrections of its own it is the base's relation");
            const Plan flat = PlanFor(grabs, user, false, &like);
            Expect(flat.act && flat.mode.snap && !flat.likeBase && flat.tune.off[2] == user.off[2], "like-the-base off: the palm on the axis plus its own corrections");
            const Plan unknown = PlanFor(grabs, user, true, nullptr);
            Expect(unknown.act && unknown.mode.snap && !unknown.likeBase && unknown.tune.off[2] == user.off[2], "the base's relation not known yet: the palm on the axis");
        }
    }
    // ---- the grasp point follows where the palm stands along the shaft; the hand's side along the shaft is the shaft's ----
    {
        Rig a = RightOnShaft({0.03f, 0.0f, 0.0f});
        for (const float along : {-0.2f, 0.0f, 0.37f, 0.8f}) {
            a.hand.palm = Add(Add(a.shaft.at, Mul(a.shaft.dir, along)), {0.03f, 0.0f, 0.0f});
            a.hand.wrist = Sub(a.hand.palm, Mul(a.hand.point, 0.045f));
            const Out o = Solve(a.hand, a.shaft, Tuning{}, both, cm);
            Expect(Near(Dot(Sub(o.grasp, a.shaft.at), a.shaft.dir), along), "grasp point along the shaft", along);
        }
        // a tilted shaft too (the shaft is any line)
        Rig t = RightOnShaft();
        t.shaft.dir = {0.3f, 0.9f, -0.2f};
        V3 d;
        Unit(t.shaft.dir, &d);
        t.shaft.dir = d;
        t.hand = Turned(t.hand, Between({0, 1, 0}, d));
        const Out o = Solve(t.hand, t.shaft, Tuning{}, both, cm);
        Expect(o.ok && Near(Len(Sub(o.anchor, Add(t.shaft.at, Mul(d, Dot(Sub(o.anchor, t.shaft.at), d))))), 0.0f), "a leaning shaft: the palm on its axis");
        Expect(Near(Deg(Apply(t.hand.knuckles, o.r), d), 0.0f, 1e-2f), "a leaning shaft: the knuckles along it");
    }
    // ---- degenerate inputs are refused, not NaN ----
    {
        Rig a = RightOnShaft();
        Hand bad = a.hand;
        bad.point = {};
        Expect(!Solve(bad, a.shaft, Tuning{}, both, cm).ok, "no pointing direction: no fit");
        bad = a.hand;
        bad.palm = {NAN, 0, 0};
        Expect(!Solve(bad, a.shaft, Tuning{}, both, cm).ok, "a NaN palm: no fit");
        Shaft s = a.shaft;
        s.dir = {};
        Expect(!Solve(a.hand, s, Tuning{}, both, cm).ok, "no shaft: no fit");
        // the fingers pointing along the shaft (a hand held up the bar): the frame still comes out, orthonormal
        Hand h = a.hand;
        h.point = a.shaft.dir;
        h.knuckles = {1, 0, 0};
        h.out = Mul(Cross(h.point, h.knuckles), -1.0f);
        const Out o = Solve(h, a.shaft, Tuning{}, both, cm);
        Expect(o.ok && Near(Len(o.axes[1]), 1.0f) && Near(Len(o.axes[2]), 1.0f) && Near(Dot(o.axes[1], o.axes[2]), 0.0f, 1e-3f) && Near(Dot(o.axes[1], o.axes[0]), 0.0f, 1e-3f),
               "fingers along the shaft: a frame all the same");
    }
    // ---- the fingers close as the grip tightens, never open while it holds ----
    {
        Expect(Close(-1.0f) == 1.0f, "no squeeze known: a full fist, as before");
        Expect(Near(Close(0.4f), 0.55f) && Near(Close(0.0f), 0.55f) && Near(Close(1.0f), 1.0f), "the ends of the closing");
        float prev = 0.0f;
        bool monotone = true;
        for (int i = 0; i <= 20; ++i) {
            const float c = Close(i / 20.0f);
            if (c < prev - 1e-6f) monotone = false;
            prev = c;
        }
        Expect(monotone, "closing never goes back as the squeeze grows");
        Expect(Close(0.6f) > 0.55f && Close(0.6f) < 1.0f, "a grip just held is between");
    }
    // ---- the whole sphere: the shaft's direction (every axis exactly, the edges between them, a spiral over the sphere), the hand's roll about it, either side, with and without the
    //      base grip's relation and big corrections: the frame is orthonormal and a proper turn comes out - no direction is a degenerate one (the shaft along or across the hand's own
    //      axes, straight up or down, level) ----
    {
        std::vector<V3> dirs = {{0, 1, 0}, {0, -1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};
        for (int i = 0; i < 3; ++i)
            for (float s1 : {-1.0f, 1.0f})
                for (float s2 : {-1.0f, 1.0f}) {
                    float c[3] = {0, 0, 0};
                    c[(i + 1) % 3] = s1;
                    c[(i + 2) % 3] = s2;
                    dirs.push_back(Axis(c[0], c[1], c[2]));
                }
        for (int i = 0; i < 120; ++i) {
            const float y = 1.0f - 2.0f * (i + 0.5f) / 120.0f, r = std::sqrt(1.0f - y * y), a = 2.39996323f * (float)i;
            dirs.push_back({r * std::cos(a), y, r * std::sin(a)});
        }
        Tuning big;
        big.off[0] = 3.0f; big.off[1] = -2.0f; big.off[2] = 1.5f;
        big.rot[0] = 170.0f; big.rot[1] = -80.0f; big.rot[2] = 95.0f;
        const Tuning like = [] { Tuning t; t.off[1] = -4.8f; t.off[2] = -4.7f; t.rot[0] = -1.0f; t.rot[1] = -9.0f; t.rot[2] = 12.0f; return t; }();
        long poses = 0, bad = 0;
        for (const V3& d : dirs)
            for (int side = 0; side < 2; ++side)
                for (const float roll : {0.0f, 90.0f, 180.0f, -37.0f})
                    for (int hold = 0; hold < 3; ++hold) {
                        // a hand whose axes lie in every possible relation to the shaft: its own pointing / knuckle row / palm way along, across and askew to d
                        Rig a = RightOnShaft();
                        Hand h = a.hand;
                        const Rot turnToD = Between({0, 1, 0}, d);   // the knuckle row (+y) along d
                        const Rot lean = hold == 0 ? Rot{} : hold == 1 ? About({0, 0, 1}, 90.0f) : About(Axis(0.3f, 0.5f, 0.8f), 130.0f);   // along / across / askew
                        h = Turned(h, Then(Then(lean, turnToD), About(d, roll)));
                        h.side = side;
                        if (side == 1) { h.out = Mul(h.out, -1.0f); }   // (the left hand's palm way is the other cross)
                        h.palm = Add(Mul(d, 0.4f), {0.02f, -0.01f, 0.03f});
                        h.wrist = Sub(h.palm, Mul(h.point, 0.045f));
                        const Shaft sh{{0, 0, 0}, d};
                        for (int variant = 0; variant < 4; ++variant) {
                            const Plan plan = variant == 0 ? PlanFor(1, Tuning{}, false, nullptr) : variant == 1 ? PlanFor(1, Tuning{}, true, &like) : variant == 2 ? PlanFor(1, big, true, &like) : PlanFor(0, big, true, &like);
                            const Out o = Solve(h, sh, plan.tune, plan.mode, cm);
                            ++poses;
                            bool ok = o.ok && Orthonormal(o.r) && std::isfinite(o.anchor.x + o.anchor.y + o.anchor.z);
                            ok = ok && Near(Len(o.axes[0]), 1.0f, 1e-3f) && Near(Len(o.axes[1]), 1.0f, 1e-3f) && Near(Len(o.axes[2]), 1.0f, 1e-3f);
                            ok = ok && Near(Dot(o.axes[1], o.axes[2]), 0.0f, 2e-3f);   // (across and up turn together: they stay square to each other)
                            const Frame fr = FrameOf(h, sh, plan.mode.align);
                            ok = ok && fr.ok && Near(Len(fr.d), 1.0f, 1e-4f) && Near(Len(fr.across), 1.0f, 1e-3f) && Near(Len(fr.up), 1.0f, 1e-3f) && Near(Dot(fr.d, fr.across), 0.0f, 2e-3f) &&
                                 Near(Dot(fr.d, fr.up), 0.0f, 2e-3f) && Near(Dot(fr.across, fr.up), 0.0f, 2e-3f);
                            bool relOk = false;
                            const Tuning rel = Relation(h, sh, &relOk);
                            ok = ok && relOk && std::isfinite(rel.rot[0] + rel.rot[1] + rel.rot[2] + rel.off[1] + rel.off[2]);
                            if (!ok) {
                                ++bad;
                                if (bad <= 3) std::printf("  sphere: d %.3f %.3f %.3f side %d roll %.0f hold %d variant %d: not a proper fit\n", d.x, d.y, d.z, side, roll, hold, variant);
                            }
                        }
                    }
        std::printf("  the sphere: %ld fits, %ld improper\n", poses, bad);
        Expect(bad == 0, "no direction gives a degenerate fit", (double)bad);
    }
    // ---- AxisOf: how the shaft lies against the hand, in the hand's own frame (what the grip library is given, interface version 2) ----
    {
        std::printf("axis in hand\n");
        const Rig a = RightOnShaft();   // the palm 3 cm out of the palm's way and 2 cm toward the fingers from the axis: the axis is 2 cm toward the wrist and 3 cm behind the palm
        const AxisInHand x = AxisOf(a.hand, a.shaft, 1.0f);
        Expect(x.ok, "the axis is read off a hand");
        Expect(Near(x.line, V3{0.0f, 1.0f, 0.0f}), "the knuckles run along the shaft: the line is the hand's y");
        Expect(Near(x.toAxis, V3{-0.02f, 0.0f, -0.03f}), "the axis from the palm: 2 cm toward the wrist, 3 cm behind the palm", x.toAxis.z);
        // the hand frame's rows are the grip pose's (f, k, n): a cm space gives the same metres
        const Rig big = [&] { Rig r = a; r.shaft.at = Mul(r.shaft.at, 100.0f); r.hand.palm = Mul(r.hand.palm, 100.0f); r.hand.wrist = Mul(r.hand.wrist, 100.0f); return r; }();
        const AxisInHand xb = AxisOf(big.hand, big.shaft, 100.0f);
        Expect(Near(xb.toAxis, x.toAxis, 1e-4f) && Near(xb.line, x.line), "a space of 100 units to the metre gives the same numbers");
        // the mirror image (a left hand with the same grip) has the same numbers: the library is given the same hand on either side
        const Rig m = Mirrored(a);
        const AxisInHand xm = AxisOf(m.hand, m.shaft, 1.0f);
        Expect(xm.ok && Near(xm.toAxis, x.toAxis) && Near(xm.line, x.line), "a hand and its mirror image have the same numbers");
        // the same grip anywhere and turned any way: the numbers are the hand's
        {
            const Rot r = Then(About(Axis(0.3f, 0.8f, -0.5f), 71.0f), About(Axis(1.0f, 0.0f, 0.2f), -33.0f));
            Rig t = a;
            t.shaft.at = Apply(t.shaft.at, r);
            t.shaft.dir = Apply(t.shaft.dir, r);
            t.hand = Turned(t.hand, r);
            t.hand.palm = Apply(a.hand.palm, r);
            t.hand.wrist = Apply(a.hand.wrist, r);
            const AxisInHand xt = AxisOf(t.hand, t.shaft, 1.0f);
            Expect(xt.ok && Near(xt.toAxis, x.toAxis, 2e-4f) && Near(xt.line, x.line, 2e-4f), "the numbers do not depend on where the hand and the shaft are in the world", Len(Sub(xt.toAxis, x.toAxis)));
        }
        // a lean: the hand turned 26 degrees about its palm's normal (the knuckles 26 degrees off the shaft, as the game's grip has them): the line leans by that much and the vector is
        // square to it
        {
            Rig l = a;
            l.hand = Turned(l.hand, About(l.hand.out, 26.0f));
            const AxisInHand xl = AxisOf(l.hand, l.shaft, 1.0f);
            Expect(xl.ok && Near(Len(xl.line), 1.0f), "the line is a unit vector");
            Expect(Near(std::fabs(xl.line.y), std::cos(26.0f * 0.0174532925f), 1e-3f) && Near(xl.line.z, 0.0f, 1e-3f), "the line leans 26 degrees in the palm's plane", xl.line.y);
            Expect(Near(Dot(xl.line, xl.toAxis), 0.0f, 1e-4f), "the vector to the axis is square to the line", Dot(xl.line, xl.toAxis));
            Expect(Near(Len(xl.toAxis), Len(x.toAxis), 1e-4f), "and as long as the palm is from the axis", Len(xl.toAxis));
        }
        // with the knuckles along the shaft it is what the grip fit's Relation says (its offsets across and up, with the sign of the other way: the Relation is the palm from the axis)
        {
            bool ok = false;
            const Tuning rel = Relation(a.hand, a.shaft, &ok);
            Expect(ok && Near(x.toAxis.x, -rel.off[1]) && Near(x.toAxis.z, -rel.off[2]), "the same offsets as the grip fit's Relation", x.toAxis.x + rel.off[1]);
        }
        // what is no hand or no shaft is refused
        {
            Rig bad = a;
            Expect(!AxisOf(bad.hand, bad.shaft, 0.0f).ok, "no scale, no numbers");
            bad.shaft.dir = {0, 0, 0};
            Expect(!AxisOf(bad.hand, bad.shaft, 1.0f).ok, "a shaft with no direction");
            bad = a;
            bad.hand.palm.x = std::nanf("");
            Expect(!AxisOf(bad.hand, bad.shaft, 1.0f).ok, "a palm that is not a number");
            bad = a;
            bad.hand.knuckles = bad.hand.point;
            Expect(!AxisOf(bad.hand, bad.shaft, 1.0f).ok, "knuckles along the fingers give no frame");
        }
    }
    // ---- WithCorrections: the user's corrections on a hand the grip library has laid (same keys, same frame, same signs as for ours) ----
    {
        std::printf("corrections on a laid hand\n");
        const Rig a = RightOnShaft();
        const float cm = 0.01f;
        Tuning user;
        user.off[0] = 4.0f; user.off[1] = -2.5f; user.off[2] = 1.5f;
        user.rot[0] = 25.0f; user.rot[1] = -12.0f; user.rot[2] = 18.0f;
        // (a) nothing to correct: the laid hand itself
        const Out snapped = Solve(a.hand, a.shaft, Tuning{}, Mode{true, true}, cm);
        const Out same = WithCorrections(a.hand, a.shaft, snapped, Tuning{}, cm);
        Expect(same.ok && Near(same.shift, snapped.shift, 0.0f) && std::memcmp(same.r.m, snapped.r.m, sizeof same.r.m) == 0, "no corrections: the laid hand is left as it is");
        // (b) the corrections after a hand laid snapped and aligned are what Solve gives with them in the same call - for every point of the hand
        const Out viaSolve = Solve(a.hand, a.shaft, user, Mode{true, true}, cm);
        const Out viaLaid = WithCorrections(a.hand, a.shaft, snapped, user, cm);
        Expect(viaLaid.ok, "the corrections apply");
        bool same2 = true;
        for (const V3 p : {a.hand.palm, a.hand.wrist, Add(a.hand.palm, Mul(a.hand.point, 0.09f)), Add(a.hand.palm, Mul(a.hand.knuckles, 0.04f)), Add(a.hand.palm, Mul(a.hand.out, 0.03f))})
            same2 = same2 && Near(Move(viaLaid, p), Move(viaSolve, p), 2e-4f);
        Expect(same2, "laid by the library's rule and corrected = laid and corrected in one go (a value means the same in both models)");
        Expect(Near(viaLaid.anchor, viaSolve.anchor, 2e-4f), "and the palm's middle ends in the same place", Len(Sub(viaLaid.anchor, viaSolve.anchor)));
        // (c) a left hand: the mirror image gets the mirror image (the same numbers)
        {
            const Rig m = Mirrored(a);
            const Out sm = Solve(m.hand, m.shaft, Tuning{}, Mode{true, true}, cm);
            const Out vm = WithCorrections(m.hand, m.shaft, sm, user, cm);
            const V3 p = Add(a.hand.palm, Mul(a.hand.point, 0.09f)), pm = Mirror(p);
            Expect(vm.ok && Near(Move(vm, pm), Mirror(Move(viaLaid, p)), 2e-4f), "the mirror image of a hand with the same numbers is the mirror image of the corrected hand", Len(Sub(Move(vm, pm), Mirror(Move(viaLaid, p)))));
        }
        // (d) each correction does what it says, on a hand the library left a little off the snapped place (a lean of 26 degrees, the palm 2 cm off the axis)
        {
            Rig l = a;
            l.hand = Turned(l.hand, About(l.hand.out, 26.0f));
            const Out lay = Solve(l.hand, l.shaft, Tuning{}, Mode{false, false}, cm);   // (the library's answer here: where the hand stands)
            const V3 palm0 = lay.anchor;
            V3 d;
            Unit(l.shaft.dir, &d);
            Tuning t;
            t.off[0] = 5.0f;
            const Out along = WithCorrections(l.hand, l.shaft, lay, t, cm);
            Expect(Near(Sub(along.anchor, palm0), Mul(d, 0.05f), 1e-4f), "X moves the hand 5 cm along the shaft");
            t = Tuning{};
            t.off[2] = -3.0f;
            const Out up = WithCorrections(l.hand, l.shaft, lay, t, cm);
            const Frame fr = FrameOf(l.hand, l.shaft, false);
            Expect(Near(Sub(up.anchor, palm0), Mul(fr.up, -0.03f), 1e-4f), "Z moves the hand 3 cm along 'up' (out of the palm)");
            t = Tuning{};
            t.rot[0] = 90.0f;
            const Out roll = WithCorrections(l.hand, l.shaft, lay, t, cm);
            const V3 grasp = fr.grasp, r0 = Sub(palm0, grasp), r1 = Sub(roll.anchor, grasp);
            Expect(Near(Len(r1), Len(r0), 1e-4f) && Near(Dot(r1, d), Dot(r0, d), 1e-4f) && Near(std::fabs(Deg(r0, r1)), 90.0f, 0.1f), "the roll turns the palm 90 degrees about the shaft's axis", Deg(r0, r1));
        }
    }
    if (g_failures == 0) std::printf("grip_fit_test: all passed\n");
    return g_failures == 0 ? 0 : 1;
}
