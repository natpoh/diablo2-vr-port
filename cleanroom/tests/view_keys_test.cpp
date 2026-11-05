// Offline checks of vr/view_keys.h: which F keys pick the view in each [view] keys mode, and which key messages the game does not get.
// No game, no window. Run: build/Release/view_keys_test.exe (exit code 0 = passed)

#include <cstdio>

#include "../../vr/view_keys.h"

using namespace viewkeys;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

constexpr unsigned F1 = 0x70, F3 = 0x72, F4 = 0x73, F5 = 0x74, F8 = 0x77, F12 = 0x7B;

static void TestIni() {
    CHECK(ModeFromIni(0) == kPlain);
    CHECK(ModeFromIni(1) == kShift);
    CHECK(ModeFromIni(2) == kNone);
    CHECK(ModeFromIni(3) == kPlain);    // out of range: as before
    CHECK(ModeFromIni(-1) == kPlain);
    CHECK(ModeName(kShift) != ModeName(kPlain) && ModeName(kNone) != ModeName(kPlain));
}

static void TestSwitches() {
    for (int k = 0; k < 4; ++k) {
        CHECK(Switches(kPlain, k, 4, false) && Switches(kPlain, k, 4, true));   // 0: F1 - F4, Shift or not, as before
        CHECK(!Switches(kShift, k, 4, false) && Switches(kShift, k, 4, true));  // 1: only with Shift
        CHECK(!Switches(kNone, k, 4, false) && !Switches(kNone, k, 4, true));   // 2: never
    }
    CHECK(!Switches(kPlain, 3, 3, false));   // flat: three views, F4 is not one
    CHECK(!Switches(kShift, 4, 4, true));    // F5 is no view
    CHECK(!Switches(kPlain, -1, 4, false));
}

static void TestPlain() {   // mode 0: exactly the old line - F1..F1+ViewCount down and up, always
    Gate g;
    CHECK(g.Take(kPlain, 4, F1, true, false, false));
    CHECK(g.Take(kPlain, 4, F1, true, true, false));
    CHECK(g.Take(kPlain, 4, F1, false, false, false));
    CHECK(g.Take(kPlain, 4, F4, true, false, true));
    CHECK(g.Take(kPlain, 4, F4, false, false, false));
    CHECK(!g.Take(kPlain, 4, F5, true, false, false));
    CHECK(!g.Take(kPlain, 3, F4, true, false, false));   // flat: F4 is the game's
    CHECK(!g.Take(kPlain, 4, F12, true, false, false));
    CHECK(!g.Take(kPlain, 4, 'W', true, false, false));
}

static void TestShift() {
    Gate g;
    // a plain F1: the game's skill hotkey, its repeats and its release too
    CHECK(!g.Take(kShift, 4, F1, true, false, false));
    CHECK(!g.Take(kShift, 4, F1, true, true, false));
    CHECK(!g.Take(kShift, 4, F1, false, false, false));
    // Shift + F3: ours, the press, its repeats, its release
    CHECK(g.Take(kShift, 4, F3, true, false, true));
    CHECK(g.Take(kShift, 4, F3, true, true, true));
    CHECK(g.Take(kShift, 4, F3, false, false, true));
    // Shift + F1, Shift let go before F1: the repeats and the release stay ours (no lone release, no stray hotkey)
    CHECK(g.Take(kShift, 4, F1, true, false, true));
    CHECK(g.Take(kShift, 4, F1, true, true, false));
    CHECK(g.Take(kShift, 4, F1, false, false, false));
    // a plain F4 held, Shift pressed while it repeats: still the game's, release too
    CHECK(!g.Take(kShift, 4, F4, true, false, false));
    CHECK(!g.Take(kShift, 4, F4, true, true, true));
    CHECK(!g.Take(kShift, 4, F4, false, false, true));
    // F5 - F8: the game's, Shift or not
    CHECK(!g.Take(kShift, 4, F5, true, false, true));
    CHECK(!g.Take(kShift, 4, F8, true, false, false));
    // flat (three views): Shift + F4 is the game's
    CHECK(!g.Take(kShift, 3, F4, true, false, true));
    CHECK(g.taken == 0);
}

static void TestNone() {
    Gate g;
    for (unsigned vk = F1; vk <= F8; ++vk) {
        CHECK(!g.Take(kNone, 4, vk, true, false, false));
        CHECK(!g.Take(kNone, 4, vk, true, false, true));
        CHECK(!g.Take(kNone, 4, vk, false, false, false));
    }
}

static void TestModeChangedMidPress() {
    // Settings switched to 2 while a Shift + F2 taken in 1 is still held: its release is still ours (the game never saw the press)
    Gate g;
    CHECK(g.Take(kShift, 4, F1 + 1, true, false, true));
    CHECK(g.Take(kNone, 4, F1 + 1, false, false, false));
    CHECK(g.taken == 0);
    // switched 0 -> 1 while F1 is held: the release goes as 0 took the press
    Gate h;
    CHECK(h.Take(kPlain, 4, F1, true, false, false));
    CHECK(h.Take(kShift, 4, F1, false, false, false));
}

// [view] own_hotkeys: F9 - F12 (the plugin's own keys) kept from the game, or not
static void TestOwnHotkeys() {
    constexpr unsigned F9 = 0x78, F10 = 0x79, F11 = 0x7A;
    CHECK(IsOwnHotkey(F9) && IsOwnHotkey(F10) && IsOwnHotkey(F11) && IsOwnHotkey(F12));
    CHECK(!IsOwnHotkey(F8) && !IsOwnHotkey(F12 + 1) && !IsOwnHotkey(F1) && !IsOwnHotkey('P'));
    OwnGate g;
    // off: as before - every message the game's
    for (unsigned vk = F9; vk <= F12; ++vk) {
        CHECK(!g.Take(false, vk, true, false));
        CHECK(!g.Take(false, vk, true, true));    // its repeat
        CHECK(!g.Take(false, vk, false, false));   // its release
    }
    CHECK(g.taken == 0);
    // on: the press, its repeats and its release ours (F11 the recentre, F12 the next view - not the game's automap party toggles)
    CHECK(g.Take(true, F11, true, false));
    CHECK(g.Take(true, F11, true, true));
    CHECK(g.Take(true, F11, false, false));
    CHECK(g.Take(true, F10, true, false) && g.Take(true, F10, false, false));
    // other keys: never
    CHECK(!g.Take(true, F8, true, false) && !g.Take(true, F8, false, false));
    CHECK(!g.Take(true, F1, true, false) && !g.Take(true, 'W', true, false));
    CHECK(g.taken == 0);
    // switched off while F12 is held: its repeats and release still ours (the game never saw it go down)
    CHECK(g.Take(true, F12, true, false));
    CHECK(g.Take(false, F12, true, true));
    CHECK(g.Take(false, F12, false, false));
    // switched on while F9 is held: the game saw it go down - its repeats and release stay the game's
    CHECK(!g.Take(false, F9, true, false));
    CHECK(!g.Take(true, F9, true, true));    // its repeat after the switch: still the game's
    CHECK(!g.Take(true, F9, false, false));
    CHECK(g.taken == 0);
    // a lone release (pressed before the window had focus): the game's
    CHECK(!g.Take(true, F11, false, false));
}

int main() {
    TestIni();
    TestSwitches();
    TestPlain();
    TestShift();
    TestNone();
    TestModeChangedMidPress();
    TestOwnHotkeys();
    std::printf("view_keys_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
