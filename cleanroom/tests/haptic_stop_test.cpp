// Offline checks of vr/haptic_stop.h ([openxr] haptics_stop): the timer that stops each controller pulse when its time is up.
// Real time, generous margins (a loaded machine). Run: haptic_stop_test.exe (exit 0 = all passed).
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

#include "haptic_stop.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

using Clock = std::chrono::steady_clock;
static std::atomic<int> g_calls[2];
static std::atomic<long long> g_at[2];   // ms since g_t0 of the last stop per hand
static Clock::time_point g_t0;

static long long Ms() { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - g_t0).count(); }
static void Sleep(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

int main() {
    hapticstop::Timer t;
    t.Arm(0, 10);   // not started: nothing armed
    CHECK(!t.Armed(0));
    CHECK(t.Start([](int hand) { ++g_calls[hand]; g_at[hand] = Ms(); }));
    CHECK(t.Start([](int) {}));   // a second start keeps the first thread and callback

    // a pulse is stopped at its time, on its own hand only
    g_t0 = Clock::now();
    t.Arm(0, 40);
    CHECK(t.Armed(0) && !t.Armed(1));
    Sleep(400);
    CHECK(g_calls[0] == 1 && g_calls[1] == 0);
    CHECK(g_at[0] >= 35 && g_at[0] < 350);
    CHECK(!t.Armed(0));

    // a newer pulse replaces the hand's deadline: the old, longer one never stops it later, the new one is stopped at its time
    g_t0 = Clock::now();
    t.Arm(1, 1000);
    t.Arm(1, 30);
    Sleep(300);
    CHECK(g_calls[1] == 1 && g_at[1] < 300);
    Sleep(900);
    CHECK(g_calls[1] == 1);   // the 1000 ms deadline is gone

    // ... and a short old one never cuts a longer new one short
    g_t0 = Clock::now();
    t.Arm(0, 30);
    t.Arm(0, 500);
    Sleep(250);
    CHECK(g_calls[0] == 1);   // not yet
    Sleep(700);
    CHECK(g_calls[0] == 2 && g_at[0] >= 480);

    // both hands at once
    t.Arm(0, 20);
    t.Arm(1, 25);
    Sleep(400);
    CHECK(g_calls[0] == 3 && g_calls[1] == 2);

    // Cancel: nothing is called for what was armed (the session going)
    t.Arm(0, 60);
    t.Arm(1, 60);
    t.Cancel();
    Sleep(300);
    CHECK(g_calls[0] == 3 && g_calls[1] == 2);
    CHECK(!t.Armed(0) && !t.Armed(1));

    // after Stop nothing is armed or called
    t.Stop();
    t.Arm(0, 5);
    Sleep(100);
    CHECK(g_calls[0] == 3 && !t.Armed(0));

    std::printf("haptic_stop_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
