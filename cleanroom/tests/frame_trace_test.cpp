// frame_trace_test.cpp - vr/frame_trace.h on its own: the meter, the probes, the situation of a pair, the slow-pair line and the
// summary, on made-up pairs (no game, no headset).
//   frame_trace_test.exe     exit code 0 = every check passed
#include "frame_trace.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <thread>

using namespace frametrace;

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)
static bool Has(const char* text, const char* part) { return std::strstr(text, part) != nullptr; }
static bool Near(double a, double b, double eps = 1e-6) { return a > b - eps && a < b + eps; }
static void SleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

constexpr int64_t ms(double v) { return (int64_t)(v * 1e6); }   // milliseconds -> ns

static Sample SlowSample(double cycleMs, Ctx ctx = kIdle) {
    Sample s;
    s.p.cycle = cycleMs * 1000.0;
    s.p.gap = 700.0;
    s.p.xr = true;
    s.p.begin = 400.0;
    s.p.wait = 200.0;
    s.p.left = 0.5 * cycleMs * 1000.0;
    s.p.right = 0.3 * cycleMs * 1000.0;
    s.p.end = 900.0;
    s.ctx = ctx;
    return s;
}

int main() {
    // the meter: adds, the longest, the draw thread's share, events; Take clears
    {
        Meter m;
        m.Add(kSkeleton, 1000000, false);   // 1 ms
        m.Add(kSkeleton, 3000000, true);    // 3 ms, on the draw thread
        m.Add(kHitHook, 250000, true);
        m.Note(kHit, 1234);
        m.Note(kHit, 5678);
        m.Note(kSweep, 99);
        Sections s;
        m.Take(&s);
        CHECK(s.n[kSkeleton] == 2 && s.us[kSkeleton] > 3999.0 && s.us[kSkeleton] < 4001.0);
        CHECK(s.drawUs[kSkeleton] > 2999.0 && s.drawUs[kSkeleton] < 3001.0);
        CHECK(s.maxUs[kSkeleton] > 2999.0 && s.maxUs[kSkeleton] < 3001.0);
        CHECK(s.n[kHitHook] == 1 && s.n[kLog] == 0);
        CHECK(s.ev[kHit] == 2 && s.ev[kSweep] == 1 && s.ev[kPress] == 0);
        CHECK(m.Last(kHit) == 5678 && m.Last(kSweep) == 99 && m.Last(kPress) == 0);
        m.Take(&s);   // taken once
        CHECK(s.n[kSkeleton] == 0 && s.us[kSkeleton] == 0.0 && s.ev[kHit] == 0 && s.maxUs[kSkeleton] == 0.0);
        CHECK(m.Last(kHit) == 5678);   // when it last happened stays
        m.Add((Section)99, 5, false);  // out of range: ignored
        m.Add(kLog, -5, false);
        m.Take(&s);
        CHECK(s.n[kLog] == 0);
    }
    // the probes: off reads nothing, on adds the time, Pause leaves the game's own call out
    {
        Sections s;
        SetOn(false);
        Global().Take(&s);
        { Scope sc(kProbe); SleepMs(3); }
        Note(kHit);
        Global().Take(&s);
        CHECK(s.n[kProbe] == 0 && s.ev[kHit] == 0);
        SetOn(true);
        { Scope sc(kProbe); SleepMs(3); }
        Global().Take(&s);
        CHECK(s.n[kProbe] == 1 && s.us[kProbe] >= 2000.0 && s.us[kProbe] < 500000.0 && s.drawUs[kProbe] == 0.0);
        {
            Scope sc(kAimHooks);
            sc.Pause();
            SleepMs(60);   // the game's function
            sc.Resume();
        }
        Global().Take(&s);
        CHECK(s.n[kAimHooks] == 1 && s.us[kAimHooks] < 40000.0);   // the 60 ms were not ours
        MarkDrawThread();
        { Scope sc(kHaptic); SleepMs(2); }
        Note(kPress);
        Global().Take(&s);
        CHECK(s.n[kHaptic] == 1 && s.drawUs[kHaptic] >= 1500.0 && s.ev[kPress] == 1);
        DrawThreadFlag() = false;
        { Scope sc(kLog); }
        Global().Take(&s);
        CHECK(s.n[kLog] == 1 && s.drawUs[kLog] == 0.0);
        // switched off in the middle: the scope that began keeps its reading; one that began while off never reads
        {
            Scope sc(kDirect);
            SetOn(false);
        }
        Global().Take(&s);
        CHECK(s.n[kDirect] == 1);
        {
            Scope sc(kDirect);
            SetOn(true);
            sc.Pause();
            sc.Resume();
        }
        Global().Take(&s);
        CHECK(s.n[kDirect] == 0);
        SetOn(false);
        Global().Take(&s);
    }
    // what a probe costs (told, and held to a loose bound): off it is a load and a branch, on two clock reads and a few atomic adds
    {
        constexpr int kN = 200000;
        Sections s;
        SetOn(false);
        int64_t t0 = NowNs();
        for (int i = 0; i < kN; ++i) { Scope sc(kSkeleton); }
        const double offNs = (double)(NowNs() - t0) / kN;
        SetOn(true);
        Global().Take(&s);
        t0 = NowNs();
        for (int i = 0; i < kN; ++i) { Scope sc(kSkeleton); }
        const double onNs = (double)(NowNs() - t0) / kN;
        Global().Take(&s);
        SetOn(false);
        std::printf("frame_trace_test: a probe costs %.1f ns off, %.1f ns on\n", offNs, onNs);
        CHECK(s.n[kSkeleton] == (uint32_t)kN);
        CHECK(offNs < 100.0 && onNs < 5000.0);
    }
    // the situation of a pair
    {
        LastTimes t;
        const int64_t now = ms(10000);
        CHECK(Classify(t, now, false) == kIdle);
        CHECK(Classify(t, now, true) == kWalk);
        t.sweep = now - ms(100);
        CHECK(Classify(t, now, false) == kSwing && Classify(t, now, true) == kSwing);   // a swing while walking is a swing
        t.sweep = now - ms(400);
        CHECK(Classify(t, now, false) == kIdle);   // the sweep is over
        t.press = now - ms(400);
        CHECK(Classify(t, now, false) == kSwing);  // the game's attack is under way
        t.press = now - ms(500);
        CHECK(Classify(t, now, false) == kIdle);
        t.hit = now - ms(200);
        CHECK(Classify(t, now, true) == kHitCtx);  // a hit beats everything
        t.hit = now - ms(350);
        CHECK(Classify(t, now, false) == kIdle);
        t.hit = now + ms(5);                       // a clock that read earlier than the event: not "within"
        CHECK(Classify(t, now, false) == kIdle);
    }
    // the slow-pair line says what it knows
    {
        char b[2048];
        Sample s = SlowSample(32.0, kHitCtx);
        s.s.ev[kSweep] = 4; s.s.ev[kPress] = 1; s.s.ev[kHit] = 1;
        s.s.n[kSkeleton] = 311; s.s.us[kSkeleton] = 620.0; s.s.maxUs[kSkeleton] = 50.0; s.s.drawUs[kSkeleton] = 100.0;
        s.s.n[kProbe] = 1; s.s.us[kProbe] = 410.0; s.s.maxUs[kProbe] = 410.0;
        s.s.n[kUpdateTick] = 30; s.s.maxUs[kUpdateTick] = 1200.0; s.s.maxUs[kMeleeTick] = 900.0;
        s.caughtAv = 3;
        s.p.cpu = 9100.0;
        s.p.moving = true;
        FormatSlow(b, sizeof b, s, 15.0, 4, 28000.0);
        CHECK(Has(b, "vrcam: frame trace - a pair took 32.0 ms (over 15)"));
        CHECK(Has(b, "XR begin 0.4 (xrWaitFrame 0.2)") && Has(b, "left 16.0 | right 9.6") && Has(b, "XR end 0.9"));
        CHECK(Has(b, "the draw thread worked 9.1 ms of the 26.9 it was in the pair"));
        CHECK(Has(b, "hit landed while moving"));
        CHECK(Has(b, "4 sweeps, 1 presses, 1 hits"));
        CHECK(Has(b, "skeleton hook 0.62 ms (n=311, longest 0.05, on the draw thread 0.10)"));
        CHECK(Has(b, "probe job 0.41 ms (n=1, longest 0.41)"));
        CHECK(!Has(b, "haptic call"));   // not measured in this pair: not listed
        CHECK(Has(b, "update thread: longest round 1.20 ms (hit by touch 0.90)"));
        CHECK(Has(b, "3 access violation(s) caught"));
        CHECK(Has(b, "+4 more over 15 ms in this second, the worst 28.0 ms"));
        // a pass (eyes by turns), no runtime, nothing of ours measured
        Sample t = SlowSample(21.0, kWalk);
        t.p.xr = false; t.p.turns = true; t.p.moving = true;
        FormatSlow(b, sizeof b, t, 15.0, 0, 0.0);
        CHECK(Has(b, "a pass took 21.0 ms") && Has(b, "| pass 10.5") && !Has(b, "XR begin") && !Has(b, "right"));
        CHECK(Has(b, "walking") && !Has(b, "while moving"));
        CHECK(Has(b, "measured: nothing in this pair") && !Has(b, "more over") && !Has(b, "access violation"));
        // a broken number does not break the line
        t.p.gap = std::numeric_limits<double>::quiet_NaN();
        FormatSlow(b, sizeof b, t, 15.0, 0, 0.0);
        CHECK(Has(b, "game 0.0"));
        // a buffer too small for it: cut, still ended
        char tiny[40];
        const int w = FormatSlow(tiny, sizeof tiny, s, 15.0, 0, 0.0);
        CHECK(w == (int)std::strlen(tiny) && w == (int)sizeof tiny - 1);
        char zero[1] = {'x'};
        FormatSlow(zero, 0, s, 15.0, 0, 0.0);   // (no room: nothing written, no crash)
        CHECK(zero[0] == 'x');
    }
    // the recorder: nothing under the threshold
    {
        Recorder r(15.0);
        Recorder::Lines out;
        for (uint64_t t = 0; t < 5000; t += 20) {   // 5 s of 14 ms pairs
            r.Pair(t, SlowSample(14.0), &out);
            CHECK(out.n == 0);
        }
    }
    // a slow pair is told when its second is over, not before - the worst of the second, with how many others; then the summary
    {
        Recorder r(15.0);
        Recorder::Lines out;
        r.Pair(0, SlowSample(14.0), &out);
        r.Pair(200, SlowSample(16.0), &out);
        r.Pair(400, SlowSample(40.0, kSwing), &out);
        r.Pair(600, SlowSample(25.0), &out);
        r.Pair(800, SlowSample(14.0), &out);
        CHECK(out.n == 0);
        r.Pair(1050, SlowSample(14.0), &out);
        CHECK(out.n == 1);
        CHECK(Has(out.text[0], "took 40.0 ms") && Has(out.text[0], "swinging") && Has(out.text[0], "+2 more over 15 ms in this second, the worst 25.0 ms"));
        r.Pair(1100, SlowSample(14.0), &out);
        CHECK(out.n == 0);   // it was told once
        // a stall that is a pair long by itself is told on its own pair, once the second is over
        r.Pair(3000, SlowSample(900.0), &out);
        CHECK(out.n == 1 && Has(out.text[0], "took 900.0 ms") && !Has(out.text[0], "more over"));
        // the summary, at the first pair 10 s after the first
        r.Pair(9999, SlowSample(14.0), &out);
        CHECK(out.n == 0);
        r.Pair(10100, SlowSample(14.0, kIdle), &out);
        CHECK(out.n == 1 && Has(out.text[0], "frame trace, last 10 s: 10 pairs"));
        CHECK(Has(out.text[0], "| idle 9 pairs") && Has(out.text[0], "| walking 0 pairs") && Has(out.text[0], "| swinging 1 pairs") &&
              Has(out.text[0], "| hit landed 0 pairs"));
        CHECK(Has(out.text[0], "worst 900.0"));
    }
    // every pair slow (a 50 Hz machine against a 15 ms line) is still one line a second, and a summary every ten
    {
        Recorder r(15.0);
        Recorder::Lines out;
        int slow = 0, summaries = 0;
        for (uint64_t t = 0; t <= 20000; t += 20) {
            r.Pair(t, SlowSample(20.0, t % 100 == 0 ? kSwing : kIdle), &out);
            for (int i = 0; i < out.n; ++i) {
                if (Has(out.text[i], "frame trace - a pair")) ++slow;
                if (Has(out.text[i], "frame trace, last")) ++summaries;
            }
        }
        CHECK(slow >= 18 && slow <= 21);
        CHECK(summaries == 2);
    }
    // the summary counts the pairs by situation and the plugin's part of each
    {
        Recorder r(15.0);
        Recorder::Lines out;
        std::string last;
        auto one = [&](uint64_t t, Ctx c, unsigned av) {
            Sample s = SlowSample(c == kHitCtx ? 30.0 : c == kSwing ? 20.0 : 12.0, c);
            s.s.n[kSkeleton] = 100;
            s.s.us[kSkeleton] = 400.0;
            s.caughtAv = av;
            r.Pair(t, s, &out);
            for (int k = 0; k < out.n; ++k) if (Has(out.text[k], "frame trace, last")) last = out.text[k];
        };
        for (int i = 0; i < 99; ++i) one((uint64_t)i * 100, i < 59 ? kIdle : i < 79 ? kWalk : i < 94 ? kSwing : kHitCtx, i == 3 ? 7u : 0u);
        CHECK(last.empty());
        one(10000, kIdle, 0);   // the hundredth pair, the one that finds the ten seconds over
        CHECK(!last.empty());
        CHECK(Has(last.c_str(), "100 pairs") && Has(last.c_str(), "20 over 15 ms (20.0%)"));
        CHECK(Has(last.c_str(), "| idle 60 pairs (0 over = 0.0%") && Has(last.c_str(), "| walking 20 pairs"));
        CHECK(Has(last.c_str(), "| swinging 15 pairs (15 over = 100.0%, mean 20.0, worst 20.0)"));
        CHECK(Has(last.c_str(), "| hit landed 5 pairs (5 over = 100.0%, mean 30.0, worst 30.0)"));
        CHECK(Has(last.c_str(), "measured, ms a pair (all threads): skeleton hook 0.400"));
        CHECK(Has(last.c_str(), "7 access violation(s) caught"));
    }
    // the threshold: clamped, and a NaN falls back to the default
    {
        Recorder r(0.5);
        CHECK(Near(r.OverMs(), kMinOverMs));
        r.SetOverMs(1e9);
        CHECK(Near(r.OverMs(), kMaxOverMs));
        r.SetOverMs(std::numeric_limits<double>::quiet_NaN());
        CHECK(Near(r.OverMs(), kDefaultOverMs));
        r.SetOverMs(40.0);
        CHECK(Near(r.OverMs(), 40.0));
        Recorder::Lines out;
        Sample s = SlowSample(30.0);
        r.Pair(0, s, &out);
        r.Pair(1500, s, &out);
        CHECK(out.n == 0);   // 30 ms is under 40
        Sample bad = SlowSample(30.0);
        bad.p.cycle = std::numeric_limits<double>::quiet_NaN();   // a pair with no usable time counts as no pair
        r.Pair(3000, bad, &out);
        CHECK(out.n == 0);
    }

    if (g_fail) std::printf("frame_trace_test: %d of %d checks FAILED\n", g_fail, g_n);
    else std::printf("frame_trace_test: all %d checks passed\n", g_n);
    return g_fail ? 1 : 0;
}
