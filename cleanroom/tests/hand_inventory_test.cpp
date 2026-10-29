// Offline checks of vr/hand_inventory.h ([openxr] hand_inventory): when the inventory goes to the left hand (away from town, F4, the panel and
// the laser on, the inventory alone open), where it hangs over the controller, the walk's verdict for the log - and the two direct-layout rules
// it sets: the left stick walks through a menu's change, its click's long press is not refused for its lean.
// No game, no OpenXR. Run: hand_inventory_test.exe (exit 0 = all passed).
#include <cmath>
#include <cstdio>
#include <utility>

#include "../../vr/hand_inventory.h"
#include "../../vr/direct_layout.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

using handinv::Facts;
using handinv::Off;
using handinv::V3;

static bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

static Facts Field() {   // the Blood Moor, walking, the inventory open
    Facts f;
    f.keyOn = f.f4 = f.panel = f.laser = f.inWorld = true;
    f.level = 2;
    f.menu = f.inventoryOnly = true;
    return f;
}

static void TestWhen() {
    CHECK(handinv::InForce(Field()) == Off::None);
    CHECK(handinv::Away(Field()) == Off::None);
    {   // each town keeps it in front: Rogue Encampment, Lut Gholein, Kurast Docks, the Pandemonium Fortress, Harrogath
        for (int town : {1, 40, 75, 103, 109}) {
            Facts f = Field();
            f.level = town;
            CHECK(handinv::InForce(f) == Off::Town);
            CHECK(handinv::Away(f) == Off::Town);
        }
        for (int field : {2, 3, 39, 41, 74, 76, 102, 104, 108, 110, 132}) {   // the areas round them are not towns
            Facts f = Field();
            f.level = field;
            CHECK(handinv::InForce(f) == Off::None);
        }
    }
    {   // the key, the view, the panel mode, the laser, a game, a known area - each its own reason, in this order
        Facts f = Field(); f.keyOn = false; CHECK(handinv::InForce(f) == Off::Key);
        f = Field(); f.f4 = false; CHECK(handinv::InForce(f) == Off::NotF4);
        f = Field(); f.panel = false; CHECK(handinv::InForce(f) == Off::NoPanel);
        f = Field(); f.laser = false; CHECK(handinv::InForce(f) == Off::NoLaser);
        f = Field(); f.inWorld = false; CHECK(handinv::InForce(f) == Off::NoGame);
        f = Field(); f.level = 0; CHECK(handinv::InForce(f) == Off::AreaUnknown);
        f = Field(); f.keyOn = false; f.level = 1; CHECK(handinv::InForce(f) == Off::Key);   // (the key first: unticked says "off")
    }
    {   // away from town with no menu: not in force (nothing to hang), but away - the stick click opens it at any lean
        Facts f = Field(); f.menu = false; f.inventoryOnly = false;
        CHECK(handinv::InForce(f) == Off::OtherMenu);
        CHECK(handinv::Away(f) == Off::None);
        f = Field(); f.inventoryOnly = false;   // a waypoint, the game menu with it: in front
        CHECK(handinv::InForce(f) == Off::OtherMenu);
        f = Field(); f.chat = true;
        CHECK(handinv::InForce(f) == Off::Chat);
        CHECK(handinv::Away(f) == Off::None);
    }
    for (int o = 0; o < (int)Off::Count; ++o) CHECK(handinv::OffText((Off)o)[0] != '?');
}

static void TestPanels() {
    CHECK(handinv::IsInventoryPanel("UISwitcher"));                       // the pad layout's tabbed menu (BodyWalk's 'D2R: Inventory', 2026-10-27)
    CHECK(handinv::IsInventoryPanel("PlayerInventoryExpansionLayout"));   // the inventory panel (the long press, 2026-10-27)
    CHECK(handinv::IsInventoryPanel("PlayerInventoryOriginalLayout"));
    CHECK(!handinv::IsInventoryPanel("BankExpansionLayout"));
    CHECK(!handinv::IsInventoryPanel("WaypointsPanel"));
    CHECK(!handinv::IsInventoryPanel("PauseLayoutGarden"));
    CHECK(!handinv::IsInventoryPanel("VendorPanelLayout"));
    CHECK(!handinv::IsInventoryPanel("HireablesPanel"));
    CHECK(!handinv::IsInventoryPanel("UISwitcherX"));
    CHECK(!handinv::IsInventoryPanel(""));
    CHECK(!handinv::IsInventoryPanel(nullptr));
}

