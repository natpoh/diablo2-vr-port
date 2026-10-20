// arrow_fire_cut.h - [bow] arrow_fire_cut: the nocked arrow's own effect (the fire, the frost or the arcane light the effect draws on the bow's arrow, [bow] arrow_fx) goes out at
// the release that shoots - at once, or [bow] arrow_fire_cut_ms after it - instead of staying on the arrow in the hand until the game hides that arrow. Visual only: the game's
// missile, its effects and its damage are the game's and are never touched; this only says whether OUR light is drawn. Pure: no game calls, no globals, no windows.h
// (cleanroom/tests/arrow_fire_cut_test.cpp feeds it synthetic frames).
//
// Once out, the effect stays out until the next arrow: the string taken again (a new draw), the game showing an arrow again after it had none (the arrow bone was not read
// after the shot - the game hid the shot arrow - and is read again: the next one nocked at rest), or another skill chosen (it lights as a choice always does). A new draw, or
// the next arrow shown, before the delay ran out drops the pending cut: that arrow is the next one.
#pragma once

#include <algorithm>
#include <cmath>

namespace arrowcut {

inline constexpr float kMaxDelayMs = 2000.0f;   // the slider's top

inline double DelayS(float ms) { return std::isfinite(ms) ? std::clamp(ms, 0.0f, kMaxDelayMs) * 0.001 : 0.0; }

struct In {
    double t = 0.0;          // seconds, the frame's time
    bool on = true;          // [bow] arrow_fire_cut
    double delayS = 0.0;     // [bow] arrow_fire_cut_ms, in seconds (DelayS)
    bool shot = false;       // a release that shoots happened since the last frame
    bool drawing = false;    // the string is taken now
    bool arrowRead = false;  // the bow's own arrow is read now (skel::BowArrow)
    int skill = -1;          // the chosen arrow skill (-1 none read now: no effect is drawn then, and it changes nothing here)
};

class Cut {
public:
    // One frame: true while the arrow's effect may be drawn, false while it is cut.
    bool Step(const In& in) {
        if (!in.on) { Reset(); skill_ = in.skill; drawingWas_ = in.drawing; return true; }
        if (in.skill >= 0 && in.skill != skill_) { out_ = pending_ = false; skill_ = in.skill; }   // another skill chosen: it lights (a moment of none read is not a choice)
        const bool drawStart = in.drawing && !drawingWas_;
        drawingWas_ = in.drawing;
        if (drawStart && !in.shot) out_ = pending_ = false;                       // the next arrow on the string
        if (in.shot) { pending_ = true; out_ = hidden_ = false; fireAt_ = in.t + std::max(in.delayS, 0.0); }
        if (pending_ || out_) {
            if (!in.arrowRead) hidden_ = true;                                    // the game took the shot arrow away
            else if (hidden_) out_ = pending_ = false;                            // ... and shows the next one: it lights (a cut still waiting is dropped)
        }
        if (pending_ && in.t >= fireAt_) { pending_ = false; out_ = true; }
        return !out_;
    }
    bool Out() const { return out_; }
    bool Pending() const { return pending_; }
    void Reset() { out_ = pending_ = hidden_ = false; fireAt_ = 0.0; }

private:
    bool out_ = false, pending_ = false, hidden_ = false, drawingWas_ = false;
    double fireAt_ = 0.0;
    int skill_ = -1;
};

}  // namespace arrowcut
