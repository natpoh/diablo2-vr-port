// The low-life heartbeat vignette (vr/low_hp.h) without the game or a headset:
//
//   - the life maths (the game's stat numbers with their fractional bits, a maximum of 0, nonsense),
//   - the beat's shape (a double beat, smooth, moderate, never faster than 90 a minute), the rate at the threshold and
//     near no life, the hysteresis, the fade in and out, a long frame hitch, life going up and down,
//   - the off states (the setting off; anywhere but first person VR F4 in a game) producing nothing at all,
//   - the vignette's shape (the middle clear, the edges tinted, symmetric) and the picture bit for bit when the
//     opacity is 0,
//   - the effect's own HLSL (the text between the LOWHP marks in reshade/D2R_DepthFog.fx) compiled and run on a software
//     D3D11 device (WARP) against the C++ mirror,
//   - and, given a folder, the pictures: the vignette over a synthetic scene at three life levels and four moments of the
//     beat, the same gentle, a wide picture, and the opacity over time. Look at them to judge the look without a headset.
//
//   low_hp_test [<folder for the pictures>] [--fx <path to D2R_DepthFog.fx>]
//
// What it cannot say: how it looks in the lenses, whether the pulse is comfortable, the colour in the game's colour space.

#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "haptics_pattern.h"
#include "low_hp.h"

using Microsoft::WRL::ComPtr;
using namespace lowhp;

