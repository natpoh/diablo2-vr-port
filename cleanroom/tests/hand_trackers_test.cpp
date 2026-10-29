// Are the runtime's hand trackers made (vr/hand_trackers.h)? Runtime names as the runtimes report them: no game, no headset.
//
//   hand_trackers_test     (exit code 0 = all passed)

#include <cstdio>

#include "hand_trackers.h"

static int g_fail = 0, g_checks = 0;
#define CHECK(cond)                                                                   \
    do {                                                                              \
        ++g_checks;                                                                   \
        if (!(cond)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

using namespace handtrackers;

int main() {
    // the Quest 2 over Link (2026-10-21 log: "runtime Oculus 1.115.0"): none in Auto
    CHECK(Decide(Mode::Auto, "Oculus") == Why::MetaRuntime);
    CHECK(Decide(Mode::Auto, "Meta") == Why::MetaRuntime);
    // the other runtimes as before
    CHECK(Decide(Mode::Auto, "SteamVR/OpenXR") == Why::Made);
    CHECK(Decide(Mode::Auto, "VirtualDesktopXR") == Why::Made);
    CHECK(Decide(Mode::Auto, "Windows Mixed Reality Runtime") == Why::Made);
    CHECK(Decide(Mode::Auto, nullptr) == Why::Made);
    CHECK(Decide(Mode::Auto, "") == Why::Made);
    // a name with "Oculus" not at its start is not Meta's runtime
    CHECK(Decide(Mode::Auto, "OpenComposite for Oculus") == Why::Made);
    // 1 = 0.175: made everywhere, Meta's runtime too
    CHECK(Decide(Mode::Always, "Oculus") == Why::Made);
    CHECK(Decide(Mode::Always, "SteamVR/OpenXR") == Why::Made);
    // 0 = never
    CHECK(Decide(Mode::Never, "SteamVR/OpenXR") == Why::Off);
    CHECK(Decide(Mode::Never, "Oculus") == Why::Off);
    // the ini's values
    CHECK(ModeOf(0) == Mode::Never);
    CHECK(ModeOf(-3) == Mode::Never);
    CHECK(ModeOf(1) == Mode::Always);
    CHECK(ModeOf(2) == Mode::Auto);
    CHECK(ModeOf(7) == Mode::Auto);

    std::printf("hand_trackers_test: %d of %d checks passed\n", g_checks - g_fail, g_checks);
    return g_fail ? 1 : 0;
}
