// log_slow_test.cpp - vr/log_slow.h on its own: is the host's log file slow on this PC ([debug] log_async_auto), and who wants the writer thread.
//   log_slow_test.exe     exit code 0 = every check passed
#include "log_slow.h"

#include <cmath>
#include <cstdio>
#include <limits>

using namespace logslow;

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

// How many of `n` lines of `ms` each brought the verdict in, and at which line the first did (-1 none).
static int FeedAll(Detector* d, const double* ms, int n, int* firstAt) {
    int slow = 0;
    *firstAt = -1;
    for (int i = 0; i < n; ++i)
        if (d->Feed(ms[i]).slow) { if (*firstAt < 0) *firstAt = i; ++slow; }
    return slow;
}

int main() {
    // The author's PC: about 1.2 ms a line, with the odd 3 ms one - never slow.
    {
        Detector d;
        bool any = false;
        for (int i = 0; i < 5000; ++i) any |= d.Feed(i % 7 == 0 ? 3.0 : 1.2).slow;
        CHECK(!any);
    }
    // The player's 0.181 FlatVR log, the lines around one Frozen Orb cast by touch (00:34:22.121 - .561, the gaps between them, ms) and then the
    // start's: slow at the 16th line, not before.
    {
        const double his[] = {31, 14, 32, 31, 31, 36, 22, 30, 28, 13, 28, 39, 31, 31, 43, 37, 34, 40, 30, 46};
        Detector d;
        int first = -1;
        const int slow = FeedAll(&d, his, (int)(sizeof his / sizeof his[0]), &first);
        CHECK(first == kWindow - 1);
        CHECK(slow == (int)(sizeof his / sizeof his[0]) - (kWindow - 1));
    }
    // A window not yet full is no verdict, however slow.
    {
        Detector d;
        Verdict v;
        for (int i = 0; i < kWindow - 1; ++i) v = d.Feed(100.0);
        CHECK(!v.slow);
        CHECK(d.Count() == kWindow - 1);
        v = d.Feed(100.0);
        CHECK(v.slow && v.slowLines == kWindow);
    }
    // The odd slow line (a disk spinning up, an antivirus look) is not a slow log: one in ten at 300 ms, the rest 1.2.
    {
        Detector d;
        bool any = false;
        for (int i = 0; i < 2000; ++i) any |= d.Feed(i % 10 == 0 ? 300.0 : 1.2).slow;
        CHECK(!any);
    }
    // The edge: 7 slow of 16 is not, 8 is; a line of exactly kSlowMs counts, one just under does not.
    {
        Detector a;
        Verdict v;
        for (int i = 0; i < kWindow; ++i) v = a.Feed(i < kSlowNeeded - 1 ? 30.0 : 1.0);
        CHECK(!v.slow && v.slowLines == kSlowNeeded - 1);
        Detector b;
        for (int i = 0; i < kWindow; ++i) v = b.Feed(i < kSlowNeeded ? kSlowMs : 1.0);
        CHECK(v.slow && v.slowLines == kSlowNeeded);
        Detector c;
        for (int i = 0; i < kWindow; ++i) v = c.Feed(i < kSlowNeeded ? kSlowMs - 0.01 : 1.0);
        CHECK(!v.slow && v.slowLines == 0);
    }
    // The window slides: slow lines that are 16 lines old no longer count.
    {
        Detector d;
        Verdict v;
        for (int i = 0; i < kWindow; ++i) v = d.Feed(30.0);
        CHECK(v.slow);
        for (int i = 0; i < kWindow - kSlowNeeded + 1; ++i) v = d.Feed(1.0);
        CHECK(!v.slow && v.slowLines == kSlowNeeded - 1);
    }
    // The numbers it tells: the median of the window and its slowest.
    {
        Detector d;
        Verdict v;
        for (int i = 0; i < kWindow; ++i) v = d.Feed(i < 8 ? 10.0 : 30.0 + i);
        CHECK(v.slow);
        CHECK(std::fabs(v.medianMs - 0.5 * (10.0 + 38.0)) < 1e-9);
        CHECK(std::fabs(v.worstMs - 45.0) < 1e-9);
    }
    // A NaN or a clock that went back counts as 0 ms, not as slow.
    {
        Detector d;
        Verdict v;
        for (int i = 0; i < kWindow; ++i) v = d.Feed(i % 2 ? std::numeric_limits<double>::quiet_NaN() : -50.0);
        CHECK(!v.slow && v.slowLines == 0 && v.worstMs == 0.0);
    }
    // Reset forgets the window.
    {
        Detector d;
        for (int i = 0; i < kWindow - 1; ++i) d.Feed(30.0);
        d.Reset();
        CHECK(d.Count() == 0);
        CHECK(!d.Feed(30.0).slow);
    }

    // AutoSwitch: log_async_auto off (as before) - nothing is timed, nothing fires, nothing wanted.
    {
        AutoSwitch s;
        CHECK(!s.Timing());
        Verdict v;
        bool fired = false;
        for (int i = 0; i < 100; ++i) fired |= s.Feed(30.0, &v);
        CHECK(!fired && !s.Wanted() && !s.Fired());
        CHECK(s.SetExplicit(true));    // log_async=1 alone
        CHECK(s.Wanted() && !s.Timing());
        CHECK(!s.SetExplicit(false));
    }
    // On, a slow log: it fires once, at the 16th line, and the timing stops; log_async=0 does not take the thread away.
    {
        AutoSwitch s;
        CHECK(!s.SetAuto(true));
        CHECK(s.Timing() && !s.Wanted());
        int fires = 0, at = -1;
        Verdict v;
        for (int i = 0; i < 40; ++i) if (s.Feed(29.0, &v)) { ++fires; if (at < 0) at = i; }
        CHECK(fires == 1 && at == kWindow - 1);
        CHECK(v.slow && std::fabs(v.medianMs - 29.0) < 1e-9);
        CHECK(s.Fired() && s.Wanted() && !s.Timing());
        CHECK(s.SetExplicit(false));   // still wanted: the verdict holds it
        // auto off: the thread is to stop (only the verdict had it), and a later auto on judges afresh
        CHECK(s.SetAuto(false));
        CHECK(!s.Wanted() && !s.Fired() && !s.Timing());
        CHECK(!s.SetAuto(true));
        CHECK(s.Timing());
        for (int i = 0; i < kWindow - 1; ++i) CHECK(!s.Feed(29.0, &v));
        CHECK(s.Feed(29.0, &v));
    }
    // With log_async=1 as well, auto off does not stop the thread.
    {
        AutoSwitch s;
        s.SetAuto(true);
        Verdict v;
        for (int i = 0; i < kWindow; ++i) s.Feed(29.0, &v);
        CHECK(s.Fired());
        CHECK(s.SetExplicit(true));
        CHECK(!s.SetAuto(false));
        CHECK(s.Wanted());
    }
    // A fast log with auto on: never wanted, timing goes on.
    {
        AutoSwitch s;
        s.SetAuto(true);
        Verdict v;
        bool fired = false;
        for (int i = 0; i < 3000; ++i) fired |= s.Feed(1.2, &v);
        CHECK(!fired && !s.Wanted() && s.Timing());
        CHECK(!s.SetAuto(true));   // the same again: no change
        CHECK(!s.SetAuto(false));  // nothing to stop
    }

    std::printf("log_slow_test: %d checks, %d failed\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