static void Rotate(const float q[4], V3 v, V3* out) {   // v by the unit quaternion q (x y z w)
    const V3 u{q[0], q[1], q[2]};
    const V3 t = handinv::Mul(handinv::Cross(u, v), 2.0f);
    *out = handinv::Add(handinv::Add(v, handinv::Mul(t, q[3])), handinv::Cross(u, t));
}

static void TestPlace() {
    const V3 eyes{0.0f, 1.65f, 0.0f};
    const V3 hand{-0.20f, 1.15f, -0.35f};   // the left controller held in front, low
    const handinv::Place p = handinv::OnHand(hand, eyes, 0.55f, 1.0f);   // a square window
    CHECK(p.ok);
    CHECK(Near(p.w, 0.55f) && Near(p.h, 0.55f));
    // turned to the eyes: the normal points from its middle to them
    V3 toEyes = handinv::Sub(eyes, p.center);
    toEyes = handinv::Mul(toEyes, 1.0f / handinv::Len(toEyes));
    CHECK(handinv::Dot(toEyes, p.normal) > 0.9999f);
    // orthonormal, right-handed (x = y cross z), upright: its right edge level, its up toward the room's
    CHECK(Near(handinv::Len(p.right), 1.0f) && Near(handinv::Len(p.up), 1.0f) && Near(handinv::Len(p.normal), 1.0f));
    CHECK(Near(handinv::Dot(p.right, p.up), 0.0f) && Near(handinv::Dot(p.up, p.normal), 0.0f) && Near(handinv::Dot(p.right, p.normal), 0.0f));
    const V3 x = handinv::Cross(p.up, p.normal);
    CHECK(Near(x.x, p.right.x) && Near(x.y, p.right.y) && Near(x.z, p.right.z));
    CHECK(Near(p.right.y, 0.0f) && p.up.y > 0.5f);
    // held by its lower edge: the edge's middle kLiftM above the controller, along the panel's up
    const V3 lower = handinv::Sub(p.center, handinv::Mul(p.up, 0.5f * p.h));
    const V3 off = handinv::Sub(lower, hand);
    CHECK(Near(handinv::Len(off), handinv::kLiftM, 1e-3f));
    CHECK(handinv::Dot(off, p.up) > 0.0f);
    // the quaternion is these axes
    float q[4];
    handinv::Orientation(p, q);
    CHECK(Near(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3], 1.0f, 1e-3f));
    V3 r;
    Rotate(q, {1, 0, 0}, &r); CHECK(Near(r.x, p.right.x, 1e-3f) && Near(r.y, p.right.y, 1e-3f) && Near(r.z, p.right.z, 1e-3f));
    Rotate(q, {0, 1, 0}, &r); CHECK(Near(r.x, p.up.x, 1e-3f) && Near(r.y, p.up.y, 1e-3f) && Near(r.z, p.up.z, 1e-3f));
    Rotate(q, {0, 0, 1}, &r); CHECK(Near(r.x, p.normal.x, 1e-3f) && Near(r.y, p.normal.y, 1e-3f) && Near(r.z, p.normal.z, 1e-3f));
    // the inventory window at the default width: a good part of the view at the hand, less than the whole panel in front (2.3 m at 1.85 m: 64 deg)
    const handinv::Place pd = handinv::OnHand(hand, eyes, handinv::kWidthDefault, 1.2f);
    const float dist = handinv::Len(handinv::Sub(eyes, pd.center));
    const float handDeg = 2.0f * std::atan(0.5f * pd.w / dist) * 57.29578f, frontDeg = 2.0f * std::atan(0.5f * 2.3f / 1.85f) * 57.29578f;
    std::printf("the inventory window on the hand %.2f m at %.2f m: %.0f deg (the whole panel in front: %.0f deg)\n", pd.w, dist, handDeg, frontDeg);
    CHECK(handDeg > 25.0f && handDeg < frontDeg);
    // the hand anywhere round the head: always turned to the eyes and upright
    for (int i = 0; i < 16; ++i) {
        const float a = (float)i * 0.3927f;
        const V3 h{0.4f * std::sin(a), 1.0f + 0.05f * (float)(i % 5), -0.4f * std::cos(a)};
        const handinv::Place q2 = handinv::OnHand(h, eyes, 0.4f, 0.75f);
        CHECK(q2.ok);
        V3 to = handinv::Sub(eyes, q2.center);
        to = handinv::Mul(to, 1.0f / handinv::Len(to));
        CHECK(handinv::Dot(to, q2.normal) > 0.9999f);
        CHECK(Near(q2.right.y, 0.0f) && q2.up.y > 0.0f);
        CHECK(Near(q2.h, 0.3f));
    }
    // the default: 30 % over the first 0.40 m (Serg, 2026-10-28)
    CHECK(Near(handinv::kWidthDefault, 0.52f) && Near(handinv::kWidthDefault / 0.40f, 1.3f));
    // the width held to its range, a broken number to the default
    CHECK(Near(handinv::OnHand(hand, eyes, 5.0f, 1.0f).w, handinv::kWidthMax));
    CHECK(Near(handinv::OnHand(hand, eyes, 0.01f, 1.0f).w, handinv::kWidthMin));
    CHECK(Near(handinv::OnHand(hand, eyes, NAN, 1.0f).w, handinv::kWidthDefault));
    // nothing to place: the hand in the eyes, straight under them, not a number, no picture's shape
    CHECK(!handinv::OnHand(eyes, eyes, 0.55f, 1.0f).ok);
    CHECK(!handinv::OnHand({0.0f, 0.5f, 0.0f}, eyes, 0.55f, 1.0f).ok);
    CHECK(!handinv::OnHand({NAN, 1.0f, -0.3f}, eyes, 0.55f, 1.0f).ok);
    CHECK(!handinv::OnHand(hand, eyes, 0.55f, 0.0f).ok);
}

