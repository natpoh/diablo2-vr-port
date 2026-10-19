#pragma once
// Flipping the skill panel's window like pages: a FLICK of a thumbstick to the left or right, or a TAP of a grip (the left one back, the
// right one forward - what a shoulder button does in a menu), moves one step along the window's pages:
//   Skills | Settings: View | Options | Video | Body | Life | Look | HUD | Belt | Melee | Left | Right | Guide
// (thirteen places: the Skills tab, then the Settings tab's twelve pages), and stops at the ends - one flick, one page, no wrap-around.
// Pure: no Windows, no OpenXR, no game; a frame's stick and grip values in, a step (-1, 0, +1) out (cleanroom/tests/headset_tabs_test.cpp).
//
// The flick has a dead zone and a re-arm: the stick must be pushed past kFlickOn, mostly sideways (more along x than along y), and then
// brought back inside kFlickOff - to the middle - before the next one counts; held over, it is one step. A stick that is already pushed when
// the window opens (the player was turning) is not a flick: it has to come back first. The grip tap is a press let go within kTapMaxS with
// the other grip never down during it: both grips together are the chord (settings panel's: both grips, then a stick click), never a page.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "hset_defs.h"

namespace wheel {

constexpr int kFlipPlaces = 1 + hset::kPageCount;   // Skills, then the Settings tab's pages
constexpr float kFlickOn = 0.65f, kFlickOff = 0.25f;
constexpr float kFlickSideways = 1.6f;   // along x at least this many times along y
constexpr float kGripOn = 0.55f, kGripOff = 0.20f;
constexpr double kTapMaxS = 0.30;

// Where a step of `dir` (-1 / +1) goes from `place` (0..kFlipPlaces - 1): the next place, or the same one at an end.
inline int FlipTo(int place, int dir) { return std::clamp(place + (dir < 0 ? -1 : dir > 0 ? 1 : 0), 0, kFlipPlaces - 1); }

class Flipper {
    bool latched_[2] = {false, false};   // the stick has been pushed (counted or not) and is not back in the middle yet
    bool first_ = true;                  // the first frame: what is pushed now is not a flick
    bool gripDown_[2] = {false, false};
    double gripAt_[2] = {0, 0};
    bool shared_[2] = {false, false};    // the other grip was down at some time during this press

public:
    void Reset() { *this = Flipper{}; }

    // One frame while the window is up. `stickX/stickY` each hand's thumbstick (x right, y up), `squeeze` each grip 0..1, `time` seconds.
    // The step: -1 back, +1 forward, 0 none.
    int Step(double time, const float stickX[2], const float stickY[2], const float squeeze[2]) {
        float ax[2], ay[2];
        bool down[2];
        for (int h = 0; h < 2; ++h) {
            const float x = std::isfinite(stickX[h]) ? stickX[h] : 0.0f, y = std::isfinite(stickY[h]) ? stickY[h] : 0.0f;
            ax[h] = std::fabs(x);
            ay[h] = std::fabs(y);
            down[h] = squeeze[h] > (gripDown_[h] ? kGripOff : kGripOn);
        }
        if (first_) {   // what is pushed or squeezed as the window opens is not a flick or a tap: it has to be let go first
            first_ = false;
            for (int h = 0; h < 2; ++h) {
                latched_[h] = std::max(ax[h], ay[h]) > kFlickOff;
                gripDown_[h] = shared_[h] = down[h];
            }
            return 0;
        }
        int step = 0;
        for (int h = 0; h < 2; ++h) {   // the sticks
            if (latched_[h]) {
                if (std::max(ax[h], ay[h]) <= kFlickOff) latched_[h] = false;
            } else if (ax[h] >= kFlickOn && ax[h] >= kFlickSideways * ay[h]) {
                latched_[h] = true;
                if (step == 0) step = stickX[h] < 0.0f ? -1 : 1;
            }
        }
        for (int h = 0; h < 2; ++h) {   // the grips: a tap of one, alone
            if (down[h] && !gripDown_[h]) {   // pressed now
                gripAt_[h] = time;
                shared_[h] = down[1 - h];
                if (down[1 - h]) shared_[1 - h] = true;
            } else if (down[h] && down[1 - h]) {
                shared_[h] = true;
            } else if (!down[h] && gripDown_[h]) {   // let go: a tap if it was short and alone
                if (!shared_[h] && time - gripAt_[h] <= kTapMaxS && step == 0) step = h == 0 ? -1 : 1;
                shared_[h] = false;
            }
        }
        for (int h = 0; h < 2; ++h) gripDown_[h] = down[h];
        return step;
    }
};

}  // namespace wheel
