#pragma once
// Controller haptics, the part that needs neither a headset nor the game nor OpenXR - so it is
// tested (cleanroom/tests/haptics_test.cpp). vr/haptics.cpp is the glue, vr/xr.cpp the output.
//
// An event (a hit landed, a bottle drunk, a menu clicked...) becomes a pulse: an amplitude 0..1
// and a duration in ms for one hand. Pulses come from any thread (the game's server thread sees a
// hit, the draw thread a click), so they are posted into a lock-free mailbox per hand and played
// by the frame loop, which runs them through a rate limiter: a flood of events, or a weak one
// right behind a strong one, must not turn into a buzz or cut the strong one short.
//
// The amplitudes and durations (0.12 - 0.72, 20 - 100 ms) and the bow's ratchet (a tick for every
// 0.018 of the draw, at least 70 ms apart) are the ones bearstonem/d2r-pcvr (MIT,
// steamvr/transport.cpp MotionFeedback) found pleasant on a Steam Frame / Index; the code is ours.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace haptics {

// The order of XrInput (shared/d2r_vr_shared.h) and vr/xr.cpp: 0 left, 1 right. NOT skel::Input's
// hand slots (0 right, 1 left) - FromSkel turns those.
enum Hand : int { kLeft = 0, kRight = 1 };
inline int FromSkel(int side) { return side ? kLeft : kRight; }
inline int Other(int hand) { return hand ? kLeft : kRight; }

constexpr float kMinAmplitude = 0.04f;   // weaker is not felt (and not worth a call)
constexpr uint32_t kMinDurationMs = 10;
constexpr uint32_t kMaxDurationMs = 300;

// The amplitude times the user's strength ([openxr] haptics_scale), kept in 0..1; NaN and
// anything below kMinAmplitude is 0 = nothing to play.
inline float Scaled(float amplitude, float scale) {
    const float a = amplitude * scale;
    if (!std::isfinite(a) || a < kMinAmplitude) return 0.0f;
    return a > 1.0f ? 1.0f : a;
}
inline uint32_t ClampDuration(int ms) {
    return ms < (int)kMinDurationMs ? kMinDurationMs : ms > (int)kMaxDurationMs ? kMaxDurationMs : (uint32_t)ms;
}

struct Request {
    float amplitude = 0.0f;
    uint32_t durationMs = 0;
};

// One hand's pending pulse. Post from any thread, lock-free; Take from the frame loop. Pulses posted
// between two Takes are one: the strongest amplitude, the longest duration. A pulse nobody took in
// maxAgeMs (the session had no focus, no frame came) is dropped - it would be a ghost by then.
class Mailbox {
public:
    void Post(float amplitude, uint32_t durationMs, uint32_t nowMs) {
        if (!(amplitude >= kMinAmplitude)) return;   // (NaN too)
        const uint64_t a = Pack(std::min(amplitude, 1.0f), ClampDuration((int)durationMs), nowMs);
        uint64_t old = v_.load(std::memory_order_relaxed);
        for (;;) {
            uint64_t now = a;
            if (old && !Stale(Stamp(old), nowMs, kKeepMs)) {   // merged with what waits
                now = Pack(std::max(Amp(old), Amp(a)), std::max(Dur(old), Dur(a)), Stamp(old));
            }
            if (v_.compare_exchange_weak(old, now, std::memory_order_acq_rel, std::memory_order_relaxed)) return;
        }
    }
    bool Take(uint32_t nowMs, uint32_t maxAgeMs, Request* out) {
        const uint64_t v = v_.exchange(0, std::memory_order_acq_rel);
        if (!v || Stale(Stamp(v), nowMs, maxAgeMs)) return false;
        out->amplitude = Amp(v);
        out->durationMs = Dur(v);
        return true;
    }
    void Clear() { v_.store(0, std::memory_order_relaxed); }

private:
    static constexpr uint32_t kKeepMs = 250;   // a waiting pulse older than this is not merged into: the new one replaces it
    static uint64_t Pack(float amp, uint32_t dur, uint32_t stamp) {
        const uint32_t a = (uint32_t)(amp * 65535.0f + 0.5f);
        return (uint64_t)(a > 65535u ? 65535u : a) | (uint64_t)dur << 16 | (uint64_t)stamp << 32;
    }
    static float Amp(uint64_t v) { return (float)(v & 0xFFFFu) / 65535.0f; }
    static uint32_t Dur(uint64_t v) { return (uint32_t)((v >> 16) & 0xFFFFu); }
    static uint32_t Stamp(uint64_t v) { return (uint32_t)(v >> 32); }
    static bool Stale(uint32_t stamp, uint32_t now, uint32_t maxAge) { return (uint32_t)(now - stamp) > maxAge; }
    std::atomic<uint64_t> v_{0};
};

// What the frame loop plays: whether a pulse may start now on a hand. Single thread (the frame loop).
//  - two starts on one hand are at least kMinGapMs apart;
//  - while a pulse still plays (the runtime replaces it with the new one), only a clearly stronger
//    one starts - a ratchet's tick does not cut a hit short;
//  - at most kMaxInWindow starts in kWindowMs: a stuck event cannot buzz the hand for good.
class Limiter {
public:
    static constexpr uint32_t kMinGapMs = 30;
    static constexpr uint32_t kWindowMs = 1000;
    static constexpr int kMaxInWindow = 14;
    static constexpr float kStrongerBy = 0.10f;

