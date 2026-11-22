// stats_leak.h - [debug] stats_leak_trace: which stat lists the game leaks when a game is left (crash reports 290, 406, 407).
//
// The game ends on its own assert "We have leaked Stats when the unit stats have all been deleted" (D2Common\src\Stats\Stats.cpp
// line 1285, BC_ASSERT s_StatsPool.IsEmpty()) when it goes back to the menu. Read in the game's code (D2RLoader 1.3.1's layout,
// dump_loader/D2R_image.bin):
//   - two pools: the plain stat lists 0x2A9A430 (its lock 0x2A9A4C8, alloc 0x2F7300 - the only caller of the pool's own alloc on it),
//     the units' stat holders ("Ex" lists, [list+0x1C] < 0) 0x2A9A4D0 (lock 0x2A9A568, alloc 0x2F65D0); a pool's live count at +0x88
//     (its free 0x1230490: dec [pool+0x88], inc [pool+0x8C]);
//   - one free for both, 0x2F9EB0 (byte, list): an Ex list frees the plain lists hung on it (two chains, +0x90 and +0x98, the next at
//     +0x68) through itself, gives itself back, and THEN - its pool now empty, the plain pool not - asserts. So the assert means: the last
//     unit's stat holder went while a plain list was alive that no unit's holder carried.
// With the option on, the plugin follows both functions (the original always runs, nothing is changed): each plain list's address,
// the allocator's arguments and the return addresses on the stack at its birth; at its free it is forgotten. When the free of the
// last stat holder is about to run, every plain list still alive that the holder does not carry is the leak - written to the log
// (synchronously: the game ends a moment later) with where it was made, and whether d2rl-vrcam.dll was on that stack.
// This file is the bookkeeping (pure, tested in cleanroom/tests/stats_leak_test.cpp); vr/stats_leak.cpp is the game's side.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace statsleak {

constexpr int kFrames = 12;   // return addresses kept per list (the allocator's caller first)

// A plain stat list at its birth: the allocator's four arguments (flags, a float's bits, and two ids: [list+0x1C], +0x24, +0x08, +0x0C),
// the stack's return addresses, the order of births.
struct Birth {
    uint32_t args[4] = {};
    uint64_t frames[kFrames] = {};
    int frameCount = 0;
    uint64_t seq = 0;
};

struct Leak {
    uintptr_t list = 0;
    Birth birth;
};

// Where the plugin's own code is loaded: a frame in [base, base + size) is ours.
struct Range {
    uintptr_t base = 0;
    size_t size = 0;
    bool Has(uint64_t a) const { return size && a >= base && a - base < size; }
};

// The first of a birth's frames that is in `r` (-1: none).
inline int FirstIn(const Birth& b, const Range& r) {
    for (int i = 0; i < b.frameCount && i < kFrames; ++i)
        if (r.Has(b.frames[i])) return i;
    return -1;
}

// The live plain lists. Not thread safe by itself: the caller holds its lock.
class Tracker {
public:
    void Born(uintptr_t list, const Birth& b) {
        if (!list) return;
        Birth x = b;
        x.seq = ++seq_;
        live_[list] = x;
        ++born_;
    }
    // A free of a list we know (or not: one born before the trace began is not counted).
    void Freed(uintptr_t list) {
        if (live_.erase(list)) ++freed_;
        else ++freedUnknown_;
    }
    size_t Live() const { return live_.size(); }
    uint64_t BornCount() const { return born_; }
    uint64_t FreedCount() const { return freed_; }
    uint64_t FreedUnknown() const { return freedUnknown_; }
    bool Knows(uintptr_t list) const { return live_.count(list) != 0; }

    // Every live list but those in `carried` (the last holder's own chains, freed with it), oldest first.
    std::vector<Leak> Leaked(const std::unordered_set<uintptr_t>& carried) const {
        std::vector<Leak> out;
        for (const auto& kv : live_)
            if (!carried.count(kv.first)) out.push_back({kv.first, kv.second});
        std::sort(out.begin(), out.end(), [](const Leak& a, const Leak& b) { return a.birth.seq < b.birth.seq; });
        return out;
    }

private:
    std::unordered_map<uintptr_t, Birth> live_;
    uint64_t seq_ = 0, born_ = 0, freed_ = 0, freedUnknown_ = 0;
};

// The leak verdict at the last holder's free: the plain pool's live count (the game's), how many of them the holder carries, how many
// we know. untracked = alive in the pool, carried by nothing, and born before the trace's hooks went in (their birth is not known).
struct Verdict {
    bool leak = false;          // the game will assert: plain lists alive that the last holder does not carry
    int64_t notCarried = 0;     // the pool's live plain lists minus the holder's own
    int64_t untracked = 0;      // of them, born before the trace began
};
inline Verdict Judge(int64_t plainLive, int64_t carried, int64_t trackedLeaks) {
    Verdict v;
    v.notCarried = plainLive - carried;
    v.leak = v.notCarried > 0;
    v.untracked = std::max<int64_t>(0, v.notCarried - trackedLeaks);
    return v;
}

// One log line for a leaked list: its address, the allocator's arguments, its caller and the frames as game RVAs (module-relative,
// so the line can be read against the game's image) or "vrcam+0x..." for ours, "?" for neither.
inline void Describe(const Leak& l, uintptr_t gameBase, size_t gameSize, const Range& ours, char* out, size_t n) {
    const Range game{gameBase, gameSize};
    int w = std::snprintf(out, n, "list %p (flags 0x%X, %08X, ids %u %u, born #%llu)%s - made at", (void*)l.list, l.birth.args[0], l.birth.args[1],
                          l.birth.args[2], l.birth.args[3], (unsigned long long)l.birth.seq, FirstIn(l.birth, ours) >= 0 ? " - D2R VR ON ITS STACK" : "");
    for (int i = 0; i < l.birth.frameCount && i < kFrames && w > 0 && (size_t)w < n; ++i) {
        const uint64_t a = l.birth.frames[i];
        if (game.Has(a)) w += std::snprintf(out + w, n - w, " 0x%llX", (unsigned long long)(a - gameBase));
        else if (ours.Has(a)) w += std::snprintf(out + w, n - w, " vrcam+0x%llX", (unsigned long long)(a - ours.base));
        else w += std::snprintf(out + w, n - w, " ?%llX", (unsigned long long)a);
    }
}

}   // namespace statsleak
