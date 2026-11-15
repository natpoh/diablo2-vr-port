// vcache.h - the game's threads on the cores with the big L3 (AMD Ryzen X3D with two CCDs: 7900X3D / 7950X3D / 9900X3D /
// 9950X3D have one CCD with 3D V-Cache and one without; a one-CCD X3D needs nothing). [render] vcache (Settings > Performance, live).
//
// Pick(): from the L3 caches the system reports (size, group, processor mask), the one that is the largest and larger than
// every other - nothing when they are all alike (an Intel part, a plain Ryzen, a one-CCD X3D). Pure, cleanroom/tests/vcache_test.cpp.
// Apply() (vcache.cpp): the calling thread's (the game's draw thread) own CPU sets to that cache's processors - nothing else.
// Off again: its sets cleared.
//
// 2026-11-15, Serg's 7950X3D, 0.217.1: with the key on the game stood 2-9 s again and again (flat and VR; the players' ini without
// it: none). Until then Apply set the PROCESS's default CPU sets to the V-Cache CCD and raised the draw thread above normal. D2R
// sizes its job pool to the machine's 32 logical processors and its idle workers spin (yield, look again - D2RLoader+0x122CA1B,
// 18-20 of them in every stall snapshot, each at 99-108 % of a core): squeezed onto the CCD's 16, they took the timeslices from
// the threads with real work - the game's main thread and the job the draw thread waited on - and a frame took seconds. So by
// default the process and the game's own threads are never touched any more, and no priority is raised (PlanFor below, tested);
// [render] vcache_how 1 - 3 bring each old part back alone, live, to prove which one it was.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vcache {

struct L3 { uint64_t bytes; uint16_t group; uint64_t mask; };

// What Apply does for `want`, Pick's answer and [render] vcache_how. 0 (the default): only the draw thread's own sets, never the
// process's default (every thread of the game - its spinning job workers crowded onto half the processors), never a priority.
// 1 - 3 only to tell the two parts of the old one apart live (the coordinator's test, 2026-11-15): 1 the process's default sets
// alone, 2 the draw thread above normal alone, 3 both as 0.217 had it. Anything else is 0. No big L3: nothing, whatever the how.
enum How : int { kDrawThread = 0, kProcessSets = 1, kPriority = 2, kAsBefore = 3 };
inline int HowOf(int how) { return how >= kDrawThread && how <= kAsBefore ? how : kDrawThread; }
struct Plan {
    bool drawThreadSets = false;   // SetThreadSelectedCpuSets on the calling (draw) thread
    bool processDefault = false;   // SetProcessDefaultCpuSets (how 1, 3 only: the 2026-11-15 stalls' suspect)
    bool raisePriority = false;    // SetThreadPriority above normal (how 2, 3 only)
};
inline Plan PlanFor(bool want, int big, int how = kDrawThread) {
    Plan p;
    if (!want || big < 0) return p;
    switch (HowOf(how)) {
        case kProcessSets: p.processDefault = true; break;
        case kPriority: p.raisePriority = true; break;
        case kAsBefore: p.processDefault = p.raisePriority = true; break;
        default: p.drawThreadSets = true; break;
    }
    return p;
}

// The index into caches of the one big L3, or -1 when there is none to prefer.
inline int Pick(const std::vector<L3>& caches) {
    if (caches.size() < 2) return -1;
    int best = -1;
    for (int i = 0; i < (int)caches.size(); ++i)
        if (best < 0 || caches[i].bytes > caches[best].bytes) best = i;
    for (int i = 0; i < (int)caches.size(); ++i)
        if (i != best && caches[i].bytes >= caches[best].bytes) return -1;   // a tie: no big one
    return caches[best].mask ? best : -1;
}

// "0-15" for a mask's processors (runs joined), for the log.
inline std::string Ranges(uint64_t mask) {
    std::string out;
    for (int i = 0; i < 64;) {
        if (!(mask >> i & 1)) { ++i; continue; }
        int j = i;
        while (j + 1 < 64 && (mask >> (j + 1) & 1)) ++j;
        if (!out.empty()) out += ",";
        out += std::to_string(i);
        if (j > i) out += "-" + std::to_string(j);
        i = j + 1;
    }
    return out;
}

// vcache.cpp
std::vector<L3> Detect();
// want: [render] vcache, how: [render] vcache_how. Call from the game's draw thread (the sets and the priority are the calling
// thread's); does something only when either changes - what the last one did is undone first. log: one line per decision.
void Apply(bool want, int how, void (*log)(const char*));

}  // namespace vcache
