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
//
// The ladder (Options::ladder, Serg 2026-11-08 - "если разрешением не спасти, то менять пресет"): instead of 2 - 4, fixed steps in a fixed
// order, each measured, the first that meets the target kept and the run over: frames fine at the baseline - nothing changes, on any card
// (a 5090's supersampling stays); else the render size down to 1600, then the presets Medium -> Low -> Potato, then the size down to 1300,
// then foveated rendering (Medium, then Maximum - "а потом фоверацию"), then the size down to 1200 ("а потом 1200 на 1200"), never lower,
// then eyes by turns with the fill ("если недостаточно fps, то делаем проход по очереди", "после 1200 пусть будет по очереди") - by turns
// and 1200 swap with one flag (SergLadder's turnsBefore1200; the run's kTurnsBefore1200).
// Before the presets go down to Low and Potato (Serg 2026-11-10: "перед переключением на лоу и потато он должен переключить режим на глаза по
// очереди" - at 1280x720 Medium one pass 107 fps, CPU 8.9 ms, eyes by turns 114, 7.9) eyes by turns is TRIED at the preset and size as they
// stand: kept and the run over if it meets the target; short, the presets go down with whichever of the two measured better (LadderStep::trial).
// Serg 2026-11-10 settles the order: "сперва уменьшаем до 1600 размера потом по очереди глаза" - the size to 1600, then eyes by turns (the trial
// above), then the presets, the size under 1600 last.
// The climb (Options::climbKnob, Serg 2026-11-10: "если много кадров то можно поднять обратно немного"; "80 фпс на 1600, на глазах по очереди
// будет 90, но можно поднять графику до 2100"): a rung that meets the target with room to spare (the frame's own time under the budget less
// climbMargin) starts a climb - the render size searched back up on its 64 px grid (halving the gap: the size it stopped at to the player's
// own) while the target holds with that margin, then, the size back at the player's own and room still left, the preset a level up at a time.
// The last set that held stays; a final miss undoes the climb (the rung's set again).
// Out of steps and still short: BelowMinimum() - the PC is below what VR needs here (no half rate in the ladder). SergLadder makes the rungs.

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
    float hz = 0;         // the headset's own rate (HeadsetRate below - never a period the runtime stretched)
};

// The headset's own refresh rate. Not the frame's predicted display period: SteamVR stretches it while the app cannot keep up (motion
// smoothing / reprojection: 22.22, 33.33, 44.44 ms on a 90 Hz PS VR2 - Serg's 0.211.2 Auto said "headset 22 Hz" at 43 frames/s and
// changed nothing), so a target taken from it falls with the very slowness it should fix.
// PeriodFloor keeps the shortest period seen this session (the session starts at the headset's own, in the menus); a stretched period is
// a whole multiple of it.
class PeriodFloor {
public:
    // A frame's predicted display period, ns. Out of any headset's range (under 1/240 s, over 1/20 s): not a rate.
    void Note(double ns) {
        if (!(ns >= 1e9 / 240.0 && ns <= 1e9 / 20.0)) return;
        if (shortest_ <= 0 || ns < shortest_) shortest_ = ns;
    }
    double Shortest() const { return shortest_; }   // 0: none seen
    void Reset() { shortest_ = 0; }
private:
    double shortest_ = 0;
};

enum class RateFrom { Period, IniMultiple, Doubled, Ini, None };
inline const char* RateFromName(RateFrom f) {
    switch (f) {
        case RateFrom::Period: return "the shortest display period of the session";
        case RateFrom::IniMultiple: return "[stereo] headset_rate (the shortest period seen is a whole multiple of it)";
        case RateFrom::Doubled: return "the shortest period seen, times a whole number (no headset runs under 60 Hz)";
        case RateFrom::Ini: return "[stereo] headset_rate (no display period seen)";
        default: return "none";
    }
}
struct Rate {
    float hz = 0;
    RateFrom from = RateFrom::None;
};
// shortestPeriodNs: PeriodFloor::Shortest(); iniHz: [stereo] headset_rate in Hz (0: not known).
inline Rate HeadsetRate(double shortestPeriodNs, float iniHz) {
    Rate r;
    if (shortestPeriodNs > 0) {
        const float seen = (float)(1e9 / shortestPeriodNs);
        if (iniHz > 0 && seen < iniHz * 0.97f) {   // the whole session stretched: the ini's rate when the seen one is a whole share of it
            const float k = iniHz / seen;
            if (std::fabs(k - std::round(k)) < 0.06f) return {iniHz, RateFrom::IniMultiple};
        }
        if (seen >= 59.0f) return {seen, RateFrom::Period};
        // stretched all session: the first whole multiple that is a headset's rate (45 -> 90, 36 -> 72, 22.5 -> 90 not 67.5), else the first over 60
        static constexpr float kRates[] = {60.0f, 72.0f, 75.0f, 80.0f, 90.0f, 120.0f, 144.0f};
        for (int k = 2; k <= 8; ++k)
            for (const float hz : kRates)
                if (std::fabs(seen * k - hz) < hz * 0.02f) return {hz, RateFrom::Doubled};
        float hz = seen;
        while (hz < 59.0f) hz += seen;
        return {hz, RateFrom::Doubled};
    }
    if (iniHz > 0) return {iniHz, RateFrom::Ini};
    return r;
}

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
    bool trial = false;   // a tried rung (LadderStep::trial): kept says whether it stayed (it met, or measured better than before it)
};

