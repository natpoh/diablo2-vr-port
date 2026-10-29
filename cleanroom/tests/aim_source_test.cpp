// Offline checks of where a staff's aim comes from (vr/aim_source.h): the controllers, never the weapon as the weight model draws it.
// No game, no headset. Run: build/Release/aim_source_test.exe   (exit code 0 = all passed)
//
// The report of 2026-10-14: with [weight] on and [hands] staff_free_left on, spells did not go where the hands point - the staff's aim came from the
// skeleton hook's staff axis, which the weight model's pose does not make, so it went stale and the aim fell to the plain hand's direction.

#include "../../vr/aim_source.h"

#include <cmath>
#include <cstdio>

using namespace aimsource;

static int g_failures = 0;

static void Expect(bool ok, const char* what) {
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s\n", what);
    }
}

// the user's setup: F4 body, native controllers both tracked, arms on the controllers, a staff in hand, staff_free_left on, staff_two_hands off
static In Mine() {
    In in;
    in.fullBody = in.sharedOk = in.staffWeapon = in.staffFreeLeft = in.armsOnControllers = true;
    in.handsValid = 3;
    return in;
}

int main() {
    // ---- [weight] off: the skeleton hook's axis, as always ----
    {
        In in = Mine();
        in.drawnFresh = true;
        Out o = Choose(in);
        Expect(o.aims && o.src == Src::Drawn && o.hand == 0, "weight off, a fresh hook axis: the drawn staff's axis from the right hand");
        in.drawnFresh = false;
        Expect(!Choose(in).aims, "weight off, the hook's axis stale: not a staff's aim (as it always was)");
        in.drawnFresh = true;
        in.handsValid = 2;
        Expect(!Choose(in).aims, "weight off, the right controller not tracked: no aim from it");
    }

    // ---- [weight] on (the report): the staff aims whatever the skeleton hook's axis says ----
    for (int stale = 0; stale < 2; ++stale) {
        In in = Mine();
        in.weightHas = true;
        in.drawnFresh = stale == 0;   // fresh or stale: the weight model's staff is never aimed by it
        in.holds[0] = true;
        in.carrying = 0;
        Out o = Choose(in);
        Expect(o.aims && o.src == Src::Shaft && o.hand == 0 && !o.leftOn, "weight on, the right hand alone: along the staff as the right controller grips it");
        in.holds[1] = true;
        o = Choose(in);
        Expect(o.aims && o.src == Src::LeftToRight && o.hand == 0 && o.leftOn, "weight on, both hands on it: from the left controller to the right one");
        in.holds[0] = false;
        in.carrying = 1;
        o = Choose(in);
        Expect(o.aims && o.src == Src::Shaft && o.hand == 1 && !o.leftOn, "weight on, only the left hand has it: along the staff as the left controller grips it");
        in.holds[0] = in.holds[1] = true;
        in.carrying = 0;
        in.handsValid = 1;
        o = Choose(in);
        Expect(o.aims && o.src == Src::Shaft && o.hand == 0 && !o.leftOn, "weight on, both hold but the left controller is not tracked: along the shaft in the right hand");
        in.handsValid = 3;
        in.holds[0] = in.holds[1] = false;
        in.carrying = -1;
        o = Choose(in);
        Expect(!o.aims, "weight on, no hand holds it (dropped, lying, on the back): not the staff's aim");
        in.holds[1] = true;
        in.carrying = 1;
        in.handsValid = 1;
        Expect(!Choose(in).aims, "weight on, the carrying hand is not tracked: no aim from it");
    }

    // ---- [bow] staff_two_hands (staff_free_left off): the weight model makes no difference ----
    {
        for (int weight = 0; weight < 2; ++weight)
            for (int fresh = 0; fresh < 2; ++fresh)
                for (int hv = 0; hv < 4; ++hv) {
                    In in = Mine();
                    in.staffFreeLeft = false;
                    in.staffTwoHands = true;
                    in.weightHas = weight != 0;
                    in.holds[0] = weight != 0;
                    in.carrying = weight ? 0 : -1;
                    in.drawnFresh = fresh != 0;
                    in.handsValid = (unsigned)hv;
                    const Out o = Choose(in);
                    Expect(o.aims == (hv == 3), "staff_two_hands: aims exactly when both controllers are tracked, weight or not");
                    if (o.aims) Expect(o.src == Src::LeftToRight && o.hand == 0 && o.leftOn, "staff_two_hands: from the left controller to the right one");
                }
    }

    // ---- a crossbow held the staff's way: the weight model does not hold it, and never changes its aim ----
    {
        In in = Mine();
        in.staffWeapon = false;
        in.xbowLike = true;
        in.staffFreeLeft = false;
        Expect(Choose(in).aims && Choose(in).src == Src::LeftToRight, "a crossbow: from hand to hand");
        in.weightHas = false;
        in.staffFreeLeft = true;
        in.drawnFresh = true;
        Expect(Choose(in).src == Src::Drawn, "a crossbow with staff_free_left: its stock as the hook posed it");
    }

    // ---- anything else is not a staff's aim, weight or not ----
    {
        In in = Mine();
        in.staffWeapon = false;   // a sword
        in.weightHas = true;
        in.holds[0] = true;
        in.carrying = 0;
        Expect(!Choose(in).aims, "a sword in hand: the aiming hand's own direction");
        in = Mine();
        in.weightHas = true;
        in.holds[0] = true;
        in.carrying = 0;
        in.fullBody = false;
        Expect(!Choose(in).aims, "not in the body: the aiming hand's own direction");
        in = Mine();
        in.staffFreeLeft = false;   // neither staff_free_left nor staff_two_hands: a staff is a plain weapon
        in.weightHas = true;
        in.holds[0] = true;
        in.carrying = 0;
        Expect(!Choose(in).aims, "a staff with both staff settings off: the aiming hand's own direction");
        in = Mine();
        in.sharedOk = false;
        Expect(!Choose(in).aims, "no controllers' block: no aim");
    }

    // ---- the shaft in a controller's frame follows the controller, not the weapon ----
    {
        const float shaft[3] = {0.0f, 0.0f, -1.0f}, id[4] = {0, 0, 0, 1};
        float out[3];
        TurnBy(id, shaft, out);
        Expect(std::fabs(out[0]) < 1e-6f && std::fabs(out[1]) < 1e-6f && std::fabs(out[2] + 1.0f) < 1e-6f, "an unturned controller: the shaft as given");
        // a quarter turn about the vertical (to the left): forward (-z) becomes -x
        const float s = std::sqrt(0.5f), yaw90[4] = {0, s, 0, s};
        TurnBy(yaw90, shaft, out);
        Expect(std::fabs(out[0] + 1.0f) < 1e-5f && std::fabs(out[1]) < 1e-5f && std::fabs(out[2]) < 1e-5f, "a controller turned a quarter to the left: the shaft turns with it");
        // a quarter turn about x (pitch up): forward becomes up
        const float pitch[4] = {s, 0, 0, s};
        TurnBy(pitch, shaft, out);
        Expect(std::fabs(out[0]) < 1e-5f && std::fabs(out[1] - 1.0f) < 1e-5f && std::fabs(out[2]) < 1e-5f, "a controller pitched up a quarter: the shaft points up");
        // unit length is kept for any turn
        const float q[4] = {0.18f, -0.52f, 0.31f, 0.0f};
        float qq[4] = {q[0], q[1], q[2], q[3]};
        qq[3] = std::sqrt(1.0f - q[0] * q[0] - q[1] * q[1] - q[2] * q[2]);
        const float v[3] = {0.3f, 0.5f, -0.81240384f};
        TurnBy(qq, v, out);
        const float l = std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]), l0 = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        Expect(std::fabs(l - l0) < 1e-5f, "a turn keeps the length");
    }

    // ---- every combination of the facts: an answer always names a hand that is tracked, and the weight model never aims what is not held ----
    {
        int checked = 0;
        for (int bits = 0; bits < (1 << 10); ++bits) {
            In in;
            in.fullBody = bits & 1;
            in.sharedOk = bits & 2;
            in.staffWeapon = bits & 4;
            in.staffTwoHands = bits & 8;
            in.staffFreeLeft = bits & 16;
            in.armsOnControllers = bits & 32;
            in.drawnFresh = bits & 64;
            in.weightHas = bits & 128;
            in.holds[0] = bits & 256;
            in.holds[1] = bits & 512;
            for (int hv = 0; hv < 4; ++hv)
                for (int carrying = -1; carrying <= 1; ++carrying) {
                    in.handsValid = (unsigned)hv;
                    in.carrying = carrying;
                    const Out o = Choose(in);
                    ++checked;
                    if (!o.aims) continue;
                    Expect(o.hand == 0 || o.hand == 1, "an aim names hand 0 or 1");
                    Expect(in.fullBody && in.sharedOk, "an aim needs the body and the controllers' block");
                    if (o.src == Src::Shaft) Expect(in.weightHas && (hv & (1 << o.hand)), "a shaft aim needs the weight model's weapon and that hand tracked");
                    if (o.src == Src::Drawn) Expect(!in.weightHas && in.drawnFresh, "the drawn axis is used only with [weight] off and a fresh axis");
                    if (o.src == Src::LeftToRight) Expect(o.hand == 0 && (hv & 3) == 3, "left->right needs both controllers");
                    if (o.src == Src::Shaft || o.src == Src::LeftToRight) Expect(!in.drawnFresh || in.weightHas || o.src == Src::LeftToRight, "a stale-able drawn axis is never what the weight model's aim leans on");
                }
        }
        Expect(checked > 1000, "the exhaustive loop ran");
    }

    if (g_failures) {
        std::printf("aim_source_test: %d FAILED\n", g_failures);
        return 1;
    }
    std::printf("aim_source_test: weight off (the hook's axis), weight on (the shaft as gripped, or hand to hand), staff_two_hands, a crossbow, other weapons, the turn into the controller's frame, every combination: all passed\n");
    return 0;
}
