// [body] flat_weapon_adjust (vr/flat_weapon.h): the monitor's first person, each arm with its weapon moved along the camera's axes - out to
// the hand's side, up, ahead - whatever way the camera faces in the model; which kind's numbers a hand takes; the bones that move; the gate.
#include "flat_weapon.h"

#include <cmath>
#include <cstdio>
#include <cwchar>

namespace {
int g_fails = 0;
void Check(bool ok, const char* what) {
    if (!ok) { ++g_fails; std::printf("FAIL: %s\n", what); }
}
bool Near(float a, float b, float tol = 1e-4f) { return std::fabs(a - b) <= tol; }
bool NearV(const flatweapon::V& v, float x, float y, float z, float tol = 1e-4f) { return Near(v.x, x, tol) && Near(v.y, y, tol) && Near(v.z, z, tol); }
}   // namespace

int main() {
    using namespace flatweapon;
    const int unarmed = 1, count = 18, axe = 5, sword = 4;
    {   // which kind's numbers
        Check(KindFor(0, axe, 0, unarmed, count) == axe, "right hand: its own kind");
        Check(KindFor(1, axe, sword, unarmed, count) == sword, "left hand with a weapon of its own: its kind");
        Check(KindFor(1, axe, 0, unarmed, count) == axe, "left hand with nothing known: the right hand's kind");
        Check(KindFor(1, axe, unarmed, unarmed, count) == axe, "left hand unarmed (a shield, an effect): the right hand's kind");
        Check(KindFor(1, axe, count, unarmed, count) == axe, "left hand's kind out of range: the right hand's");
    }
    {   // the frame: camera right / ahead on the ground, unit, up +y
        const float r[3] = {2.0f, 0.3f, 0.0f}, f[3] = {0.0f, -0.8f, 3.0f};
        const Frame fr = GroundFrame(r, f);
        Check(fr.ok, "a camera's right and forward: a frame");
        Check(NearV(fr.right, 1, 0, 0) && NearV(fr.ahead, 0, 0, 1) && NearV(fr.up, 0, 1, 0), "flattened and made unit, up +y");
        const float down[3] = {0.0f, -1.0f, 0.0f};
        Check(!GroundFrame(r, down).ok, "a forward straight down has no ground part: no frame");
        Check(!GroundFrame(r, r).ok, "right and forward along each other: no frame");
        const float nan[3] = {NAN, 0.0f, 1.0f};
        Check(!GroundFrame(nan, f).ok, "not finite: no frame");
        Check(!GroundFrame(nullptr, f).ok, "no right: no frame");
    }
    {   // the shift: out, up, ahead in model units (upm 4: 25 cm = 1 unit)
        const float r[3] = {1, 0, 0}, f[3] = {0, 0, 1};
        const Frame fr = GroundFrame(r, f);
        const float cm[3] = {25.0f, -30.0f, 37.5f};
        Check(NearV(Shift(fr, 0, cm, 4.0f), 1.0f, -1.2f, 1.5f), "right hand: + out = the camera's right");
        Check(NearV(Shift(fr, 1, cm, 4.0f), -1.0f, -1.2f, 1.5f), "left hand: + out = the camera's left, up and ahead the same");
        const float big[3] = {400.0f, -400.0f, NAN};
        Check(NearV(Shift(fr, 0, big, 1.0f), 0.4f, -0.4f, 0.0f), "clamped to +-40 cm, not finite = 0");
        Check(NearV(Shift(Frame{}, 0, cm, 4.0f), 0, 0, 0), "no frame: nothing moves");
    }
    {   // a camera turned in the model (and a mirrored model: its right is the other cross): the shift follows the camera's own axes
        const float a = 0.7f;
        const float r[3] = {std::cos(a), 0.0f, -std::sin(a)}, f[3] = {std::sin(a), 0.0f, std::cos(a)};
        const Frame fr = GroundFrame(r, f);
        const float cm[3] = {10.0f, 0.0f, 0.0f};
        const V s = Shift(fr, 0, cm, 10.0f);
        Check(Near(s.x, std::cos(a)) && Near(s.z, -std::sin(a)) && Near(s.y, 0.0f), "turned camera: out along its right");
        const float rm[3] = {-std::cos(a), 0.0f, std::sin(a)};   // a mirrored model space: the camera's right comes out the other way
        const V m = Shift(GroundFrame(rm, f), 0, cm, 10.0f);
        Check(Near(m.x, -std::cos(a)) && Near(m.z, std::sin(a)), "mirrored model: still the camera's right, as read, never a cross worked out here");
        const float ahead[3] = {0.0f, 0.0f, 20.0f};
        const V h = Shift(fr, 1, ahead, 10.0f);
        Check(Near(h.x, 2.0f * std::sin(a)) && Near(h.z, 2.0f * std::cos(a)), "ahead: along the camera's forward on the ground, either hand");
    }
    {   // units per metre, as PoseBody's
        Check(Near(UnitsPerMetre(6.4f, 1.6f), 4.0f), "6.4 units of eyes, 1.6 m: 4 a metre");
        Check(Near(UnitsPerMetre(100.0f, 1.6f), 8.0f) && Near(UnitsPerMetre(0.5f, 1.6f), 2.0f), "clamped 2 .. 8");
        Check(Near(UnitsPerMetre(3.2f, 0.1f), 4.0f), "a player under 0.8 m counts 0.8");
        Check(Near(UnitsPerMetre(NAN, 1.6f), 2.0f), "not finite: the smallest");
    }
    {   // the bones that move: the upper arm and all under it
        //            0 root, 1 spine, 2 clav R, 3 upper R, 4 fore R, 5 hand R, 6 weapon R, 7 clav L, 8 upper L, 9 finger R
        const int parent[10] = {-1, 0, 1, 2, 3, 4, 5, 1, 7, 5};
        char mark[10];
        Check(Subtree(parent, 10, 3, mark) == 5, "right upper arm: itself, forearm, hand, weapon, finger");
        Check(mark[3] && mark[4] && mark[5] && mark[6] && mark[9] && !mark[2] && !mark[7] && !mark[8] && !mark[0], "only those");
        Check(Subtree(parent, 10, 8, mark) == 1 && mark[8] && !mark[3], "left upper arm alone");
        Check(Subtree(parent, 10, -1, mark) == 0 && !mark[3], "no bone: none");
        Check(Subtree(parent, 10, 10, mark) == 0, "out of range: none");
    }
    {   // the gate
        Check(Gate(true, 0, true, 0, true) == Why::On, "flat, first person, the game's arms, numbers: on");
        Check(Gate(false, 0, true, 0, true) == Why::KeyOff, "key off");
        Check(Gate(true, 1, true, 0, true) == Why::NotFlat, "VR: not");
        Check(Gate(true, 0, false, 0, true) == Why::NotFirstPerson, "F1 / F2: not");
        Check(Gate(true, 0, true, 1, true) == Why::TestPose, "the test pose: not");
        Check(Gate(true, 0, true, 0, false) == Why::AllZero, "all zero: nothing");
        for (Why w : {Why::On, Why::KeyOff, Why::NotFlat, Why::NotFirstPerson, Why::TestPose, Why::AllZero}) Check(WhyText(w)[0] != '?', "every reason has its words");
    }
    {   // a two-handed hold: the other fist on the weapon's shaft (upm 4: 10 cm = 0.4 units), whatever the bone's scale
        const V origin{1.0f, 2.0f, 0.0f}, axis{0.0f, 3.0f, 0.0f};   // the attach bone's Y, scaled
        Check(FistOnShaft({1.2f, 1.5f, 0.0f}, origin, axis, 4.0f), "a fist 5 cm off the grip, below the attach bone (a sword's hilt): on it");
        Check(FistOnShaft({1.0f, 5.0f, 0.3f}, origin, axis, 4.0f), "a pole-arm's far hand 75 cm up the shaft, 7.5 cm off: on it");
        Check(!FistOnShaft({1.8f, 2.0f, 0.0f}, origin, axis, 4.0f), "a fist 20 cm away (the hand hanging free): not on it");
        Check(!FistOnShaft({1.0f, 7.2f, 0.0f}, origin, axis, 4.0f), "on the line but 1.3 m along: past the weapon");
        Check(!FistOnShaft({1.0f, 2.0f, 0.0f}, origin, {0.0f, 0.0f, 0.0f}, 4.0f), "a hidden slot's bone shrunk to a point: no shaft");
        Check(!FistOnShaft({1.0f, 2.0f, 0.0f}, origin, {NAN, 1.0f, 0.0f}, 4.0f), "not finite: no shaft");
        const bool right[2] = {true, false}, left[2] = {false, true}, both[2] = {true, true}, none[2] = {false, false};
        Check(Holder(right) == 0 && Holder(left) == 1 && Holder(both) == 0 && Holder(none) == -1, "the holder: the right first");
        Check(FistsTogether({0.0f, 0.0f, 0.0f}, {0.4f, 0.2f, 0.0f}, 4.0f), "two fists 11 cm apart on one grip: together");
        Check(!FistsTogether({0.0f, 0.0f, 0.0f}, {0.8f, 0.0f, 0.0f}, 4.0f), "20 cm apart: not");
        Check(!FistsTogether({0.0f, 0.0f, 0.0f}, {NAN, 0.0f, 0.0f}, 4.0f) && !FistsTogether({0, 0, 0}, {0, 0, 0}, 0.0f), "not finite / no scale: not");
        Check(ShowsWeapon({0.0f, 2.0f, 0.0f}) && !ShowsWeapon({0.0f, 0.0f, 0.0f}) && !ShowsWeapon({NAN, 1.0f, 0.0f}), "a slot shrunk to a point shows no weapon");
    }
    {   // the other arm goes over to the holder's shift, eased: no jump on a frame the fist is off the hilt
        float w = 0.0f;
        w = EaseHold(w, true, 1.0f / 60.0f);
        Check(w > 0.1f && w < 0.2f, "one frame held: a little of the way");
        for (int i = 0; i < 30; ++i) w = EaseHold(w, true, 1.0f / 60.0f);
        Check(Near(w, 1.0f), "held half a second: all the way");
        w = EaseHold(w, false, 1.0f / 60.0f);
        Check(w > 0.8f, "one frame let go: still nearly all");
        Check(Near(EaseHold(0.0f, true, 10.0f), 1.0f), "a long gap (a view change): at once, never more than 1");
        Check(Near(EaseHold(NAN, false, 0.01f), 0.0f) && Near(EaseHold(0.5f, true, NAN), 0.5f), "not finite: settled");
        const V own{-1.0f, -2.0f, 0.0f}, holder{1.0f, -2.0f, 0.5f};
        Check(NearV(Blend(own, holder, 1.0f), 1.0f, -2.0f, 0.5f), "held: the holder's shift, the grip kept");
        Check(NearV(Blend(own, holder, 0.0f), -1.0f, -2.0f, 0.0f), "free: its own");
        Check(NearV(Blend(own, holder, 0.5f), 0.0f, -2.0f, 0.25f) && NearV(Blend(own, holder, 7.0f), 1.0f, -2.0f, 0.5f), "between; clamped");
        // the paladin's case (0.201.2): 10 out / 12 down on both arms, a two-handed sword - the two hands no longer go 20 cm apart
        const float r[3] = {1, 0, 0}, f[3] = {0, 0, 1}, both[3] = {10.0f, -12.0f, 0.0f};
        const Frame fr = GroundFrame(r, f);
        const V right = Shift(fr, 0, both, 4.0f), leftOwn = Shift(fr, 1, both, 4.0f), leftHeld = Blend(leftOwn, right, 1.0f);
        Check(Near(right.x - leftOwn.x, 0.8f), "by their own numbers the hands part by 20 cm");
        Check(NearV(leftHeld, right.x, right.y, right.z), "held: the left moves with the right, nothing between them changes");
    }
    {   // the defaults: 0 - ticked, nothing moves until the player sets a slider (Serg 2026-11-04)
        for (float d : kDefaultCm) Check(d == 0.0f, "every default 0");
        const float zero[3] = {0, 0, 0}, some[3] = {0, -3, 0};
        Check(!Any(zero) && !Any(kDefaultCm) && Any(some) && !Any(nullptr), "Any");
        Check(Gate(true, 0, true, 0, Any(kDefaultCm)) == Why::AllZero, "ticked at the defaults: nothing to move");
        // its own ini, not VR's [weapon_<kind>]: its own section prefix, six keys, the right arm's first
        Check(std::wcscmp(kSectionPrefix, L"flat_weapon_") == 0 && std::wcscmp(kKeys[0], L"right_x") == 0 && std::wcscmp(kKeys[5], L"left_z") == 0, "own keys");
    }
    std::printf(g_fails ? "flat_weapon_test: %d FAILED\n" : "flat_weapon_test: all passed\n", g_fails);
    return g_fails ? 1 : 0;
}
