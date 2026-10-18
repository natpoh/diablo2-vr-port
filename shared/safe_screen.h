#pragma once
// The game's interface size ("Safe Screen Percent" in its Settings.json, 100 = the whole screen, the game's default) and what D2R VR does to
// it. At 100 the interface is cut off at the edges in the headset (a player had to lower it by hand to 70); D2R VR starts it at 80. Same
// shape as shared/frame_cap.h (the game's frame cap) and shared/window_json.h (its window): VR writes its value - only where the player has
// not chosen one of his own (the game's default 100 is the "not chosen") - the value the player had is kept before it ([hud]
// safe_screen_before) and put back in flat mode, and what VR wrote is kept too ([hud] safe_screen_written), so the 80 VR wrote is told from an
// 80 the player chose. No Windows in it: shared by vrcam (at its start, before the game reads the file) and D2R VR Settings (on a switch of
// the platform, on START, when the box is ticked or unticked), and tested without either.

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

#include "frame_cap.h"   // (its two number readers work on any key)

namespace safescreen {

constexpr const char* kKey = "Safe Screen Percent";
constexpr int kGameDefault = 100;   // what the game has when nobody chose: not a player's choice
constexpr int kVrValue = 80;        // what VR starts at
constexpr int kMin = 30, kMax = 100;   // the range the interface code takes (vr/hud_rects.h: 0.3 - 1)

// The key's whole number in the file, or -1 (no such key, or not a number).
inline int Read(const std::string& text) { return framecap::Read(text, kKey); }
// The text with the key's number set to `value`, everything else byte for byte; the same text back (ok false) when the key is missing.
inline std::string With(const std::string& text, int value, bool* ok = nullptr) { return framecap::With(text, value, ok, kKey); }

// The interface as a fraction of the screen, what the toolbar's cut and the hand's globes are worked out from (vr/hud.cpp, hud_rects.h,
// hud_globes.h): the file's number over 100, 1 when the key is missing or out of the interface code's range.
inline float Scale(const std::string& text) {
    const int pct = Read(text);
    return pct >= kMin && pct <= kMax ? (float)pct / 100.0f : 1.0f;
}

// -- how big the interface may be for two side panels to fit the picture ---------------------------------------------------
// Two panels open at once - a trader, the mercenary or a player's trade on the LEFT and the inventory on the RIGHT - need room side by side. The
// game's own layout (data\global\ui\Layouts\_profileHD.json and the layouts of the panels; read from its archives with tools\casc_extract, only
// the numbers are here) sets them in units of a 2160-high screen, k = H / 2160 * s pixels a unit, in a frame as wide as the PICTURE
// (W / k units = aspect * 2160 / s): the left panel's frame 1162 units wide at 236 from the frame's left edge, the right panel's the same
// width at 1394 from its right edge. What the player reaches is the content: the left panel's ends at 1160 of its 1162 (its close button, so
// 1396 from the frame's left), the inventory's starts 93 units in (a grid cell, the paperdoll's weapon slot at 107: W - 1301). They meet
// at W = 2697 units: wider and they do not overlap (the controller layout, panels about the middle, needs less: 2408). In a SQUARE
// picture 2160 / s = 2697 at s = 80.09 %. Above that the panels lie over one another (or, with the controller layout, run off the
// picture's edges): the outer columns cannot be seen or pointed at - a player's "I can't use the two right-most columns of my
// inventory with the trade / mercenary window open" at the game's default 100 %.
constexpr float kRefHeightUnits = 2160.0f;
constexpr float kTwoPanelsUnits = 2697.0f;   // the width, in units, at which the left panel's content and the inventory's meet
// The highest interface size (percent) at which two side panels fit a picture of this shape (width over height), capped to the game's range.
inline int FitPercent(float aspect) {
    if (!std::isfinite(aspect) || aspect <= 0.0f) return kMax;
    const float p = 100.0f * kRefHeightUnits * aspect / kTwoPanelsUnits;
    return (int)std::clamp(std::floor(p + 1e-3f), (float)kMin, (float)kMax);
}
inline bool PanelsFit(int percent, float aspect) { return percent <= FitPercent(aspect); }
// The picture the interface is laid out in, as the ini says it: a render size's square (native OpenXR, [render]), the headset's own shape
// (its recommendation, when the game has told it), or the game's window - a square when D2R VR's square window is on (native without a render
// size, FlatVR: its picture is the window), else the file's own window, else a wide screen (nothing to fear).
inline float PictureAspect(bool squarePick, bool headsetPick, int recW, int recH, bool squareWindow, int windowW, int windowH) {
    if (squarePick) return 1.0f;
    if (headsetPick && recW > 0 && recH > 0) return (float)recW / (float)recH;
    if (squareWindow) return 1.0f;
    return windowW > 0 && windowH > 0 ? (float)windowW / (float)windowH : 16.0f / 9.0f;
}

// What the ini kept: -1 = nothing.
struct State {
    int own = -1;       // [hud] safe_screen_before: what the file had before VR wrote its own (the game's default)
    int written = -1;   // [hud] safe_screen_written: what VR wrote
};

struct Plan {
    bool write = false;      // write `value` into Settings.json
    int value = -1;
    bool saveOwn = false;    // keep `own` as the player's own
    int own = -1;
    bool saveWritten = false;   // keep `written`
    int written = -1;
    bool clearWritten = false;  // VR's value is not in the file and no write-back of it is coming: forget it was ever written
    bool unknownOwn = false;    // flat mode, VR's value in the file and none of the player's kept: nothing is written
    const char* why = "";
};

// What VR starts the interface at for a picture of this shape: 80, or less where two side panels would not fit it at 80 (the headset's own,
// taller-than-wide shape: 74 for a Quest 3's 2496 x 2688).
inline int VrValueFor(float aspect) { return std::min(kVrValue, FitPercent(aspect)); }

// VR ([mode] platform=1, native OpenXR and FlatVR alike). `now` = the file's value (-1: no key, nothing to write on). `on` = the player's box
// ([hud] safe_screen_vr). `value` = what VR starts at for this picture (VrValueFor). A value other than the game's default is the player's own -
// whether it is 90, or an 80 of his - and stays; the value VR wrote itself follows the picture's shape (a headset's own size after a square).
inline Plan ForVr(int now, const State& s, bool on, int value = kVrValue) {
    Plan p;
    if (!on || now < 0) return p;
    if (now != kGameDefault && !(s.written >= 0 && now == s.written && now != value)) return p;
    p.write = true;
    p.value = value;
    p.why = now == kGameDefault ? "the game's default interface size cut off at the edges in the headset" : "VR's own interface size, for the shape of this picture";
    if (now == kGameDefault && s.own != kGameDefault) { p.saveOwn = true; p.own = kGameDefault; }
    if (s.written != value) { p.saveWritten = true; p.written = value; }
    return p;
}

// FLAT mode (and the box unticked in VR): the interface size is the player's own. VR's value in the file goes, the kept one comes back; none
// kept: nothing is written (`unknownOwn`). Any other value in the file is the player's. `writeBackPending`: the game is running and will write
// its own copy back at its quit - which may be VR's 80 again - so VR's value stays remembered until the next start deals with it (the plugin's
// start passes false: the file is read right after).
inline Plan ForFlat(int now, const State& s, bool writeBackPending) {
    Plan p;
    if (now < 0) return p;
    if (s.written >= 0 && now == s.written) {
        if (s.own < 0) {
            p.unknownOwn = true;
            p.why = "VR's interface size in the file and none of the player's kept";
            return p;
        }
        if (s.own != now) {
            p.write = true;
            p.value = s.own;
            p.why = "the interface size the player had before VR's";
        }
        p.clearWritten = !writeBackPending;
        return p;
    }
    if (s.written >= 0 && !writeBackPending) p.clearWritten = true;   // not VR's any more, nothing coming back: a later 80 of the player's is his
    return p;
}

}  // namespace safescreen
