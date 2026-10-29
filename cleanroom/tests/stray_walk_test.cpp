// Offline checks of vr/stray_walk.h: which mouse buttons reach the game in the F4 world, and which walk commands are dropped.
// Run: build/Release/stray_walk_test.exe (exit code 0 = passed)

#include <cstdio>
#include <initializer_list>

#include "../../vr/stray_walk.h"

using namespace straywalk;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static World F4Worlds() {
    World w;
    w.on = true; w.f4 = true; w.inWorld = true; w.menuOpen = false; w.mouseLook = false; w.ownClicks = false;
    return w;
}

static void TestMessages() {
    CHECK(ButtonOf(kWmLButtonDown) == 0 && ButtonOf(kWmLButtonUp) == 0 && ButtonOf(kWmLButtonDblClk) == 0);
    CHECK(ButtonOf(kWmRButtonDown) == 1 && ButtonOf(kWmMButtonUp) == 2 && ButtonOf(kWmXButtonDown) == 3);
    CHECK(ButtonOf(0x200) == -1);    // WM_MOUSEMOVE
    CHECK(ButtonOf(0x20A) == -1);    // WM_MOUSEWHEEL
    CHECK(ButtonOf(0x100) == -1);    // WM_KEYDOWN
    CHECK(IsUp(kWmLButtonUp) && IsUp(kWmRButtonUp) && !IsUp(kWmLButtonDown) && !IsUp(kWmLButtonDblClk));
}

static void TestClicks() {
    ClickFilter f;
    World w = F4Worlds();
    // a stray left click in the F4 world: pressed and released, neither reaches the game
    CHECK(f.Drop(kWmLButtonDown, w, false));
    CHECK(f.Drop(kWmLButtonUp, w, false));
    // a release that had no press dropped goes through
    CHECK(!f.Drop(kWmLButtonUp, w, false));
    // the right button, the middle, a double click
    CHECK(f.Drop(kWmRButtonDown, w, false) && f.Drop(kWmRButtonUp, w, false));
    CHECK(f.Drop(kWmMButtonDown, w, false) && f.Drop(kWmMButtonUp, w, false));
    CHECK(f.Drop(kWmLButtonDblClk, w, false) && f.Drop(kWmLButtonUp, w, false));
    // not a button: untouched
    CHECK(!f.Drop(0x200, w, false));
    // another injected click too (the plugin's own laser acts only on panels and menus)
    CHECK(f.Drop(kWmLButtonDown, w, true) && f.Drop(kWmLButtonUp, w, true));
    // the other views, the switch off, no game: the game's own
    World o = w; o.f4 = false;
    CHECK(!f.Drop(kWmLButtonDown, o, false) && !f.Drop(kWmLButtonUp, o, false));
    o = w; o.on = false;
    CHECK(!f.Drop(kWmLButtonDown, o, false));
    f.Drop(kWmLButtonUp, o, false);
    o = w; o.inWorld = false;                                  // the title and character screens: the laser clicks there
    CHECK(!f.Drop(kWmLButtonDown, o, true) && !f.Drop(kWmLButtonUp, o, true));
    // a panel or a menu open: clicks are for it
    o = w; o.menuOpen = true;
    CHECK(!f.Drop(kWmLButtonDown, o, false) && !f.Drop(kWmLButtonUp, o, false));
    // mouse look: the mouse is the game's pointer by design
    o = w; o.mouseLook = true;
    CHECK(!f.Drop(kWmLButtonDown, o, false));
    f.Drop(kWmLButtonUp, o, false);
    // [bow] mode 2: the plugin's own injected Shift + click goes through, a device's does not
    o = w; o.ownClicks = true;
    CHECK(!f.Drop(kWmLButtonDown, o, true) && !f.Drop(kWmLButtonUp, o, true));
    CHECK(f.Drop(kWmLButtonDown, o, false) && f.Drop(kWmLButtonUp, o, false));
}

static void TestPressAcrossAPanel() {
    ClickFilter f;
    World w = F4Worlds();
    // pressed in a panel (let through), the panel closes, released in the world: the release goes through too - the game saw the press
    World panel = w; panel.menuOpen = true;
    CHECK(!f.Drop(kWmLButtonDown, panel, false));
    CHECK(!f.Drop(kWmLButtonUp, w, false));
    // pressed in the world (dropped), a panel opens, released in it: the release is dropped - the game never saw the press
    CHECK(f.Drop(kWmLButtonDown, w, false));
    CHECK(f.Drop(kWmLButtonUp, panel, false));
    // and the next click in the panel is its own
    CHECK(!f.Drop(kWmLButtonDown, panel, false) && !f.Drop(kWmLButtonUp, panel, false));
    // each button keeps its own state
    CHECK(f.Drop(kWmLButtonDown, w, false));
    CHECK(!f.Drop(kWmRButtonDown, panel, false));
    CHECK(f.Drop(kWmLButtonUp, panel, false));
    CHECK(!f.Drop(kWmRButtonUp, w, false));
}

static void TestContext() {
    CHECK(ContextOf(0, true) == Ctx::GroundClick && ContextOf(1, true) == Ctx::GroundClick);
    CHECK(ContextOf(3, true) == Ctx::GroundClick && ContextOf(4, true) == Ctx::GroundClick);
    CHECK(ContextOf(0, false) == Ctx::UnitClick && ContextOf(4, false) == Ctx::UnitClick);
    CHECK(ContextOf(6, true) == Ctx::Stop && ContextOf(6, false) == Ctx::Stop);
    for (int t : {7, 8, 9, 10}) CHECK(ContextOf(t, true) == Ctx::Pad);
    for (int t : {11, 12, 13}) CHECK(ContextOf(t, true) == Ctx::ForceMove);
    CHECK(ContextOf(2, true) == Ctx::Other && ContextOf(5, true) == Ctx::Other && ContextOf(14, true) == Ctx::Other);
}

