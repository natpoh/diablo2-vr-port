// Offline checks of vr/band_icons.h ([render] ui_band_corner_icons): where the square and the 16:9 icon hang over a menu's panel (outside the
// game's picture), which one a point is on, when they show, what a click writes to [render] ui_band_16x9, and the click's mode held over the ini.
// No game, no OpenXR. Run: band_icons_test.exe (exit 0 = all passed).
#include <cmath>
#include <cstdio>
#include <initializer_list>

#include "../../vr/band_icons.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

using namespace bandicons;

static bool Near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) < eps; }

static void TestPlace() {
    CHECK(Near(Side(1.0f), 0.06f));
    CHECK(Near(Side(0.2f), 0.035f));   // a small panel: not smaller than 3.5 cm
    CHECK(Near(Side(3.0f), 0.09f));    // a big one: not bigger than 9 cm
    for (float w : {0.4f, 1.0f, 1.6f, 2.4f})
        for (float h : {0.4f, 1.0f, 1.6f, 2.4f}) {
            const Strip s = Place(w, h);
            CHECK(Near(s.w, 2.0f * s.h));                        // two square tiles side by side
            CHECK(Near(s.x + 0.5f * s.w, 0.5f * w));             // its right edge on the panel's right edge (Serg: top right, where the panel ends)
            CHECK(s.x - 0.5f * s.w > 0.0f);                       // in the right half of the panel
            CHECK(s.y - 0.5f * s.h > 0.5f * h);                  // wholly above the panel's top edge: outside the game's picture
            CHECK(s.y - 0.5f * s.h - 0.5f * h < 0.5f * s.h);     // ... but close to it (under half a tile)
        }
}

static void TestAt() {
    const float w = 0.18f, h = 0.09f;
    CHECK(At(-0.045f, 0.0f, w, h) == kSquare);
    CHECK(At(0.045f, 0.0f, w, h) == kRect);
    CHECK(At(-0.089f, 0.044f, w, h) == kSquare);   // corners still in
    CHECK(At(0.089f, -0.044f, w, h) == kRect);
    CHECK(At(-0.091f, 0.0f, w, h) == kNone);       // just outside each edge
    CHECK(At(0.091f, 0.0f, w, h) == kNone);
    CHECK(At(0.0f, 0.046f, w, h) == kNone);
    CHECK(At(0.0f, -0.046f, w, h) == kNone);
    CHECK(At(0.0f, 0.0f, 0.0f, h) == kNone);       // no strip hung
    CHECK(At(0.0f, 0.0f, w, 0.0f) == kNone);
}

static void TestShown() {
    Facts f;
    f.keyOn = f.f4 = f.inFront = f.vrResUi = true;
    f.w = 2112; f.h = 2304;   // a headset's own eye size: taller than 16:9
    CHECK(Shown(f));
    { Facts g = f; g.keyOn = false; CHECK(!Shown(g)); }       // the key off: as before
    { Facts g = f; g.f4 = false; CHECK(!Shown(g)); }          // F1, F2, F3: never
    { Facts g = f; g.inFront = false; CHECK(!Shown(g)); }     // the inventory on the hand: no
    { Facts g = f; g.vrResUi = false; CHECK(!Shown(g)); }     // no hook, no band
    { Facts g = f; g.campfire = true; CHECK(!Shown(g)); }     // the creation scene forces the band
    { Facts g = f; g.w = 2048; g.h = 1152; CHECK(!Shown(g)); }   // the main menu drawn 16:9: both the same picture
    { Facts g = f; g.w = 2048; g.h = 1153; CHECK(Shown(g)); }
    { Facts g = f; g.w = 1920; g.h = 1920; CHECK(Shown(g)); }    // the square window
    { Facts g = f; g.w = 0; g.h = 0; CHECK(!Shown(g)); }
    CHECK(Matters(1536, 1536));
    CHECK(!Matters(1536, 864));
    CHECK(!Matters(3000, 1000));
}

static void TestClick() {
    CHECK(Lit(0) == kSquare);
    CHECK(Lit(1) == kRect);
    CHECK(Lit(2) == kRect);
    CHECK(Lit(7) == kSquare);   // (not a mode: the whole picture, as SetUiBandMode takes it)
    // 0.214: the rectangle always gives 2 (with the inventory open) - 1 (always) is no longer offered
    CHECK(RectMode(1) == 2);
    CHECK(RectMode(2) == 2);
    CHECK(RectMode(0) == 2);
    CHECK(RectMode(-1) == 2);
    CHECK(RectMode(5) == 2);
    // the square on the band: the whole picture
    CHECK(ClickMode(kSquare, 1, 2) == 0);
    CHECK(ClickMode(kSquare, 2, 1) == 0);
    // the rectangle on the whole picture: the band with the inventory open, whatever was last
    CHECK(ClickMode(kRect, 0, 1) == 2);
    CHECK(ClickMode(kRect, 0, 2) == 2);
    CHECK(ClickMode(kRect, 0, 0) == 2);
    // the lit one: nothing written
    CHECK(ClickMode(kSquare, 0, 2) == -1);
    CHECK(ClickMode(kRect, 1, 2) == -1);
    CHECK(ClickMode(kRect, 2, 1) == -1);
    CHECK(ClickMode(kNone, 0, 2) == -1);
    CHECK(ClickMode(kNone, 2, 2) == -1);
}

