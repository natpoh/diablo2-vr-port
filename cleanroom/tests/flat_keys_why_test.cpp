// Offline checks of vr/flat_keys_why.h: why W A S D do not walk the hero in flat F2 / F3, and the log of it. No game, no window.
// Run: build/Release/flat_keys_why_test.exe (exit code 0 = passed)

#include <cstdio>

#include "../../vr/flat_keys_why.h"

using namespace flatkeys;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

// A key held in flat F3 with everything in order; a test turns one thing off.
static Facts Good() {
    Facts f;
    f.keysHeld = true;
    return f;
}

static void TestDecide() {
    CHECK(Decide(Good()) == Why::Walks);
    Facts f = Good(); f.keysHeld = false; CHECK(Decide(f) == Why::NoKeys);
    f = Good(); f.mouseLook = false; CHECK(Decide(f) == Why::MouseLookOff);
    f = Good(); f.cameraOn = false; CHECK(Decide(f) == Why::CameraOff);
    f = Good(); f.inWorld = false; CHECK(Decide(f) == Why::NotInWorld);
    f = Good(); f.menuOpen = true; CHECK(Decide(f) == Why::Menu);
    f = Good(); f.focused = false; CHECK(Decide(f) == Why::NotInFront);
    f = Good(); f.chatOpen = true; CHECK(Decide(f) == Why::Chat);
    f = Good(); f.gameAsked = false; CHECK(Decide(f) == Why::GameNotAsking);
    f = Good(); f.gateOk = false; CHECK(Decide(f) == Why::GateBlocked);
    // the plugin's own reasons come first: they keep the keys from our walk before the game is asked at all
    f = Good(); f.menuOpen = true; f.gameAsked = false; f.gateOk = false; CHECK(Decide(f) == Why::Menu);
    f = Good(); f.mouseLook = false; f.menuOpen = true; CHECK(Decide(f) == Why::MouseLookOff);
    f = Good(); f.gameAsked = false; f.gateOk = false; CHECK(Decide(f) == Why::GameNotAsking);
    // nothing held: no verdict, whatever else is wrong
    f = Good(); f.keysHeld = false; f.mouseLook = false; f.menuOpen = true; CHECK(Decide(f) == Why::NoKeys);
    for (int w = 0; w <= (int)Why::GateBlocked; ++w) CHECK(Text((Why)w)[0] != '?');
}

static void TestWatch() {
    Watch w;
    Why say = Why::NoKeys;
    CHECK(!w.Step(Why::Walks, &say));          // walking from the start: no news
    CHECK(!w.Step(Why::NoKeys, &say));
    CHECK(w.Step(Why::GameNotAsking, &say) && say == Why::GameNotAsking);   // the keys stop walking: said once
    CHECK(!w.Step(Why::GameNotAsking, &say));
    CHECK(!w.Step(Why::NoKeys, &say));         // let go and pressed again into the same trouble: nothing new
    CHECK(!w.Step(Why::GameNotAsking, &say));
    CHECK(w.Step(Why::Walks, &say) && say == Why::Walks);                   // and back: said
    CHECK(!w.Step(Why::Walks, &say));
    CHECK(w.Step(Why::Menu, &say) && say == Why::Menu);
    CHECK(w.Step(Why::MouseLookOff, &say) && say == Why::MouseLookOff);     // one trouble after another: each said
    CHECK(w.Lines() == 4);
    // a key mashed between two states fills no log: kLines in all
    Watch m;
    for (uint32_t i = 0; i < 3 * Watch::kLines; ++i) m.Step(i % 2 ? Why::Walks : Why::GateBlocked, &say);
    CHECK(m.Lines() == Watch::kLines);
    say = Why::NoKeys;
    CHECK(!m.Step(Why::Chat, &say) && say == Why::NoKeys);
}

int main() {
    TestDecide();
    TestWatch();
    if (g_failures) { std::printf("flat_keys_why_test: %d of %d checks FAILED\n", g_failures, g_checks); return 1; }
    std::printf("flat_keys_why_test: all %d checks passed\n", g_checks);
    return 0;
}
