// Offline checks of a potion bottle let go of (vr/bottle_fall.h): it falls with its weight, bounces, rolls and comes to rest on the floor, and the hand's
// motion at the release. No game, no headset.
// Run: build/Release/bottle_fall_test.exe   (exit code 0 = all passed)

#include "../../vr/bottle_fall.h"

#include <cmath>
#include <cstdio>

using namespace bottlefall;

static int g_failures = 0, g_checks = 0;
static void Expect(bool ok, const char* what, double value = 0.0) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s (%g)\n", what, value);
    }
}

static const double kUp[4] = {0, 0, 0, 1};

// A quaternion turning `deg` degrees about x.
static void AboutX(double deg, double q[4]) {
    const double h = deg * 3.14159265358979 / 360.0;
    q[0] = std::sin(h); q[1] = 0; q[2] = 0; q[3] = std::cos(h);
}

// The bottle's up axis (its own +y) in the frame.
static double UpY(const Sim& s) {
    const double y[3] = {0, 1, 0};
    double o[3];
    detail::Rot(s.body.q, y, o);
    return o[1];
}

// Steps at 90 Hz until it rests (or `seconds` pass); the time it took.
static double Run(Sim& s, const Params& p, double floorY, double seconds, unsigned* events = nullptr, double* highest = nullptr) {
    double t = 0;
    unsigned ev = 0;
    while (t < seconds && s.phase != Phase::Rest) {
        ev |= Step(s, p, floorY, 1.0 / 90.0);
        t += 1.0 / 90.0;
        if (highest && s.touched) *highest = std::fmax(*highest, s.body.p[1]);
    }
    if (events) *events = ev;
    return t;
}

