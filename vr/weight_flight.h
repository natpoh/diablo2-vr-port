// weight_flight.h - where a thrown weapon lands: the flight of a body let go with a position and a velocity, over the real ground. Pure.
//
// The weight model (cleanroom/heft, vr/weight.cpp) knows what the throw wire's estimate from the hand's raw velocity cannot: the weapon's mass, the
// velocity the weapon really has at the release (the hand moved it through muscles with ceilings: a heavy weapon lags and leaves slower than a light one
// for the same swing), the release moment and the real floor height. The flight from there is plain ballistics: gravity only, no air.
//
// Frame and units: any right-handed frame with y up, metres and seconds (the room's axes in the weight model; the head frame - x right, y up, z back,
// metres from the head - is the one the results are published in). The floor is a height (y), flat.
//
// A real arm throws a few metres; a game screen wants tens. SimulateThrow scales the HORIZONTAL part of the release velocity by [weight] throw_reach_pct
// (the vertical part, and so the flight time, stays real: gravity is the earth's), then holds the landing to the game's own limit: the command check
// refuses an aim point more than 50 cells off the hero on an axis (never bypassed), so [weight] throw_max_cells (48) of them at most, a cell being two
// world units: capM = cells * 2 / unitsPerMetre. The frame's origin (x = z = 0) is the hero - the head. A capped landing keeps its direction; the flight
// is slowed to land there (the same flight time).
#pragma once

#include <algorithm>
#include <cmath>

