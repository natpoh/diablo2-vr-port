#pragma once
// D2R VR Settings' "Draw distance" slider (Home and its own tab): one stop, near to far, sets the keys that are set apart elsewhere so
// that they agree -
//
//   [render] rings, model_radius        how many rings of rooms are drawn, how far out models (monsters, objects) are drawn: the edge
//                                       of the drawn world
//   [sky] game_sky_radius               the game's sky dome AT that edge (the author, 2026-10-28: never farther than the draw distance)
//   [fog] start, end                    the fog outdoors: full (end) just before the dome, at 0.9 of the radius - the last tenth all fog,
//                                       so nothing pops in out of the void and the dome's horizon is in the fog
//   [fog] caves_start, caves_end        underground: full at the radius (the dome's place)
//
// The cave dome ([sky] game_dome_caves_size) is a share of model_radius already: it follows the stop by itself and is not written.
// Stop 3 is the author's middle (2026-10-28: objects and the dome at 200), 2 rings; every stop by one rule (fog start 0.24 r,
// caves 0.5 r - r), fog end 0.9 r and the dome r, rounded to the sliders' steps, and rings = ceil(r / 150) - the presets' 1 ring at
// 150, 2 at 210, 3 at 400. Nothing is kept: the slider shows the stop all seven keys match, else Custom.
// All of it is live in the plugin (vrcam re-reads the ini; drawdist reads the rings each tick, the model radius re-sorts at once).
// No Windows in it.

#include <cmath>

namespace drawdistance {

struct Stop {
    int rings;
    float modelRadius, fogStart, fogEnd, cavesStart, cavesEnd, skyRadius;
};

inline constexpr Stop kStops[] = {
    {1, 150, 35, 140, 75, 150, 150},
    {2, 180, 45, 160, 90, 180, 180},
    {2, 200, 50, 180, 100, 200, 200},   // the author's middle: 200 / 200
    {2, 260, 60, 230, 130, 260, 260},
    {3, 320, 75, 290, 160, 320, 320},
    {3, 400, 95, 360, 200, 400, 400},
    {4, 600, 145, 540, 300, 600, 600},
    {7, 1000, 240, 900, 500, 1000, 1000},
};
inline constexpr int kCount = (int)(sizeof(kStops) / sizeof(kStops[0]));
inline constexpr int kDefault = 2;   // (0-based) the slider's default and the shipped d2r_vr.ini's: stop 3

// The stop these values are, or -1 (Custom).
inline int Match(const Stop& v) {
    auto same = [](float a, float b) { return std::fabs(a - b) < 0.5f; };
    for (int i = 0; i < kCount; ++i) {
        const Stop& s = kStops[i];
        if (v.rings == s.rings && same(v.modelRadius, s.modelRadius) && same(v.fogStart, s.fogStart) && same(v.fogEnd, s.fogEnd) &&
            same(v.cavesStart, s.cavesStart) && same(v.cavesEnd, s.cavesEnd) && same(v.skyRadius, s.skyRadius))
            return i;
    }
    return -1;
}

// Where the slider sits for Custom: the stop with the nearest model radius (the farther one on a tie).
inline int Nearest(float modelRadius) {
    int best = 0;
    for (int i = 1; i < kCount; ++i)
        if (std::fabs(kStops[i].modelRadius - modelRadius) <= std::fabs(kStops[best].modelRadius - modelRadius)) best = i;
    return best;
}

}  // namespace drawdistance
