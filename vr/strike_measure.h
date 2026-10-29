#pragma once
// strike_measure.h - [debug] strike_measure: what a strike delivers into a monster's body, measured on the weight model's contact (docs/notes/strike_force.md, step 2:
// the measuring pass). The thresholds of hit by touch by force are to be set from these numbers - the author records real blows (a light tap, a hard two-handed
// blow, a slide along the body, a drop) and the log has each one. Pure: the weight model hands a Step per weight step, Tracker makes strikes of them.
//
// The measure: the bodies do not move, so every bit of motion the contact takes out of the weapon's (contact::Report::stepImpulse - its normal kicks) is impulse
// the weapon delivered into the body. Its first step is the ARRIVAL - the weapon's momentum, mass times closing speed, normal to the surface; the steps after
// it the DRIVE - the hands' pull toward the controllers pressing it on, step by step (two hands, two pulls); the CUT - the surface's normal force (the step's
// impulse over its dt) times the blade's slide along it, J; a weapon in a wound (stab_in) - the joint's force along it. Gravity is no part of any of it: the
// contact meets only a weapon in a hand (a weapon let go falls through the bodies), and the arrival is its speed, not its weight.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace strikemeasure {

constexpr double kGapS = 0.06;      // s without a contact on that monster: the strike is over
constexpr double kWindowS = 0.15;   // s from the first contact: the window a strike is judged in (its total there is told apart)
constexpr double kLongestS = 1.0;   // s: a contact held longer is told and begun again (a weapon resting on a body)

// One weight step's contact with one monster (Strikes below takes every monster the step met: a blade caught between two strikes both).
struct Step {
    double t = 0;            // s (the caller's clock)
    double dt = 0;           // s, the step
    uint32_t id = 0;         // the monster (0: no contact this step)
    int bone = -1;           // the limb (-1: its own capsule)
    double impulse = 0;      // N s into the body this step (contact::Report::stepImpulse)
    double friction = 0;     // N s the slide was held back (stepFriction)
    double slid = 0;         // m slid along it (stepSlid)
    double push = 0;         // kg m the weapon put back out of it (stepPush; its push over dt is the same impulse again - told, not added)
    double approach = 0;     // m/s the closing speed (the strongest point's)
    double stabForce = 0;    // N: the stab's joint force while the weapon is in this monster (0 otherwise)
    int hands = 0;           // hands on the weapon
    unsigned handMask = 0;   // ... which: bit 0 the right, bit 1 the left
    int crowd = 1;           // monsters the weapon touched this step (this one among them)
    float along = -1.0f;     // where along the weapon it touched (0 the butt end, 1 the tip; < 0 not known) - [weight] strike_by_force's head rule
};

// A strike: one contact with one monster, from the first step to the gap.
struct Strike {
    uint32_t id = 0;
    int bone = -1;
    int hands = 0;            // the most hands on it during the strike
    unsigned handMask = 0;    // ... which hands held it (bit 0 the right, bit 1 the left)
    int crowd = 1;            // the most monsters the weapon touched at once during it
    double startS = 0, durS = 0;
    int steps = 0;
    double arrival = 0;       // N s: the first step's impulse (the weapon's momentum, normal to the surface)
    double approach = 0;      // m/s: the first step's closing speed
    double drive = 0;         // N s: the steps after the first (the hands' pressing)
    double window = 0;        // N s: arrival + drive within kWindowS of the first contact
    double cut = 0;           // J: the normal force times the slide, summed
    double slid = 0;          // m slid along the body
    double friction = 0;      // N s held back along it
    double push = 0;          // kg m put back out of it
    double stab = 0;          // N s: the stab's joint force over the steps it was in
    double peakForce = 0;     // N: the largest step impulse over its dt
    // [weight] strike_by_force (vr/strike_force.h) judges it while it is on: which strike (a new number each contact), the furthest toward the tip it touched,
    // and the drive of about the last kWindowS (the hands pushing it on now: an exponential average with that time)
    uint32_t serial = 0;
    float along = -1.0f;
    double recent = 0;
    double Total() const { return arrival + drive + stab; }
};

