// Offline checks of vr/panel_grab.h ([openxr] panel_grab): a game panel in VR F4 taken by a grip that starts on it, carried with the hand,
// pulled nearer / pushed farther, kept where it was let go. No OpenXR, no headset. Run: panel_grab_test.exe (exit 0 = all passed).
#include <cmath>
#include <cstdio>

#include "../../vr/panel_grab.h"
#include "../../vr/menu_zoom.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

using namespace panelgrab;
static bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

// The standing player: eyes 1.7 m up at the origin, looking along -z; the panel 1.6 m ahead, 2.4 m x 1.35 m (flat_width_m's default).
static In Scene() {
    In in;
    in.applies = true;
    in.eyes = {0.0f, 1.7f, 0.0f};
    in.panel = FaceEyes({0.0f, 1.7f, -1.6f}, in.eyes);
    in.widthM = 2.4f; in.heightM = 1.35f;
    in.dtS = 1.0f / 90.0f;
    for (Hand& h : in.hand) { h.tracked = true; h.dir = {0.0f, 0.0f, -1.0f}; }
    in.hand[0].at = {-0.2f, 1.3f, -0.35f};
    in.hand[1].at = {0.2f, 1.3f, -0.35f};
    return in;
}

// the grip counts where menu_zoom (the direct layout) counts it
static_assert(kGripOn == menuzoom::kGripOn && kGripOff == menuzoom::kGripOff, "grip thresholds");
static_assert(kTriggerOn == menuzoom::kTriggerOn && kTriggerOff == menuzoom::kTriggerOff, "trigger thresholds");
static_assert(kGripTapMs == menuzoom::kGripTapMs, "tap pulse");
static_assert(kPushGain > 1.0f && kMinM < 0.5f && kMaxM >= 4.0f, "push and ends");

static void TestFaceAndRay() {
    const V3 eyes{0.0f, 1.7f, 0.0f};
    const Panel p = FaceEyes({0.0f, 1.7f, -1.6f}, eyes);
    CHECK(p.ok && Near(p.front.z, 1.0f) && Near(p.up.y, 1.0f) && Near(p.right.x, 1.0f));
    float t = 0.0f, lx = 0.0f, ly = 0.0f;
    CHECK(RayPanel({0.2f, 1.3f, -0.35f}, {0.0f, 0.0f, -1.0f}, p, 2.4f, 1.35f, &t, &lx, &ly));
    CHECK(Near(t, 1.25f) && Near(lx, 0.2f) && Near(ly, -0.4f));
    CHECK(!RayPanel({0.2f, 1.3f, -0.35f}, {0.0f, 0.0f, 1.0f}, p, 2.4f, 1.35f, &t, &lx, &ly));        // pointing away
    CHECK(!RayPanel({0.2f, 1.3f, -0.35f}, Unit({1.0f, 0.0f, -0.3f}), p, 2.4f, 1.35f, &t, &lx, &ly));  // off its side
    CHECK(Touches({0.5f, 1.5f, -1.55f}, p, 2.4f, 1.35f) && !Touches({0.5f, 1.5f, -1.4f}, p, 2.4f, 1.35f) && !Touches({1.5f, 1.5f, -1.6f}, p, 2.4f, 1.35f));
    // a panel low and near turns up to the eyes (pitch), never rolls
    const Panel low = FaceEyes({0.0f, 1.2f, -0.5f}, eyes);
    CHECK(low.front.y > 0.5f && Near(low.right.y, 0.0f) && Near(Dot(low.up, low.front), 0.0f));
}

static void TestKept() {
    const V3 eyes{0.3f, 1.6f, -0.2f};
    const float yaw = 0.4f;
    Kept k{0.7f, 0.3f, -0.25f, true};
    const Panel p = PlaceKept(eyes, yaw, k);
    const Kept back = Relative(p.at, eyes, yaw);
    CHECK(Near(back.distM, 0.7f) && Near(back.sideRad, 0.3f) && Near(back.upM, -0.25f));
    CHECK(InView(k));
    CHECK(!InView(Kept{0.7f, 1.2f, 0.0f, true}));    // 69 degrees aside
    CHECK(!InView(Kept{0.7f, 0.0f, -1.7f, true}));   // under the floor
    CHECK(!InView(Kept{5.0f, 0.0f, 0.0f, true}));    // past the far end
    CHECK(!InView(Kept{0.1f, 0.0f, 0.0f, true}));    // inside the head
    CHECK(!InView(Kept{}));                          // nothing kept
}

