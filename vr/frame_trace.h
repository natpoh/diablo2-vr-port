#pragma once
// frame_trace.h - [debug] frame_trace: where the time of a SLOW pair went, our own part of it counted apart. Measurement only.
//
// A stutter while swinging is a pair of 20-40 ms among pairs of 11-18 ms: far under the 80 ms line of vr/frame_hitch.h, and it comes
// together with the plugin's work around the swing and the hit - the pose hook (every skeleton the game poses), the hit observer, the
// aim hooks, the server's unit lookups, the 100 ms probe job on the game's thread, the haptic call, the log. The 10 s averages hide it.
// With [debug] frame_trace=1 each of these costs is added up between two pairs; a pair longer than [debug] frame_trace_ms (15) is
// told once the second is over (the worst pair of the second), and every 10 s a summary says how many pairs were long in each
// situation - idle, walking, swinging, a hit landed - so a log sent in says whether swings (and hits) are really where the long pairs are.
//
// Nothing here calls the game or changes what happens in play. Off (the default) a probe is one relaxed load. On, a probe is two
// reads of the steady clock (QueryPerformanceCounter) and a few relaxed atomic adds.
//
// Pure (no game, no windows.h): tested in cleanroom/tests/frame_trace_test.cpp.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace frametrace {

constexpr double kDefaultOverMs = 15.0;   // a pair over this is "slow"
constexpr double kMinOverMs = 2.0, kMaxOverMs = 1000.0;
constexpr uint64_t kWindowMs = 1000;      // one slow-pair line a second at most (the worst pair of it)
constexpr uint64_t kSummaryMs = 10000;    // and the summary this often
constexpr int64_t kHitWindowNs = 300 * 1000000LL;     // a landed hit this long ago: the pair is a "hit" pair
constexpr int64_t kSwingWindowNs = 250 * 1000000LL;   // a sweep of the blade this long ago, or ...
constexpr int64_t kPressWindowNs = 450 * 1000000LL;   // ... a press of the game's attack (its animation runs ~360 ms): a "swing" pair

// ---- what is measured --------------------------------------------------------------------------------------------------------

// The costs, each the plugin's OWN part (the game's function a hook wraps is left out), added up over all threads. The two timers of
// the update thread are told by their longest.
enum Section : int {
    kSkeleton = 0,   // vr/skeletons.cpp Hook: Arms + the bow rig, after the game's own pose - once per skeleton the game poses
    kHitHook,        // vr/haptics.cpp HookHitFlags, after the game's roll
    kAimHooks,       // vr/vrcam.cpp HookAttackTarget / HookAttackPoint / HookUnitFacing: ours around the game's call
    kPadHook,        // vr/vrcam.cpp XDetour: the game's pad poll on the draw thread - BodyWalk's pad, the plugin's actions, the input log, ours around the real XInput
    kXInputPad0,     // vr/vrcam.cpp XDetour: the real XInputGetState on pad 0 - the Xbox driver's answer (a pad, a virtual pad, or ERROR_DEVICE_NOT_CONNECTED)
    kXInputOther,    // the same on pads 1-3, which the game polls too (empty slots: documented as the slow case)
    kXInputSet,      // vr/vrcam.cpp XSetDetour: the game's own XInputSetState - its rumble (RumbleManager: OnHit, SkillUse...) through the driver
    kUnitLookup,     // vr/melee_touch.cpp HookLookup: the server calls the lookup for every unit it asks for
    kProbe,          // vr/melee_touch.cpp Probe: the job on the game's thread, every 100 ms (hostile? a wall between?) per monster near
    kDirect,         // vr/melee_direct.cpp: the Direct hit's job on the game's thread (an option, off by default)
    kHaptic,         // vr/xr.cpp: xrApplyHapticFeedback, the runtime's call
    kLog,            // the host's LogInfo (a line of the plugin's log); the runtime writes the file
    kMeleeTick,      // vr/vrcam.cpp MeleeTouchTick: the update thread's share for hit by touch (a 1 ms timer)
    kUpdateTick,     // one round of the update thread (1 ms timer): a long one is a hand that lags
    kSections
};
constexpr int kSummed = kMeleeTick;   // the sections before this are told by their sum, a pair at a time; the last two by their longest
inline const char* Name(int s) {
    static const char* const n[kSections] = {"skeleton hook", "hit hook", "aim hooks", "pad poll hook", "xinput poll of pad 0 (driver)", "xinput poll of pads 1-3 (driver)", "xinput rumble call (driver)", "unit lookup hook", "probe job", "direct hit job",
                                              "haptic call", "log", "melee tick", "update tick"};
    return s >= 0 && s < kSections ? n[s] : "?";
}