namespace {

int g_failed = 0, g_checks = 0;
void Expect(bool ok, const char* fmt, ...) {
    ++g_checks;
    if (ok) return;
    ++g_failed;
    char b[300];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    printf("  FAIL: %s\n", b);
}
bool Near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

constexpr float kDt = 1.0f / 90.0f;

// Steps `seconds` at the headset's rate with the same input; returns the last output.
Heartbeat::Out Run(Heartbeat& hb, const Config& c, Heartbeat::In in, float seconds) {
    Heartbeat::Out out;
    in.dt = kDt;
    for (float t = 0.0f; t < seconds; t += kDt) out = hb.Step(c, in);
    return out;
}
Heartbeat::In Seen(float frac) {
    Heartbeat::In in;
    in.inView = true;
    in.lifeKnown = true;
    in.frac = frac;
    return in;
}
Config On() {
    Config c;
    c.on = true;
    return c;
}

// ---- the life ----------------------------------------------------------------------------------------------------------
void TestLife() {
    printf("the life\n");
    Life l = LifeFromStats(50 * 256, 100 * 256);
    Expect(l.known && !l.dead && Near(l.frac, 0.5f, 1e-6f), "50 of 100");
    // the fractional bits: 29.996 of 100 is under 30%, exactly 30.000 is not, 30.004 is over
    const float thr = 0.30f;
    Expect(LifeFromStats(7679, 25600).frac < thr, "29.996 of 100 is under 30%%");
    Expect(!(LifeFromStats(7680, 25600).frac < thr), "30.000 of 100 is not under 30%%");
    Expect(LifeFromStats(7681, 25600).frac > thr, "30.004 of 100 is over 30%%");
    l = LifeFromStats(25 * 256 + 128, 100 * 256);
    Expect(l.known && Near(l.frac, 0.255f, 1e-6f), "25.5 of 100 = 0.255 (half a point kept)");
    l = LifeFromStats(1, 1000 * 256);
    Expect(l.known && !l.dead && l.frac > 0.0f && l.frac < 1e-5f, "a 256th of a point left is alive");
    l = LifeFromStats(0, 100 * 256);
    Expect(l.known && l.dead && l.frac == 0.0f, "no life: dead");
    Expect(!LifeFromStats(50 * 256, 0).known, "maximum 0: unknown");
    Expect(!LifeFromStats(0, 0).known, "0 of 0: unknown");
    Expect(!LifeFromStats(50 * 256, -1).known, "a negative maximum: unknown");
    Expect(!LifeFromStats(5 * 256, 255).known, "a maximum under one point: unknown");
    Expect(!LifeFromStats(-1, 100 * 256).known, "a negative life: unknown");
    Expect(!LifeFromStats(201 * 256, 100 * 256).known, "life over twice the maximum: unknown");
    Expect(LifeFromStats(150 * 256, 100 * 256).known && LifeFromStats(150 * 256, 100 * 256).frac == 1.0f, "life over the maximum (a buff): full");
    Expect(LifeFromStats(1000 * 256, 999999 * 256).known && !LifeFromStats(1000 * 256, 1000000 * 256).known, "a maximum of a million points: unknown");
    Expect(!LifeFromStats(2147483647, 2147483647).known, "the largest numbers: unknown, no overflow");
    Expect(Fresh(0.0) && Fresh(1500.0) && !Fresh(1500.5) && !Fresh(-1.0), "a read's age");
}

// ---- the settings ------------------------------------------------------------------------------------------------------
void TestConfig() {
    printf("the settings\n");
    const Config d;
    Expect(!d.on && d.threshold == 0.30f && d.intensityStart == 0.25f && d.intensityEnd == 0.85f && d.saturation == 0.8f && d.radius == 0.40f &&
               d.softness == 0.80f && d.roundness == 2.5f && !d.gentle && !d.haptics && d.hapticsStrength == 1.0f,
           "the defaults: off, 30%%, intensity 0.25 .. 0.85, red 0.8, reaches 40%%, softness 0.8, shape 2.5, haptics strength 1");
    Config c;
    c.threshold = 0.05f; c.intensityStart = 0.0f; c.intensityEnd = -3.0f; c.saturation = -1.0f; c.radius = 0.05f; c.softness = -2.0f;
    c.roundness = 0.5f; c.hapticsStrength = 0.0f;
    Config s = Sanitized(c);
    Expect(s.threshold == kThresholdMin && s.intensityStart == kIntensityMin && s.intensityEnd == kIntensityMin && s.saturation == 0.0f &&
               s.radius == kRadiusMin && s.softness == 0.0f && s.roundness == kRoundMin && s.hapticsStrength == kHapticsStrengthMin,
           "low values come up to the range");
    c.threshold = 0.8f; c.intensityStart = 5.0f; c.intensityEnd = 9.0f; c.saturation = 3.0f; c.radius = 2.0f; c.softness = 9.0f;
    c.roundness = 99.0f; c.hapticsStrength = 50.0f;
    s = Sanitized(c);
    Expect(s.threshold == kThresholdMax && s.intensityStart == kIntensityMax && s.intensityEnd == kIntensityMax && s.saturation == 1.0f &&
               s.radius == kRadiusMax && s.softness == 1.0f && s.roundness == kRoundMax && s.hapticsStrength == kHapticsStrengthMax,
           "high values come down to the range");
    c.threshold = NAN; c.intensityStart = INFINITY; c.intensityEnd = NAN; c.saturation = NAN; c.radius = -INFINITY; c.softness = NAN;
    c.roundness = NAN; c.hapticsStrength = INFINITY;
    s = Sanitized(c);
    Expect(s.threshold == d.threshold && s.intensityStart == d.intensityStart && s.intensityEnd == d.intensityEnd && s.saturation == d.saturation &&
               s.radius == d.radius && s.softness == d.softness && s.roundness == d.roundness && s.hapticsStrength == d.hapticsStrength,
           "not a number: the default");
    Expect(Near(ThresholdFromPercent(30.0f), 0.30f, 1e-6f) && Near(RadiusFromPercent(40.0f), 0.40f, 1e-6f) && Near(RadiusFromPercent(95.0f), 0.95f, 1e-6f), "percent in the ini");
    // the first build's single strength, in an older ini, is the intensity at no life; its own key wins
    Expect(ResolveIntensityEnd(0.7f, 0.3f) == 0.7f, "the new key wins over the old one");
    Expect(ResolveIntensityEnd(-1.0f, 0.6f) == 0.6f, "an older ini's low_hp_strength is the intensity at no life");
    Expect(ResolveIntensityEnd(-1.0f, -1.0f) == d.intensityEnd, "neither: the default");
    Expect(ResolveIntensityEnd(0.0f, 0.6f) == 0.0f, "a key set to 0 is a key (it is then clamped, not replaced)");
}

// ---- where ------------------------------------------------------------------------------------------------------------
void TestWhere() {
    printf("where it may show\n");
    int shown = 0;
    for (int m = 0; m < 32; ++m) {
        Where w;
        w.cameraOn = m & 1; w.fullBody = m & 2; w.thirdPerson = m & 4; w.native = m & 8; w.inWorld = m & 16;
        const bool want = w.cameraOn && w.fullBody && !w.thirdPerson && w.native && w.inWorld;
        Expect(InView(w) == want, "InView for mask %d", m);
        shown += InView(w) ? 1 : 0;
    }
    Expect(shown == 1, "exactly one combination shows: first person VR (F4) on native in a game (%d)", shown);
    // every other view is nothing at once, however low the life and however far the fade had come
    Heartbeat hb;
    Config c = On();
    Run(hb, c, Seen(0.05f), 3.0f);
    Expect(hb.Active(), "setup: showing");
    Heartbeat::In out = Seen(0.05f);
    out.inView = false;
    out.dt = kDt;
    const Heartbeat::Out o = hb.Step(c, out);
    Expect(o.alpha == 0.0f && o.amount == 0.0f && !o.active && !hb.Active(), "left F4: nothing in the very next step (%g)", o.alpha);
    Expect(!Run(hb, c, out, 2.0f).active, "and nothing after");
    // back in F4 it starts from nothing (no tint kept)
    const Heartbeat::Out back = hb.Step(c, [] { auto i = Seen(0.05f); i.dt = kDt; return i; }());
    Expect(back.alpha < 0.01f, "back in F4 it comes up again from nothing (%g)", back.alpha);
}

// ---- the beat's shape -------------------------------------------------------------------------------------------------
void TestShape() {
    printf("the beat\n");
    for (int g = 0; g < 2; ++g) {
        const bool gentle = g != 0;
        const BeatShape s = ShapeFor(gentle);
        const char* name = gentle ? "gentle" : "normal";
        Expect(Near(Beats01(0.0f, s), 0.0f, 1e-6f), "%s: rest at phase 0", name);
        Expect(Near(Beats01(s.lubRise, s), 1.0f, 1e-5f), "%s: the first beat tops at 1", name);
        Expect(Near(Beats01(s.lubRise + s.lubFall, s), 0.0f, 1e-5f) && Near(Beats01(s.lubRise + s.lubFall + 0.01f, s), 0.0f, 1e-6f), "%s: the first beat ends", name);
        Expect(Near(Beats01(s.dubAt + s.dubRise, s), s.dubPeak, 1e-5f), "%s: the second tops at %.2f", name, s.dubPeak);
        Expect(s.dubPeak < 1.0f && s.dubPeak > 0.4f, "%s: the second is softer than the first", name);
        Expect(s.lubRise + s.lubFall < s.dubAt, "%s: the beats do not overlap", name);
        Expect(s.dubAt + s.dubRise + s.dubFall < 1.0f, "%s: a rest follows", name);
        Expect(Near(Beats01(0.8f, s), 0.0f, 1e-6f), "%s: rest late in the cycle", name);
        Expect(Near(Beats01(1.0f + 0.2f, s), Beats01(0.2f, s), 1e-5f) && Near(Beats01(-0.93f, s), Beats01(0.07f, s), 1e-5f), "%s: periodic", name);
        // two humps per cycle, smooth, never a jump
        int humps = 0;
        float prev = Beats01(0.0f, s), prevPrev = prev, maxStep = 0.0f, lo = 1.0f, hi = 0.0f;
        constexpr int N = 4000;
        for (int i = 1; i <= N; ++i) {
            const float v = Beats01((float)i / N, s);
            maxStep = std::max(maxStep, std::fabs(v - prev));
            if (prev > prevPrev && prev >= v && prev > 0.1f) ++humps;
            prevPrev = prev; prev = v;
            lo = std::min(lo, Pulse((float)i / N, gentle)); hi = std::max(hi, Pulse((float)i / N, gentle));
        }
        Expect(humps == 2, "%s: two beats a cycle (%d)", name, humps);
        Expect(maxStep < 0.02f, "%s: no jump anywhere (largest step %.4f of 4000 a cycle)", name, maxStep);
        Expect(Near(lo, s.rest, 1e-5f) && Near(hi, 1.0f, 1e-4f), "%s: the pulse runs from %.2f to 1 (%.3f .. %.3f)", name, s.rest, lo, hi);
        Expect(1.0f - s.rest <= 0.65f + 1e-6f, "%s: the pulse's depth is moderate (%.2f)", name, 1.0f - s.rest);
        // how fast a rise is at the top rate, in real time: never a flash
        const float topBpm = Bpm(1.0f, gentle);
        const float riseMs = std::min(s.lubRise, s.dubRise) * 60.0f / topBpm * 1000.0f;
        Expect(riseMs >= (gentle ? 90.0f : 45.0f), "%s: the quickest rise takes %.0f ms at %.0f a minute", name, riseMs, topBpm);
        Expect(topBpm * 2.0f / 60.0f <= (gentle ? 2.5f : 3.0f) + 1e-4f, "%s: at most %.1f pulses a second", name, topBpm * 2.0f / 60.0f);
    }
    Expect(ShapeFor(true).lubRise > ShapeFor(false).lubRise && ShapeFor(true).rest > ShapeFor(false).rest, "gentle: a softer attack and a shallower pulse");
    // a phase just past the wrap and the crossing detector
    Beats b = Crossed(0.95f, 0.1f, 0.05f, 0.40f);
    Expect(b.lub && !b.dub, "crossing the wrap and the first beat's moment is a lub");
    b = Crossed(0.39f, 0.02f, 0.05f, 0.40f);
    Expect(!b.lub && b.dub, "crossing the second beat's moment is a dub");
    b = Crossed(0.50f, 0.02f, 0.05f, 0.40f);
    Expect(!b.lub && !b.dub, "nothing crossed");
    b = Crossed(0.40f, 0.02f, 0.05f, 0.40f);
    Expect(!b.dub, "standing on the moment is not crossing it again");
    b = Crossed(0.99f, 0.05f, 0.02f, 0.40f);
    Expect(b.lub && !b.dub, "a moment just past the wrap is crossed across it");
    for (int g = 0; g < 2; ++g) {
        const BeatShape s = ShapeFor(g != 0);
        Expect(LubAt(s) > 0.0f && LubAt(s) < s.lubRise && DubAt(s) > s.dubAt && DubAt(s) < s.dubAt + s.dubRise, "the pulse moments are inside the rises (%s)", g ? "gentle" : "normal");
    }
}

// ---- rate, intensity --------------------------------------------------------------------------------------------------
void TestRate() {
    printf("rate and intensity\n");
    Expect(Near(Severity(0.30f, 0.30f), 0.0f, 1e-6f) && Near(Severity(0.15f, 0.30f), 0.5f, 1e-6f) && Severity(0.0f, 0.30f) == 1.0f, "severity: 0 at the threshold, 1 at nothing");
    Expect(Severity(0.9f, 0.30f) == 0.0f && Severity(NAN, 0.30f) == 0.0f && Severity(0.1f, 0.0f) == 0.0f, "severity: clamped, not a number safe");
    Expect(Bpm(0.0f, false) == kBpmLow && Bpm(1.0f, false) == kBpmHigh, "bpm: %.0f at the threshold, %.0f near no life", Bpm(0.0f, false), Bpm(1.0f, false));
    Expect(Bpm(0.0f, true) == kBpmLowGentle && Bpm(1.0f, true) == kBpmHighGentle, "gentle bpm: %.0f and %.0f", Bpm(0.0f, true), Bpm(1.0f, true));
    Expect(Bpm(-3.0f, false) == kBpmLow && Bpm(7.0f, false) == kBpmHigh, "bpm is clamped");
    // the intensity: the user's start at the threshold, the end at no life, a straight line between
    const Config d;
    Config g = d;
    g.gentle = true;
    bool line = true, mono = true, slower = true, gentleScaled = true;
    float prevB = 0.0f, prevP = 0.0f;
    for (int i = 0; i <= 20; ++i) {
        const float sv = (float)i / 20.0f;
        line = line && Near(PeakAlpha(sv, d), d.intensityStart + (d.intensityEnd - d.intensityStart) * sv, 1e-6f);
        mono = mono && Bpm(sv, false) >= prevB && PeakAlpha(sv, d) >= prevP;
        slower = slower && Bpm(sv, true) < Bpm(sv, false);
        gentleScaled = gentleScaled && Near(PeakAlpha(sv, g), PeakAlpha(sv, d) * kGentleAlpha, 1e-6f);
        prevB = Bpm(sv, false); prevP = PeakAlpha(sv, d);
    }
    Expect(line, "the intensity is a straight line of how low the life is, from the start to the end");
    Expect(mono, "the lower the life, the faster and the stronger");
    Expect(slower && gentleScaled, "gentle: slower, and %.0f%% of the intensity, at every level of life", kGentleAlpha * 100.0f);
    Expect(Near(PeakAlpha(0.0f, d), 0.25f, 1e-6f) && Near(PeakAlpha(0.5f, d), 0.55f, 1e-6f) && Near(PeakAlpha(1.0f, d), 0.85f, 1e-6f), "defaults: 0.25 at the threshold, 0.55 half way, 0.85 at no life");
    Config k = d;
    k.intensityStart = 0.10f; k.intensityEnd = 0.50f;
    Expect(Near(PeakAlpha(0.0f, k), 0.10f, 1e-6f) && Near(PeakAlpha(0.5f, k), 0.30f, 1e-6f) && Near(PeakAlpha(1.0f, k), 0.50f, 1e-6f), "other ends: 0.10 .. 0.30 .. 0.50");
    k.intensityStart = 0.80f; k.intensityEnd = 0.20f;
    Expect(Near(PeakAlpha(0.0f, k), 0.80f, 1e-6f) && Near(PeakAlpha(1.0f, k), 0.20f, 1e-6f), "a start above the end is followed as set");
    Expect(PeakAlpha(-5.0f, d) == d.intensityStart && PeakAlpha(9.0f, d) == d.intensityEnd, "the line is clamped to its ends");
    Expect(Bpm(1.0f, false) <= 90.0f, "the fastest is 90 a minute (3 pulses a second)");
    // through the state machine: the top of a beat reaches the line's value at both ends
    for (float frac : {0.299f, 0.15f, 0.001f}) {
        Heartbeat hb;
        Heartbeat::In in = Seen(frac);
        in.dt = kDt;
        Run(hb, On(), in, 3.0f);
        float best = 0.0f;
        for (int i = 0; i < 180; ++i) best = std::max(best, hb.Step(On(), in).alpha);   // two seconds, a few beats
        const float want = PeakAlpha(Severity(frac, 0.30f), d);
        Expect(best <= want + 1e-4f && best >= 0.95f * want, "life %.1f%%: the top of a beat is %.3f (the line says %.3f)", frac * 100.0f, best, want);
    }
    // the rate just under the threshold and near no life
    Heartbeat hb;
    Heartbeat::Out o = Run(hb, On(), Seen(0.299f), 2.0f);
    Expect(o.active && Near(o.bpm, kBpmLow, 0.2f), "just under the threshold: %.1f a minute", o.bpm);
    Heartbeat hb2;
    o = Run(hb2, On(), Seen(0.001f), 2.0f);
    Expect(o.active && Near(o.bpm, kBpmHigh, 0.5f), "near no life: %.1f a minute", o.bpm);
    Config gc = On();
    gc.gentle = true;
    Heartbeat hb3;
    o = Run(hb3, gc, Seen(0.001f), 3.0f);
    Expect(o.active && Near(o.bpm, kBpmHighGentle, 0.5f), "gentle near no life: %.1f a minute", o.bpm);
    Config hi = On();
    hi.threshold = 0.6f;
    Heartbeat hb4;
    o = Run(hb4, hi, Seen(0.59f), 1.0f);
    Expect(o.active, "a threshold of 60%%");
    Config t = On();
    t.threshold = 0.5f;
    Heartbeat hb5;
    o = Run(hb5, t, Seen(0.25f), 2.0f);
    Expect(Near(o.severity, 0.5f, 1e-5f) && Near(o.bpm, Bpm(0.5f, false), 0.2f), "threshold 50%%, life 25%%: half way (%.2f, %.1f a minute)", o.severity, o.bpm);
}

// ---- hysteresis ------------------------------------------------------------------------------------------------------
void TestHysteresis() {
    printf("hysteresis\n");
    Config c = On();   // 30%
    Heartbeat hb;
    auto at = [&](float frac) {
        Heartbeat::In in = Seen(frac);
        in.dt = kDt;
        hb.Step(c, in);
        return hb.Active();
    };
    Expect(!at(0.5f) && !at(0.31f) && !at(0.30f), "above or at the threshold: off");
    Expect(at(0.299f), "just under: on");
    Expect(at(0.33f) && at(0.35f), "healed a little: still on");
    Expect(!at(0.351f), "past threshold + 5 points: off");
    Expect(!at(0.34f) && !at(0.31f), "and it stays off in the band");
    Expect(at(0.29f), "under again: on");
    // noise round the threshold flickers nothing
    int flips = 0;
    bool last = hb.Active();
    for (int i = 0; i < 400; ++i) {
        const bool now = at(i % 2 ? 0.295f : 0.305f);
        flips += now != last ? 1 : 0;
        last = now;
    }
    Expect(flips == 0 && last, "alternating 29.5%% and 30.5%%: on throughout (%d flips)", flips);
    Heartbeat fresh;
    hb = fresh;
    flips = 0;
    last = false;
    for (int i = 0; i < 400; ++i) {
        const bool now = at(i % 2 ? 0.28f : 0.33f);
        flips += now != last ? 1 : 0;
        last = now;
    }
    Expect(flips == 1, "alternating 28%% and 33%%: switches on once and stays (%d flips)", flips);
    // the band's top follows the threshold, and never passes full life
    Config top = On();
    top.threshold = 0.6f;
    Heartbeat hb2;
    Heartbeat::In in = Seen(0.5f);
    in.dt = kDt;
    hb2.Step(top, in);
    in.frac = 0.64f;
    hb2.Step(top, in);
    Expect(hb2.Active(), "threshold 60%%: still on at 64%%");
    in.frac = 0.66f;
    hb2.Step(top, in);
    Expect(!hb2.Active(), "threshold 60%%: off at 66%%");
}

// ---- the fade ---------------------------------------------------------------------------------------------------------
void TestFade() {
    printf("the fade\n");
    Config c = On();
    Heartbeat hb;
    Heartbeat::In in = Seen(0.2f);
    in.dt = kDt;
    Heartbeat::Out o = hb.Step(c, in);
    Expect(o.alpha < 0.02f && o.amount < 0.001f, "the first step shows (almost) nothing: %g", o.alpha);
    float last = o.amount, half = 0.0f;
    bool mono = true, within = true;
    int steps = 1;
    for (; steps < 120 && o.amount < 1.0f; ++steps) {
        o = hb.Step(c, in);
        mono = mono && o.amount >= last;
        within = within && o.alpha <= PeakAlpha(o.severity, c) + 1e-6f && o.alpha >= 0.0f;
        if (steps == 40) half = o.amount;
        last = o.amount;
    }
    Expect(mono && within, "fading in: only upwards, never over the peak");
    Expect(half > 0.3f && half < 0.7f, "at 0.44 s the fade is about half way (%.2f)", half);
    Expect(Near(steps * kDt, kFadeIn, 0.03f), "fully in after %.2f s (%.2f wanted)", steps * kDt, kFadeIn);
    // healed: out, slowly, and only after the band
    Heartbeat::In healed = Seen(0.6f);
    healed.dt = kDt;
    o = hb.Step(c, healed);
    Expect(!o.active && o.amount > 0.99f, "healed: it starts to go, not at once (%.3f)", o.amount);
    float prev = o.amount;
    bool down = true;
    int outSteps = 0;
    float mid = 1.0f;
    for (; outSteps < 300 && o.amount > 0.0f; ++outSteps) {
        o = hb.Step(c, healed);
        down = down && o.amount <= prev;
        prev = o.amount;
        if (outSteps == 60) mid = o.amount;
    }
    Expect(down, "fading out: only downwards");
    Expect(mid > 0.1f && mid < 0.9f, "at 0.67 s out it is still part way (%.2f)", mid);
    Expect(Near(outSteps * kDt, kFadeOut, 0.05f), "out after %.2f s (%.2f wanted)", outSteps * kDt, kFadeOut);
    Expect(o.alpha == 0.0f && !hb.Active(), "at rest: nothing");
    // gentle takes longer both ways
    Config g = c;
    g.gentle = true;
    Heartbeat hg;
    int gin = 0;
    o = Heartbeat::Out();
    for (; gin < 400 && o.amount < 1.0f; ++gin) o = hg.Step(g, in);
    Expect(Near(gin * kDt, kFadeInGentle, 0.05f), "gentle in after %.2f s", gin * kDt);
    int gout = 0;
    for (; gout < 600 && o.amount > 0.0f; ++gout) o = hg.Step(g, healed);
    Expect(Near(gout * kDt, kFadeOutGentle, 0.06f), "gentle out after %.2f s", gout * kDt);
    // dead, a menu, no read: out fast
    struct Case { const char* name; Heartbeat::In in; } cases[3];
    cases[0] = {"dead", Seen(0.0f)};   cases[0].in.dead = true;
    cases[1] = {"a menu", Seen(0.1f)}; cases[1].in.menu = true;
    cases[2] = {"no read", Seen(0.1f)}; cases[2].in.lifeKnown = false;
    for (Case& k : cases) {
        Heartbeat h;
        Run(h, c, Seen(0.1f), 2.0f);
        k.in.dt = kDt;
        int n = 0;
        o = Heartbeat::Out();
        o.amount = 1.0f;
        for (; n < 200 && !(o.alpha == 0.0f && n > 0); ++n) o = h.Step(c, k.in);
        Expect(n * kDt <= kFadeOutHard + 0.04f && n > 3, "%s: gone in %.2f s, not in one frame", k.name, n * kDt);
    }
    // dead at once while it was coming up
    Heartbeat h2;
    Run(h2, c, Seen(0.1f), 0.3f);
    Heartbeat::In dead = Seen(0.0f);
    dead.dead = true;
    dead.dt = kDt;
    Expect(h2.Step(c, dead).alpha < PeakAlpha(1.0f, c), "dead: on its way out");
    Run(h2, c, dead, 0.5f);
    Expect(!h2.Active(), "dead: not active");
}

// ---- a long frame -----------------------------------------------------------------------------------------------------
void TestHitch() {
    printf("a long frame\n");
    Config c = On();
    Heartbeat hb;
    Heartbeat::In in = Seen(0.1f);
    in.dt = kDt;
    // a hitch in the middle of the fade in
    Run(hb, c, in, 0.3f);
    const Heartbeat::Out before = hb.Step(c, in);
    in.dt = 2.5f;
    const Heartbeat::Out after = hb.Step(c, in);
    in.dt = kDt;
    Expect(after.amount - before.amount <= 1.5f * kMaxStep / kFadeIn + 0.01f, "a 2.5 s frame moves the fade by at most %.3f (%.3f)", 1.5f * kMaxStep / kFadeIn, after.amount - before.amount);
    Expect(after.amount < 1.0f, "and does not finish it");
    // a hitch in steady state: the beat moves at most a tenth of a second's worth
    Run(hb, c, in, 3.0f);
    const Heartbeat::Out a = hb.Step(c, in);
    in.dt = 5.0f;
    const Heartbeat::Out b = hb.Step(c, in);
    float moved = b.phase - a.phase;
    if (moved < 0.0f) moved += 1.0f;
    Expect(moved <= kMaxStep * b.bpm / 60.0f + 0.01f, "a 5 s frame moves the phase by %.3f of a cycle (at most %.3f)", moved, kMaxStep * b.bpm / 60.0f);
    Expect(b.alpha >= 0.0f && b.alpha <= PeakAlpha(b.severity, c) + 1e-6f, "and the opacity stays in range");
    // odd times
    for (float dt : {-1.0f, 0.0f, NAN, INFINITY, -INFINITY, 1e30f}) {
        Heartbeat h;
        Heartbeat::In x = Seen(0.1f);
        x.dt = dt;
        const Heartbeat::Out o = h.Step(c, x);
        Expect(std::isfinite(o.alpha) && o.alpha >= 0.0f && o.alpha <= 1.0f && std::isfinite(o.phase), "dt %g: a sane output", dt);
    }
    for (float f : {NAN, INFINITY, -1.0f, 2.0f}) {
        Heartbeat h;
        Heartbeat::In x = Seen(f);
        x.dt = kDt;
        const Heartbeat::Out o = h.Step(c, x);
        Expect(std::isfinite(o.alpha) && o.alpha >= 0.0f && o.alpha <= 1.0f, "life fraction %g: a sane output", f);
    }
}

// ---- life going up and down --------------------------------------------------------------------------------------------
void TestRamps() {
    printf("life going up and down\n");
    Config c = On();
    Heartbeat hb;
    Heartbeat::In in = Seen(1.0f);
    in.dt = kDt;
    // down from full to 3% over 12 s, then up to 80% over 12 s, then a sharp drop to 10%
    float onAt = -1.0f, offAt = -1.0f, lastBpm = 0.0f;
    bool bpmUp = true, bpmDown = true, range = true;
    int transitions = 0;
    bool was = false;
    for (float t = 0.0f; t < 12.0f; t += kDt) {
        in.frac = 1.0f - 0.97f * (t / 12.0f);
        const Heartbeat::Out o = hb.Step(c, in);
        if (o.active != was) { ++transitions; was = o.active; if (o.active) onAt = in.frac; }
        if (o.active && o.amount > 0.999f) { bpmUp = bpmUp && o.bpm >= lastBpm - 1e-3f; lastBpm = o.bpm; }
        range = range && o.alpha >= 0.0f && o.alpha <= std::max(c.intensityStart, c.intensityEnd) + 1e-6f;
    }
    Expect(transitions == 1 && onAt < 0.30f && onAt > 0.29f, "on once, as the life crossed 30%% (at %.3f)", onAt);
    Expect(bpmUp && lastBpm > 85.0f, "falling: the rate only rises (to %.0f)", lastBpm);
    transitions = 0;
    lastBpm = 1000.0f;   // (healing starts a hair below where the fall ended)
    for (float t = 0.0f; t < 12.0f; t += kDt) {
        in.frac = 0.03f + 0.77f * (t / 12.0f);
        const Heartbeat::Out o = hb.Step(c, in);
        if (o.active != was) { ++transitions; was = o.active; if (!o.active) offAt = in.frac; }
        if (o.active && o.amount > 0.999f) { bpmDown = bpmDown && o.bpm <= lastBpm + 1e-3f; lastBpm = o.bpm; }
        range = range && o.alpha >= 0.0f && o.alpha <= std::max(c.intensityStart, c.intensityEnd) + 1e-6f;
    }
    Expect(transitions == 1 && offAt > 0.35f && offAt < 0.36f, "healing: off once, past the band (at %.3f)", offAt);
    Expect(bpmDown, "healing: the rate only falls");
    Expect(range, "the opacity never leaves 0 .. %.2f", std::max(c.intensityStart, c.intensityEnd));
    in.frac = 0.10f;
    Heartbeat::Out o = Run(hb, c, in, 2.0f);
    Expect(o.active && o.amount == 1.0f, "a sharp drop: on again");
    // the opacity changes smoothly from one frame to the next, at every rate, all the way
    Heartbeat h2;
    float prev = 0.0f, maxJump = 0.0f;
    in.frac = 0.01f;
    for (float t = 0.0f; t < 8.0f; t += kDt) {
        const Heartbeat::Out x = h2.Step(c, in);
        maxJump = std::max(maxJump, std::fabs(x.alpha - prev));
        prev = x.alpha;
    }
    const float peak = PeakAlpha(Severity(0.01f, c.threshold), c);
    Expect(maxJump <= 0.2f * peak, "no flash: the largest change in opacity between two frames at 90 Hz is %.3f (%.0f%% of its peak %.2f)", maxJump, 100.0f * maxJump / peak, peak);
    Config strong = c;
    strong.intensityStart = strong.intensityEnd = 1.0f;
    Heartbeat h3;
    prev = 0.0f; maxJump = 0.0f;
    for (float t = 0.0f; t < 8.0f; t += kDt) {
        const Heartbeat::Out x = h3.Step(strong, in);
        maxJump = std::max(maxJump, std::fabs(x.alpha - prev));
        prev = x.alpha;
    }
    Expect(maxJump <= 0.2f, "and at full intensity %.3f", maxJump);
}

// ---- beats, haptics --------------------------------------------------------------------------------------------------
void TestBeats() {
    printf("beats and haptics\n");
    Config c = On();
    const BeatShape shape = ShapeFor(false);
    Heartbeat hb;
    Heartbeat::In in = Seen(0.20f);
    in.dt = kDt;
    int lub = 0, dub = 0, order = 0, last = 0;   // last: 1 a lub, 2 a dub
    float bpm = 0.0f, worstLub = 0.5f, worstDub = 0.5f;
    for (float t = 0.0f; t < 20.0f; t += kDt) {
        const Heartbeat::Out o = hb.Step(c, in);
        bpm = o.bpm;
        if (t < 2.0f) continue;   // (let it settle)
        if (o.beat.lub) {
            ++lub;
            if (last == 1) ++order;
            last = 1;
            worstLub = std::max(worstLub, std::fabs(Beats01(o.phase, shape) - 0.5f) + 0.5f);   // how far from half way up the rise it landed
        }
        if (o.beat.dub) {
            ++dub;
            if (last == 2) ++order;
            last = 2;
            worstDub = std::max(worstDub, std::fabs(Beats01(o.phase, shape) / shape.dubPeak - 0.5f) + 0.5f);
        }
    }
    const float expected = bpm * 18.0f / 60.0f;
    Expect(std::fabs(lub - expected) <= 1.5f && std::fabs(dub - expected) <= 1.5f, "18 s at %.0f a minute: %d lubs and %d dubs (%.1f)", bpm, lub, dub, expected);
    Expect(order == 0, "a dub always follows a lub");
    Expect(worstLub <= 0.85f && worstDub <= 0.85f, "the pulse lands in the middle of each rise of the red (worst %.2f and %.2f of the way up)", worstLub, worstDub);

    // the amplitude by life: weak just under the threshold, the strongest at the lowest life, the same line as the opacity
    const Config d;
    Config big = d;
    big.hapticsStrength = 2.0f;
    Config quiet = d;
    quiet.hapticsStrength = 0.1f;
    Config gentle = d;
    gentle.gentle = true;
    bool rising = true, dubSofter = true, inRange = true, line = true;
    float prev = -1.0f;
    for (int i = 0; i <= 20; ++i) {
        const float sv = (float)i / 20.0f;
        const Haptic l = HapticFor(false, sv, d), b = HapticFor(true, sv, d);
        rising = rising && l.amplitude >= prev;
        prev = l.amplitude;
        dubSofter = dubSofter && Near(b.amplitude, l.amplitude * kDubSofter, 1e-6f);
        inRange = inRange && l.amplitude > 0.0f && l.amplitude <= kHapticMax && b.amplitude > 0.0f;
        line = line && Near(l.amplitude, kHapticOfAlpha * PeakAlpha(sv, d), 1e-6f);   // (the visual line, scaled)
    }
    Expect(rising && line, "the lower the life, the stronger the pulse, along the visual intensity's own line");
    Expect(dubSofter && inRange, "the dub is %.0f%% of the lub; never silent, never over %.1f", kDubSofter * 100.0f, kHapticMax);
    const Haptic weak = HapticFor(false, 0.0f, d), strong = HapticFor(false, 1.0f, d);
    Expect(Near(weak.amplitude, 0.55f * 0.25f, 1e-5f) && Near(strong.amplitude, 0.55f * 0.85f, 1e-5f) && strong.amplitude > 3.0f * weak.amplitude,
           "just under the threshold %.3f, at no life %.3f (a third of the span or more)", weak.amplitude, strong.amplitude);
    Expect(weak.amplitude > 0.04f, "and even the weakest is above what the runtime's motors drop (0.04)");
    Expect(Near(HapticFor(false, 0.5f, big).amplitude, 2.0f * HapticFor(false, 0.5f, d).amplitude, 1e-5f) &&
               Near(HapticFor(false, 0.5f, quiet).amplitude, 0.1f * HapticFor(false, 0.5f, d).amplitude, 1e-5f),
           "the strength slider scales it (x2, x0.1)");
    Config loud = d;
    loud.intensityStart = loud.intensityEnd = 1.0f;
    loud.hapticsStrength = 2.0f;
    Expect(HapticFor(false, 1.0f, loud).amplitude == kHapticMax, "never over %.1f whatever the sliders say", kHapticMax);
    Expect(Near(HapticFor(false, 0.7f, gentle).amplitude, kGentleAlpha * HapticFor(false, 0.7f, d).amplitude, 1e-6f), "gentle: quieter");
    Expect(HapticFor(false, 0.5f, d).ms == 40 && HapticFor(true, 0.5f, d).ms == 28, "short: 40 and 28 ms");
    Expect(HapticFor(false, NAN, d).amplitude >= 0.0f && std::isfinite(HapticFor(false, NAN, d).amplitude), "a life that is not a number gives a number");
    // the amplitude the pulses take as the life falls through a state machine
    Heartbeat h2;
    float a1 = 0.0f, a2 = 0.0f;
    for (float frac : {0.28f, 0.03f}) {
        Heartbeat::In x = Seen(frac);
        x.dt = kDt;
        Run(h2, c, x, 2.5f);
        for (float t = 0.0f; t < 2.0f; t += kDt) {
            const Heartbeat::Out o = h2.Step(c, x);
            if (o.beat.lub) (frac > 0.1f ? a1 : a2) = HapticFor(false, o.severity, c).amplitude;
        }
    }
    Expect(a1 > 0.0f && a2 > 1.8f * a1, "the pulses at 3%% life are stronger than at 28%% (%.3f against %.3f)", a2, a1);
}

// ---- off --------------------------------------------------------------------------------------------------------------
void TestOff() {
    printf("off\n");
    Config off;   // the default is off
    Expect(!off.on, "the default is off");
    Heartbeat hb;
    bool nothing = true;
    for (float f : {1.0f, 0.5f, 0.29f, 0.05f, 0.0001f}) {
        Heartbeat::In in = Seen(f);
        in.dt = kDt;
        for (int i = 0; i < 200; ++i) {
            const Heartbeat::Out o = hb.Step(off, in);
            nothing = nothing && o.alpha == 0.0f && o.amount == 0.0f && !o.active && !o.beat.lub && !o.beat.dub && o.bpm == 0.0f;
        }
    }
    Expect(nothing && !hb.Active(), "the setting off: nothing, whatever the life");
    // switched off while it shows: gone in the very next step
    Config c = On();
    Run(hb, c, Seen(0.05f), 3.0f);
    Expect(hb.Active(), "setup: showing");
    Heartbeat::In in = Seen(0.05f);
    in.dt = kDt;
    const Heartbeat::Out o = hb.Step(off, in);
    Expect(o.alpha == 0.0f && !hb.Active(), "unticked: nothing at once");
    Uniforms u = MakeUniforms(0.0f, c);
    Expect(u.alpha == 0.0f, "no opacity, no uniforms to send");
    // alpha 0 leaves a picture bit for bit, with and without a field of view
    uint32_t seed = 12345;
    auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return (float)(seed >> 8) / 16777216.0f; };
    bool same = true;
    float color[3];
    ColorOf(0.8f, color);
    const Shape flat = ShapeOf(Config()), wide = ShapeOf(Config(), 1.19f, 1.19f);
    for (int i = 0; i < 20000; ++i) {
        const float px[3] = {rnd(), rnd(), rnd()};
        float out[3];
        Apply(px, rnd(), rnd(), 0.0f, i % 2 ? flat : wide, color, out);
        same = same && memcmp(px, out, sizeof px) == 0;
    }
    Expect(same, "opacity 0: every colour comes out as it went in, bit for bit");
}