static void TestWidth() {
    CHECK(Near(WidthAt(1.6f, 2.4f, 1.2f), 2.4f));   // past the readable near: its own size
    CHECK(Near(WidthAt(0.6f, 2.4f, 1.2f), 1.2f));   // half as near: half as wide - the same size in view
    CHECK(Near(WidthAt(0.6f, 2.4f, 0.0f), 2.4f));   // the near not known: its own size
}

// A grip that starts off the panel never takes it, and a hold that started elsewhere stays elsewhere when the ray comes onto it.
static void TestOffPanel() {
    Grab g;
    In in = Scene();
    in.hand[1].dir = Unit({1.0f, 0.0f, 0.2f});   // pointing aside
    in.hand[1].grip = 1.0f;
    Out o = g.Step(in);
    CHECK(o.hand == -1 && g.state(1) == Grab::State::Spent && !o.takeY[1]);
    in.hand[1].dir = {0.0f, 0.0f, -1.0f};
    in.hand[1].at.x += 0.2f;
    o = g.Step(in);
    CHECK(o.hand == -1 && !o.moving);
    in.hand[1].grip = 0.0f;
    g.Step(in);
    CHECK(g.state(1) == Grab::State::Idle);
    // not in force (the key off, no panel): never the panel's
    in.applies = false;
    in.hand[1].grip = 1.0f;
    o = g.Step(in);
    CHECK(o.hand == -1 && g.state(1) == Grab::State::Spent);
}

// Held, then a trigger: a click (right click / Shift click), not a grab. Held and let go still: its press as a tap where the laser left it free.
static void TestHeldClickAndTap() {
    Grab g;
    In in = Scene();
    in.hand[0].grip = 1.0f;
    Out o = g.Step(in);
    CHECK(o.hand == 0 && !o.moving && o.takeY[0] && g.state(0) == Grab::State::Held);
    in.hand[1].trigger = 1.0f;   // the other hand's trigger: a Shift click with this grip
    o = g.Step(in);
    CHECK(g.state(0) == Grab::State::Spent && o.hand == -1 && !o.takeTrigger[1] && !o.spent[0]);
    in.hand[0].at.x += 0.3f;     // moved after that: still nothing
    o = g.Step(in);
    CHECK(!o.moving);
    in.hand[0].grip = 0.0f; in.hand[1].trigger = 0.0f;
    g.Step(in);
    // a tap: squeezed on the panel, let go without moving; the laser did not take this grip
    Grab t;
    In s = Scene();
    s.hand[0].grip = 1.0f;
    t.Step(s);
    bool freeG[2] = {true, true}, hide[2] = {}, tap[2] = {};
    t.Post(freeG, 1000, hide, tap);
    CHECK(hide[0] && !tap[0] && !hide[1]);
    s.hand[0].grip = 0.0f;
    t.Step(s);
    t.Post(freeG, 1010, hide, tap);
    CHECK(!hide[0] && tap[0]);
    t.Post(freeG, 1010 + kGripTapMs + 1, hide, tap);
    CHECK(!tap[0]);
    // the laser took it all along: no tap from us (laser_grip_tap gives it)
    Grab u;
    s.hand[0].grip = 1.0f;
    u.Step(s);
    bool taken[2] = {false, true};
    u.Post(taken, 2000, hide, tap);
    CHECK(!hide[0]);
    s.hand[0].grip = 0.0f;
    u.Step(s);
    u.Post(taken, 2010, hide, tap);
    CHECK(!tap[0]);
}