static void TestWalk() {
    // the stray walk: a walk or a run to a point out of a click on bare ground
    CHECK(WalkVerdict(true, true, kWalkToPoint, Ctx::GroundClick) == Verdict::Drop);
    CHECK(WalkVerdict(true, true, kRunToPoint, Ctx::GroundClick) == Verdict::Drop);
    // sanctioned: the stick's stop, Force Move, a click on a unit
    for (Ctx c : {Ctx::Stop, Ctx::ForceMove, Ctx::UnitClick}) {
        CHECK(WalkVerdict(true, true, kWalkToPoint, c) == Verdict::Pass);
        CHECK(WalkVerdict(true, true, kRunToPoint, c) == Verdict::Pass);
    }
    // not known to be sanctioned, not known to be stray: said, not dropped
    for (Ctx c : {Ctx::Pad, Ctx::None, Ctx::Other}) {
        CHECK(WalkVerdict(true, true, kWalkToPoint, c) == Verdict::Note);
        CHECK(WalkVerdict(true, true, kRunToPoint, c) == Verdict::Note);
    }
    // every other command is never touched, whatever click it comes out of: the stick's walk, walks to a unit, skills
    for (int cmd : {kStickDirection, kWalkToUnit, kRunToUnit, 5, 6, 7, 8, 9, 0x0C, 0x0D, 0x13, 0x16, 0x20, 0x41})
        for (Ctx c : {Ctx::GroundClick, Ctx::UnitClick, Ctx::Stop, Ctx::ForceMove, Ctx::Pad, Ctx::None, Ctx::Other})
            CHECK(WalkVerdict(true, true, cmd, c) == Verdict::Pass);
    // the switch off, another view: nothing
    CHECK(WalkVerdict(false, true, kRunToPoint, Ctx::GroundClick) == Verdict::Pass);
    CHECK(WalkVerdict(true, false, kRunToPoint, Ctx::GroundClick) == Verdict::Pass);
    CHECK(WalkVerdict(true, false, kWalkToPoint, Ctx::Pad) == Verdict::Pass);
    CHECK(IsWalkToPoint(1) && IsWalkToPoint(3) && !IsWalkToPoint(2) && !IsWalkToPoint(4) && !IsWalkToPoint(0x42));
}

static void TestAttackRun() {
    // an attack of ours is pressed with Stand Still: the run the game makes out of the pad's cast is not meant
    for (Ctx c : {Ctx::Pad, Ctx::Other}) {
        CHECK(WalkVerdict(true, true, kRunToPoint, c, true) == Verdict::Drop);
        CHECK(WalkVerdict(true, true, kWalkToPoint, c, true) == Verdict::Drop);
        CHECK(WalkVerdict(true, true, kRunToPoint, c, false) == Verdict::Note);   // no attack of ours: only noted, as before
    }
    // a caller that is no click at all is still only noted (not known what it is)
    CHECK(WalkVerdict(true, true, kRunToPoint, Ctx::None, true) == Verdict::Note);
    // the sanctioned ones stay sanctioned, with an attack too
    for (Ctx c : {Ctx::Stop, Ctx::ForceMove, Ctx::UnitClick})
        CHECK(WalkVerdict(true, true, kRunToPoint, c, true) == Verdict::Pass);
    // a bare-ground click is dropped either way
    CHECK(WalkVerdict(true, true, kRunToPoint, Ctx::GroundClick, true) == Verdict::Drop);
    // walks to a unit, the stick and the skills are never touched, with an attack either
    for (int cmd : {kStickDirection, kWalkToUnit, kRunToUnit, 5, 0x0C, 0x20})
        for (Ctx c : {Ctx::Pad, Ctx::Other, Ctx::None, Ctx::GroundClick, Ctx::UnitClick})
            CHECK(WalkVerdict(true, true, cmd, c, true) == Verdict::Pass);
    // the switch off, another view: nothing
    CHECK(WalkVerdict(false, true, kRunToPoint, Ctx::Pad, true) == Verdict::Pass);
    CHECK(WalkVerdict(true, false, kRunToPoint, Ctx::Pad, true) == Verdict::Pass);
}

static void TestLimiter() {
    Limiter l;
    uint32_t count = 0, said = 0;
    // 100 events in a burst at one moment: the head, then nothing
    for (int i = 0; i < 100; ++i) if (l.Say(1000, &count)) ++said;
    CHECK(said == Limiter::kHead && count == 1);
    // then one line in two seconds, standing for those since
    CHECK(!l.Say(1500, &count));
    CHECK(!l.Say(2900, &count));
    CHECK(l.Say(3000, &count) && count == 73);   // the 70 after the head, and these three
    CHECK(l.Total() == 103);
}

int main() {
    TestMessages();
    TestClicks();
    TestPressAcrossAPanel();
    TestContext();
    TestWalk();
    TestAttackRun();
    TestLimiter();
    if (g_failures) { std::printf("%d check(s) FAILED\n", g_failures); return 1; }
    std::printf("stray_walk_test: all %d checks passed\n", g_checks);
    return 0;
}
