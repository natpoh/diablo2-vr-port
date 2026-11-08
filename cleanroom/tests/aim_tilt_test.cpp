// [body] flat_aim_tilt, vr/aim_tilt.h: which bones tilt (the arms and what they hold, not the trunk, the neck or the legs; a weapon bone hung elsewhere
// taken along), the look's tilt (sign, share, cap), the target's tilt from the shoulders, the hold through a swing, the blend, and the turn on a pose.
#include "aim_tilt.h"

#include <cmath>
#include <cstdio>

static int g_fail = 0, g_checks = 0;
static void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; }
}
static bool Near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }
static float Deg(float r) { return r * 180.0f / attacklean::kPi; }
static float Rad(float d) { return d * attacklean::kPi / 180.0f; }

using namespace aimtilt;

// a rig: 0 root, 1 pelvis, 2 spine_01, 3 spine_02, 4 spine_03 (chest), 5 neck, 6 head, 7 R clavicle, 8 R upper arm, 9 R hand, 10 R weapon attach,
// 11 L clavicle, 12 L upper arm, 13 L shield attach hung under the root (a rig that does so), 14 L thigh, 15 a finger of the shield (under 13)
static const int kN = 16;
static const int kParent[kN] = {-1, 0, 1, 2, 3, 4, 5, 4, 7, 8, 9, 4, 11, 0, 1, 13};
static const char kInNeck[kN] = {0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static void TestMarks() {
    const int chest = attacklean::Chest(kN, kParent, 2, 8, 12);
    Check(chest == 4, "the chest: spine_03");
    const int extra[3] = {10, 13, -1};
    const std::vector<char> m = Marks(kN, kParent, chest, kInNeck, extra, 3);
    Check((int)m.size() == kN, "a mark for every bone");
    if ((int)m.size() != kN) return;
    const int yes[] = {7, 8, 9, 10, 11, 12, 13, 15}, no[] = {0, 1, 2, 3, 4, 5, 6, 14};
    for (int b : yes) Check(m[b] == 1, "tilts: the collarbones, arms, hands, the weapon; the shield bone hung under the root and its own");
    for (int b : no) Check(m[b] == 0, "stays: root, pelvis, the spine, the chest, the neck and head, the thigh");
    Check(Marks(kN, kParent, -1, kInNeck, extra, 3).empty(), "no chest: nothing tilts");
    const std::vector<char> plain = Marks(kN, kParent, chest, nullptr, nullptr, 0);
    Check(!plain.empty() && plain[5] && plain[6] && !plain[13], "without the neck's marks and extras: all under the chest, nothing else");
    const int loop[3] = {1, 2, 0};
    Check(Marks(3, loop, 0, nullptr, nullptr, 0).size() <= 3, "a looping parent list ends");
}

static void TestLook() {
    Check(Near(LookRad({0, 0, 1}, 100, 60), 0.0f, 1e-6f), "level: no tilt");
    const float c30 = std::cos(Rad(30)), s30 = std::sin(Rad(30));
    Check(Near(Deg(LookRad({0, -s30, c30}, 100, 60)), 30.0f, 0.01f), "30 down: 30 down (+)");
    Check(Near(Deg(LookRad({0, s30, c30}, 100, 60)), -30.0f, 0.01f), "30 up: 30 up (-)");
    Check(Near(Deg(LookRad({0, -s30, c30}, 50, 60)), 15.0f, 0.01f), "half the share: half the tilt");
    Check(Near(Deg(LookRad({0, -1, 0.01f}, 100, 60)), 60.0f, 0.01f), "straight down: capped at the most");
    Check(Near(Deg(LookRad({0, 1, 0.01f}, 100, 45)), -45.0f, 0.01f), "straight up: capped the other way");
    Check(Near(Deg(LookRad({0, -2 * s30, 2 * c30}, 100, 60)), 30.0f, 0.01f), "a forward not of unit length: the same");
    Check(LookRad({0, 0, 0}, 100, 60) == 0.0f && LookRad({NAN, 0, 1}, 100, 60) == 0.0f, "no forward: no tilt");
    Check(Near(Deg(LookRad({0, -s30, c30}, NAN, NAN)), 30.0f, 0.01f), "keys not numbers: the defaults (all the way, 60)");
    Check(LookRad({0, -s30, c30}, 0, 60) == 0.0f, "share 0: no tilt");
}

static void TestTarget() {
    float r = 0.0f;
    Check(TargetRad({0, 5, 0}, {0, 0, 5}, 60, &r) && Near(Deg(r), 45.0f, 0.01f), "a point as far below as ahead: 45 down");
    Check(TargetRad({0, 5, 0}, {3, 9, 0}, 60, &r) && Near(Deg(r), -53.13f, 0.02f), "above, to the side: up, by its own elevation");
    Check(TargetRad({0, 5, 0}, {0, -100, 1}, 60, &r) && Near(Deg(r), 60.0f, 0.01f), "far below: capped");
    Check(TargetRad({0, 5, 0}, {0, 5, 4}, 60, &r) && Near(r, 0.0f, 1e-6f), "level with the shoulders: none");
    Check(!TargetRad({0, 5, 0}, {0, 5, 0}, 60, &r), "the point at the shoulders: no way to it");
    Check(!TargetRad({0, 5, 0}, {NAN, 0, 0}, 60, &r), "not a number: none");
}

static void TestHold() {
    Hold h;
    h.Step(false, true, 0.5f, 0.0f);
    Check(!h.have, "a body under the crosshair while not attacking: no target");
    h.Step(true, true, 0.5f, 0.2f);
    Check(h.have && h.rad == 0.5f, "the swing starts on a body: its tilt held");
    h.Step(true, false, 0.0f, 0.8f);
    Check(h.have && h.rad == 0.5f, "the crosshair slips off it mid-swing: still held");
    h.Step(true, true, 0.3f, 1.0f);
    Check(h.rad == 0.3f, "a fresh body mid-swing: retaken");
    h.Step(false, false, 0.0f, 0.4f);
    Check(h.have, "the swing over, easing out: held");
    h.Step(false, false, 0.0f, 0.0f);
    Check(!h.have, "eased out: let go");
    h.Step(true, true, NAN, 0.5f);
    Check(!h.have, "a tilt not a number: not taken");
}

static void TestBlend() {
    Check(Blend(0.2f, false, 0.9f, 1.0f) == 0.2f, "no target: the look's");
    Check(Near(Blend(0.2f, true, 0.6f, 0.5f), 0.4f, 1e-6f), "half eased: half way");
    Check(Near(Blend(0.2f, true, 0.6f, 1.0f), 0.6f, 1e-6f), "fully eased: the target's");
    Check(Near(Blend(0.2f, true, 0.6f, 3.0f), 0.6f, 1e-6f) && Blend(0.2f, true, 0.6f, -1.0f) == 0.2f, "weight kept in 0..1");
}

static void TestTurn() {
    // the hand at (0, 5, 1), the shoulders' middle (0, 5, 0), facing +z: a 90 deg tilt down takes the hand to (0, 4, 0); the chest does not move
    float w[3][16] = {};
    for (auto& m : w) { m[0] = m[5] = m[10] = m[15] = 1.0f; }
    w[0][13] = 5.0f;                                  // 0: the chest at the shoulders
    w[1][13] = 5.0f; w[1][14] = 1.0f;                 // 1: the hand ahead
    w[2][13] = 6.0f;                                  // 2: the head above
    const std::vector<char> marks = {0, 1, 0};
    const V3 axis = attacklean::Axis({0, 0, 1});
    Turn(w, 3, marks, axis, Rad(90), Mid({-1, 5, 0}, {1, 5, 0}));
    Check(Near(w[1][12], 0.0f, 1e-5f) && Near(w[1][13], 4.0f, 1e-5f) && Near(w[1][14], 0.0f, 1e-5f), "+90 about the shoulders: the hand ahead goes down");
    Check(w[0][13] == 5.0f && w[2][13] == 6.0f && w[0][14] == 0.0f, "the chest and the head stay");
    Check(Near(w[1][5], 0.0f, 1e-5f) && Near(std::fabs(w[1][6]), 1.0f, 1e-5f), "the hand's own axes turn with it (the weapon's tilt)");
    float before[16];
    for (int i = 0; i < 16; ++i) before[i] = w[1][i];
    Turn(w, 3, marks, axis, 0.0f, {0, 5, 0});
    Turn(w, 3, marks, axis, NAN, {0, 5, 0});
    Turn(w, 3, std::vector<char>{1}, axis, Rad(10), {0, 5, 0});
    bool same = true;
    for (int i = 0; i < 16; ++i) same = same && w[1][i] == before[i];
    Check(same, "no tilt, not a number, marks of another rig: nothing moves");
}

int main() {
    TestMarks();
    TestLook();
    TestTarget();
    TestHold();
    TestBlend();
    TestTurn();
    std::printf("aim_tilt_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
