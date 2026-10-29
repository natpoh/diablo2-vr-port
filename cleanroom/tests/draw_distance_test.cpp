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
// 10-2000 by 10, the caves the same, game_sky_radius 50-30000 by 10)
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
        CHECK(OnSlider(s.skyRadius, 50, 30000, 10));
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
    if (g_failures) { std::printf("%d check(s) FAILED\n", g_failures); return 1; }
    std::printf("draw_distance_test: all %d checks passed\n", g_checks);
    return 0;
}
