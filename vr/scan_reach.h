// scan_reach.h - the bodies drawn away keep their units in the scans. Pure (cleanroom/tests/scan_reach_test.cpp); the scans are vr/melee_touch.cpp's
// (the units within 24 game units of the hero, MonstersNear) and vr/skeletons.cpp's (the 16 nearest skeletons within 6 m, MonsterRigs).
//
// Serg, 0.196.0: «монстры исчезают, когда я их поднимаю» (grab_bodies + grab_throw, grab_moves_unit; in a druid's form too). His log: each body vanished a
// second after its unit left the scans - "knock down: 9 let go of - not seen for 1.0 s". Kashya carried 13 cells from where the game keeps her (her
// knockback refused, she stays home); a zombie carried by a running werebear, its knockbacks a step behind, let go on the run. The knock-down library draws a
// body only on its unit's own skeleton; a unit out of the scans has none, the library lets it go, and the drawing goes back to the unit - far behind the hero,
// out of his sight: gone. So the scans reach as far as the farthest body drawn away (its unit's last place, a margin on top, a cap), and the skeleton at
// such a place is taken whatever the 16 nearest are.
#pragma once

#include <algorithm>
#include <cmath>

namespace scanreach {

constexpr float kMarginM = 2.0f;   // past the unit's last place (it walks on between two scans)
constexpr float kCapM = 30.0f;     // never farther (a unit that far is let go of, as before)
constexpr float kKeepM = 1.0f;     // a skeleton this near a kept place is that body's (its unit moves a little between ticks)

// The reach a scan needs (its units: game world units = render units; `upm` of them a metre): `base`, or the farthest of `at` (x, z) from the hero plus
// kMarginM, held to kCapM.
inline float Reach(float base, float heroX, float heroZ, const float (*at)[2], int n, float upm) {
    float r = base;
    if (!at || n <= 0 || !(upm > 0.0f) || !std::isfinite(heroX + heroZ)) return r;
    for (int i = 0; i < n; ++i) {
        if (!std::isfinite(at[i][0] + at[i][1])) continue;
        const float d = std::hypot(at[i][0] - heroX, at[i][1] - heroZ);
        r = std::max(r, std::min(d + kMarginM * upm, kCapM * upm));
    }
    return r;
}
// Is (x, z) one of the kept places (within kKeepM)?
inline bool Kept(float x, float z, const float (*at)[2], int n, float upm) {
    if (!at || n <= 0 || !(upm > 0.0f)) return false;
    const float tol = kKeepM * upm;
    for (int i = 0; i < n; ++i)
        if (std::fabs(at[i][0] - x) <= tol && std::fabs(at[i][1] - z) <= tol) return true;
    return false;
}

}  // namespace scanreach
