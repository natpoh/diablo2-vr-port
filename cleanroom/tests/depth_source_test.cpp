// Offline checks of vr/depth_source.h ([render] depth_fix_mode 2): the game's depth range rebuilt from our camera's near plane
// and the game's own far end. Run: depth_source_test.exe (exit 0 = all passed).
#include <cmath>
#include <cstdio>

#include "depth_source.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static bool Near(float a, float b) { return std::fabs(a - b) <= 1e-3f * std::fmax(1.0f, std::fabs(b)); }

// The game's reversed-Z infinite projection: [0] [5] the focal scales, [10] 0, [11] -1, [14] the near plane, [15] 0.
static void Projection(float p[16], float nearPlane) {
    for (int i = 0; i < 16; ++i) p[i] = 0.0f;
    p[0] = 1.2f; p[5] = 1.0f; p[11] = -1.0f; p[14] = nearPlane;
}

int main() {
    using depthrange::PerspectiveRange;
    float p[16];
    {   // the game's own range from above (start 550, range 510 -> far 1060): our camera's near plane 0.5 -> 0.5 .. 1060
        Projection(p, 0.5f);
        float r[4] = {510.0f, 1.0f / 510.0f, 550.0f, 7.0f};
        CHECK(PerspectiveRange(p, r));
        CHECK(Near(r[2], 0.5f) && Near(r[0], 1059.5f) && Near(r[1], 1.0f / 1059.5f) && r[3] == 7.0f);
        // a pixel at 30 units (a hero's few steps) now decodes inside 0..1, not below 0 as with the game's start
        const float linear = (30.0f - r[2]) * r[1];
        CHECK(linear > 0.0f && linear < 0.05f);
        CHECK((30.0f - 550.0f) / 510.0f < 0.0f);
    }
    {   // not the game's kind of projection: nothing changed
        float r[4] = {510.0f, 1.0f / 510.0f, 550.0f, 0.0f};
        const float keep[4] = {r[0], r[1], r[2], r[3]};
        Projection(p, 0.5f); p[10] = 0.1f;           // a finite far plane
        CHECK(!PerspectiveRange(p, r));
        Projection(p, 0.5f); p[11] = 1.0f;           // left-handed
        CHECK(!PerspectiveRange(p, r));
        Projection(p, 0.0f);                          // no near plane
        CHECK(!PerspectiveRange(p, r));
        Projection(p, 0.5f); p[3] = NAN;
        CHECK(!PerspectiveRange(p, r));
        CHECK(r[0] == keep[0] && r[1] == keep[1] && r[2] == keep[2] && r[3] == keep[3]);
    }
    {   // not a range in word 53: nothing changed
        Projection(p, 0.5f);
        float a[4] = {510.0f, 0.5f, 550.0f, 0.0f};     // 1 / range wrong
        CHECK(!PerspectiveRange(p, a) && a[0] == 510.0f);
        float b[4] = {0.0f, 0.0f, 550.0f, 0.0f};
        CHECK(!PerspectiveRange(p, b));
        Projection(p, 2000.0f);                       // a near plane past the far end
        float c[4] = {510.0f, 1.0f / 510.0f, 550.0f, 0.0f};
        CHECK(!PerspectiveRange(p, c) && c[2] == 550.0f);
    }
    {   // Store writes three floats and leaves the fourth lane
        float mapped[4] = {1, 2, 3, 4};
        const float r[4] = {9, 8, 7, 6};
        depthrange::Store(mapped, r);
        CHECK(mapped[0] == 9 && mapped[1] == 8 && mapped[2] == 7 && mapped[3] == 4);
    }
    std::printf("depth_source_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
