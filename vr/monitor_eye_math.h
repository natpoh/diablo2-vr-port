#pragma once
// [render] monitor_eye (vr/monitor_eye.h): what each present of an eye does to the window's picture, apart from D3D12 - so that it
// can be tested (tools/monitor_eye_test.cpp). The idea is bearstonem/d2r-pcvr's ([render] desktop_one_eye, steamvr/transport.cpp, MIT):
// the headset has taken the eye this present carries; the window gets the one eye kept instead.

#include <cmath>
#include <cstdint>

namespace monitoreye {

// [render] monitor_eye: 0 as before (each present shows the eye it carries: both eyes in turn), 1 the left eye, 2 the right.
enum Setting { kAsBefore = 0, kLeft = 1, kRight = 2 };
// The eye the window keeps to: 0 left, 1 right; -1 none (as before).
inline int EyeOf(int setting) { return setting == kLeft ? 0 : setting == kRight ? 1 : -1; }

// A kept picture older than this is not shown: it is from before a menu, a load or a view without stereo.
constexpr uint64_t kKeptMaxMs = 250;

// A part of a picture: its top left and size, 0..1 of the picture's width and height.
struct Rect {
    float u, v, w, h;
};

// DXGI stretches the back buffer (bw x bh) over the window's client area (ww x wh): with a VR render size apart from the window
// ([render] vr_resolution_px - a square picture in a wide window) the monitor showed the picture stretched. True when the two
// shapes differ by more than half a percent (a size known on both sides).
inline bool FitWanted(uint32_t bw, uint32_t bh, uint32_t ww, uint32_t wh) {
    if (!bw || !bh || !ww || !wh) return false;
    const double b = (double)bw / bh, w = (double)ww / wh;
    return std::fabs(b - w) > 0.005 * w;
}

// The middle of a picture (sw x sh pixels) in the window's shape (ww x wh): as wide as the picture and cut at the top and the
// bottom, or as high and cut at the sides. Drawn over the whole back buffer, the window's stretch then shows it at its own
// proportions. The whole picture when a size is not known.
inline Rect Crop(uint32_t sw, uint32_t sh, uint32_t ww, uint32_t wh) {
    if (!sw || !sh || !ww || !wh) return {0.0f, 0.0f, 1.0f, 1.0f};
    const double want = (double)ww / wh, have = (double)sw / sh;
    if (have > want) {   // the picture is wider: its sides cut
        const float w = (float)(want / have);
        return {(1.0f - w) * 0.5f, 0.0f, w, 1.0f};
    }
    const float h = (float)(have / want);   // the picture is higher (or the same): its top and bottom cut
    return {0.0f, (1.0f - h) * 0.5f, 1.0f, h};
}

// Where the window's picture comes from at this present.
enum class Source {
    Nothing,   // the back buffer as it is (the eye it carries is the one wanted, or nothing to put there)
    Kept,      // the wanted eye's picture kept from an earlier present or the left eye's hold (or just now, to be cropped)
    Made       // eyes by turns: the wanted eye made on the GPU this frame (the picture the headset has for it now)
};
struct In {
    int want;          // EyeOf(the setting): -1 none
    int shown;         // the eye this present carries: -1 none (a menu, a flat picture, mono)
    bool keptOk;       // a kept picture of the wanted eye, the back buffer's size and family, not older than kKeptMaxMs
    bool madeOk;       // eyes by turns: the wanted eye was made this frame (the back buffer's size and family)
    bool drawnOnly;    // [render] monitor_eye_drawn_only: never the made eye - only pictures the game drew
    bool fit;          // FitWanted and [render] monitor_eye_fit
};
struct Step {
    bool keep;         // the back buffer (the wanted eye) kept first
    Source from;       // then this into the back buffer
    bool crop;         // drawn as Crop (stretched over the back buffer), not copied
};
inline Step Decide(const In& in) {
    if (in.want < 0 || in.shown < 0) return {false, Source::Nothing, false};
    if (in.shown == in.want)   // the eye wanted: as it is - kept for the presents of the other eye; cropped from that copy
        return {true, in.fit ? Source::Kept : Source::Nothing, in.fit};
    const Source from = in.madeOk && !in.drawnOnly ? Source::Made : in.keptOk ? Source::Kept : Source::Nothing;
    return {false, from, from != Source::Nothing && in.fit};
}

}  // namespace monitoreye