namespace weightflight {

constexpr double kGravity = 9.81;     // m/s^2, downward
constexpr double kMaxFlightS = 20.0;  // a flight longer than this (thrown straight up at 100 m/s, or never coming down) is no throw
constexpr double kMaxSpeed = 60.0;    // m/s: a release velocity above this is a tracking jump, not a throw

struct In {
    double pos[3] = {0, 0, 0};   // where the body is at the release
    double vel[3] = {0, 0, 0};   // its velocity at the release
    double floorY = 0;           // the ground's height: the flight ends where the body is this high, coming down
    double gravity = kGravity;
};

struct Out {
    bool valid = false;          // it comes down to the floor within kMaxFlightS
    double vel[3] = {0, 0, 0};   // the velocity the flight uses (SimulateThrow: the horizontal part scaled, and slowed by the cap)
    double landing[3] = {0, 0, 0};   // where it first touches the floor (y = floorY)
    double flightS = 0;
    double speed = 0;            // |vel|
    double elevationDeg = 0;     // the launch direction over the horizontal, degrees (+ up), 0 for a body that is not moving
    double hScale = 1;           // the horizontal factor applied on top of the push (reach, and the cap's reduction): SimulateThrow
    double massFactor = 1;       // the weapon's weight against the arm's push (MassFactor): SimulateThrow
    bool capped = false;         // the cap held the landing
};

inline Out Simulate(const In& in) {
    Out out;
    const double sum = in.pos[0] + in.pos[1] + in.pos[2] + in.vel[0] + in.vel[1] + in.vel[2] + in.floorY + in.gravity;
    if (!std::isfinite(sum) || in.gravity < 0.0) return out;
    for (int i = 0; i < 3; ++i) out.vel[i] = in.vel[i];
    out.speed = std::sqrt(in.vel[0] * in.vel[0] + in.vel[1] * in.vel[1] + in.vel[2] * in.vel[2]);
    const double horizontal = std::sqrt(in.vel[0] * in.vel[0] + in.vel[2] * in.vel[2]);
    out.elevationDeg = out.speed > 1e-9 ? std::atan2(in.vel[1], horizontal) * 57.29577951308232 : 0.0;
    const double h = in.pos[1] - in.floorY;   // height over the floor
    double t = 0.0;
    if (h >= 0.0) {   // coming down from the floor's height or above: y(t) = h + vy t - g t^2 / 2 = 0, the later root (at h = 0 and going down: now)
        if (in.gravity > 1e-12) {
            const double disc = in.vel[1] * in.vel[1] + 2.0 * in.gravity * h;
            t = (in.vel[1] + std::sqrt(disc)) / in.gravity;
        } else if (in.vel[1] < -1e-9) {   // no gravity: a straight line, down or never
            t = h / -in.vel[1];
        } else {
            return out;   // it never comes down
        }
    }   // at or under the floor already: it lands where it is, at once
    if (!std::isfinite(t) || t < 0.0 || t > kMaxFlightS) return out;
    out.flightS = t;
    out.landing[0] = in.pos[0] + in.vel[0] * t;
    out.landing[1] = in.floorY;
    out.landing[2] = in.pos[2] + in.vel[2] * t;
    out.valid = true;
    return out;
}

// The throw's strength against the weapon's weight. The hand's motion is the player's own (a VR controller weighs nothing), and the weapon follows it through
// the model's muscles: a heavy weapon lags behind the swing but then OVERSHOOTS it (it carries its momentum), so the model's release speed alone does not
// make a heavy weapon go short. A throw is a push of the weapon by an arm of some effective mass: the same push gives a heavy weapon less speed, as the
// momentum shared between the arm (kArmKg) and the weapon: v = v_hand * kArmKg / (kArmKg + m). Normalised to the reference weapon (a javelin, kRefKg) so a
// javelin keeps the speed it was swung at: factor = (kArmKg + kRefKg) / (kArmKg + m): a throwing knife (0.3 kg) 1.07, a javelin (0.6) 0.97, a sword (1.2)
// 0.81, a two-handed axe (3.1) 0.54, a polearm (3.5) 0.50. Light and fast goes far, heavy goes short.
constexpr double kArmKg = 2.5, kRefKg = 0.5;
inline double MassFactor(double steelKg) {
    if (!std::isfinite(steelKg) || steelKg < 0.0) return 1.0;
    return (kArmKg + kRefKg) / (kArmKg + steelKg);
}

// The metres the game's aim limit allows from the hero: cells * 2 / unitsPerMetre (a cell is two world units; unitsPerMetre = the world's scale at the hero's size).
inline double CapMetres(double cells, double unitsPerMetre) {
    if (!(cells > 0.0) || !(unitsPerMetre > 0.5) || !std::isfinite(cells + unitsPerMetre)) return 0.0;
    return cells * 2.0 / unitsPerMetre;
}

// A throw: the release velocity (all of it) x MassFactor(steelKg) (steelKg < 0: none), its horizontal part scaled by reachPct / 100 (0..2000 %), the flight
// over the real ground, the landing held to capM metres from the frame's origin on the ground (capM <= 0: no cap). A release velocity above kMaxSpeed is
// not a throw (valid = false).
inline Out SimulateThrow(const In& release, double reachPct, double capM, double steelKg = -1.0) {
    const double speed0 = std::sqrt(release.vel[0] * release.vel[0] + release.vel[1] * release.vel[1] + release.vel[2] * release.vel[2]);
    if (!std::isfinite(speed0) || speed0 > kMaxSpeed) return Out{};
    const double m = MassFactor(steelKg);
    const double k = std::isfinite(reachPct) ? (reachPct < 0.0 ? 0.0 : reachPct > 2000.0 ? 20.0 : reachPct * 0.01) : 1.0;
    In pushed = release;   // what the arm gives this weapon
    for (int i = 0; i < 3; ++i) pushed.vel[i] *= m;
    In scaled = pushed;
    scaled.vel[0] *= k;
    scaled.vel[2] *= k;
    Out out = Simulate(scaled);
    out.hScale = k;
    out.massFactor = m;
    if (!out.valid) return out;
    if (capM > 0.0) {
        // the landing's horizontal distance from the origin; held to capM by slowing the horizontal flight (the same flight time): |p + s d| = capM, s in [0, 1]
        const double px = release.pos[0], pz = release.pos[2];
        const double dx = out.landing[0] - px, dz = out.landing[2] - pz;
        const double dist = std::sqrt(out.landing[0] * out.landing[0] + out.landing[2] * out.landing[2]);
        if (dist > capM) {
            const double a = dx * dx + dz * dz, b = px * dx + pz * dz, c = px * px + pz * pz - capM * capM;
            double s = 0.0;
            if (a > 1e-12 && c <= 0.0) s = (-b + std::sqrt(std::max(0.0, b * b - a * c))) / a;
            s = s < 0.0 ? 0.0 : s > 1.0 ? 1.0 : s;
            scaled.vel[0] = pushed.vel[0] * k * s;
            scaled.vel[2] = pushed.vel[2] * k * s;
            out = Simulate(scaled);
            out.hScale = k * s;
            out.massFactor = m;
            out.capped = true;
        }
    }
    return out;
}

}  // namespace weightflight