    bool Allow(int hand, float amplitude, uint32_t durationMs, uint64_t nowMs) {
        if (hand < 0 || hand > 1 || !(amplitude >= kMinAmplitude)) return false;
        H& h = h_[hand];
        if (h.n && nowMs < h.last + kMinGapMs) return false;
        if (h.n && nowMs < h.endsAt && amplitude < h.amp + kStrongerBy) return false;
        int inWindow = 0;
        for (int i = 0; i < h.n && i < kMaxInWindow; ++i)
            if (nowMs - h.starts[i] < kWindowMs) ++inWindow;
        if (inWindow >= kMaxInWindow) return false;
        h.starts[h.head] = nowMs;
        h.head = (h.head + 1) % kMaxInWindow;
        if (h.n < kMaxInWindow) ++h.n;
        h.last = nowMs;
        h.endsAt = nowMs + durationMs;
        h.amp = amplitude;
        return true;
    }
    void Reset() { h_[0] = H{}; h_[1] = H{}; }

private:
    struct H {
        uint64_t starts[kMaxInWindow] = {};
        int head = 0, n = 0;
        uint64_t last = 0, endsAt = 0;
        float amp = 0.0f;
    };
    H h_[2];
};

// The events the plugin knows by name. A caller that has a gesture of its own (the bow draw, the
// shield, a melee swing) can say which of these it is, or post its own amplitude with haptics::Pulse.
enum class Event : int {
    Hit,             // a blow the hero landed (the game's own hit roll said yes) - the weapon hand
    ShieldHit,       // a bash or a smite with the shield landed - the left hand
    Drink,           // a belt potion drunk - the hand that tipped it
    BeltTake,        // a bottle taken off the belt into a hand
    MenuClick,       // the laser's left click on a menu
    MenuRightClick,  // and the right one (grip held)
    Interact,        // pick up / open / talk with a target there
    BowNock,         // an arrow laid on the string - the string hand; the bow hand lighter
    BowRelease,      // the string let go
    kCount
};
constexpr int kEventCount = (int)Event::kCount;

struct Part { bool other; float amplitude; uint32_t durationMs; };   // other: the hand opposite the one given
struct Pattern { const char* name; int hand; int n; Part part[2]; };   // hand: the default (the event's own)

inline const Pattern& PatternOf(Event e) {
    static const Pattern k[kEventCount] = {
        {"hit", kRight, 1, {{false, 0.72f, 45}, {}}},
        {"shield hit", kLeft, 1, {{false, 0.72f, 45}, {}}},
        {"drink", kRight, 1, {{false, 0.45f, 70}, {}}},
        {"belt take", kRight, 1, {{false, 0.25f, 30}, {}}},
        {"menu click", kRight, 1, {{false, 0.30f, 35}, {}}},
        {"menu right click", kRight, 1, {{false, 0.55f, 35}, {}}},
        {"interact", kRight, 1, {{false, 0.45f, 45}, {}}},
        {"bow nock", kRight, 2, {{false, 0.45f, 45}, {true, 0.25f, 35}}},
        {"bow release", kRight, 2, {{false, 0.65f, 45}, {true, 0.65f, 35}}},
    };
    return k[(int)e >= 0 && (int)e < kEventCount ? (int)e : 0];
}

struct Resolved { int hand; float amplitude; uint32_t durationMs; };
// The pulses an event makes: `hand` (-1 = the event's own) and, for the bow's, the other one too.
inline int Expand(Event e, int hand, Resolved out[2]) {
    const Pattern& p = PatternOf(e);
    const int h = hand == kLeft || hand == kRight ? hand : p.hand;
    for (int i = 0; i < p.n; ++i) out[i] = Resolved{p.part[i].other ? Other(h) : h, p.part[i].amplitude, p.part[i].durationMs};
    return p.n;
}

// The bow's string while it is drawn: a light tick for every 0.018 of the draw, never closer than
// 70 ms apart, the string hand a little stronger the farther it is drawn; nothing while it is held
// still, and after a nock or a release the count starts again from where it is. Single thread.
class Ratchet {
public:
    static constexpr float kStep = 0.018f;
    static constexpr uint32_t kGapMs = 70;
    // draw 0..1 (<= 0: no string in the hand). True: play stringAmp for 25 ms on the string hand and
    // bowAmp for 20 ms on the bow hand.
    bool Step(float draw, uint32_t nowMs, float* stringAmp, float* bowAmp) {
        if (!(draw > 0.0f)) { prev_ = 0.0f; return false; }
        if (draw > 1.0f) draw = 1.0f;
        if (have_ && (uint32_t)(nowMs - last_) < kGapMs) return false;
        if (std::fabs(draw - prev_) <= kStep) return false;
        *stringAmp = 0.22f + 0.28f * draw;
        *bowAmp = 0.10f + 0.15f * draw;
        prev_ = draw;
        last_ = nowMs;
        have_ = true;
        return true;
    }
    void Rearm(float draw, uint32_t nowMs) { prev_ = draw > 0.0f ? draw : 0.0f; last_ = nowMs; have_ = true; }

private:
    float prev_ = 0.0f;
    uint32_t last_ = 0;
    bool have_ = false;
};

}   // namespace haptics
