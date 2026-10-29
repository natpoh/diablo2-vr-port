// [body] lock (VR F4) and [body] flat_hold_trunk (flat F3), vr/trunk_hold.h: the trunk's axis in a rig, and which pose each bone above the
// pelvis takes - F4 exactly as before, flat F3 the axis held and the arms the game's.
#include "trunk_hold.h"

#include <cstdio>

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; }
}

using namespace trunkhold;

int main() {
    // a rig: 0 root, 1 pelvis, 2 spine_01, 3 spine_02, 4 spine_03, 5 neck, 6 head, 7 jaw, 8 R clavicle, 9 R upper arm, 10 R hand, 11 weapon, 12 L thigh
    const int parent[13] = {-1, 0, 1, 2, 3, 4, 5, 6, 4, 8, 9, 10, 1};
    const char upper[13] = {0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0};
    const char inHand[13] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0};
    const int head = 6, spine = 2;
    const std::vector<char> axis = Axis(13, parent, head, spine);
    Check(axis.size() == 13, "an axis in this rig");
    const int onAxis[] = {2, 3, 4, 5, 6}, offAxis[] = {0, 1, 7, 8, 9, 10, 11, 12};
    for (int b : onAxis) Check(axis[b] == 1, "spine_01 up through the neck to the head are the axis");
    for (int b : offAxis) Check(axis[b] == 0, "the root, the pelvis, the jaw, the arms, the weapon and the legs are not");
    Check(Axis(13, parent, 12, spine).empty(), "a 'head' not above spine_01: no axis, nothing held");
    Check(Axis(13, parent, -1, spine).empty() && Axis(13, parent, head, -1).empty() && Axis(0, parent, head, spine).empty(), "missing bones: none");
    const int loop[3] = {1, 0, -1};   // a broken rig whose head loops (0 -> 1 -> 0) and never reaches spine_01 (2)
    Check(Axis(3, loop, 0, 2).empty(), "a looping parent list ends");

    auto pose = [&](int i, bool lock, bool hold) {
        return For(upper[i] != 0, i == spine || parent[i] < 0, inHand[i] != 0, lock, hold, hold && !axis.empty() && axis[i]);
    };
    // neither: every bone the game's
    for (int i = 0; i < 13; ++i) Check(pose(i, false, false) == Pose::Game, "off: the game's animation everywhere");
    // VR F4's lock, as before: the whole upper body bind, spine_01 at its bind place, what the hand holds the animation's, the rest the game's
    for (int i = 0; i < 13; ++i) {
        const Pose want = !upper[i] ? Pose::Game : i == spine ? Pose::Root : inHand[i] ? Pose::Anim : Pose::Bind;
        Check(pose(i, true, false) == want, "F4: as before");
        Check(pose(i, true, true) == want, "F4 with the flat tick too: the lock wins");
    }
    // flat F3's hold: the axis bind, the arms, the jaw and the weapon the animation's on it, below the pelvis the game's (held by legs_under_body)
    Check(pose(2, false, true) == Pose::Root, "flat: spine_01 at its bind place");
    for (int b : {3, 4, 5, 6}) Check(pose(b, false, true) == Pose::Bind, "flat: the axis in the bind pose");
    for (int b : {7, 8, 9, 10, 11}) Check(pose(b, false, true) == Pose::Anim, "flat: the jaw, the arms, the hand and the weapon keep the game's swing");
    for (int b : {0, 1, 12}) Check(pose(b, false, true) == Pose::Game, "flat: the root, the pelvis and the legs are the game's");

    if (g_fail) { std::printf("trunk_hold_test: %d FAILED\n", g_fail); return 1; }
    std::printf("trunk_hold_test: all passed\n");
    return 0;
}
