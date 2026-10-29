#pragma once
// [openxr] laser_grip_tap: in a game menu the grips are the pad's bumpers (direct_layout.h MenuTable, BodyWalk's "D2R Menu" mapping) - LB / RB turn
// the pages of the tabbed menu (the pad layout's UISwitcher: inventory, character, skills ... options), the stash's and a trader's. While the laser
// points at the menu it takes both grips (xr.cpp LaserStep: its own hand's is the right click's, the other's Shift), so no page could be turned
// with the laser up: a player's report (0.182) - "the laser cannot reach the Options tab"; he had to push a stick (the laser waits for the pad)
// and then press the left grip until Options came. Here a grip the laser took is TAPPED back to the game: squeezed and let go within kTapMaxMs,
// with no trigger pressed meanwhile (a right click, a Shift or Ctrl click) and nothing else made of the hold (the menu zoom moved the panel), its
// own press goes to the game for kPulseMs on letting go - the bumper, the page. Held longer, or used with a click: the laser's, as before.
// Pure: a hand's controls in, "its grip goes to the game now" out. Test: cleanroom/tests/laser_grip_tap_test.cpp.
#include <cstdint>

namespace lasergrip {

constexpr uint64_t kTapMaxMs = 400;   // longer is a hold: a modifier waiting for its click (or the zoom), never a page
constexpr uint64_t kPulseMs = 100;    // the press given to the game (menu_zoom.h kGripTapMs: the pad is polled many times in it)

struct In {
    bool allowed = false;   // the box on, VR F4, the laser on a game menu
    bool claimed = false;   // the laser has this grip this frame (hidden from the game)
    bool grip = false;      // the grip down, as pressed (before anything took it)
    bool click = false;     // a trigger down this frame (either hand: the laser's click, the other hand's Ctrl)
    bool spent = false;     // something else made this hold its own (the menu zoom moved the panel)
    uint64_t nowMs = 0;     // a monotonic clock
};

class Tap {
public:
    // Each frame, for one hand. True while its grip's own press goes to the game.
    bool Step(const In& in) {
        if (in.grip && !down_) {   // squeezed: a tap only if the laser takes it and no click is under way
            ok_ = in.allowed && in.claimed && !in.click && !in.spent;
            startMs_ = in.nowMs;
        } else if (in.grip) {
            if (!in.allowed || !in.claimed || in.click || in.spent || in.nowMs - startMs_ > kTapMaxMs) ok_ = false;
        } else if (down_) {        // let go
            if (ok_ && in.allowed && in.nowMs - startMs_ <= kTapMaxMs) untilMs_ = in.nowMs + kPulseMs;
            ok_ = false;
        }
        down_ = in.grip;
        if (!in.allowed || in.grip) untilMs_ = 0;   // (the menu gone, or a new squeeze: the old pulse ends)
        return in.nowMs < untilMs_;
    }
    void Reset() { *this = Tap{}; }

private:
    bool down_ = false, ok_ = false;
    uint64_t startMs_ = 0, untilMs_ = 0;
};

}  // namespace lasergrip
