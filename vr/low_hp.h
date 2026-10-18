#pragma once
// low_hp.h - [hud] low_hp_vignette: the logic of the low-life heartbeat vignette. Pure: no game, no Windows, no
// globals, no clock (the caller passes the seconds since the last step), so the test (cleanroom/tests/low_hp_test.cpp)
// drives it with synthetic lives and times. The plumbing - the life read from the game, the ini, the effect's
// uniforms, the haptic pulses - is vr/low_hp.cpp.
//
// What the player sees: when the hero's life falls under a threshold the edges of the view tint deep red, and the
// tint beats like a heart - a double beat ("lub-dub") and a rest - faster and stronger the lower the life goes. It
// fades in as the life drops under the threshold and out when it is healed above it (a little hysteresis so it does
// not flicker at the edge); nothing at all when dead, in a menu, or anywhere but first person VR (F4) in a game.
//
// Comfort comes first. It is a colour overlay at the edges of each eye's picture and nothing else: the picture is
// never scaled, moved or shaken, the field of view stays as it is, and the centre of the view is clear. The beat
// changes the overlay's opacity only - its shape never moves. The pulse stays moderate (the rest between beats is
// a third of the peak), the top rate is capped at 90 beats a minute (3 pulses a second, the usual limit for
// flashing), and "gentle" is slower still (at most 72 a minute, 2.4 pulses a second), softer in its attack and lower
// in opacity.
//
// The settings are all sliders (D2R VR Settings, UI: F4 body > Low life): the threshold; the intensity just under it
// and at no life (a straight line of how low the life is between them, the same line the haptic pulse follows); the
// red's saturation; the vignette's size (how far in from the edge it reaches), softness and shape. The size is
// measured in the ANGLE of the view, not in picture pixels: the headset's runtime says how wide each eye's view is,
// vrcam hands that to the effect, and "the middle 60% of the view stays clear" then means 60% of the angle on any
// headset (see Radius). Without a field of view (an older effect, no runtime) it falls back to the picture's own
// proportions.
//
// The vignette's shape (Mask, Apply) is mirrored in reshade/D2R_DepthFog.fx (LowHp); the test compiles that text
// and runs it against these functions, so the two cannot drift apart unseen.
//
// Idea: bearstonem/d2r-pcvr (MIT) shows the hero's life and mana from the game's stat getter (vr/native_ui.cpp,
// steamvr/hand_vitals.h) on a wrist panel; this is a different thing (an overlay on the picture) and written here.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace lowhp {

// ---- the settings --------------------------------------------------------------------------------------------

struct Config {
    bool on = false;               // [hud] low_hp_vignette
    float threshold = 0.30f;       // [hud] low_hp_threshold, a fraction of the maximum life (the ini has percent, 10 - 60)
    float intensityStart = 0.25f;  // [hud] low_hp_intensity_start: the tint's opacity at the top of a beat when the life has just fallen under the threshold
    float intensityEnd = 0.85f;    // [hud] low_hp_intensity_end: the same at no life; in between it follows the life, linearly
    float saturation = 0.8f;       // [hud] low_hp_saturation: 0 a dark grey, 1 the full deep red
    float radius = 0.40f;          // [hud] low_hp_radius: how far in from the edge the tint reaches, a share of the view's half-angle (the ini has percent, 20 - 95)
    float softness = 0.80f;        // [hud] low_hp_softness: how gradually it builds up over that reach (1 all the way, 0 a short ramp then flat)
    float roundness = 2.5f;        // [hud] low_hp_roundness: the shape, 2 an oval ... 6 a rounded square
    bool gentle = false;           // [hud] low_hp_gentle
    bool haptics = false;          // [hud] low_hp_haptics
    float hapticsStrength = 1.0f;  // [hud] low_hp_haptics_strength: a scale on the pulse's amplitude
};

constexpr float kThresholdMin = 0.10f, kThresholdMax = 0.60f, kIntensityMin = 0.05f, kIntensityMax = 1.0f;
constexpr float kRadiusMin = 0.20f, kRadiusMax = 0.95f, kRoundMin = 2.0f, kRoundMax = 6.0f;
constexpr float kHapticsStrengthMin = 0.1f, kHapticsStrengthMax = 2.0f;

