// [bow] hand_pose: how far the hand that takes a bow's string closes its fingers. Pure - no game, no skeleton; the pose itself
// (skeletons.cpp, Fist) takes the amount from here, and cleanroom/tests/bow_hand_test.cpp drives it.
//
// What closes the hand, strongest first:
//   - the draw: the string is taken (the draw detector's word) - the hand is hooked on it whatever the controls read. They read
//     as let go once the draw has TAKEN them from the layout (directinput::Take hides the control it took), so the layout's own
//     grip says nothing at the very moment the hand has to close;
//   - the controller's own trigger and squeeze, as the wire read them before the draw took them (raw) - the fingers follow the
//     squeeze, so a hand closing on the controller closes on the screen;
//   - with no raw reading (the draw switched off): the layout's grip, as the fist of an empty hand has it.
// The amount is then eased: fast on the way in (the fingers hook on the string), slower out (the release lets go).
#pragma once
#include <algorithm>
#include <cmath>

namespace bowhand {

constexpr float kHookDefault = 0.85f;   // [bow] hook_close: the string hand's fingers, as a share of a full fist's bend (was 0.70: too little to see)
constexpr float kThumbShare = 0.25f;    // the thumb stays loose: this share of the fingers' bend
constexpr float kCloseS = 0.08f;        // seconds from open to closed
constexpr float kOpenS = 0.18f;         // ... and back

enum class Source { Rest, Draw, Trigger, Grip, GameGrip };

inline const char* SourceName(Source s) {
    switch (s) {
    case Source::Draw: return "the draw (the string is taken)";
    case Source::Trigger: return "the controller's trigger";
    case Source::Grip: return "the controller's grip";
    case Source::GameGrip: return "the layout's grip";
    default: return "nothing squeezed";
    }
}

inline float Clean(float v) { return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f; }

struct Reading {
    bool drawing = false;     // the draw detector's word: the string is nocked and held
    bool haveRaw = false;     // the string hand's own trigger and squeeze, read before the draw took them (fresh, for this very hand)
    float trigger = 0.0f, grip = 0.0f;
    float gameGrip = -1.0f;   // the grip as the layout left it; negative = none sent
};

struct Squeeze {
    float k = 0.0f;           // 0 = relaxed, 1 = hooked
    Source source = Source::Rest;
};

inline Squeeze SqueezeOf(const Reading& r) {
    if (r.drawing) return {1.0f, Source::Draw};
    if (r.haveRaw) {
        const float t = Clean(r.trigger), g = Clean(r.grip);
        if (t <= 0.0f && g <= 0.0f) return {0.0f, Source::Rest};
        return t >= g ? Squeeze{t, Source::Trigger} : Squeeze{g, Source::Grip};
    }
    if (r.gameGrip > 0.0f) return {Clean(r.gameGrip), Source::GameGrip};
    return {0.0f, Source::Rest};
}

// The fingers' bend as a share of a full fist: open when relaxed, `hook` when hooked (skeletons.cpp Fist's own formula).
inline float Amount(float k, float open, float hook) {
    return open + (hook - open) * Clean(k);
}

// k eased toward its target: a full swing takes closeS going up, openS going down. The state a pose carries from frame to frame.
struct Ease {
    float v = 0.0f;
    float Step(float target, float dt, float closeS = kCloseS, float openS = kOpenS) {
        target = Clean(target);
        if (!(dt > 0.0f)) return v;
        dt = std::min(dt, 0.1f);   // a hitch is not a long swing
        const float span = target > v ? closeS : openS;
        if (span <= 1e-4f) { v = target; return v; }
        const float step = dt / span;
        v = target > v ? std::min(target, v + step) : std::max(target, v - step);
        return v;
    }
    bool Settled(float target) const { return std::fabs(v - Clean(target)) < 0.005f; }
};

}   // namespace bowhand
