// Offline checks of vr/f2_map.h: F2's corner map moved and sized ([hud_third] map_x / map_y / map_size) - the sliders turned into the effect's shift
// and zoom, nothing moved at the defaults, the box held inside the picture whatever the sliders say, and only in F2.
// No game, no window. Run: build/Release/f2_map_test.exe (exit code 0 = passed)

#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>

#include "../../vr/f2_map.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }
static const float kNan = std::numeric_limits<float>::quiet_NaN();

static void TestDefaultsMoveNothing() {   // 0 / 0 / 100: the game's own map, not taken out
    CHECK(!f2map::Moved(0.0f, 0.0f, f2map::kSizeDefault));
    const f2map::Uv u = f2map::ShiftUv(0.0f, 0.0f);
    CHECK(Near(u.x, 0.0f) && Near(u.y, 0.0f));
    CHECK(Near(f2map::Zoom(f2map::kSizeDefault), 1.0f));
    CHECK(!f2map::Moved(kNan, kNan, kNan));   // a broken ini line reads as the default
    CHECK(f2map::Moved(1.0f, 0.0f, 100.0f) && f2map::Moved(0.0f, 1.0f, 100.0f) && f2map::Moved(0.0f, 0.0f, 101.0f) && f2map::Moved(0.0f, 0.0f, 99.0f));
}

static void TestShiftDirections() {   // toward the middle = left for the top right corner's map (hud.cpp turns it round for the left corner); down = +y
    const f2map::Uv u = f2map::ShiftUv(20.0f, 15.0f);
    CHECK(Near(u.x, -0.20f) && Near(u.y, 0.15f));
    const f2map::Uv back = f2map::ShiftUv(-5.0f, -5.0f);
    CHECK(Near(back.x, 0.05f) && Near(back.y, -0.05f));
}

static void TestSliderEnds() {   // the ini held to the sliders' ends
    const f2map::Uv far = f2map::ShiftUv(500.0f, 500.0f), near = f2map::ShiftUv(-500.0f, -500.0f);
    CHECK(Near(far.x, -0.01f * f2map::kShiftMax) && Near(far.y, 0.01f * f2map::kShiftMax));
    CHECK(Near(near.x, -0.01f * f2map::kShiftMin) && Near(near.y, 0.01f * f2map::kShiftMin));
    CHECK(Near(f2map::Zoom(1000.0f), 3.0f) && Near(f2map::Zoom(1.0f), 0.3f) && Near(f2map::Zoom(150.0f), 1.5f));
    CHECK(Near(f2map::Clamp(kNan, 0.0f, 1.0f), 0.0f));
    // inside the effect's own clamp (hud::SetPictureZoom: 0.3 - 3) and hud::SetPictureMapOffset's (-1 - 1)
    CHECK(f2map::Zoom(f2map::kSizeMin) >= 0.3f && f2map::Zoom(f2map::kSizeMax) <= 3.0f && f2map::kShiftMax <= 100.0f && f2map::kShiftMin >= -100.0f);
}

// The box as hud.cpp's Publish has it for the game's top right map (uv l, t, r, b), the shift as it goes on the screen.
static void Shifted(const float box[4], float dx, float dy, float out[4]) {
    f2map::KeepOnScreen(box, &dx, &dy);
    out[0] = box[0] + dx; out[2] = box[2] + dx; out[1] = box[1] + dy; out[3] = box[3] + dy;
}
static bool OnScreen(const float b[4]) { return b[0] >= -1e-5f && b[1] >= -1e-5f && b[2] <= 1.0f + 1e-5f && b[3] <= 1.0f + 1e-5f; }