// The inventory window found in the picture (the Evil Islands way) and kept steady.
static void TestCut() {
    using handinv::Box;
    CHECK(handinv::GridH(2048, 2048) == 96 && handinv::GridH(3840, 2160) == 54 && handinv::GridH(0, 10) == 0 && handinv::GridH(4000, 100) == 16);
    CHECK(handinv::SampleAt(0, 96, 2048) == 10 && handinv::SampleAt(95, 96, 2048) == 2037);
    // the alpha as the layer keeps it (half floats, transmittance): 1 nothing, 0 the panels' art
    CHECK(handinv::HalfToFloat(0x3C00) == 1.0f && handinv::HalfToFloat(0x0000) == 0.0f && handinv::HalfToFloat(0x3800) == 0.5f);
    CHECK(handinv::HalfToFloat(0xBC00) == -1.0f && std::isnan(handinv::HalfToFloat(0x7E00)) && Near(handinv::HalfToFloat(0x3555), 0.3333f, 1e-3f));
    CHECK(handinv::Drawn(0x0000) && handinv::Drawn(0x3000) && !handinv::Drawn(0x3800) && !handinv::Drawn(0x3C00) && !handinv::Drawn(0x7E00));
    // the tabbed menu's backdrop over the whole screen (ControllerOverlayHD.json TransparentBG: black at 0.7) is NOT the panel (Serg's log 2026-10-28:
    // with the threshold at 0.4 the cut was the whole picture, and no release was ever off the inventory)
    CHECK(!handinv::Drawn(0x34CD) && handinv::HalfToFloat(0x34CD) > 0.29f && handinv::HalfToFloat(0x34CD) < 0.31f);
    {   // a 96 x 96 grid of a 2048 square: the inventory window on the right, the toolbar's strip low across, a speck, a lone cell
        constexpr int gw = 96, gh = 96;
        static uint8_t g[gw * gh] = {};
        auto fill = [&](int x0, int y0, int x1, int y1) { for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) g[y * gw + x] = 1; };
        fill(50, 10, 92, 70);   // the inventory: 42 x 60 cells
        fill(0, 80, 96, 92);    // the toolbar: 96 x 12 (apart from the panel by 10 rows)
        fill(10, 20, 12, 22);   // a speck of 4
        g[5 * gw + 5] = 1;      // a lone cell
        const Box b = handinv::Biggest(g, gw, gh, 2048, 2048);
        const double c = 2048.0 / 96.0;
        CHECK(b.ok());
        CHECK(b.x0 == (int)((50 - 0.25) * c) && b.y0 == (int)((10 - 0.25) * c) && b.x1 == (int)((92 + 0.25) * c) && b.y1 == (int)((70 + 0.25) * c));
        fill(50, 70, 92, 80);   // the panel reaching down to the toolbar: one patch, the biggest
        const Box m = handinv::Biggest(g, gw, gh, 2048, 2048);
        CHECK(m.x0 == 0 && m.x1 == 2048 && m.y1 == (int)((92 + 0.25) * c));
        uint8_t none[16 * 16] = {};
        CHECK(!handinv::Biggest(none, 16, 16, 100, 100).ok());
        CHECK(!handinv::Biggest(nullptr, 16, 16, 100, 100).ok());
        uint8_t full[16 * 16];
        for (uint8_t& v : full) v = 1;
        const Box f = handinv::Biggest(full, 16, 16, 160, 160);   // all drawn: the whole picture, held to it
        CHECK(f.x0 == 0 && f.y0 == 0 && f.x1 == 160 && f.y1 == 160);
    }
    {   // steady: bigger at once, smaller after kShrinkS, gone after kGoneS
        handinv::Steady s;
        const Box a{100, 100, 500, 700}, big{80, 100, 500, 700}, small{120, 120, 480, 680};
        CHECK(s.Step(a, 1.0f) == a);
        CHECK(s.Step(big, 1.0f) == big);   // a tooltip past its left edge: at once
        for (int i = 0; i < 7; ++i) CHECK(s.Step(a, 1.0f) == big);
        CHECK(s.Step(a, 1.0f) == a);       // 8 s smaller: its own size again
        CHECK(s.Step(small, 1.0f) == a);
        CHECK(s.Step(Box{}, 1.0f) == a);   // not found a second: kept
        CHECK(!s.Step(Box{}, 1.0f).ok());  // two: gone
        CHECK(s.Step(small, 1.0f) == small);
        const Box moved{600, 100, 900, 700};   // somewhere else (not enclosed): at once, the two together
        CHECK(s.Step(moved, 1.0f) == (Box{120, 100, 900, 700}));
        s.Reset();
        CHECK(!s.Now().ok());
    }
    {   // the laser's point on the cut as the window's, and past its edges for a drag carried off it
        const Box b{1000, 200, 1800, 1400};
        float pu = 0.0f, pv = 0.0f;
        handinv::ToPicture(b, 0.5f, 0.5f, 2048, 2048, &pu, &pv);
        CHECK(Near(pu, 1400.0f / 2048.0f) && Near(pv, 800.0f / 2048.0f));
        handinv::ToPicture(b, 0.0f, 0.0f, 2048, 2048, &pu, &pv);
        CHECK(Near(pu, 1000.0f / 2048.0f) && Near(pv, 200.0f / 2048.0f));
        handinv::ToPicture(b, -0.5f, 0.5f, 2048, 2048, &pu, &pv);   // off its left edge: the window left of the panel - where the drop goes
        CHECK(Near(pu, 600.0f / 2048.0f) && !b.Has(pu * 2048.0f, pv * 2048.0f));
        handinv::ToPicture(b, 3.0f, -2.0f, 2048, 2048, &pu, &pv);   // far off: held inside the window
        CHECK(pu < 1.0f && pu > 0.99f && pv == 0.0f);
        CHECK(b.Has(1000.0f, 200.0f) && !b.Has(1800.0f, 300.0f) && !b.Has(999.0f, 300.0f));
    }
    {   // let go off the inventory: on the hand off the quad (whatever the cut found - his whole-picture cut), in front off the window or the picture
        CHECK(handinv::ReleasedOff(true, false, false, true));     // the hand: off the quad, though the cut (the whole picture) holds the point
        CHECK(!handinv::ReleasedOff(true, true, false, false));    // the hand: on the quad is on the inventory
        CHECK(handinv::ReleasedOff(false, true, false, false));    // in front: on the picture, off the inventory's window
        CHECK(!handinv::ReleasedOff(false, true, false, true));    // in front: on its window
        CHECK(handinv::ReleasedOff(false, false, false, true));    // in front: off the picture
        CHECK(!handinv::ReleasedOff(true, false, true, false) && !handinv::ReleasedOff(false, false, true, false));   // on our button: never
    }
    {   // the button: beside the top right corner, never on the panel, a sensible size
        CHECK(Near(handinv::ButtonSide(0.4f), 0.036f) && Near(handinv::ButtonSide(0.1f), 0.035f) && Near(handinv::ButtonSide(2.3f), 0.12f));
        for (float w : {0.2f, 0.4f, 1.0f, 2.3f}) {
            float x = 0.0f, y = 0.0f;
            const float h = 1.3f * w, s = handinv::ButtonSide(w);
            handinv::ButtonAt(w, h, &x, &y);
            CHECK(x - 0.5f * s > 0.5f * w);              // all of it right of the panel
            CHECK(Near(y + 0.5f * s, 0.5f * h));         // its top at the panel's top
        }
    }
}

