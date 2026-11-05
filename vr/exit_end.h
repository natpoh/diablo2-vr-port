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
#pragma once

#include <cstdint>
#include <cstring>

namespace exitend {

enum class Trigger { None, WindowClose, WindowsEnd, TitleExit };
inline const char* Name(Trigger t) {
    switch (t) {
        case Trigger::WindowClose: return "the game's window closed";
        case Trigger::WindowsEnd: return "Windows ends the session";
        case Trigger::TitleExit: return "Exit on the title screen";
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

// After a quit ended the session: no new one while the game closes (its presents go on for a while: the title screen's Exit took 12 s to the end in 239). Still
// running kResumeMs later, the quit was not carried out (a dialog cancelled, a message the game ignored): a session again. endedAt 0: no quit.
constexpr uint64_t kResumeMs = 30000;
inline bool HoldStart(uint64_t endedAtMs, uint64_t nowMs) { return endedAtMs != 0 && nowMs - endedAtMs < kResumeMs; }
inline bool Resume(uint64_t endedAtMs, uint64_t nowMs) { return endedAtMs != 0 && nowMs - endedAtMs >= kResumeMs; }

}  // namespace exitend
