// hold_tap.h - hold-or-tap for one control of direct input (vr/direct_layout.h), pure: no game, no OpenXR, no clock of its
// own - the caller hands in the time. Tested on synthetic timelines in cleanroom/tests/direct_input_test.cpp.
//
// A control that has a "hold" line ([direct] right_stick_hold ...) can no longer act the moment it is pressed:
//   - let go before `holdMs`: a TAP. The plain line fires as a press of `pulseMs`, starting at the release.
//   - held to `holdMs`: the HOLD line fires ONCE, as a press of `pulseMs`, at that moment; the tap is gone, and
//     the release afterwards does nothing more.
// A plain output that is HELD by nature (a game key command such as Alt, held as long as the click is) is not
// waited for: it is on at once, as before, and goes off when the hold fires or the control is let go. That is the
// output's choice, not the machine's: Out.immediate says "still undecided, show what is shown at once".
#pragma once

#include <algorithm>
#include <cstdint>

namespace directinput {
namespace holdtap {

constexpr uint32_t kDefaultHoldMs = 500;   // [direct] hold_ms
constexpr uint32_t kMinHoldMs = 200, kMaxHoldMs = 1500;
constexpr uint32_t kPulseMs = 100;         // a tap or a hold pressed for this long: the pad is polled many times in it

inline uint32_t ClampHoldMs(long v) {
    return (uint32_t)std::clamp<long>(v, (long)kMinHoldMs, (long)kMaxHoldMs);
}

// What a control gives right now.
struct Out {
    bool immediate = false;   // pressed and not yet decided (before holdMs): the plain line's held outputs are on
    bool tap = false;         // a tap's press is on: the plain line's other outputs
    bool hold = false;        // the hold's press is on: the hold line's outputs
    bool fired = false;       // true only in the step that the hold fired (an edge, for a request that must come once)
};

class Machine {
public:
    // down: the control is pressed now (the caller gives false for a press it does not hand to the machine, a chord);
    // nowMs: a monotonic clock in ms. Call once per step, in time order.
    Out Step(bool down, uint64_t nowMs, uint32_t holdMs, uint32_t pulseMs = kPulseMs) {
        Out o;
        if (down && state_ == State::Idle && !wasDown_) { state_ = State::Pressed; t0_ = nowMs; }
        if (state_ == State::Pressed) {
            if (!down) {                                   // let go in time: a tap
                state_ = State::Idle;
                tapUntil_ = std::max(tapUntil_, nowMs + pulseMs);
            } else if (nowMs - t0_ >= holdMs) {            // held long enough: the hold, once
                state_ = State::Held;
                holdUntil_ = nowMs + pulseMs;
                o.fired = true;
            }
        } else if (state_ == State::Held && !down) {
            state_ = State::Idle;
        }
        wasDown_ = down;
        o.immediate = state_ == State::Pressed;
        o.tap = nowMs < tapUntil_;
        o.hold = nowMs < holdUntil_;
        return o;
    }
    // Everything forgotten, no tap: the context changed under the press (a menu opened, the focus went, a panel came up).
    void Cancel() { *this = Machine(); }
    // Something to give still: a tap's or a hold's press not yet over, or a press under way.
    bool Busy(uint64_t nowMs) const { return state_ != State::Idle || nowMs < tapUntil_ || nowMs < holdUntil_; }

private:
    enum class State : uint8_t { Idle, Pressed, Held };
    State state_ = State::Idle;
    bool wasDown_ = false;
    uint64_t t0_ = 0, tapUntil_ = 0, holdUntil_ = 0;
};

}  // namespace holdtap
}  // namespace directinput