// The stick's own walk while the game's is locked.
static void TestWalk() {
    float gx = 0.0f, gy = 0.0f;
    handinv::ScreenToGrid(0.0f, 1.0f, &gx, &gy);    // up on the screen: both of the grid's axes down (the isometric north)
    CHECK(Near(gx, -0.70710678f) && Near(gy, -0.70710678f));
    handinv::ScreenToGrid(1.0f, 0.0f, &gx, &gy);    // right: +x, -y
    CHECK(Near(gx, 0.70710678f) && Near(gy, -0.70710678f));
    handinv::ScreenToGrid(0.0f, -1.0f, &gx, &gy);   // down: +x, +y
    CHECK(Near(gx, 0.70710678f) && Near(gy, 0.70710678f));
    handinv::ScreenToGrid(0.3f, 0.4f, &gx, &gy);    // the length is kept (the strength)
    CHECK(Near(std::sqrt(gx * gx + gy * gy), 0.5f));
    using handinv::Send;
    CHECK(handinv::WalkStep(true, true, false, false) == Send::Walk);    // pushed, the game's walk locked: ours
    CHECK(handinv::WalkStep(true, true, true, false) == Send::Nothing);  // the game walks itself: never both
    CHECK(handinv::WalkStep(true, false, false, true) == Send::Stop);    // let go after ours walked: the stick's stop
    CHECK(handinv::WalkStep(true, false, false, false) == Send::Nothing);
    CHECK(handinv::WalkStep(false, false, false, true) == Send::Stop);   // the inventory closed at rest: stopped
    CHECK(handinv::WalkStep(false, true, false, true) == Send::Nothing); // closed with it pushed: the game's own walk takes it on
    CHECK(handinv::WalkStep(false, true, false, false) == Send::Nothing);
    CHECK(handinv::kWalkEveryMs == 40);
}

