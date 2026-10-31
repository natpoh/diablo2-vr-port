// bw_autorun.h - what D2R VR Settings starts or stops of BodyWalk and FlatVR when the 3D in the headset is switched between FlatVR and native
// OpenXR, and when the game is started (START, the launcher's `--start`). Pure: the facts in, the plan out; the unit test is
// cleanroom/tests/bw_autorun_test.cpp, the doing tools/settings/settings.cpp (AutoRun).
//
// Serg, 2026-10-29: FlatVR mode starts BodyWalk and FlatVR by itself; switching to native closes FlatVR, which native does not need and which
// holds the headset - BodyWalk stays running (his correction the same day: only FlatVR). Each behaviour has its own key ([mode]
// flatvr_autostart, [mode] native_close_flatvr); off = as before:
//   - the game's start: BodyWalk started in VR when none runs (not with native OpenXR and "Controls without BodyWalk"), and for FlatVR its
//     Start asked once BodyWalk's Game Link is up (2026-10-10) - that stays whatever the keys say;
//   - the switch in Settings: nothing started or stopped (only BodyWalk's Mapping source and FlatVR's 3D source follow, through Game Link).
// BodyWalk itself is never closed. Flat mode ([mode] platform=0) is never touched.
#pragma once

namespace bwauto {

enum class When {
    ModeSwitch,  // the 3D in the headset switched in Settings from native OpenXR to a FlatVR choice, or back
    GameStart,   // START, or `--start` (the launcher's Play)
};

struct Facts {
    When when = When::GameStart;
    bool vr = true;                 // [mode] platform=1 (the headset)
    bool native = false;            // [openxr] on: the game holds the headset itself
    bool directInput = false;       // [openxr] direct_input: native's controls without BodyWalk
    bool startOn = true;            // [mode] flatvr_autostart
    bool closeOn = true;            // [mode] native_close_flatvr
    bool bodyWalkFound = false;     // a BodyWalkVR.exe found on this PC (one too old is refused by StartBodyWalk itself, which says so)
    int bodyWalkRunning = 0;        // BodyWalkVR.exe processes running now (a second copy counts too)
    bool linkUp = false;            // the running BodyWalk's Game Link Host block moves
    bool linkStopsFlatVr = false;   // ... and it takes FlatVR's Start / Stop (BW_LINK_CAP_FLATVR_RUNNING)
    bool flatVrRunning = false;     // FlatVR's head sample moves (FlatVR started, in BodyWalk or on its own)
    int flatVrAppRunning = 0;       // FlatVR.exe processes (FlatVR on its own, not inside BodyWalk)
};

// Why the plan is what it is - one line in the log.
enum class Why {
    Flat,              // flat mode: nothing
    Nothing,           // nothing to do (all as it should be, or the key is off)
    FlatVrStart,       // FlatVR: BodyWalk and / or FlatVR started
    FlatVrNoBodyWalk,  // FlatVR: no BodyWalk found to start
    NativeStartBodyWalk,  // native through BodyWalk's Mapping at the game's start: BodyWalk started (as before)
    NativeStopFlatVr,  // native: FlatVR in BodyWalk stopped through Game Link, BodyWalk stays
    NativeCloseFlatVr, // native: FlatVR.exe of its own closed, BodyWalk stays
    NativeCannotStop,  // native: FlatVR in BodyWalk holds the headset and this BodyWalk cannot stop it (no Game Link: Lite, an old copy) - left
};

struct Plan {
    bool startBodyWalk = false;   // start BodyWalkVR.exe (the newest found) as its own shortcut does
    bool startFlatVr = false;     // ask BodyWalk's Game Link to start FlatVR once it is up (given up after a minute)
    bool stopFlatVr = false;      // ask BodyWalk's Game Link to stop FlatVR (BodyWalk stays)
    bool closeFlatVrApp = false;  // close every FlatVR.exe gently (its own close; never killed while it is on its way out)
    Why why = Why::Nothing;
};

inline Plan Decide(const Facts& f) {
    Plan p;
    if (!f.vr) { p.why = Why::Flat; return p; }
    if (!f.native) {
        // FlatVR: the game's start always as before; the switch only with the key
        if (f.when == When::ModeSwitch && !f.startOn) return p;
        p.startBodyWalk = f.bodyWalkRunning == 0 && f.bodyWalkFound;
        p.startFlatVr = !f.flatVrRunning;   // (with no BodyWalk found too: one started by hand within the minute gets it, as before)
        if (f.bodyWalkRunning == 0 && !f.bodyWalkFound) p.why = Why::FlatVrNoBodyWalk;
        else if (p.startBodyWalk || p.startFlatVr) p.why = Why::FlatVrStart;
        return p;
    }
    // Native OpenXR. BodyWalk is the controls (its Mapping on the game's headset) unless "Controls without BodyWalk" is on: started at the
    // game's start then, as before. BodyWalk is never closed; only FlatVR must let the headset go.
    if (f.when == When::GameStart && !f.directInput && f.bodyWalkRunning == 0 && f.bodyWalkFound) {
        p.startBodyWalk = true;
        p.why = Why::NativeStartBodyWalk;
    }
    if (!f.closeOn) return p;
    if (f.flatVrAppRunning > 0) {   // FlatVR on its own: closed (BodyWalk's own FlatVR, if any, is stopped below too)
        p.closeFlatVrApp = true;
        p.why = Why::NativeCloseFlatVr;
    }
    if (f.flatVrRunning && f.bodyWalkRunning > 0 && f.flatVrAppRunning == 0) {   // FlatVR inside BodyWalk
        if (f.linkStopsFlatVr) {
            p.stopFlatVr = true;
            p.why = Why::NativeStopFlatVr;
        } else {
            p.why = Why::NativeCannotStop;   // the busy_guard note says it: close BodyWalk and restart SteamVR
        }
    }
    return p;
}

// Closing one process gently, a step at a time: its own close first (WM_CLOSE to its window, as its title bar's X; the program then ends its
// loop and writes its settings), then it is waited for. Killed only as the last resort: its window still there and not answering (the
// system's "not responding") after kCloseWaitMs. One whose window is gone is on its way out - writing its settings - and is never killed:
// waited for up to kExitWaitMs more, then left. One whose window is there and answers kept it open (a question of its own): left too.
enum class CloseStep { Done, Wait, Kill, GiveUp };
constexpr unsigned kCloseWaitMs = 8000;
constexpr unsigned kExitWaitMs = 30000;

inline CloseStep NextCloseStep(bool exited, bool windowThere, bool hung, unsigned waitedMs) {
    if (exited) return CloseStep::Done;
    if (!windowThere) return waitedMs < kCloseWaitMs + kExitWaitMs ? CloseStep::Wait : CloseStep::GiveUp;
    if (waitedMs < kCloseWaitMs) return CloseStep::Wait;
    return hung ? CloseStep::Kill : CloseStep::GiveUp;
}

inline const wchar_t* Text(Why w) {
    switch (w) {
    case Why::Flat: return L"flat mode - BodyWalk and FlatVR left as they are";
    case Why::FlatVrStart: return L"FlatVR - BodyWalk and FlatVR started where they were not running";
    case Why::FlatVrNoBodyWalk: return L"FlatVR - no BodyWalk found to start";
    case Why::NativeStartBodyWalk: return L"native OpenXR through BodyWalk's Mapping - BodyWalk started";
    case Why::NativeStopFlatVr: return L"native OpenXR - FlatVR stopped through Game Link, BodyWalk stays";
    case Why::NativeCloseFlatVr: return L"native OpenXR - FlatVR.exe closed (it would hold the headset), BodyWalk stays";
    case Why::NativeCannotStop: return L"native OpenXR - FlatVR holds the headset and this BodyWalk has no Game Link to stop it (BodyWalk Lite or an old copy): "
                                       L"left as it is - stop FlatVR in BodyWalk, or close BodyWalk and restart SteamVR";
    case Why::Nothing:
    default: return L"nothing to start or stop";
    }
}

}  // namespace bwauto
