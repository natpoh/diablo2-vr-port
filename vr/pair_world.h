#pragma once
// pair_world.h - is the pair (or, by turns, the pass) about to be drawn one of the game's world? Pure: no game, no OpenXR, no Windows
// (cleanroom/tests/pair_world_test.cpp).
//
// d2rcam::InWorld says yes while the world camera's follow ran within the last second: the follow stops when a game ends, and a stale
// frame is no world. But the follow runs inside every pass the game draws, so a pass that takes over a second leaves the NEXT pair's
// start more than a second after the last follow, in the middle of a game. A Quest 2 player on a 4 GB card (0.174, 2026-10-21: "a weird
// double vision thing, kinda looks like im drunk"): pairs of 1.2 - 1.8 s, and every pair after a right pass of over a second was taken for
// no world - no stereo for it, its right eye went to the headset as the flat picture 1.1 m ahead, the next pair in stereo again: the
// world jumped between its depth and a flat screen, one pair each (his log: 72 pairs and 64 flat in 10 s, every pair without its XR
// begin came after a right pass and the game's frame of about a second or more).
//
// The pair is of the world, too, when the follow ran since the previous pair began: it did run in that pair, so the game was drawing its
// world a pair ago. A game left: the pair after the last one with a follow is still taken (one pair, as the second was before at the
// game's own speed), the next one not. Fast pairs: the second rule says nothing the first did not.

#include <cstdint>

namespace pairworld {

enum class Why { Fresh, SincePrevious, No };   // the follow within the second, since the previous pair began, neither

struct Gate {
    uint64_t prevBegin = 0;   // when the previous pair began (ms, GetTickCount64); 0 = none yet

    // A pair begins at `now`. `fresh`: d2rcam::InWorld (the follow within the second, a world camera there). `lastSync`: when the follow
    // last ran with a world camera there (d2rcam::LastSyncTick; 0 = never, or no camera now). `slow`: [stereo] slow_pair_world - off,
    // the second rule is not asked (as before).
    Why Begin(uint64_t now, bool fresh, uint64_t lastSync, bool slow) {
        const uint64_t prev = prevBegin;
        prevBegin = now;
        if (fresh) return Why::Fresh;
        if (slow && lastSync != 0 && prev != 0 && lastSync >= prev && lastSync <= now) return Why::SincePrevious;
        return Why::No;
    }
};

inline bool World(Why w) { return w != Why::No; }

}  // namespace pairworld
