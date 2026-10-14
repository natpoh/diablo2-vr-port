#pragma once
// The VR render size as the ini states it, read the same way by the plugin (vr/vr_resolution.h) and by D2R VR Settings.
// No Windows in it.
//
//   [render] vr_resolution_px   the SQUARE side in pixels, 1024..4096 (the settings' slider: steps of 64). 0 = not used.
//   [render] vr_resolution      the older mode: 0 Desktop (the window's own size), 1 Headset (what the headset's runtime
//                               recommends), 2..6 a square of 1536 / 2048 / 2560 / 3072 / 4096. Still read from older inis.
//
// Together: mode 1 (the settings' tick box "Use the headset's recommended size") wins; else a side in vr_resolution_px; else the
// mode's own square (an old ini) or Desktop. So an ini written by the first version keeps working as it was.

#include <algorithm>
#include <cstdint>

namespace rendersize {

inline constexpr int kSideMin = 1024, kSideMax = 4096, kSideStep = 64;
inline constexpr int kModeSides[5] = {1536, 2048, 2560, 3072, 4096};   // [render] vr_resolution 2..6

enum class Kind { Desktop, Headset, Square };
struct Pick {
    Kind kind = Kind::Desktop;
    int side = 0;   // Square: pixels a side, kSideMin..kSideMax and even
    bool operator==(const Pick&) const = default;
};

// A side from the ini (hand-edited or not): kept to the range and made even.
inline int ClampSide(int side) { return std::clamp(side, kSideMin, kSideMax) & ~1; }

inline Pick FromIni(int mode, int px) {
    if (mode == 1) return {Kind::Headset, 0};
    if (px > 0) return {Kind::Square, ClampSide(px)};
    if (mode >= 2 && mode <= 6) return {Kind::Square, kModeSides[mode - 2]};
    return {};
}

// The slider in D2R VR Settings: one stop below the smallest side reads "window's size" (Desktop, px = 0); the others are
// the sides themselves. These are the two ways between the slider's stop and the ini's key.
inline constexpr int kStopWindow = kSideMin - kSideStep;
inline int StopOf(const Pick& p) { return p.kind == Kind::Square ? p.side : kStopWindow; }
inline int PxOfStop(int stop) { return stop <= kStopWindow ? 0 : ClampSide(stop); }
// Moving the slider makes the slider's number the whole answer: an older square mode (2..6) in the ini is cleared. Mode 1 (the
// tick box) stays - the slider is out of reach while it is ticked.
inline int ModeAfterSlider(int mode) { return mode >= 2 && mode <= 6 ? 0 : mode; }

// What a picture of w x h costs against one of 2048 x 2048 (the pixels to draw and to keep: 1 = the same).
inline double CostVs2048(double w, double h) { return w * h / (2048.0 * 2048.0); }

}  // namespace rendersize
