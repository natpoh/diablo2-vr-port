// Offline checks of tools/settings/bw_path.h: what D2R VR Settings' Status row says of BodyWalk's path to the game - Game Link direct (raw
// controllers, head, hands, walk; no gamepad, no Mapping) or BodyWalk's gamepad and Mapping, and why.
// Run: build/Release/bw_path_test.exe (exit code 0 = passed)

#include <cstdio>
#include <cwchar>
#include <initializer_list>

#include "../../tools/settings/bw_path.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

using namespace bwpath;

static Facts Direct() {
    Facts f;
    f.trackingLive = true;
    f.trackingVersion = 2;
    f.trackingFlags = BW_LINK_TRACK_HEAD | BW_LINK_TRACK_CONTROLS;
    f.walkLive = true;
    f.directInput = true;
    f.flatVrControls = true;
    return f;
}

int main() {
    CHECK(Decide(Direct()) == Path::Direct && ToneOf(Path::Direct) == Tone::Ok);
    CHECK(std::wcsstr(Text(Path::Direct), L"Game Link direct") != nullptr);
    Facts f = Direct();
    f.walkLive = false;   // BodyWalk's Output is not Game Link
    CHECK(Decide(f) == Path::DirectNoWalk && ToneOf(Path::DirectNoWalk) == Tone::Warn);
    f = Direct();
    f.trackingVersion = 1;   // an older game_link.dll
    CHECK(Decide(f) == Path::PadOldBodyWalk);
    f = Direct();
    f.trackingFlags = BW_LINK_TRACK_HEAD;   // no raw controls vouched for
    CHECK(Decide(f) == Path::PadOldBodyWalk);
    f = Direct();
    f.trackingFlags = BW_LINK_TRACK_CONTROLS;   // the headset not tracking: nothing to place the hands from
    CHECK(Decide(f) == Path::PadOldBodyWalk);
    f = Direct();
    f.directInput = false;
    CHECK(Decide(f) == Path::PadDirectOff);
    f = Direct();
    f.flatVrControls = false;
    CHECK(Decide(f) == Path::PadSwitchedOff);
    f = Direct();
    f.trackingLive = false;   // no BodyWalk tracking at all: the other rows say why
    CHECK(Decide(f) == Path::NoTracking && ToneOf(Path::NoTracking) == Tone::Unknown);
    for (Path p : {Path::NoTracking, Path::Direct, Path::DirectNoWalk, Path::PadOldBodyWalk, Path::PadDirectOff, Path::PadSwitchedOff})
        CHECK(Text(p) && Text(p)[0] != 0);
    if (g_failures) { std::printf("%d check(s) FAILED\n", g_failures); return 1; }
    std::printf("bw_path_test: all %d checks passed\n", g_checks);
    return 0;
}
