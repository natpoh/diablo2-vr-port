// How native OpenXR's session ends while the game goes on (vr/session_end.h): a live end off the game's present, the exit asked for and waited for to STOPPING
// before xrEndSession, a time limit on that; the game's closing as before. Run: session_end_test.exe
#include <cstdio>
#include <initializer_list>

#include "session_end.h"

namespace {
int g_fails = 0, g_checks = 0;
#define CHECK(c)                                                                \
    do {                                                                        \
        ++g_checks;                                                             \
        if (!(c)) { ++g_fails; std::printf("  FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

using namespace sessionend;

void TestPlan() {
    // [openxr] on=0 live (the 2026-10-31 freeze): off the game's present, the queue waited for there, the exit asked for
    const Plan live = PlanFor(false, false, false, true);
    CHECK(live.offThread && live.waitQueue && live.requestExit);
    // the game closing: here, as before (the DLL goes)
    const Plan close = PlanFor(true, false, false, true);
    CHECK(!close.offThread && !close.waitQueue && !close.requestExit);
    // the runtime ended it (STOPPING, EXITING, the headset lost) or failed: off the present, no exit to ask for
    const Plan ended = PlanFor(false, false, false, false);
    CHECK(ended.offThread && ended.waitQueue && !ended.requestExit);
    // a call of ours stuck in the runtime: no request on top of it, still off the present
    const Plan stuck = PlanFor(false, false, true, true);
    CHECK(stuck.offThread && stuck.waitQueue && !stuck.requestExit);
    // the device gone: nothing waits on its queue, nothing asked of the runtime
    const Plan removed = PlanFor(false, true, false, true);
    CHECK(removed.offThread && !removed.waitQueue && !removed.requestExit);
}

void TestExitStep() {
    // running states: poll until the time is up
    for (int s : {kSynchronized, kVisible, kFocused, kReady})
        CHECK(ExitStep(s, 10.0, kExitTimeoutMs) == Step::Poll && ExitStep(s, kExitTimeoutMs, kExitTimeoutMs) == Step::Skip);
    // STOPPING: end it now (the only state xrEndSession takes - not NOT_STOPPING)
    CHECK(ExitStep(kStopping, 0.0, kExitTimeoutMs) == Step::End);
    CHECK(ExitStep(kStopping, 5000.0, kExitTimeoutMs) == Step::End);
    // already stopped or going away: no xrEndSession
    for (int s : {kIdle, kExiting, kLossPending}) CHECK(ExitStep(s, 0.0, kExitTimeoutMs) == Step::Skip);
    // the sequence a runtime gives: FOCUSED -> VISIBLE -> SYNCHRONIZED -> STOPPING -> (end) -> IDLE -> EXITING
    const int seq[] = {kFocused, kVisible, kSynchronized, kStopping};
    double t = 0.0;
    Step last = Step::Poll;
    for (int s : seq) { last = ExitStep(s, t, kExitTimeoutMs); t += 11.0; if (last != Step::Poll) break; }
    CHECK(last == Step::End);
    CHECK(kExitTimeoutMs > 0.0 && kCloseWaitMs > 0);
}
}  // namespace

int main() {
    TestPlan();
    TestExitStep();
    std::printf("session_end_test: %d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
