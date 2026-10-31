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

// The controller a landed blow buzzes (what the hit observer asks, vr/haptics.cpp), -1 = the event's own (the right hand). Skeleton sides in (0 right, 1 left), haptics hands out.
//   claimed:  the side that ASKED for this blow (vr/hit_claim.h): the controller whose blade touched the monster (either hand), the arm that wears the shield for a bash. A claim is
//             the hand of the blow whatever the weight model holds - a Barbarian's left blade, a weapon drawn into the left hand, a shield on either arm.
//   else:     no press of ours made it (the game's own attack button, a thrown weapon in the air): the hand that holds the weapon (`carrySide`) while the weight model has one placed,
//             else -1 - the right, where the throw gesture's weapon is thrown from too.
inline int HitHand(int claimedSide, int carrySide, bool weaponPlaced) {
    if (claimedSide == 0 || claimedSide == 1) return FromSkel(claimedSide);
    return weaponPlaced && (carrySide == 0 || carrySide == 1) ? FromSkel(carrySide) : -1;
}

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
    float amplitude;
    uint32_t durationMs;
    constexpr Request(float a = 0.0f, uint32_t ms = 0) : amplitude(a), durationMs(ms) {}
};

// The motors. A Quest's Touch controller feels every pulse above kMinAmplitude. The PlayStation VR2 Sense controllers under SteamVR (the runtime names the headset
// "playstation_vr2", the controllers run the Touch profile) took the hit (0.72 for 45 ms) but not the low-life heartbeat (0.14 - 0.47 for 28 - 40 ms): the gentle,
// short pulses are below what that motor shows. A pulse meant to be gentle is lifted for it to kWeakFloorAmp .. 1 (the order kept: the strongest stays the strongest, a
// quiet one is still the quieter) and lasts at least kWeakFloorMs - the hit's own. Plain is untouched, bit for bit.
enum class Motor : int { Plain = 0, Weak = 1 };
constexpr float kWeakFloorAmp = 0.55f;
constexpr uint32_t kWeakFloorMs = 45;
inline Motor MotorOfHeadset(const char* systemName) {   // "SteamVR/OpenXR : playstation_vr2": any case, anywhere in the name
    if (!systemName) return Motor::Plain;
    static const char key[] = "playstation";
    for (const char* p = systemName; *p; ++p) {
        size_t i = 0;
        while (key[i] && p[i] && (p[i] | 0x20) == key[i]) ++i;
        if (!key[i]) return Motor::Weak;
    }
    return Motor::Plain;
}
// The ini's [openxr] haptics_motor: 0 by the headset's name (the default), 1 plain, 2 weak.
inline Motor MotorChosen(int choice, Motor ofHeadset) { return choice == 1 ? Motor::Plain : choice == 2 ? Motor::Weak : ofHeadset; }
inline Request ForMotor(Motor motor, float amplitude, uint32_t durationMs) {
    Request r;
    if (!(amplitude >= kMinAmplitude)) return r;   // nothing to play stays nothing (NaN too)
    const float a = amplitude > 1.0f ? 1.0f : amplitude;
    if (motor != Motor::Weak) {
        r.amplitude = a;
        r.durationMs = durationMs;
        return r;
    }
    r.amplitude = kWeakFloorAmp + (1.0f - kWeakFloorAmp) * (a - kMinAmplitude) / (1.0f - kMinAmplitude);
    r.durationMs = durationMs < kWeakFloorMs ? kWeakFloorMs : durationMs;
    return r;
}

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
    bool Take(uint32_t nowMs, uint32_t maxAgeMs, Request* out, bool* stale = nullptr) {   // stale: set when a pulse was there but too old to play (a ghost)
        const uint64_t v = v_.exchange(0, std::memory_order_acq_rel);
        if (stale) *stale = v && Stale(Stamp(v), nowMs, maxAgeMs);
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

// ---- the extra pulses (the Vibration tab; each its own key, off = nothing of this runs) --------------------------------------------------------------
// The numbers below are bearstonem/d2r-pcvr's (MIT, before its MPL change of 2026-10-28): steamvr/haptic_strength.h Samples (the weapon's 0.95 for
// 90 ms, the spell's 0.6, the alert's 0.25 for 100 ms) and steamvr/interactions.h's low mana alert (under 10 %, again every 10 s). The code is ours.

// [openxr] haptics_hit_long: a blow's tap. Off: 0.72 for 45 ms, as ever. On: d2r-pcvr's longer weapon tap, 0.95 for 90 ms - easier to feel in a fast swing.
inline Request HitTap(bool longTap) {
    Request r;
    r.amplitude = longTap ? 0.95f : 0.72f;
    r.durationMs = longTap ? 90u : 45u;
    return r;
}

// [openxr] haptics_touch: hit by touch's counted contact buzzes the blade's hand at once - whether the game then lands the blow, misses it or refuses
// it (d2r-pcvr's melee.cpp Feel: one tap a stroke and monster). The landed hit that follows the same hand's touch within kTouchCoverMs is that touch's
// own blow: it gives no second pulse (Covers). Any thread; times are GetTickCount64 milliseconds.
constexpr uint64_t kTouchCoverMs = 400;   // hit_claim.h's window: a press -> the game's hit is 24 - 53 ms, 253 at the most
class TouchCover {
public:
    void Touched(int hand, uint64_t nowMs) {
        if (hand == kLeft || hand == kRight) at_[hand].store(nowMs ? nowMs : 1, std::memory_order_relaxed);
    }
    bool Covers(int hand, uint64_t nowMs) const {
        if (hand != kLeft && hand != kRight) return false;
        const uint64_t t = at_[hand].load(std::memory_order_relaxed);
        return t && nowMs >= t && nowMs - t <= kTouchCoverMs;
    }

private:
    std::atomic<uint64_t> at_[2] = {};
};

// [openxr] haptics_spell: the hero's own casting animation starting (Diablo II's unit mode 10, "SC": spells, warcries, the other cast skills) is a cast
// the game accepted - one it refused (no mana, a skill not ready) never animates. True on the step into the mode; -1 (the unit not known) forgets it.
// A held cast whose animations follow one another with no frame out of the mode in between is one pulse.
constexpr int kCastMode = 10;
constexpr Request kSpellTap = {0.60f, 50u};
class CastEdge {
public:
    bool Step(int mode) {
        const bool now = mode == kCastMode;
        const bool edge = now && !was_;
        was_ = now;
        return edge;
    }

private:
    bool was_ = false;
};

// [openxr] haptics_low_mana: a pulse in the left hand when the mana falls under the threshold, again every kManaAgainMs while it stays under; a little
// above it (kManaRearm) it is ready again. Unknown numbers do nothing (no hero, a load). Single thread.
constexpr uint64_t kManaAgainMs = 10000;
constexpr float kManaRearm = 0.03f;
constexpr Request kManaTap = {0.25f, 100u};
class ManaAlert {
public:
    bool Step(bool known, float frac, float threshold, uint64_t nowMs) {
        if (!known || !std::isfinite(frac) || !(threshold > 0.0f)) return false;
        if (frac >= threshold + kManaRearm) { low_ = false; return false; }
        if (frac >= threshold) return false;   // (between the two: as it was)
        if (low_ && nowMs - at_ < kManaAgainMs) return false;
        low_ = true;
        at_ = nowMs;
        return true;
    }

private:
    bool low_ = false;
    uint64_t at_ = 0;
};

// [openxr] haptics_game_rumble: the game's own pad rumble (its RumbleManager calls XInputSetState: a hit taken, a skill used ...) followed in the controllers.
// A rumble is a level the game sets and later sets to 0; here it becomes pulses of kRumblePulseMs, one every kRumbleEveryMs while it stays up, both
// hands, the stronger of the pad's two motors (0 - 65535) as the amplitude. A level held past kRumbleMaxMs is let go (a stuck rumble does not buzz for
// good) until the game sets 0 or another level. Set from the game's thread, Step from the frame loop: lock-free.
constexpr uint32_t kRumblePulseMs = 100, kRumbleEveryMs = 90, kRumbleMaxMs = 1500;
class RumbleFollow {
public:
    void Set(uint16_t low, uint16_t high, uint64_t nowMs) {
        const uint32_t level = low > high ? low : high;
        const uint64_t old = v_.load(std::memory_order_relaxed);
        if ((uint32_t)(old & 0xFFFFu) == level && level) return;   // the same level again: it goes on
        v_.store((uint64_t)level | (nowMs << 16), std::memory_order_relaxed);
    }
    // True: play `out` on both hands now.
    bool Step(uint64_t nowMs, Request* out) {
        const uint64_t v = v_.load(std::memory_order_relaxed);
        const uint32_t level = (uint32_t)(v & 0xFFFFu);
        const uint64_t since = v >> 16;
        if (v != seen_) { seen_ = v; next_ = 0; }   // a new level: at once
        if (!level || nowMs < since || nowMs - since > kRumbleMaxMs || nowMs < next_) return false;
        const float a = (float)level / 65535.0f;
        if (!(a >= kMinAmplitude)) return false;
        next_ = nowMs + kRumbleEveryMs;
        out->amplitude = a;
        out->durationMs = kRumblePulseMs;
        return true;
    }

private:
    std::atomic<uint64_t> v_{0};   // level | set at (ms) << 16
    uint64_t seen_ = 0, next_ = 0;  // (the frame loop's)
};

// [openxr] haptics_pad_mute: while the plugin's controllers are the game's (VR, F4) the game's rumble does not reach a pad that is plugged in - only
// the controllers vibrate. A pad left rumbling when that starts is stopped once. Otherwise every call goes through as it came. (d2r-pcvr's
// steamvr/xinput_rumble.h GamepadRumble, MIT, rewritten.) Single thread per caller in practice; the game calls from one.
class PadGate {
public:
    enum class Act { Pass, Swallow, StopThenSwallow };
    Act Step(bool mute) {
        if (!mute) return Act::Pass;
        if (active_) { active_ = false; return Act::StopThenSwallow; }
        return Act::Swallow;
    }
    void Passed(bool ok, bool nonzero) { active_ = ok && nonzero; }   // after a Pass: what the pad does now

private:
    bool active_ = false;
};

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