// The grab: the hand's travel carries it rigidly; coming in pulls it nearer kPushGain times faster; let go, it stays and is told.
static void TestCarryAndPull() {
    Grab g;
    In in = Scene();
    in.hand[1].grip = 1.0f;
    g.Step(in);
    in.hand[1].at.x += 0.02f;   // a tremble: still held, not moved
    Out o = g.Step(in);
    CHECK(!o.moving && g.state(1) == Grab::State::Held);
    in.hand[1].at.x += 0.03f;   // 5 cm aside: a grab
    o = g.Step(in);
    CHECK(o.started && o.moving && o.hand == 1 && o.spent[1] && o.takeY[1]);
    CHECK(Near(o.panel.at.x, 0.05f, 2e-3f) && Near(o.panel.at.z, -1.6f, 0.01f));   // carried 1:1 sideways
    in.panel = o.panel;
    // pull the hand 20 cm straight in towards the eyes
    const float d0 = Len(Sub(o.panel.at, in.eyes));
    const V3 towards = Unit(Sub(in.eyes, in.hand[1].at));
    const float r0 = Len(Sub(in.hand[1].at, in.eyes));
    for (int i = 1; i <= 10; ++i) {
        in.hand[1].at = Add(in.hand[1].at, Mul(towards, 0.02f));
        o = g.Step(in);
        in.panel = o.panel;
    }
    const float pulled = r0 - Len(Sub(in.hand[1].at, in.eyes));
    const float d1 = Len(Sub(o.panel.at, in.eyes));
    CHECK(o.moving && d1 < d0 - 0.4f);   // much more than the hand's own 20 cm
    CHECK(Near(Dot(o.panel.front, Unit(Sub(in.eyes, o.panel.at))), 1.0f));   // turned to the eyes
    CHECK(pulled > 0.15f);
    // a trigger while carried clicks nothing until let go
    in.hand[1].trigger = 1.0f;
    o = g.Step(in);
    CHECK(o.takeTrigger[1] && o.moving);
    in.hand[1].grip = 0.0f;
    o = g.Step(in);
    CHECK(o.released && !o.moving && Near(o.fromM, 1.6f, 0.01f) && o.travelM > 0.4f && o.takeTrigger[1]);
    CHECK(Near(o.panel.at.x, in.panel.at.x) && Near(o.panel.at.z, in.panel.at.z));   // stays where it was carried
    bool freeG[2] = {true, true}, hide[2] = {}, tap[2] = {};
    g.Post(freeG, 5000, hide, tap);
    CHECK(!tap[1]);   // a grab is no tap (no page turned)
    in.hand[1].trigger = 0.0f;
    o = g.Step(in);
    CHECK(!o.takeTrigger[1] && g.state(1) == Grab::State::Idle);
}

// The stick moves it too, and the ends hold: never nearer than kMinM, never farther than kMaxM; back from an end moves at once.
static void TestStickAndEnds() {
    Grab g;
    In in = Scene();
    in.hand[1].grip = 1.0f;
    g.Step(in);
    in.hand[1].stickY = 1.0f;   // forward: nearer (a grab at once, the hand still)
    Out o{};
    for (int i = 0; i < 300; ++i) { o = g.Step(in); in.panel = o.panel; }
    CHECK(o.moving && Near(o.distM, kMinM));
    in.hand[1].stickY = -1.0f;
    o = g.Step(in);
    CHECK(o.distM > kMinM + 0.01f);   // no slack to take up at the end
    for (int i = 0; i < 400; ++i) { o = g.Step(in); in.panel = o.panel; }
    CHECK(Near(o.distM, kMaxM));
    in.hand[1].stickY = 0.0f;
    // a stall is not a jump
    in.dtS = 5.0f;
    in.hand[1].stickY = 1.0f;
    const float before = o.distM;
    o = g.Step(in);
    CHECK(before - o.distM <= kStickSpeed * kMaxStepS + 1e-3f);
}

// One hand at a time; the panel closing under the hand ends the grab (kept), the hand losing tracking too.
static void TestOneHandAndClose() {
    Grab g;
    In in = Scene();
    in.hand[1].grip = 1.0f;
    g.Step(in);
    in.hand[1].at.x += 0.06f;
    Out o = g.Step(in);
    CHECK(o.moving && o.hand == 1);
    in.panel = o.panel;
    in.hand[0].grip = 1.0f;   // the other hand on the panel too: not a second grab
    o = g.Step(in);
    CHECK(o.hand == 1 && g.state(0) == Grab::State::Spent);
    in.applies = false;       // the panel closed
    o = g.Step(in);
    CHECK(o.released && !o.moving && o.spent[1] && g.state(1) == Grab::State::Spent);
    in.hand[1].grip = 0.0f; in.hand[0].grip = 0.0f;
    o = g.Step(in);
    CHECK(!o.spent[1] && g.state(1) == Grab::State::Idle);
}

// A hand at the panel (no ray onto it) takes it too - the panel brought to arm's length is taken by touching it.
static void TestTouchGrab() {
    Grab g;
    In in = Scene();
    in.panel = FaceEyes({0.0f, 1.4f, -0.45f}, in.eyes);
    in.widthM = 0.8f; in.heightM = 0.45f;
    in.hand[0].at = {0.1f, 1.4f, -0.43f};
    in.hand[0].dir = {0.0f, 1.0f, 0.0f};   // pointing up, not at it
    in.hand[0].grip = 1.0f;
    Out o = g.Step(in);
    CHECK(o.hand == 0 && g.state(0) == Grab::State::Held);
}

int main() {
    TestFaceAndRay();
    TestKept();
    TestWidth();
    TestOffPanel();
    TestHeldClickAndTap();
    TestCarryAndPull();
    TestStickAndEnds();
    TestOneHandAndClose();
    TestTouchGrab();
    std::printf("panel_grab_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
