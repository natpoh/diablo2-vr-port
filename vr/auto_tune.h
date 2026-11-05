#pragma once
// auto_tune.h - [autotune] the "Auto" graphics tuning's step logic, pure (no Windows, no game, no time): which settings to try
// next and which to keep, from frames/s the caller measured with them. tools/auto_tune_test.cpp drives it with a fake cost
// table; vr/auto_tune_run.cpp applies its levels live in the game and measures them.
//
// The idea (Serg 2026-11-05): press Auto, the game tunes itself to the headset's rate.
//   1. Baseline - the player's own settings measured as they are. The target is the headset's rate.
//   2. Probe    - below the target, each thing that is on is turned down ONE level alone and measured: its real cost, proved
//                 live (our GPU timestamps under-read about 10x, so the logged cost only orders the probes). A thing that wins
//                 nothing measurable is never turned down.
//   3. Down     - the dearest per picture lost first (cost / worth), one at a time, measured after each, until the target is met.
//                 Everything down and still short: the target becomes half the headset's rate (a stable 45 for 90 Hz), as the
//                 runtime's reprojection holds it.
//   4. Up       - back up while the target holds: the most picture per millisecond first; a raise that misses is undone.
//                 Never past the player's own level (a thing he had off stays off) unless the knob says so (the render size).
//   5. Final    - the chosen settings measured once more over a longer window; a miss undoes the last raise.
// A settings set measured once is not measured again (the cache), so Up never retries the step Down just took.
//
// Frame generation is never a knob here (it needs a restart and is a lead in GPU hangs): the caller leaves it as it is.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace autotune {

// One thing the tuner may turn down. Level 0 is the cheapest: an effect has two (0 off, 1 on), the render size one per scale,
// the stereo mode two (0 eyes by turns, 1 full stereo).
struct Knob {
    const char* id = "";     // short name for the log ("sky", "fog", "scale", "stereo")
    int levels = 2;
    int start = 1;           // the player's level when Auto was pressed
    int floor = 0;           // the lowest the tuner may set
    int ceiling = -1;        // the highest it may raise to; -1 = start (never past the player's own choice)
    float worth = 1.0f;      // how much the picture loses per level taken off: higher = turned down later, raised sooner
    float hintMs = 0.0f;     // the GPU cost our own timestamps logged (under-read): orders the probes, guesses an unprobed raise
};

struct Measure {
    bool valid = false;   // false: a load, a menu, the headset off, too few frames - the same settings are measured again
    float fps = 0;        // frames the headset got a second (xrEndFrame)
    float busyMs = 0;     // a frame's own time: its interval less what xrWaitFrame blocked (holds when the runtime locks to half)
    float hz = 0;         // the headset's rate (1 / the predicted display period)
};

enum class Phase { Baseline, Probe, Down, Up, Final, Done };
inline const char* PhaseName(Phase p) {
    switch (p) {
        case Phase::Baseline: return "baseline";
        case Phase::Probe: return "probe";
        case Phase::Down: return "down";
        case Phase::Up: return "up";
        case Phase::Final: return "final";
        default: return "done";
    }
}

// One measured step: knob `knob` from level `from` to `to` (knob -1: the baseline / final measure of the whole set).
struct LogRow {
    Phase phase = Phase::Baseline;
    int knob = -1, from = 0, to = 0;
    float fpsBefore = 0, fpsAfter = 0, busyBefore = 0, busyAfter = 0;
    bool kept = false;
};

struct Options {
    float fpsTolDown = 0.03f;   // the target counts as met at (1 - this) x target frames/s
    float fpsTolUp = 0.015f;    // a raise is kept only at (1 - this) x target (a little headroom: no flapping)
    float minGainMs = 0.25f;    // a probe that wins less than this is "costs nothing measurable": never turned down
    int retries = 3;            // invalid measures of one set in a row before the run gives up (all back to the start)
    bool halfRate = true;       // short of the headset's rate with everything down: aim at half of it
};

class Tuner {
public:
    explicit Tuner(std::vector<Knob> knobs, Options o = {}) : k_(std::move(knobs)), o_(o) {
        for (auto& k : k_) {
            k.levels = std::max(1, k.levels);
            k.start = std::clamp(k.start, 0, k.levels - 1);
            k.floor = std::clamp(k.floor, 0, k.start);
            k.ceiling = k.ceiling < 0 ? k.start : std::clamp(k.ceiling, k.start, k.levels - 1);
        }
        cur_.resize(k_.size());
        for (size_t i = 0; i < k_.size(); ++i) cur_[i] = k_[i].start;
        gain_.assign(k_.size(), -1.0f);
        want_ = cur_;
    }

