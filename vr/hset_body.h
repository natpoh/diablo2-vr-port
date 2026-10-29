#pragma once
// The Body page of the in-headset settings panel: the T-pose measurement of the player's eye height and arm span
// (vr/calibration.*, vr/body_calibration.h). The page starts the measurement from the headset - the plugin shows its own
// prompt and writes the result into [calibration] - clears a stored one, and says what is stored. This file is the pure
// part: what the stored keys mean and the page's lines. No Windows, no OpenXR - hset_test.exe runs it.

#include <cmath>
#include <cstdio>
#include <string>

#include "body_calibration.h"
#include "ui_text.h"   // the page's words in the player's language ([hud] headset_language)

namespace hset::body {

// What [calibration] holds, as the plugin reads it (calib::LoadSettings): a measurement counts only with version=1 and numbers
// that are a person's (bodycal::InForce: standing, both; seated, the arm span, and the eye height with it when it goes with it);
// `use` says whether it is in force.
struct Stored {
    bool any = false;     // something is stored (there is something to forget)
    bool valid = false;   // a measurement the plugin would use
    bool use = false;     // [calibration] use
    bool seated = false;  // [calibration] seated: played seated
    bool eyeValid = false;   // the eye height is used with it (standing: always when valid; seated: a standing one is stored)
    float eye = 0, span = 0;
};
inline Stored Of(float use, float version, float eye, float span, float seated = 0.0f) {
    Stored s;
    s.use = std::isfinite(use) && use > 0.5f;
    s.seated = std::isfinite(seated) && seated > 0.5f;
    s.eye = std::isfinite(eye) ? eye : 0.0f;
    s.span = std::isfinite(span) ? span : 0.0f;
    const bool versioned = std::isfinite(version) && version > 0.5f;
    const bodycal::Measurement in = versioned ? bodycal::InForce(s.seated, {s.eye, s.span}) : bodycal::Measurement{};
    s.valid = in.armSpan > 0.0f;
    s.eyeValid = in.eyeHeight > 0.0f;
    s.any = versioned || s.eye > 0.0f || s.span > 0.0f;
    return s;
}

// "Eye height 1.62 m, arm span 1.71 m" / seated without a standing eye height "Arm span 1.71 m (eye height: not measured)" / "Not measured yet".
inline std::string MeasuredText(const Stored& s) {
    if (s.valid) {
        if (s.eyeValid)
            return uitext::TrF("vr.body.measured", "Eye height {eye} m, arm span {span} m",
                               {{"eye", uitext::Num("%.2f", s.eye)}, {"span", uitext::Num("%.2f", s.span)}});
        return uitext::TrF("vr.body.measured_span", "Arm span {span} m (eye height: not measured)", {{"span", uitext::Num("%.2f", s.span)}});
    }
    if (s.seated) return s.any ? uitext::Tr("vr.body.span_unusable", "A stored arm span is not usable: measure again") : uitext::Tr("vr.body.span_none", "Arm span not measured yet");
    return s.any ? uitext::Tr("vr.body.unusable", "A stored measurement is not usable: measure again") : uitext::Tr("vr.body.none", "Not measured yet");
}
// Whether the numbers are in force (the box in D2R VR Settings > Body).
inline std::string UseText(const Stored& s) {
    if (!s.valid) return uitext::Tr("vr.body.use_until", "Until you measure, the height from BodyWalk or \"Your height\" and the arm scale apply.");
    if (!s.use) return uitext::Tr("vr.body.use_off", "Not in use: the box \"Use the measured height and arm span\" is unticked in D2R VR Authentic Settings.");
    if (!s.eyeValid) return uitext::Tr("vr.body.use_span", "In use: the hero's arms follow it; the world's scale is \"Your height\" (measure standing once for it).");
    return uitext::Tr("vr.body.use_on", "In use: the world's scale and the hero's arms follow it.");
}
// The Arm span stepper under Calibrate (the author, 2026-10-25: a slider to move by hand): it shows the span the hero's arms are fitted to now
// and sets [calibration] arm_span_m by hand, a centimetre a press, live. A new measurement writes the key again, so the stepper follows it.
// Nothing stored, it shows the span in use without one ([calibration] default_span, "auto"), and the first press starts from that.
constexpr float kSpanStepM = 0.01f, kSpanMinM = 0.85f, kSpanMaxM = 2.6f;   // (bodycal::ValidSpan's range)
constexpr float kSpanFallbackM = 1.60f;   // no span known at all: where the first press starts
inline float StepSpan(float shown, int delta) {
    const float from = std::isfinite(shown) && shown >= kSpanMinM && shown <= kSpanMaxM ? shown : kSpanFallbackM;
    const float to = std::round((from + (float)delta * kSpanStepM) * 100.0f) / 100.0f;
    return std::fmin(kSpanMaxM, std::fmax(kSpanMinM, to));
}
// "1.68 m" (a span stored), "1.66 m (auto)" (none stored: the span in use without one), "Auto" (not known).
inline std::string SpanText(float shown, bool stored) {
    char b[48];
    if (stored) std::snprintf(b, sizeof b, "%.2f m", (double)shown);
    else if (shown > 0.0f) return uitext::TrF("vr.body.span_auto", "{span} m (auto)", {{"span", uitext::Num("%.2f", shown)}});
    else return uitext::Tr("vr.body.auto", "Auto");
    return b;
}

// The line under the title: what Calibrate reads, standing or seated.
inline const char* WhyText(bool seated) {
    return seated ? uitext::Tr("vr.body.why_seated", "Seated: Calibrate reads your arm span only. Recentre view sets the hero's eyes where your head is.")
                  : uitext::Tr("vr.body.why", "Your real eye height sets the world's scale, your arm span the hero's arms.");
}

}  // namespace hset::body