// ---- the vignette's shape -----------------------------------------------------------------------------------------------
float ClearShare(const Shape& s) {
    int clear = 0, total = 0;
    for (int j = 0; j <= 48; ++j)
        for (int i = 0; i <= 48; ++i, ++total) clear += Mask(i / 48.0f, j / 48.0f, s) == 0.0f ? 1 : 0;
    return (float)clear / total;
}

void TestMask() {
    printf("the vignette's shape\n");
    const Config d;
    Shape s = ShapeOf(d);
    const float outer = 0.6f + 0.5f * (kShortestRamp + (1.0f - kShortestRamp) * 0.8f);
    Expect(Near(s.inner, 0.60f, 1e-6f) && Near(s.outer, outer, 1e-6f) && s.roundness == 2.5f && s.tanX == 0.0f, "the default shape: from %.2f to %.3f of the view's angle", s.inner, s.outer);
    Expect(Mask(0.5f, 0.5f, s) == 0.0f, "the centre is clear");
    bool clear = true, symmetric = true, bounded = true;
    for (int j = 0; j <= 64; ++j)
        for (int i = 0; i <= 64; ++i) {
            const float u = i / 64.0f, v = j / 64.0f, m = Mask(u, v, s);
            if (Radius(u, v, s.roundness) <= s.inner) clear = clear && m == 0.0f;
            symmetric = symmetric && Near(m, Mask(1.0f - u, v, s), 1e-6f) && Near(m, Mask(u, 1.0f - v, s), 1e-6f) && Near(m, Mask(v, u, s), 1e-6f);
            bounded = bounded && m >= 0.0f && m <= 1.0f;
        }
    Expect(clear, "nothing inside the inner radius");
    Expect(symmetric, "the same in all four quadrants and turned over");
    Expect(bounded, "0 to 1 everywhere");
    Expect(ClearShare(s) > 0.25f, "%.0f%% of the picture is untouched", 100.0f * ClearShare(s));
    float prev = 0.0f;
    bool mono = true;
    for (int i = 0; i <= 100; ++i) {
        const float m = Mask(0.5f + 0.5f * i / 100.0f, 0.5f, s);
        mono = mono && m >= prev;
        prev = m;
    }
    Expect(mono, "from the middle out to an edge it only rises");
    Expect(Mask(1.0f, 0.5f, s) > 0.9f && Mask(0.5f, 0.0f, s) > 0.9f, "an edge's middle is %.2f tinted", Mask(1.0f, 0.5f, s));
    Expect(Mask(1.0f, 1.0f, s) == 1.0f && Mask(0.0f, 0.0f, s) == 1.0f, "a corner is wholly tinted");

    // the radius slider: a thin rim at 20%, a deep vignette at 95%, the clear middle only shrinking as it grows
    float lastClear = 2.0f, lastTint = -1.0f;
    bool shrinks = true, grows = true, edgeAlways = true;
    for (int pct = 20; pct <= 95; pct += 5) {
        Config c = d;
        c.radius = RadiusFromPercent((float)pct);
        const Shape r = ShapeOf(c);
        const float cs = ClearShare(r), tint = Mask(0.875f, 0.5f, r);
        shrinks = shrinks && cs <= lastClear + 1e-6f;
        grows = grows && tint >= lastTint - 1e-6f;
        edgeAlways = edgeAlways && Mask(1.0f, 0.5f, r) > 0.5f;
        lastClear = cs;
        lastTint = tint;
    }
    Expect(shrinks, "a larger vignette leaves a smaller clear middle, every step from 20 to 95");
    Expect(grows, "and tints a point three quarters out more and more");
    Expect(edgeAlways, "the rim is tinted at every size");
    Config thin = d, deep = d;
    thin.radius = 0.20f;
    deep.radius = 0.95f;
    const Shape st = ShapeOf(thin), sd = ShapeOf(deep);
    Expect(Mask(0.75f, 0.5f, st) == 0.0f && Mask(0.95f, 0.5f, st) > 0.3f && ClearShare(st) > 0.45f, "20%%: only a thin rim (%.0f%% of the picture clear)", 100.0f * ClearShare(st));
    Expect(Mask(0.60f, 0.5f, sd) > 0.0f && ClearShare(sd) < 0.02f, "95%%: a deep vignette (%.1f%% clear)", 100.0f * ClearShare(sd));
    Expect(Near(st.inner, 0.80f, 1e-6f) && Near(sd.inner, 0.05f, 1e-6f), "the clear middle is 100%% minus the radius of the view's angle");

    // softness: 1 a fade all the way, 0 a short ramp then flat
    Config hard = d, soft = d;
    hard.softness = 0.0f;
    soft.softness = 1.0f;
    const Shape sh = ShapeOf(hard), ss = ShapeOf(soft);
    Expect(sh.outer < ss.outer && Near(sh.inner, ss.inner, 1e-6f), "softness only moves where the colour is complete (%.3f against %.3f)", sh.outer, ss.outer);
    const float mid = 0.5f * (sh.outer + ss.inner);
    Expect(Smoothstep(sh.inner, sh.outer, sh.outer) == 1.0f && Smoothstep(ss.inner, ss.outer, sh.outer) < 1.0f && mid > 0.0f, "hard: complete sooner");
    Expect(Near(ss.outer, 1.0f - 0.40f + 0.50f, 1e-6f), "softness 1: complete a little past the edge (%.2f)", ss.outer);
    bool outerMono = true;
    float lastOuter = -1.0f;
    for (int i = 0; i <= 10; ++i) {
        Config c = d;
        c.softness = i / 10.0f;
        outerMono = outerMono && ShapeOf(c).outer >= lastOuter;
        lastOuter = ShapeOf(c).outer;
    }
    Expect(outerMono, "softer is longer, every step");

    // the shape: an oval to a rounded square - the clear middle reaches further toward the corners
    Config oval = d, square = d;
    oval.roundness = 2.0f;
    square.roundness = 6.0f;
    Expect(Radius(1.0f, 1.0f, 2.0f) > Radius(1.0f, 1.0f, 6.0f) && Near(Radius(1.0f, 0.5f, 2.5f), 1.0f, 1e-6f), "the rings: rounder rectangles toward the corners (%.3f and %.3f)", Radius(1.0f, 1.0f, 2.0f), Radius(1.0f, 1.0f, 6.0f));
    Expect(ClearShare(ShapeOf(square)) > ClearShare(ShapeOf(oval)) + 0.05f, "a rounded square leaves more clear than an oval (%.0f%% against %.0f%%)", 100.0f * ClearShare(ShapeOf(square)), 100.0f * ClearShare(ShapeOf(oval)));
    Expect(Mask(0.85f, 0.85f, ShapeOf(oval)) > Mask(0.85f, 0.85f, ShapeOf(square)), "and tints a point toward the corner less");

    // the colour: dark grey to the deep red
    float c0[3], c1[3], c8[3], cNaN[3];
    ColorOf(0.0f, c0);
    ColorOf(1.0f, c1);
    ColorOf(0.8f, c8);
    ColorOf(NAN, cNaN);
    Expect(memcmp(c0, kGrey, sizeof c0) == 0 && memcmp(c1, kRed, sizeof c1) == 0, "saturation 0 is the grey, 1 the deep red");
    Expect(Near(c0[0], c0[1], 1e-6f) && Near(c0[1], c0[2], 1e-6f), "the grey is grey");
    Expect(c1[0] > c1[1] + 0.2f && c1[1] == c1[2], "the red is red");
    bool redder = true;
    float lastSat = -1.0f;
    for (int i = 0; i <= 20; ++i) {
        float c[3];
        ColorOf(i / 20.0f, c);
        const float hi = std::max(c[0], std::max(c[1], c[2])), lo = std::min(c[0], std::min(c[1], c[2])), sat = hi > 0.0f ? (hi - lo) / hi : 0.0f;
        redder = redder && sat >= lastSat - 1e-6f && c[0] >= c[1];
        lastSat = sat;
    }
    Expect(redder, "the colour only gets redder with the slider");
    Expect(memcmp(cNaN, c8, sizeof c8) == 0, "not a number: the default saturation");
    for (int i = 0; i < 3; ++i) Expect(c8[i] > std::min(c0[i], c1[i]) - 1e-6f && c8[i] < std::max(c0[i], c1[i]) + 1e-6f, "0.8 is between the two (%d)", i);

    // the colour over the picture
    const float grey[3] = {0.4f, 0.4f, 0.4f};
    float out[3];
    Apply(grey, 0.5f, 0.5f, 0.85f, s, c8, out);
    Expect(memcmp(grey, out, sizeof grey) == 0, "the middle keeps the picture's colour at full opacity");
    Apply(grey, 1.0f, 1.0f, 0.5f, s, c8, out);
    Expect(Near(out[0], 0.4f + (c8[0] - 0.4f) * 0.5f, 1e-6f) && Near(out[1], 0.4f + (c8[1] - 0.4f) * 0.5f, 1e-6f), "a corner at half opacity is halfway to the colour (%.3f %.3f %.3f)", out[0], out[1], out[2]);
    Apply(grey, 1.0f, 1.0f, 1.0f, s, c8, out);
    Expect(Near(out[0], c8[0], 1e-6f) && Near(out[1], c8[1], 1e-6f) && Near(out[2], c8[2], 1e-6f), "a corner at full opacity is the colour");
    Apply(grey, 1.0f, 1.0f, NAN, s, c8, out);
    Expect(memcmp(grey, out, sizeof grey) == 0, "an opacity that is not a number changes nothing");

    // what the effect is handed
    const Uniforms u = MakeUniforms(0.4f, Config());
    float want[3];
    ColorOf(0.8f, want);
    Expect(u.alpha == 0.4f && Near(u.shape[0], 0.60f, 1e-6f) && Near(u.shape[1], outer, 1e-6f) && Near(u.shape[2], 2.5f, 1e-6f) && memcmp(u.color, want, sizeof want) == 0 &&
               u.fov[0] == 0.0f && u.fov[1] == 0.0f,
           "uniforms: inner %.2f outer %.3f roundness %.1f, no field of view", u.shape[0], u.shape[1], u.shape[2]);
    Expect(MakeUniforms(7.0f, Config()).alpha == 1.0f && MakeUniforms(NAN, Config()).alpha == 0.0f && MakeUniforms(-1.0f, Config()).alpha == 0.0f, "uniform opacity is clamped, not a number is 0");
    const Uniforms uf = MakeUniforms(0.4f, Config(), 1.19f, 1.4f);
    Expect(uf.fov[0] == 1.19f && uf.fov[1] == 1.4f, "the field of view goes along");
    const Uniforms ub = MakeUniforms(0.4f, Config(), NAN, 1.0f), uz = MakeUniforms(0.4f, Config(), 1.0f, 0.0f);
    Expect(ub.fov[0] == 0.0f && ub.fov[1] == 0.0f && uz.fov[0] == 0.0f && uz.fov[1] == 0.0f, "a field of view that is half missing or not a number is none");
    const Uniforms def;
    Expect(def.alpha == 0.0f && def.shape[0] == 0.5f && def.shape[1] == 1.1f && def.shape[2] == 2.5f && def.color[0] == 0.30f && def.color[1] == 0.005f && def.fov[0] == 0.0f,
           "the effect's own defaults are what the header says they are");
}

