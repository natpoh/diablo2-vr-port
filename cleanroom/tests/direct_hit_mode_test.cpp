// direct_hit_mode_test.cpp - Settings' "Direct hit" selector over [gameplay] melee_direct and melee_direct_client (shared/direct_hit_mode.h).
//   direct_hit_mode_test.exe
#include "direct_hit_mode.h"

#include <cstdio>

using namespace directhit;

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

int main() {
    // each choice writes both keys, and reads back as itself
    for (int m = kOff; m < kModes; ++m) {   // each of the four choices writes its keys and reads back as itself (quick blow on or off)
        int d = -1, c = -1, s = -2;
        ToKeys(m, &d, &c, &s);
        CHECK(FromKeys(d, c, s, true) == m && FromKeys(d, c, s, false) == m);
    }
    int d = -1, c = -1, s = -2;
    ToKeys(kOff, &d, &c, &s);    CHECK(d == 0 && c == 0 && s == -1);
    ToKeys(kCleave, &d, &c, &s); CHECK(d == 2 && c == 1 && s == 0);
    ToKeys(kRules, &d, &c, &s);  CHECK(d == 2 && c == 1 && s == 1);
    ToKeys(kFull, &d, &c, &s);   CHECK(d == 1 && c == 2 && s == -1);
    ToKeys(7, &d, &c, &s);       CHECK(d == 0 && c == 0 && s == -1);   // out of range: Off
    ToKeys(kRules, &d, &c);      CHECK(d == 2 && c == 1);              // (the scope pointer may be left out)
    // inis of before (no melee_direct_scope): By the rules is what quick blow made of it in 0.204
    CHECK(FromKeys(0, 0) == kOff);
    CHECK(FromKeys(1, 0) == kFull);    // the old experimental box: every touch where the server runs
    CHECK(FromKeys(0, 1, -1, false) == kRules && FromKeys(0, 1, -1, true) == kCleave);   // 0.203 / 0.204's By the rules
    CHECK(FromKeys(2, 1, -1, true) == kCleave && FromKeys(2, 1, -1, false) == kRules);
    CHECK(FromKeys(1, 1) == kFull);    // both old boxes: Always wins
    CHECK(FromKeys(2, 0) == kRules && FromKeys(0, 2) == kFull);
    CHECK(FromKeys(2, 1, 0, false) == kCleave && FromKeys(2, 1, 1, true) == kRules);   // the scope chosen wins over quick blow
    CHECK(FromKeys(5, -1) == kOff);    // nonsense: Off
    std::printf("direct_hit_mode_test: %d checks, %d failed\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
