// quiver_ctl.h - the quiver's controller: the hands behind the shoulder, the steps, the haptic ticks and the selection. Pure (no game, no clock, no
// globals); unit test cleanroom/tests/quiver_ctl_test.cpp. vr/quiver_wire.cpp feeds it a headset frame and carries its answer out.
//
// The reach: a hand behind a shoulder (the volume of the shoulder swap, vr/shoulder_swap.h) - the javelin bag on the back. In it, a TAP of that hand's
// trigger steps the choice: the right hand to the NEXT skill, the left to the PREVIOUS one, with a short pulse in that hand (two pulses when the choice is
// back at the plain throw). The trigger of a hand in the reach is the quiver's, not the layout's (directinput::Take): the right trigger does not attack
// from behind the head, the left does not hold the second skill row. A trigger already held when the hand comes in does nothing until it is let go.
// A remapped button of the layout (action:quivernext / action:quiverprev) steps the same way from anywhere.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "quiver.h"
#include "shoulder_swap.h"

namespace quiver {

constexpr float kTriggerDown = 0.60f, kTriggerUp = 0.30f;
constexpr double kReachDwellS = 0.10;      // in the reach this long before its trigger is the quiver's
constexpr double kTapMaxS = 0.45;     // a press longer than this is not a tap

struct HandSample {
    bool tracked = false;
    float pos[3] = {};      // from the head, in the frame of its turn: x right, y up, z back
    float trigger = 0.0f;   // 0..1
};

// One hand's reach and trigger.
class ZoneTap {
public:
    struct Out {
        bool inReach = false;
        bool claim = false;   // the trigger is the quiver's this frame
        bool tap = false;     // exactly one frame per tap
    };
    void Reset() { *this = ZoneTap(); }
    Out Step(double t, bool allowed, const HandSample& h) {
        Out o;
        if (!allowed || !h.tracked || !std::isfinite(h.trigger)) { Reset(); return o; }   // nothing is claimed, nothing is under way
        if (!shoulderswap::Near(h.pos)) {   // out of the reach: the press in progress is dropped; a claimed trigger stays claimed until it is let go
            inSince_ = -1.0;
            armed_ = false;
            seenUp_ = false;
            if (h.trigger < kTriggerUp) held_ = false;
            o.claim = held_;
            return o;
        }
        o.inReach = true;
        if (inSince_ < 0.0) { inSince_ = t; seenUp_ = h.trigger < kTriggerUp; }
        else if (h.trigger < kTriggerUp) seenUp_ = true;
        const bool ready = t - inSince_ >= kReachDwellS;
        if (!armed_) {
            if (ready && seenUp_ && h.trigger > kTriggerDown) { armed_ = true; downAt_ = t; held_ = true; }
        } else if (h.trigger < kTriggerUp) {
            armed_ = false;
            held_ = false;
            if (t - downAt_ <= kTapMaxS) o.tap = true;
        }
        o.claim = ready || armed_ || held_;
        return o;
    }

private:
    double inSince_ = -1.0, downAt_ = 0.0;
    bool armed_ = false, seenUp_ = false, held_ = false;
};

struct FrameIn {
    double t = 0.0;
    bool play = false;                 // focused, no menu, VR first person
    bool zoneOn = true;                // the reach is switched on at all
    Kind weapon = Kind::None;          // the weapon hand's item
    bool otherHandThrown = false;
    const Learned* learned = nullptr;  // the skills the hero has (the skill wheel's catalog)
    size_t nLearned = 0;
    bool learnedKnown = false;         // the list is a fresh read
    HandSample hand[2];                // 0 left, 1 right
    bool rightTriggerIsThrows = false; // [hands] throw_gesture: the right trigger is the throw's own control
    int layoutStep = 0;                // +1 / -1 from the layout's action:quivernext / action:quiverprev
    uint32_t session = 0, epoch = 0;
};

struct FrameOut {
    bool take[2] = {};         // this hand's trigger is the quiver's (Take it from the layout)
    int tick[2] = {};          // haptic pulses to give this hand now
    bool changed = false;      // the choice changed this frame (a step, or the pool changed under it)
    bool stepped = false;      // ... by a step of the player's
    bool inReach[2] = {};
};

class Controller {
public:
    FrameOut Step(const FrameIn& in) {
        FrameOut out;
        if (!seen_ || in.session != session_) { Reset(); seen_ = true; session_ = in.session; epoch_ = in.epoch; }
        else if (in.epoch != epoch_) { epoch_ = in.epoch; zone_[0].Reset(); zone_[1].Reset(); }
        const bool thrown = IsThrownKind(in.weapon) && in.weapon != Kind::Potion;
        // the pool: rebuilt when the weapon, the other hand or the learned list changed (a list not read yet builds the default alone, and again when it comes)
        const uint64_t sigNow = in.learnedKnown ? Signature(in) : sig_;
        if (!built_ || in.weapon != weapon_ || in.otherHandThrown != other_ || sigNow != sig_ || (in.learnedKnown && !hadList_)) {
            if (!thrown) {
                out.changed = sel_.Id() != kDefault;
                sel_.Rebuild(Pool{});
            } else {
                Needs need;
                need.weapon = in.weapon;
                need.otherHandThrown = in.otherHandThrown;
                out.changed = sel_.Rebuild(Build(need, in.learnedKnown ? in.learned : nullptr, in.learnedKnown ? in.nLearned : 0));
            }
            weapon_ = in.weapon;
            other_ = in.otherHandThrown;
            sig_ = sigNow;
            hadList_ = in.learnedKnown;
            built_ = true;
        }
        const bool live = thrown && in.play;
        int step = live ? in.layoutStep : 0;
        int tapHand = step ? 1 : -1;   // a layout step is given to the weapon's hand (the right)
        for (int hand = 0; hand < 2; ++hand) {
            const bool allowed = live && in.zoneOn && sel_.CanCycle() && !(hand == 1 && in.rightTriggerIsThrows);
            const ZoneTap::Out z = zone_[hand].Step(in.t, allowed, in.hand[hand]);
            out.inReach[hand] = z.inReach;
            out.take[hand] = z.claim;
            if (z.tap && !step) { step = hand == 1 ? +1 : -1; tapHand = hand; }
        }
        if (step && sel_.Step(step)) {
            out.changed = true;
            out.stepped = true;
            out.tick[tapHand] = sel_.Id() == kDefault ? 2 : 1;
        }
        return out;
    }
    const Selector& Sel() const { return sel_; }
    void Reset() {
        zone_[0].Reset();
        zone_[1].Reset();
        sel_ = Selector();
        built_ = false;
        hadList_ = false;
        weapon_ = Kind::None;
        other_ = false;
        sig_ = 0;
    }

private:
    static uint64_t Signature(const FrameIn& in) {
        uint64_t h = 1469598103934665603ull;
        for (size_t i = 0; i < in.nLearned && in.learned; ++i) {
            h = (h ^ (uint64_t)(uint32_t)in.learned[i].id) * 1099511628211ull;
            h = (h ^ (uint64_t)(in.learned[i].level > 0 ? 1u : 0u)) * 1099511628211ull;
        }
        return h;
    }
    ZoneTap zone_[2];
    Selector sel_;
    bool seen_ = false, built_ = false, hadList_ = false, other_ = false;
    uint32_t session_ = 0, epoch_ = 0;
    Kind weapon_ = Kind::None;
    uint64_t sig_ = 0;
};

}  // namespace quiver
