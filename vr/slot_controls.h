#pragma once
// The right trigger and the right A button as the game's own RT and A skill slots ([openxr] skill_rt_a, F4 only, direct input).
//
// The game's controller has six skill buttons - A X B Y RB RT - and the same six with the left trigger held (the twelve slots of wheel_types.h).
// B, X, Y and RB are already controls of a VR controller (vr/direct_layout.h). The other two were not: the layout's right trigger is the attack
// (pad A) and its A button is interact. With the key on, a pull of the right trigger is the game's own RT and a press of the right A is the game's
// own A - held while the control is - when that slot holds a skill; the left trigger held is the second layer, as it is on a pad. A slot that
// holds the plain action (nothing, the basic attack, the pick-up commands: IsPlainAction) leaves its control to the layout, which is the attack
// and interact it has always been - so a slot "set back" is today's behaviour.
//
// Nothing here casts anything: the pad buttons it says are down (pad A, the right trigger, the left trigger first) are read by the game's own
// poll, and its slot, skill, mana and cooldown are the game's. Pure: controls and the slots' contents in, pad buttons and takes out.
// The left trigger is read apart from the button by the game (about one tick first, wheel_cast.h kLeadS), hence the lead.

#include <algorithm>
#include <cstdint>

#include "wheel_cast.h"
#include "wheel_types.h"

namespace wheel {

enum class Control : uint8_t { A = 0, Trigger = 1 };

// The slot (wheel_types.h) a control presses: A is 8 (LT + A 10), the right trigger RT 9 (LT + RT 11).
constexpr int SlotOfControl(Control c, bool lt) { return (c == Control::A ? 8 : 9) + (lt ? 2 : 0); }

// Whether the slot holds a skill its control casts: the game's table was read, the slot is placed, and its content is neither empty nor the plain
// action, and the player still has it. Anything less leaves the control to the layout.
inline bool HoldsSkill(const Snapshot& s, int slot) {
    return slot >= 0 && slot < kSlotCount && CastsSkill(s.slot[slot]);
}

// The four slots a SlotControls reads, as bits of its input (bit n = slot n holds a skill), for the snapshot as it is now; 0 when it is not usable
// (not read, another session, older than maxAgeMs: the controls are the layout's).
inline uint16_t SkillMask(const Snapshot& s, uint64_t nowMs, uint64_t session, uint64_t maxAgeMs = 3000) {
    if (!Fresh(s, nowMs, session, maxAgeMs)) return 0;
    uint16_t m = 0;
    for (int slot = 8; slot < 12; ++slot)
        if (HoldsSkill(s, slot)) m |= uint16_t(1u << slot);
    return m;
}

class SlotControls {
    struct One {
        bool down = false;      // the control, with hysteresis
        bool armed = false;     // it was up since the last time it was allowed: a control held through a panel or a menu starts nothing
        bool active = false;    // its press is the game's now
        bool lt = false;        // the layer this press started in (the second layer: the left trigger held)
        double startAt = 0;
        double lead = 0;        // the left trigger alone first, this long
    };
    One one_[2];
    bool ltDown_ = false;       // the left trigger, with hysteresis
    double ltSince_ = 0;        // when it went down

public:
    struct In {
        double time = 0;
        bool allowed = false;         // first person, direct input, focused, in a game, no menu or panel
        bool triggerFree = true;      // the right trigger is not someone else's (a slot picked on the panel, the bow's draw, a gesture that took it)
        bool aFree = true;            // the right A is not a gesture's
        float trigger = 0;            // the right hand's trigger
        float leftTrigger = 0;        // the left hand's: the second layer
        bool a = false;               // the right hand's A (Button::kPrimary)
        bool gripHeld = false;        // the right grip is down: a press with it is the layout's chord (grip + A, grip + trigger), not a slot's
        uint16_t skillMask = 0;       // SkillMask
    };
    struct Out {
        bool takeTrigger = false;      // the right trigger / A are the cast's: the layout sees them let go
        bool takeA = false;
        bool takeLeftTrigger = false;  // and the left trigger: the layer is the press's own (taken, the layout's attack or chord cannot see it)
        uint16_t buttons = 0;          // pad buttons down now (XInput bits)
        uint8_t lt = 0, rt = 0;
        bool casting = false;
        int started[2] = {-1, -1};     // the slot of a press that began this frame, per control (A, trigger): for the log
    };

    void Reset() { *this = SlotControls{}; }

    // Whether a press of this control is the game's now.
    bool Active(Control c) const { return one_[int(c)].active; }
    // The left trigger is held (the second layer), as the last Step saw it.
    bool LeftDown() const { return ltDown_; }

    Out Step(const In& in) {
        Out out;
        const bool ltWas = ltDown_;
        ltDown_ = in.leftTrigger > (ltDown_ ? kTriggerOff : kTriggerOn);
        if (ltDown_ && !ltWas) ltSince_ = in.time;
        const bool now[2] = {in.a, in.trigger > (one_[1].down ? kTriggerOff : kTriggerOn)};
        for (int i = 0; i < 2; ++i) {
            One& o = one_[i];
            const bool was = o.down;
            o.down = now[i];
            const bool free = i == 0 ? in.aFree : in.triggerFree;
            if (!in.allowed || !free) {   // (a control held through a panel, a menu or the bow's draw starts nothing when it is free again)
                o.active = false;
                o.armed = false;
                continue;
            }
            if (!o.down) o.armed = true;
            if (o.active || !(o.down && !was && o.armed) || in.gripHeld) continue;
            const bool lt = ltDown_;
            const int other = 1 - i;
            if (one_[other].active && one_[other].lt != lt) continue;   // (the other control's press is in the other layer: one layer at a time)
            const int slot = SlotOfControl(Control(i), lt);
            if (!(in.skillMask & (1u << slot))) continue;                // a plain slot: the layout's own control, as ever
            o.active = true;
            o.lt = lt;
            o.startAt = in.time;
            o.lead = lt ? std::max(0.0, kLeadS - (in.time - ltSince_)) : 0.0;   // (the left trigger has been down already: no wait)
            out.started[i] = slot;
        }
        for (int i = 0; i < 2; ++i) {
            One& o = one_[i];
            if (!o.active) continue;
            const double age = in.time - o.startAt;
            if (!o.down && age >= o.lead + kMinPressS) {   // let go: the pulse already sent runs out by itself
                o.active = false;
                continue;
            }
            out.casting = true;
            out.takeLeftTrigger = true;
            if (i == 0) out.takeA = true; else out.takeTrigger = true;
            if (o.lt) out.lt = 255;
            if (age >= o.lead) {
                if (i == 0) out.buttons |= kPadA;
                else out.rt = 255;
            }
        }
        return out;
    }
};

}  // namespace wheel
