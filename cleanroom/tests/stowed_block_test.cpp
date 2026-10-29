// [hands] shield_stow_no_block (vr/stowed_block.h): which block checks are refused.
#include "stowed_block.h"

#include <cstdio>

static int g_fail = 0;
static void Expect(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; }
}

int main() {
    Expect(stowedblock::Refuse(true, true, true, 1), "on, put away, the hero, a block to roll: refused");
    Expect(!stowedblock::Refuse(false, true, true, 1), "the tick off: the game's block");
    Expect(!stowedblock::Refuse(true, false, true, 1), "the shield on an arm: the game's block");
    Expect(!stowedblock::Refuse(true, true, false, 1), "a monster or the mercenary defending: the game's block");
    Expect(!stowedblock::Refuse(true, true, true, 0), "no block to roll anyway: left as it is");
    Expect(stowedblock::Refuse(true, true, true, -1), "any block != 0 counts as a block to roll");
    if (g_fail) return 1;
    std::printf("stowed_block_test: ok\n");
    return 0;
}
