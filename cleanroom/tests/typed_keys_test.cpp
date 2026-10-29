// typed_keys_test - [openxr] laser_typing (vr/typed_keys.h, after bearstonem/d2r-pcvr 34f20a8, MIT) without the game: a real key lets
// the keyboard layout in for kTypingMs, the plugin's own keys (kOwnKeyTag) never, another program's extra information still counts.
//   typed_keys_test        exit code 0 = all passed

#include <cstdio>

#include "typed_keys.h"

using namespace typedkeys;

static int g_failed = 0, g_checks = 0;
static void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_failed; std::printf("FAIL: %s\n", what); }
}

int main() {
    Typing t;
    Check(!t.Active(100), "no key: the laser keeps the controller layout");
    Check(!t.Key(100, kOwnKeyTag) && !t.Active(100), "the plugin's own Ctrl, Shift and Esc never count as typing");
    Check(t.Key(100, 0) && t.Active(100) && t.Active(100 + kTypingMs - 1), "a real key lets the keyboard layout in");
    Check(!t.Active(100 + kTypingMs), "for a moment only: the laser's next clicks keep the controller layout again");
    Check(t.Key(500, 0x1234) && t.Active(500 + kTypingMs - 1), "another program's extra information is still a real key");
    Check(!t.Key(600, kOwnKeyTag) && t.Active(700) && !t.Active(500 + kTypingMs), "an own key between neither lengthens nor ends it");
    Check(kOwnKeyTag != 0, "the tag is not a plain key's 0");
    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
