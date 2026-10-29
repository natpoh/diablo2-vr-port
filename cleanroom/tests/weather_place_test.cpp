// weather_place_test - [sky] weather_first_person (vr/weather_place.h) without the game: when the weather is moved, where it goes, and
// that a matrix as the game writes it (no turn, the place in the last row) gets only its place changed - anything else is left alone.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

#include "../../vr/weather_place.h"

namespace {
int g_failed = 0, g_checks = 0;
void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_failed; printf("  FAIL: %s\n", what); }
}
}  // namespace

int main() {
    using namespace weatherplace;

    // the setting
    Check(ClampMode(-1) == kGame && ClampMode(3) == kGame && ClampMode(1) == kHidden && ClampMode(2) == kAround, "the mode clamped: unknown = the game's");
    Check(!Wanted(kGame, true, true), "0: never moved (as before)");
    Check(Wanted(kHidden, true, true) && Wanted(kAround, true, true), "1, 2 in first person in the world: moved");
    Check(!Wanted(kHidden, false, true) && !Wanted(kAround, false, true), "not first person (F1, F2, from above): the game's place");
    Check(!Wanted(kAround, true, false), "not in the world: the game's place");
    Check(!Wanted(7, true, true), "an unknown value: the game's place");

    // where it goes
    const float game[3] = {20681.8f, 441.9f, 25876.8f}, head[3] = {20210.0f, 6.5f, 25400.0f};
    float at[3];
    Check(Place(kGame, game, head, at) && at[0] == game[0] && at[1] == game[1] && at[2] == game[2], "the game's: its camera's place");
    Check(Place(kHidden, game, head, at) && at[0] == game[0] && at[1] == game[1] - kHiddenDrop && at[2] == game[2], "hidden: far under the game's place");
    Check(Place(kAround, game, head, at) && at[0] == head[0] && at[1] == head[1] && at[2] == head[2], "round the viewer: the head's place");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float badHead[3] = {nan, 0.0f, 0.0f};
    Check(!Place(kAround, game, badHead, at), "no head yet (not finite): not moved");

    // the matrix: the game's (identity turn, the place in the last row)
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, game[0], game[1], game[2], 1};
    float want[16];
    std::memcpy(want, m, sizeof m);
    want[12] = head[0]; want[13] = head[1]; want[14] = head[2];
    Check(SetPlace(m, head) && std::memcmp(m, want, sizeof m) == 0, "the game's matrix: only its place changed");
    float turned[16] = {1, 0, 0, 0.5f, 0, 1, 0, 0, 0, 0, 1, 0, 1, 2, 3, 1};
    float before[16];
    std::memcpy(before, turned, sizeof turned);
    Check(!SetPlace(turned, head) && std::memcmp(turned, before, sizeof turned) == 0, "a matrix not of the game's kind (last column): untouched");
    float notFinite[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, nan, 2, 3, 1};
    Check(!SetPlace(notFinite, head), "a matrix not finite: untouched");

    // a root's place only
    Check(IsRoot(0xFFFFFu) && IsRoot(0xABCFFFFFu), "no parent (the low 20 bits all set): a root");
    Check(!IsRoot(0x00012345u), "a parent: not moved");

    Check(std::strcmp(ModeName(kHidden), "hidden") == 0 && std::strcmp(ModeName(kAround), "round the viewer") == 0, "the log's names");

    printf("weather_place_test: %d of %d checks passed\n", g_checks - g_failed, g_checks);
    return g_failed ? 1 : 0;
}
