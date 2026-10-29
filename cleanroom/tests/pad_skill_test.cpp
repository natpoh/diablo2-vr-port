// Offline checks of vr/pad_skill.h: a pad report's skill slot press - the "skill held" that lets the hand aim a cast in [bow] mode 4. Every slot of the
// game's controller (wheel_types.h kSlotDefs) counts, the right trigger's RT slot too (a player's report, 0.182: Ice Bolt on the trigger flew where the
// last Fire Bolt had gone). Run: pad_skill_test.exe (exit 0 = all passed).
#include <cstdio>

#include "../../vr/pad_skill.h"
#include "../../vr/point_cast.h"
#include "../../vr/wheel_types.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static void TestEverySlot() {
    // each slot's own press (its button, or the right trigger where it names none), with the left trigger's row or not: a skill held
    for (int s = 0; s < wheel::kSlotCount; ++s) {
        const wheel::SlotDef& d = wheel::kSlotDefs[s];
        const uint8_t rt = d.button ? 0 : 255;
        CHECK(padskill::Pressed(d.button, rt));
        if (!padskill::Pressed(d.button, rt)) std::printf("  slot %d (%s) is not a skill held\n", s, d.label);
    }
    // the masks are the game's slot buttons, the same bits the slot table has
    CHECK(padskill::kA == wheel::kPadA && padskill::kB == wheel::kPadB && padskill::kX == wheel::kPadX && padskill::kY == wheel::kPadY && padskill::kRB == wheel::kPadRb);
    // the trigger's threshold is the one the point skills' filter reads the RT slot by
    CHECK(padskill::kRtDown == pointcast::kRtDown);
}

static void TestTrigger() {
    CHECK(!padskill::Pressed(0, 0));
    CHECK(!padskill::Pressed(0, padskill::kRtDown));        // at the threshold: not yet (XInput's "past")
    CHECK(padskill::Pressed(0, padskill::kRtDown + 1));
    CHECK(padskill::Pressed(0, 255));                         // [openxr] skill_rt_a's pulse: the trigger full
}

static void TestNotSkills() {
    // the left trigger alone picks a row, casts nothing; LB, the d-pad, Start, Back and the stick clicks hold no skill slot
    const uint16_t none[] = {0x0001, 0x0002, 0x0004, 0x0008, 0x0010, 0x0020, 0x0040, 0x0080, 0x0100};
    for (uint16_t b : none) CHECK(!padskill::Pressed(b, 0));
    CHECK(padskill::Pressed(0x0100 | 0x2000, 0));   // LB with B: B's slot
}

int main() {
    TestEverySlot();
    TestTrigger();
    TestNotSkills();
    std::printf("%s: %d checks, %d failures\n", g_failures ? "FAILED" : "passed", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
