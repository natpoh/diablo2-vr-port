#pragma once
// The trigger casts the slot the wheel picked: while a slot is picked, a press of the pointing hand's trigger is the
// game's own pad button of that slot (B X Y RB; the left trigger with it for the second row), held while the trigger is.
// The cast itself is the game's - its pad poll, its skill, its animation, its mana and cooldown. This only says which pad
// buttons are down and which controls the layout must let go of so its attack (pad A) does not fire with it.
// Pure: a trigger value in, pad buttons out.
//
// The game reads the left trigger apart from the face button: the modifier comes first, by itself, for about a game
// tick, then the button (d2r-pcvr's skill_pad.h found the same on its pad path; MIT). Hence kLeadS.

#include <cmath>
#include <cstdint>

#include "wheel_types.h"

namespace wheel {

constexpr float kTriggerOn = 0.55f, kTriggerOff = 0.20f;   // down past the first, up below the second
constexpr double kLeadS = 0.09;       // the left trigger alone before the button, for the second row
constexpr double kMinPressS = 0.12;   // a tap is held at least this long: the game's poll never misses it
constexpr uint32_t kHoldMs = 80;      // each frame's pulse; the press ends this long after the last frame that refreshed it

class CastGate {
    bool down_ = false;        // the trigger, with hysteresis
    bool armed_ = false;       // it was up since the last time the gate was allowed: a held trigger does not start a cast
    bool casting_ = false;
    int slot_ = -1;            // the slot this cast started with (a new pick mid-hold does not switch it)
    double startAt_ = 0;

public:
    struct In {
        double time = 0;
        int slot = -1;           // the picked slot, -1 = the basic attack
        float trigger = 0;       // the pointing hand's trigger, 0..1
        bool allowed = false;    // focused, in a game, no menu, the wheel closed
    };
    struct Out {
        bool takeTrigger = false;      // the pointing hand's trigger is the cast's: the layout sees it let go
        bool takeLeftTrigger = false;  // and the left trigger too: it must not turn a first-row cast into a second-row one
        uint16_t buttons = 0;          // pad buttons down now (XInput bits)
        uint8_t lt = 0;
        bool casting = false;
    };

    void Reset() { *this = CastGate{}; }

    Out Step(const In& in) {
        Out out;
        const bool was = down_;
        down_ = in.trigger > (down_ ? kTriggerOff : kTriggerOn);
        if (!in.allowed) {   // (a trigger held through the wheel, a menu or a lost focus starts nothing when it comes back)
            casting_ = false;
            armed_ = false;
            return out;
        }
        if (!down_) armed_ = true;
        const bool rising = down_ && !was;
        // (only a slot whose cast is a pad button of its own: A is the layout's attack already, RT is not castable from here - wheel_types.h Cast)
        if (!casting_ && rising && armed_ && in.slot >= 0 && in.slot < kSlotCount && kSlotDefs[in.slot].cast == Cast::Pad) {
            casting_ = true;
            slot_ = in.slot;
            startAt_ = in.time;
        }
        if (casting_) {
            const double age = in.time - startAt_;
            const SlotDef& d = kSlotDefs[slot_];
            const double lead = d.alt ? kLeadS : 0.0;
            if (!down_ && age >= lead + kMinPressS) {
                casting_ = false;     // let go: the pulse already sent runs out by itself
            } else {
                out.casting = true;
                out.takeTrigger = out.takeLeftTrigger = true;
                if (d.alt) out.lt = 255;
                if (age >= lead) out.buttons = d.button;
            }
        }
        return out;
    }
};

}  // namespace wheel