// ---- the view's angle ----------------------------------------------------------------------------------------------------
void TestFov() {
    printf("the view's angle\n");
    constexpr float kPi = 3.14159265f;
    // the half-extent from the runtime's field of view, two eyes
    Expect(Near(HalfTan(-1.0f, 1.0f, -1.0f, 1.0f), 1.0f, 1e-6f), "a symmetric view of 90 degrees: tangent 1");
    Expect(Near(HalfTan(-1.2f, 0.8f, -1.1f, 0.9f), 1.0f, 1e-6f), "an asymmetric view: half its extent, the eyes' mean");
    Expect(HalfTan(NAN, 1.0f, -1.0f, 1.0f) == 0.0f && HalfTan(0.0f, 0.01f, 0.0f, 0.01f) == 0.0f && HalfTan(-50.0f, 50.0f, -50.0f, 50.0f) == 0.0f && HalfTan(1.0f, -1.0f, 1.0f, -1.0f) == 0.0f,
           "not numbers, no view at all, absurdly wide, or upside down: 0 (not known)");
    const float t100 = std::tan(50.0f * kPi / 180.0f);
    // an edge's middle is 1, the centre 0, whatever the field of view
    Expect(Near(Radius(1.0f, 0.5f, 2.5f, t100, t100), 1.0f, 1e-5f) && Near(Radius(0.0f, 0.5f, 2.5f, t100, t100), 1.0f, 1e-5f) && Radius(0.5f, 0.5f, 2.5f, t100, t100) == 0.0f,
           "with a field of view: an edge's middle is 1, the centre 0");
    Expect(Near(Radius(0.5f, 1.0f, 2.5f, 2.0f, 1.4f), 1.0f, 1e-5f) && Near(Radius(1.0f, 0.5f, 2.5f, 2.0f, 1.4f), 1.0f, 1e-5f), "each axis its own tangent: both edges' middles are 1");
    Expect(Near(Radius(0.75f, 0.5f, 2.5f), 0.5f, 1e-6f), "without a field of view: the picture's own uv (%.3f)", Radius(0.75f, 0.5f, 2.5f));
    Expect(Radius(0.75f, 0.5f, 2.5f, 1.0f, 0.0f) == Radius(0.75f, 0.5f, 2.5f), "with half a field of view: the picture's own uv");
    // half way out in ANGLE: for 100 degrees that is 25 degrees, tan 25 / tan 50 across the picture
    const float uvAt25 = std::tan(25.0f * kPi / 180.0f) / t100;
    Expect(Near(Radius(0.5f + 0.5f * uvAt25, 0.5f, 2.5f, t100, t100), 0.5f, 1e-4f), "half the angle of a 100 degree view is %.1f%% of the way across the picture", 100.0f * uvAt25);

    // the same slider is the same angle on any headset: where the tint begins, along the horizontal, as a share of the half-angle
    const float fovs[] = {80.0f, 100.0f, 130.0f};
    float startUv[3] = {};
    for (int k = 0; k < 3; ++k) {
        const float t = std::tan(0.5f * fovs[k] * kPi / 180.0f);
        const Shape s = ShapeOf(Config(), t, t);
        float frac = -1.0f;
        for (int i = 0; i <= 20000; ++i) {
            const float u = 0.5f + 0.5f * i / 20000.0f;
            if (Mask(u, 0.5f, s) > 1e-4f) {
                frac = std::atan((2.0f * u - 1.0f) * t) / std::atan(t);
                startUv[k] = 2.0f * u - 1.0f;
                break;
            }
        }
        Expect(Near(frac, s.inner, 0.01f), "a %.0f degree view: the tint begins at %.3f of its half-angle (%.1f degrees off the middle)", fovs[k], frac, frac * 0.5f * fovs[k]);
    }
    Expect(startUv[0] > startUv[1] && startUv[1] > startUv[2], "on the picture the clear middle is smaller on a wider headset (%.2f, %.2f, %.2f of the half-width)", startUv[0], startUv[1], startUv[2]);
    Expect(Near(startUv[1], 0.5f, 0.03f), "on a 100 degree view the first build's look is kept: the clear middle is %.2f of the half-width (it was 0.50)", startUv[1]);

    // the field of view reaches the effect
    const Uniforms u = MakeUniforms(0.5f, Config(), t100, 0.9f * t100);
    const Shape s = ShapeOf(Config(), t100, 0.9f * t100);
    Expect(u.fov[0] == t100 && u.fov[1] == 0.9f * t100 && s.tanX == t100 && s.tanY == 0.9f * t100, "uniforms carry the field of view");
}

