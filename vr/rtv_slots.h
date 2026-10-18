// rtv_slots.h - the places in xr.cpp's render-target descriptor heap, handed out and given back.
//
// Every swapchain xr.cpp draws the interface into (the toolbar, the map, the gesture zones, a menu's panel, the monster's bar...) needs one render-target view for each of
// its images, in one small heap (kRtvs). They used to be handed out round and round: when the next chain did not fit, the count went back to 0 and the views of
// whatever chain stood there first were overwritten - views of a LIVE chain now pointing at another chain's images (a test of test-56, 2026-10-17: the monster bar's
// swapchain was made again for every width of a name - 128x64, 256x64, 128x64... - and the game was gone at the seventh, with the heap wrapped; the draw into
// one chain went to another's image). Now a chain asks for its places, gives them back when it goes, and gets nothing - not somebody else's - when the heap is full.
#pragma once

#include <array>

namespace rtvslots {

template <int N>
class Slots {
public:
    static constexpr int kSize = N;
    // n places in a row: the first one's index, or -1 when there is no run of n free places (nothing is taken then).
    int Take(int n) {
        if (n < 1 || n > N) return -1;
        for (int at = 0; at + n <= N; ++at) {
            int run = 0;
            while (run < n && !used_[at + run]) ++run;
            if (run == n) {
                for (int i = 0; i < n; ++i) used_[at + i] = true;
                return at;
            }
            at += run;   // (the used place that stopped the run is skipped too, by the loop's own step)
        }
        return -1;
    }
    // Give back n places from `at` (only those that are taken; a place out of the heap is left alone).
    void Give(int at, int n) {
        for (int i = 0; i < n; ++i)
            if (at + i >= 0 && at + i < N) used_[at + i] = false;
    }
    void Reset() { used_.fill(false); }
    int Used() const {
        int n = 0;
        for (bool u : used_) n += u ? 1 : 0;
        return n;
    }
    bool IsUsed(int i) const { return i >= 0 && i < N && used_[i]; }
private:
    std::array<bool, N> used_{};
};

}  // namespace rtvslots
