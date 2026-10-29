// hit_latency_test.cpp - the press -> hit pairing of vr/hit_latency.h on made-up clocks (no game, no headset).
//   hit_latency_test.exe
#include "hit_latency.h"

#include <cstdio>
#include <cstring>
#include <thread>

using namespace hitlatency;

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)
static bool Near(double a, double b, double eps = 1e-6) { return a > b - eps && a < b + eps; }

constexpr int64_t ms(double v) { return (int64_t)(v * 1e6); }   // milliseconds -> ns

int main() {
    // one press, one hit: the latency is the difference
    {
        Tracker t;
        CHECK(t.Press(kMelee, ms(1000)));
        t.Landed(ms(1143));
        Tracker::Outcome o[4];
        const int n = t.Collect(kMelee, ms(1200), o, 4);
        CHECK(n == 1 && o[0].hit && Near(o[0].ms, 143.0));
        CHECK(t.Collect(kMelee, ms(1300), o, 4) == 0);   // taken once
    }
    // no hit within the window: lost, but not before the window is over
    {
        Tracker t;
        t.Press(kMelee, ms(1000));
        Tracker::Outcome o[4];
        CHECK(t.Collect(kMelee, ms(2000), o, 4) == 0);   // 1.0 s: still waiting
        const int n = t.Collect(kMelee, ms(2300), o, 4);
        CHECK(n == 1 && !o[0].hit);
        t.Landed(ms(2400));                              // a late hit finds nobody
        CHECK(t.Collect(kMelee, ms(2500), o, 4) == 0);
    }
    // a hit later than the window does not answer a press (it would be somebody else's)
    {
        Tracker t;
        t.Press(kMelee, ms(1000));
        t.Landed(ms(2250));
        Tracker::Outcome o[4];
        CHECK(t.Collect(kMelee, ms(2150), o, 4) == 0);   // 1.15 s: the window is still open
        const int n = t.Collect(kMelee, ms(2250), o, 4);   // 1.25 s: over, and the late hit answered nothing
        CHECK(n == 1 && !o[0].hit);
    }
    // a hit before the press is not its answer
    {
        Tracker t;
        t.Press(kMelee, ms(1000));
        t.Landed(ms(900));
        Tracker::Outcome o[4];
        CHECK(t.Collect(kMelee, ms(1100), o, 4) == 0);
        t.Landed(ms(1050));
        CHECK(t.Collect(kMelee, ms(1100), o, 4) == 1 && Near(o[0].ms, 50.0));
    }
    // two presses, hits answer the oldest first; one press with a burst of hits (Zeal) counts once
    {
        Tracker t;
        t.Press(kMelee, ms(1000));
        t.Press(kMelee, ms(1300));
        t.Landed(ms(1400));
        t.Landed(ms(1700));
        t.Landed(ms(1800));   // no press left to answer
        Tracker::Outcome o[8];
        const int n = t.Collect(kMelee, ms(1900), o, 8);
        CHECK(n == 2);
        double sum = 0;
        for (int i = 0; i < n; ++i) { CHECK(o[i].hit); sum += o[i].ms; }
        CHECK(Near(sum, 400.0 + 400.0));   // 1000->1400 and 1300->1700
    }
    // sources are kept apart: one collects only its own
    {
        Tracker t;
        t.Press(kMelee, ms(1000));
        t.Press(kShield, ms(1010));
        t.Landed(ms(1100));
        t.Landed(ms(1200));
        Tracker::Outcome o[4];
        CHECK(t.Collect(kShield, ms(1300), o, 4) == 1 && o[0].hit && Near(o[0].ms, 190.0));
        CHECK(t.Collect(kMelee, ms(1300), o, 4) == 1 && o[0].hit && Near(o[0].ms, 100.0));
    }
    // out-of-range sources and times are refused; slots run out and come back
    {
        Tracker t;
        CHECK(!t.Press(-1, ms(10)) && !t.Press(kSources, ms(10)) && !t.Press(kMelee, 0) && !t.Press(kMelee, -5));
        for (int i = 0; i < kSlots; ++i) CHECK(t.Press(kMelee, ms(1000 + i)));
        CHECK(!t.Press(kMelee, ms(1100)));
        Tracker::Outcome o[kSlots];
        CHECK(t.Collect(kMelee, ms(5000), o, kSlots) == kSlots);
        CHECK(t.Press(kMelee, ms(5001)));
    }
    // a collect that is given room for one takes one
    {
        Tracker t;
        t.Press(kMelee, ms(1000));
        t.Press(kMelee, ms(1001));
        Tracker::Outcome o[1];
        CHECK(t.Collect(kMelee, ms(4000), o, 1) == 1);
        CHECK(t.Collect(kMelee, ms(4000), o, 1) == 1);
        CHECK(t.Collect(kMelee, ms(4000), o, 1) == 0);
    }
    // the statistics line: n, min, median (even and odd), max, the counts, and a new period after it
    {
        Stats s;
        for (int i = 0; i < 15; ++i) s.Press();
        const double v[] = {200, 100, 150, 400, 120, 130, 140, 160, 170, 180, 190, 110};   // 12 hits
        for (double x : v) s.Add({true, x});
        for (int i = 0; i < 3; ++i) s.Add({false, 0});
        CHECK(s.Any());
        char b[300];
        s.Line(b, sizeof b, "melee touch:");
        std::printf("%s\n", b);
        CHECK(std::strstr(b, "melee touch: press->hit ms n=12 min=100 median=155 max=400 | presses=15 hits=12 lost=3 (no hit within 1.2 s)") == b);
        CHECK(!s.Any());
        s.Add({true, 90});
        s.Press();
        s.Line(b, sizeof b, "shield smite:");
        CHECK(std::strstr(b, "shield smite: press->hit ms n=1 min=90 median=90 max=90 | presses=1 hits=1 lost=0") == b);
        s.Press();
        s.Line(b, sizeof b, "x");
        CHECK(std::strstr(b, "x press->hit ms n=0 min=- median=- max=- | presses=1 hits=0 lost=0") == b);
    }
    // who wants the observer
    {
        CHECK(!Wanted());
        Want(kMelee, true);
        CHECK(Wanted());
        Want(kShield, true);
        Want(kMelee, false);
        CHECK(Wanted());
        Want(kShield, false);
        CHECK(!Wanted());
    }
    constexpr int kN = 600;
    // a press from one thread, hits from another, the collect from a third: every press ends up answered or lost exactly once
    {
        Tracker t;
        std::atomic<bool> stop{false};
        std::atomic<int> pressed{0};
        std::thread hitter([&] { while (!stop.load()) t.Landed(NowNs()); });
        int hits = 0, lost = 0;
        std::thread presser([&] {
            for (int i = 0; i < kN; ++i) {
                while (!t.Press(i & 1 ? kShield : kMelee, NowNs())) std::this_thread::yield();
                ++pressed;
                for (volatile int k = 0; k < 3000; k = k + 1) {}
            }
        });
        Tracker::Outcome o[kSlots];
        const int64_t until = NowNs() + ms(6000);
        int total = 0;
        while (NowNs() < until && total < kN) {
            for (int src = 0; src < kSources; ++src) {
                const int n = t.Collect(src, NowNs(), o, kSlots);
                for (int i = 0; i < n; ++i) { ++total; o[i].hit ? ++hits : ++lost; CHECK(!o[i].hit || (o[i].ms >= 0 && o[i].ms < 1200.0)); }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        presser.join();
        stop.store(true);
        hitter.join();
        for (int rounds = 0; rounds < 20 && total < kN; ++rounds) {   // the rest expires
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            for (int src = 0; src < kSources; ++src) {
                const int n = t.Collect(src, NowNs(), o, kSlots);
                for (int i = 0; i < n; ++i) { ++total; o[i].hit ? ++hits : ++lost; }
            }
        }
        std::printf("threads: %d presses, %d answered, %d lost\n", pressed.load(), hits, lost);
        CHECK(total == kN && hits + lost == kN);
        CHECK(hits > kN * 95 / 100);   // the hitter answers all the time
    }
    std::printf("hit_latency_test: %d checks, %d failed\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
