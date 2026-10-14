#pragma once
// hit_latency.h - how long the game takes from OUR press of its attack to ITS hit. Observation only.
//
// Hit by touch (vr/melee_touch.cpp) and the shield's Smite (vr/shield_smite.cpp) ask the game to attack by pressing its own
// button. For an A/B comparison with a build that applies the hit directly we need numbers from this side: the time between
// the moment we ask (Press) and the moment the game's hit-flags function answers "the blow landed" for a player's attack
// (Landed, called by the hit observer in vr/haptics.cpp - the one hook there is on that function, shared, read-only).
//
// Nothing here calls the game, changes a key's meaning, or alters what happens in play. The press and the hit are paired
// by time alone: a hit goes to the oldest press that has not had one yet and is not older than kWindowS; a press that
// gets none in kWindowS is "lost" (the game refused it - its animation gate, out of reach, a miss - or it landed nothing).
// One press can be answered by several hits (Zeal): only the first counts.
//
// Press(), Landed() and Collect() are lock-free; Landed() runs on the game's server thread and does nothing else.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace hitlatency {

enum Source : int { kMelee = 0, kShield = 1, kMeleeDirect = 2, kSources = 3 };   // (kMeleeDirect: the direct hit's call -> hit, vr/melee_direct.cpp)

constexpr double kWindowS = 1.2;          // a press with no hit within this is lost
constexpr int kSlots = 16;

// Nanoseconds on the steady clock (the MSVC library reads QueryPerformanceCounter for it).
inline int64_t NowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

class Tracker {
    struct Slot {
        std::atomic<int64_t> press{0};    // (ns << 2) | source; 0 = the slot is free
        std::atomic<int64_t> hit{0};      // ns of the hit that answered it; 0 = none yet
    };
    Slot slot_[kSlots];
    int64_t windowNs_;

public:
    explicit Tracker(double windowSeconds = kWindowS) : windowNs_((int64_t)(windowSeconds * 1e9)) {}

    // A press was asked of the game. false = no free slot (more than kSlots presses unanswered inside the window).
    bool Press(int source, int64_t ns) {
        if (ns <= 0 || source < 0 || source >= kSources) return false;
        const int64_t v = (ns << 2) | source;
        for (Slot& s : slot_) {
            int64_t zero = 0;
            if (s.press.compare_exchange_strong(zero, v, std::memory_order_acq_rel)) {
                s.hit.store(0, std::memory_order_release);
                return true;
            }
        }
        return false;
    }

    // A player's blow landed (the observer; any thread). The oldest unanswered press inside the window gets it.
    void Landed(int64_t ns) {
        for (int tries = 0; tries < 4; ++tries) {
            int best = -1;
            int64_t bestAt = 0;
            for (int i = 0; i < kSlots; ++i) {
                const int64_t v = slot_[i].press.load(std::memory_order_acquire);
                if (!v || slot_[i].hit.load(std::memory_order_acquire)) continue;
                const int64_t at = v >> 2;
                if (ns < at || ns - at > windowNs_) continue;
                if (best < 0 || at < bestAt) { best = i; bestAt = at; }
            }
            if (best < 0) return;
            int64_t zero = 0;
            if (slot_[best].hit.compare_exchange_strong(zero, ns, std::memory_order_acq_rel)) return;
        }
    }

    struct Outcome {
        bool hit = false;      // false = lost
        double ms = 0;         // press -> hit
    };

    // Takes the answered and the expired presses of one source (the caller's own thread, now and then).
    int Collect(int source, int64_t nowNs, Outcome* out, int max) {
        int n = 0;
        for (Slot& s : slot_) {
            if (n >= max) break;
            const int64_t v = s.press.load(std::memory_order_acquire);
            if (!v || (v & 3) != source) continue;
            const int64_t at = v >> 2, h = s.hit.load(std::memory_order_acquire);
            if (h) {
                out[n++] = Outcome{true, (double)(h - at) * 1e-6};
            } else if (nowNs - at > windowNs_) {
                out[n++] = Outcome{false, 0.0};
            } else {
                continue;
            }
            s.hit.store(0, std::memory_order_release);
            s.press.store(0, std::memory_order_release);
        }
        return n;
    }
};

// What each press-making module counts, and the 10-second line it writes. One thread only (the module's own).
class Stats {
    std::vector<double> ms_;
    unsigned presses_ = 0, hits_ = 0, lost_ = 0;

public:
    void Press() { ++presses_; }
    void Add(const Tracker::Outcome& o) {
        if (o.hit) { ++hits_; ms_.push_back(o.ms); }
        else ++lost_;
    }
    bool Any() const { return presses_ || hits_ || lost_; }

    // "<prefix> press->hit ms n=12 min=.. median=.. max=.. | presses=15 hits=12 lost=3 (no hit within 1.2 s)"; starts a new period.
    int Line(char* buf, size_t cap, const char* prefix) {
        std::sort(ms_.begin(), ms_.end());
        const size_t n = ms_.size();
        int w;
        if (n) {
            const double med = n % 2 ? ms_[n / 2] : 0.5 * (ms_[n / 2 - 1] + ms_[n / 2]);
            w = snprintf(buf, cap, "%s press->hit ms n=%zu min=%.0f median=%.0f max=%.0f | presses=%u hits=%u lost=%u (no hit within %.1f s)", prefix, n,
                         ms_.front(), med, ms_.back(), presses_, hits_, lost_, kWindowS);
        } else {
            w = snprintf(buf, cap, "%s press->hit ms n=0 min=- median=- max=- | presses=%u hits=%u lost=%u (no hit within %.1f s)", prefix, presses_, hits_,
                         lost_, kWindowS);
        }
        ms_.clear();
        presses_ = hits_ = lost_ = 0;
        return w;
    }
};

// ---- the one tracker of the plugin, and who wants the observer on --------------------------------------------------------

inline Tracker& Global() {
    static Tracker t;
    return t;
}
inline std::atomic<unsigned>& WantMask() {
    static std::atomic<unsigned> m{0};
    return m;
}
inline void Want(int source, bool on) {
    if (on) WantMask().fetch_or(1u << source);
    else WantMask().fetch_and(~(1u << source));
}
inline bool Wanted() { return WantMask().load(std::memory_order_relaxed) != 0; }

inline void Press(int source) { Global().Press(source, NowNs()); }
inline void Landed() { Global().Landed(NowNs()); }
inline int Collect(int source, Tracker::Outcome* out, int max) { return Global().Collect(source, NowNs(), out, max); }

}   // namespace hitlatency
