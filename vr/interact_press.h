#pragma once
// interact_press.h - [input] interact_one_press: one target per press of "pick up / interact" (vrcam.cpp ApplyActions).
//
// Before: pad A was held down for as long as the action was held and anything was under it, so one press on a chest opened
// it and then took the loot it spilled, and a hold over a pile took all of it. Now the target under the action when it is
// pressed is the press's: A stays down while that is still the target; when the target changes (the chest opened, the item
// taken) A is let go until the action is released and pressed again; a press on nothing spends that press. A press held
// across a change of context (a menu opened or closed, another game) counts for nothing until it is let go.
// Pure: no game call, no clock. From bearstonem/d2r-pcvr (MIT) steamvr/interact_press.h, contributed there by CalMac91
// (PR #70), rewritten here.
#include <cstdint>

namespace interactpress {

class Press {
    bool wasHeld_ = false, primed_ = false, blocked_ = false;
    uintptr_t latch_ = 0;
    uint64_t session_ = 0;
    uint32_t epoch_ = 0;

public:
    struct Result {
        bool down = false;   // pad A down now
        bool miss = false;   // a fresh press with nothing under it (the press is spent)
        bool lost = false;   // the press's target changed: A let go until the next press
    };
    // held: the action is held; target: what the game would interact with now (0 none); hooked: the target can be asked at all
    // (without it the action is a plain A, as before); session: the local player; epoch: a counter of context changes (menus).
    Result Poll(bool held, uintptr_t target, bool hooked, uint64_t session, uint32_t epoch) {
        const bool changed = primed_ && (session != session_ || epoch != epoch_);
        primed_ = true;
        session_ = session;
        epoch_ = epoch;
        if (changed) {
            latch_ = 0;
            if (wasHeld_ && held) blocked_ = true;   // carried over the change: nothing until let go
        }
        Result out;
        if (!held) {
            latch_ = 0;
            blocked_ = false;
        } else if (!wasHeld_) {
            latch_ = target;
            out.miss = !latch_ && hooked;
        } else if (latch_ && target != latch_) {
            latch_ = 0;
            out.lost = true;
        }
        wasHeld_ = held;
        out.down = held && !blocked_ && (latch_ != 0 || !hooked);
        return out;
    }
};

}  // namespace interactpress
