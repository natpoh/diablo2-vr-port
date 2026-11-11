// replay_slot_check.h - are the replay's camera slots stale? (Tycootank's 0.212.0 report, 2026-11-10)
//
// In one pass (vr/render/stereo/replay.cpp, camfix in vr/render/depth/depth_fix.cpp) every buffer camfix judged to be a camera's is bound from
// a slot of ours, and the slot is filled with the game's values when the game RECORDS the binding. A buffer the game writes after it bound it
// reaches the GPU with what that memory held before - in both eyes; in two passes the game's own buffer is read at GPU time and is right.
// His headset showed the world squashed into a thin slice, hands gone, for exactly as long as the replay ran (two passes: fine). Whether this is
// it is told by looking: once in a while, after the left pass is recorded, each slot's source is hashed again and compared with the hash taken
// when it was copied. Pure: the hash and the tally; camfix does the reading.
#pragma once

#include <cstddef>
#include <cstdint>

namespace slotcheck {

// FNV-1a, 64 bits, over n bytes (cached memory: camfix hashes its own copy at the binding, and the game's buffer once in a while)
inline uint64_t Hash(const void* p, size_t n) {
    const uint8_t* b = (const uint8_t*)p;
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    return h;
}

// One look's results, summed over the log period
struct Tally {
    uint32_t looks = 0;       // pairs looked at
    uint32_t slots = 0;       // slots compared in them
    uint32_t changed = 0;     // ... whose source the game wrote after the copy
    uint32_t viewKeys = 0;    // ... of them buffers with a view matrix (not a position alone)
    void Add(uint32_t n, uint32_t ch, uint32_t views) { ++looks; slots += n; changed += ch; viewKeys += views; }
    // percent of the slots compared that were stale (0 with none compared)
    double StalePct() const { return slots ? 100.0 * changed / slots : 0.0; }
};

// How often to look: every `every`-th replayed pair (reading the game's upload memory, write-combined, costs; 28 slots x 4 KB a look)
inline bool LookNow(uint32_t pair, uint32_t every) { return every && pair % every == 0; }

}  // namespace slotcheck
