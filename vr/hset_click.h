#pragma once
// The two pieces of hands the settings pages have in every place they are hosted - the panel of their own (hset_panel.h) and the
// skill panel window's Settings tab (wheel_controller.h): the CLICK (a trigger edge on a button presses it, a held trigger on a stepper
// goes on pressing it, a held trigger on the slider's track moves the thumb with the beam, also off the sheet's edge) and the CHORD
// (both grips squeezed a third of a second, then a thumbstick click: the one silent control in the layouts). Pure: no Windows, no
// OpenXR; a frame's values in, what to do out (tools/hset_test.cpp, cleanroom/tests/headset_tabs_test.cpp).

#include <cstdint>

#include "hset_model.h"
#include "hset_ray.h"

namespace hset {

class Clicker {
    Hit hold_;
    double nextRepeat_ = 0;
    bool held_ = false;   // a trigger pressed on a button is still down

public:
    void Reset() { *this = Clicker{}; }
    // `hover`: the button under the beam (None off the pages). `rise`, `held`: the pointing hand's trigger went down this frame / is down.
    // `onSheet`: the beam is on the pages. `plane`: the beam meets the pages' plane in front of the hand (also past the sheet's edge);
    // `sheetX`: where, in the sheet's pixels. `ms`: a clock.
    Action Step(Model& m, const Hit& hover, bool onSheet, bool rise, bool held, bool plane, float sheetX, double ms) {
        Action act;
        if (rise && onSheet) {
            act = m.Click(hover);
            hold_ = hover;
            nextRepeat_ = ms + kRepeatFirstMs;
            held_ = true;
        } else if (held_ && held && Repeats(hold_) && hover == hold_) {
            if (ms >= nextRepeat_) {
                act = m.Click(hover);
                nextRepeat_ += kRepeatMs;
                if (nextRepeat_ < ms) nextRepeat_ = ms + kRepeatMs;
            }
        } else if (held_ && held && hold_.kind == Kind::Slider && hold_.index >= 0 && plane) {
            // the slider's thumb follows the beam along the track for as long as the trigger is held, also when the beam has left it
            act = m.Click(SliderAt(sheetX));
        }
        if (!held) held_ = false;
        return act;
    }
};

class Chord {
    double gripSince_ = 0;   // both grips down since (0: not both)
    bool gripDown_[2] = {false, false};
    bool prevStick_[2] = {false, false};   // a thumbstick click as of the frame before (a press is its edge)

public:
    struct Out {
        bool armed = false;   // both grips have been squeezed long enough: a thumbstick click is the chord's now, not the game's
        bool edge = false;    // a thumbstick click went down this frame (either hand): with `armed`, the chord is made
    };
    void Reset() { *this = Chord{}; }
    // Both grips being squeezed (or not yet long enough): something of the chord is going on.
    bool Arming() const { return gripSince_ != 0; }
    // `enabled` false: the grips count as let go. `squeeze`: each hand's grip 0..1; `stick`: each hand's thumbstick click is down.
    Out Step(double ms, bool enabled, const float squeeze[2], const bool stick[2]) {
        bool gripsDown = true;
        for (int h = 0; h < 2; ++h) {
            const bool g = enabled && squeeze[h] > (gripDown_[h] ? kReleaseAt : kPressAt);
            gripDown_[h] = g;
            gripsDown = gripsDown && g;
        }
        if (!gripsDown) gripSince_ = 0;
        else if (gripSince_ == 0) gripSince_ = ms;
        Out o;
        o.armed = gripsDown && ms - gripSince_ >= kGripHoldMs;
        for (int h = 0; h < 2; ++h) {
            if (stick[h] && !prevStick_[h]) o.edge = true;
            prevStick_[h] = stick[h];
        }
        return o;
    }
};

}  // namespace hset
