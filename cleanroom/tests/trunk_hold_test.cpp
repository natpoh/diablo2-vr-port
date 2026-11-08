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

    // [body] flat_hold_trunk_twist: the chest's turn about the vertical from its bind pose, and the turn that puts it back on the arms
    {
        auto yawMat = [](float a, float scale, float out[16]) {   // a model-space chest turned by a about the vertical (row vectors), scaled
            trunkhold::YawAbout(a, 0.0f, 0.0f, out);
            for (int i : {0, 2, 5, 8, 10}) out[i] *= scale;
            out[13] = 5.0f;
        };
        float bind[16], anim[16];
        const float d = 3.14159265f / 180.0f;
        yawMat(0.0f, 1.0f, bind);
        yawMat(70.0f * d, 1.0f, anim);
        Check(std::fabs(trunkhold::TwistYaw(bind, anim, 120.0f * d) - 70.0f * d) < 1e-4f, "twist: a chest turned 70 deg reads 70");
        yawMat(-30.0f * d, 1.0f, anim);
        Check(std::fabs(trunkhold::TwistYaw(bind, anim, 120.0f * d) + 30.0f * d) < 1e-4f, "twist: the other way, negative");
        yawMat(20.0f * d, 1.0f, bind);
        yawMat(65.0f * d, 2.5f, anim);
        Check(std::fabs(trunkhold::TwistYaw(bind, anim, 120.0f * d) - 45.0f * d) < 1e-4f, "twist: from a bind turned already, a scale in the anim: the difference");
        yawMat(0.0f, 1.0f, bind);
        yawMat(150.0f * d, 1.0f, anim);
        Check(std::fabs(trunkhold::TwistYaw(bind, anim, 120.0f * d) - 120.0f * d) < 1e-4f, "twist: capped");
        float zero[16] = {};
        Check(trunkhold::TwistYaw(bind, zero, 2.0f) == 0.0f, "twist: an empty matrix: none");
        // the turn: a hand 1 ahead of a chest at (2, *, 3), turned 90 deg, goes to the chest's side (+x: the turn's own sense, model z to +x)
        float t[16];
        trunkhold::YawAbout(90.0f * d, 2.0f, 3.0f, t);
        const float hx = 2.0f, hz = 4.0f;
        const float x2 = hx * t[0] + hz * t[8] + t[12], z2 = hx * t[2] + hz * t[10] + t[14];
        Check(std::fabs(x2 - 3.0f) < 1e-4f && std::fabs(z2 - 3.0f) < 1e-4f, "the turn: about the chest's vertical, in TwistYaw's sense");
        const float cx = 2.0f * t[0] + 3.0f * t[8] + t[12], cz = 2.0f * t[2] + 3.0f * t[10] + t[14];
        Check(std::fabs(cx - 2.0f) < 1e-4f && std::fabs(cz - 3.0f) < 1e-4f, "the turn: the chest itself stays");
    }

    if (g_fail) { std::printf("trunk_hold_test: %d FAILED\n", g_fail); return 1; }
    std::printf("trunk_hold_test: all passed\n");
    return 0;
}
