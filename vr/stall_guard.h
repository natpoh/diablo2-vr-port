// stall_guard.h - a GPU queue that stops moving, told apart from one that is only busy; and what is done about it. Pure (tools/stall_guard_test.cpp).
//
// 2026-10-18, two hangs on test-58..61 (crash_1009, hang_1033): the game's direct queue stood still for good - no device removal (TDR) in the second, the process
// alive with its draw thread inside ReShade's flush (an infinite WaitForSingleObject on the fence of a submission eight flushes old). All of our fences were
// complete; the queue stood after a satisfied wait on the swap chain's own fence, in the game's own work. Nothing of ours waited forever - ReShade's wait and
// the game's are what stood. So what can be done is on three levels, by how long the queue has stood:
//   1. SKIP: our optional work that would wait on the queue (ReShade's flush before a copy, a texture's replacement that needs the queue idle) is not started.
//   2. UNSTICK: the fences a queue waits for are signalled from the CPU - the ones nothing queued will reach (the display's, a thread's, the runtime's: someone
//      else's signal that did not come), the ones already reached (a wait that was not woken is woken by the signal's check), and our own cross-queue fences.
//   3. REMOVE (off by default): the device is taken away on purpose - the game ends as with a driver reset, and DRED says where each queue stood. For the tests.
#pragma once

#include <cstddef>
#include <cstdint>

namespace stallguard {

// A queue's fence looked at now and then (any thread that looks keeps its own Progress). `signalled`: the last value put on the queue, `completed`: the
// fence's own.
struct Progress {
    uint64_t completed = 0;
    double movedAtMs = 0.0;       // when the completed value was last seen to change, or the queue last seen idle, or work first seen outstanding
    bool outstanding = false;     // at the last look
};

// How long the queue has stood still with work on it (ms): 0 when it moves, or has nothing to do. The clock starts when a look first sees work outstanding
// (so a queue idle for a minute and given work is not "stuck for a minute") and restarts at every change of the completed value.
inline double Feed(Progress& p, uint64_t signalled, uint64_t completed, double nowMs) {
    const bool out = signalled > completed;
    if (!out || completed != p.completed || !p.outstanding) p.movedAtMs = nowMs;
    p.completed = completed;
    p.outstanding = out;
    return out ? nowMs - p.movedAtMs : 0.0;
}

struct Limits {
    double skipMs = 250.0;        // level 1
    double unstickMs = 8000.0;    // level 2 (a long hitch of a loading frame is 3-6 s here: a runtime's xrWaitFrame blocked 6.4 s and the game went on)
    double removeMs = 0.0;        // level 3, 0 = never
};

enum class Level { None = 0, Skip = 1, Unstick = 2, Remove = 3 };

inline Level Decide(double stuckMs, const Limits& l) {
    if (l.removeMs > 0.0 && stuckMs >= l.removeMs) return Level::Remove;
    if (stuckMs >= l.unstickMs) return Level::Unstick;
    if (stuckMs >= l.skipMs) return Level::Skip;
    return Level::None;
}

// A GPU wait that was put on a queue: its fence, the value waited for, and the fence's completed value now.
struct Wait { const void* fence; uint64_t value; uint64_t completed; };
// A signal that was put on a queue (done or not).
struct Signal { const void* fence; uint64_t value; };
// A fence to signal from the CPU, and to what value.
struct Pick { const void* fence; uint64_t value; };

// The fences to signal from the CPU:
//  - a wait above the fence's completed value that no queued signal on that fence reaches ("external": a signal that is someone else's) -> to the highest value
//    waited for on it;
//  - a fence that is waited for and reached (its completed value covers every wait seen) -> to its completed value (the same: the signal's check of the
//    queues waiting for it is what is wanted; nothing is told earlier than it happened);
//  - a wait above the completed value that a queued signal does reach is left alone (the signal is in the queue; it comes when the queue gets there).
// Each fence once. Returns the count written to `out` (at most `cap`).
inline size_t PickForUnstick(const Wait* w, size_t nw, const Signal* s, size_t ns, Pick* out, size_t cap) {
    size_t n = 0;
    for (size_t i = 0; i < nw; ++i) {
        bool seen = false;
        for (size_t k = 0; k < n; ++k) if (out[k].fence == w[i].fence) { seen = true; break; }
        if (seen || !w[i].fence) continue;
        uint64_t maxWaited = 0, completed = w[i].completed;
        for (size_t j = 0; j < nw; ++j) if (w[j].fence == w[i].fence && w[j].value > maxWaited) maxWaited = w[j].value;
        uint64_t maxSignalled = 0;
        for (size_t j = 0; j < ns; ++j) if (s[j].fence == w[i].fence && s[j].value > maxSignalled) maxSignalled = s[j].value;
        if (maxWaited > completed) {
            if (maxSignalled >= maxWaited) continue;                    // a queued signal will come
            if (n < cap) out[n++] = {w[i].fence, maxWaited};            // external, and it did not
        } else if (completed > 0) {
            if (n < cap) out[n++] = {w[i].fence, completed};            // reached: wake whatever did not hear
        }
    }
    return n;
}

}  // namespace stallguard
