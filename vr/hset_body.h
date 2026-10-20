#pragma once
// The Body page of the in-headset settings panel: the T-pose measurement of the player's eye height and arm span
// (vr/calibration.*, vr/body_calibration.h). The page starts the measurement from the headset - the plugin shows its own
// prompt and writes the result into [calibration] - clears a stored one, and says what is stored. This file is the pure
// part: what the stored keys mean and the page's lines. No Windows, no OpenXR - hset_test.exe runs it.

#include <cmath>
#include <cstdio>
#include <string>

#include "body_calibration.h"

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
        char b[96];
        if (s.eyeValid) std::snprintf(b, sizeof b, "Eye height %.2f m, arm span %.2f m", (double)s.eye, (double)s.span);
        else std::snprintf(b, sizeof b, "Arm span %.2f m (eye height: not measured)", (double)s.span);
        return b;
    }
    if (s.seated) return s.any ? "A stored arm span is not usable: measure again" : "Arm span not measured yet";
    return s.any ? "A stored measurement is not usable: measure again" : "Not measured yet";
}
// Whether the numbers are in force (the box in D2R VR Settings > Body).
inline std::string UseText(const Stored& s) {
    if (!s.valid) return "Until you measure, the height from BodyWalk or \"Your height\" and the arm scale apply.";
    if (!s.use) return "Not in use: the box \"Use the measured height and arm span\" is unticked in D2R VR Authentic Settings.";
    if (s.seated && !s.eyeValid) return "In use: the hero's arms follow it; the world's scale is \"Your height\" (measure standing once for it).";
    return "In use: the world's scale and the hero's arms follow it.";
}
// The line under the title: what Calibrate reads, standing or seated.
inline const char* WhyText(bool seated) {
    return seated ? "Seated: Calibrate reads your arm span only. Recentre view sets the hero's eyes where your head is."
                  : "Your real eye height sets the world's scale, your arm span the hero's arms.";
}

}  // namespace hset::body
