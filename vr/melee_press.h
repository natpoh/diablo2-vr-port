// melee_press.h - hit by touch: the press of the game's own attack key and the window in which the game's target lookups
// are answered with the touched monster (vr/melee_touch.cpp). Pure: no game calls, so it is tested on made-up clocks
// (cleanroom/tests/melee_touch_test.cpp), among them what a change of the view must do to it.
//
// A touch accepted opens the window (kTargetMs) and holds the press (kPressMs, extended by a touch within it). Nothing
// else about the attack is decided here: the game animates, checks the reach and rolls.
#pragma once

#include <atomic>
#include <cstdint>

namespace meleetouch {

class Press {
 public:
    static constexpr uint64_t kPressMs = 70, kTargetMs = 350;

    // The timer's thread: a touch accepted. Opens the target window on monster `id`; true when the press goes DOWN now
    // (it was up - the caller sends it), false when it is down already (its time is extended).
    bool Begin(uint32_t id, uint64_t now) {
        targetId_.store(id);
        targetUntil_.store(now + kTargetMs);
        until_ = now + kPressMs;
        if (down_) return false;
        down_ = true;
        return true;
    }
    // The timer's thread: true when the press goes UP now - its time is over, or `force` (the caller sends the release).
    bool Release(uint64_t now, bool force) {
        if (down_ && (force || now >= until_)) {
            down_ = false;
            return true;
        }
        return false;
    }
    bool Down() const { return down_; }

    // The game's thread: a touched monster is waiting for its attack to be aimed / the id it waits with.
    bool Pending(uint64_t now) const { return targetId_.load() != 0 && now < targetUntil_.load(); }
    uint32_t Target(uint64_t now) const { return Pending(now) ? targetId_.load() : 0; }

    // The module stands down (not F4, a menu, another weapon): the window is closed at once. The press itself is let go
    // by Release(now, true).
    void CloseWindow() {
        targetId_.store(0);
        targetUntil_.store(0);
    }

 private:
    std::atomic<uint32_t> targetId_{0};
    std::atomic<uint64_t> targetUntil_{0};
    bool down_ = false;   // the timer's thread only
    uint64_t until_ = 0;
};

}  // namespace meleetouch
