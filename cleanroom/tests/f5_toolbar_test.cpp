// Offline checks of vr/f5_toolbar.h: VR F5's toolbar low in front ([hud] f5_toolbar) - the height and distance sliders turned into the look's
// bar_side_cm / bar_lift_cm land the strip where they say, the defaults are "Low in front" itself, and it is only wanted in F5 with the box.
// No game, no window. Run: build/Release/f5_toolbar_test.exe (exit code 0 = passed)

#include <cmath>
#include <cstdio>

#include "../../vr/f5_toolbar.h"
#include "../../vr/ar_view.h"
#include "../../vr/f5_hud.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static bool Near(float a, float b, float eps = 0.01f) { return std::fabs(a - b) < eps; }

static void TestDefaultsAreLowInFront() {   // the sliders at "Low in front"'s own 32 / 55: exactly F4's place, nothing moved
    const f5bar::Offset o = f5bar::OffsetFor(f5bar::kBelowCm, f5bar::kAheadCm);
    CHECK(Near(o.sideCm, 0.0f) && Near(o.liftCm, 0.0f));
    const f5bar::At at = f5bar::Placed(o);
    CHECK(Near(at.upCm, -32.0f) && Near(at.backCm, -55.0f));
}

static void TestSergsDefaults() {   // the sliders' defaults (Serg 2026-11-19): 33 under the eyes, 32 ahead - inside the sliders, landing there
    CHECK(f5bar::kDefaultBelowCm == 33.0f && f5bar::kDefaultAheadCm == 32.0f);
    CHECK(f5bar::kDefaultBelowCm >= f5bar::kBelowMin && f5bar::kDefaultBelowCm <= f5bar::kBelowMax);
    CHECK(f5bar::kDefaultAheadCm >= f5bar::kAheadMin && f5bar::kDefaultAheadCm <= f5bar::kAheadMax);
    const f5bar::At at = f5bar::Placed(f5bar::OffsetFor(f5bar::kDefaultBelowCm, f5bar::kDefaultAheadCm));
    CHECK(Near(at.upCm, -33.0f) && Near(at.backCm, -32.0f));
}

static void TestSlidersLandWhereTheySay() {   // every height and distance on the sliders' grid: the strip's middle there
    for (float below = f5bar::kBelowMin; below <= f5bar::kBelowMax; below += 5.0f)
        for (float ahead = f5bar::kAheadMin; ahead <= f5bar::kAheadMax; ahead += 5.0f) {
            const f5bar::At at = f5bar::Placed(f5bar::OffsetFor(below, ahead));
            CHECK(Near(at.upCm, -below) && Near(at.backCm, -ahead));
        }
    // lower and nearer: both offsets move, height alone does not change the distance
    const f5bar::At low = f5bar::Placed(f5bar::OffsetFor(60.0f, 55.0f));
    CHECK(Near(low.backCm, -55.0f) && Near(low.upCm, -60.0f));
    const f5bar::At far = f5bar::Placed(f5bar::OffsetFor(32.0f, 90.0f));
    CHECK(Near(far.upCm, -32.0f) && Near(far.backCm, -90.0f));
}

static void TestClamped() {   // off the sliders' ends (a hand-edited ini, NaN): held to them
    const f5bar::At hi = f5bar::Placed(f5bar::OffsetFor(-50.0f, 500.0f));
    CHECK(Near(hi.upCm, -f5bar::kBelowMin) && Near(hi.backCm, -f5bar::kAheadMax));
    const f5bar::At nan = f5bar::Placed(f5bar::OffsetFor(NAN, NAN));
    CHECK(Near(nan.upCm, -f5bar::kBelowMin) && Near(nan.backCm, -f5bar::kAheadMin));
}

static void TestWanted() {   // only in VR F5 with the box ticked
    CHECK(!f5bar::Wanted(false, true));
    CHECK(!f5bar::Wanted(true, false));
    CHECK(!f5bar::Wanted(false, false));
    CHECK(f5bar::Wanted(true, true));
    for (int v = 1; v <= 4; ++v) CHECK(!f5bar::Wanted(true, arview::IsArView(1, v, true)));   // F1 - F4: untouched
    CHECK(!f5bar::Wanted(true, arview::IsArView(0, 5, true)));                               // the flat game: never
    CHECK(!f5bar::Wanted(true, arview::IsArView(1, 5, false)));                              // F5 off
    CHECK(f5bar::Wanted(true, arview::IsArView(1, 5, true)));
    CHECK(f5bar::kAnchor == 3);   // FlatVRGameHudLook.bar_anchor: low in front
}