static void TestHold() {
    Hold h;
    CHECK(h.Apply(2, 0) == 2);                 // nothing picked: the ini's
    h.Pick(0, 1000);
    CHECK(h.Apply(2, 1200) == 0);              // the ini still says 2: the click's 0 held
    CHECK(h.Apply(2, 5900) == 0);
    CHECK(h.Apply(0, 6000) == 0);              // the write landed
    CHECK(h.mode == -1);
    CHECK(h.Apply(1, 6100) == 1);              // then Settings' own change is taken at once
    h.Pick(2, 10000);
    CHECK(h.Apply(0, 15001) == 0);             // not written in 5 s: the ini's again
    CHECK(h.mode == -1);
    h.Pick(1, 20000);
    CHECK(h.Apply(1, 20000) == 1);
    CHECK(h.Apply(0, 20001) == 0);             // (cleared by the match: a later change of the ini is the ini's)
}

static void TestHandButtonApart() {   // the inventory's own button hangs beside the same corner, under the top edge - never on the strip
    for (float w : {0.4f, 1.0f, 1.6f, 2.4f}) {
        const float h = w * 1.09f;
        const Strip s = Place(w, h);
        const float bs = w * 0.09f < 0.035f ? 0.035f : w * 0.09f > 0.12f ? 0.12f : w * 0.09f;   // (hand_inventory.h ButtonSide / ButtonAt)
        const float bx = 0.5f * w + 0.6f * bs, by = 0.5f * h - 0.5f * bs;
        const bool apart = by + 0.5f * bs <= s.y - 0.5f * s.h || bx - 0.5f * bs >= s.x + 0.5f * s.w;
        CHECK(apart);
    }
}

static void TestReach() {
    CHECK(Near(BandShare(2112, 2304), (float)(2112u * 9u / 16u) / 2304.0f));
    CHECK(Near(BandShare(1920, 1920), 1080.0f / 1920.0f));
    CHECK(Near(BandShare(2048, 1152), 1.0f));   // 16:9 already: the band is all of it
    CHECK(Near(BandShare(0, 0), 1.0f));
    const float sh = 0.5f;
    CHECK(Reach::Classify(false, 0.5f, 0.2f, sh) == kNothing);
    CHECK(Reach::Classify(true, 0.5f, 0.2f, sh) == kOnBand);
    CHECK(Reach::Classify(true, 0.5f, 0.6f, sh) == kBelow);     // under the band: nothing of the game's there
    CHECK(Reach::Classify(true, 0.5f, 0.5f, sh) == kBelow);
    CHECK(Reach::Classify(true, 0.005f, 0.2f, sh) == kEdge);    // the outer 1 % each way
    CHECK(Reach::Classify(true, 0.995f, 0.2f, sh) == kEdge);
    CHECK(Reach::Classify(true, 0.5f, 0.004f, sh) == kEdge);
    CHECK(Reach::Classify(true, 0.5f, 0.496f, sh) == kEdge);
    CHECK(Reach::Classify(true, 0.02f, 0.02f, sh) == kOnBand);
    Reach r;
    CHECK(!r.Any());
    CHECK(!r.Pointed());
    r.AddPoint(0.3f, 0.1f, sh);
    r.AddPoint(0.9f, 0.4f, sh);
    r.AddPoint(0.5f, 0.8f, sh);   // below the band: not a reach of it
    CHECK(r.Pointed());
    CHECK(Near(r.left, 0.3f));
    CHECK(Near(r.right, 0.9f));
    CHECK(Near(r.top, 0.2f));
    CHECK(Near(r.bottom, 0.8f));
    r.AddPress(true, 0.5f, 0.2f, sh);
    r.AddPress(true, 0.5f, 0.7f, sh);
    r.AddPress(false, 0.0f, 0.0f, sh);
    r.AddPress(true, 0.999f, 0.2f, sh);
    CHECK(r.n[kOnBand] == 1 && r.n[kBelow] == 1 && r.n[kNothing] == 1 && r.n[kEdge] == 1);
    CHECK(r.Presses() == 4);
    r.Clear();
    CHECK(!r.Any());
    r.AddPress(false, 0.0f, 0.0f, sh);
    CHECK(r.Any() && !r.Pointed());
}

int main() {
    TestPlace();
    TestAt();
    TestShown();
    TestClick();
    TestHold();
    TestHandButtonApart();
    TestReach();
    std::printf("band_icons_test: %d/%d passed\n", g_checks - g_failures, g_checks);
    return g_failures ? 1 : 0;
}
