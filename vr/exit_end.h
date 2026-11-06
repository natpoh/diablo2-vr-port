// exit_end.h - [openxr] end_session_on_exit: the headset session ended before the game closes (vr/xr.cpp EndOnExit, the triggers in vr/hud/window.cpp and
// vr/gamestate.cpp). Pure, tested in cleanroom/tests/exit_end_test.cpp.
//
// The 2026-11-05 crash batch (group A, crash_batch_2026-11-05.md; rows 227, 228, 234-243, 245, 246): the game dies with 0xC0000409 0.3-1 s after the world's teardown
// when the player quits to Windows - 23 of 25 sessions on the Oculus runtime, 2 of 2 on Virtual Desktop, none on SteamVR, with nothing of ours placed too (239). Our
// DLL_PROCESS_DETACH (crash_watch "quiet") is never reached. The lead (not proven: a fast fail leaves no dump): the OpenXR session and its swapchains are still alive
// while the game tears down its D3D12 device and leaves - the Windows events of one of those PCs name RuntimeIPCServiceClient_64.dll (the Oculus runtime's client)
// for an 0xc0000409 (offset 0x1AAE2D). So with the key on, the game's own quit (its window closed - the X, Alt+F4 - Windows ending the session, or Exit on the
// title screen) first ends the session the way a live switch-off does (vr/session_end.h: the exit asked for, STOPPING, xrEndSession, the swapchains, the session
// and the instance destroyed on a thread of their own), and the game waits for that a bounded time - the exit never hangs on it.
//
// Not a trigger: the pause menu's "Save and Exit" (PausePanelMessage ExitGame) - it goes back to the character screen, where VR goes on.
//
// 2026-11-06 (row 274 and Serg's own exits, Virtual Desktop, the key on): the crash is Virtual Desktop's own DLL_PROCESS_DETACH. On the game's main thread, at
// the process's exit, virtualdesktop-openxr.dll's entry point (its address in R12 and on the stack) destroys the swapchains still alive, and
// VirtualDesktop.LibOVRRT64_1.dll (+0xE900, a swapchain image's destructor: three images, each releasing its COM pointers) releases a D3D11 texture whose memory is
// already freed (RAX 0xDEDE..., d3d11.dll+0x4E760) - d3d11.dll, loaded by the runtime after it, is detached before it. Our own detach never comes (it would be after
// the runtime's). The triggers above missed both quits: the title screen's Exit in controller mode goes through a confirm box (DismissableModal, no ToExit), and
// the other one left a game straight to Windows. So the process's own exit is a trigger too: ntdll!RtlExitUserProcess, which every quit to Windows passes before
// Windows unloads any DLL (other threads still running, the loader lock free) - the session, its swapchains and the instance destroyed there, on a thread of ours,
// waited for a bounded time. And Serg's 0.203.2 exit, ended at ToExit: the instance's end let the runtime go and d3d11.dll with it, and 0.8 s later the game's own
// DXGI teardown called a module that still read d3d11.dll's data (unmapped). So when a quit ends the session d3d11.dll is kept loaded to the process's end.
#pragma once

#include <cstdint>
#include <cstring>

namespace exitend {

enum class Trigger { None, WindowClose, WindowsEnd, TitleExit, ProcessExit };
inline const char* Name(Trigger t) {
    switch (t) {
        case Trigger::WindowClose: return "the game's window closed";
        case Trigger::WindowsEnd: return "Windows ends the session";
        case Trigger::TitleExit: return "Exit on the title screen";
        case Trigger::ProcessExit: return "the game's process exits";
        default: return "none";
    }
}

// The game's window procedure: WM_CLOSE (the X, Alt+F4 - SC_CLOSE becomes WM_CLOSE) and WM_ENDSESSION with wParam TRUE (log off, shut down). (The values are
// winuser.h's, so this header needs no windows.h.)
constexpr unsigned kWmClose = 0x0010, kWmEndSession = 0x0016;
inline Trigger FromWindow(unsigned msg, uintptr_t wParam) {
    if (msg == kWmClose) return Trigger::WindowClose;
    if (msg == kWmEndSession && wParam != 0) return Trigger::WindowsEnd;
    return Trigger::None;
}
// The game's UI messages (D2RLoader's UiMessage event): only the title screen's Exit quits the game.
inline Trigger FromUi(const char* target, const char* command) {
    if (!target || !command) return Trigger::None;
    return std::strcmp(target, "FrontEndNavigation") == 0 && std::strcmp(command, "ToExit") == 0 ? Trigger::TitleExit : Trigger::None;
}

// Whether a quit ends the session: the key, a session there (starting or made), and not already asked for this quit.
enum class Begin { End, Off, NoSession, Already };
inline Begin Decide(bool enabled, bool session, bool asked) {
    if (!enabled) return Begin::Off;
    if (asked) return Begin::Already;
    if (!session) return Begin::NoSession;
    return Begin::End;
}

// The end, waited for on our own thread: the draw thread is asked first (it ends a live session between two frames - none in flight - as a switch-off does);
// if it has not taken the stop in kDrawGraceMs (it presents no more, or the quit came on its own thread) our thread ends it itself. Done when the session is
// gone and no end of ours is still destroying the runtime's handles; past kBudgetMs the game goes on closing as before.
constexpr double kDrawGraceMs = 250.0;
constexpr double kBudgetMs = 3000.0;
struct Progress {
    bool stopTaken = false;    // the stop asked for was taken (by the draw thread or by us)
    bool sessionGone = false;  // no session any more (Stop is through)
    int endsBusy = 0;          // live ends still destroying on their own threads (xr.cpp g_goneBusy)
};
enum class Step { Wait, StopHere, Done, GiveUp };
inline Step WaitStep(double elapsedMs, const Progress& p) {
    if (p.stopTaken && p.sessionGone && p.endsBusy <= 0) return Step::Done;
    if (elapsedMs >= kBudgetMs) return Step::GiveUp;
    if (!p.stopTaken && elapsedMs >= kDrawGraceMs) return Step::StopHere;
    return Step::Wait;
}

// The process's exit (ntdll!RtlExitUserProcess): the last moment before Windows detaches the DLLs - the runtime's first, with d3d11.dll gone before it. With the key
// on, anything of the runtime's still there is destroyed now: a session or an instance (alive), or an earlier quit's end still destroying on its own thread
// (endsBusy) - waited for. Nothing there (flat, FlatVR, a quit already ended it all): nothing to do. Once per process.
enum class AtExit { Off, Nothing, End };
inline AtExit AtProcessExit(bool enabled, bool alive, int endsBusy) {
    if (!enabled) return AtExit::Off;
    if (!alive && endsBusy <= 0) return AtExit::Nothing;
    return AtExit::End;
}
// How long the exit waits for that end (our thread: an end still under way given its 2 s - vr/session_end.h kCloseWaitMs -, then the destroy); past it the process
// exits as before. A hung runtime never holds the game open longer than this.
constexpr double kExitStopMs = 5000.0;

// After a quit ended the session: no new one while the game closes (its presents go on for a while: the title screen's Exit took 12 s to the end in 239). Still
// running kResumeMs later, the quit was not carried out (a dialog cancelled, a message the game ignored): a session again. endedAt 0: no quit.
constexpr uint64_t kResumeMs = 30000;
inline bool HoldStart(uint64_t endedAtMs, uint64_t nowMs) { return endedAtMs != 0 && nowMs - endedAtMs < kResumeMs; }
inline bool Resume(uint64_t endedAtMs, uint64_t nowMs) { return endedAtMs != 0 && nowMs - endedAtMs >= kResumeMs; }

}  // namespace exitend