inline float Clamp(float v, float lo, float hi, float otherwise) {
    if (!std::isfinite(v)) return otherwise;
    return v < lo ? lo : v > hi ? hi : v;
}

// What is read from the ini, put in range (a number that is not one takes the default).
inline Config Sanitized(Config c) {
    const Config d;
    c.threshold = Clamp(c.threshold, kThresholdMin, kThresholdMax, d.threshold);
    c.intensityStart = Clamp(c.intensityStart, kIntensityMin, kIntensityMax, d.intensityStart);
    c.intensityEnd = Clamp(c.intensityEnd, kIntensityMin, kIntensityMax, d.intensityEnd);
    c.saturation = Clamp(c.saturation, 0.0f, 1.0f, d.saturation);
    c.radius = Clamp(c.radius, kRadiusMin, kRadiusMax, d.radius);
    c.softness = Clamp(c.softness, 0.0f, 1.0f, d.softness);
    c.roundness = Clamp(c.roundness, kRoundMin, kRoundMax, d.roundness);
    c.hapticsStrength = Clamp(c.hapticsStrength, kHapticsStrengthMin, kHapticsStrengthMax, d.hapticsStrength);
    return c;
}
inline float ThresholdFromPercent(float percent) { return percent * 0.01f; }
inline float RadiusFromPercent(float percent) { return percent * 0.01f; }

// The intensity at no life from the ini: the key's own value, else the retired low_hp_strength of an older ini (which
// it replaced), else the default. -1 stands for "the ini has no such key".
inline float ResolveIntensityEnd(float intensityEnd, float legacyStrength) {
    if (intensityEnd >= 0.0f) return intensityEnd;
    if (legacyStrength >= 0.0f) return legacyStrength;
    return Config().intensityEnd;
}

// ---- the life -------------------------------------------------------------------------------------------------

constexpr float kStatScale = 256.0f;   // the game keeps life with 8 fractional bits (stat 6 = life, 7 = its maximum)

struct Life {
    bool known = false;     // the numbers made sense
    bool dead = false;      // no life left
    float frac = 1.0f;      // life over maximum life, 0 - 1
};

// From the game's raw stat values (1/256 of a point). Maximum 0, a maximum under one point or past a million, a
// negative life or one more than twice the maximum are not a hero's life: unknown. (The same sanity bounds as
// d2r-pcvr's vitals read.) The scale cancels in the ratio; it only decides what a "point" is for the bounds.
inline Life LifeFromStats(int32_t life, int32_t maxLife) {
    Life l;
    const int64_t one = (int64_t)kStatScale;
    if (maxLife < one || (int64_t)maxLife >= 1000000 * one) return l;
    if (life < 0 || (int64_t)life > 2 * (int64_t)maxLife) return l;
    l.known = true;
    l.dead = life == 0;
    l.frac = std::min(1.0f, (float)((double)life / (double)maxLife));
    return l;
}

// While the in-headset settings panel is on one of the vignette's pages the vignette is shown as if the life were this far down
// from the threshold to nothing (0.5: half way), so the player sees what the rows change without losing a hero's life.
constexpr float kPreviewSeverity = 0.5f;
inline float PreviewFrac(float threshold) { return threshold * (1.0f - kPreviewSeverity); }

// A read older than this is not the hero's life any more.
constexpr double kStaleMs = 1500.0;
inline bool Fresh(double ageMs) { return ageMs >= 0.0 && ageMs <= kStaleMs; }

// ---- where it may show ----------------------------------------------------------------------------------------

// Everything the vignette needs of the view; vrcam fills it from its own flags.
struct Where {
    bool cameraOn = false;      // vrcam's camera is on
    bool fullBody = false;      // VR first person, F4 (FullBody())
    bool thirdPerson = false;
    bool native = false;        // native OpenXR has the pair (the eyes of the headset)
    bool inWorld = false;       // a game area, not the title or character screens
};
// First person VR (F4) in a game, nothing else: F1, F2, F3 and every flat view stay exactly as they are.
inline bool InView(const Where& w) { return w.cameraOn && w.fullBody && !w.thirdPerson && w.native && w.inWorld; }

// ---- the heartbeat --------------------------------------------------------------------------------------------

