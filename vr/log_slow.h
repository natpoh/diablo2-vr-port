#pragma once
// log_slow.h - [debug] log_async_auto: is the host's log file slow on this PC? Pure (no host, no game, no Windows): the lines' write times in,
// one verdict out. vr/log_async.h times each line a thread writes itself and, on the verdict, hands the lines to the writer thread of
// [debug] log_async (vr/log_queue.h) for the rest of the run.
//
// Why: a Quest 2 player's 0.181 logs (2026-10-26, GTX 1650 SUPER, the game on D:) - the lines of one block, with nothing between them, are
// 29 ms apart (the median gap in 1228 runs of five or more lines; the plugin's start "settings (d2r_vr.ini) - done (1234 ms)" for about 30
// lines; the watchdog's dump 450 lines in 26 s). On the author's PC a line is 1.2 ms. Each Frozen Orb cast writes about eleven lines on the
// game's thread (the pad, the spell aim, the attack target's callers, the arm), and the pair right after it says "game 334.8" ms: "every cast is
// a lag spike". With the writer thread the game's thread copies the line and goes on.
//
// The verdict: of the last kWindow lines written by their own threads, kSlowNeeded or more took kSlowMs or longer. A single slow line (the
// disk spinning up, an antivirus look) is not enough; at 1.2 ms a line it never comes.

#include <algorithm>
#include <cstdint>

namespace logslow {

constexpr double kSlowMs = 5.0;   // a line this slow counts (four times the author's PC's)
constexpr int kWindow = 16;        // the last 16 lines
constexpr int kSlowNeeded = 8;     // half of them slow: the log's file is slow here

struct Verdict {
    bool slow = false;     // the verdict is in: slow
    int slowLines = 0;     // of the window, the slow ones
    double medianMs = 0;   // the window's median write time
    double worstMs = 0;    // the window's slowest
};

// Not thread safe: the caller holds a lock (vr/log_async.h) or feeds from one thread.
class Detector {
public:
    // One line's write time, ms. The verdict as it stands after it (slow only once the window is full).
    Verdict Feed(double ms) {
        if (!(ms >= 0.0)) ms = 0.0;   // (NaN, a clock that went back)
        ms_[next_] = ms;
        next_ = (next_ + 1) % kWindow;
        if (count_ < kWindow) ++count_;
        Verdict v;
        if (count_ < kWindow) return v;
        double sorted[kWindow];
        std::copy(ms_, ms_ + kWindow, sorted);
        std::sort(sorted, sorted + kWindow);
        for (double x : sorted) if (x >= kSlowMs) ++v.slowLines;
        v.medianMs = 0.5 * (sorted[kWindow / 2 - 1] + sorted[kWindow / 2]);
        v.worstMs = sorted[kWindow - 1];
        v.slow = v.slowLines >= kSlowNeeded;
        return v;
    }
    void Reset() { count_ = next_ = 0; }
    int Count() const { return count_; }

private:
    double ms_[kWindow] = {};
    int count_ = 0, next_ = 0;
};

// Who wants the writer thread: [debug] log_async (explicit), or the verdict while [debug] log_async_auto is on. Not thread safe either.
class AutoSwitch {
public:
    // [debug] log_async_auto. Off forgets the verdict; true: the writer thread is to stop now (only the verdict had it on).
    bool SetAuto(bool on) {
        const bool was = Wanted();
        if (on == auto_) return false;
        auto_ = on;
        det_.Reset();
        if (!on) fired_ = false;
        return was && !Wanted();
    }
    // [debug] log_async. true: the writer thread is wanted after it (the key, or the verdict already in).
    bool SetExplicit(bool on) {
        explicit_ = on;
        return Wanted();
    }
    // The lines a thread writes itself are to be timed: auto on, no verdict yet.
    bool Timing() const { return auto_ && !fired_; }
    // One line's write time (only while Timing). true once, when the verdict comes in: start the writer thread. *v: the window's numbers.
    bool Feed(double ms, Verdict* v) {
        if (!Timing()) return false;
        const Verdict now = det_.Feed(ms);
        if (v) *v = now;
        if (!now.slow) return false;
        fired_ = true;
        return true;
    }
    bool Wanted() const { return explicit_ || fired_; }
    bool Fired() const { return fired_; }

private:
    Detector det_;
    bool auto_ = false, explicit_ = false, fired_ = false;
};

}   // namespace logslow