// What happens in a swing, for the situation of a pair (when the last one happened) and the counts in a pair.
enum Event : int { kSweep = 0, kPress, kHit, kEvents };

enum Ctx : int { kIdle = 0, kWalk, kSwing, kHitCtx, kCtxs };
inline const char* CtxName(int c) {
    static const char* const n[kCtxs] = {"idle", "walking", "swinging", "hit landed"};
    return c >= 0 && c < kCtxs ? n[c] : "?";
}

inline int64_t NowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// What the meter gives for one pair (microseconds), taken and cleared.
struct Sections {
    double us[kSections] = {};        // summed over all threads
    double drawUs[kSections] = {};    // of it, on the draw thread
    double maxUs[kSections] = {};     // the longest single probe
    uint32_t n[kSections] = {};
    uint32_t ev[kEvents] = {};        // sweeps, presses, hits in the pair
};

// The meter: lock-free adds from any thread, one reader (the draw thread, at the pair's end).
class Meter {
    struct Cell {
        std::atomic<int64_t> ns{0}, drawNs{0}, maxNs{0};
        std::atomic<uint32_t> n{0};
    };
    Cell cell_[kSections];
    std::atomic<int64_t> last_[kEvents];
    std::atomic<uint32_t> count_[kEvents];

public:
    Meter() {
        for (auto& l : last_) l.store(0);
        for (auto& c : count_) c.store(0);
    }
    void Add(Section s, int64_t ns, bool onDrawThread) {
        if (s < 0 || s >= kSections || ns < 0) return;
        Cell& c = cell_[s];
        c.ns.fetch_add(ns, std::memory_order_relaxed);
        if (onDrawThread) c.drawNs.fetch_add(ns, std::memory_order_relaxed);
        c.n.fetch_add(1, std::memory_order_relaxed);
        int64_t m = c.maxNs.load(std::memory_order_relaxed);
        while (ns > m && !c.maxNs.compare_exchange_weak(m, ns, std::memory_order_relaxed)) {}
    }
    void Note(Event e, int64_t nowNs) {
        if (e < 0 || e >= kEvents) return;
        last_[e].store(nowNs, std::memory_order_relaxed);
        count_[e].fetch_add(1, std::memory_order_relaxed);
    }
    int64_t Last(Event e) const { return e >= 0 && e < kEvents ? last_[e].load(std::memory_order_relaxed) : 0; }
    // What was added since the last Take; the cells and the counts start again (when the events last happened stays).
    void Take(Sections* out) {
        for (int i = 0; i < kSections; ++i) {
            out->us[i] = (double)cell_[i].ns.exchange(0, std::memory_order_relaxed) * 1e-3;
            out->drawUs[i] = (double)cell_[i].drawNs.exchange(0, std::memory_order_relaxed) * 1e-3;
            out->maxUs[i] = (double)cell_[i].maxNs.exchange(0, std::memory_order_relaxed) * 1e-3;
            out->n[i] = cell_[i].n.exchange(0, std::memory_order_relaxed);
        }
        for (int e = 0; e < kEvents; ++e) out->ev[e] = count_[e].exchange(0, std::memory_order_relaxed);
    }
};