// The life under which the vignette comes on is the threshold; it goes off again above the threshold plus this.
constexpr float kHysteresis = 0.05f;

// Seconds the opacity takes to come up and to go: gently when healed, fast when a menu opens or the hero dies.
constexpr float kFadeIn = 0.9f, kFadeInGentle = 1.6f, kFadeOut = 1.4f, kFadeOutGentle = 2.2f, kFadeOutHard = 0.25f;

// One step is never more than this many seconds: a long frame (a load, a stall) must not jump the fade or the beat.
constexpr float kMaxStep = 0.1f;

// How low the life is: 0 at the threshold, 1 at nothing.
inline float Severity(float frac, float threshold) {
    if (!(threshold > 0.0f) || !std::isfinite(frac)) return 0.0f;
    return std::clamp((threshold - frac) / threshold, 0.0f, 1.0f);
}

// Beats a minute: the resting rate at the threshold up to the top rate near no life. The top is capped at 90
// (3 pulses a second with the double beat); gentle is slower all the way.
constexpr float kBpmLow = 60.0f, kBpmHigh = 90.0f, kBpmLowGentle = 50.0f, kBpmHighGentle = 72.0f;
inline float Bpm(float severity, bool gentle) {
    const float lo = gentle ? kBpmLowGentle : kBpmLow, hi = gentle ? kBpmHighGentle : kBpmHigh;
    return lo + (hi - lo) * std::clamp(severity, 0.0f, 1.0f);
}

// How opaque the tint is at its edge-most at the top of a beat, 0 - 1: the user's intensity at the threshold
// (low_hp_intensity_start) up to the intensity at no life (low_hp_intensity_end), a straight line of how low the life
// is, and a little less in gentle. `c` is a Sanitized config.
constexpr float kGentleAlpha = 0.6f;
inline float PeakAlpha(float severity, const Config& c) {
    const float s = Clamp(severity, 0.0f, 1.0f, 0.0f);
    return (c.intensityStart + (c.intensityEnd - c.intensityStart) * s) * (c.gentle ? kGentleAlpha : 1.0f);
}

// The shape of one cycle (phase 0 - 1): a big beat ("lub") and a smaller one ("dub") after it, then a rest. Each
// beat is a smooth rise and a smooth fall (sin^2 up, cos^2 down: no corner anywhere, so no flash), in parts of the
// cycle, so they shorten as the rate goes up.
struct BeatShape {
    float lubRise, lubFall;                 // the first beat starts at phase 0
    float dubAt, dubRise, dubFall, dubPeak; // the second starts at dubAt and peaks at dubPeak (of the first)
    float rest;                             // between beats the tint keeps this share of its peak (the pulse's depth is 1 - rest)
};
inline BeatShape ShapeFor(bool gentle) {
    return gentle ? BeatShape{0.14f, 0.24f, 0.44f, 0.12f, 0.22f, 0.65f, 0.50f}
                  : BeatShape{0.10f, 0.18f, 0.34f, 0.08f, 0.16f, 0.72f, 0.35f};
}

inline float Bump(float x, float rise, float fall) {
    constexpr float kHalfPi = 1.57079632679f;
    if (x < 0.0f || x >= rise + fall) return 0.0f;
    if (x < rise) { const float s = std::sin(kHalfPi * (x / rise)); return s * s; }
    const float c = std::cos(kHalfPi * ((x - rise) / fall));
    return c * c;
}
// The two beats alone: 0 at rest, 1 at the top of the first.
inline float Beats01(float phase, const BeatShape& s) {
    phase -= std::floor(phase);
    return std::min(1.0f, Bump(phase, s.lubRise, s.lubFall) + s.dubPeak * Bump(phase - s.dubAt, s.dubRise, s.dubFall));
}
// The pulse the tint is multiplied by: rest at a floor, 1 at the top of the first beat.
inline float Pulse(float phase, bool gentle) {
    const BeatShape s = ShapeFor(gentle);
    return s.rest + (1.0f - s.rest) * Beats01(phase, s);
}

