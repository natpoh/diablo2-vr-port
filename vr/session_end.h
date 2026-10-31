// session_end.h - how native OpenXR's session ends while the game goes on (vr/xr.cpp Teardown, GoneThread): pure, tested in cleanroom/tests/session_end_test.cpp.
//
// 2026-10-31 17:49, PC2 (Quest on Virtual Desktop, eyes by turns), 0.192.29: switched to FlatVR in the game, [openxr] on=0 live - the game froze at once, 0 CPU, the log
// ending at "xrEndSession failed: XR_ERROR_SESSION_NOT_STOPPING" / "session ended (switched off)". The switch-off ran inside the game's present: xrEndSession on a
// session still running (no xrRequestExitSession first: the spec's NOT_STOPPING), then every swapchain, the session and the instance destroyed there. Virtual
// Desktop's xrDestroySwapchain flushes the game's queue - a Signal, then a wait with no limit (vr/chain_reaper.h, the 2026-10-22 hang) - and Teardown had waited
// only 500 ms for that queue: with the game's present thread sitting in the destroy, the queue's work that waits on that thread never came, and both waited
// for good. (No stack: the watchdog wrote none; the log is asynchronous, so what came after "session ended" never reached the file.)
//
// So a live end (not the game's closing) hands the runtime's handles to a thread of its own: it waits for the game's queue past our last work with no limit (it
// is not the game's thread), asks the runtime for the exit (xrRequestExitSession) and polls its events until the session is STOPPING - then xrEndSession, as
// the spec has it - with a time limit, then destroys the swapchains, the session and the instance. The game's present returns at once. A new session waits for
// that thread to be done (one instance at a time); the game's closing waits for it a short, bounded time.
#pragma once

namespace sessionend {

// XrSessionState's values (openxr.h), so this header needs no OpenXR.
enum State : int { kUnknown = 0, kIdle = 1, kReady = 2, kSynchronized = 3, kVisible = 4, kFocused = 5, kStopping = 6, kLossPending = 7, kExiting = 8 };

// What an end does and where. final: the game is closing (the DLL goes: done here, as before); removed: the D3D12 device is gone (nothing waits on its queue);
// stuck: one of our runtime calls (xrWaitFrame, xrBeginSession) has not returned; requestExit: the session was running and we end it (on=0, a new session for
// frame generation) - not when the runtime ended it or failed.
struct Plan {
    bool offThread = false;     // the runtime's handles destroyed on a thread of their own, not in the game's present
    bool waitQueue = false;     // ... after the game's queue is past our last work (no limit there)
    bool requestExit = false;   // ... and the exit asked for and waited for (xrRequestExitSession, events to STOPPING, xrEndSession)
};
inline Plan PlanFor(bool final, bool removed, bool stuck, bool requestExit) {
    Plan p;
    p.offThread = !final;
    p.waitQueue = p.offThread && !removed;
    p.requestExit = p.offThread && !removed && !stuck && requestExit;
    return p;
}

// After xrRequestExitSession, polling the runtime's events: what to do with the state it said last, `elapsedMs` after the request.
enum class Step { Poll, End, Skip };   // keep polling; xrEndSession now; no xrEndSession (not running any more, or the time is up): destroy as it is
inline Step ExitStep(int state, double elapsedMs, double timeoutMs) {
    if (state == kStopping) return Step::End;
    if (state == kIdle || state == kExiting || state == kLossPending) return Step::Skip;   // (already stopped, or going away: xrEndSession would fail)
    return elapsedMs >= timeoutMs ? Step::Skip : Step::Poll;
}
constexpr double kExitTimeoutMs = 1500.0;   // how long the runtime is given to say STOPPING
constexpr unsigned kCloseWaitMs = 2000;     // the game's closing: how long it waits for an end still under way (the DLL must not go under that thread)

}  // namespace sessionend
