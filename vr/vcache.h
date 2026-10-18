// vcache.h - the game's threads on the cores with the big L3 (AMD Ryzen X3D with two CCDs: 7900X3D / 7950X3D / 9900X3D /
// 9950X3D have one CCD with 3D V-Cache and one without; a one-CCD X3D needs nothing). [render] vcache (Settings > Performance, live).
//
// Pick(): from the L3 caches the system reports (size, group, processor mask), the one that is the largest and larger than
// every other - nothing when they are all alike (an Intel part, a plain Ryzen, a one-CCD X3D). Pure, cleanroom/tests/vcache_test.cpp.
// Apply() (vcache.cpp): the process's default CPU sets to that cache's processors - a preference the scheduler keeps, not a
// hard affinity: threads that chose their own sets keep them - and the calling thread (the game's draw thread) above normal.
// Off again: the defaults cleared, the priority back to normal.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vcache {

struct L3 { uint64_t bytes; uint16_t group; uint64_t mask; };

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
// want: [render] vcache. Call from the game's draw thread (its priority is the calling thread's); does something only when
// `want` changes. log: one line per decision.
void Apply(bool want, void (*log)(const char*));

}  // namespace vcache