static void TestStaysOnScreen() {
    const float topRight[4] = {0.80f, 0.02f, 0.98f, 0.30f};   // the game's corner map, near the edge
    float out[4];
    {   // a small move: as asked
        const f2map::Uv u = f2map::ShiftUv(20.0f, 10.0f);
        Shifted(topRight, u.x, u.y, out);
        CHECK(Near(out[0], 0.60f) && Near(out[1], 0.12f) && OnScreen(out));
    }
    {   // the sliders' far end: held inside (left edge at 0 at most, bottom at 1 at most)
        const f2map::Uv u = f2map::ShiftUv(f2map::kShiftMax, f2map::kShiftMax);
        Shifted(topRight, u.x, u.y, out);
        CHECK(OnScreen(out) && Near(out[1], 0.72f) && Near(out[3], 1.0f) && Near(out[0], 0.0f));
    }
    {   // pushed past the corner (the -10 end): held at the edge
        const f2map::Uv u = f2map::ShiftUv(f2map::kShiftMin, f2map::kShiftMin);
        Shifted(topRight, u.x, u.y, out);
        CHECK(OnScreen(out) && Near(out[2], 1.0f) && Near(out[1], 0.0f));
    }
    {   // the left corner's map: hud.cpp turns x round (toward the middle = right); held at the right edge
        const float topLeft[4] = {0.02f, 0.02f, 0.20f, 0.30f};
        const f2map::Uv u = f2map::ShiftUv(f2map::kShiftMax, 0.0f);
        Shifted(topLeft, -u.x, u.y, out);
        CHECK(OnScreen(out) && Near(out[2], 1.0f) && Near(out[0], 0.82f));
    }
    {   // grown three times from its corner (wider than the room left): still inside
        const float big[4] = {0.44f, 0.02f, 0.98f, 0.86f};
        const f2map::Uv u = f2map::ShiftUv(0.0f, 30.0f);
        Shifted(big, u.x, u.y, out);
        CHECK(OnScreen(out) && Near(out[3], 1.0f) && Near(out[0], 0.44f));
    }
    {   // a box larger than the picture: centred on it, not flung off one side
        const float huge[4] = {-0.2f, -0.1f, 1.4f, 1.3f};
        Shifted(huge, -0.5f, 0.5f, out);
        CHECK(Near(out[0] + out[2], 1.0f) && Near(out[1] + out[3], 1.0f));
    }
    {   // a not-a-number shift: nothing moved
        float dx = kNan, dy = kNan;
        f2map::KeepOnScreen(topRight, &dx, &dy);
        CHECK(Near(dx, 0.0f) && Near(dy, 0.0f));
    }
    // a sweep: every slider value, every size, both corners - never off the screen
    int off = 0;
    for (float in = f2map::kShiftMin; in <= f2map::kShiftMax; in += 5.0f)
        for (float down = f2map::kShiftMin; down <= f2map::kShiftMax; down += 5.0f)
            for (float z : {0.3f, 1.0f, 2.0f, 3.0f})
                for (int left = 0; left < 2; ++left) {
                    const float w = 0.18f * z, h = 0.28f * z;
                    float box[4];
                    if (left) { box[0] = 0.02f; box[2] = 0.02f + w; } else { box[2] = 0.98f; box[0] = 0.98f - w; }
                    box[1] = 0.02f; box[3] = 0.02f + h;
                    const f2map::Uv u = f2map::ShiftUv(in, down);
                    Shifted(box, left ? -u.x : u.x, u.y, out);
                    if (!OnScreen(out)) ++off;
                }
    CHECK(off == 0);
}

static void TestOnlyInF2() {   // vrcam's BarOffsetNow order: our camera on, third person; not VR F1 through our camera, not VR F3 (run as view 2), not F5
    CHECK(f2map::IsF2(true, false, false, true, false));
    CHECK(!f2map::IsF2(false, false, false, true, false));   // our camera off: F1, the game's own
    CHECK(!f2map::IsF2(true, true, false, true, false));     // VR F1 through our camera
    CHECK(!f2map::IsF2(true, false, true, true, false));     // VR F3 (ApplyView runs it as view 2)
    CHECK(!f2map::IsF2(true, false, false, false, false));   // first person
    CHECK(!f2map::IsF2(true, false, false, true, true));     // VR F5
}

// [hud_third] allies_x / allies_y: VR F2's allies' portraits moved right and down, as F5's f5_allies_x / _y; 0 / 0 = every view's allies_x / allies_y
static void TestAllies() {
    const float kNan = std::numeric_limits<float>::quiet_NaN();
    CHECK(!f2map::AlliesMoved(0.0f, 0.0f));
    CHECK(!f2map::AlliesMoved(kNan, kNan));   // a broken ini line reads as 0: as before
    CHECK(f2map::AlliesMoved(33.0f, 0.0f) && f2map::AlliesMoved(0.0f, 33.0f) && f2map::AlliesMoved(-5.0f, 0.0f));
    f2map::Uv u = f2map::AlliesOff(33.0f, 33.0f);   // Serg's arrow: about a third across and a third down
    CHECK(Near(u.x, 0.33f) && Near(u.y, -0.33f));   // (the effect's AlliesOff: x + right, y + up)
    u = f2map::AlliesOff(500.0f, -500.0f);          // held to the sliders' ends
    CHECK(Near(u.x, f2map::kAlliesMax * 0.01f) && Near(u.y, -f2map::kAlliesMin * 0.01f));
    u = f2map::AlliesOff(0.0f, 0.0f);
    CHECK(Near(u.x, 0.0f) && Near(u.y, 0.0f));
}

int main() {
    TestDefaultsMoveNothing();
    TestShiftDirections();
    TestSliderEnds();
    TestStaysOnScreen();
    TestOnlyInF2();
    TestAllies();
    std::printf("f2_map_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