inline Meter& Global() {
    static Meter m;
    return m;
}
inline std::atomic<bool>& OnFlag() {
    static std::atomic<bool> on{false};
    return on;
}
inline bool On() { return OnFlag().load(std::memory_order_relaxed); }
inline void SetOn(bool on) { OnFlag().store(on); }
inline bool& DrawThreadFlag() {
    static thread_local bool draw = false;
    return draw;
}
inline void MarkDrawThread() { DrawThreadFlag() = true; }   // the draw thread, at the start of every pair
inline void Note(Event e) {
    if (On()) Global().Note(e, NowNs());
}

// One probe: the time from here to the end of the scope goes to a section as ONE call (n counts scopes), less what Pause..Resume
// leaves out - the game's own function a hook wraps. Off: nothing is read.
class Scope {
    Section s_;
    int64_t t0_;        // the running stretch began here; 0 = none running (off, or paused)
    int64_t acc_ = 0;   // the stretches before it, ns
    bool live_;         // the probe was on when the scope began

public:
    explicit Scope(Section s) : s_(s), t0_(On() ? NowNs() : 0), live_(t0_ != 0) {}
    Scope(Section s, bool active) : s_(s), t0_(active && On() ? NowNs() : 0), live_(t0_ != 0) {}   // (active false: a call inside another's, not counted twice)
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    ~Scope() {
        if (!live_) return;
        if (t0_) acc_ += NowNs() - t0_;
        Global().Add(s_, acc_, DrawThreadFlag());
    }
    void Pause() {   // around the game's own call: its time is not ours
        if (t0_) { acc_ += NowNs() - t0_; t0_ = 0; }
    }
    void Resume() {
        if (live_ && !t0_) t0_ = NowNs();
    }
};

// ---- the situation of a pair ---------------------------------------------------------------------------------------------------

struct LastTimes { int64_t sweep = 0, press = 0, hit = 0; };   // steady-clock ns, 0 = never

// A landed hit within kHitWindowNs, else a sweep or a press within theirs, else the hero moving, else idle.
inline Ctx Classify(const LastTimes& t, int64_t nowNs, bool moving) {
    if (t.hit && nowNs - t.hit >= 0 && nowNs - t.hit <= kHitWindowNs) return kHitCtx;
    if ((t.sweep && nowNs - t.sweep >= 0 && nowNs - t.sweep <= kSwingWindowNs) || (t.press && nowNs - t.press >= 0 && nowNs - t.press <= kPressWindowNs))
        return kSwing;
    return moving ? kWalk : kIdle;
}

// ---- one finished pair, the line for a slow one and the summary ------------------------------------------------------------------

struct PairInfo {              // microseconds, as vr/frame_hitch.h Pair
    double cycle = 0;          // from the end of the last pair to the end of this one
    double gap = 0;            // the game's own frame before the pair
    double begin = 0;          // XR begin
    double wait = 0;           //   of it, xrWaitFrame
    double left = 0, right = 0;   // the passes (by turns: one pass, `left`)
    double end = 0;            // XR end
    double cpu = -1;           // the draw thread's CPU time over the passes; < 0 = not measured
    bool xr = false, turns = false, moving = false;
};

struct Sample {
    PairInfo p;
    Sections s;
    Ctx ctx = kIdle;
    uint32_t caughtAv = 0;     // access violations caught (SEH-guarded reads) since the last pair, all threads
};

namespace detail {
class Writer {
    char* b_;
    size_t cap_, n_ = 0;

public:
    Writer(char* b, size_t cap) : b_(b), cap_(cap) {
        if (cap_) b_[0] = 0;
    }
    void F(const char* fmt, ...) {
        if (n_ + 1 >= cap_) return;
        va_list a;
        va_start(a, fmt);
        const int w = vsnprintf(b_ + n_, cap_ - n_, fmt, a);
        va_end(a);
        if (w > 0) n_ = std::min(cap_ - 1, n_ + (size_t)w);
    }
    size_t size() const { return n_; }
};
inline double Ms(double us) { return std::isfinite(us) ? us * 0.001 : 0.0; }
}   // namespace detail

