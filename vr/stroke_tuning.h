// stroke_tuning.h - the two numbers of the swing engine (vr/stroke_contact.h) the user sets live ([gameplay] melee_min_speed and
// melee_stroke_start_cm, Settings > Strikes): how fast the blade must be moving for a contact to count, and how far the
// blade must have moved for a movement to be a stroke at all. "The blade" is the weapon as drawn (stroke::Sample::room: its two ends in the
// room, the faster one - the tip of a sword - and at a touch the point that touches, stroke::ContactSpeed); only a caller with no such blade
// (the shield's thrust) has the engine fall back on the hand's travel plus its turn times a lever. Pure maths: no game calls, no globals.
//
// ONE threshold for every weapon (the kind's own blade - vr/blade_shape.h - is its only difference). The engine's gates around the
// speed are scaled with it only where they would otherwise stand in its way:
//   strokeSpeed   the minimum: a blade moving slower does nothing (a stroke already begun, or not)         = melee_min_speed
//   beginSpeed    the speed that may BEGIN a stroke, 1.5 x the minimum (1.2 m/s at the default 0.8), never above 1.2 - so a lower
//                 minimum lowers it with it (a poke at 0.5 m/s could not begin a stroke otherwise), a higher one leaves it
//   quietSpeed    the speed under which the blade is at rest and the stroke ends, 0.5625 x the minimum, never above 0.45 - the same way
//   onsetExcursion  how far the blade (its faster end; the hand with its turn weighted by the blade's length when no blade is given) must
//                 have moved within 0.12 s to begin a stroke = melee_stroke_start_cm
// At and above the default minimum (0.8 m/s) the engine is exactly what it was.
#pragma once

#include <algorithm>
#include <cmath>

#include "stroke_contact.h"

namespace stroke {

inline constexpr float kDefaultMinSpeed = 0.8f, kDefaultStartCm = 8.0f;

struct StrokeTuning {
    float minSpeed = kDefaultMinSpeed;   // m/s, 0.3 .. 8
    float startCm = kDefaultStartCm;     // cm, 2 .. 30
};

inline StrokeTuning Clamp(StrokeTuning t) {
    t.minSpeed = std::isfinite(t.minSpeed) ? std::clamp(t.minSpeed, 0.3f, 8.0f) : kDefaultMinSpeed;
    t.startCm = std::isfinite(t.startCm) ? std::clamp(t.startCm, 2.0f, 30.0f) : kDefaultStartCm;
    return t;
}

// The engine's parameters for the user's two numbers (the other gates as `base` has them).
inline Params ParamsFor(StrokeTuning t, Params base = Params{}) {
    t = Clamp(t);
    base.strokeSpeed = t.minSpeed;
    base.beginSpeed = std::min(base.beginSpeed, 1.5f * t.minSpeed);
    base.quietSpeed = std::min(base.quietSpeed, 0.5625f * t.minSpeed);
    base.onsetExcursion = t.startCm * 0.01f;
    return base;
}

}  // namespace stroke
