#pragma once
// eye_gap.h - the distance between the two eyes (IPD) of native OpenXR's stereo pair: the runtime's own, or the player's (D2R VR Settings > Stereo >
// Native OpenXR: [openxr] ipd_auto, ipd_mm). Pure: no OpenXR, no Windows (tools/ipd_test.cpp).
//
// Auto (the default): the poses xrLocateViews gives, as always - except for a runtime that locates both eyes in ONE place (no position for the
// eyes, only the head's turn: a Meta Air Link player's log, 2026-10-14, "eyes 0 mm apart", a double image that never merged): they get
// [openxr] ipd_fallback_mm (64) apart. Manual: the player's number, whatever the runtime says - for a headset whose IPD was not taken up
// (or taken up wrong) for some reason. Either way the two eyes keep their midpoint and their own turns (only the positions move): the left one
// half the gap one way along the line between them and the right one half the other (Place says which line).

#include <algorithm>
#include <cmath>

namespace eyegap {

constexpr float kMinM = 0.040f, kMaxM = 0.090f;   // what the ini takes (D2R VR Settings' slider is 50 - 80 mm); a hand-edited number is kept to it
constexpr float kOneSpotM = 0.020f;               // two views closer than this are no pair of eyes: one place
constexpr float kDefaultM = 0.063f;               // [openxr] ipd_mm when the key is missing

enum class Why { Runtime, Fallback, Manual };   // whose number the pair is drawn with

struct Choice {
    Why why = Why::Runtime;
    float gapM = 0.0f;   // the gap the eyes are drawn at (the runtime's own when why is Runtime)
    bool place = false;  // the eyes have to be moved to gapM (Runtime: no)
};

inline float ClampGap(float m) { return std::isfinite(m) ? std::clamp(m, kMinM, kMaxM) : kDefaultM; }

// `measuredM`: the gap between the two views as the runtime located them. `fallbackM`: [openxr] ipd_fallback_mm in metres (0 = off).
inline Choice Choose(float measuredM, bool autoOn, float manualM, float fallbackM) {
    Choice c;
    c.gapM = measuredM;
    if (!autoOn) {
        c.why = Why::Manual;
        c.gapM = ClampGap(manualM);
        c.place = true;
    } else if (fallbackM > 0.0f && !(measuredM >= kOneSpotM)) {   // (not a number counts as one place)
        c.why = Why::Fallback;
        c.gapM = fallbackM;
        c.place = true;
    }
    return c;
}

inline float Gap(const float a[3], const float b[3]) {
    const float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return std::sqrt(x * x + y * y + z * z);
}

// The two eyes' positions moved to `gapM` apart about their midpoint (which stays where it is), along the line the runtime put them on - or, when
// it put them in one place, along the mean of their two right axes (`right[e]`, unit vectors: the view's own X; the eyes of a canted display
// differ by a few degrees, which is why the mean and not each its own: the midpoint would move). Left is the first eye, the one on that line's
// negative side.
// The eyes `gapM` apart about their middle along `axis` (any length but nothing), the first eye on its negative side. False: no axis, nothing moved.
inline bool PlaceAlong(float pos[2][3], const float axisIn[3], float gapM) {
    float axis[3] = {axisIn[0], axisIn[1], axisIn[2]};
    const float len = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
    if (!(len > 1e-6f)) return false;
    float mid[3];
    for (int i = 0; i < 3; ++i) { mid[i] = 0.5f * (pos[0][i] + pos[1][i]); axis[i] /= len; }
    for (int e = 0; e < 2; ++e) {
        const float side = (e == 0 ? -0.5f : 0.5f) * gapM;
        for (int i = 0; i < 3; ++i) pos[e][i] = mid[i] + axis[i] * side;
    }
    return true;
}
inline void Place(float pos[2][3], const float right[2][3], float gapM) {
    float axis[3] = {right[0][0] + right[1][0], right[0][1] + right[1][1], right[0][2] + right[1][2]};
    const float apart[3] = {pos[1][0] - pos[0][0], pos[1][1] - pos[0][1], pos[1][2] - pos[0][2]};
    const float gap = std::sqrt(apart[0] * apart[0] + apart[1] * apart[1] + apart[2] * apart[2]);
    if (gap >= kOneSpotM)
        for (int i = 0; i < 3; ++i) axis[i] = apart[i];   // the runtime's own line between the eyes
    if (PlaceAlong(pos, axis, gapM)) return;
    PlaceAlong(pos, right[0], gapM);   // (opposite rights: nothing to take the mean of) the first eye's
}

}  // namespace eyegap
