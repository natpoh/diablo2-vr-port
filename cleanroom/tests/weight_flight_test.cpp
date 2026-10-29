// Offline checks of where a thrown weapon lands (vr/weight_flight.h): the flight over the ground, the reach scale and the cap. No game, no headset.
// Run: build/Release/weight_flight_test.exe   (exit code 0 = all passed)

#include "../../vr/weight_flight.h"

#include <cmath>
#include <cstdio>

using namespace weightflight;

static int g_failures = 0;
static void Expect(bool ok, const char* what, double value = 0.0) {
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s (%g)\n", what, value);
    }
}
static bool Near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }
static double Dist(const Out& o) { return std::sqrt(o.landing[0] * o.landing[0] + o.landing[2] * o.landing[2]); }

static In Throw(double x, double y, double z, double vx, double vy, double vz, double floorY = 0.0) {
    In in;
    in.pos[0] = x; in.pos[1] = y; in.pos[2] = z;
    in.vel[0] = vx; in.vel[1] = vy; in.vel[2] = vz;
    in.floorY = floorY;
    return in;
}

int main() {
    const double g = kGravity;
    // a body at no speed (no throw): it drops straight down
    {
        const Out o = Simulate(Throw(0.2, 1.5, -0.4, 0, 0, 0));
        Expect(o.valid && Near(o.flightS, std::sqrt(2.0 * 1.5 / g), 1e-9), "drop: the time of a fall from 1.5 m", o.flightS);
        Expect(Near(o.landing[0], 0.2, 1e-12) && Near(o.landing[2], -0.4, 1e-12) && Near(o.landing[1], 0.0, 1e-12), "drop: straight under where it was let go");
        Expect(o.speed == 0.0 && o.elevationDeg == 0.0, "drop: no speed, no elevation");
    }
    // no gravity: a straight line; down it comes to the floor, level or up it never does
    {
        In in = Throw(0, 1.0, 0, 0, -2.0, -4.0);
        in.gravity = 0.0;
        const Out o = Simulate(in);
        Expect(o.valid && Near(o.flightS, 0.5, 1e-9) && Near(o.landing[2], -2.0, 1e-9), "no gravity, going down: the straight line to the floor", o.flightS);
        in = Throw(0, 1.0, 0, 0, 0.0, -4.0);
        in.gravity = 0.0;
        Expect(!Simulate(in).valid, "no gravity, level: it never comes down");
        in = Throw(0, 1.0, 0, 0, 1.0, -4.0);
        in.gravity = 0.0;
        Expect(!Simulate(in).valid, "no gravity, going up: it never comes down");
    }
    // horizontal: d = v t, t = sqrt(2 h / g)
    {
        const Out o = Simulate(Throw(0, 1.5, 0, 0, 0, -10.0));
        Expect(o.valid && Near(o.landing[2], -10.0 * std::sqrt(2.0 * 1.5 / g), 1e-9), "horizontal throw from 1.5 m at 10 m/s", o.landing[2]);
        Expect(Near(o.elevationDeg, 0.0, 1e-9) && Near(o.speed, 10.0, 1e-9), "horizontal: no elevation, the speed");
    }
    // 45 degrees from the floor's own height: the range v^2 / g, the flight 2 vy / g; and from a hand 1.5 m up, farther
    {
        const double v = 10.0, c = v / std::sqrt(2.0);
        const Out o = Simulate(Throw(0, 0.0, 0, 0, c, -c));
        Expect(o.valid && Near(-o.landing[2], v * v / g, 1e-9), "45 degrees from the floor: the range v^2/g", o.landing[2]);
        Expect(Near(o.flightS, 2.0 * c / g, 1e-9), "45 degrees from the floor: the time 2 vy / g", o.flightS);
        Expect(Near(o.elevationDeg, 45.0, 1e-9), "45 degrees: the elevation", o.elevationDeg);
        const Out hi = Simulate(Throw(0, 1.5, 0, 0, c, -c));
        Expect(hi.valid && -hi.landing[2] > -o.landing[2] + 1.0, "45 degrees from a hand 1.5 m up: farther", -hi.landing[2]);
        const Out down = Simulate(Throw(0, 1.5, 0, 0, -c, -c));
        Expect(down.valid && Near(down.elevationDeg, -45.0, 1e-9) && -down.landing[2] < -hi.landing[2], "thrown down at 45 degrees: shorter, elevation -45");
    }
    // the floor's height: a higher floor is nearer, a lower one farther
    {
        const Out flat = Simulate(Throw(0, 1.4, 0, 0, 0, -8.0, 0.0));
        const Out high = Simulate(Throw(0, 1.4, 0, 0, 0, -8.0, 0.3));   // the ground 30 cm higher (the old "30 cm above the ground")
        const Out low = Simulate(Throw(0, 1.4, 0, 0, 0, -8.0, -0.5));
        Expect(flat.valid && high.valid && low.valid, "floors: all valid");
        Expect(-high.landing[2] < -flat.landing[2] && -low.landing[2] > -flat.landing[2], "a higher floor is nearer, a lower one farther");
        Expect(Near(high.landing[1], 0.3, 1e-12) && Near(low.landing[1], -0.5, 1e-12), "the landing is at the floor's height");
        Expect(Near(-high.landing[2], 8.0 * std::sqrt(2.0 * 1.1 / g), 1e-9), "the time from 1.1 m over the floor", high.landing[2]);
        // already on / under the floor: where it is, at once
        const Out under = Simulate(Throw(1.0, 0.1, 2.0, 5.0, 0.0, 5.0, 0.2));
        Expect(under.valid && under.flightS == 0.0 && Near(under.landing[0], 1.0, 1e-12) && Near(under.landing[2], 2.0, 1e-12), "under the floor: lands where it is");
    }
    // heavy vs light: the same swing - the weight model gives the heavy weapon a lower release velocity (cleanroom/heft, heft_test); the flight is plain
    // ballistics, so the lower velocity is the shorter throw
    {
        const Out light = Simulate(Throw(0, 1.4, 0, 0, 0.5, -6.0)), heavy = Simulate(Throw(0, 1.4, 0, 0, 0.3, -3.5));
        Expect(light.valid && heavy.valid && -light.landing[2] > 1.4 * -heavy.landing[2], "light and fast goes far, heavy goes short");
    }
    // the weapon's weight against the arm's push: the same swing, a heavy weapon leaves slower and so lands nearer
    {
        Expect(Near(MassFactor(kRefKg), 1.0, 1e-12), "the reference weapon keeps its speed");
        Expect(MassFactor(0.3) > 1.0 && MassFactor(0.3) < 1.15, "a throwing knife is a little faster than the reference", MassFactor(0.3));
        Expect(Near(MassFactor(0.62), 0.9615, 0.01) && Near(MassFactor(1.2), 0.811, 0.005), "a javelin ~0.97, a sword ~0.81");
        Expect(Near(MassFactor(3.1), 0.536, 0.005) && Near(MassFactor(3.5), 0.5, 1e-12), "a two-handed axe ~0.54, a polearm 0.5", MassFactor(3.5));
        Expect(MassFactor(-1.0) == 1.0 && MassFactor(std::nan("")) == 1.0, "no mass known: none applied");
        const In swing = Throw(0, 1.4, 0, 0, 0.5, -6.0);
        const Out knife = SimulateThrow(swing, 100.0, 0.0, 0.3), jav = SimulateThrow(swing, 100.0, 0.0, 0.62), sword = SimulateThrow(swing, 100.0, 0.0, 1.2),
                  axe = SimulateThrow(swing, 100.0, 0.0, 3.1), pole = SimulateThrow(swing, 100.0, 0.0, 3.5), none = SimulateThrow(swing, 100.0, 0.0);
        Expect(knife.valid && jav.valid && sword.valid && axe.valid && pole.valid && none.valid, "mass: all valid");
        Expect(-knife.landing[2] > -jav.landing[2] && -jav.landing[2] > -sword.landing[2] && -sword.landing[2] > -axe.landing[2] && -axe.landing[2] > -pole.landing[2],
               "the same swing: the heavier, the nearer", -pole.landing[2]);
        Expect(-pole.landing[2] < 0.6 * -jav.landing[2], "a polearm lands well short of a javelin on the same swing", -pole.landing[2]);
        Expect(Near(none.massFactor, 1.0, 1e-12) && Near(pole.massFactor, 0.5, 1e-12), "the factor is reported");
        // the whole push is scaled, so the vertical part too: a heavy weapon thrown up goes lower
        Expect(Near(pole.vel[1], 0.5 * 0.5, 1e-12), "the vertical speed is scaled by the push", pole.vel[1]);
        // the mass and the reach together, and the cap after both
        const In hard = Throw(0, 1.4, 0, 0, 0.0, -9.0);
        const double capM = CapMetres(48.0, 3.64);
        const Out lightHard = SimulateThrow(hard, 800.0, capM, 0.62), heavyHard = SimulateThrow(hard, 800.0, capM, 3.5);
        Expect(lightHard.capped && Near(Dist(lightHard), capM, 1e-6), "a hard light throw reaches the cap", Dist(lightHard));
        Expect(heavyHard.valid && Dist(heavyHard) < Dist(lightHard), "a hard heavy throw does not (or less)", Dist(heavyHard));
    }
    // the reach scale: the HORIZONTAL part x pct, the flight time stays
    {
        const In in = Throw(0.3, 1.4, -0.5, 0.0, 0.5, -4.0);
        const Out real = SimulateThrow(in, 100.0, 0.0), x8 = SimulateThrow(in, 800.0, 0.0), half = SimulateThrow(in, 50.0, 0.0);
        Expect(real.valid && x8.valid && half.valid, "reach: all valid");
        Expect(Near(x8.flightS, real.flightS, 1e-12) && Near(half.flightS, real.flightS, 1e-12), "reach: the flight time is the real one", x8.flightS);
        Expect(Near(x8.landing[2] - in.pos[2], 8.0 * (real.landing[2] - in.pos[2]), 1e-9), "reach 800 %: eight times as far from the hand", x8.landing[2]);
        Expect(Near(half.landing[2] - in.pos[2], 0.5 * (real.landing[2] - in.pos[2]), 1e-9), "reach 50 %: half as far");
        Expect(Near(x8.vel[1], in.vel[1], 1e-12) && Near(x8.vel[2], 8.0 * in.vel[2], 1e-12) && Near(x8.hScale, 8.0, 1e-12), "reach: the vertical speed is real, the horizontal x8");
        Expect(!x8.capped && Near(x8.landing[0], in.pos[0], 1e-12), "reach: no cap asked, none applied; no side speed, no side move");
        // elevation reflects the velocity the flight uses
        Expect(x8.elevationDeg < real.elevationDeg, "reach: a scaled flight is flatter");
        // a gentle swing (4 m/s) at the default 800 % from a hand 1.2 m up lands about 15-20 m; a lazy 2 m/s about 8-10 m
        const Out easy = SimulateThrow(Throw(0, 1.2, 0, 0, 0.0, -4.0), 800.0, 0.0);
        Expect(easy.valid && -easy.landing[2] > 14.0 && -easy.landing[2] < 20.0, "a comfortable 4 m/s at 800 %: about 15-20 m", -easy.landing[2]);
        const Out lazy = SimulateThrow(Throw(0, 1.2, 0, 0, 0.0, -2.0), 800.0, 0.0);
        Expect(lazy.valid && -lazy.landing[2] > 7.0 && -lazy.landing[2] < 10.0, "a lazy 2 m/s at 800 %: about 8-10 m", -lazy.landing[2]);
    }
    // the cap: the landing never farther than capM from the origin; the direction kept; the flight time kept
    {
        const double capM = CapMetres(48.0, 3.64);
        Expect(Near(capM, 48.0 * 2.0 / 3.64, 1e-12) && Near(capM, 26.37, 0.01), "48 cells at 3.64 units per metre: about 26.4 m", capM);
        const In in = Throw(0.2, 1.4, -0.4, 3.0, 0.0, -9.0);   // a hard swing at 800 %: far past the cap, a little to the right
        const Out free = SimulateThrow(in, 800.0, 0.0), cap = SimulateThrow(in, 800.0, capM);
        Expect(free.valid && Dist(free) > capM + 5.0, "cap: uncapped it goes well past", Dist(free));
        Expect(cap.valid && cap.capped && Near(Dist(cap), capM, 1e-6), "cap: held to the cap", Dist(cap));
        Expect(Near(cap.flightS, free.flightS, 1e-12), "cap: the same flight time (slowed, not shortened)");
        Expect(cap.hScale < free.hScale && cap.hScale > 0.0, "cap: the horizontal scale is reduced", cap.hScale);
        // the direction of the throw is kept: the horizontal displacement from the hand is along the same line
        const double ax = free.landing[0] - in.pos[0], az = free.landing[2] - in.pos[2], bx = cap.landing[0] - in.pos[0], bz = cap.landing[2] - in.pos[2];
        Expect(Near(ax * bz - az * bx, 0.0, 1e-6) && ax * bx + az * bz > 0.0, "cap: the direction of the throw is kept");
        // inside the cap: untouched
        const Out near = SimulateThrow(Throw(0, 1.4, 0, 0, 0.0, -2.0), 800.0, capM);
        Expect(near.valid && !near.capped && Dist(near) < capM, "cap: a throw inside it is not touched", Dist(near));
        // no cap known (capM 0), a cap of none: nothing held
        Expect(!SimulateThrow(in, 800.0, 0.0).capped && !SimulateThrow(in, 800.0, -1.0).capped, "cap: none asked, none applied");
        // the units: more world units per metre = fewer metres to the same 48 cells
        Expect(CapMetres(48.0, 5.0) < CapMetres(48.0, 3.64) && Near(CapMetres(48.0, 5.0), 19.2, 1e-9), "cap: 48 cells at 5 units per metre = 19.2 m");
        Expect(CapMetres(10.0, 3.64) < capM, "cap: ten cells are fewer metres");
        Expect(CapMetres(0.0, 3.64) == 0.0 && CapMetres(48.0, 0.0) == 0.0 && CapMetres(48.0, std::nan("")) == 0.0, "cap: no cells or no scale = no cap");
    }
    // not numbers to act on
    {
        Expect(!Simulate(Throw(0, std::nan(""), 0, 0, 0, 0)).valid, "NaN position");
        Expect(!Simulate(Throw(0, 1, 0, std::nan(""), 0, 0)).valid, "NaN velocity");
        Expect(!SimulateThrow(Throw(0, 1.4, 0, 0, 0, -61.0), 100.0, 0.0).valid, "a release velocity of 61 m/s is a tracking jump, not a throw");
        Expect(SimulateThrow(Throw(0, 1.4, 0, 0, 0, -59.0), 100.0, 0.0).valid, "59 m/s is a throw");
        Expect(!Simulate(Throw(0, 1.0, 0, 0, 500.0, 0)).valid, "thrown straight up at 500 m/s: no flight to speak of");
        In bad = Throw(0, 1.0, 0, 0, 0, -3.0);
        bad.gravity = -1.0;
        Expect(!Simulate(bad).valid, "a negative gravity");
        Expect(SimulateThrow(Throw(0, 1.4, 0, 0, 0, -4.0), std::nan(""), 0.0).valid, "no reach number: the real throw");
    }
    if (g_failures) {
        std::printf("%d FAILED\n", g_failures);
        return 1;
    }
    std::printf("weight_flight_test: all passed\n");
    return 0;
}