struct Beats {
    bool lub = false, dub = false;
};
// The moment of each beat, for the haptic pulse: the middle of its rise, so the pulse in the hands lands as the red
// climbs, not before it.
inline float LubAt(const BeatShape& s) { return 0.5f * s.lubRise; }
inline float DubAt(const BeatShape& s) { return s.dubAt + 0.5f * s.dubRise; }

// A phase moved on from p0 by dp (0 <= dp < 1, a step is a tenth of a second at most): did it pass the moment `at`
// (a phase, 0 - 1)? A pass across the wrap round to 0 counts.
inline bool Passed(float p0, float dp, float at) {
    const float p1 = p0 + dp;
    return (p0 < at && p1 >= at) || (p0 < at + 1.0f && p1 >= at + 1.0f);
}
inline Beats Crossed(float p0, float dp, float lubAt, float dubAt) {
    Beats b;
    b.lub = Passed(p0, dp, lubAt);
    b.dub = Passed(p0, dp, dubAt);
    return b;
}

class Heartbeat {
public:
    struct In {
        bool inView = false;     // InView(): false is nothing at all, at once
        bool menu = false;       // a menu is open (the game's menus are flat pictures): fast out
        bool lifeKnown = false;  // a fresh read
        bool dead = false;
        float frac = 1.0f;       // life over maximum life
        float dt = 0.0f;         // seconds since the last step (0 on the first)
    };
    struct Out {
        float alpha = 0.0f;      // what the effect gets: the tint's opacity at the edge now, 0 = none (the effect off)
        float amount = 0.0f;     // the fade alone, 0 - 1
        float bpm = 0.0f;
        float phase = 0.0f;      // 0 - 1 through the cycle
        float severity = 0.0f;   // 0 at the threshold, 1 at no life
        bool active = false;     // under the threshold (with its hysteresis)
        Beats beat;              // a beat started in this step (for the haptic pulse)
    };

    Out Step(const Config& config, const In& in) {
        const Config cfg = Sanitized(config);
        Out out;
        if (!cfg.on || !in.inView) {   // nothing is shown, and nothing is kept
            Reset();
            return out;
        }
        const float dt = std::isfinite(in.dt) ? std::clamp(in.dt, 0.0f, kMaxStep) : 0.0f;
        const bool usable = in.lifeKnown && !in.dead && !in.menu && std::isfinite(in.frac) && in.frac > 0.0f;
        const bool hard = !usable;   // dead, a menu, no read: out fast
        if (usable) sev_ = Severity(in.frac, cfg.threshold);
        // the threshold and its hysteresis
        if (!usable) active_ = false;
        else if (active_) { if (in.frac > std::min(1.0f, cfg.threshold + kHysteresis)) active_ = false; }
        else if (in.frac < cfg.threshold) active_ = true;
        // the fade
        const float target = active_ ? 1.0f : 0.0f;
        if (target > fade_) fade_ = std::min(target, fade_ + dt / (cfg.gentle ? kFadeInGentle : kFadeIn));
        else if (target < fade_) fade_ = std::max(target, fade_ - dt / (hard ? kFadeOutHard : cfg.gentle ? kFadeOutGentle : kFadeOut));
        if (!active_ && fade_ <= 0.0f) {   // at rest: the next time starts from the first beat
            phase_ = 0.0f;
            return out;
        }
        // the beat, at the rate the life asks for now
        const float bpm = Bpm(sev_, cfg.gentle);
        const float dp = std::min(0.99f, dt * bpm / 60.0f);
        const BeatShape shape = ShapeFor(cfg.gentle);
        out.beat = Crossed(phase_, dp, LubAt(shape), DubAt(shape));
        phase_ += dp;
        phase_ -= std::floor(phase_);
        const float amount = fade_ * fade_ * (3.0f - 2.0f * fade_);
        out.amount = amount;
        out.bpm = bpm;
        out.phase = phase_;
        out.severity = sev_;
        out.active = active_;
        out.alpha = amount * PeakAlpha(sev_, cfg) * Pulse(phase_, cfg.gentle);
        return out;
    }

    void Reset() { *this = Heartbeat(); }
    bool Active() const { return active_; }

private:
    bool active_ = false;
    float fade_ = 0.0f;     // 0 - 1, linear in time (the eased amount is made of it)
    float phase_ = 0.0f;
    float sev_ = 0.0f;      // the last usable read's severity, kept while the tint fades out
};