// One rung of the ladder: knob `knob` set to `level` (taken only when that is below where it stands). A trial rung is measured and, short of
// the target, kept only when it measured better than the set before it (more frames/s; the same frames/s - a runtime that locks to a share of
// its rate - less of the frame's own time), else undone before the next rung.
struct LadderStep { int knob = 0; int level = 0; bool trial = false; };

// A trial's measure against the one before it: better, as LadderStep::trial says.
inline bool MeasuredBetter(float fps, float busyMs, float fpsWas, float busyWas) {
    if (fps > fpsWas * 1.01f) return true;
    if (fps < fpsWas * 0.99f) return false;
    return busyMs < busyWas;
}

// Serg's ladder over the run's knobs (-1: a knob not offered). The size knob's levels go from 0 (the smallest) up to sizeStart (the player's
// own); size1600 / size1300 are the levels of those sizes (-1: none - the player's own was at or under it). The preset and the foveation
// knobs: level 0 the cheapest, their start the player's own. Eyes by turns (turnsKnob: level 0 by turns, its start the player's one pass)
// before the size's last drop to 1200, or after it with turnsBefore1200 false (Serg to confirm which).
// sizeRungs (a fine size grid, the climb's): the levels the first drop steps through, largest first, down to size1600 - then the drops under
// it jump straight to 1300 and to level 0 (1200); none: every level is a rung (a coarse size knob).
inline std::vector<LadderStep> SergLadder(int sizeKnob, int sizeStart, int size1600, int size1300, int presetKnob, int presetStart, int fovKnob,
                                          int fovStart, int turnsKnob = -1, int turnsStart = 0, bool turnsBefore1200 = true,
                                          const std::vector<int>* sizeRungs = nullptr) {
    std::vector<LadderStep> r;
    const int mid = sizeKnob >= 0 && size1600 >= 0 ? size1600 : sizeStart;          // the size after the first drop (1600, or his own)
    const int low = sizeKnob >= 0 && size1300 >= 0 && size1300 < mid ? size1300 : mid;   // ... after the second (1300, or as it was)
    if (sizeRungs && sizeKnob >= 0) {
        std::vector<LadderStep> t;
        for (const int l : *sizeRungs) if (l < sizeStart && l >= mid) t.push_back({sizeKnob, l});
        if (turnsKnob >= 0 && turnsStart > 0) t.push_back({turnsKnob, 0, true});   // eyes by turns at 1600 (Serg: "потом по очереди глаза")
        if (presetKnob >= 0) for (int l = presetStart - 1; l >= 0; --l) t.push_back({presetKnob, l});
        if (low < mid) t.push_back({sizeKnob, low});
        if (fovKnob >= 0) for (int l = fovStart - 1; l >= 0; --l) t.push_back({fovKnob, l});
        if (turnsKnob >= 0 && turnsBefore1200) for (int l = turnsStart - 1; l >= 0; --l) t.push_back({turnsKnob, l});
        if (low > 0) t.push_back({sizeKnob, 0});
        if (turnsKnob >= 0 && !turnsBefore1200) for (int l = turnsStart - 1; l >= 0; --l) t.push_back({turnsKnob, l});
        return t;
    }
    if (sizeKnob >= 0) for (int l = sizeStart - 1; l >= mid; --l) r.push_back({sizeKnob, l});
    // eyes by turns tried at 1600, before Low / Potato (Serg 2026-11-10); kept short of the target only if it measured better - then the later rung is nothing
    if (turnsKnob >= 0 && turnsStart > 0) r.push_back({turnsKnob, 0, true});
    if (presetKnob >= 0) for (int l = presetStart - 1; l >= 0; --l) r.push_back({presetKnob, l});
    if (sizeKnob >= 0) for (int l = mid - 1; l >= low; --l) r.push_back({sizeKnob, l});
    if (fovKnob >= 0) for (int l = fovStart - 1; l >= 0; --l) r.push_back({fovKnob, l});
    if (turnsKnob >= 0 && turnsBefore1200) for (int l = turnsStart - 1; l >= 0; --l) r.push_back({turnsKnob, l});
    if (sizeKnob >= 0) for (int l = low - 1; l >= 0; --l) r.push_back({sizeKnob, l});
    if (turnsKnob >= 0 && !turnsBefore1200) for (int l = turnsStart - 1; l >= 0; --l) r.push_back({turnsKnob, l});
    return r;
}

