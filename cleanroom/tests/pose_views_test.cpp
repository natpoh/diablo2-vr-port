// pose_views_test.cpp - vr/body/pose_views.h: every pose writer walked through the views 1 - 4, VR and flat. VR F1 / F3: none; VR F2 / F4: all; flat: all
// but the legs in view 1 and 3.
#include "../../vr/body/pose_views.h"

#include <cstdio>

namespace {
int g_fail = 0;
#define CHECK(c)                                                    \
    do {                                                            \
        if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } \
    } while (0)
const char* kNames[poseviews::kWriters] = {"hero body", "hero legs", "our pose copy", "hero gear", "monster doll", "remote hero"};
}  // namespace

int main() {
    for (int platform = 0; platform < 2; ++platform)
        for (int view = 1; view <= 4; ++view) {
            const bool vanilla = platform == 1 && (view == 1 || view == 3);
            CHECK(poseviews::Vanilla(platform, view) == vanilla);
            CHECK(poseviews::AnyAllowed(platform, view) == !vanilla);
            for (int w = 0; w < poseviews::kWriters; ++w) {
                const bool allowed = poseviews::Allowed(static_cast<poseviews::Writer>(w), platform, view);
                const bool want = !vanilla && !(w == poseviews::kHeroLegs && (view == 1 || view == 3));
                if (allowed != want) std::printf("  %s, %s view %d: %s\n", kNames[w], platform ? "VR" : "flat", view, allowed ? "writes" : "does not write");
                CHECK(allowed == want);
            }
        }
    CHECK(!poseviews::Allowed(poseviews::kWriters, 1, 4));   // (no such writer)
    std::printf(g_fail ? "pose_views_test: %d failed\n" : "pose_views_test: every writer through the views 1 - 4: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
