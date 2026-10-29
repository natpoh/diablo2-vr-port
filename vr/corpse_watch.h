// corpse_watch.h - [weight] monster_ragdoll: a body whose pose the game holds still is a corpse - no doll for it (docs/notes/monster_ragdoll.md). Pure
// (cleanroom/tests/bone_segments_test.cpp).
//
// A living monster's animation never holds a pose (it breathes, it idles); a dead one's last frame stays. Each model the bodies are read from is watched by
// a few points of its body (its largest capsules' ends, world units): one that has not moved more than kStillM (in metres) for kStillMs is still - its
// limbs are no doll and no hostile monster takes it for its body (monsterbodies::Assemble: a live monster standing over a corpse got the corpse's limbs).
// bearstonem/d2r-pcvr's rule (feat/monster-limb-push e935df9, limb_physics.cpp Lifeless, MIT): a still pose for a second is lifeless.
// ... and it lies (Lying): 0.188.6, Serg's "the NPCs don't give way to my weapon any more" - a townsperson standing at rest (Akara: "its pose held
// still for a second - a corpse") barely moves for a second, and lost its doll and its limbs. A corpse lies on the ground: its bodies are lower than they
// are long. A body standing still is no corpse.
#pragma once

#include <cmath>
#include <cstdint>

namespace corpsewatch {

constexpr uint64_t kStillMs = 1000;
constexpr float kStillM = 0.003f;
constexpr int kPoints = 6;

struct Watch {
    float p[kPoints][3] = {};
    int n = 0;
    uint64_t movedMs = 0, seenMs = 0;
    // A new look at its points (n of them, world units; upm world units a metre) at nowMs: true when it has been still for kStillMs.
    bool Look(const float (*pts)[3], int count, float upm, uint64_t nowMs) {
        seenMs = nowMs;
        if (count > kPoints) count = kPoints;
        bool moved = movedMs == 0 || count != n;
        const float lim = kStillM * (upm > 1e-3f ? upm : 1.0f);
        for (int i = 0; i < count && !moved; ++i)
            for (int k = 0; k < 3; ++k)
                if (std::fabs(pts[i][k] - p[i][k]) > lim || !std::isfinite(pts[i][k])) moved = true;
        if (moved) {
            for (int i = 0; i < count; ++i)
                for (int k = 0; k < 3; ++k) p[i][k] = pts[i][k];
            n = count;
            movedMs = nowMs;
        }
        return nowMs - movedMs >= kStillMs;
    }
};

// Does a body lie (its bodies' box: x, y up, z)? Lower than half its length across the ground.
inline bool Lying(float extentX, float extentY, float extentZ) {
    const float across = extentX > extentZ ? extentX : extentZ;
    return extentY < 0.5f * across;
}

}  // namespace corpsewatch