// "vrcam: frame trace - ..." for one slow pair. `moreOver` other slow pairs in the same second, the worst of them `moreWorstUs`.
inline int FormatSlow(char* out, size_t cap, const Sample& s, double overMs, unsigned moreOver, double moreWorstUs) {
    using detail::Ms;
    detail::Writer w(out, cap);
    const PairInfo& p = s.p;
    w.F("vrcam: frame trace - a %s took %.1f ms (over %.0f): game %.1f | ", p.turns ? "pass" : "pair", Ms(p.cycle), overMs, Ms(p.gap));
    if (p.xr) w.F("XR begin %.1f (xrWaitFrame %.1f) | ", Ms(p.begin), Ms(p.wait));
    if (p.turns) w.F("pass %.1f", Ms(p.left));
    else w.F("left %.1f | right %.1f", Ms(p.left), Ms(p.right));
    if (p.xr) w.F(" | XR end %.1f", Ms(p.end));
    if (p.cpu >= 0.0) w.F(" | the draw thread worked %.1f ms of the %.1f it was in the pair (the rest it waited: GPU, fences, locks)", Ms(p.cpu), Ms(p.begin + p.left + p.right + p.end));
    w.F(" | %s%s", CtxName(s.ctx), p.moving && s.ctx != kWalk ? " while moving" : "");
    w.F(" (this pair: %u sweeps, %u presses, %u hits)", s.s.ev[kSweep], s.s.ev[kPress], s.s.ev[kHit]);
    bool any = false;
    for (int i = 0; i < kSummed; ++i) {
        if (!s.s.n[i]) continue;
        w.F("%s %s %.2f ms (n=%u, longest %.2f", any ? "," : " | measured:", Name(i), Ms(s.s.us[i]), s.s.n[i], Ms(s.s.maxUs[i]));
        if (s.s.drawUs[i] > 0.0) w.F(", on the draw thread %.2f", Ms(s.s.drawUs[i]));
        w.F(")");
        any = true;
    }
    if (!any) w.F(" | measured: nothing in this pair");
    if (s.s.n[kUpdateTick])
        w.F(" | update thread: longest round %.2f ms (hit by touch %.2f)", Ms(s.s.maxUs[kUpdateTick]), Ms(s.s.maxUs[kMeleeTick]));
    if (s.caughtAv) w.F(" | %u access violation(s) caught (each costs the game thread's time to unwind)", s.caughtAv);
    if (moreOver) w.F(" | +%u more over %.0f ms in this second, the worst %.1f ms", moreOver, overMs, Ms(moreWorstUs));
    return (int)w.size();
}

// The summary of a period: how the pairs divided over the situations, and our own cost a pair on average.
struct CtxStats {
    unsigned pairs = 0, over = 0;
    double sumUs = 0, maxUs = 0;
};
struct Summary {
    CtxStats ctx[kCtxs];
    double sumSections[kSections] = {};     // microseconds, all pairs
    double maxSections[kSections] = {};
    uint32_t nSections[kSections] = {};
    unsigned pairs = 0, over = 0, caughtAv = 0;
    double sumUs = 0, maxUs = 0;
};

inline int FormatSummary(char* out, size_t cap, const Summary& su, double overMs, double periodS) {
    using detail::Ms;
    detail::Writer w(out, cap);
    const double mean = su.pairs ? Ms(su.sumUs) / su.pairs : 0.0;
    w.F("vrcam: frame trace, last %.0f s: %u pairs, mean %.1f ms, %u over %.0f ms (%.1f%%), worst %.1f", periodS, su.pairs, mean, su.over, overMs,
        su.pairs ? 100.0 * su.over / su.pairs : 0.0, Ms(su.maxUs));
    for (int c = 0; c < kCtxs; ++c) {
        const CtxStats& k = su.ctx[c];
        w.F(" | %s %u pairs", CtxName(c), k.pairs);
        if (k.pairs) w.F(" (%u over = %.1f%%, mean %.1f, worst %.1f)", k.over, 100.0 * k.over / k.pairs, Ms(k.sumUs) / k.pairs, Ms(k.maxUs));
    }
    bool any = false;
    for (int i = 0; i < kSummed && su.pairs; ++i) {
        if (!su.nSections[i]) continue;
        w.F("%s %s %.3f (%u calls)", any ? "," : " | measured, ms a pair (all threads):", Name(i), Ms(su.sumSections[i]) / su.pairs, su.nSections[i]);
        any = true;
    }
    if (su.nSections[kUpdateTick])
        w.F(" | update thread: longest round %.2f ms (hit by touch %.2f)", Ms(su.maxSections[kUpdateTick]), Ms(su.maxSections[kMeleeTick]));
    if (su.caughtAv) w.F(" | %u access violation(s) caught", su.caughtAv);
    return (int)w.size();
}