static void TestVerdict() {
    using handinv::Walk;
    CHECK(handinv::Verdict(false, 90, 10.0f) == Walk::NotAsked);   // the stick at rest says nothing
    CHECK(handinv::Verdict(true, 0, 0.0f) == Walk::NotRead);       // the game's gate kept the walk from the stick
    CHECK(handinv::Verdict(true, 0, 3.0f) == Walk::NotRead);       // (moved by something else: still not read)
    CHECK(handinv::Verdict(true, 60, 0.2f) == Walk::ReadNoStep);
    CHECK(handinv::Verdict(true, 60, 4.0f) == Walk::Walks);
    CHECK(handinv::Verdict(true, 0, 4.0f, 25) == Walk::OursWalks);     // the game's walk locked, ours sent: he walks
    CHECK(handinv::Verdict(true, 0, 0.2f, 25) == Walk::OursNoStep);
    CHECK(handinv::Verdict(true, 0, 0.0f, 0) == Walk::NotRead);        // his test of 2026-10-28, before ours
    for (Walk w : {Walk::NotAsked, Walk::Walks, Walk::ReadNoStep, Walk::NotRead, Walk::OursWalks, Walk::OursNoStep}) CHECK(handinv::WalkText(w)[0] != 0);
}

// The direct layout's two rules the key sets (vr/direct_layout.h ReleaseGate::SetStickFree, Layout::SetLeanFree).
static void TestLayout() {
    using namespace directinput::layout;
    {   // a menu opens with the left stick pushed: as before it is held back until let go; free, it walks on
        ReleaseGate g;
        Controls c{};
        c.stick[0][1] = 0.9f;
        g.Step(1, c);                                         // (the first frame starts the gate: what is held, held back)
        Controls out = g.Step(2, c);                          // the context changed (a menu): held back
        CHECK(out.stick[0][1] == 0.0f);
        out = g.Step(2, c);
        CHECK(out.stick[0][1] == 0.0f);
        g.SetStickFree(0, true);
        out = g.Step(2, c);
        CHECK(out.stick[0][1] == 0.9f);
        out = g.Step(3, c);                                   // closed again, still pushed: on
        CHECK(out.stick[0][1] == 0.9f);
        c.stick[1][0] = 0.8f;                                 // the right stick is not freed by it
        out = g.Step(4, c);
        CHECK(out.stick[1][0] == 0.0f && out.stick[0][1] == 0.9f);
        c.v[kLStick] = 1.0f;                                  // nor the left stick's click
        out = g.Step(5, c);
        CHECK(out.v[kLStick] == 0.0f);
        g.SetStickFree(0, false);                             // unticked / in town: as before
        out = g.Step(6, c);
        CHECK(out.stick[0][1] == 0.0f);
    }
    {   // the left stick leaned past 70 % and its click held: no inventory as before; lean-free, the inventory
        auto run = [](bool free) {
            Layout l;
            l.SetLeanFree(0, free);
            Controls c{};
            c.stick[0][1] = 0.95f;
            c.lean[0] = 0.95f;
            c.v[kLStick] = 1.0f;
            bool inv = false;
            for (uint64_t t = 0; t <= 800; t += 10) {
                const Outputs o = l.Step(Mode::World, c, t);
                if (o.actions & act::kInventory) inv = true;
            }
            return std::make_pair(inv, l.TakeLeanRefused(0));
        };
        const auto before = run(false), free = run(true);
        CHECK(!before.first && before.second == Layout::kLeanPressed);
        CHECK(free.first && free.second == Layout::kLeanNone);
    }
    {   // in the menu the same click's long press closes it - walking, too
        Layout l;
        l.SetLeanFree(0, true);
        Controls c{};
        c.stick[0][0] = 1.0f;
        c.lean[0] = 1.0f;
        c.v[kLStick] = 1.0f;
        bool inv = false;
        for (uint64_t t = 0; t <= 800; t += 10) if (l.Step(Mode::Menu, c, t).actions & act::kInventory) inv = true;
        CHECK(inv);
    }
    {   // the right stick keeps its rule (the skill panel): only the left hand is freed
        Layout l;
        l.SetLeanFree(0, true);
        Controls c{};
        c.stick[1][1] = 0.95f;
        c.lean[1] = 0.95f;
        c.v[kRStick] = 1.0f;
        for (uint64_t t = 0; t <= 800; t += 10) l.Step(Mode::World, c, t);
        CHECK(l.TakeLeanRefused(1) == Layout::kLeanPressed);
    }
}

int main() {
    TestWhen();
    TestPanels();
    TestPlace();
    TestCut();
    TestWalk();
    TestVerdict();
    TestLayout();
    std::printf("hand_inventory_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
