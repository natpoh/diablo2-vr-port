// [body] flat_hide_shoulder_right / _left (vr/shoulder_pads.h): which bones of a hero rig carry a shoulder pad, which side, and the shrink.
// The names are the game's own (3.3's skeletons, read 2026-11-16).
#include "shoulder_pads.h"

#include <cmath>
#include <cstdio>

namespace {
int g_fails = 0;
void Check(bool ok, const char* what) {
    if (!ok) { ++g_fails; std::printf("FAIL: %s\n", what); }
}
}   // namespace

int main() {
    using namespace shoulderpads;
    {   // pad names of every class, and what is not a pad
        const char* pads[] = {"R_hvy_shoulder_pad", "L_med_pad_pivot", "R_shoulder_pad", "R_pad_pivot", "L_shoulderPad_hvy_bind_jnt",
                              "HVY_shoulderPad_plates_01_R_JNT", "MED_shoulderPad_L_JNT", "shoulderPad_r_hvy_bind_jnt", "R_heavy_shoulderPad_pivot",
                              "L_heavy_shoulderPad"};
        for (const char* p : pads) Check(IsPadName(p), p);
        const char* not_pads[] = {"R_Clavicle", "R_UpperArm", "L_shoulder_bind_jnt", "shoulder_R_JNT", "shoulder_twist_01_L_JNT", "kneePad_L_JNT",
                                  "R_hipPad", "HVY_kneePad_plate_R_JNT", "chest", "R_weapon_attach"};
        for (const char* p : not_pads) Check(!IsPadName(p), p);
    }
    {   // sides
        Check(SideOfName("R_hvy_pad_pivot") == kRight, "R_ prefix: right");
        Check(SideOfName("HVY_shoulderPad_L_JNT") == kLeft, "_L_ inside: left");
        Check(SideOfName("shoulderPad_r_med_bind_jnt") == kRight, "_r_ inside: right");
        Check(SideOfName("L_shoulderPad_bind_jnt") == kLeft, "L_ prefix: left");
        Check(SideOfName("shoulder_pad") == kEither, "no side in the name: either");
        Check(SideOfName("HVY_shoulderPad") == kEither, "HVY is no side");
    }
    {   // a Barbarian-like rig: the pads under the collarbones, the strap under the left pivot, the arms not marked
        const std::vector<std::string> names = {"root", "chest", "R_Clavicle", "R_UpperArm", "L_Clavicle", "L_UpperArm", "R_hvy_shoulder_pad",
                                                "R_hvy_pad_pivot", "L_shoulder_pad", "L_pad_pivot", "shoulder_strap_adjust", "R_shoulder_pad", "clip_adjust"};
        const std::vector<int> parent = {-1, 0, 1, 2, 1, 4, 2, 6, 4, 8, 9, 2, 11};
        const std::vector<char> m = Marks(names, parent);
        Check(m[0] == kNone && m[1] == kNone && m[2] == kNone && m[3] == kNone && m[4] == kNone && m[5] == kNone, "trunk, collarbones and arms: no pad");
        Check(m[6] == kRight && m[7] == kRight && m[11] == kRight && m[12] == kRight, "right pads and what hangs under them");
        Check(m[8] == kLeft && m[9] == kLeft && m[10] == kLeft, "left pad, its pivot and the strap under it");
        Check(List(names, m, kLeft) == "L_shoulder_pad L_pad_pivot shoulder_strap_adjust", "the log's list");

        float w[13][16] = {};
        for (int i = 0; i < 13; ++i) { w[i][0] = w[i][5] = w[i][10] = 2.0f; w[i][15] = 1.0f; w[i][12] = (float)i; }
        Check(Shrink(w, 13, m, true, false) == 4, "right box: four bones shrunk");
        Check(std::fabs(w[7][0] - 2.0f * kScale) < 1e-7f && w[7][12] == 7.0f, "shrunk in place");
        Check(w[9][0] == 2.0f && w[3][5] == 2.0f, "the left pad and the arm left alone");
        Check(Shrink(w, 13, m, false, false) == 0, "no box: nothing");
    }
    {   // a child listed before its parent, and a mismatch of sizes
        const std::vector<std::string> names = {"root", "R_pad_child", "R_shoulder_pad"};
        const std::vector<int> parent = {-1, 2, 0};
        const std::vector<char> m = Marks(names, parent);
        Check(m[1] == kRight && m[2] == kRight, "a child before its parent still follows it");
        Check(Marks(names, {-1}).size() == 3 && Marks(names, {-1})[2] == kNone, "parents of another size: nothing marked");
        Check(Hidden(kEither, false, true) && Hidden(kEither, true, false) && !Hidden(kEither, false, false), "either: by either box");
    }
    if (g_fails) { std::printf("%d failed\n", g_fails); return 1; }
    std::printf("shoulder_pads_test: all passed\n");
    return 0;
}
