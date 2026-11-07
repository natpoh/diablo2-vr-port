#pragma once
// swing_reason.h - [hud] swing_timer_reasons: why a VR first person touch did not become the game's attack, as the swing timer shows it
// (vr/swing_timer.h's refusal texts). Serg 2026-11-06, after zbiromir's 0.205.0 log (a bardiche: nine shaft contacts - a push, no hit - and four
// touches handed to the game) and a streamer saying the same live, "the sword does no damage": the player should see in the headset what went
// wrong with each swing - the shaft, too slow, not ready ... - in the hit / miss window the swing timer already has.
//
// vr/melee_touch.cpp refuses a touch in a few places; each maps here to one word and, where it has them, the swing's number against the gate's:
//   - a swing's contact (the stroke engine, vr/strike_direction.h): slower where it touched than the least speed (melee_min_speed[_<kind>]) -
//     "slow 3.1/5.0"; the strike rules - the shaft below a weapon's head (melee_metal_head) "shaft", the butt behind the front hand
//     (melee_not_butt) "handle", outside the head's share of the blade (melee_head_only) "not the tip", not toward the monster or backward
//     (melee_toward, melee_not_backward) "wrong way", a swing too small for the kind (melee_amp) "short 12/30" (cm);
//   - a thrust's contact (vr/thrust_stroke.h): the same, and a thrust too short "short" (cm along the blade), a push not a jab "push" (m/s^2);
//   - a strike judged by its force ([weight] strike_by_force, vr/strike_force.h - a weapon the weight model holds): too weak "weak 0.62/1.10"
//     (N s), the shaft or the butt by where along the weapon it met the body;
//   - a blade that entered a body with no stroke on (the engine never started one): slower than the stroke's speed "slow", else "short" -
//     shorter than melee_stroke_start_cm;
//   - a monster's shield or weapon stopped it ([weight] monster_shields) "shield";
//   - a touch dropped because the hero's attack is not over ([shapeshift] claw_hits' recovery, Direct hit's least interval or the hero's mode on
//     the server): "too soon", as the timer's own red.
// The game's outcomes (hit, crit, miss by the roll, block ...) come after a touch was handed over - vr/swing_timer.h's labels.
// Pure: no game, no Windows; the test is cleanroom/tests/swing_reason_test.cpp.

#include <cstdint>

#include "strike_direction.h"
#include "strike_force.h"
#include "swing_timer.h"
#include "thrust_stroke.h"

namespace swingreason {

using swingtimer::Text;

// One refusal: the word, and the numbers in hundredths (swingtimer::Numbers; got -1 when not known).
struct Refusal {
    Text text = Text::None;
    int32_t got = -1, need = 0;
    bool operator==(const Refusal& o) const { return text == o.text && got == o.got && need == o.need; }
};
inline Refusal Make(Text t, float got, float need) { return {t, swingtimer::Hundredths(got), swingtimer::Hundredths(need)}; }
inline Refusal Make(Text t) { return {t, -1, 0}; }

// Where it met the body, or which way it moved - the strike rules' verdict (both the swing's and the thrust's). None for Counts.
inline Refusal FromRule(const strikedir::Judgement& jd) {
    switch (jd.why) {
    case strikedir::Why::Shaft: return Make(Text::Shaft);
    case strikedir::Why::Butt: return Make(Text::Handle);
    case strikedir::Why::NotHead: return Make(Text::NotTip);
    case strikedir::Why::Away:
    case strikedir::Why::Backward: return Make(Text::Away);
    case strikedir::Why::TooSmall: return Make(Text::Short, jd.travelCm, jd.needCm);
    default: return {};
    }
}

// A swing's contact: `speed` where it touched, `least` the stroke's least speed (the kind's own or the general one) - the speed first, as
// vr/melee_touch.cpp judges it, then the strike rules.
inline Refusal FromSwing(float speed, float least, const strikedir::Judgement& jd) {
    if (speed < least) return Make(Text::Slow, speed, least);
    return FromRule(jd);
}

// A thrust's contact (only a movement along the blade gets here: NotAlong and Turned are left to the swing, quietly): the thrust's least
// speed, the strike rules, then the thrust's own distance and its jab.
inline Refusal FromThrust(float speed, float least, const strikedir::Judgement& jd, const thrust::Verdict& v) {
    if (speed < least) return Make(Text::Slow, speed, least);
    const Refusal r = FromRule(jd);
    if (r.text != Text::None) return r;
    if (v.why == thrust::Why::Weak) return Make(Text::Push, v.accel, v.needAccel);
    if (v.why == thrust::Why::Short) return Make(Text::Short, v.alongCm, v.needCm);
    return {};
}

// A strike judged by its force: too weak (its score against the threshold), or where along the weapon it met the body (shaft, butt).
inline Refusal FromForce(const strikeforce::Verdict& v, bool shaft, bool butt) {
    if (!v.Counts()) return v.why == strikeforce::Why::TooWeak ? Make(Text::Weak, v.score, v.need) : Refusal{};
    if (shaft) return Make(Text::Shaft);
    if (butt) return Make(Text::Handle);
    return {};
}

// A blade that entered a monster's body while its engine had no stroke on: it moved `speed`, a stroke needs `strokeSpeed` (and `onsetCm` of
// movement first, melee_stroke_start_cm) - slow below the speed, short when fast enough but not far enough.
inline Refusal FromNoStroke(float speed, float strokeSpeed, float onsetCm) {
    if (speed < strokeSpeed) return Make(Text::Slow, speed, strokeSpeed);
    return {Text::Short, -1, swingtimer::Hundredths(onsetCm)};
}

// The log's word for each (vr/swing_timer_wire.cpp's rate-limited line).
inline const char* LogText(Text t) {
    switch (t) {
    case Text::Shaft: return "the shaft below the head touched (a push, no hit)";
    case Text::Handle: return "the butt behind the front hand touched";
    case Text::NotTip: return "outside the head of the blade";
    case Text::Slow: return "too slow where it touched";
    case Text::Weak: return "a strike too weak";
    case Text::Push: return "a push, not a jab";
    case Text::Short: return "a swing or thrust too short";
    case Text::Away: return "not toward the monster";
    case Text::Shield: return "the monster's shield or weapon stopped it";
    case Text::TooSoon: return "the hero's attack is not over";
    default: return "?";
    }
}

}  // namespace swingreason