    const std::vector<Knob>& Knobs() const { return k_; }
    const std::vector<int>& Want() const { return want_; }      // the levels to apply and measure now (Result() once Done)
    const std::vector<int>& Result() const { return cur_; }
    Phase phase() const { return ph_; }
    bool Done() const { return ph_ == Phase::Done; }
    bool Reached() const { return reached_; }                    // the final set met the target
    bool Aborted() const { return aborted_; }                    // gave up (no valid measure): Result() is the start
    float Target() const { return target_; }
    float Hz() const { return hz_; }
    float ProbeGainMs(size_t i) const { return i < gain_.size() ? gain_[i] : -1.0f; }   // proved cost, -1 not probed
    const std::vector<LogRow>& Log() const { return log_; }
    const Measure& Last() const { return last_; }                // the measure of Result()
    int Measured() const { return measured_; }                   // measures fed so far (valid ones)
    // A rough count of measures still to come, for a progress bar.
    int Remaining() const {
        if (ph_ == Phase::Done) return 0;
        int probes = 0, room = 0;
        for (size_t i = 0; i < k_.size(); ++i) {
            if (gain_[i] < 0 && cur_[i] > k_[i].floor) ++probes;
            room += cur_[i] - k_[i].floor;
        }
        switch (ph_) {
            case Phase::Baseline: return 2 + probes + room / 2;
            case Phase::Probe: return 2 + probes + room / 2;
            case Phase::Down: return 2 + room / 2;
            case Phase::Up: return 1 + (int)upQueue().size();
            default: return 1;
        }
    }

    // The measure of Want(). Moves on and sets the next Want().
    void Feed(const Measure& m) {
        if (ph_ == Phase::Done) return;
        if (!m.valid || !(m.fps > 0) || !(m.hz > 0)) {
            if (++badInRow_ >= o_.retries) Abort();
            return;   // the same Want() again
        }
        badInRow_ = 0;
        ++measured_;
        if (ph_ != Phase::Final) cache_[want_] = m;
        switch (ph_) {
            case Phase::Baseline: OnBaseline(m); break;
            case Phase::Probe: OnProbe(m); break;
            case Phase::Down: OnDown(m); break;
            case Phase::Up: OnUp(m); break;
            case Phase::Final: OnFinal(m); break;
            default: break;
        }
    }

    // Stop now: everything back to the start (the caller applies Result()).
    void Abort() {
        aborted_ = true;
        reached_ = false;
        for (size_t i = 0; i < k_.size(); ++i) cur_[i] = k_[i].start;
        want_ = cur_;
        ph_ = Phase::Done;
    }

    bool Meets(const Measure& m, bool up) const { return m.fps >= target_ * (1.0f - (up ? o_.fpsTolUp : o_.fpsTolDown)); }

private:
    std::vector<Knob> k_;
    Options o_;
    std::vector<int> cur_, want_;
    std::vector<float> gain_;          // measured ms won by one level down (probe, later each Down step's own)
    std::map<std::vector<int>, Measure> cache_;
    std::vector<LogRow> log_;
    std::vector<char> upTried_;        // knobs whose raise missed (or was measured missing): not tried again
    Measure base_{}, last_{};
    Phase ph_ = Phase::Baseline;
    float hz_ = 0, target_ = 0;
    size_t probeAt_ = 0;
    std::vector<size_t> probeOrder_;
    int lastRaised_ = -1, lastRaisedFrom_ = 0, badInRow_ = 0, measured_ = 0;
    bool reached_ = false, aborted_ = false, halved_ = false;
    LogRow pending_{};

    void Row(Phase p, int knob, int from, int to, const Measure& before, const Measure& after, bool kept) {
        LogRow r;
        r.phase = p; r.knob = knob; r.from = from; r.to = to; r.kept = kept;
        r.fpsBefore = before.fps; r.fpsAfter = after.fps; r.busyBefore = before.busyMs; r.busyAfter = after.busyMs;
        log_.push_back(r);
    }

    // Asks for `w`; a set measured before is taken from the cache at once (the caller only ever sees new sets).
    // Returns false when it was cached - the caller then handles `cached` itself.
    bool Ask(const std::vector<int>& w, Measure* cached) {
        auto it = cache_.find(w);
        if (it != cache_.end()) { *cached = it->second; return false; }
        want_ = w;
        return true;
    }

    void OnBaseline(const Measure& m) {
        base_ = last_ = m;
        hz_ = m.hz;
        target_ = hz_;
        Row(Phase::Baseline, -1, 0, 0, m, m, true);
        if (Meets(m, false)) { reached_ = true; StartUp(); return; }
        // probe order: the logged cost first (it under-reads but ranks), only what is above its floor
        for (size_t i = 0; i < k_.size(); ++i) if (cur_[i] > k_[i].floor) probeOrder_.push_back(i);
        std::stable_sort(probeOrder_.begin(), probeOrder_.end(), [&](size_t a, size_t b) { return k_[a].hintMs > k_[b].hintMs; });
        ph_ = Phase::Probe;
        probeAt_ = 0;
        NextProbe();
    }

    void NextProbe() {
        while (probeAt_ < probeOrder_.size()) {
            const size_t i = probeOrder_[probeAt_];
            std::vector<int> w = cur_;
            --w[i];
            Measure c;
            if (Ask(w, &c)) return;   // wait for its measure
            RecordProbe(i, c);         // (cached: never in practice for probes)
            ++probeAt_;
        }
        StartDown();
    }

