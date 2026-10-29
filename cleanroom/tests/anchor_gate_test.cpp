// Offline checks of vr/anchor_gate.h ([openxr] menus_wait_tracking): the flat picture and a menu's panel are placed only with the
// head tracked and the focus ours, and come up in front again after a long focus loss. Run: anchor_gate_test.exe (exit 0 = passed).
#include <cstdio>

#include "anchor_gate.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main() {
    using namespace anchorgate;
    {   // a start: views before the headset is tracked are followed, not placed; tracked and focused: placed
        Ready r;
        CHECK(!r.Step(false, true, 1000));
        CHECK(!r.Step(true, false, 1100));    // tracked, not focused yet
        CHECK(!r.Step(false, false, 1200));
        CHECK(r.Step(true, true, 1300));
        CHECK(r.Step(true, true, 1311));
        CHECK(!r.Step(false, true, 1400));    // tracking lost: follow again
        CHECK(r.Step(true, true, 1500));
    }
    {   // a runtime that never says "tracked": placed after kFallbackMs, as before this gate
        Ready r;
        CHECK(!r.Step(false, true, 5000));
        CHECK(!r.Step(false, true, 5000 + kFallbackMs - 1));
        CHECK(r.Step(false, true, 5000 + kFallbackMs));
        CHECK(r.Step(false, true, 5000 + kFallbackMs + 500));
        r.Reset();
        CHECK(!r.Step(false, true, 20000));   // a new session waits again
    }
    {   // a clock at 0 (the first tick) is not taken for "never"
        Ready r;
        CHECK(!r.Step(false, true, 0));
        CHECK(!r.Step(false, true, 10));
    }
    {   // the focus back after a long loss: once; a short loss or the first focus: nothing
        Refocus f;
        CHECK(!f.Step(false, 0));
        CHECK(!f.Step(true, 100));            // the first focus of the session
        CHECK(!f.Step(true, 200));
        CHECK(!f.Step(false, 1000));          // lost (the dashboard)
        CHECK(!f.Step(true, 1000 + kRefocusMs - 1));   // back soon: nothing moves (PS VR2's moments)
        CHECK(!f.Step(false, 5000));
        CHECK(!f.Step(false, 5000 + kRefocusMs));
        CHECK(f.Step(true, 5000 + kRefocusMs));        // back after a while: in front again, once
        CHECK(!f.Step(true, 5000 + kRefocusMs + 10));
        f.Reset();
        CHECK(!f.Step(true, 100000));         // a new session's first focus
    }
    std::printf("anchor_gate_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
