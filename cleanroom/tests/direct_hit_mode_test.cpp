// direct_hit_mode_test.cpp - Settings' "Direct hit" selector over [gameplay] melee_direct and melee_direct_client (shared/direct_hit_mode.h).
//   direct_hit_mode_test.exe
#include "direct_hit_mode.h"

#include <cstdio>

using namespace directhit;

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

int main() {
    // each choice writes both keys, and reads back as itself
    for (int m = kOff; m <= kFull; ++m) {
        int d = -1, c = -1;
        ToKeys(m, &d, &c);
        CHECK(FromKeys(d, c) == m);
    }
    int d = -1, c = -1;
    ToKeys(kOff, &d, &c);   CHECK(d == 0 && c == 0);
    ToKeys(kRules, &d, &c); CHECK(d == 2 && c == 1);
    ToKeys(kFull, &d, &c);  CHECK(d == 1 && c == 2);
    ToKeys(7, &d, &c);      CHECK(d == 0 && c == 0);   // out of range: Off
    // inis of before
    CHECK(FromKeys(0, 0) == kOff);     // the shipped one
    CHECK(FromKeys(1, 0) == kFull);    // the old experimental box: every touch where the server runs
    CHECK(FromKeys(0, 1) == kRules);   // the author's test build: melee_direct_client=1 (and the cleave box) - By the rules
    CHECK(FromKeys(1, 1) == kFull);    // both old boxes: Fully on wins
    CHECK(FromKeys(2, 0) == kRules && FromKeys(0, 2) == kFull);
    CHECK(FromKeys(5, -1) == kOff);    // nonsense: Off
    std::printf("direct_hit_mode_test: %d checks, %d failed\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
