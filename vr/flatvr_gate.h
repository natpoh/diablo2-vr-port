// flatvr_gate.h - may the native OpenXR session be started over a running FlatVR? Pure (tools/xr_busy_test.cpp).
//
// FlatVR holds the headset in the runtime; a second session over it gets no frames (xrWaitFrame never returns). At a session's start FlatVR is stopped through BodyWalk's
// Game Link (BodyWalk 1.78+); BodyWalk Lite has no Game Link, so it cannot be stopped from here. 2026-10-18: a player went from FlatVR 3D depth (Lite) back to Native, and
// the game hung. A FlatVR that runs but whose head sample stands still is not tracking a headset (a FlatVR left open, with none): the session may start.
#pragma once

namespace flatgate {

enum class Act {
    Start,          // FlatVR is not there, or does not matter: the session starts
    StopThenStart,  // FlatVR runs and Game Link can stop it
    Refuse,         // FlatVR runs, holds the headset and cannot be stopped: no session; said on Settings > Status; tried again when FlatVR is gone
};

// running: FlatVR's head sample is there; canStop: BodyWalk's Game Link can stop it; moving: its head sample counter changed while we looked; guard: [openxr] busy_guard
inline Act Decide(bool running, bool canStop, bool moving, bool guard) {
    if (!running) return Act::Start;
    if (canStop) return Act::StopThenStart;
    return guard && moving ? Act::Refuse : Act::Start;
}

// [openxr] stop_flatvr_watch: FlatVR is stopped once, at the session's start - but only one that runs THEN. One that starts after (BodyWalk
// opened after the game, its start held until SteamVR came up, its START button, a FlatVR not yet moving at that moment) ran beside the
// native session for the rest of the game: both in the headset, the frames shared (Serg, 2026-10-31: "why does FlatVR not stop when I switch
// to OpenXR" - 45 fps, the graphics card 60 % busy). Looked at about once a second while the session runs: when its head sample moved between
// two looks it is asked to stop through Game Link again (at most every kAgainMs while it goes on moving); one that Game Link cannot stop
// (BodyWalk Lite) is said once each time it starts moving.
struct Watch {
    enum class Do { Nothing, Stop, CannotStop };
    static constexpr unsigned long long kAgainMs = 10000;
    bool seen = false;               // a first look taken since the session (or FlatVR's block) came: where its counter stands
    unsigned counter = 0;
    unsigned long long askedAt = 0;  // when a stop was last asked (ms), 0 never
    bool told = false;               // the cannot-stop line said for this run of moves

    // sessionRunning: a native session runs (and is not being switched off); present: FlatVR's head-sample block is there; counter: its counter;
    // canStop: BodyWalk's Game Link is there and takes FlatVR's Stop
    Do Look(bool sessionRunning, bool present, unsigned now_counter, bool canStop, unsigned long long nowMs) {
        if (!sessionRunning || !present) { seen = false; told = false; return Do::Nothing; }
        if (!seen) { seen = true; counter = now_counter; return Do::Nothing; }   // (the first look is where it stands, not a move)
        const bool moved = now_counter != counter;
        counter = now_counter;
        if (!moved) { told = false; return Do::Nothing; }
        if (!canStop) {
            if (told) return Do::Nothing;
            told = true;
            return Do::CannotStop;
        }
        if (askedAt && nowMs - askedAt < kAgainMs) return Do::Nothing;
        askedAt = nowMs;
        return Do::Stop;
    }
};

}  // namespace flatgate
