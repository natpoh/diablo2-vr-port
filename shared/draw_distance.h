#pragma once
// The draw distance stops. kStops: the first table, made for D2R VR Settings' Draw distance slider (2026-10-28) and since 0.209 VR F3's own
// ([table] draw_distance, ViewStop below) - kept as they were. The Home slider has its own table since 2026-11-09: kReach (further down).
// kStops' rule: one stop, near to far, sets the keys that are set apart elsewhere so that they agree -
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

// The Home slider's own stops (Serg 2026-11-09, the slider back after 0.208 hid it: "ползунок дравдистанс давай вернем", "по умолчанию
// поставим как сейчас 2", "комнаты 200 дистанция" - the option to draw farther, the default as today). kStops above stay VR F3's ([table]
// draw_distance, unchanged); this table is what D2R VR Settings' Draw distance slider (Home, its tab) and the headset's Video page row write.
// Six keys, no dome: [sky] game_sky_radius_m ships at 1000 m (Serg 2026-11-03), past every stop, and the cave dome is its own slider - the
// slider leaves both as they are. The near stops are the Quality presets' world (shared/quality_presets.h: 2 rings everywhere, the game's fog
// 55 / 140, in caves 65 / 200 - "размер тумана одинаковый на всех"): stop 1 = Low and Potato (models 150), stop 2 = Medium (200), the
// default and the shipped d2r_vr.ini's. Past it the fog opens with the distance, today's proportions kept (start 0.275 r, full 0.7 r; in
// caves 0.325 r to r), rounded to the sliders' steps, so the farther world drawn is seen; rings = ceil(r / 150), at least 2.
struct Reach {
    int rings;
    float modelRadius, fogStart, fogEnd, cavesStart, cavesEnd;
};
inline constexpr Reach kReach[] = {
    {2, 150, 55, 140, 65, 200},    // Low, Potato
    {2, 200, 55, 140, 65, 200},    // Medium: the default
    {2, 260, 70, 180, 85, 260},
    {3, 320, 90, 220, 105, 320},
    {3, 400, 110, 280, 130, 400},
    {4, 600, 165, 420, 195, 600},
    {7, 1000, 275, 700, 325, 1000},
};
inline constexpr int kReachCount = (int)(sizeof(kReach) / sizeof(kReach[0]));
inline constexpr int kReachDefault = 1;   // (0-based) the slider's default, Medium's and the shipped d2r_vr.ini's: stop 2 - 2 rings, models 200

// The stop these values are, or -1 (Custom).
inline int MatchReach(const Reach& v) {
    auto same = [](float a, float b) { return std::fabs(a - b) < 0.5f; };
    for (int i = 0; i < kReachCount; ++i) {
        const Reach& s = kReach[i];
        if (v.rings == s.rings && same(v.modelRadius, s.modelRadius) && same(v.fogStart, s.fogStart) && same(v.fogEnd, s.fogEnd) &&
            same(v.cavesStart, s.cavesStart) && same(v.cavesEnd, s.cavesEnd))
            return i;
    }
    return -1;
}
// Where the slider sits for Custom: the stop with the nearest model radius (the farther one on a tie).
inline int NearestReach(float modelRadius) {
    int best = 0;
    for (int i = 1; i < kReachCount; ++i)
        if (std::fabs(kReach[i].modelRadius - modelRadius) <= std::fabs(kReach[best].modelRadius - modelRadius)) best = i;
    return best;
}
// A press of < (-1) or > (+1) on the headset's row: stopping at the ends; from Custom it starts at the nearest stop, one step on.
inline int StepReach(int now, float modelRadius, int delta) {
    const int from = now >= 0 && now < kReachCount ? now : NearestReach(modelRadius);
    if (now < 0) return from;   // (Custom: first onto the nearest stop itself)
    const int to = from + (delta > 0 ? 1 : -1);
    return to < 0 ? 0 : to >= kReachCount ? kReachCount - 1 : to;
}

// VR F3's own draw distance ([table] draw_distance, Serg 2026-11-08: "вернуть ползунок драв дистанс в режиме ф3, он нужен"): 0 = F3 draws as
// far as the shared keys above say (as before), 1 ... kCount = that stop's rings, models' radius, dome and fog while F3 is the view. The
// shared keys - and so VR F4 and the other views - are never written by it: the slider stays out of F4 (seeing farther than a player
// from above is an advantage there). nullptr = the shared keys.
inline const Stop* ViewStop(int stop) { return stop >= 1 && stop <= kCount ? &kStops[stop - 1] : nullptr; }
// ... and its room rings. F3 is looked at from above, so the ground's tiles must be seen all round the hero (Serg 2026-11-08: "там
// смотришь сверху и плитки надо чтобы было видно"): enough rings that the rooms reach the fog's full end wherever the hero stands in his
// room. The kth ring ends at least k rooms past the hero's room edge. A room is 8 tiles of 5 cells, 2 world units a cell (the game's grid;
// grabmove::kUnitsPerCell), 80 units outdoors - the smallest the game makes. Never fewer than the stop's own rings, at most [render]
// rings' 8. Stops 1-8: 2, 2, 3, 3, 4, 5, 7, 8 rings.
inline constexpr float kUnitsPerCell = 2.0f;
inline constexpr float kRoomUnits = 8.0f * 5.0f * kUnitsPerCell;
inline constexpr int kMaxRings = 8;
inline int FromAboveRings(const Stop& s) {
    const int k = (int)std::ceil(s.fogEnd / kRoomUnits - 1e-4f);
    return k < s.rings ? s.rings : k > kMaxRings ? kMaxRings : k;
}
// The fog's distances are the eye's. In F4 the eye is the hero's, so a stop's fog lies round him; in F3 the eye is the head high over the
// diorama ([table] hero_cm: the smaller the hero, the more world units it is above him), and the same numbers fogged the hero himself. A
// ring r round the hero, seen from `heightAbove` over him, is this far from the eye: the stop's ring kept round the hero, as in F4.
inline float FromEyeAbove(float heightAbove, float r) { return std::sqrt(heightAbove * heightAbove + r * r); }

// The two domes' radii are set in metres (Serg, 2026-10-31: "тут радиус в метрах, а не единицах"): [sky] game_sky_radius_m and
// game_dome_caves_radius_m, the game's world units at kUnitsPerMetre - the mod's own world units a metre at the hero's size (melee_touch's
// and melee_boxes' upm; Serg's live scale, the hero's 7.05-unit eyes over his 1.76 m, is 4.0). One number for both keys and the slider's
// stops, so the dome stays at the models' radius whoever plays. The keys they took over from count while the new ones are not in the ini:
inline constexpr float kUnitsPerMetre = 4.0f;
inline float ToUnits(float metres) { return metres * kUnitsPerMetre; }
inline float ToMetres(float units) { return units / kUnitsPerMetre; }
// [sky] game_sky_radius: world units
inline float SkyRadiusM(float oldUnits) { return ToMetres(oldUnits); }
// [sky] game_dome_caves_size: a share of [render] model_radius (world units)
inline float CaveRadiusM(float oldShare, float modelRadius) { return ToMetres(oldShare * modelRadius); }
// Neither key in the ini: the cave dome 200 m round (Serg, 2026-10-31: "200 м по умолчанию")
inline constexpr float kCaveRadiusDefaultM = 200.0f;
// The slider moves the models' radius from `oldModel` to `newModel`: the cave dome keeps its share of it, as the share key did by itself.
inline float CaveRadiusFollowing(float caveM, float oldModel, float newModel) { return oldModel > 0.0f ? caveM * newModel / oldModel : caveM; }

}  // namespace drawdistance
