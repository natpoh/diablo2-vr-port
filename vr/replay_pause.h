// replay_pause.h - when the replay (the right eye drawn again from the left pass's lists) may run: only in a loaded area that
// has settled for 240 pairs. The game frees and makes resources while an area loads and for a while after, and its frame
// fences pass before our replay on the queue - a replay then reads what was freed and the GPU hangs (2026-10-09, an area
// change; 2026-10-18, a portal into act2_palace_cells that loaded inside one 421 ms frame with the world flags never off).
//
// The settle starts again at: no world (a menu, a loading screen), the game setting a biome (an area loads), or a game frame
// of over 150 ms between two pairs (a load the other two did not show). Pure: vrcam's replay::Wanted drives it once a pair,
// cleanroom/tests/replay_pause_test.cpp proves it.
#pragma once

#include <cstdint>

namespace replaypause {

enum class Why { kNone, kNoWorld, kArea, kLongFrame };

struct Settle {
    static constexpr int kPairs = 240;           // pairs in a settled area before the replay runs
    static constexpr double kLongFrameMs = 150;  // a gap between two pairs longer than this is a load

    uint32_t seenArea = 0;
    double lastUs = 0.0;
    int pairs = 0;

    // Once a pair, at its start. areaChanges: the game's biome sets so far; nowUs: a steady clock; inWorld: the world flags.
    // True: the replay may run this pair. *stopped: why a running (settled) replay stops now, else kNone.
    bool Step(uint32_t areaChanges, double nowUs, bool inWorld, Why* stopped) {
        *stopped = Why::kNone;
        const double gapMs = lastUs > 0.0 ? (nowUs - lastUs) / 1000.0 : 0.0;
        lastUs = nowUs;
        const bool newArea = areaChanges != seenArea;
        seenArea = areaChanges;
        Why why = Why::kNone;
        if (!inWorld) why = Why::kNoWorld;
        else if (newArea) why = Why::kArea;
        else if (gapMs > kLongFrameMs) why = Why::kLongFrame;
        if (why != Why::kNone) {
            if (pairs >= kPairs) *stopped = why;
            pairs = 0;
            return false;
        }
        if (pairs < kPairs) ++pairs;
        return pairs >= kPairs;
    }
    bool Settled() const { return pairs >= kPairs; }
};

}  // namespace replaypause
