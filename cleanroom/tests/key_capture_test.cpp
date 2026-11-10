// The overlays' "press a key" (vr/key_capture.h): names, the keys each use refuses, the capture - the answer, Esc, a refused key, the releases,
// a repeat, the time-out.
#include "key_capture.h"

#include <cstdio>
#include <cstring>

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; }
}

using namespace keycap;

int main() {
    char b[32];
    Text('E', b, sizeof b);
    Check(std::strcmp(b, "E") == 0, "E");
    Text(0x75, b, sizeof b);
    Check(std::strcmp(b, "F6") == 0, "F6");
    Text(0x63, b, sizeof b);
    Check(std::strcmp(b, "Num 3") == 0, "Num 3");
    Text(0x20, b, sizeof b);
    Check(std::strcmp(b, "Space") == 0, "Space");
    Text(0x05, b, sizeof b);
    Check(std::strcmp(b, "Mouse 4") == 0, "Mouse 4");
    Text(0xE7, b, sizeof b);
    Check(std::strcmp(b, "Key 231") == 0, "an unnamed key: its number");

    Check(Bindable('E', Use::PickupKey) && Bindable('F', Use::PickupKey) && Bindable(0x20, Use::PickupKey), "E, F, Space: a pick-up key");
    Check(!Bindable('W', Use::PickupKey) && !Bindable('D', Use::PickupKey), "W A S D walk: not the pick-up key");
    Check(!Bindable(0x12, Use::PickupKey) && !Bindable(0xA4, Use::GameKey), "Alt: never");
    Check(!Bindable(0x70, Use::PickupKey) && !Bindable(0x7B, Use::PickupKey) && Bindable(0x75, Use::PickupKey), "F1 - F4, F9 - F12 ours; F6 free");
    Check(Bindable('W', Use::GameKey) && Bindable(0x70, Use::GameKey) && Bindable(0x05, Use::GameKey), "a gesture's game key: W, F1, Mouse 4 too");
    Check(!Bindable(0x01, Use::GameKey) && !Bindable(0x0D, Use::GameKey) && !Bindable(0, Use::GameKey), "left click, Enter, nothing: never");
    Check(!Bindable(0x05, Use::PickupKey), "a mouse button is not the pick-up key");
    // [hud] flat_map_*_key: the map's keys
    Check(Bindable(0x6F, Use::MapKey) && Bindable(0x6A, Use::MapKey) && Bindable(0xBB, Use::MapKey) && Bindable('M', Use::MapKey), "Num /, Num *, =, M: a map key");
    Check(!Bindable(0x09, Use::MapKey) && !Bindable('W', Use::MapKey) && !Bindable(0x71, Use::MapKey) && !Bindable(0x79, Use::MapKey),
          "Tab (the game's map), W A S D, F2, F10: not a map key");
    Check(!Bindable(0x05, Use::MapKey) && !Bindable(0x0D, Use::MapKey) && !Bindable(0x10, Use::MapKey), "a mouse button, Enter, Shift: not a map key");

    {   // the answer
        Capture c;
        Check(c.Feed('E', true, false, 0).r == R::Pass, "not armed: the game's");
        c.Arm(7, Use::PickupKey, 1000);
        Check(c.Armed(1500) && c.Owner() == 7, "armed for row 7");
        Check(c.Feed('W', false, false, 1100).r == R::Pass, "a key held before (W) comes up in the game");
        Res r = c.Feed('W', true, false, 1200);
        Check(r.r == R::Refused && r.vk == 'W' && c.Armed(1200), "W: refused, still armed");
        Check(c.Feed('W', true, true, 1250).r == R::Swallow, "its repeat: swallowed");
        r = c.Feed('G', true, false, 1300);
        Check(r.r == R::Took && r.vk == 'G' && !c.Armed(1300), "G: taken, no longer armed");
        Check(c.Feed('G', false, false, 1400).r == R::Swallow, "G's release: ours too");
        Check(c.Feed('G', false, false, 1500).r == R::Pass && c.Feed('G', true, false, 1500).r == R::Pass, "then G is the game's again");
    }
    {   // Esc, the time-out
        Capture c;
        c.Arm(1, Use::GameKey, 0);
        Check(c.Feed(kEsc, true, false, 100).r == R::Cancelled && !c.Armed(100), "Esc cancels");
        Check(c.Feed(kEsc, false, false, 150).r == R::Swallow, "its release: ours (the panel does not close)");
        c.Arm(1, Use::GameKey, 1000);
        Check(!c.Armed(1000 + kTimeoutMs), "10 s: no longer armed");
        Check(c.Feed('E', true, false, 1000 + kTimeoutMs).r == R::TimedOut, "a press after the time-out: said, and the game's");
        c.Arm(1, Use::GameKey, 50000);
        Check(!c.Expire(50000 + 100) && c.Expire(50000 + kTimeoutMs), "Expire: once the time is out");
    }

    if (g_fail) { std::printf("%d failure(s)\n", g_fail); return 1; }
    std::printf("key_capture_test: all passed\n");
    return 0;
}
