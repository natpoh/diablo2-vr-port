// trunk_hold.h - which pose each bone above the pelvis takes when the upper body is held: VR F4's [body] lock (everything above the pelvis
// in the bind pose, the hands' contents the animation's) and flat F3's [body] flat_hold_trunk (only the trunk's axis - spine_01 up to the head -
// in the bind pose, every other upper bone the animation's turn from its parent on that held trunk). Pure; the test is
// cleanroom/tests/trunk_hold_test.cpp, the game side vr/skeletons.cpp PoseBody.
#pragma once

#include <vector>

namespace trunkhold {

// The trunk's axis: the head, its parents down to spine_01 (both included). Empty when either is missing or the head is not above spine_01.
inline std::vector<char> Axis(int n, const int* parent, int head, int spine) {
    std::vector<char> axis(n > 0 ? n : 0, 0);
    if (n <= 0 || head < 0 || head >= n || spine < 0 || spine >= n) return {};
    for (int b = head, steps = 0; b >= 0 && b < n && steps <= n; b = parent[b], ++steps) {
        axis[b] = 1;
        if (b == spine) return axis;
    }
    return {};   // spine_01 is not under the head: nothing held
}

enum class Pose { Game, Root, Bind, Anim };   // the game's own; spine_01 at its bind place; bind turn from its parent; the animation's turn from its parent

// One bone. upper: spine_01 or above it; root: spine_01 itself (or a bone with no parent); inHand: what a hand holds (and the fingers).
// lock: VR F4's whole upper body held; hold: flat F3's trunk only (with the axis above).
inline Pose For(bool upper, bool root, bool inHand, bool lock, bool hold, bool onAxis) {
    if (!upper || !(lock || hold)) return Pose::Game;
    if (root) return Pose::Root;
    if (inHand) return Pose::Anim;
    if (lock) return Pose::Bind;
    return onAxis ? Pose::Bind : Pose::Anim;
}

}  // namespace trunkhold