int main() {
    const Shape flask{0.04, 0.12, 0.35};
    const Params prm;
    const double zero[3] = {0, 0, 0};

    {   // upright from a metre: lands on its base, rests standing, on the floor
        const double base[3] = {0, 1.0, 0};
        Sim s = Release(flask, base, kUp, zero, zero);
        Expect(s.valid, "upright release valid");
        unsigned ev = 0;
        const double t = Run(s, prm, 0.0, 5.0, &ev);
        Expect(s.phase == Phase::Rest, "upright drop comes to rest", t);
        Expect(t < 3.0, "upright drop rests within 3 s", t);
        Expect((ev & kTouchedFirst) != 0, "upright drop: the first touch is told");
        Expect(std::fabs(Lowest(s)) < 0.002, "upright drop: on the floor, not in it, not over it", Lowest(s));
        Expect(UpY(s) > 0.95, "upright drop: still standing", UpY(s));
        double b[3];
        Base(s, b);
        Expect(std::fabs(b[1]) < 0.003 && std::fabs(b[0]) < 0.05 && std::fabs(b[2]) < 0.05, "upright drop: its base where it fell", b[1]);
    }
    {   // on its side from a metre: lies, the axis level, its middle a radius over the floor
        double q[4];
        AboutX(90, q);
        const double base[3] = {0, 1.0, 0};
        Sim s = Release(flask, base, q, zero, zero);
        const double t = Run(s, prm, 0.0, 6.0);
        Expect(s.phase == Phase::Rest, "side drop comes to rest", t);
        Expect(std::fabs(UpY(s)) < 0.15, "side drop: lies", UpY(s));
        Expect(std::fabs(s.body.p[1] - flask.radius) < 0.006, "side drop: its middle a radius over the floor", s.body.p[1]);
    }
    {   // thrown level at 3 m/s from 1.2 m: first touch about where the ballistics says, then still; no energy from nowhere
        double q[4];
        AboutX(70, q);
        const double base[3] = {0, 1.2, 0};
        const double vel[3] = {3.0, 0, 0};
        Sim s = Release(flask, base, q, vel, zero);
        double x0 = 0;
        bool told = false;
        double t = 0, highest = -1;
        while (t < 8.0 && s.phase != Phase::Rest) {
            const unsigned ev = Step(s, prm, 0.0, 1.0 / 90.0);
            t += 1.0 / 90.0;
            if (ev & kTouchedFirst) { Expect(!told, "the first touch told once"); told = true; x0 = s.body.p[0]; }
            if (s.touched) highest = std::fmax(highest, s.body.p[1]);
        }
        const double fall = std::sqrt(2.0 * 1.1 / kGravity) * 3.0;   // the centre ~1.1 m over the floor when its rim meets it
        Expect(told, "throw: the first touch told");
        Expect(std::fabs(x0 - fall) < 0.25 * fall, "throw: lands where ballistics says", x0);
        Expect(s.phase == Phase::Rest, "throw: comes to rest", t);
        Expect(highest < 1.0, "throw: no bounce higher than it fell", highest);
        Expect(s.body.p[0] > x0 - 0.05, "throw: slides / rolls on, not back", s.body.p[0] - x0);
        Expect(std::fabs(Lowest(s)) < 0.003, "throw: at rest on the floor", Lowest(s));
        Expect(s.bounces >= 1, "throw: a hard landing bounces", s.bounces);
    }
    {   // tilted 30 degrees from half a metre with a spin: ends still, on the floor, standing or lying (not balanced on a rim)
        double q[4];
        AboutX(30, q);
        const double base[3] = {0.3, 0.5, -0.2};
        const double spin[3] = {0, 6.0, 2.0};
        Sim s = Release(flask, base, q, zero, spin);
        const double t = Run(s, prm, 0.0, 8.0);
        Expect(s.phase == Phase::Rest, "tilted spin drop rests", t);
        Expect(std::fabs(Lowest(s)) < 0.003, "tilted: on the floor", Lowest(s));
        const double u = std::fabs(UpY(s));
        Expect(u > 0.93 || u < 0.2, "tilted: standing or lying, not on a rim", u);
    }
    {   // at rest it stays at rest
        const double base[3] = {0, 0.3, 0};
        Sim s = Release(flask, base, kUp, zero, zero);
        Run(s, prm, 0.0, 5.0);
        const double p[3] = {s.body.p[0], s.body.p[1], s.body.p[2]};
        for (int i = 0; i < 90; ++i) Step(s, prm, 0.0, 1.0 / 90.0);
        Expect(std::fabs(s.body.p[0] - p[0]) + std::fabs(s.body.p[1] - p[1]) + std::fabs(s.body.p[2] - p[2]) < 1e-9, "a bottle at rest does not move");
    }
    {   // a floor at another height (the hero's ground is not 0), and a heavy flask falls as fast as a light vial
        const Shape vial{0.02, 0.1, 0.12};
        const double base[3] = {5, 13.0, -2};
        Sim a = Release(flask, base, kUp, zero, zero), b = Release(vial, base, kUp, zero, zero);
        double ta = 0, tb = 0;
        while (!a.touched && ta < 5) { Step(a, prm, 12.0, 1.0 / 90.0); ta += 1.0 / 90.0; }
        while (!b.touched && tb < 5) { Step(b, prm, 12.0, 1.0 / 90.0); tb += 1.0 / 90.0; }
        Expect(std::fabs(ta - tb) < 0.02, "a flask and a vial land together (gravity knows no mass)", ta - tb);
        Expect(std::fabs(ta - std::sqrt(2.0 / kGravity)) < 0.03, "a metre's fall takes 0.45 s", ta);
        Run(a, prm, 12.0, 5.0);
        Expect(std::fabs(Lowest(a) - 12.0) < 0.003, "rests on the floor at its height", Lowest(a));
    }
    {   // a thrown bottle from the floor's height going down is caught at the floor, not under it, even fast
        const double base[3] = {0, 0.05, 0};
        const double vel[3] = {0, -11.0, 0};
        Sim s = Release(flask, base, kUp, vel, zero);
        Step(s, prm, 0.0, 1.0 / 30.0);
        Expect(Lowest(s) > -0.01, "a fast fall is not through the floor", Lowest(s));
    }
    {   // what is not a bottle, or not a number, is no release; a release too fast is held
        const double base[3] = {0, 1, 0};
        const double nanv[3] = {NAN, 0, 0};
        Expect(!Release(Shape{0, 0.1, 0.3}, base, kUp, zero, zero).valid, "no radius: no bottle");
        Expect(!Release(Shape{0.04, 0.12, 0}, base, kUp, zero, zero).valid, "no mass: no bottle");
        Expect(!Release(flask, base, kUp, nanv, zero).valid, "NaN velocity: no release");
        const double fast[3] = {100, 0, 0};
        const Sim s = Release(flask, base, kUp, fast, zero);
        Expect(s.valid && std::fabs(s.body.v[0] - prm.maxSpeed) < 1e-9, "a tracking jump is held to the most a hand throws", s.body.v[0]);
        Sim dead;
        Expect(Step(dead, prm, 0.0, 0.01) == 0, "an invalid bottle does not step");
    }
    {   // the hand's motion: a hand moving at a steady (1, 2, -0.5) m/s and turning 3 rad/s about y
        Tracker tr;
        for (int i = 0; i < 20; ++i) {
            const double t = i / 90.0;
            const double p[3] = {1.0 * t, 2.0 * t, -0.5 * t};
            const double a = 3.0 * t / 2.0;
            const double q[4] = {0, std::sin(a), 0, std::cos(a)};
            tr.Add(t, p, q);
        }
        double v[3], w[3];
        const bool ok = tr.Motion(19 / 90.0, 0.08, v, w);
        Expect(ok, "the hand's motion is known");
        Expect(std::fabs(v[0] - 1.0) < 1e-6 && std::fabs(v[1] - 2.0) < 1e-6 && std::fabs(v[2] + 0.5) < 1e-6, "its velocity", v[0]);
        Expect(std::fabs(w[1] - 3.0) < 1e-3 && std::fabs(w[0]) < 1e-6 && std::fabs(w[2]) < 1e-6, "its spin", w[1]);
        double v2[3], w2[3];
        Expect(!tr.Motion(5.0, 0.08, v2, w2) && v2[0] == 0, "poses long gone: no motion (it just drops)");
        Tracker one;
        one.Add(0, zero, kUp);
        one.Add(0, zero, kUp);
        Expect(!one.Motion(0, 0.1, v2, w2), "one pose: no motion");
    }
    {   // a jittery hand: one pose 2 cm off does not throw the bottle
        Tracker tr;
        for (int i = 0; i < 10; ++i) {
            double p[3] = {0, 1, 0};
            if (i == 9) p[0] = 0.02;
            tr.Add(i / 90.0, p, kUp);
        }
        double v[3], w[3];
        tr.Motion(9 / 90.0, 0.1, v, w);
        Expect(std::fabs(v[0]) < 0.6, "one jittery pose moves the estimate a little only", v[0]);
    }

    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