// The controllers' pulse on a beat ([hud] low_hp_haptics, through [openxr] haptics): the lower the life, the stronger
// - it follows the visual intensity exactly (the same start and end sliders, the same straight line of how low the life
// is, quieter in gentle too), times its own scale (low_hp_haptics_strength). Short (40 ms, the dub 28 and softer),
// never over kHapticMax (a pulse that strong numbs the hand or annoys). It lands in the middle of each beat's rise
// (LubAt, DubAt), in both hands at once, as the red climbs. Only while the tint is plainly there (kHapticFrom of
// the fade), so a vignette still coming up does not buzz.
constexpr float kHapticFrom = 0.35f, kHapticOfAlpha = 0.55f, kHapticMax = 0.8f, kDubSofter = 0.7f;
struct Haptic {
    float amplitude;   // 0 - 1
    int ms;
};
inline Haptic HapticFor(bool dub, float severity, const Config& config) {
    const Config c = Sanitized(config);
    const float a = kHapticOfAlpha * PeakAlpha(severity, c) * c.hapticsStrength * (dub ? kDubSofter : 1.0f);
    return Haptic{std::min(a, kHapticMax), dub ? 28 : 40};
}

// The settings panel's "Preview 5 s" button (the bottom bar of the Life and the Look page). Opening a page changes nothing - no red, no pulse in the hands.
// A press shows the vignette and plays the beat, both with the panel's rows as they stand (whatever the ticks say: a preview that stays dark because the
// tick is off shows nothing), for five seconds, and then it stops by itself - the red fades out for a moment (kPreviewTailMs) and the hands are quiet at
// once. A second press starts the five seconds again; leaving the page or closing the panel stops it. In play none of this exists.
constexpr uint64_t kPreviewMs = 5000, kPreviewTailMs = 1600;
class PreviewWindow {   // a clock in ms (GetTickCount64); any thread
    std::atomic<uint64_t> until_{0};
public:
    void Start(uint64_t nowMs) { until_.store(nowMs + kPreviewMs, std::memory_order_relaxed); }
    void Stop() { until_.store(0, std::memory_order_relaxed); }
    bool Active(uint64_t nowMs) const { return nowMs < until_.load(std::memory_order_relaxed); }                             // the five seconds: life shown as half down, the pulse
    bool Showing(uint64_t nowMs) const { const uint64_t u = until_.load(std::memory_order_relaxed); return u && nowMs < u + kPreviewTailMs; }   // and the red's fade-out after
};
// The panel's rows as the preview plays them: the vignette on, the beat in the hands on.
inline Config PreviewConfig(const Config& draft) {
    Config c = Sanitized(draft);
    c.on = true;
    c.haptics = true;
    return c;
}
// May the heartbeat pulse in the hands now? In play the setting's tick decides, as ever; the red's fade-out after a preview is quiet (it is not the life's beat).
inline bool PulseAllowed(bool tick, bool afterPreview) { return tick && !afterPreview; }

// ---- the vignette's shape (mirrored in reshade/D2R_DepthFog.fx) ----------------------------------------------

// How wide each eye's view is, as the tangent of the half-angle on each axis, from the runtime's field of view (the
// picture IS that field of view). Two eyes' (left, right) = (lo, hi) tangents averaged; 0 when they are not numbers
// or are not a view at all (under 3 degrees, over 170): then the shape follows the picture's own proportions.
inline float HalfTan(float lo0, float hi0, float lo1, float hi1) {
    const float a = 0.25f * ((hi0 - lo0) + (hi1 - lo1));
    return std::isfinite(a) && a > 0.05f && a < 12.0f ? a : 0.0f;
}

