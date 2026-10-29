// thrust_debug.h - [debug] thrust_debug: what the last thrust (a poke along the blade, vr/thrust_stroke.h) read, against the numbers it needs, for the author to tune the
// thrust in the headset. Pure: the readings, which threshold failed, the panel's lines and their colours. No game, no clock, no drawing (vr/melee_touch.cpp fills it,
// vr/thrust_debug_wire.cpp paints it; cleanroom/tests/thrust_stroke_test.cpp tests it).
//
// Two readings are kept per hand:
//   the last TOUCH - a thrust's contact with a monster: COUNTED (blue) or REJECTED (orange) with the first threshold it failed: not straight (the part of the path along the
//     blade), too slow (the speed where it touched), too short (the cm along the blade), no accel (the peak acceleration), not toward the monster, not the head;
//   the last JAB - the latest movement the thrust engine took for a stroke, touching or not, with its peaks (speed, cm, the part along the blade, acceleration): what a
//     real jab reads before a monster is near, and whether it would count.
// Each number is shown with its threshold, and whether the threshold is the weapon kind's own or the general one. The colours are the thrust's own (blue passes, orange fails),
// apart from the swing's green / red. [debug] thrust_debug_only: a movement that is not straight enough to be a thrust (a swing, an overhead blow) is not shown at all (Shown).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "strike_direction.h"
#include "thrust_stroke.h"