// ---- the preview in the headset's settings panel ----------------------------------------------------------------------------
void TestPreview() {
    printf("the preview while the settings panel is on the vignette's pages\n");
    Expect(Near(PreviewFrac(0.30f), 0.15f, 1e-6f) && kPreviewSeverity == 0.5f, "half way down from the threshold to nothing");
    bool all = true;
    for (int pct = 10; pct <= 60; pct += 5) {
        const float thr = ThresholdFromPercent((float)pct), f = PreviewFrac(thr);
        all = all && f > 0.0f && f < thr && Near(Severity(f, thr), kPreviewSeverity, 1e-5f);
    }
    Expect(all, "for every threshold the setting allows: under it, above nothing, at half severity");
    const Config c = On();
    Heartbeat hb;
    Heartbeat::In in = Seen(PreviewFrac(c.threshold));
    in.dt = kDt;
    const Heartbeat::Out o = Run(hb, c, in, 2.0f);
    Expect(o.active && o.amount == 1.0f && Near(o.severity, 0.5f, 1e-5f) && Near(o.bpm, Bpm(0.5f, false), 1e-3f), "the heartbeat runs at half severity: %.1f a minute", o.bpm);
    Expect(Near(PeakAlpha(kPreviewSeverity, c), 0.5f * (c.intensityStart + c.intensityEnd), 1e-6f), "and its intensity is half way between the two ends (%.2f)", PeakAlpha(kPreviewSeverity, c));
    // outside the vignette's own view (F4 on native, in a game) a preview shows nothing
    Heartbeat other;
    Heartbeat::In away = in;
    away.inView = false;
    const Heartbeat::Out none = Run(other, c, away, 2.0f);
    Expect(none.alpha == 0.0f && !none.active, "in any other view the preview is nothing");
    // and the setting off: nothing either
    Heartbeat offHb;
    Config off = c;
    off.on = false;
    Expect(Run(offHb, off, in, 2.0f).alpha == 0.0f, "unticked: no preview");
}

// ---- the "Preview 5 s" button -------------------------------------------------------------------------------------------------
void TestPreviewWindow() {
    printf("the Preview 5 s button\n");
    PreviewWindow w;
    Expect(!w.Active(0) && !w.Showing(0) && !w.Active(1000000) && !w.Showing(1000000), "opening a page starts nothing: no preview, no red, until the button is pressed");
    w.Start(1000);
    Expect(w.Active(1000) && w.Active(5999) && !w.Active(6000) && !w.Active(9000) && kPreviewMs == 5000, "five seconds, then it stops by itself");
    Expect(w.Showing(1000) && w.Showing(6000) && w.Showing(6000 + kPreviewTailMs - 1) && !w.Showing(6000 + kPreviewTailMs), "the red fades out for a moment after them, then nothing is shown");
    w.Start(3000);
    Expect(w.Active(7999) && !w.Active(8000), "a second press starts the five seconds again");
    w.Stop();
    Expect(!w.Active(3500) && !w.Showing(3500) && !w.Showing(9000), "leaving the page (or closing the panel) stops it at once, the red with it");

    // the rows as the preview plays them: the vignette on, the hands on, whatever the rows' ticks say; every other row as it stands
    Config draft = On();
    draft.on = false;
    draft.haptics = false;
    draft.radius = 0.7f;
    draft.intensityEnd = 0.6f;
    draft.hapticsStrength = 1.5f;
    const Config p = PreviewConfig(draft);
    Expect(p.on && p.haptics && Near(p.radius, 0.7f, 1e-6f) && Near(p.intensityEnd, 0.6f, 1e-6f) && Near(p.hapticsStrength, 1.5f, 1e-6f) && Near(p.threshold, draft.threshold, 1e-6f),
           "the preview shows the vignette and plays the beat even with both ticks off, with the other rows as they stand");
    Config wild = draft;
    wild.radius = 7.0f;
    wild.hapticsStrength = NAN;
    Expect(Near(PreviewConfig(wild).radius, kRadiusMax, 1e-6f) && Near(PreviewConfig(wild).hapticsStrength, Config().hapticsStrength, 1e-6f), "and sanitised like any other");

    // who may pulse: in play the tick, as ever; the red's fade-out after a preview is quiet
    Expect(PulseAllowed(true, false) && !PulseAllowed(false, false), "in play the tick decides");
    Expect(!PulseAllowed(true, true) && !PulseAllowed(false, true), "after a preview, in the fade-out, the hands are quiet");

    // five seconds of the beat at the preview's severity: about a minute-rate heartbeat's pulses, then none
    Config c = PreviewConfig(On());
    Heartbeat hb;
    Heartbeat::In in = Seen(PreviewFrac(c.threshold));
    in.dt = kDt;
    PreviewWindow test;
    test.Start(0);
    int pulses = 0, lub = 0, dub = 0, loud = 0;
    float firstAt = -1.0f, lastAt = 0.0f;
    for (int i = 0;; ++i) {
        const uint64_t nowMs = (uint64_t)(i * 1000.0 * kDt);
        if (!test.Active(nowMs)) break;
        const Heartbeat::Out o = hb.Step(c, in);
        const int n = (o.beat.lub ? 1 : 0) + (o.beat.dub ? 1 : 0);
        if (n && PulseAllowed(c.haptics && o.amount >= kHapticFrom, false)) {
            pulses += n;
            lub += o.beat.lub ? 1 : 0;
            dub += o.beat.dub ? 1 : 0;
            if (HapticFor(!o.beat.lub, o.severity, c).amplitude >= haptics::kMinAmplitude) ++loud;
            if (firstAt < 0.0f) firstAt = (float)nowMs;
            lastAt = (float)nowMs;
        }
    }
    const float expected = 2.0f * Bpm(kPreviewSeverity, false) * 5.0f / 60.0f;
    Expect(std::fabs(pulses - expected) <= 3.0f && std::abs(lub - dub) <= 1, "5 s at %.0f a minute: %d pulses (%d lub, %d dub) - about %.1f (the first beat waits for the red to come up)",
           Bpm(kPreviewSeverity, false), pulses, lub, dub, expected);
    Expect(loud == pulses, "every one of them is felt (over the weakest the motor shows)");
    Expect(firstAt >= 0.0f && firstAt < 1500.0f && lastAt <= 5000.0f, "the first pulse comes with the red (%.0f ms), the last before the five seconds are over (%.0f ms)", firstAt, lastAt);
    // the strength the preview plays is the panel's own row
    Config strong = c, weak = c;
    strong.hapticsStrength = 2.0f;
    weak.hapticsStrength = 0.5f;
    Expect(HapticFor(false, kPreviewSeverity, strong).amplitude > HapticFor(false, kPreviewSeverity, weak).amplitude, "the pulse of the preview follows the strength row");
}