// A picture's uv (0 - 1 across and down): how far out it is, in parts of the view's ANGLE. 0 in the middle, 1 at the
// middle of an edge, a bit more in the corners; `roundness` bends the rings between ovals (2) and rounded
// rectangles (higher). With the field of view (tanX, tanY: the half-extent of the picture as tangents) each axis is
// turned into the angle it looks at - atan(x tan) / atan(tan) - so the same slider means the same angle on a headset
// of 90 degrees and of 130; with 0 the picture's own linear uv.
inline float Radius(float u, float v, float roundness, float tanX = 0.0f, float tanY = 0.0f) {
    float x = std::fabs(u * 2.0f - 1.0f), y = std::fabs(v * 2.0f - 1.0f);
    if (tanX > 0.0f && tanY > 0.0f) {
        x = std::atan(x * tanX) / std::atan(tanX);
        y = std::atan(y * tanY) / std::atan(tanY);
    }
    return std::pow(std::pow(x, roundness) + std::pow(y, roundness), 1.0f / roundness);
}
inline float Smoothstep(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Where the tint is: nothing inside `inner` (the clear centre of the view), rising to the whole of it at `outer`,
// and flat past that to the edge. From the settings: the radius is how far in from the edge it reaches (the clear
// centre is the rest: 1 - radius of the view's angle across); the softness is how much of that reach the rise takes
// - 1 all of it, up to a little past the edge (a gradual vignette), 0 a third of it (a short ramp, then flat to the
// rim). The edge's middle is 1, so with a reach of 40% the middle 60% of the view's angle is clear.
constexpr float kOverEdge = 0.10f, kShortestRamp = 0.35f;
struct Shape {
    float inner, outer, roundness;
    float tanX = 0.0f, tanY = 0.0f;   // the field of view (HalfTan), 0 = not known
};
inline Shape ShapeOf(const Config& config, float tanX = 0.0f, float tanY = 0.0f) {
    const Config c = Sanitized(config);
    Shape s;
    s.inner = 1.0f - c.radius;
    s.outer = s.inner + (c.radius + kOverEdge) * (kShortestRamp + (1.0f - kShortestRamp) * c.softness);
    s.roundness = c.roundness;
    s.tanX = tanX;
    s.tanY = tanY;
    return s;
}
inline float Mask(float u, float v, const Shape& s) { return Smoothstep(s.inner, s.outer, Radius(u, v, s.roundness, s.tanX, s.tanY)); }

// The colour of the tint: a dark grey (saturation 0) to a deep red (1). Whatever colour space the effect's picture
// is in, it is a colour; tuned in the headset.
constexpr float kRed[3] = {0.34f, 0.0f, 0.0f}, kGrey[3] = {0.09f, 0.09f, 0.09f};
inline void ColorOf(float saturation, float out[3]) {
    const float t = Clamp(saturation, 0.0f, 1.0f, Config().saturation);
    for (int i = 0; i < 3; ++i) out[i] = kGrey[i] + (kRed[i] - kGrey[i]) * t;
}

// The picture's colour with the tint over it: unchanged when alpha is 0.
inline void Apply(const float in[3], float u, float v, float alpha, const Shape& shape, const float color[3], float out[3]) {
    const float k = Clamp(alpha, 0.0f, 1.0f, 0.0f) * Mask(u, v, shape);
    for (int i = 0; i < 3; ++i) out[i] = k > 0.0f ? in[i] + (color[i] - in[i]) * k : in[i];
}

// What the effect is handed each frame (the same in both eyes): the opacity, the shape (inner, outer, roundness),
// the colour and the field of view. (The effect's own defaults, before it is handed any, are a half-way vignette in
// deep red on the picture's own proportions.)
struct Uniforms {
    float alpha = 0.0f;
    float shape[4] = {0.5f, 1.1f, 2.5f, 0.0f};
    float color[3] = {0.30f, 0.005f, 0.005f};
    float fov[2] = {0.0f, 0.0f};
};
inline Uniforms MakeUniforms(float alpha, const Config& config, float tanX = 0.0f, float tanY = 0.0f) {
    Uniforms u;
    u.alpha = Clamp(alpha, 0.0f, 1.0f, 0.0f);
    const bool fov = tanX > 0.0f && tanY > 0.0f && std::isfinite(tanX) && std::isfinite(tanY);
    const Shape s = ShapeOf(config, fov ? tanX : 0.0f, fov ? tanY : 0.0f);
    u.shape[0] = s.inner;
    u.shape[1] = s.outer;
    u.shape[2] = s.roundness;
    ColorOf(Sanitized(config).saturation, u.color);
    u.fov[0] = s.tanX;
    u.fov[1] = s.tanY;
    return u;
}

}  // namespace lowhp