struct Options {
    std::vector<LadderStep> ladder;   // non-empty: the fixed ladder instead of probe / down / up (see above)
    float fpsTolDown = 0.03f;   // the target counts as met at (1 - this) x target frames/s
    float fpsTolUp = 0.015f;    // a raise is kept only at (1 - this) x target (a little headroom: no flapping)
    float minGainMs = 0.25f;    // a probe that wins less than this is "costs nothing measurable": never turned down
    int retries = 3;            // invalid measures of one set in a row before the run gives up (all back to the start)
    bool halfRate = true;       // short of the headset's rate with everything down: aim at half of it
    // the ladder's climb (see above): the size knob searched back up (-1: none), then the preset knob a level at a time (-1: none); a raise holds
    // at the target with the frame's own time at most (1 - climbMargin) of the budget (1000 / target ms)
    int climbKnob = -1, climbPresetKnob = -1;
    float climbMargin = 0.07f;
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
    bool BelowMinimum() const { return below_; }                 // the ladder's last rung measured and still short of the target
    float Target() const { return target_; }
    float Hz() const { return hz_; }
    float ProbeGainMs(size_t i) const { return i < gain_.size() ? gain_[i] : -1.0f; }   // proved cost, -1 not probed
    const std::vector<LogRow>& Log() const { return log_; }
    const Measure& Last() const { return last_; }                // the measure of Result()
    int Measured() const { return measured_; }                   // measures fed so far (valid ones)
    // A rough count of measures still to come, for a progress bar.
    int Remaining() const {
        if (ph_ == Phase::Done) return 0;
        if (climbing_) {   // the climb: about the halvings left of the size's gap, a preset step, the final
            int n = 2;
            for (int gap = climbHi_ - climbLo_; gap > 0; gap /= 2) ++n;
            return n;
        }
        if (!o_.ladder.empty()) {   // the rungs still ahead that would move something, and the final measure
            int n = 1;
            for (size_t r = ladderAt_; r < o_.ladder.size(); ++r) {
                const LadderStep& st = o_.ladder[r];
                if (st.knob >= 0 && st.knob < (int)k_.size() && st.level < cur_[(size_t)st.knob]) ++n;
            }
            return ph_ == Phase::Baseline ? n + 1 : n;
        }
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
            case Phase::Up: if (climbing_) OnClimb(m); else OnUp(m); break;
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
    bool reached_ = false, aborted_ = false, halved_ = false, below_ = false;
    size_t ladderAt_ = 0;
    LogRow pending_{};
    bool climbing_ = false, climbed_ = false;   // the ladder's climb runs / raised something (a final miss undoes it)
    int climbStage_ = 0, climbLo_ = 0, climbHi_ = 0;   // 0 the size (its held level, its highest still possible), 1 the preset
    std::vector<int> climbBase_;
    Measure climbBaseMeasure_{};

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
        if (!o_.ladder.empty()) {   // the ladder: frames fine - nothing changes; else its rungs in order
            if (Meets(m, false)) { reached_ = true; ph_ = Phase::Done; want_ = cur_; return; }
            ph_ = Phase::Down;
            ladderAt_ = 0;
            NextRung();
            return;
        }
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
        if (!o_.ladder.empty()) { TakeRung(m); return; }
        if (!TakeDown(m)) NextDown();
    }

    // The ladder's next rung that moves something; out of rungs: short - BelowMinimum, the final measure of where it ended.
    void NextRung() {
        while (ladderAt_ < o_.ladder.size()) {
            const LadderStep st = o_.ladder[ladderAt_++];
            if (st.knob < 0 || st.knob >= (int)k_.size()) continue;
            const size_t i = (size_t)st.knob;
            const int to = std::clamp(st.level, k_[i].floor, k_[i].levels - 1);
            if (to >= cur_[i]) continue;   // (not below where it stands: nothing to take)
            std::vector<int> w = cur_;
            w[i] = to;
            pending_ = LogRow{};
            pending_.knob = (int)i;
            pending_.from = cur_[i];
            pending_.to = to;
            pending_.trial = st.trial;
            Measure c;
            if (Ask(w, &c)) return;
            if (TakeRungMeasure(c)) return;   // (cached)
        }
        reached_ = false;
        below_ = true;
        StartFinal();
    }
    // A rung measured: kept (the way down); true when the target is met (the final measure next).
    bool TakeRungMeasure(const Measure& m) {
        const int i = pending_.knob;
        const bool met = Meets(m, false);
        // a trial short of the target: kept only if it measured better than the set before it; else undone (the next rung from before it)
        const bool keep = !pending_.trial || met || MeasuredBetter(m.fps, m.busyMs, last_.fps, last_.busyMs);
        Row(Phase::Down, i, pending_.from, pending_.to, last_, m, keep);
        log_.back().trial = pending_.trial;
        if (!keep) return false;
        cur_[(size_t)i] = pending_.to;
        last_ = m;
        if (met) { reached_ = true; StartClimb(); return true; }
        return false;
    }

