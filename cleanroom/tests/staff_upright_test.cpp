// vr/staff_upright.h: a staff held upright aims along the arm, blended with its own axis as it tilts ([hands] staff_upright_arm). Synthetic staffs and
// arms, no game, no headset. Run: build/Release/staff_upright_test.exe   (exit code 0 = all passed)

#include "../../vr/staff_upright.h"

#include <cmath>
#include <cstdio>
#include <limits>

using namespace staffupright;

static int g_failures = 0, g_checks = 0;

static void Expect(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s\n", what);
    }
}

static bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
static void Set(float v[3], float x, float y, float z) { v[0] = x; v[1] = y; v[2] = z; }
static bool Unit(const float d[3]) { return Near(Len3(d), 1.0f); }
static float Rad(float deg) { return deg * kPi / 180.0f; }
static float YawDeg(const float d[3]) { return std::atan2(d[0], d[2]) * 180.0f / kPi; }   // heading on the ground, 0 = +z
static float YawGap(float a, float b) { float g = std::fmod(std::fabs(a - b), 360.0f); return g > 180.0f ? 360.0f - g : g; }   // degrees between two headings (180 = -180)
static bool YawNear(float a, float b, float eps = 1e-2f) { return YawGap(a, b) <= eps; }
// a staff tilted `tilt` degrees off vertical (tip up) toward the heading `yaw` degrees (0 = +z, 90 = +x)
static void Tilted(float d[3], float tilt, float yaw) { Set(d, std::sin(Rad(tilt)) * std::sin(Rad(yaw)), std::cos(Rad(tilt)), std::sin(Rad(tilt)) * std::cos(Rad(yaw))); }

