// The first-start guide's flow (vr/first_start.h) on synthetic frames - no headset, no game, no files:
//
//   first_start_test        every case; exit code 0 = all passed
//
// done      DoneOf: an ini without the key (done for a player with a stored measurement), the key either way
// when      it comes up only after the context has held; never in a menu, another view, without focus, the plugin's controls or the right stick
// offer     no measurement stored: the offer page first; up measures (once), down goes on; the measurement hides it
// pages     flicks of the right stick page on and back, the last page's flick up finishes; a slow push is no flick; held up closes it
// gate      a stick pushed as it comes up (or comes back) counts only after it has been in the middle; a sideways turn is no flick
// ini       show=0 keeps it away; done=1 from elsewhere closes it; a stale done=0 after the end does not bring it back; done=0 later does
// reopen    the headset panel's button: the guide's pages at once, whatever show and done say
// pages     the words (vr/first_start_pages.h): a title and a foot on every page, rows from the top, every line short enough for the painter

#include "first_start.h"
#include "first_start_pages.h"

#include <cstdio>
#include <cstring>

using namespace firststart;

namespace {

int g_failed = 0, g_checks = 0;

void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_failed; std::printf("FAIL: %s\n", what); }
}

// A player standing in the world in F4 with the plugin's controls, nothing open, the right stick in the middle; 90 frames a second.
struct Sim {
    Guide g;
    Context c;
    Out last;
    int measures = 0;
    End ended = End::None;
    Sim() {
        c.focused = c.f4 = c.direct = c.stickOurs = true;
    }
    Out Frame() {
        c.time += 1.0 / 90.0;
        last = g.Step(c);
        if (last.measure) ++measures;
        if (last.end != End::None) ended = last.end;
        return last;
    }
    void Run(double s) {
        for (int i = 0, n = (int)(s * 90.0 + 0.5); i < n; ++i) Frame();
    }
    // The right stick pushed up (+1) or down (-1) for `hold` seconds, back in the middle, then a moment.
    void Flick(int dir, double hold = 0.15) {
        c.stick[0] = 0.1f;
        c.stick[1] = dir > 0 ? 0.95f : -0.95f;
        Run(hold);
        c.stick[0] = c.stick[1] = 0.0f;
        Run(0.1);
    }
};

void TestDone() {
    Check(!DoneOf(-1, false), "no key, not measured: not done (a new player)");
    Check(DoneOf(-1, true), "no key, measured: done (he has played in F4 before)");
    Check(!DoneOf(0, true), "done=0 wins over a measurement");
    Check(DoneOf(1, false), "done=1");
}

void TestWhen() {
    Sim s;
    s.g.Configure(true, false, true);
    s.Run(kSettleS - 0.5);
    Check(!s.last.show, "not before the context has held kSettleS");
    s.Run(1.0);
    Check(s.last.show && s.last.page == 1, "calibrated: comes up on the first guide page");

    const auto hiddenBy = [](void (*set)(Context&), const char* what) {
        Sim t;
        t.g.Configure(true, false, true);
        set(t.c);
        t.Run(kSettleS + 2.0);
        Check(!t.last.show, what);
    };
    hiddenBy([](Context& c) { c.menu = true; }, "not in a menu of the game (or before the world)");
    hiddenBy([](Context& c) { c.f4 = false; }, "not in another view");
    hiddenBy([](Context& c) { c.focused = false; }, "not without the input focus");
    hiddenBy([](Context& c) { c.direct = false; }, "not without the plugin's own controls");
    hiddenBy([](Context& c) { c.stickOurs = false; }, "not while the game has the right stick ([turn] right_stick=0)");
    hiddenBy([](Context& c) { c.uiUp = true; }, "not while a panel of ours is up");
    hiddenBy([](Context& c) { c.measuring = true; }, "not while the measurement runs");

    // a menu opened while it shows: hidden, its page kept, back after the short settle
    s.Flick(1);
    Check(s.last.show && s.last.page == 2, "up: page 2");
    s.c.menu = true;
    s.Run(1.0);
    Check(!s.last.show, "the inventory opens: hidden");
    s.c.menu = false;
    s.Run(kQuickSettleS - 0.2);
    Check(!s.last.show, "not at once after the menu");
    s.Run(0.4);
    Check(s.last.show && s.last.page == 2, "back on the same page");
    Check(s.ended == End::None, "nothing ended");
}