    // -- the climb ------------------------------------------------------------------------------------------------------------
    bool Room(const Measure& m) const {
        return target_ > 0 && m.busyMs > 0 && Meets(m, true) && m.busyMs <= 1000.0f / target_ * (1.0f - o_.climbMargin);
    }
    bool Valid(int k) const { return k >= 0 && k < (int)k_.size(); }
    // A rung met: with room to spare, the size back up, then the preset; else straight to the final measure.
    void StartClimb() {
        climbBase_ = cur_;
        climbBaseMeasure_ = last_;
        climbed_ = false;
        if (!Room(last_) || (!Valid(o_.climbKnob) && !Valid(o_.climbPresetKnob))) { StartFinal(); return; }
        climbing_ = true;
        ph_ = Phase::Up;
        climbStage_ = 0;
        if (Valid(o_.climbKnob)) {
            climbLo_ = cur_[(size_t)o_.climbKnob];
            climbHi_ = k_[(size_t)o_.climbKnob].ceiling;
        } else {
            climbLo_ = climbHi_ = 0;
        }
        NextClimb();
    }
    void NextClimb() {
        for (;;) {
            std::vector<int> w = cur_;
            if (climbStage_ == 0 && Valid(o_.climbKnob) && climbLo_ < climbHi_) {   // the size: the middle of what is left (rounded up)
                const int mid = (climbLo_ + climbHi_ + 1) / 2;
                w[(size_t)o_.climbKnob] = mid;
                pending_ = LogRow{};
                pending_.knob = o_.climbKnob;
                pending_.from = cur_[(size_t)o_.climbKnob];
                pending_.to = mid;
            } else {
                climbStage_ = 1;   // the preset: only with the size back at the player's own, a level he had above where it is now, and room left
                const bool sizeTop = !Valid(o_.climbKnob) || cur_[(size_t)o_.climbKnob] >= k_[(size_t)o_.climbKnob].ceiling;
                const int p = o_.climbPresetKnob;
                if (!Valid(p) || !sizeTop || cur_[(size_t)p] >= k_[(size_t)p].start || !Room(last_)) {
                    climbing_ = false;
                    StartFinal();
                    return;
                }
                w[(size_t)p] = cur_[(size_t)p] + 1;
                pending_ = LogRow{};
                pending_.knob = p;
                pending_.from = cur_[(size_t)p];
                pending_.to = w[(size_t)p];
            }
            Measure c;
            if (Ask(w, &c)) return;
            if (!TakeClimb(c)) { climbing_ = false; StartFinal(); return; }   // (cached)
        }
    }
    // A climb step measured. False: the climb is over (a preset raise missed).
    bool TakeClimb(const Measure& m) {
        const bool holds = Room(m);
        const int k = pending_.knob;
        Row(Phase::Up, k, pending_.from, pending_.to, last_, m, holds);
        if (climbStage_ == 0) {
            if (holds) { cur_[(size_t)k] = pending_.to; last_ = m; climbLo_ = pending_.to; climbed_ = true; }
            else climbHi_ = pending_.to - 1;
            return true;
        }
        if (!holds) return false;
        cur_[(size_t)k] = pending_.to;
        last_ = m;
        climbed_ = true;
        return true;
    }
    void OnClimb(const Measure& m) {
        if (!TakeClimb(m)) { climbing_ = false; StartFinal(); return; }
        NextClimb();
    }
    void TakeRung(const Measure& m) {
        if (!TakeRungMeasure(m)) NextRung();
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
        if (!o_.ladder.empty()) {   // the ladder: a climb that held only by luck - undone, the rung's set measured again; a rung by luck - on down
            if (!ok && climbed_) {
                climbed_ = false;
                cur_ = climbBase_;
                last_ = climbBaseMeasure_;
                want_ = cur_;
                return;
            }
            last_ = m;
            if (!ok && reached_ && ladderAt_ < o_.ladder.size()) {
                reached_ = false;
                ph_ = Phase::Down;
                NextRung();
                return;
            }
            if (!ok) { reached_ = false; below_ = ladderAt_ >= o_.ladder.size(); }
            ph_ = Phase::Done;
            want_ = cur_;
            return;
        }
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
