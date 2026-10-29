// The first-start guide's flow (vr/first_start.h) on synthetic frames - no headset, no game, no files:
//
//   first_start_test        every case; exit code 0 = all passed
//
// done      DoneOf: an ini without the key (done for a player with a stored measurement), the key either way
// when      it asks for the settings window only after the context has held; never in a menu, another view, without focus, the plugin's own
//           controls or a window to host it, nor while a panel of ours is up or the measurement runs
// page      a measurement stored: the guide's first page; none: the measurement's offer
// window    the window came up: nothing more is asked; it ends (Done / closed) and the guide is over; a window that never comes is asked for
//           again, three times, then not this session; one that vanished without a word is asked for again
// ini       show=0 keeps it away; done=1 from elsewhere keeps it away; a stale done=0 after the end does not bring it back; done=0 later does
// pages     the words (vr/first_start_pages.h): a title and a foot on every page, rows from the top, every line short enough for its place
// (what the page shows, its buttons and the windows that hold it: tools/hset_test.cpp TestGuide, cleanroom/tests/headset_tabs_test.cpp)

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

// A player standing in the world in F4 with the plugin's controls, nothing open, the skill panel's Settings tab there to host the guide; 90 frames a second.
struct Sim {
    Guide g;
    Context c;
    Out last;
    int asks = 0, lastPage = -1;
    bool gaveUp = false;   // it said so (once, whenever it was)
    Sim() { c.focused = c.f4 = c.direct = c.host = true; }
    Out Frame() {
        c.time += 1.0 / 90.0;
        last = g.Step(c);
        if (last.open) { ++asks; lastPage = last.page; }
        if (last.gaveUp) gaveUp = true;
        return last;
    }
    void Run(double s) {
        for (int i = 0, n = (int)(s * 90.0 + 0.5); i < n; ++i) Frame();
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
    Check(s.asks == 0, "not before the context has held kSettleS");
    s.Run(1.0);
    Check(s.asks == 1 && s.lastPage == 1 && s.g.phase() == Phase::Hosted, "calibrated: it asks for the window on the guide's first page, once");
    s.Run(1.0);
    Check(s.asks == 1, "and does not ask again while it waits for the window");

    const auto heldBy = [](void (*set)(Context&), const char* what) {
        Sim t;
        t.g.Configure(true, false, true);
        set(t.c);
        t.Run(kSettleS + 3.0);
        Check(t.asks == 0, what);
    };
    heldBy([](Context& c) { c.menu = true; }, "not in a menu of the game (or before the world)");
    heldBy([](Context& c) { c.f4 = false; }, "not in another view");
    heldBy([](Context& c) { c.focused = false; }, "not without the input focus");
    heldBy([](Context& c) { c.direct = false; }, "not without the plugin's own controls");
    heldBy([](Context& c) { c.host = false; }, "not without a window to show it in");
    heldBy([](Context& c) { c.uiUp = true; }, "not while a panel of ours is up");
    heldBy([](Context& c) { c.measuring = true; }, "not while the measurement runs");

    // a menu opened while it waits: the count starts again
    Sim m;
    m.g.Configure(true, false, true);
    m.Run(kSettleS - 1.0);
    m.c.menu = true;
    m.Run(1.0);
    m.c.menu = false;
    m.Run(kSettleS - 1.0);
    Check(m.asks == 0, "a menu in between: the settle starts again");
    m.Run(1.5);
    Check(m.asks == 1, "and then it asks");
}

void TestPage() {
    Sim s;
    s.g.Configure(true, false, false);
    s.Run(kSettleS + 0.5);
    Check(s.asks == 1 && s.lastPage == kOfferPage, "not measured: the measurement's offer comes first");
    Sim c;
    c.g.Configure(true, false, true);
    c.Run(kSettleS + 0.5);
    Check(c.asks == 1 && c.lastPage == 1, "measured: the first page");
}

void TestWindow() {
    Sim s;
    s.g.Configure(true, false, true);
    s.Run(kSettleS + 0.5);
    s.c.uiUp = true;   // the window is up
    s.Run(20.0);
    Check(s.asks == 1 && s.g.phase() == Phase::Hosted, "the window is up: nothing is asked, it is the window's");
    s.g.Ended(End::Finished);
    s.c.uiUp = false;
    s.Run(kSettleS + 5.0);
    Check(s.g.phase() == Phase::Closed && s.asks == 1, "Done: over, and it stays over");

    Sim k;   // closed with the page on show
    k.g.Configure(true, false, true);
    k.Run(kSettleS + 0.5);
    k.c.uiUp = true;
    k.Run(2.0);
    k.c.uiUp = false;
    k.g.Ended(End::Skipped);
    k.Run(kSettleS + 5.0);
    Check(k.g.phase() == Phase::Closed && k.asks == 1, "closed: over too");

    Sim n;   // a window that never comes: asked for again after a while, kMaxAttempts times, then not this session
    n.g.Configure(true, false, true);
    n.Run(kSettleS + 0.5);
    Check(n.asks == 1, "asked");
    n.Run(kOpenWaitS - 0.5);
    Check(n.asks == 1, "not again at once");
    n.Run(kSettleS + kOpenWaitS + 1.0);
    Check(n.asks == 2, "after the wait and the settle: asked again");
    n.Run(2.0 * (kSettleS + kOpenWaitS) + 4.0);
    Check(n.asks == kMaxAttempts, "up to kMaxAttempts times");
    n.Run(40.0);
    Check(n.gaveUp && n.asks == kMaxAttempts, "and then not any more this session, with one word for the log");
    n.Run(60.0);
    Check(n.asks == kMaxAttempts, "it stays away");

    Sim v;   // a window that was up and vanished without a report (the view left F4, a new session)
    v.g.Configure(true, false, true);
    v.Run(kSettleS + 0.5);
    v.c.uiUp = true;
    v.Run(3.0);
    v.c.uiUp = false;
    v.Run(kLostS - 0.5);
    Check(v.asks == 1, "gone a moment: the report may be on its way");
    v.Run(kLostS + kSettleS + 1.0);
    Check(v.asks == 2, "gone for good: asked again once the context has held");

    Sim e;   // told it ended without having asked (the Body page's button, the Guide tab): over as well
    e.g.Configure(true, false, true);
    e.g.Ended(End::Finished);
    e.Run(kSettleS + 3.0);
    Check(e.asks == 0 && e.g.phase() == Phase::Closed, "seen by hand: it does not come by itself");
    e.g.Ended(End::None);
    Check(e.g.phase() == Phase::Closed, "(no end is no end)");
}

void TestIni() {
    Sim s;
    s.g.Configure(false, false, false);
    s.Run(kSettleS + 3.0);
    Check(s.asks == 0, "show=0: never by itself");
    s.g.Configure(true, true, false);
    s.Run(kSettleS + 3.0);
    Check(s.asks == 0, "done=1: never by itself");

    Sim d;   // ticked "seen" in D2R VR Authentic Settings while it waits
    d.g.Configure(true, false, true);
    d.Run(kSettleS - 1.0);
    d.g.Configure(true, true, true);
    d.Run(kSettleS + 3.0);
    Check(d.asks == 0, "done=1 while it waits: it does not come");
    d.g.Configure(true, false, true);
    d.Run(kSettleS + 0.5);
    Check(d.asks == 1, "unticked again: it asks");

    Sim u;   // "Show the first-start guide" unticked while it waits
    u.g.Configure(true, false, true);
    u.Run(kSettleS - 1.0);
    u.g.Configure(false, false, true);
    u.Run(kSettleS + 3.0);
    Check(u.asks == 0, "show=0 while it waits: it does not come");
    u.g.Configure(true, false, true);
    u.Run(kSettleS + 0.5);
    Check(u.asks == 1, "ticked again: it asks");

    Sim f;   // finished; the file has not been read back yet
    f.g.Configure(true, false, true);
    f.Run(kSettleS + 0.5);
    f.g.Ended(End::Finished);
    f.g.Configure(true, false, true);   // a read of the ini before our done=1 reached it
    f.Run(kSettleS + 3.0);
    Check(f.asks == 1, "a stale done=0 after the end does not bring it back");
    f.g.Configure(true, true, true);    // our done=1 read back
    f.g.Configure(true, false, true);   // and later unticked in D2R VR Authentic Settings
    f.Run(kSettleS + 0.5);
    Check(f.asks == 2 && f.lastPage == 1, "done=0 after done=1: it asks again");
}

// The words: the scene (vr/hset_scene.h) sets a row in lines of its own, so what this checks is that the data is sane - and the long text a row may have is
// tools/hset_test.cpp TestGuide's, which measures every line in the painter's font.
void TestWords() {
    constexpr size_t kMost = 250;
    for (int p = 0; p < kPageCount; ++p) {
        const PageText& t = kPages[p];
        Check(t.title && *t.title && std::strlen(t.title) < 80, "every page has a short title");
        Check(t.nav && *t.nav && std::strlen(t.nav) <= 72, "every page has its foot, one line long");
        bool gap = false;
        int rows = 0;
        for (const Row& r : t.rows) {
            const bool empty = !r.does || !*r.does;
            if (empty) { gap = true; continue; }
            ++rows;
            Check(!gap, "rows are filled from the top");
            Check(r.control && std::strlen(r.control) <= 24, "a control's name is short (two lines at most)");
            Check(std::strlen(r.does) < kMost, "a row's text is short enough");
            Check(std::strlen(r.does) <= (r.control && *r.control ? 150u : 210u), "a row's text fits three lines");
        }
        Check(rows >= 4, "a page has its rows");
    }
    Check(std::strstr(kPages[kOfferPage].nav, "Measure") != nullptr, "the offer's foot says how to measure");
    Check(std::strstr(kPages[kGuidePages].nav, "Done") != nullptr, "the last page's foot says how it ends");
    for (int p = 0; p < kPageCount; ++p)
        for (const Row& r : kPages[p].rows)
            Check(!r.does || !std::strstr(r.does, "guide's") || !std::strstr(r.does, "up and down"), "no word of the right stick's up and down being the guide's");
}

}  // namespace

int main() {
    TestWords();
    TestDone();
    TestWhen();
    TestPage();
    TestWindow();
    TestIni();
    std::printf("first_start_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