    void RecordProbe(size_t i, const Measure& m) {
        gain_[i] = base_.busyMs - m.busyMs;
        Row(Phase::Probe, (int)i, cur_[i], cur_[i] - 1, base_, m, false);
    }

    void OnProbe(const Measure& m) {
        RecordProbe(probeOrder_[probeAt_], m);
        ++probeAt_;
        NextProbe();
    }

    // The knob to turn down next: the dearest per picture lost; -1 none left that wins anything.
    int PickDown() const {
        int best = -1;
        float bestScore = 0;
        for (size_t i = 0; i < k_.size(); ++i) {
            if (cur_[i] <= k_[i].floor || gain_[i] < o_.minGainMs) continue;
            const float score = gain_[i] / std::max(0.01f, k_[i].worth);
            if (best < 0 || score > bestScore) { best = (int)i; bestScore = score; }
        }
        return best;
    }

    void StartDown() {
        ph_ = Phase::Down;
        NextDown();
    }

    void NextDown() {
        for (;;) {
            const int i = PickDown();
            if (i < 0) { OutOfDown(); return; }
            std::vector<int> w = cur_;
            --w[i];
            pending_ = LogRow{};
            pending_.knob = i;
            pending_.from = cur_[i];
            pending_.to = w[i];
            Measure c;
            if (Ask(w, &c)) return;
            if (TakeDown(c)) return;   // (cached - the probe from the start set: its measure stands)
        }
    }

    // A Down step measured: kept always (it is the way down). True when the target is met (Up has begun).
    bool TakeDown(const Measure& m) {
        const int i = pending_.knob;
        const float won = last_.busyMs - m.busyMs;
        if (won > 0) gain_[i] = won;          // the next level's guess: what this one won
        else gain_[i] = 0;                    // won nothing (the next level is not tried)
        Row(Phase::Down, i, pending_.from, pending_.to, last_, m, true);
        cur_[i] = pending_.to;
        last_ = m;
        if (Meets(m, false)) { reached_ = true; StartUp(); return true; }
        return false;
    }

    void OnDown(const Measure& m) {
        if (!TakeDown(m)) NextDown();
    }

    // Nothing more to turn down and still short of the target.
    void OutOfDown() {
        if (o_.halfRate && !halved_) {
            halved_ = true;
            target_ = hz_ * 0.5f;
            if (Meets(last_, false)) { reached_ = true; StartUp(); return; }
        }
        reached_ = false;
        StartFinal();
    }

    std::vector<size_t> upQueue() const {
        std::vector<size_t> q;
        for (size_t i = 0; i < k_.size(); ++i)
            if (cur_[i] < k_[i].ceiling && !(i < upTried_.size() && upTried_[i])) q.push_back(i);
        auto worthPerMs = [&](size_t i) {
            const float cost = gain_[i] > 0 ? gain_[i] : (k_[i].hintMs > 0 ? k_[i].hintMs * 10.0f : 1.0f);
            return k_[i].worth / std::max(0.05f, cost);
        };
        std::stable_sort(q.begin(), q.end(), [&](size_t a, size_t b) { return worthPerMs(a) > worthPerMs(b); });
        return q;
    }

    void StartUp() {
        ph_ = Phase::Up;
        upTried_.assign(k_.size(), 0);
        NextUp();
    }

    void NextUp() {
        for (;;) {
            const auto q = upQueue();
            if (q.empty()) { StartFinal(); return; }
            const size_t i = q.front();
            std::vector<int> w = cur_;
            ++w[i];
            pending_ = LogRow{};
            pending_.knob = (int)i;
            pending_.from = cur_[i];
            pending_.to = w[i];
            Measure c;
            if (Ask(w, &c)) return;
            TakeUp(c, true);
        }
    }

    void TakeUp(const Measure& m, bool cached) {
        const int i = pending_.knob;
        const bool keep = Meets(m, true);
        if (!cached || keep) Row(Phase::Up, i, pending_.from, pending_.to, last_, m, keep);
        if (keep) {
            if (m.busyMs > last_.busyMs) gain_[i] = m.busyMs - last_.busyMs;   // what this level costs (a "win" is noise: kept)
            lastRaised_ = i;
            lastRaisedFrom_ = pending_.from;
            cur_[i] = pending_.to;
            last_ = m;
        } else {
            upTried_[i] = 1;
        }
    }

    void OnUp(const Measure& m) {
        TakeUp(m, false);
        NextUp();
    }

    void StartFinal() {
        ph_ = Phase::Final;
        want_ = cur_;
    }

    void OnFinal(const Measure& m) {
        const bool ok = Meets(m, false);
        Row(Phase::Final, -1, 0, 0, last_, m, ok);
        if (!ok && reached_ && lastRaised_ >= 0) {   // the last raise held only by luck: undone, measured again
            cur_[lastRaised_] = lastRaisedFrom_;
            lastRaised_ = -1;
            want_ = cur_;
            return;
        }
        if (!ok) reached_ = false;
        last_ = m;
        ph_ = Phase::Done;
        want_ = cur_;
    }
};

}  // namespace autotune
