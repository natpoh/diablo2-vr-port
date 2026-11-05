// fence_wait.h - a CPU wait for a fence value with a time limit that does not end early on a wake meant for an older wait. Pure C++ (tools/fence_wait_test.cpp).
//
// The replay's waits (vr/render/stereo/replay.cpp) share one auto-reset event. A wait that ran out leaves its SetEventOnCompletion armed: when that older value
// is reached later the event is set, and the next wait - for a newer value - woke at once on it and went on as if its own value had been reached. One of those
// waits keeps the game from resetting the command allocators of lists the GPU still replays; another keeps our own allocators. Here the value is looked at
// again after every wake, and the wait goes on until it is reached or the limit has passed.
//
// WaitValue(v, ms, completed, arm, sleep, nowMs): completed() the fence's value now; arm(v) asks for the event at v (SetEventOnCompletion); sleep(ms) waits on
// the event at most ms; nowMs() a clock in ms. True: v was reached. False: the limit passed first.
#pragma once

#include <cstdint>

namespace fencewait {

template <class Completed, class Arm, class Sleep, class Now>
bool WaitValue(uint64_t v, unsigned ms, Completed&& completed, Arm&& arm, Sleep&& sleep, Now&& nowMs) {
    if (completed() >= v) return true;
    const double end = nowMs() + ms;
    for (;;) {
        arm(v);
        if (completed() >= v) return true;   // (reached between the look and the arming)
        const double left = end - nowMs();
        if (left <= 0.0) return false;
        sleep((unsigned)(left + 0.999));
        if (completed() >= v) return true;
        if (nowMs() >= end) return false;
    }
}

}  // namespace fencewait
