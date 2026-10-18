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
// that are a person's (bodycal::Valid); `use` says whether it is in force.
struct Stored {
    bool any = false;     // something is stored (there is something to forget)
    bool valid = false;   // a measurement the plugin would use
    bool use = false;     // [calibration] use
    float eye = 0, span = 0;
};
inline Stored Of(float use, float version, float eye, float span) {
    Stored s;
    s.use = std::isfinite(use) && use > 0.5f;
    s.eye = std::isfinite(eye) ? eye : 0.0f;
    s.span = std::isfinite(span) ? span : 0.0f;
    const bool versioned = std::isfinite(version) && version > 0.5f;
    s.valid = versioned && bodycal::Valid({s.eye, s.span});
    s.any = versioned || s.eye > 0.0f || s.span > 0.0f;
    return s;
}

// "Eye height 1.62 m, arm span 1.71 m" / "Not measured yet".
inline std::string MeasuredText(const Stored& s) {
    if (s.valid) {
        char b[96];
        std::snprintf(b, sizeof b, "Eye height %.2f m, arm span %.2f m", (double)s.eye, (double)s.span);
        return b;
    }
    return s.any ? "A stored measurement is not usable: measure again" : "Not measured yet";
}
// Whether the numbers are in force (the box in D2R VR Settings > Body).
inline std::string UseText(const Stored& s) {
    if (!s.valid) return "Until you measure, the height from BodyWalk or \"Your height\" and the arm scale apply.";
    return s.use ? "In use: the world's scale and the hero's arms follow it."
                 : "Not in use: the box \"Use the measured height and arm span\" is unticked in D2R VR Authentic Settings.";
}

}  // namespace hset::body