int main() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    // ---- how far off vertical ----
    {
        float d[3];
        Set(d, 0, 3, 0); Expect(Near(OffVerticalDeg(d), 0.0f), "straight up: 0 degrees off vertical (any length)");
        Set(d, 0, -1, 0); Expect(Near(OffVerticalDeg(d), 0.0f), "straight down: 0 too (either end up)");
        Set(d, 1, 0, 0); Expect(Near(OffVerticalDeg(d), 90.0f), "flat: 90");
        Set(d, 0, 1, 1); Expect(Near(OffVerticalDeg(d), 45.0f, 1e-3f), "half way: 45");
        Set(d, 0, 0, 0); Expect(OffVerticalDeg(d) < 0.0f, "no direction: -1");
        Set(d, nan, 1, 0); Expect(OffVerticalDeg(d) < 0.0f, "not a number: -1");
    }
    // ---- the arm's share ----
    {
        float d[3];
        Tilted(d, 0, 0); Expect(Near(ArmWeight(d, kFullDeg, kNoneDeg), 1.0f), "upright: the arm alone");
        Tilted(d, kFullDeg - 0.5f, 90); Expect(Near(ArmWeight(d, kFullDeg, kNoneDeg), 1.0f), "inside the full angle: the arm alone");
        Tilted(d, (kFullDeg + kNoneDeg) / 2, 90); Expect(Near(ArmWeight(d, kFullDeg, kNoneDeg), 0.5f, 1e-3f), "half way between the two angles: half");
        Tilted(d, kNoneDeg + 0.5f, 90); Expect(Near(ArmWeight(d, kFullDeg, kNoneDeg), 0.0f), "past the none angle: the staff alone");
        Set(d, 0, 0, -1); Expect(Near(ArmWeight(d, kFullDeg, kNoneDeg), 0.0f), "levelled out like a wand: the staff alone");
        Set(d, 0, -1, 0.05f); Expect(Near(ArmWeight(d, kFullDeg, kNoneDeg), 1.0f), "held tip down, near vertical: the arm too");
        float last = 2.0f;
        bool falls = true;
        for (float t = 0; t <= 90; t += 0.25f) {
            Tilted(d, t, 30);
            const float w = ArmWeight(d, kFullDeg, kNoneDeg);
            if (w > last + 1e-6f || w < 0.0f || w > 1.0f) falls = false;
            last = w;
        }
        Expect(falls, "the share only falls as the staff tilts, within 0..1");
        Tilted(d, 25, 0);
        Expect(Near(ArmWeight(d, 20, 20), 0.0f) && Near(ArmWeight(d, 30, 20), 1.0f), "none at or below full: a step at the full angle");
        Set(d, 0, 0, 0); Expect(Near(ArmWeight(d, kFullDeg, kNoneDeg), 0.0f), "no direction: no arm");
        Tilted(d, 0, 0); Expect(Near(ArmWeight(d, nan, kNoneDeg), 0.0f), "angles not numbers: no arm");
    }
    // ---- the arm ----
    {
        float s[3], h[3], a[3];
        Set(s, 0.17f, -0.22f, 0.0f);
        Set(h, 0.25f, -0.45f, -0.55f);
        Expect(Arm(s, h, a) && Unit(a) && a[2] < 0.0f && a[1] < 0.0f, "shoulder to hand: a unit line ahead and down");
        Set(h, 0.2f, -0.3f, -0.05f);
        Expect(!Arm(s, h, a), "a hand 10 cm from the shoulder: no line");
        Set(h, 0.18f, 0.45f, -0.02f);
        Expect(!Arm(s, h, a), "the arm straight up: no heading");
        Set(h, nan, -0.45f, -0.55f);
        Expect(!Arm(s, h, a), "not a number: no line");
    }
    // ---- the blend ----
    {
        float arm[3], staff[3], out[3];
        Set(arm, 0.0f, -0.3f, 0.954f);
        const float al = Len3(arm);
        for (float& v : arm) v /= al;
        Tilted(staff, 2, 120);
        Expect(Blend(staff, arm, 1.0f, out) && Near(out[0], arm[0]) && Near(out[1], arm[1]) && Near(out[2], arm[2]), "share 1: the arm's line exactly");
        Tilted(staff, 40, 90);
        Expect(Blend(staff, arm, 0.0f, out) && Near(out[0], staff[0]) && Near(out[1], staff[1]) && Near(out[2], staff[2]), "share 0: the staff's line exactly");
        Set(staff, 2.0f, 0.0f, 0.0f);
        Expect(Blend(staff, arm, 0.0f, out) && Unit(out) && Near(out[0], 1.0f), "  made unit (the staff is any length)");
        Set(staff, 1.0f, 0.0f, 0.0f);
        Expect(Blend(staff, arm, 0.5f, out) && Unit(out) && Near(YawDeg(out), 45.0f, 1e-2f), "share 0.5: the heading half way between the arm's (0) and the staff's (90)");
        Expect(Near(std::asin(out[1]), 0.5f * std::atan2(arm[1], std::sqrt(arm[0] * arm[0] + arm[2] * arm[2])), 1e-3f), "  the slope half way too");
        // the shorter way round: the arm at +170, the staff at -170 - half way is 180, not 0
        Set(arm, std::sin(Rad(170)), 0, std::cos(Rad(170)));
        Set(staff, std::sin(Rad(-170)), 0, std::cos(Rad(-170)));
        Expect(Blend(staff, arm, 0.5f, out) && Near(std::fabs(YawDeg(out)), 180.0f, 1e-2f), "headings either side of behind: half way is behind, the shorter way");
        Set(arm, 0, 0, 1);
        Set(staff, 0, 1, 0);
        Expect(Blend(staff, arm, 0.5f, out) && Unit(out) && Near(YawDeg(out), 0.0f), "a staff exactly upright has no heading: the arm's, even at half");
        Set(staff, nan, 1, 0);
        Expect(!Blend(staff, arm, 0.5f, out), "not a number: no line");
        Set(staff, 1, 0, 0);
        Set(arm, 0, 1, 0);
        Expect(!Blend(staff, arm, 0.5f, out), "an arm with no heading: no line");
    }
    // ---- the user's case: the Sorceress's staff tilted toward a side sends the attack that way, upright it follows the arm ----
    {
        float arm[3], staff[3], out[3];
        Set(arm, 0.0f, -0.25f, -0.97f);   // the arm ahead (-z) and a little down
        const float al = Len3(arm);
        for (float& v : arm) v /= al;
        Tilted(staff, 0, 0);
        Expect(Blend(staff, arm, ArmWeight(staff, kFullDeg, kNoneDeg), out) && YawNear(YawDeg(out), YawDeg(arm)), "upright: where the arm points");
        Tilted(staff, 5, 90);
        Expect(Blend(staff, arm, ArmWeight(staff, kFullDeg, kNoneDeg), out) && YawNear(YawDeg(out), YawDeg(arm)), "a wobble of 5 degrees to the right: still the arm");
        Tilted(staff, 20, 90);   // the arm at 180 (-z), the staff leaning toward +x (90): half way, the shorter way round, is 135
        Expect(Blend(staff, arm, ArmWeight(staff, kFullDeg, kNoneDeg), out) && Near(YawDeg(out), 135.0f, 0.5f), "tilted 20 degrees to the right: half way to the right");
        Tilted(staff, 35, 90);
        Expect(Blend(staff, arm, ArmWeight(staff, kFullDeg, kNoneDeg), out) && Near(YawDeg(out), 90.0f, 1e-2f), "tilted 35 degrees to the right: to the right, as the staff leans");
        Tilted(staff, 35, -90);
        Expect(Blend(staff, arm, ArmWeight(staff, kFullDeg, kNoneDeg), out) && Near(YawDeg(out), -90.0f, 1e-2f), "tilted to the left: to the left");
        // smooth: tilting from upright to flat toward the right, the heading never jumps
        float lastYaw = 180.0f, worst = 0.0f;
        for (float t = 0; t <= 90; t += 0.25f) {
            Tilted(staff, t, 90);
            if (!Blend(staff, arm, ArmWeight(staff, kFullDeg, kNoneDeg), out)) { worst = 999.0f; break; }
            const float y = YawDeg(out);
            worst = std::fmax(worst, YawGap(y, lastYaw));
            lastYaw = y;
        }
        Expect(worst < 2.0f, "tilting a quarter degree at a time moves the heading by under 2 degrees - no jump (d2r-pcvr's switch jumped 90 here)");
    }

    if (g_failures) {
        std::printf("staff_upright_test: %d of %d checks FAILED\n", g_failures, g_checks);
        return 1;
    }
    std::printf("staff_upright_test: all %d checks passed\n", g_checks);
    return 0;
}