namespace thrustdbg {

enum class Fail : uint8_t { None, NotStraight, Slow, Short, NoAccel, Away, NotHead, Turned };
inline const char* FailText(Fail f) {
    switch (f) {
        case Fail::NotStraight: return "not straight";
        case Fail::Turned: return "the weapon turned";
        case Fail::Slow: return "too slow";
        case Fail::Short: return "too short";
        case Fail::NoAccel: return "no accel";
        case Fail::Away: return "not toward the monster";
        case Fail::NotHead: return "not the head";
        default: return "";
    }
}

struct Reading {
    bool have = false;        // anything to show
    bool contact = false;     // a monster touched (COUNTED / REJECTED); false: a jab with no monster (would count / would fail)
    bool counted = false;     // contact: it counted
    Fail fail = Fail::None;   // the first threshold that failed (contact: what refused it; a jab: what would)
    int kind = -1;            // strikedir kind
    uint32_t monster = 0;
    uint64_t stroke = 0;      // the thrust engine's stroke
    uint64_t ms = 0;          // when (the caller's clock)
    float speed = 0.0f, needSpeed = 0.0f;    // m/s: where it touched (a contact) or the jab's peak
    float cm = 0.0f, needCm = 0.0f;          // along the blade
    float alongPct = 0.0f, needAlongPct = 0.0f;
    float accel = 0.0f, needAccel = 0.0f;    // m/s^2, needAccel 0 = not asked
    float turnDeg = 0.0f, needTurnDeg = 0.0f;   // how far the weapon's axis turned in the stroke, and the most a thrust may (thrust::kMaxTurnDeg)
    bool ownSpeed = false, ownCm = false, ownAccel = false;   // the kind's own threshold (else the general one)
};

// Is this reading shown with [debug] thrust_debug_only: a thrust attempt, not a swing. A thrust attempt is a path at least half along the blade (the
// straightness slider's own floor), NOT the player's straightness threshold: with that set high (94 %, the author's 2026-10-22 test) every real poke failed
// it and was hidden - no tint, no log line - exactly when the readout was needed to show "straight 80 / 94". A swing goes across the blade and stays hidden.
inline bool Shown(const Reading& r, bool thrustsOnly) {
    return r.have && (!thrustsOnly || r.fail != Fail::NotStraight || r.alongPct >= thrust::kAlongFloor);
}

// The numbers a thrust of `kind` needs, and whether each is the kind's own.
inline void Needs(const thrust::Rule& r, int kind, Reading* out) {
    const thrust::Numbers n = thrust::NumbersFor(r, kind);
    out->kind = kind;
    out->needSpeed = n.minSpeed;
    out->needCm = n.cm;
    out->needAccel = n.minAccel;
    out->needAlongPct = std::clamp(r.alongPct, thrust::kAlongFloor, thrust::kAlongCeil);
    out->needTurnDeg = thrust::kMaxTurnDeg;
    const bool k = kind >= 0 && kind < strikedir::kKinds;
    out->ownSpeed = k && r.kindSpeed[static_cast<size_t>(kind)] >= thrust::kMinSpeedFloor;
    out->ownCm = k && r.kindCm[static_cast<size_t>(kind)] >= thrust::kCmFloor;
    out->ownAccel = k && r.kindAccel[static_cast<size_t>(kind)] > 0.0f;
}

// The thrust's own thresholds, in the order the touch checks them: straight (the part along the blade), the speed, the distance, the acceleration. The strike rules
// (toward the monster, the head) are the caller's (Contact).
inline Fail FirstFail(const Reading& r) {
    if (r.alongPct < r.needAlongPct) return Fail::NotStraight;
    if (r.turnDeg > r.needTurnDeg) return Fail::Turned;
    if (r.speed < r.needSpeed) return Fail::Slow;
    if (r.cm < r.needCm) return Fail::Short;
    if (r.needAccel > 0.0f && r.accel < r.needAccel) return Fail::NoAccel;
    return Fail::None;
}

// A thrust's contact with a monster: its numbers, the speed where it touched, the strike rules' verdict on it.
inline Reading ContactReading(const thrust::Rule& r, int kind, const thrust::Verdict& v, float speedAt, strikedir::Why strike, uint32_t monster, uint64_t stroke, uint64_t ms) {
    Reading o;
    Needs(r, kind, &o);
    o.have = o.contact = true;
    o.monster = monster;
    o.stroke = stroke;
    o.ms = ms;
    o.speed = speedAt;
    o.cm = v.alongCm;
    o.alongPct = v.share * 100.0f;
    o.accel = v.accel;
    o.turnDeg = v.turnDeg;
    // the touch's own order: not along the blade first (then the swing judges it), the weapon turned, the speed where it touched, the strike rules, the distance, the acceleration
    if (v.why == thrust::Why::NotAlong) o.fail = Fail::NotStraight;
    else if (v.why == thrust::Why::Turned) o.fail = Fail::Turned;
    else if (speedAt < o.needSpeed) o.fail = Fail::Slow;
    else if (strike == strikedir::Why::Away) o.fail = Fail::Away;
    else if (strike == strikedir::Why::NotHead) o.fail = Fail::NotHead;
    else if (v.why == thrust::Why::Short) o.fail = Fail::Short;
    else if (v.why == thrust::Why::Weak) o.fail = Fail::NoAccel;
    o.counted = o.fail == Fail::None;
    return o;
}

// A jab with no monster: its peaks against the thresholds - what it would do.
inline Reading JabReading(const thrust::Rule& r, int kind, float peakSpeed, float cm, float alongPct, float peakAccel, uint64_t stroke, uint64_t ms, float turnDeg = 0.0f) {
    Reading o;
    Needs(r, kind, &o);
    o.have = true;
    o.stroke = stroke;
    o.ms = ms;
    o.speed = peakSpeed;
    o.cm = cm;
    o.alongPct = alongPct;
    o.accel = peakAccel;
    o.turnDeg = turnDeg;
    o.fail = FirstFail(o);
    o.counted = o.fail == Fail::None;
    return o;
}

// ---- the panel's lines ----
// kPass / kFail are painted in the thrust's own tint (vr/thrust_debug_paint.cpp: blue / orange, as the blade's - apart from the swing's green / red).
enum Colour : uint8_t { kWhite = 0, kPass = 1, kFail = 2, kGrey = 3 };
constexpr size_t kLineChars = 64;
struct Line {
    char text[kLineChars] = {};
    Colour colour = kWhite;
};

// The head line and four numbers of a reading: "SPEAR - COUNTED", "SPEAR - REJECTED: too short", "SPEAR jab - would count", "SPEAR jab - would fail: too slow"; then
// "speed 1.3 / 0.8 m/s", "along 6.2 / 5 cm (own)", "straight 88 / 70 %", "accel 31 / 12 m/s2" (or "accel 31 (not asked)") - each kPass when it passes, kFail when it fails.
inline size_t Lines(const Reading& r, const char* title, Line* out, size_t max) {
    if (!out || max == 0) return 0;
    size_t n = 0;
    const auto add = [&](Colour c, const char* fmt, auto... a) {
        if (n >= max) return;
        std::snprintf(out[n].text, kLineChars, fmt, a...);
        out[n].colour = c;
        ++n;
    };
    if (!r.have) {
        add(kGrey, "%s: none yet", title);
        return n;
    }
    char kind[24];
    std::snprintf(kind, sizeof kind, "%s", r.kind >= 0 ? strikedir::KindName(r.kind) : "weapon");
    for (char* c = kind; *c; ++c) if (*c >= 'a' && *c <= 'z') *c = static_cast<char>(*c - 32);
    if (r.contact) {
        if (r.counted) add(kPass, "%s %s - COUNTED", title, kind);
        else add(kFail, "%s %s - REJECTED: %s", title, kind, FailText(r.fail));
    } else {
        if (r.counted) add(kPass, "%s %s - would count", title, kind);
        else add(kFail, "%s %s - would fail: %s", title, kind, FailText(r.fail));
    }
    const auto mark = [](bool ok) { return ok ? kPass : kFail; };
    add(mark(r.alongPct >= r.needAlongPct), " straight %.0f / %.0f %%", r.alongPct, r.needAlongPct);
    add(mark(r.turnDeg <= r.needTurnDeg), " turn %.0f / %.0f deg", r.turnDeg, r.needTurnDeg);
    add(mark(r.speed >= r.needSpeed), " speed %.2f / %.2f m/s%s", r.speed, r.needSpeed, r.ownSpeed ? " (own)" : "");
    add(mark(r.cm >= r.needCm), " along %.1f / %.0f cm%s", r.cm, r.needCm, r.ownCm ? " (own)" : "");
    if (r.needAccel > 0.0f) add(mark(r.accel >= r.needAccel), " accel %.0f / %.0f m/s2%s", r.accel, r.needAccel, r.ownAccel ? " (own)" : "");
    else add(kWhite, " accel %.0f m/s2 (not asked)", r.accel);
    return n;
}

// ---- the jab being made: its peaks while the thrust engine has a stroke on ----
// Fed every pose with the thrust engine's stroke number, its speed this step and the path's numbers; when a stroke ends (or another begins) Take gives the finished jab once.
class JabTracker {
public:
    void Reset() { *this = JabTracker{}; }
    // done: set when a jab has just ended (filled with its peaks); the caller turns it into a Reading.
    struct Peaks { uint64_t stroke = 0; float speed = 0.0f, cm = 0.0f, alongPct = 0.0f, accel = 0.0f, turnDeg = 0.0f; };
    bool Feed(uint64_t stroke, float speed, float cm, float alongPct, float accel, Peaks* done, float turnDeg = 0.0f) {
        bool ended = false;
        if (cur_.stroke && stroke != cur_.stroke) {   // the stroke that was on ended
            if (done) *done = cur_;
            ended = true;
            cur_ = {};
        }
        if (stroke) {
            cur_.stroke = stroke;
            if (std::isfinite(speed)) cur_.speed = std::max(cur_.speed, speed);
            if (std::isfinite(cm) && (cm > cur_.cm || cur_.alongPct <= 0.0f) && std::isfinite(alongPct)) {   // the part along the blade as it stood at the jab's furthest reach
                cur_.cm = std::max(cur_.cm, cm);                                                             // (not after: the pull-back before the reversal lowers it)
                cur_.alongPct = alongPct;
            }
            if (std::isfinite(accel)) cur_.accel = std::max(cur_.accel, accel);
            if (std::isfinite(turnDeg)) cur_.turnDeg = std::max(cur_.turnDeg, turnDeg);
        }
        return ended;
    }
    // The jab on now (stroke 0: none).
    const Peaks& Current() const { return cur_; }

private:
    Peaks cur_;
};

}  // namespace thrustdbg
