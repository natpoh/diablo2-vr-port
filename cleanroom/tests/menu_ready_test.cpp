// vr/menu_ready.h: no window move and no mouse message to the game before its UI system is up (crash group J, rows 270 / 271). Run: menu_ready_test.exe
#include <cstdio>

#include "menu_ready.h"

namespace {
int g_fails = 0, g_checks = 0;
#define CHECK(c)                                                                \
    do {                                                                        \
        ++g_checks;                                                             \
        if (!(c)) { ++g_fails; std::printf("  FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

using namespace menuready;

void TestUp() {
    // 270 / 271: step 15 of the start, nothing heard yet, the window seen a moment ago - not up
    CHECK(!Up(false, false, false, 1000, 1000 + 1300));
    CHECK(!Up(false, false, false, 1000, 1000 + 12000));   // 271's step 15 took 12 s: still not up
    CHECK(!Up(false, false, false, 0, 99999));              // no window seen yet
    // up: the panel manager spoke (the logo panel at the start's end), the hero in a world, the title's world settled
    CHECK(Up(true, false, false, 1000, 1001));
    CHECK(Up(false, true, false, 0, 0));
    CHECK(Up(false, false, true, 0, 0));
    // never stuck: a loader that sends no UI messages - up kFallbackMs after the window was seen
    CHECK(!Up(false, false, false, 1000, 1000 + kFallbackMs - 1));
    CHECK(Up(false, false, false, 1000, 1000 + kFallbackMs));
    CHECK(kFallbackMs >= 20000 && kFallbackMs <= 60000);   // longer than a slow step 15, short enough not to annoy
}

void TestMouse() {
    CHECK(IsMouseMessage(0x0200));    // WM_MOUSEMOVE
    CHECK(IsMouseMessage(0x0201));    // WM_LBUTTONDOWN (270)
    CHECK(IsMouseMessage(0x0202));    // WM_LBUTTONUP
    CHECK(IsMouseMessage(0x0204));    // WM_RBUTTONDOWN
    CHECK(IsMouseMessage(0x020A));    // WM_MOUSEWHEEL
    CHECK(IsMouseMessage(0x020E));    // WM_MOUSEHWHEEL
    CHECK(IsMouseMessage(0x02A3));    // WM_MOUSELEAVE (271)
    CHECK(IsMouseMessage(0x02A1));    // WM_MOUSEHOVER
    CHECK(!IsMouseMessage(0x00A0));   // WM_NCMOUSEMOVE: the frame's, Windows' own
    CHECK(!IsMouseMessage(0x0100));   // WM_KEYDOWN
    CHECK(!IsMouseMessage(0x0010));   // WM_CLOSE: the quit always gets through
    CHECK(!IsMouseMessage(0x0005));   // WM_SIZE
    CHECK(!IsMouseMessage(0x0003));   // WM_MOVE
    CHECK(!IsMouseMessage(0x00FF));   // WM_INPUT
    CHECK(DropBeforeMenu(0x0201, false));
    CHECK(DropBeforeMenu(0x02A3, false));
    CHECK(!DropBeforeMenu(0x0201, true));   // the menus up: every click as before
    CHECK(!DropBeforeMenu(0x0010, false));
    CHECK(!MayMoveWindow(false));
    CHECK(MayMoveWindow(true));
}
}  // namespace

int main() {
    TestUp();
    TestMouse();
    std::printf("%d of %d checks passed\n", g_checks - g_fails, g_checks);
    return g_fails ? 1 : 0;
}
