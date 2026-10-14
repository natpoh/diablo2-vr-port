// focus_gate.h - a press held through a focus loss is not a press when the focus returns (vr/xr.cpp Publish).
//
// With the input focus away (SteamVR's dashboard, the headset's own menu, another app's overlay, a sleep)
// xrSyncActions returns XR_SESSION_NOT_FOCUSED and every action reads inactive: the trigger and the
// buttons go out as let go. When the focus comes back the next frame reads the hand as it is - and a
// trigger the player still holds (often the very click that closed the dashboard) goes out as a press
// from nothing: an attack, a pick-up, a click, at once, at whatever the hand points at.
//
// The gate keeps what is down on the first live frame after a loss (and at the session's start) at
// "let go" until the player lets go of it; the next press is then a real one. The same rule as
// bearstonem/d2r-pcvr's (MIT) steamvr/transport.cpp: a held trigger must be released before it can
// attack. Pure (no globals, no runtime), tested in tools/xr_hardening_test.cpp.
#pragma once

#include <cstdint>

namespace focusgate {

constexpr float kDown = 0.1f;   // an analog trigger at or above this is held (the laser's let-go level too)

// One hand.
class Gate {
 public:
    // Once a frame, with what the hand reads. `focused`: the actions are live this frame.
    // `*buttons`: bit flags as pressed; only the bits in `gatedMask` are held back. `*trigger`: its
    // analog value, set to 0 while it is held back. Returns true when something was held back.
    // F4 only (vr/view_gate.h): the gate follows the hands in every view, but holds a press back only where `apply` is true - in
    // the other views `*buttons` and `*trigger` come out as they went in, as before the gate existed. Back in F4 it knows what is
    // still down from a loss of the focus that happened meanwhile. Returns true when something was held back (never when not applied).
    bool StepIf(bool apply, bool focused, uint32_t* buttons, float* trigger, uint32_t gatedMask) {
        uint32_t b = *buttons;
        float t = *trigger;
        const bool held = Step(focused, &b, &t, gatedMask);
        if (!apply) return false;
        *buttons = b;
        *trigger = t;
        return held;
    }

    bool Step(bool focused, uint32_t* buttons, float* trigger, uint32_t gatedMask) {
        if (!focused) {   // everything reads let go meanwhile: what is held now is known at the return
            armed_ = true;
            return false;
        }
        if (armed_) {
            armed_ = false;
            bits_ = *buttons & gatedMask;
            trigger_ = *trigger >= kDown;
        } else {
            bits_ &= *buttons;                       // let go once: free again
            if (trigger_ && *trigger < kDown) trigger_ = false;
        }
        const bool held = (bits_ & *buttons) != 0 || trigger_;
        *buttons &= ~bits_;
        if (trigger_) *trigger = 0.0f;
        return held;
    }

 private:
    bool armed_ = true;      // the next live frame sees what is held (the start counts as a return)
    uint32_t bits_ = 0;      // buttons held through the loss, still down
    bool trigger_ = false;   // the analog trigger held through it
};

}  // namespace focusgate
