// replay_pause.h - when the replay (the right eye drawn again from the left pass's lists) may run: only in a loaded area that
// has settled for 240 pairs. The game frees and makes resources while an area loads and for a while after, and its frame
// fences pass before our replay on the queue - a replay then reads what was freed and the GPU hangs (2026-10-09, an area
// change; 2026-10-18, a portal into act2_palace_cells that loaded inside one 421 ms frame with the world flags never off).
//
// The settle starts again at: no world (a menu, a loading screen), the game setting another biome (an area loads), or a game
// frame of over 150 ms between two pairs (a load the other two did not show). The same biome set again is not one (BiomeIsNew):
// the game does that every few seconds in one area (act2_town, act2_sewer: a Quest 2 player, 2026-10-26), and the settle never
// held - the right eye was a pass of the game's own 915 s of a 1514 s session, and under heavy action its particles and spells,
// drawn again, were not the left eye's ("two different views ... like the frames were from different points in time"). A load
// in the same biome (the sewer's next level) still has its loading screen or its long frame. Pure: vrcam's replay::Wanted
// drives it once a pair, cleanroom/tests/replay_pause_test.cpp proves it.
#pragma once

#include <cstdint>
#include <cstring>

namespace replaypause {

enum class Why { kNone, kNoWorld, kArea, kLongFrame };

// The game's biome set: a new area only when the name is another than the one before (bare names, as vrcam keeps them)
inline bool BiomeIsNew(const char* before, const char* now) { return !before || !now || strcmp(before, now) != 0; }

struct Settle {
    static constexpr int kPairs = 240;           // pairs in a settled area before the replay runs
    static constexpr double kLongFrameMs = 150;  // a gap between two pairs longer than this is a load

    uint32_t seenArea = 0;
    double lastUs = 0.0;
    int pairs = 0;
    uint32_t restarts[4] = {};   // the settle started again, by Why (for the log: what kept the replay off), till TakeRestarts

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
            ++restarts[(int)why];
            pairs = 0;
            return false;
        }
        if (pairs < kPairs) ++pairs;
        return pairs >= kPairs;
    }
    bool Settled() const { return pairs >= kPairs; }
    // the restarts since the last call (no world, another biome, a long frame), counted again from 0
    void TakeRestarts(uint32_t* noWorld, uint32_t* area, uint32_t* longFrame) {
        *noWorld = restarts[(int)Why::kNoWorld];
        *area = restarts[(int)Why::kArea];
        *longFrame = restarts[(int)Why::kLongFrame];
        for (uint32_t& r : restarts) r = 0;
    }
};

}  // namespace replaypause