void TestOffer() {
    Sim s;
    s.g.Configure(true, false, false);
    s.Run(kSettleS + 0.5);
    Check(s.last.show && s.last.page == kOfferPage, "not measured: the offer comes first");
    s.Flick(1);
    Check(s.measures == 1, "up asks for the measurement once");
    Check(!s.last.show && s.g.phase() == Phase::Measuring, "and the guide is out of the way");
    s.c.measuring = true;   // the prompt is up
    s.Run(5.0);
    Check(!s.last.show && s.measures == 1, "hidden while it runs, no second request");
    s.c.measuring = false;   // saved (or cancelled with B)
    s.Run(kQuickSettleS + 0.2);
    Check(s.last.show && s.last.page == 1, "after it: the guide's first page");

    Sim n;   // a measurement that never shows up (no floor) is not waited for
    n.g.Configure(true, false, false);
    n.Run(kSettleS + 0.5);
    n.Flick(1);
    n.Run(kMeasureStartS + kQuickSettleS + 0.5);
    Check(n.last.show && n.last.page == 1, "no measurement started: on to the guide anyway");

    Sim l;   // not now
    l.g.Configure(true, false, false);
    l.Run(kSettleS + 0.5);
    l.Flick(-1);
    Check(l.measures == 0 && l.last.show && l.last.page == 1, "down skips the measurement");
}

void TestPages() {
    Sim s;
    s.g.Configure(true, false, true);
    s.Run(kSettleS + 0.5);
    s.Flick(-1);
    Check(s.last.page == 1, "down on the first page: stays");
    for (int p = 2; p <= kGuidePages; ++p) {
        s.Flick(1);
        Check(s.last.show && s.last.page == p, "up: the next page");
    }
    s.Flick(-1);
    Check(s.last.page == kGuidePages - 1, "down: back one");
    s.Flick(-1, kFlickMaxS + 0.3);
    Check(s.last.page == kGuidePages - 1, "a slow push is no flick");
    s.Flick(1);
    s.Flick(1);
    Check(!s.last.show && s.ended == End::Finished && s.g.phase() == Phase::Closed, "the last page's flick up finishes it");
    s.Run(10.0);
    Check(!s.last.show, "and it stays closed");

    Sim b;   // held up
    b.g.Configure(true, false, false);
    b.Run(kSettleS + 0.5);
    b.c.stick[1] = 1.0f;
    b.Run(kCloseHoldS - 0.3);
    Check(b.last.show, "held up: not yet");
    b.Run(0.5);
    Check(!b.last.show && b.ended == End::Skipped && b.measures == 0, "held up a second: closed, nothing measured");
    b.c.stick[1] = 0.0f;
    b.Run(1.0);
    Check(b.ended == End::Skipped && !b.last.show && b.measures == 0, "letting go after it is nothing");

    Sim d;   // held down is only back
    d.g.Configure(true, false, true);
    d.Run(kSettleS + 0.5);
    d.c.stick[1] = -1.0f;
    d.Run(3.0);
    Check(d.last.show && d.ended == End::None, "held down closes nothing");
    d.c.stick[1] = 0.0f;
    d.Run(0.2);

    Sim t;   // a turn is no flick
    t.g.Configure(true, false, true);
    t.Run(kSettleS + 0.5);
    t.c.stick[0] = 1.0f;
    t.c.stick[1] = 0.6f;
    t.Run(0.2);
    t.c.stick[0] = 0.9f;
    t.c.stick[1] = 0.8f;
    t.Run(0.1);
    t.c.stick[0] = t.c.stick[1] = 0.0f;
    t.Run(0.2);
    Check(t.last.show && t.last.page == 1, "the stick to the side (a turn), even a little up: nothing");
}

void TestGate() {
    Sim s;
    s.g.Configure(true, false, true);
    s.c.stick[1] = 1.0f;   // pushed up as it comes up
    s.Run(kSettleS + 0.5);
    Check(s.last.show && s.last.page == 1 && s.ended == End::None, "up with the stick pushed: it neither pages nor closes");
    s.c.stick[1] = 0.0f;
    s.Run(0.1);
    Check(s.last.page == 1, "back in the middle: still nothing");
    s.Flick(1);
    Check(s.last.page == 2, "the next flick is");
    s.c.menu = true;
    s.c.stick[1] = -1.0f;
    s.Run(0.5);
    s.c.menu = false;
    s.Run(kQuickSettleS + 0.1);
    s.c.stick[1] = 0.0f;
    s.Run(0.1);
    Check(s.last.show && s.last.page == 2, "a push begun in a menu does not page after it");
}

