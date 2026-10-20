// [gameplay] melee_skill_armed (vr/melee_arm.h NeedOf / Fits): an armed skill the weapon in hand cannot use is the plain attack - the
// Amazon armed with a spear skill and swinging a sword went to the game as that skill, refused: no attack (the author, 2026-10-20).
//
//   melee_arm_fit_test        exit code 0 = all passed

#include "melee_arm.h"

#include <cstdio>
#include <string_view>

using namespace meleearm;

namespace {
int g_failed = 0, g_checks = 0;
void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_failed; std::printf("FAIL: %s\n", what); }
}
}  // namespace

int main() {
    for (int id : {10, 14, 19, 24, 30, 34}) {
        Check(NeedOf(id) == Need::Spear, "the Amazon's spear skills need a spear or a javelin");
        Check(Fits(id, true, false), "... a spear (or a javelin) in hand: the skill");
        Check(!Fits(id, false, false), "... a sword in hand: the plain attack");
        Check(!Fits(id, false, true), "... a dagger in hand: the plain attack");
    }
    Check(NeedOf(73) == Need::Dagger && Fits(73, false, true) && !Fits(73, true, false), "Poison Dagger: a dagger only");
    for (int id : {126, 147, 133, 97, 106, 254, 232, 151})
        Check(NeedOf(id) == Need::Any && Fits(id, false, false) && Fits(id, true, false), "the others: any melee weapon (the game checks forms and claws)");
    for (const Skill& s : kRows)
        Check(std::string_view(s.cls) != "Amazon" || NeedOf(s.row.id) == Need::Spear, "every Amazon skill in the list is a spear skill");
    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