// The recorder: one call a finished pair. It keeps the worst slow pair of a second and the summary of ten seconds, and hands back
// the lines that are due - at most one slow-pair line a second and one summary every ten.
class Recorder {
public:
    struct Lines {
        char text[2][1536];
        int n = 0;
    };

    explicit Recorder(double overMs = kDefaultOverMs) { SetOverMs(overMs); }
    void SetOverMs(double ms) { overUs_ = 1000.0 * (std::isfinite(ms) ? std::clamp(ms, kMinOverMs, kMaxOverMs) : kDefaultOverMs); }
    double OverMs() const { return overUs_ * 0.001; }

    void Pair(uint64_t nowMs, const Sample& s, Lines* out) {
        out->n = 0;
        if (!started_) {
            started_ = true;
            windowAt_ = summaryAt_ = nowMs;
        }
        const double cyc = std::isfinite(s.p.cycle) && s.p.cycle > 0.0 ? s.p.cycle : 0.0;
        // the ten seconds
        Fold(sum_, s, cyc);
        // the second
        if (cyc > overUs_) {
            if (!haveWorst_ || cyc > worst_.p.cycle) {
                if (haveWorst_) { ++more_; moreWorst_ = std::max(moreWorst_, worst_.p.cycle); }   // the one it replaces is "another"
                worst_ = s;
                haveWorst_ = true;
            } else {
                ++more_;
                moreWorst_ = std::max(moreWorst_, cyc);
            }
        }
        if (nowMs - windowAt_ >= kWindowMs) {
            if (haveWorst_ && out->n < 2) {
                FormatSlow(out->text[out->n], sizeof out->text[out->n], worst_, overUs_ * 0.001, more_, moreWorst_);
                ++out->n;
            }
            haveWorst_ = false;
            more_ = 0;
            moreWorst_ = 0.0;
            windowAt_ = nowMs;
        }
        if (nowMs - summaryAt_ >= kSummaryMs) {
            if (sum_.pairs && out->n < 2) {
                FormatSummary(out->text[out->n], sizeof out->text[out->n], sum_, overUs_ * 0.001, (double)(nowMs - summaryAt_) * 0.001);
                ++out->n;
            }
            sum_ = Summary();
            summaryAt_ = nowMs;
        }
    }

private:
    void Fold(Summary& su, const Sample& s, double cyc) const {
        ++su.pairs;
        su.sumUs += cyc;
        su.maxUs = std::max(su.maxUs, cyc);
        const bool over = cyc > overUs_;
        su.over += over ? 1u : 0u;
        CtxStats& k = su.ctx[s.ctx >= 0 && s.ctx < kCtxs ? s.ctx : kIdle];
        ++k.pairs;
        k.over += over ? 1u : 0u;
        k.sumUs += cyc;
        k.maxUs = std::max(k.maxUs, cyc);
        for (int i = 0; i < kSections; ++i) {
            su.sumSections[i] += s.s.us[i];
            su.maxSections[i] = std::max(su.maxSections[i], s.s.maxUs[i]);
            su.nSections[i] += s.s.n[i];
        }
        su.caughtAv += s.caughtAv;
    }

    double overUs_ = kDefaultOverMs * 1000.0;
    bool started_ = false, haveWorst_ = false;
    uint64_t windowAt_ = 0, summaryAt_ = 0;
    Sample worst_;
    unsigned more_ = 0;
    double moreWorst_ = 0.0;
    Summary sum_;
};

}   // namespace frametrace
