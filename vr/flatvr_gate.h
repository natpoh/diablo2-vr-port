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

}  // namespace flatgate