// ---- the heartbeat in the hands of a weak motor (the PlayStation VR2 Sense under SteamVR) ---------------------------------------------
void TestWeakMotor() {
    printf("the heartbeat on a weak motor\n");
    using haptics::ForMotor;
    using haptics::Motor;
    // the player's own settings (his ini: threshold 50, intensity 0.55 .. 0.8, strength 1): the life at 25% is half way down
    Config c = On();
    c.intensityStart = 0.55f;
    c.intensityEnd = 0.8f;
    c.hapticsStrength = 1.0f;
    const Haptic lub = HapticFor(false, kPreviewSeverity, c), dub = HapticFor(true, kPreviewSeverity, c);
    Expect(Near(lub.amplitude, 0.55f * PeakAlpha(kPreviewSeverity, c), 1e-5f) && lub.ms == 40 && dub.ms == 28, "the heartbeat as made: lub %.2f for %d ms, dub %.2f for %d ms", lub.amplitude, lub.ms, dub.amplitude, dub.ms);
    const haptics::Request pl = ForMotor(Motor::Plain, lub.amplitude, (uint32_t)lub.ms), pd = ForMotor(Motor::Plain, dub.amplitude, (uint32_t)dub.ms);
    Expect(pl.amplitude == lub.amplitude && pl.durationMs == 40 && pd.amplitude == dub.amplitude && pd.durationMs == 28, "on a plain motor (a Quest) it goes through exactly as made");
    const haptics::Request wl = ForMotor(Motor::Weak, lub.amplitude, (uint32_t)lub.ms), wd = ForMotor(Motor::Weak, dub.amplitude, (uint32_t)dub.ms);
    Expect(wl.amplitude > lub.amplitude && wd.amplitude > dub.amplitude && wl.amplitude > wd.amplitude, "on a weak one it is stronger, and the lub is still the stronger beat (%.2f, %.2f)", wl.amplitude, wd.amplitude);
    Expect(wl.durationMs == 45 && wd.durationMs == 45 && wl.amplitude >= 0.55f && wd.amplitude >= 0.55f && wl.amplitude <= 1.0f, "and as long as the hit that the motor shows (45 ms), at least 0.55");

    // across the whole range of the settings: nothing below the floor, the order kept, the lower the life the stronger
    bool floorOk = true, orderOk = true, lifeOk = true, plainOk = true;
    for (float strength : {0.1f, 0.5f, 1.0f, 2.0f})
        for (float start : {0.05f, 0.25f, 0.55f}) {
            Config k = c;
            k.hapticsStrength = strength;
            k.intensityStart = start;
            k.intensityEnd = std::max(start, 0.85f);
            float prevLub = 0.0f;
            for (int s = 0; s <= 10; ++s) {
                const float sev = s / 10.0f;
                const Haptic l = HapticFor(false, sev, k), d = HapticFor(true, sev, k);
                const haptics::Request wL = ForMotor(Motor::Weak, l.amplitude, (uint32_t)l.ms), wD = ForMotor(Motor::Weak, d.amplitude, (uint32_t)d.ms);
                if (l.amplitude >= haptics::kMinAmplitude) floorOk = floorOk && wL.amplitude >= haptics::kWeakFloorAmp - 1e-5f && wL.durationMs >= haptics::kWeakFloorMs;
                if (d.amplitude >= haptics::kMinAmplitude) floorOk = floorOk && wD.amplitude >= haptics::kWeakFloorAmp - 1e-5f && wD.durationMs >= haptics::kWeakFloorMs;
                orderOk = orderOk && wL.amplitude >= wD.amplitude - 1e-6f;
                lifeOk = lifeOk && wL.amplitude >= prevLub - 1e-6f;
                prevLub = wL.amplitude;
                plainOk = plainOk && ForMotor(Motor::Plain, l.amplitude, (uint32_t)l.ms).amplitude == (l.amplitude >= haptics::kMinAmplitude ? std::min(l.amplitude, 1.0f) : 0.0f);
            }
        }
    Expect(floorOk, "every pulse the settings can make that is felt at all is lifted to at least the floor (%.2f, %u ms)", haptics::kWeakFloorAmp, haptics::kWeakFloorMs);
    Expect(orderOk && lifeOk, "the lub is never weaker than the dub, and the lower the life the stronger it stays");
    Expect(plainOk, "and a plain motor never gets anything but what was made");
}

// ---- the effect's own HLSL ----------------------------------------------------------------------------------------------
std::string ReadAll(const std::string& path) {
    FILE* f = nullptr;
    std::string s;
    if (fopen_s(&f, path.c_str(), "rb") || !f) return s;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    fclose(f);
    return s;
}

struct GpuItem {
    float u, v, r, g, b;
};

void TestHlsl(const std::string& fxPath) {
    printf("the effect's HLSL (%s)\n", fxPath.c_str());
    const std::string fx = ReadAll(fxPath);
    if (fx.empty()) { Expect(false, "could not read %s", fxPath.c_str()); return; }
    const size_t b = fx.find("// LOWHP-BEGIN"), e = fx.find("// LOWHP-END");
    Expect(b != std::string::npos && e != std::string::npos && b < e, "the LOWHP marks are in the effect");
    if (b == std::string::npos || e == std::string::npos || b > e) return;
    const std::string body = fx.substr(fx.find('\n', b) + 1, e - fx.find('\n', b) - 1);

    // the effect's own defaults (before vrcam hands it any) are the header's Uniforms
    Expect(fx.find("float4(0.50, 1.10, 2.5, 0.0)") != std::string::npos, "the effect's default shape is float4(0.50, 1.10, 2.5, 0.0)");
    Expect(fx.find("float3(0.30, 0.005, 0.005)") != std::string::npos, "the effect's default colour is float3(0.30, 0.005, 0.005)");
    Expect(fx.find("uniform float2 LowHpFov < hidden = true; > = float2(0.0, 0.0);") != std::string::npos, "the effect's default field of view is none");
    Expect(fx.find("uniform float LowHpAlpha < hidden = true; > = 0.0;") != std::string::npos, "the effect's opacity defaults to 0 (off)");
    Expect(fx.find("LowHp(Phantom(pos, uv, 0, false), uv)") != std::string::npos && fx.find("LowHp(Phantom(pos, uv, 1, false), uv)") != std::string::npos,
           "both eyes' passes run it");

    const std::string src =
        "cbuffer Uniforms : register(b0) { float LowHpAlpha; float3 pad0; float4 LowHpShape; float3 LowHpColor; float pad1; float2 LowHpFov; float2 pad2; };\n"
        "StructuredBuffer<float4> In : register(t0);\n"
        "RWStructuredBuffer<float4> Out : register(u0);\n" +
        body +
        "[numthreads(64, 1, 1)] void main(uint3 id : SV_DispatchThreadID) {\n"
        "    const float4 a = In[2 * id.x], c = In[2 * id.x + 1];\n"
        "    Out[id.x] = float4(LowHp(c.rgb, a.xy), 1.0);\n"
        "}\n";
    ComPtr<ID3DBlob> blob, errors;
    const HRESULT hr = D3DCompile(src.data(), src.size(), "LowHp", nullptr, nullptr, "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL1, 0, &blob, &errors);
    Expect(SUCCEEDED(hr), "the effect's LowHp compiles%s%s", errors ? ": " : "", errors ? (const char*)errors->GetBufferPointer() : "");
    if (FAILED(hr)) return;

    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, nullptr, &ctx))) {
        Expect(false, "no WARP device");
        return;
    }
    ComPtr<ID3D11ComputeShader> cs;
    if (FAILED(dev->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &cs))) { Expect(false, "the shader could not be made"); return; }

    // a grid of picture points and colours, a few opacities, shapes, colours and fields of view
    std::vector<GpuItem> items;
    uint32_t seed = 777;
    auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return (float)(seed >> 8) / 16777216.0f; };
    for (int j = 0; j <= 48; ++j)
        for (int i = 0; i <= 48; ++i) items.push_back({i / 48.0f, j / 48.0f, rnd(), rnd(), rnd()});
    const UINT n = (UINT)items.size();
    struct Case { float alpha; float radius, softness, round, saturation, tanX, tanY; };
    const Case cases[] = {
        {0.0f, 0.40f, 0.8f, 2.5f, 0.8f, 0.0f, 0.0f},      // off, no field of view
        {0.0f, 0.40f, 0.8f, 2.5f, 0.8f, 1.19f, 1.19f},    // off, with one: still bit for bit
        {0.2f, 0.40f, 0.8f, 2.5f, 0.8f, 0.0f, 0.0f},      // the picture's own proportions
        {0.5f, 0.40f, 0.8f, 2.5f, 0.8f, 1.19f, 1.19f},    // a 100 degree view
        {0.85f, 0.40f, 0.8f, 2.5f, 0.8f, 1.19f, 1.19f},
        {1.0f, 0.40f, 0.8f, 2.5f, 1.0f, 2.14f, 2.14f},    // 130 degrees
        {0.6f, 0.20f, 0.0f, 2.0f, 0.0f, 1.0f, 1.0f},      // a thin hard rim, an oval, grey
        {0.6f, 0.95f, 1.0f, 6.0f, 0.4f, 1.6f, 0.9f},      // a deep soft one, a rounded square, a wide view
        {0.7f, 0.50f, 0.5f, 4.0f, 0.8f, 0.84f, 0.84f},    // 80 degrees
        {0.7f, 0.30f, 0.2f, 3.0f, 0.5f, 0.0f, 0.0f},
        {1.7f, 0.40f, 0.8f, 2.5f, 0.8f, 1.19f, 1.19f},    // an opacity over 1 (the effect saturates it)
    };
    float worst = 0.0f;
    bool exactOff = true;
    for (const Case& k : cases) {
        Config cfg;
        cfg.radius = k.radius; cfg.softness = k.softness; cfg.roundness = k.round; cfg.saturation = k.saturation;
        const Uniforms uni = MakeUniforms(1.0f, cfg, k.tanX, k.tanY);
        struct Cb { float alpha, pad0[3], shape[4], color[3], pad1, fov[2], pad2[2]; } cb = {k.alpha, {}, {uni.shape[0], uni.shape[1], uni.shape[2], 0.0f},
                                                                                            {uni.color[0], uni.color[1], uni.color[2]}, 0.0f, {uni.fov[0], uni.fov[1]}, {}};
        std::vector<float> input;
        for (const GpuItem& it : items) { const float x[8] = {it.u, it.v, 0, 0, it.r, it.g, it.b, 0}; input.insert(input.end(), x, x + 8); }
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = sizeof cb; bd.Usage = D3D11_USAGE_IMMUTABLE; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA sd{&cb, 0, 0};
        ComPtr<ID3D11Buffer> cbuf;
        if (FAILED(dev->CreateBuffer(&bd, &sd, &cbuf))) { Expect(false, "constant buffer"); return; }
        bd = {};
        bd.ByteWidth = (UINT)(input.size() * sizeof(float)); bd.Usage = D3D11_USAGE_IMMUTABLE; bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; bd.StructureByteStride = 16;
        sd = {input.data(), 0, 0};
        ComPtr<ID3D11Buffer> inBuf;
        if (FAILED(dev->CreateBuffer(&bd, &sd, &inBuf))) { Expect(false, "input buffer"); return; }
        ComPtr<ID3D11ShaderResourceView> srv;
        D3D11_SHADER_RESOURCE_VIEW_DESC vd{};
        vd.Format = DXGI_FORMAT_UNKNOWN; vd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER; vd.Buffer.NumElements = n * 2;
        if (FAILED(dev->CreateShaderResourceView(inBuf.Get(), &vd, &srv))) { Expect(false, "input view"); return; }
        bd = {};
        bd.ByteWidth = n * 16; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; bd.StructureByteStride = 16;
        ComPtr<ID3D11Buffer> outBuf;
        if (FAILED(dev->CreateBuffer(&bd, nullptr, &outBuf))) { Expect(false, "output buffer"); return; }
        D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
        ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; ud.Buffer.NumElements = n;
        ComPtr<ID3D11UnorderedAccessView> uav;
        if (FAILED(dev->CreateUnorderedAccessView(outBuf.Get(), &ud, &uav))) { Expect(false, "output view"); return; }
        ctx->CSSetShader(cs.Get(), nullptr, 0);
        ID3D11Buffer* cbs[] = {cbuf.Get()};
        ctx->CSSetConstantBuffers(0, 1, cbs);
        ID3D11ShaderResourceView* srvs[] = {srv.Get()};
        ctx->CSSetShaderResources(0, 1, srvs);
        ID3D11UnorderedAccessView* uavs[] = {uav.Get()};
        ctx->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
        ctx->Dispatch((n + 63) / 64, 1, 1);
        bd.Usage = D3D11_USAGE_STAGING; bd.BindFlags = 0; bd.MiscFlags = 0; bd.StructureByteStride = 0; bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Buffer> stage;
        if (FAILED(dev->CreateBuffer(&bd, nullptr, &stage))) { Expect(false, "readback buffer"); return; }
        ctx->CopyResource(stage.Get(), outBuf.Get());
        D3D11_MAPPED_SUBRESOURCE m{};
        if (FAILED(ctx->Map(stage.Get(), 0, D3D11_MAP_READ, 0, &m))) { Expect(false, "map"); return; }
        const float* got = (const float*)m.pData;
        const Shape shape = ShapeOf(cfg, k.tanX, k.tanY);
        float caseWorst = 0.0f;
        for (UINT i = 0; i < n; ++i) {
            const float px[3] = {items[i].r, items[i].g, items[i].b};
            float cpu[3];
            Apply(px, items[i].u, items[i].v, k.alpha, shape, uni.color, cpu);
            for (int c = 0; c < 3; ++c) {
                caseWorst = std::max(caseWorst, std::fabs(cpu[c] - got[i * 4 + c]));
                if (k.alpha == 0.0f && memcmp(&px[c], &got[i * 4 + c], 4) != 0) exactOff = false;
            }
        }
        ctx->Unmap(stage.Get(), 0);
        ID3D11ShaderResourceView* none[] = {nullptr};
        ctx->CSSetShaderResources(0, 1, none);
        ID3D11UnorderedAccessView* noneU[] = {nullptr};
        ctx->CSSetUnorderedAccessViews(0, 1, noneU, nullptr);
        worst = std::max(worst, caseWorst);
        printf("  opacity %.2f, reach %.0f%%, softness %.1f, shape %.1f, red %.1f, field of view %.2f x %.2f: the HLSL differs from the C++ mirror by at most %.2e over %u points\n",
               k.alpha, k.radius * 100.0f, k.softness, k.round, k.saturation, k.tanX, k.tanY, caseWorst, n);
    }
    Expect(worst < 3e-4f, "the HLSL and the C++ mirror agree (worst %.2e)", worst);
    Expect(exactOff, "opacity 0 in the HLSL: every colour comes out bit for bit");
}