class Tracker {
public:
    // A step; true when a strike has ended (Last() is it). A step on another monster ends the one on before it.
    bool Feed(const Step& s) {
        bool ended = false;
        const bool touching = s.id != 0 && (s.impulse > 0 || s.slid > 0 || s.stabForce > 0 || s.push > 0);
        if (on_ && (!touching || s.id != cur_.id)) {
            if (!touching && s.t - lastT_ < kGapS) return false;   // (a short gap: the same strike)
            End();
            ended = true;
        }
        if (on_ && s.t - cur_.startS > kLongestS) {   // resting on it: told, and begun again
            End();
            ended = true;
        }
        if (!touching) return ended;
        if (!on_) {
            on_ = true;
            cur_ = Strike {};
            cur_.id = s.id;
            cur_.bone = s.bone;
            cur_.startS = s.t;
            cur_.arrival = s.impulse;
            cur_.approach = s.approach;
            cur_.serial = NextSerial();
        } else {
            cur_.drive += s.impulse;
            cur_.recent = cur_.recent * std::exp(-std::max(1e-4, s.dt) / kWindowS) + s.impulse;
        }
        if (s.along >= 0.0f) cur_.along = std::max(cur_.along, s.along);
        const double dt = std::max(1e-4, s.dt);
        cur_.stab += s.stabForce * dt;
        if (s.t - cur_.startS <= kWindowS + 1e-9) cur_.window += s.impulse + s.stabForce * dt;
        cur_.cut += (s.impulse / dt) * s.slid;
        cur_.slid += s.slid;
        cur_.friction += s.friction;
        cur_.push += s.push;
        cur_.peakForce = std::max(cur_.peakForce, s.impulse / dt);
        cur_.hands = std::max(cur_.hands, s.hands);
        cur_.handMask |= s.handMask;
        cur_.crowd = std::max(cur_.crowd, s.crowd);
        ++cur_.steps;
        cur_.durS = s.t - cur_.startS;
        lastT_ = s.t;
        return ended;
    }
    const Strike& Last() const { return last_; }
    bool On() const { return on_; }
    const Strike& Current() const { return cur_; }   // (while On: the strike so far)

private:
    static uint32_t NextSerial() {
        static uint32_t n = 0;
        return ++n ? n : ++n;   // (never 0)
    }
    void End() {
        last_ = cur_;
        on_ = false;
    }
    Strike cur_, last_;
    bool on_ = false;
    double lastT_ = 0;
};

// The strikes on every monster at once: a Tracker for each monster the weapon is on (a blade caught between two: each its own strike, judged on its own).
class Strikes {
public:
    static constexpr int kMax = 4;
    // A weight step: the monsters it met (steps[i].id, each once), at t. The strikes that ended go to `ended` (up to kMax); returns how many.
    int Feed(const Step* steps, int n, double t, double dt, Strike* ended) {
        int out = 0;
        bool met[kMax] = {};
        for (int i = 0; i < n; ++i) {
            if (steps[i].id == 0) continue;
            int k = Find(steps[i].id);
            if (k < 0) k = Find(0);   // a free tracker (all busy: a fifth monster at once goes unmeasured)
            if (k < 0) continue;
            id_[k] = steps[i].id;
            met[k] = true;
            if (tr_[k].Feed(steps[i]) && out < kMax) ended[out++] = tr_[k].Last();
        }
        for (int k = 0; k < kMax; ++k) {   // the monsters not met this step: a gap on theirs
            if (met[k] || id_[k] == 0) continue;
            Step none;
            none.t = t;
            none.dt = dt;
            if (tr_[k].Feed(none) && out < kMax) ended[out++] = tr_[k].Last();
            if (!tr_[k].On()) id_[k] = 0;
        }
        return out;
    }
    // The strikes on now (each monster's so far), up to cap; returns how many.
    int Live(Strike* out, int cap) const {
        int n = 0;
        for (int k = 0; k < kMax && n < cap; ++k)
            if (id_[k] != 0 && tr_[k].On()) out[n++] = tr_[k].Current();
        return n;
    }

private:
    int Find(uint32_t id) const {
        for (int k = 0; k < kMax; ++k)
            if (id_[k] == id) return k;
        return -1;
    }
    Tracker tr_[kMax];
    uint32_t id_[kMax] = {};
};

}  // namespace strikemeasure
