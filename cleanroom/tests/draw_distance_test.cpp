// Offline checks of shared/draw_distance.h: D2R VR Settings' Draw distance slider (Home, its own tab). Every stop is a consistent set -
// the sky dome at the models' radius (the edge of the drawn world, never farther), the fog full just before it outdoors and at it in
// caves, rooms enough for the radius, each value on its own slider's range and step - the stops go near to far, and a value changed by
// hand reads as Custom.
// Run: build/Release/draw_distance_test.exe (exit code 0 = passed)

#include <cmath>
#include <cstdio>

#include "../../shared/draw_distance.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

// on the single slider's range and step (settings.cpp: rings 1-8 by 1, model_radius 150-1000 by 10, fog start 0-1000 by 5, Full fog
// 10-2000 by 10, the caves the same; game_sky_radius_m 10-7500 m by 0.5)
static bool OnSlider(float v, float min, float max, float step) {
    const float k = (v - min) / step;
    return v >= min && v <= max && std::fabs(k - std::round(k)) < 1e-4f;
}

int main() {
    using namespace drawdistance;
    CHECK(kCount == 8);
    for (int i = 0; i < kCount; ++i) {
        const Stop& s = kStops[i];
        CHECK(s.rings >= 1 && s.rings <= 8);
        CHECK(OnSlider(s.modelRadius, 150, 1000, 10));
        CHECK(OnSlider(s.fogStart, 0, 1000, 5));
        CHECK(OnSlider(s.fogEnd, 10, 2000, 10));
        CHECK(OnSlider(s.cavesStart, 0, 1000, 5));
        CHECK(OnSlider(s.cavesEnd, 10, 2000, 10));
        CHECK(OnSlider(ToMetres(s.skyRadius), 10, 7500, 0.5f));   // (in metres on its slider: each stop exactly, Match holds)
        // the dome at the edge of the drawn world, the fog full just before it (0.9 of the radius, to the fog end slider's step) and at
        // it in caves, rooms enough for the radius (the presets' 1 at 150, 3 at 400)
        CHECK(s.skyRadius == s.modelRadius);
        CHECK(s.fogStart < s.fogEnd && s.fogEnd < s.skyRadius && std::fabs(s.fogEnd - 0.9f * s.modelRadius) <= 5.0f);
        CHECK(s.cavesStart < s.cavesEnd && s.cavesEnd == s.skyRadius);
        CHECK(s.rings >= (int)std::ceil(s.modelRadius / 150.0f));
        CHECK(Match(s) == i);
        CHECK(Nearest(s.modelRadius) == i);
        if (i > 0) {   // near to far: nothing comes nearer
            const Stop& p = kStops[i - 1];
            CHECK(s.rings >= p.rings && s.modelRadius > p.modelRadius && s.fogStart > p.fogStart && s.fogEnd > p.fogEnd);
            CHECK(s.cavesStart > p.cavesStart && s.cavesEnd > p.cavesEnd && s.skyRadius > p.skyRadius);
        }
    }
    // stop 3, the default and the shipped ini's: the author's middle, objects and the dome at 200 (2026-10-28)
    const Stop three{2, 200, 50, 180, 100, 200, 200};
    CHECK(Match(three) == kDefault && kDefault == 2);
    // the author's ini before the dome came to the edge (210, fog end 150, the dome at 320): Custom, the slider at 3 (210 nearest 200)
    const Stop before{2, 210, 50, 150, 105, 210, 320};
    CHECK(Match(before) == -1 && Nearest(before.modelRadius) == kDefault);
    // any one value by hand: Custom
    Stop s = three; s.fogEnd = 190; CHECK(Match(s) == -1);
    s = three; s.rings = 3; CHECK(Match(s) == -1);
    s = three; s.skyRadius = 210; CHECK(Match(s) == -1);
    s = three; s.cavesStart = 105; CHECK(Match(s) == -1);
    // the Performance presets' sets are their own: Low (1 ring, 150, fog 30 - 100) and Medium (2, 210, 30 - 200) read Custom
    CHECK(Match(Stop{1, 150, 30, 100, 60, 100, 200}) == -1);
    CHECK(Match(Stop{2, 210, 30, 200, 100, 200, 200}) == -1);
    CHECK(Nearest(150) == 0 && Nearest(100) == 0 && Nearest(5000) == kCount - 1);
    CHECK(Nearest(190) == 2);   // a tie between 180 and 200: the farther
    CHECK(Nearest(400) == 5 && Nearest(999) == 7);
    // the domes in metres (Serg, 2026-10-31), 4 units a metre; the older keys convert to the same size
    CHECK(ToUnits(50.0f) == 200.0f && ToMetres(200.0f) == 50.0f && ToUnits(ToMetres(5090.0f)) == 5090.0f);
    CHECK(SkyRadiusM(600.0f) == 150.0f && SkyRadiusM(5090.0f) == 1272.5f);   // the shipped ini's 600 units, Serg's 5090
    CHECK(CaveRadiusM(1.0f, 210.0f) == 52.5f && std::fabs(CaveRadiusM(1.05f, 200.0f) - 52.5f) < 1e-4f && CaveRadiusM(0.5f, 210.0f) == 26.25f);
    CHECK(kCaveRadiusDefaultM == 200.0f && OnSlider(kCaveRadiusDefaultM, 5, 500, 0.5f));   // Serg's default
    // the slider moving the models' radius: the cave dome keeps its share of it
    CHECK(CaveRadiusFollowing(52.5f, 210.0f, 400.0f) == 100.0f && CaveRadiusFollowing(52.5f, 0.0f, 400.0f) == 52.5f);
    CHECK(std::fabs(CaveRadiusFollowing(ToMetres(1.05f * 200.0f), 200.0f, 150.0f) - ToMetres(1.05f * 150.0f)) < 1e-4f);
    // VR F3's own stop ([table] draw_distance): 0 = the shared keys, 1 ... kCount the slider's stops, anything else the shared keys
    CHECK(ViewStop(0) == nullptr && ViewStop(-1) == nullptr && ViewStop(kCount + 1) == nullptr);
    CHECK(ViewStop(1) == &kStops[0] && ViewStop(3)->modelRadius == 200.0f && ViewStop(kCount) == &kStops[kCount - 1]);
    // ... its room rings from above: the rooms (80 units, 8 tiles of 5 cells of 2 units) reach the fog's full end wherever the hero stands
    // in his room (k rings: at least k rooms past his room's edge), never fewer than the stop's own, at most 8
    CHECK(kRoomUnits == 80.0f);
    {
        const int want[kCount] = {2, 2, 3, 3, 4, 5, 7, 8};
        for (int i = 0; i < kCount; ++i) {
            const int k = FromAboveRings(kStops[i]);
            CHECK(k == want[i]);
            CHECK(k >= kStops[i].rings && k <= kMaxRings);
            if (k < kMaxRings) CHECK(k * kRoomUnits >= kStops[i].fogEnd);
        }
        CHECK(FromAboveRings(Stop{1, 100, 20, 160, 50, 100, 100}) == 2);   // exactly two rooms: two rings, not three
    }
    // ... its fog measured round the hero from the head high above him: a ring r on his ground, h over him, is sqrt(h^2 + r^2) away;
    // no height (F4's eye at the hero) leaves the stop's numbers as they are
    CHECK(FromEyeAbove(0.0f, 50.0f) == 50.0f && FromEyeAbove(30.0f, 40.0f) == 50.0f && FromEyeAbove(54.0f, 0.0f) == 54.0f);
    CHECK(FromEyeAbove(54.0f, 180.0f) > 180.0f && FromEyeAbove(54.0f, 180.0f) < 180.0f + 54.0f);
    // The Home slider's own stops since 2026-11-09 (kReach; Serg: "ползунок дравдистанс давай вернем", "по умолчанию поставим как сейчас 2",
    // "комнаты 200 дистанция"): each value on its own slider, near to far, the fog full before the models' radius past the presets' stops,
    // the default stop 2 = 2 rings, models 200 and today's game fog
    CHECK(kReachCount == 7 && kReachDefault == 1);
    for (int i = 0; i < kReachCount; ++i) {
        const Reach& q = kReach[i];
        CHECK(q.rings >= 2 && q.rings <= 8);
        CHECK(OnSlider(q.modelRadius, 150, 1000, 10));
        CHECK(OnSlider(q.fogStart, 0, 1000, 5) && OnSlider(q.fogEnd, 10, 2000, 10));
        CHECK(OnSlider(q.cavesStart, 0, 1000, 5) && OnSlider(q.cavesEnd, 10, 2000, 10));
        CHECK(q.fogStart < q.fogEnd && q.fogEnd < q.modelRadius && q.cavesStart < q.cavesEnd);
        CHECK(q.rings >= (int)std::ceil(q.modelRadius / 150.0f));
        CHECK(MatchReach(q) == i && NearestReach(q.modelRadius) == i);
        if (i > kReachDefault) {   // past the presets' world: the fog opens with the distance, today's proportions (0.275 / 0.7 r, caves 0.325 r - r)
            CHECK(std::fabs(q.fogStart - 0.275f * q.modelRadius) <= 2.5f && std::fabs(q.fogEnd - 0.7f * q.modelRadius) <= 5.0f);
            CHECK(std::fabs(q.cavesStart - 0.325f * q.modelRadius) <= 2.5f && q.cavesEnd == q.modelRadius);
        }
        if (i > 0) {   // near to far: nothing comes nearer
            const Reach& p = kReach[i - 1];
            CHECK(q.rings >= p.rings && q.modelRadius > p.modelRadius && q.fogStart >= p.fogStart && q.fogEnd >= p.fogEnd);
            CHECK(q.cavesStart >= p.cavesStart && q.cavesEnd >= p.cavesEnd);
        }
    }
    {   // the presets' distances (shared/quality_presets.h: 2 rings, the game's fog 55 / 140, caves 65 / 200): Low / Potato stop 1, Medium stop 2
        const Reach low{2, 150, 55, 140, 65, 200}, medium{2, 200, 55, 140, 65, 200};
        CHECK(MatchReach(low) == 0 && MatchReach(medium) == kReachDefault);
        // 0.208's Medium (models 210): Custom, the slider at 2 (210 nearest 200)
        const Reach was{2, 210, 55, 140, 65, 200};
        CHECK(MatchReach(was) == -1 && NearestReach(was.modelRadius) == kReachDefault);
        Reach r = medium; r.fogEnd = 150; CHECK(MatchReach(r) == -1);
        r = medium; r.rings = 3; CHECK(MatchReach(r) == -1);
        r = medium; r.cavesEnd = 210; CHECK(MatchReach(r) == -1);
        CHECK(NearestReach(100) == 0 && NearestReach(5000) == kReachCount - 1 && NearestReach(175) == 1 && NearestReach(220) == 1 && NearestReach(230) == 2 && NearestReach(800) == 6);
        // the headset's < / >: at the ends it stays, from Custom first onto the nearest stop
        CHECK(StepReach(0, 150, -1) == 0 && StepReach(kReachCount - 1, 1000, 1) == kReachCount - 1 && StepReach(1, 200, 1) == 2);
        CHECK(StepReach(-1, 210, 1) == kReachDefault && StepReach(-1, 210, -1) == kReachDefault);
    }
    // VR F3's table stays as it was (its stop 3: 2 rings, 200, fog 50 - 180, the dome at 200)
    CHECK(kStops[2].rings == 2 && kStops[2].modelRadius == 200.0f && kStops[2].fogEnd == 180.0f && kStops[2].skyRadius == 200.0f && kCount == 8);
    if (g_failures) { std::printf("%d check(s) FAILED\n", g_failures); return 1; }
    std::printf("draw_distance_test: all %d checks passed\n", g_checks);
    return 0;
}