// ---- pictures ---------------------------------------------------------------------------------------------------------
struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> rgb;
    Image(int ww, int hh) : w(ww), h(hh), rgb((size_t)ww * hh * 3, 0) {}
    void Put(int x, int y, const float c[3]) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        for (int i = 0; i < 3; ++i) rgb[((size_t)y * w + x) * 3 + i] = (uint8_t)std::clamp((int)std::lround(c[i] * 255.0f), 0, 255);
    }
    void Rect(int x0, int y0, int x1, int y1, const float c[3]) { for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) Put(x, y, c); }
    void Blit(const Image& s, int x0, int y0) {
        for (int y = 0; y < s.h; ++y)
            for (int x = 0; x < s.w; ++x)
                if (x0 + x >= 0 && y0 + y >= 0 && x0 + x < w && y0 + y < h)
                    memcpy(&rgb[((size_t)(y0 + y) * w + x0 + x) * 3], &s.rgb[((size_t)y * s.w + x) * 3], 3);
    }
};

uint32_t Crc(const uint8_t* d, size_t n, uint32_t crc = 0xFFFFFFFFu) {
    static uint32_t table[256];
    static bool made = false;
    if (!made) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        made = true;
    }
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ d[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}
void Be32(std::vector<uint8_t>& v, uint32_t x) { for (int s = 24; s >= 0; s -= 8) v.push_back((uint8_t)(x >> s)); }
void Chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    Be32(out, (uint32_t)data.size());
    std::vector<uint8_t> td(type, type + 4);
    td.insert(td.end(), data.begin(), data.end());
    out.insert(out.end(), td.begin(), td.end());
    Be32(out, ~Crc(td.data(), td.size()));
}
bool WritePng(const std::string& path, const Image& img) {
    std::vector<uint8_t> raw;
    for (int y = 0; y < img.h; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), &img.rgb[(size_t)y * img.w * 3], &img.rgb[(size_t)y * img.w * 3] + (size_t)img.w * 3);
    }
    std::vector<uint8_t> z = {0x78, 0x01};   // zlib, stored blocks (no compression: simple, and every viewer reads it)
    for (size_t at = 0; at < raw.size(); at += 65535) {
        const size_t len = std::min<size_t>(65535, raw.size() - at);
        z.push_back(at + len >= raw.size() ? 1 : 0);
        z.push_back((uint8_t)(len & 0xFF)); z.push_back((uint8_t)(len >> 8));
        z.push_back((uint8_t)(~len & 0xFF)); z.push_back((uint8_t)((~len >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + at, raw.begin() + at + len);
    }
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    Be32(z, (b << 16) | a);
    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A}, ihdr;
    Be32(ihdr, (uint32_t)img.w); Be32(ihdr, (uint32_t)img.h);
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
    Chunk(png, "IHDR", ihdr);
    Chunk(png, "IDAT", z);
    Chunk(png, "IEND", {});
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "wb") || !f) return false;
    fwrite(png.data(), 1, png.size(), f);
    fclose(f);
    return true;
}

// 5x7 letters and digits, for the labels (rows, the left pixel is the top bit of five)
struct Glyph7 {
    char ch;
    uint8_t row[7];
};
const Glyph7 kFont[] = {
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}}, {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}}, {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}}, {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}}, {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}}, {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}}, {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}}, {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}}, {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}}, {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}}, {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}}, {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}}, {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}}, {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}}, {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}}, {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}}, {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}}, {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}}, {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}}, {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}}, {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}}, {'%', {0x19, 0x1A, 0x02, 0x04, 0x08, 0x0B, 0x13}}, {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}}, {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}}, {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11}},
    {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}}, {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}}, {'/', {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10}},
    {':', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}}, {'=', {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}}, {'(', {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}},
    {')', {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}},
};
const Glyph7* FindGlyph(char ch) {
    for (const Glyph7& g : kFont) if (g.ch == ch) return &g;
    return nullptr;
}
void Text(Image& img, int x, int y, int scale, const char* s, const float color[3]) {
    for (; *s; ++s, x += 6 * scale) {
        const Glyph7* g = FindGlyph(*s);
        if (!g) continue;
        for (int r = 0; r < 7; ++r)
            for (int c = 0; c < 5; ++c)
                if (g->row[r] & (0x10 >> c)) img.Rect(x + c * scale, y + r * scale, x + (c + 1) * scale, y + (r + 1) * scale, color);
    }
}

// A made-up game scene, 0 - 1 colour at a uv: a dusk sky behind a wall, a tiled floor, a torch's glow, a monster, the hero,
// and a bit of interface (a white name plate, a blue orb) - so the tint is seen over dark, mid and bright.
void Scene(float u, float v, float out[3]) {
    const float horizon = 0.42f;
    float c[3];
    if (v < horizon) {
        const float t = v / horizon;
        c[0] = 0.16f + 0.26f * t; c[1] = 0.17f + 0.20f * t; c[2] = 0.26f + 0.18f * t;
        const float wall = 0.30f + 0.12f * std::sin(u * 60.0f) * std::sin(v * 40.0f);
        if (v > 0.24f) { c[0] = wall * 0.7f; c[1] = wall * 0.62f; c[2] = wall * 0.55f; }
    } else {
        const float t = (v - horizon) / (1.0f - horizon);
        const float z = 1.0f / (t * 1.1f + 0.05f), x = (u - 0.5f) * z * 1.1f;
        const bool line = std::fmod(std::fabs(x), 1.0f) < 0.05f || std::fmod(z * 0.8f, 1.0f) < 0.06f;
        const float tile = ((int)std::floor(x) + (int)std::floor(z * 0.8f)) & 1 ? 0.30f : 0.36f;
        const float base = line ? 0.12f : tile + 0.20f * t;
        c[0] = base * 1.05f; c[1] = base * 0.92f; c[2] = base * 0.78f;
    }
    auto glow = [&](float cx, float cy, float r, float R, float G, float B) {
        const float d = std::sqrt((u - cx) * (u - cx) + (v - cy) * (v - cy));
        const float k = std::max(0.0f, 1.0f - d / r);
        c[0] += R * k * k; c[1] += G * k * k; c[2] += B * k * k;
    };
    glow(0.20f, 0.50f, 0.35f, 0.55f, 0.35f, 0.10f);   // a torch
    glow(0.82f, 0.30f, 0.20f, 0.25f, 0.28f, 0.35f);    // a cold window
    auto body = [&](float cx, float cy, float rx, float ry, float R, float G, float B) {
        const float dx = (u - cx) / rx, dy = (v - cy) / ry;
        if (dx * dx + dy * dy < 1.0f) { c[0] = R; c[1] = G; c[2] = B; }
    };
    body(0.64f, 0.60f, 0.07f, 0.12f, 0.35f, 0.30f, 0.22f);   // a monster
    body(0.64f, 0.50f, 0.04f, 0.04f, 0.45f, 0.40f, 0.30f);
    body(0.43f, 0.68f, 0.05f, 0.14f, 0.55f, 0.50f, 0.45f);   // the hero
    body(0.43f, 0.55f, 0.03f, 0.04f, 0.65f, 0.55f, 0.45f);
    if (v > 0.10f && v < 0.14f && u > 0.35f && u < 0.65f) { c[0] = c[1] = c[2] = 0.92f; }   // a name plate
    {
        const float dx = u - 0.88f, dy = v - 0.86f;
        if (dx * dx + dy * dy < 0.0030f) { c[0] = 0.10f; c[1] = 0.25f; c[2] = 0.75f; }   // an orb
    }
    for (int i = 0; i < 3; ++i) out[i] = std::clamp(c[i], 0.0f, 1.0f);
}

Image Render(int w, int h, float alpha, const Config& c, float tanX, float tanY) {
    Image img(w, h);
    const Shape shape = ShapeOf(c, tanX, tanY);
    float color[3];
    ColorOf(c.saturation, color);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const float u = (x + 0.5f) / w, v = (y + 0.5f) / h;
            float px[3], out[3];
            Scene(u, v, px);
            Apply(px, u, v, alpha, shape, color, out);
            img.Put(x, y, out);
        }
    return img;
}

// The opacity at a life level and a moment of the beat (the fade fully in).
float AlphaAt(float frac, float phase, const Config& c) {
    const Config s = Sanitized(c);
    return PeakAlpha(Severity(frac, s.threshold), s) * Pulse(phase, s.gentle);
}
float TopOfBeat(const Config& c) { return ShapeFor(c.gentle).lubRise; }   // the phase at which the first beat tops

void Line(Image& img, int x0, int y0, int x1, int y1, const float color[3]) {
    const int steps = std::max(1, std::max(std::abs(x1 - x0), std::abs(y1 - y0)));
    for (int d = 0; d <= steps; ++d) {
        img.Put(x0 + (x1 - x0) * d / steps, y0 + (y1 - y0) * d / steps, color);
        img.Put(x0 + (x1 - x0) * d / steps, y0 + (y1 - y0) * d / steps + 1, color);
    }
}

// A grid of pictures with labels over the columns and beside the rows; cell(r, c) draws a picture and says a note for its corner.
struct Cell {
    Image img;
    std::string note;
};
template <class F>
void Sheet(const std::string& path, const std::vector<std::string>& cols, const std::vector<std::string>& rows, int size, F cell) {
    const float white[3] = {1, 1, 1}, grey[3] = {0.15f, 0.15f, 0.15f}, dim[3] = {0.65f, 0.65f, 0.65f};
    constexpr int gap = 8, head = 22, side = 92;
    Image sheet(side + (int)cols.size() * (size + gap), head + (int)rows.size() * (size + gap));
    sheet.Rect(0, 0, sheet.w, sheet.h, grey);
    for (size_t c = 0; c < cols.size(); ++c) Text(sheet, side + (int)c * (size + gap) + 4, 6, 1, cols[c].c_str(), white);
    for (size_t r = 0; r < rows.size(); ++r) {
        Text(sheet, 4, head + (int)r * (size + gap) + size / 2 - 4, 1, rows[r].c_str(), dim);
        for (size_t c = 0; c < cols.size(); ++c) {
            const Cell x = cell((int)r, (int)c);
            sheet.Blit(x.img, side + (int)c * (size + gap), head + (int)r * (size + gap));
            if (!x.note.empty()) Text(sheet, side + (int)c * (size + gap) + 6, head + (int)r * (size + gap) + size - 12, 1, x.note.c_str(), white);
        }
    }
    WritePng(path, sheet);
}

