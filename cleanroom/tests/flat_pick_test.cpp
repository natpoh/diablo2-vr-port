// The flat mouse look's targeting log ([debug] flat_pick, vr/flat_pick.h): the pixel's distance from the middle, the ground the ray meets, the slip.
#include "flat_pick.h"

#include <cmath>
#include <cstdio>

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; }
}
static bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

int main() {
    using namespace flatpick;
    // the pixel
    Check(Near(OffCentre(960.0f, 540.0f, 1920.0f, 1080.0f), 0.0f), "the middle is 0");
    Check(AtCentre(960.0f, 540.0f, 1920.0f, 1080.0f), "the middle is the crosshair's");
    Check(AtCentre(961.0f, 539.5f, 1920.0f, 1080.0f), "a pixel of rounding is still the middle");
    Check(Near(OffCentre(0.0f, 540.0f, 1920.0f, 1080.0f), 1.0f), "the left edge is 1");
    Check(Near(OffCentre(960.0f, 810.0f, 1920.0f, 1080.0f), 0.5f), "halfway down is 0.5");
    Check(!AtCentre(1100.0f, 540.0f, 1920.0f, 1080.0f), "140 px right is not the middle");
    // the client's middle against a viewport of another size (a render scale): not the middle
    Check(!AtCentre(960.0f, 540.0f, 1280.0f, 720.0f), "the window's middle in a smaller viewport is off");
    Check(OffCentre(1.0f, 1.0f, 0.0f, 720.0f) < 0.0f && !AtCentre(1.0f, 1.0f, 0.0f, 720.0f), "no viewport: unknown");

    // the ground
    const float eye[3] = {0.0f, 1.7f, 0.0f};
    const float down45[3] = {0.0f, -1.0f, 1.0f};
    Ahead a = GroundAhead(eye, down45, 0.0f);
    Check(a.hit && Near(a.along, 1.7f) && Near(a.downDeg, 45.0f, 0.01f), "45 degrees down from 1.7 lands 1.7 ahead");
    const float straight[3] = {0.0f, -1.0f, 0.0f};
    a = GroundAhead(eye, straight, 0.0f);
    Check(a.hit && Near(a.along, 0.0f) && Near(a.downDeg, 90.0f, 0.01f), "straight down lands under the eye");
    const float level[3] = {1.0f, 0.0f, 0.0f};
    a = GroundAhead(eye, level, 0.0f);
    Check(!a.hit && Near(a.downDeg, 0.0f, 0.01f), "level never lands");
    const float up[3] = {1.0f, 0.5f, 0.0f};
    a = GroundAhead(eye, up, 0.0f);
    Check(!a.hit && a.downDeg < 0.0f, "up never lands, the angle negative");
    a = GroundAhead(eye, down45, 2.0f);
    Check(!a.hit, "a ground above the eye is never met going down");
    const float shallow[3] = {std::cos(10.0f / 57.2957795f), -std::sin(10.0f / 57.2957795f), 0.0f};
    a = GroundAhead(eye, shallow, 0.0f);
    Check(a.hit && Near(a.along, 1.7f / std::tan(10.0f / 57.2957795f), 1e-2f) && Near(a.downDeg, 10.0f, 0.01f), "10 degrees down lands ~9.6 ahead");
    const float zero[3] = {0.0f, 0.0f, 0.0f};
    Check(!GroundAhead(eye, zero, 0.0f).hit, "no direction: no hit");

    // the slip
    Check(Near(SlipPerUnit(90.0f), 0.0f), "straight down does not slip");
    Check(Near(SlipPerUnit(45.0f), 1.0f), "45 degrees slips one for one");
    Check(Near(SlipPerUnit(10.0f), 5.671f, 1e-2f), "10 degrees slips ~5.7 times");
    Check(SlipPerUnit(0.0f) == 0.0f && SlipPerUnit(-5.0f) == 0.0f, "level or up: none");

    if (g_fail) { std::printf("flat_pick_test: %d FAILED\n", g_fail); return 1; }
    std::printf("flat_pick_test: all passed\n");
    return 0;
}
