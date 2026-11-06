// [openxr] end_session_on_exit (vr/exit_end.h): which of the game's messages are a quit to Windows, when a quit ends the session, the bounded wait for the end
// (the draw thread first, then our own thread, then the game goes on), and no new session while the game closes. Run: exit_end_test.exe
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "exit_end.h"

namespace {
int g_fails = 0, g_checks = 0;
#define CHECK(c)                                                                \
    do {                                                                        \
        ++g_checks;                                                             \
        if (!(c)) { ++g_fails; std::printf("  FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

using namespace exitend;

void TestTriggers() {
    CHECK(FromWindow(0x0010, 0) == Trigger::WindowClose);   // WM_CLOSE: the X, Alt+F4
    CHECK(FromWindow(0x0016, 1) == Trigger::WindowsEnd);    // WM_ENDSESSION, the session really ends
    CHECK(FromWindow(0x0016, 0) == Trigger::None);          // ... a log-off that was cancelled
    CHECK(FromWindow(0x0011, 0) == Trigger::None);          // WM_QUERYENDSESSION only asks
    CHECK(FromWindow(0x0002, 0) == Trigger::None);          // WM_DESTROY: too late to be first, not a trigger
    CHECK(FromWindow(0x0112, 0xF060) == Trigger::None);     // WM_SYSCOMMAND SC_CLOSE: it becomes WM_CLOSE, counted there once
    CHECK(FromUi("FrontEndNavigation", "ToExit") == Trigger::TitleExit);
    CHECK(FromUi("FrontEndNavigation", "ToFrontEnd") == Trigger::None);   // leaving a game to the character screen: VR goes on
    CHECK(FromUi("FrontEndNavigation", "ToGame") == Trigger::None);
    CHECK(FromUi("PausePanelMessage", "ExitGame") == Trigger::None);       // "Save and Exit": back to the character screen
    CHECK(FromUi("PanelManager", "UnloadAll") == Trigger::None);
    CHECK(FromUi(nullptr, "ToExit") == Trigger::None);
    CHECK(FromUi("FrontEndNavigation", nullptr) == Trigger::None);
}

void TestDecide() {
    CHECK(Decide(false, true, false) == Begin::Off);        // the key 0: as before
    CHECK(Decide(true, true, false) == Begin::End);
    CHECK(Decide(true, false, false) == Begin::NoSession);  // flat, FlatVR, a failed runtime: nothing to end
    CHECK(Decide(true, true, true) == Begin::Already);      // WM_CLOSE then ToExit, or WM_CLOSE twice: one end
    CHECK(Decide(false, true, true) == Begin::Off);
}

void TestWait() {
    Progress p;
    // the draw thread is given its grace to take the stop between two frames
    CHECK(WaitStep(0.0, p) == Step::Wait);
    CHECK(WaitStep(kDrawGraceMs - 1.0, p) == Step::Wait);
    // it did not take it (no presents any more, or the quit is on its thread): our own thread ends the session
    CHECK(WaitStep(kDrawGraceMs, p) == Step::StopHere);
    CHECK(WaitStep(1000.0, p) == Step::StopHere);
    // taken, the session gone, but the runtime's handles still being destroyed on the end's thread: waited for
    p.stopTaken = true;
    p.sessionGone = true;
    p.endsBusy = 1;
    CHECK(WaitStep(400.0, p) == Step::Wait);
    // all of it done
    p.endsBusy = 0;
    CHECK(WaitStep(400.0, p) == Step::Done);
    CHECK(WaitStep(kBudgetMs + 100.0, p) == Step::Done);   // (done wins over the time: said as done)
    // taken but Stop not through yet (it waits for the frame in flight under the lock)
    Progress q;
    q.stopTaken = true;
    CHECK(WaitStep(kDrawGraceMs + 10.0, q) == Step::Wait);   // never a second stop on top of one under way
    // the budget: the game goes on closing, the exit never hangs on the runtime
    CHECK(WaitStep(kBudgetMs, q) == Step::GiveUp);
    Progress stuck;
    stuck.stopTaken = true;
    stuck.sessionGone = true;
    stuck.endsBusy = 2;
    CHECK(WaitStep(kBudgetMs, stuck) == Step::GiveUp);
    Progress untaken;
    CHECK(WaitStep(kBudgetMs, untaken) == Step::GiveUp);   // (our own Stop never got the lock in time)
    // the order holds: the grace is shorter than the budget, the budget shorter than the DLL's own wait plus the game's teardown
    CHECK(kDrawGraceMs < kBudgetMs);
}

void TestHold() {
    CHECK(!HoldStart(0, 5000));                 // no quit: sessions as ever
    CHECK(HoldStart(1000, 1000));
    CHECK(HoldStart(1000, 1000 + 12000));       // the title screen's Exit took 12 s to the end (239): no session in between
    CHECK(HoldStart(1000, 1000 + kResumeMs - 1));
    CHECK(!HoldStart(1000, 1000 + kResumeMs));  // the game did not close: a session again
    CHECK(!Resume(0, 99999));
    CHECK(!Resume(1000, 1000 + kResumeMs - 1));
    CHECK(Resume(1000, 1000 + kResumeMs));
}

// The process's exit (ntdll!RtlExitUserProcess, 2026-11-06): the quits no message shows - the title screen's confirm box, a game left straight to Windows
void TestProcessExit() {
    CHECK(AtProcessExit(false, true, 0) == AtExit::Off);      // the key 0: left to the DLLs' detach, as before
    CHECK(AtProcessExit(false, true, 1) == AtExit::Off);
    CHECK(AtProcessExit(false, false, 0) == AtExit::Off);
    CHECK(AtProcessExit(true, true, 0) == AtExit::End);       // Serg's two exits of 2026-11-06: a session and its swapchains alive at the exit
    CHECK(AtProcessExit(true, false, 1) == AtExit::End);      // a quit's end still destroying on its own thread: waited for, not cut by the exit
    CHECK(AtProcessExit(true, false, 0) == AtExit::Nothing);  // flat, FlatVR, or a quit already ended it all: nothing to do
    CHECK(AtProcessExit(true, false, -1) == AtExit::Nothing);
    // the wait: an end under way given its time, then the destroy (the 0.203.2 end took 69 ms); never forever
    CHECK(kExitStopMs > kBudgetMs);
    CHECK(kExitStopMs <= 10000.0);
    CHECK(Name(Trigger::ProcessExit) && std::strcmp(Name(Trigger::ProcessExit), "none") != 0);
    // the process's exit is no window message and no UI message
    CHECK(FromWindow(0x0002, 0) != Trigger::ProcessExit);
    CHECK(FromUi("PanelManager", "ClosePanel") == Trigger::None);   // the confirm box's close: also a cancel, not a quit by itself
}

void TestNames() {
    for (Trigger t : {Trigger::WindowClose, Trigger::WindowsEnd, Trigger::TitleExit, Trigger::ProcessExit}) CHECK(Name(t) && Name(t)[0] && std::strcmp(Name(t), "none") != 0);
    CHECK(std::strcmp(Name(Trigger::None), "none") == 0);
}

}  // namespace

int main() {
    TestTriggers();
    TestDecide();
    TestWait();
    TestHold();
    TestProcessExit();
    TestNames();
    std::printf("%d of %d checks passed\n", g_checks - g_fails, g_checks);
    return g_fails ? 1 : 0;
}
