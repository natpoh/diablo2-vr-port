// leap_air_aim.h - [gameplay] leap_air_aim (VR F4, with leap_arc): the NEXT leap aimed in the air. Pure (cleanroom/tests/leap_air_aim_test.cpp).
//
// Serg (2026-11-07): «варвар подпрыгивает и прямо в воздухе можно взглядом указывать направление, куда он приземлится, когда он прыгает высоко».
// The game fixes a Leap's landing when it is cast: the client asks its target once (the cast flags' lookup, the resolver's AttackPoint, the click's
// lookup), the cast goes out with that point, and the hero flies the path the game set for it - nothing re-aims a leap under way, and moving the unit
// ourselves or calling the server is out (never break game mechanics). So THIS leap's landing cannot be steered in the air. What can be: the next one.
// Held in the air, [gameplay] leap_arc's arc and circle are up as on the ground (from the belly, aimed by the head with leap_arc_head); let go in the
// air, the circle is kept as a point of the WORLD (not "so far from the hero": he is still flying on), the slot's own button is held through the
// landing until the game's own cast of it asks its point - then let go - and the point given is that world spot seen from where he landed, held to
// the leap's range. The press is the game's own button in the game's own poll: its mana, its recovery, its range, its arc.
//
// Airborne: the hero's model matrix over the camera's ground (vr/under_camera.h Measure's lift) - the game lifts it 12 - 13.7 units at a leap's
// top (the author's log, 2026-10-26); walking it stays under undercam::kLiftFrom (0.3). Up past kUpUnits it is in the air, back under kDownUnits
// it has landed. A release in the take-off's crouch (the matrix not lifted yet) or on the ground is today's release: pressed at once.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace leapair {

constexpr float kUpUnits = 1.0f;       // world units of the matrix over the look-at: in a leap's air (undercam::kTellGap: ~28 cm at a man's height)
constexpr float kDownUnits = 0.5f;     // ... back under this: landed
constexpr uint32_t kHoldMs = 1500;     // a release in the air: the slot's button held at most this long for the game's cast (the rest of the arc and its landing)
constexpr uint64_t kPointAgeMs = kHoldMs + 300;   // ... and the kept point answers the cast this long after the release

// The air watched pose by pose (hysteresis on the lift). Feed: lift in world units (+ up); not a number: no change.
class Air {
public:
    bool Feed(float lift) {
        if (!std::isfinite(lift)) return up_;
        if (!up_ && lift > kUpUnits) up_ = true;
        else if (up_ && lift < kDownUnits) up_ = false;
        return up_;
    }
    bool Up() const { return up_; }

private:
    bool up_ = false;
};

// On a release: is it held through the landing (true) or pressed at once as ever (false)? on: the tick; leapSkill: Leap or Leap Attack; airborne: Air::Up
// of a fresh pose; circle: a hold's release with its circle up (a tap is the gaze's at its own cast, as ever).
inline bool HoldThrough(bool on, bool leapSkill, bool airborne, bool circle) { return on && leapSkill && airborne && circle; }

// The kept world point (wx, wz) seen from the hero at (hx, hz), world units across the ground: held to [minUnits, capUnits] along its own heading. On the
// hero (or not numbers): false - the cast then takes the gaze's point as without a circle.
inline bool FromHero(float wx, float wz, float hx, float hz, float minUnits, float capUnits, float* ox, float* oz) {
    const float dx = wx - hx, dz = wz - hz;
    const float l = std::hypot(dx, dz);
    if (!std::isfinite(l) || !(l > 1e-3f)) return false;
    const float hi = std::max(minUnits, std::isfinite(capUnits) ? capUnits : minUnits);
    const float to = std::clamp(l, minUnits, hi);
    *ox = dx / l * to;
    *oz = dz / l * to;
    return true;
}

}  // namespace leapair