static void TestMapAndAllies() {   // vr/f5_hud.h: [hud] f5_map / f5_allies
    using namespace f5hud;
    // the corner: off = flat F3's own choice untouched; on = "as in the game's options" becomes flat_map_corner's corner, a choice stays
    for (int place = 0; place <= 3; ++place)
        for (int corner = 0; corner <= 1; ++corner) CHECK(MapPlace(place, corner, false) == place);
    CHECK(MapPlace(0, 0, true) == 1 && MapPlace(0, 1, true) == 3);
    CHECK(MapPlace(1, 1, true) == 1 && MapPlace(2, 0, true) == 2 && MapPlace(3, 0, true) == 3);   // (Centre: the place key's - kept)
    // toward the middle: a right-corner map goes left, a left-corner one right; down is +y
    const Uv u = MapShiftUv(12.0f, 10.0f);
    CHECK(Near(u.x, -0.12f, 1e-5f) && Near(u.y, 0.10f, 1e-5f));
    CHECK(MapShiftX(u, false) < 0.0f && MapShiftX(u, true) > 0.0f && Near(MapShiftX(u, true), 0.12f, 1e-5f));
    const Uv big = MapShiftUv(500.0f, -500.0f), nan = MapShiftUv(NAN, NAN);
    CHECK(Near(big.x, -kShiftMax * 0.01f, 1e-5f) && Near(big.y, kShiftMin * 0.01f, 1e-5f) && nan.x == 0.0f && nan.y == 0.0f);
    // the depth: [hud_floor] map_near's units (0.005 uv a step, + nearer), held to -10 .. 10
    CHECK(Near(MapNearUv(2.0f), 0.01f, 1e-6f) && Near(MapNearUv(50.0f), 0.05f, 1e-6f) && MapNearUv(0.0f) == 0.0f);
    // the allies: right +x, down = -y (allies_y's + is up)
    const Uv a = AlliesOff(12.0f, 10.0f);
    CHECK(Near(a.x, 0.12f, 1e-5f) && Near(a.y, -0.10f, 1e-5f));
    // the defaults inside the sliders, moving the pieces into the view (in, down, nearer)
    CHECK(kMapInDefault > 0.0f && kMapDownDefault > 0.0f && kMapNearDefault > 0.0f && kAlliesRightDefault > 0.0f && kAlliesDownDefault > 0.0f);
    CHECK(kMapInDefault <= kShiftMax && kAlliesDownDefault <= kShiftMax && kMapNearDefault <= kNearMax);
    // the size: % of the corner map's size now - 100 leaves [hud_inside] map_zoom as it is; held to the slider and to the effect's 0.3 - 3
    const float insides[] = {0.3f, 0.9f, 1.0f, 2.0f, 3.0f};
    for (float inside : insides) CHECK(Near(MapZoom(inside, kMapSizeDefault), inside, 1e-5f));
    CHECK(kMapSizeDefault == 100.0f && Near(MapZoom(0.9f, 200.0f), 1.8f, 1e-5f) && Near(MapZoom(0.9f, 50.0f), 0.45f, 1e-5f));
    CHECK(Near(MapZoom(1.0f, 1000.0f), 3.0f, 1e-5f) && Near(MapZoom(1.0f, 1.0f), 0.3f, 1e-5f) && Near(MapZoom(2.0f, 300.0f), 3.0f, 1e-5f));
    CHECK(Near(MapZoom(0.9f, NAN), 0.9f, 1e-5f));
    // wanted only in VR F5 with its box
    CHECK(Wanted(true, arview::IsArView(1, 5, true)) && !Wanted(false, arview::IsArView(1, 5, true)));
    for (int v = 1; v <= 4; ++v) CHECK(!Wanted(true, arview::IsArView(1, v, true)));
    CHECK(!Wanted(true, arview::IsArView(0, 3, true)));
}

int main() {
    TestDefaultsAreLowInFront();
    TestSergsDefaults();
    TestSlidersLandWhereTheySay();
    TestClamped();
    TestWanted();
    TestMapAndAllies();
    std::printf("f5_toolbar_test: %d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures ? 1 : 0;
}
