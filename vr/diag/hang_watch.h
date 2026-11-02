// vr/diag/hang_watch.h - the hang watchdog's rule: when the game's presents have stood still long enough to say where every thread is
// (d2r_vr_hang.txt), and longer to take a minidump. Pure (cleanroom/tests/hang_watch_test.cpp); the thread is vr/diag/crash.cpp.
//
// 2026-11-02, rivas_10 on 0.197.0: the game's draw thread spun for a minute (presents 0, the thread at 165 % of a core) and nothing was
// written - the only stack watchdog was the replay's (vr/render/stereo/replay.cpp), and with no ReShade (the default since 0.186) the
// replay never runs. This one watches the presents in every mode, from the game's first present on.
#pragma once

#include <cstdint>

namespace hangwatch {

enum class Act {
    None,
    Stacks,   // stood still for stacksMs: every thread's stack to the file
    Dump,     // ... and for dumpMs: a minidump beside it
    Back,     // presents again after a Stacks (or Dump): it was a long frame, `stoodMs` long - said in the log
};

struct Limits {
    double stacksMs = 5000.0;   // a loading frame stands 3-6 s here (vr/stall_guard.h): a stack at 5 s may still be a load, the log says if it came back
    double dumpMs = 15000.0;    // no load stands this long; Windows calls the game "not responding" after 5 s - the player waits about a minute
};

struct Watch {
    uint32_t seen = 0;        // the presents' count at the last look
    double movedAtMs = 0.0;   // when it last changed (or the wait last restarted)
    bool started = false;     // a present has been seen: before the game's first one nothing is watched
    int stage = 0;            // what this stall has had: 0 nothing, 1 the stacks, 2 the minidump too
    double stoodMs = 0.0;     // with Back: how long the presents stood
};

// One look: `presents` the count now (never cleared), `paused` true when standing still is no hang (the game's window minimized).
inline Act Step(Watch& w, uint32_t presents, double nowMs, bool paused, const Limits& l = Limits{}) {
    if (presents != w.seen || !w.started) {
        const bool moved = presents != w.seen;
        w.seen = presents;
        if (!moved) return Act::None;   // (not started, still nothing)
        const Act a = w.started && w.stage > 0 ? Act::Back : Act::None;
        w.stoodMs = nowMs - w.movedAtMs;
        w.started = true;
        w.stage = 0;
        w.movedAtMs = nowMs;
        return a;
    }
    if (paused) {   // the clock starts again when the window comes back
        if (w.stage == 0) w.movedAtMs = nowMs;
        return Act::None;
    }
    const double stood = nowMs - w.movedAtMs;
    if (w.stage == 0 && stood >= l.stacksMs) {
        w.stage = 1;
        return Act::Stacks;
    }
    if (w.stage == 1 && l.dumpMs > 0.0 && stood >= l.dumpMs) {
        w.stage = 2;
        return Act::Dump;
    }
    return Act::None;
}

}  // namespace hangwatch
