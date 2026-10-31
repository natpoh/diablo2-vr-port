// bone_cap.h - [weight] skeleton_bones_max: how many bones of a monster's skeleton are read - one cap for the rig reader (vr/monster_rig.h), the humanoid
// judge (vr/monster_humanoid.h), the doll's pose (vr/doll_pose.h) and the knock-down's pose (vr/knock_pose.h). A skeleton longer than the cap is not read:
// the monster keeps the game's one capsule, no limbs (the log names its bone count). Pure: no game, no windows.h.
//
// Serg, 0.192.36 (the Ancients read under 256): «может ползунок добавить сколько костей считать, чтобы люди могли сами настроить». The arrays are sized
// for the slider's top (kCeiling); the cap read now is Max(). A change is live: the rig reads made under the old cap are made again (vr/skeletons.cpp).
#pragma once

#include <algorithm>
#include <atomic>

namespace bonecap {

constexpr int kFloor = 64;        // the slider's ends
constexpr int kCeiling = 1024;    // ... and the arrays' size
constexpr int kDefault = 256;     // (160 until 0.192.36: Arreat Summit's ancientbarb1 / 2 had no limbs)

inline std::atomic<int>& Cap() {
    static std::atomic<int> cap{kDefault};
    return cap;
}
inline int Max() { return std::clamp(Cap().load(std::memory_order_relaxed), kFloor, kCeiling); }
inline void Set(int n) { Cap().store(std::clamp(n, kFloor, kCeiling), std::memory_order_relaxed); }

}  // namespace bonecap
