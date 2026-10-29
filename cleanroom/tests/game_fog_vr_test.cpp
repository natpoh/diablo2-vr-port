// game_fog_vr_test - vr/game_fog_vr.h: our fog's numbers as the game's own distance fog ([fog] game_fog).
//   1. none before our start, kFullAt of the strength at our end, more in between, never past the strength much farther;
//   2. a start of 0 (the game divides by its plane), an end before the start, a cave's strength of 0.6;
//   3. the colour in scene units shows as ours below the tone map's knee;
//   4. the layout check: the frame's five floats the blend's - not when they differ, are not finite, or the plane is 0.
#include "game_fog_vr.h"

#include <cstdio>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

using namespace gamefogvr;

int main() {
    {   // the outdoors: start 150, end 600, strength 1
        const Ours o{150.0f, 600.0f, 1.0f, {0.20f, 0.22f, 0.26f}};
        const Fog f = FromOurs(o);
        CHECK(f.plane == 150.0f && f.density > 0.0f);
        CHECK(GameFogAmount(f, 100.0f) == 0.0f);   // nearer than the start: none
        CHECK(GameFogAmount(f, 150.0f) == 0.0f);
        CHECK(std::fabs(GameFogAmount(f, 600.0f) - kFullAt) < 1e-4f);
        const float mid = GameFogAmount(f, 375.0f);
        CHECK(mid > 0.3f && mid < kFullAt);
        CHECK(GameFogAmount(f, 1500.0f) < 1.0f && GameFogAmount(f, 1500.0f) > kFullAt);
        CHECK(GameFogAmount(f, 1e9f) == 0.0f);   // past 10000 planes: the void, none
        // the colour: back through the game's tone map (below its knee) it is ours
        const fogframe::V3 shown = fogframe::DisplayFromScene({f.r, f.g, f.b});
        CHECK(std::fabs(shown.r - 0.20f) < 0.003f && std::fabs(shown.g - 0.22f) < 0.003f && std::fabs(shown.b - 0.26f) < 0.003f);
        CHECK(fogframe::BelowKnee({f.r, f.g, f.b}));
    }
    {   // a cave: start 0 (plane 1), end 120, strength 0.6
        const Fog f = FromOurs({0.0f, 120.0f, 0.6f, {0.02f, 0.02f, 0.025f}});
        CHECK(f.plane == 1.0f);
        CHECK(std::fabs(GameFogAmount(f, 120.0f) - kFullAt * 0.6f) < 1e-4f);
    }
    {   // an end before the start: still a fog, never a division by nothing
        const Fog f = FromOurs({300.0f, 200.0f, 1.0f, {0.1f, 0.1f, 0.1f}});
        CHECK(f.plane == 300.0f && std::isfinite(f.density) && f.density > 0.0f);
    }
    {   // the layout check
        const float blend[5] = {0.1f, 0.2f, 0.3f, 0.0f, 885.0f};
        float frame[5];
        std::memcpy(frame, blend, sizeof frame);
        CHECK(SameFog(blend, frame));
        frame[4] = 884.0f;
        CHECK(!SameFog(blend, frame));
        const float zeroPlane[5] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        CHECK(!SameFog(zeroPlane, zeroPlane));   // all zero (nothing written yet): no proof of the layout
        const float nanBlend[5] = {NAN, 0.2f, 0.3f, 0.0f, 885.0f};
        CHECK(!SameFog(nanBlend, nanBlend));
        CHECK(!SameFog(nullptr, frame));
    }
    {   // the frame's fog found by value wherever the game writes it (PC1, 3.3.94995: never at the reading's place)
        const float blend[5] = {0.1f, 0.2f, 0.3f, 0.0f, 885.0f};
        float frame[512] = {};
        CHECK(FindFog(blend, frame, 512) == -1);                  // nowhere
        std::memcpy(frame + 100, blend, sizeof blend);
        CHECK(FindFog(blend, frame, 512) == 100);
        std::memcpy(frame + 300, blend, sizeof blend);
        CHECK(FindFog(blend, frame, 512) == -2);                  // two places: none taken
        float edge[8] = {};
        std::memcpy(edge + 3, blend, sizeof blend);
        CHECK(FindFog(blend, edge, 8) == 3);                      // at the very end
        const float zeros[5] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        CHECK(FindFog(zeros, frame, 512) == -1);                  // the menus' nothing: not looked for
        const float noColour[5] = {0.0f, 0.0f, 0.0f, 0.0f, 885.0f};
        CHECK(FindFog(noColour, frame, 512) == -1);               // a plane alone matches too easily: not looked for
        CHECK(FindFog(nullptr, frame, 512) == -1);
    }
    if (g_fail) { std::printf("game_fog_vr_test: %d FAILED\n", g_fail); return 1; }
    std::printf("game_fog_vr_test: our fog as the game's own - none before the start, %.0f%% of the strength at the end, the colour as ours "
                "through the game's tone map, the layout checked before a write: all passed\n", kFullAt * 100.0f);
    return 0;
}