void Pictures(const std::string& dir) {
    printf("pictures -> %s\n", dir.c_str());
    const float white[3] = {1, 1, 1}, grey[3] = {0.15f, 0.15f, 0.15f}, dim[3] = {0.65f, 0.65f, 0.65f};
    const float t100 = std::tan(50.0f * 3.14159265f / 180.0f);   // a 100 degree view: every picture below is one unless it says
    const Config normal = On();
    Config gentle = On();
    gentle.gentle = true;
    const BeatShape sn = ShapeFor(false), sg = ShapeFor(true);
    char note[24];
    auto percent = [&](float alpha) { snprintf(note, sizeof note, "%d%%", (int)std::lround(alpha * 100.0f)); return std::string(note); };
    auto at = [&](const char* name) { return dir + "\\" + name; };

    // life x moment of the beat, and gentle
    {
        struct Row { float frac; Config cfg; } rows[] = {{0.28f, normal}, {0.15f, normal}, {0.05f, normal}, {0.05f, gentle}};
        Sheet(at("low_hp_sheet.png"), {"NO VIGNETTE", "REST", "LUB", "DUB"}, {"LIFE 28%", "LIFE 15%", "LIFE 5%", "GENTLE 5%"}, 230, [&](int r, int c) {
            const BeatShape& s = rows[r].cfg.gentle ? sg : sn;
            const float phase[4] = {0.0f, 0.90f, s.lubRise, s.dubAt + s.dubRise};
            const float alpha = c == 0 ? 0.0f : AlphaAt(rows[r].frac, phase[c], rows[r].cfg);
            return Cell{Render(230, 230, alpha, rows[r].cfg, t100, t100), c ? percent(alpha) : ""};
        });
    }
    // the radius slider: a thin rim, the middle, a deep vignette - at the top of a beat at two lives
    {
        const int pct[] = {25, 45, 85};
        Sheet(at("low_hp_radius.png"), {"NO VIGNETTE", "LIFE 28% TOP", "LIFE 5% TOP"}, {"RADIUS 25", "RADIUS 45", "RADIUS 85"}, 250, [&](int r, int c) {
            Config cfg = normal;
            cfg.radius = RadiusFromPercent((float)pct[r]);
            const float frac = c == 1 ? 0.28f : 0.05f, alpha = c == 0 ? 0.0f : AlphaAt(frac, TopOfBeat(cfg), cfg);
            return Cell{Render(250, 250, alpha, cfg, t100, t100), c ? percent(alpha) : ""};
        });
    }
    // the saturation: grey to deep red, at the top of a beat at two lives
    {
        const float sat[] = {0.0f, 0.25f, 0.5f, 0.8f, 1.0f};
        Sheet(at("low_hp_saturation.png"), {"SAT 0 GREY", "SAT 0.25", "SAT 0.5", "SAT 0.8 DEFAULT", "SAT 1 FULL RED"}, {"LIFE 15% TOP", "LIFE 5% TOP"}, 200, [&](int r, int c) {
            Config cfg = normal;
            cfg.saturation = sat[c];
            const float alpha = AlphaAt(r ? 0.05f : 0.15f, TopOfBeat(cfg), cfg);
            return Cell{Render(200, 200, alpha, cfg, t100, t100), percent(alpha)};
        });
    }
    // the intensity at the two ends of the life line: three settings of start .. end, at three lives, at the top of a beat
    {
        const float ends[3][2] = {{0.10f, 0.50f}, {0.25f, 0.85f}, {0.50f, 1.00f}};
        const float lives[3] = {0.29f, 0.15f, 0.02f};
        Sheet(at("low_hp_intensity.png"), {"LIFE 29% JUST UNDER", "LIFE 15% HALF WAY", "LIFE 2% NEARLY DEAD"},
              {"0.10 TO 0.50", "0.25 TO 0.85 DEFAULT", "0.50 TO 1.00"}, 250, [&](int r, int c) {
                  Config cfg = normal;
                  cfg.intensityStart = ends[r][0];
                  cfg.intensityEnd = ends[r][1];
                  const float alpha = AlphaAt(lives[c], TopOfBeat(cfg), cfg);
                  return Cell{Render(250, 250, alpha, cfg, t100, t100), percent(alpha)};
              });
    }
    // softness and shape at the top of a beat at 5% life
    {
        const float soft[] = {0.0f, 0.5f, 1.0f}, round[] = {2.0f, 3.5f, 6.0f};
        const char* colLabels[] = {"", "", ""};
        (void)colLabels;
        Sheet(at("low_hp_softness_shape.png"), {"LOW", "MIDDLE", "HIGH"}, {"SOFTNESS 0 .5 1", "SHAPE 2 3.5 6"}, 250, [&](int r, int c) {
            Config cfg = normal;
            if (r == 0) cfg.softness = soft[c]; else cfg.roundness = round[c];
            return Cell{Render(250, 250, AlphaAt(0.05f, TopOfBeat(cfg), cfg), cfg, t100, t100), ""};
        });
    }
    // the same slider on three headsets: the angle is the same, the picture's share is not (and with no field of view known)
    {
        const float fov[] = {80.0f, 100.0f, 130.0f, 0.0f};
        const int pct[] = {40, 70};
        Sheet(at("low_hp_fov.png"), {"80 DEG VIEW", "100 DEG VIEW", "130 DEG VIEW", "FOV UNKNOWN"}, {"RADIUS 40", "RADIUS 70"}, 200, [&](int r, int c) {
            Config cfg = normal;
            cfg.radius = RadiusFromPercent((float)pct[r]);
            const float t = fov[c] > 0.0f ? std::tan(0.5f * fov[c] * 3.14159265f / 180.0f) : 0.0f;
            return Cell{Render(200, 200, AlphaAt(0.05f, TopOfBeat(cfg), cfg), cfg, t, t), ""};
        });
    }
    // one large square eye picture (the headset's eye is about square), before and after, at the top of a beat near no life
    {
        const float alpha = AlphaAt(0.05f, sn.lubRise, normal);
        Image pair(2 * 560 + 8, 560 + 22);
        pair.Rect(0, 0, pair.w, pair.h, grey);
        Text(pair, 4, 6, 2, "BEFORE", white);
        Text(pair, 568, 6, 2, "AFTER", white);
        pair.Blit(Render(560, 560, 0.0f, normal, t100, t100), 0, 22);
        pair.Blit(Render(560, 560, alpha, normal, t100, t100), 568, 22);
        WritePng(at("low_hp_eye_square.png"), pair);
    }
    // a wide picture (16:9), in case the window is not square, at the lub's top and at rest
    {
        Image wide(2 * 480 + 8, 270 + 22);
        wide.Rect(0, 0, wide.w, wide.h, grey);
        Text(wide, 4, 6, 2, "LUB", white);
        Text(wide, 488, 6, 2, "REST", white);
        const float tx = t100, ty = t100 * 9.0f / 16.0f;
        wide.Blit(Render(480, 270, AlphaAt(0.10f, sn.lubRise, normal), normal, tx, ty), 0, 22);
        wide.Blit(Render(480, 270, AlphaAt(0.10f, 0.90f, normal), normal, tx, ty), 488, 22);
        WritePng(at("low_hp_eye_wide.png"), wide);
    }
    // the opacity over four seconds, from the real state machine at 90 Hz: three lives and gentle
    {
        constexpr int W = 960, H = 340, L = 50, T = 18, B = 40;
        Image plot(W, H);
        plot.Rect(0, 0, W, H, grey);
        const float axis[3] = {0.35f, 0.35f, 0.35f};
        for (int s = 0; s <= 4; ++s) {
            const int x = L + (W - L - 10) * s / 4;
            plot.Rect(x, T, x + 1, H - B, axis);
            char t[8];
            snprintf(t, sizeof t, "%d", s);
            Text(plot, x - 3, H - B + 6, 2, t, dim);
        }
        Text(plot, W / 2 - 40, H - 18, 1, "SECONDS", dim);
        for (int a = 0; a <= 4; ++a) {
            const int y = (H - B) - (H - B - T) * a / 4;
            plot.Rect(L, y, W - 10, y + 1, axis);
            char t[8];
            snprintf(t, sizeof t, "%d", a * 25);
            Text(plot, 4, y - 4, 1, t, dim);
        }
        Text(plot, 4, 4, 1, "OPACITY %", dim);
        struct Trace { float frac; Config cfg; float color[3]; const char* label; } traces[] = {
            {0.28f, normal, {0.95f, 0.85f, 0.35f}, "28%"}, {0.15f, normal, {0.95f, 0.55f, 0.20f}, "15%"},
            {0.05f, normal, {1.0f, 0.25f, 0.25f}, "5%"}, {0.05f, gentle, {0.45f, 0.80f, 1.0f}, "GENTLE 5%"}};
        int k = 0;
        for (const Trace& t : traces) {
            Heartbeat hb;
            Heartbeat::In in = Seen(t.frac);
            in.dt = kDt;
            int px = -1, py = -1;
            for (float s = 0.0f; s < 4.0f; s += kDt) {
                const Heartbeat::Out o = hb.Step(t.cfg, in);
                const int x = L + (int)((W - L - 10) * s / 4.0f), y = (H - B) - (int)((H - B - T) * std::min(1.0f, o.alpha));
                if (px >= 0) Line(plot, px, py, x, y, t.color);
                px = x; py = y;
            }
            Text(plot, W - 110, T + 6 + k * 12, 1, t.label, t.color);
            ++k;
        }
        WritePng(at("low_hp_pulse.png"), plot);
    }
    // by life: the opacity at the top of a beat, the pulse in the hands (lub and dub) and the rate, from the threshold to no life
    {
        constexpr int W = 960, H = 380, L = 54, T = 20, B = 44;
        Image plot(W, H);
        plot.Rect(0, 0, W, H, grey);
        const float axis[3] = {0.35f, 0.35f, 0.35f};
        const float thr = normal.threshold;
        auto xOf = [&](float frac) { return L + (int)((W - L - 10) * (1.0f - frac / thr)); };   // the threshold on the left, no life on the right
        auto yOf = [&](float v) { return (H - B) - (int)((H - B - T) * std::clamp(v, 0.0f, 1.0f)); };
        for (int a = 0; a <= 4; ++a) {
            const int y = yOf(a / 4.0f);
            plot.Rect(L, y, W - 10, y + 1, axis);
            char t[8];
            snprintf(t, sizeof t, "%d", a * 25);
            Text(plot, 4, y - 4, 1, t, dim);
        }
        for (int p = 0; p <= 30; p += 5) {
            const int x = xOf(p / 100.0f);
            plot.Rect(x, T, x + 1, H - B, axis);
            char t[8];
            snprintf(t, sizeof t, "%d", p);
            Text(plot, x - 4, H - B + 6, 1, t, dim);
        }
        Text(plot, W / 2 - 70, H - 20, 1, "LIFE % (THRESHOLD 30 LEFT, DEAD RIGHT)", dim);
        Text(plot, 4, 4, 1, "OPACITY / PULSE STRENGTH % / BPM OF 100", dim);
        struct Trace { const char* label; float color[3]; Config cfg; int kind; } traces[] = {
            {"OPACITY", {1.0f, 0.3f, 0.3f}, normal, 0}, {"PULSE LUB", {0.95f, 0.85f, 0.35f}, normal, 1}, {"PULSE DUB", {0.95f, 0.55f, 0.20f}, normal, 2},
            {"BPM OF 100", {0.45f, 0.80f, 1.0f}, normal, 3}, {"GENTLE OPACITY", {0.6f, 0.2f, 0.2f}, gentle, 0}};
        int k = 0;
        for (const Trace& t : traces) {
            const Config cfg = Sanitized(t.cfg);
            int px = -1, py = -1;
            for (int i = 0; i <= 120; ++i) {
                const float frac = thr * (1.0f - i / 120.0f), sev = Severity(frac, thr);
                const float v = t.kind == 0 ? PeakAlpha(sev, cfg) : t.kind == 1 ? HapticFor(false, sev, cfg).amplitude : t.kind == 2 ? HapticFor(true, sev, cfg).amplitude : Bpm(sev, cfg.gentle) / 100.0f;
                const int x = xOf(frac), y = yOf(v);
                if (px >= 0) Line(plot, px, py, x, y, t.color);
                px = x; py = y;
            }
            Text(plot, W - 150, T + 6 + k * 12, 1, t.label, t.color);
            ++k;
        }
        WritePng(at("low_hp_by_life.png"), plot);
    }
    printf("  low_hp_sheet, _radius, _saturation, _intensity, _softness_shape, _fov, _eye_square, _eye_wide, _pulse, _by_life (.png)\n");
}

}  // namespace

int main(int argc, char** argv) {
    std::string dir, fx = LOWHP_FX_PATH;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--fx") && i + 1 < argc) fx = argv[++i];
        else dir = argv[i];
    }
    TestLife();
    TestConfig();
    TestWhere();
    TestShape();
    TestRate();
    TestHysteresis();
    TestFade();
    TestHitch();
    TestRamps();
    TestBeats();
    TestOff();
    TestMask();
    TestFov();
    TestPreview();
    TestPreviewWindow();
    TestWeakMotor();
    TestHlsl(fx);
    if (!dir.empty()) Pictures(dir);
    printf(g_failed ? "FAILED: %d of %d checks\n" : "all %d checks passed\n", g_failed ? g_failed : g_checks, g_checks);
    return g_failed ? 1 : 0;
}
