// [hud] flat_map_turn_key / flat_map_place_key / flat_map_corner (vr/flat_map_keys.h): the flat game's map turned and put in the middle by keys.
#include "flat_map_keys.h"

#include <cstdio>

namespace {
int g_fails = 0;
void Check(bool ok, const char* what) {
    if (!ok) { ++g_fails; std::printf("FAIL: %s\n", what); }
}
}   // namespace

int main() {
    using namespace flatmapkeys;
    Check(KeyOf(0x6F, kDefaultTurnKey) == 0x6F && KeyOf(0, kDefaultTurnKey) == 0 && KeyOf(-1, kDefaultTurnKey) == kDefaultTurnKey &&
          KeyOf(300, kDefaultPlaceKey) == kDefaultPlaceKey, "a key from the ini: its code, 0 none, nonsense the default");
    Check(CornerPlace(0) == 1 && CornerPlace(1) == 3 && CornerPlace(7) == 3, "the corner: 0 left (place 1), else right (place 3)");
    Check(NextPlace(0, 1) == 2 && NextPlace(1, 1) == 2 && NextPlace(3, 0) == 2, "the game's own place or a corner: the key puts it in the middle");
    Check(NextPlace(2, 0) == 1 && NextPlace(2, 1) == 3, "in the middle: back to the corner chosen in the options");
    Check(NextPlace(NextPlace(3, 1), 1) == 3, "two presses: where it was (a corner)");
    Check(Of(0x6F, 0x6F, 0x6A) == Act::Turn && Of(0x6A, 0x6F, 0x6A) == Act::Place && Of('W', 0x6F, 0x6A) == Act::None, "the keys");
    Check(Of(0, 0, 0) == Act::None && Of(0x6F, 0, 0) == Act::None, "no keys set: nothing is ours");
    Check(Of('M', 'M', 'M') == Act::Turn, "one key set for both: the turn's");
    Check(Takes(true, true, false, false), "flat, in a game, no chat, no panel: ours");
    Check(!Takes(false, true, false, false) && !Takes(true, false, false, false), "VR or not in a game: the game's");
    Check(!Takes(true, true, true, false) && !Takes(true, true, false, true), "the chat line or a panel up: the game's");
    std::printf("flat_map_keys_test: %s (%d failed)\n", g_fails ? "FAILED" : "ok", g_fails);
    return g_fails ? 1 : 0;
}