void TestIni() {
    Sim s;
    s.g.Configure(false, false, false);
    s.Run(kSettleS + 3.0);
    Check(!s.last.show && s.measures == 0, "show=0: never by itself");
    s.g.Configure(true, true, false);
    s.Run(kSettleS + 3.0);
    Check(!s.last.show, "done=1: never by itself");

    Sim d;   // ticked "seen" in D2R VR Settings while it shows
    d.g.Configure(true, false, true);
    d.Run(kSettleS + 0.5);
    d.g.Configure(true, true, true);
    d.Run(0.2);
    Check(!d.last.show && d.ended == End::None, "done=1 from elsewhere closes it, nothing to write");
    d.g.Configure(true, false, true);
    d.Run(kSettleS + 0.5);
    Check(d.last.show && d.last.page == 1, "unticked again: it comes back");

    Sim u;   // "Show the first-start guide" unticked while it shows
    u.g.Configure(true, false, true);
    u.Run(kSettleS + 0.5);
    u.g.Configure(false, false, true);
    u.Run(0.2);
    Check(!u.last.show && u.ended == End::None, "show=0 while up: it goes, nothing written");

    Sim f;   // finished; the file has not been read back yet
    f.g.Configure(true, false, true);
    f.Run(kSettleS + 0.5);
    f.c.stick[1] = 1.0f;
    f.Run(kCloseHoldS + 0.2);
    f.c.stick[1] = 0.0f;
    f.Run(0.1);
    f.g.Configure(true, false, true);   // a read of the ini before our done=1 reached it
    f.Run(kSettleS + 1.0);
    Check(!f.last.show, "a stale done=0 after the end does not bring it back");
    f.g.Configure(true, true, true);    // our done=1 read back
    f.g.Configure(true, false, true);   // and later unticked in D2R VR Settings
    f.Run(kSettleS + 0.5);
    Check(f.last.show && f.last.page == 1, "done=0 after done=1: it comes up again");
}

void TestReopen() {
    Sim s;
    s.g.Configure(false, true, false);   // off and done
    s.Run(1.0);
    s.g.Reopen();
    s.Run(kQuickSettleS + 0.1);
    Check(s.last.show && s.last.page == 1, "the panel's button: the guide's first page, no offer, even with show=0 and done=1");
    s.g.Configure(false, true, false);   // the ini read again meanwhile
    s.Run(0.1);
    Check(s.last.show, "a reopened guide is not closed by show=0 / done=1");
    for (int p = 1; p < kGuidePages; ++p) s.Flick(1);
    s.Flick(1);
    Check(!s.last.show && s.ended == End::Finished, "and it finishes as ever");
    s.Run(kSettleS + 1.0);
    Check(!s.last.show, "then stays closed");
}

// paint_kit's Text takes up to 255 characters (its wide buffer); a line longer than that would be cut, so the test keeps them under it.
void TestWords() {
    constexpr size_t kMost = 250;
    for (int p = 0; p < kPageCount; ++p) {
        const PageText& t = kPages[p];
        Check(t.title && *t.title && std::strlen(t.title) < 80, "every page has a short title");
        Check(t.nav && *t.nav && std::strlen(t.nav) <= 72, "every page has its foot, one line long");
        bool gap = false;
        for (const Row& r : t.rows) {
            const bool empty = !r.does || !*r.does;
            if (empty) { gap = true; continue; }
            Check(!gap, "rows are filled from the top");
            Check(r.control && std::strlen(r.control) <= 24, "a control's name fits its column (one line)");
            Check(std::strlen(r.does) < kMost, "a row's text is short enough for the painter");
            // three lines of a row (first_start_panel.cpp: 20 px Georgia, some 55 letters a line beside a name, 80 across the panel), with room to spare
            Check(std::strlen(r.does) <= (r.control && *r.control ? 150u : 210u), "a row's text fits its three lines");
        }
    }
    Check(std::strstr(kPages[kOfferPage].nav, "measure") != nullptr, "the offer's foot says how to measure");
    Check(std::strstr(kPages[kGuidePages].nav, "close") != nullptr, "the last page's foot says how it closes");
}

}  // namespace

int main() {
    TestWords();
    TestDone();
    TestWhen();
    TestOffer();
    TestPages();
    TestGate();
    TestIni();
    TestReopen();
    std::printf("first_start_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
