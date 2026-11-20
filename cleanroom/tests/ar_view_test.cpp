// Offline checks of vr/ar_view.h: VR F5 for AR glasses ([mode] f5_ar_view) - which views there are, what a vr_mode=5 reads as, where the
// flat game's rules hold, what FlatVR's Head Lock gets - and, with vr/view_keys.h, which F key picks F5. No game, no window.
// Run: build/Release/ar_view_test.exe (exit code 0 = passed)

#include <cstdio>

#include "../../vr/ar_view.h"
#include "../../vr/body/pose_views.h"
#include "../../vr/attack_view.h"
#include "../../vr/view_keys.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

constexpr unsigned F4 = 0x73, F5 = 0x74;

static void TestOffIsAsBefore() {   // f5_ar_view=0: exactly the old views
    CHECK(arview::ViewCount(1, false) == 4);
    CHECK(arview::ViewCount(0, false) == 3);
    for (int v = 1; v <= 4; ++v) CHECK(arview::ClampVrMode(v, false) == v);
    CHECK(arview::ClampVrMode(5, false) == 4);    // a vr_mode=5 left in the ini: F4
    CHECK(arview::ClampVrMode(0, false) == 1);
    CHECK(arview::ClampVrMode(6, false) == 1);
    for (int v = 1; v <= 5; ++v) {
        CHECK(!arview::IsArView(1, v, false));
        CHECK(!arview::FlatRules(1, v, false));                // VR keeps VR's rules in every view
        CHECK(arview::FlatRulesView(1, 3, v, false) == 0);
        const arview::Rules r = arview::RulesFor(1, v, v, false);
        CHECK(r.platform == 1 && r.view == v);
    }
    for (int fv = 1; fv <= 3; ++fv) {                           // the flat game as before
        CHECK(arview::FlatRules(0, 4, false) && arview::FlatRules(0, 4, true));
        CHECK(arview::FlatRulesView(0, fv, 4, false) == fv && arview::FlatRulesView(0, fv, 5, true) == fv);
        CHECK(!arview::IsArView(0, 5, true));                   // flat never has F5, even ticked
        const arview::Rules r = arview::RulesFor(0, fv, 5, true);
        CHECK(r.platform == 0 && r.view == fv);
    }
    CHECK(arview::ViewCount(0, true) == 3);
}

static void TestOn() {
    CHECK(arview::ViewCount(1, true) == 5);
    CHECK(arview::ClampVrMode(5, true) == 5);
    for (int v = 1; v <= 4; ++v) {
        CHECK(arview::ClampVrMode(v, true) == v);
        CHECK(!arview::IsArView(1, v, true));                   // F1 - F4 untouched with the box ticked
        CHECK(!arview::FlatRules(1, v, true));
        const arview::Rules r = arview::RulesFor(1, v, v, true);
        CHECK(r.platform == 1 && r.view == v);
    }
    CHECK(arview::IsArView(1, 5, true));
    CHECK(arview::FlatRules(1, 5, true));
    CHECK(arview::FlatRulesView(1, 1, 5, true) == 3);           // F5 is flat F3 whatever flat_view says
    const arview::Rules r = arview::RulesFor(1, 5, 5, true);
    CHECK(r.platform == 0 && r.view == 3);
}

static void TestPoseAndAimAsFlatF3() {   // the pure helpers fed RulesFor give F5 exactly flat F3's answers
    const arview::Rules f5 = arview::RulesFor(1, 5, 5, true);
    CHECK(!poseviews::Vanilla(f5.platform, f5.view));
    CHECK(poseviews::AnyAllowed(f5.platform, f5.view) == poseviews::AnyAllowed(0, 3));
    for (int w = 0; w < poseviews::kWriters; ++w)
        CHECK(poseviews::Allowed((poseviews::Writer)w, f5.platform, f5.view) == poseviews::Allowed((poseviews::Writer)w, 0, 3));
    CHECK(!poseviews::Allowed(poseviews::kHeroLegs, f5.platform, f5.view));   // legs_rear off, as flat F3
    CHECK(attackview::HandMayAim(f5.platform, 5));             // the flat game: not touched
}

static void TestLinkHeadLock() {   // FlatVR's Head Lock: 0 - 4 as before, F5 never
    using arview::LinkHeadLock;
    CHECK(!LinkHeadLock(0, false) && LinkHeadLock(1, false) && !LinkHeadLock(2, false) && LinkHeadLock(3, false) && LinkHeadLock(4, false));
    for (unsigned m = 0; m <= 5; ++m) CHECK(!LinkHeadLock(m, true));   // a flat menu: never
    CHECK(!LinkHeadLock(arview::kLinkViewMode, false));
    CHECK(arview::kLinkViewMode == 5);
}

static void TestKeys() {   // F5 picks a view only with the box ticked, through the same [view] keys modes as F1 - F4
    using namespace viewkeys;
    const int on = arview::ViewCount(1, true), off = arview::ViewCount(1, false);
    CHECK(!Switches(kPlain, 4, off, false) && !Switches(kShift, 4, off, true));   // off: F5 is the game's
    CHECK(Switches(kPlain, 4, on, false));
    CHECK(!Switches(kShift, 4, on, false) && Switches(kShift, 4, on, true));
    CHECK(!Switches(kNone, 4, on, true));
    CHECK(!Switches(kPlain, 4, arview::ViewCount(0, true), false));            // flat: never
    Gate g;   // the game window's messages
    CHECK(!g.Take(kPlain, off, F5, true, false, false));                       // off: the game gets F5
    CHECK(!g.Take(kPlain, off, F5, false, false, false));
    CHECK(g.Take(kPlain, on, F5, true, false, false));                         // on, keys=0: F5 ours
    CHECK(g.Take(kPlain, on, F5, false, false, false));
    Gate s;
    CHECK(!s.Take(kShift, on, F5, true, false, false));                        // keys=1: a plain F5 the game's skill key
    CHECK(!s.Take(kShift, on, F5, false, false, false));
    CHECK(s.Take(kShift, on, F5, true, false, true));                          // Shift + F5 ours, its release too
    CHECK(s.Take(kShift, on, F5, false, false, false));
    CHECK(s.Take(kShift, on, F4, true, false, true));                          // F4 as before
}

static void TestHeadLook() {   // [mode] f5_head_look: F5's camera on the head as F4's, F3's controls
    using arview::NativeF5;
    using arview::NativeF5Mode;
    CHECK(NativeF5Mode(true) == NativeF5::HeadCamera);    // on: the head's camera (native: the headset's eyes; FlatVR: Head Lock)
    CHECK(NativeF5Mode(false) == NativeF5::Flat);         // off: the mouse only, the flat picture on native
    CHECK(arview::LinkViewMode(true) == 6 && arview::LinkViewMode(false) == 5);
    CHECK(arview::LinkHeadLock(arview::LinkViewMode(true), false));    // FlatVR's Head Lock on with the head
    CHECK(!arview::LinkHeadLock(arview::LinkViewMode(true), true));    // (a flat menu: off, as every view)
    CHECK(!arview::LinkHeadLock(arview::LinkViewMode(false), false));
    CHECK(arview::IsLinkF5(5) && arview::IsLinkF5(6) && !arview::IsLinkF5(4) && !arview::IsLinkF5(1));
}

int main() {
    TestHeadLook();
    TestOffIsAsBefore();
    TestOn();
    TestPoseAndAimAsFlatF3();
    TestLinkHeadLock();
    TestKeys();
    std::printf("ar_view_test: %d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures ? 1 : 0;
}
