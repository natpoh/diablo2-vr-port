#pragma once
// The wrist status' wiring ([hud] wrist_status): it listens to direct_input.h's per-frame hands and head, runs the glance test (the BACK of a wrist turned
// to the eyes - vr/hand_vitals.h's reveal with the palm's normal reversed, one for each wrist) on them, keeps what the game says the hero has
// (vr/wrist_status_game.h: its companions and the effects on it), paints each wrist's panel on a worker thread (vr/wrist_status_paint.h) and hands xr.cpp what to
// hang: up to TWO quad layers, one over each wrist. F4 only (vr/view_gate.h).
//
// Unticked: nothing exists beyond a listener that returns at once - no read, no thread, no picture, no layer.

#include <cstdint>
#include <vector>

#include "wrist_status.h"

namespace D2RL {
struct PluginContext;
}

namespace wriststatus {

// Once, from the plugin's start: the services, and the frame listener.
void Init(const D2RL::PluginContext* ctx);

// On every read of d2r_vr.ini, live (vrcam's LoadSettings). Switching off lets everything go at the next frame.
void Configure(const Config& config);
bool Enabled();
Config Settings();   // as last read (any thread)

// vrcam's timer: `inGame` = the camera is on and the hero is in a game area. Queues the reads (5 a second) while the box is ticked, in F4 and in a game.
void Tick(bool inGame);

// What xr.cpp hangs for one wrist (side 0 left, 1 right): the panel as a quad in the room's space (the space the frames' poses are in), `colorScale` the
// layer's colour and alpha scale 0 - 1 (the fade; the opacity setting is in the picture).
struct Layer {
    hset::Pose pose;
    float widthM = 0.0f, heightM = 0.0f;
    float colorScale = 0.0f;
    bool visible = false;   // false: the panel is coming (the wrist is turned but not held yet): upload the picture, hang nothing
};

// xr.cpp CloseFrame (the draw thread), every frame and for each wrist: true while that wrist's panel is wanted (coming, up or going), with the revision of the
// picture it must not be shown before. False: nothing to do - no cost beyond this call.
bool Draw(int side, Layer* layer, uint32_t* needRevision);

// A finished picture (premultiplied pixels, R in the low byte, kPicW x kPicH) for that wrist newer than the last fetched, with its revision.
bool Picture(int side, std::vector<uint32_t>* px, uint32_t* revision, uint32_t* width, uint32_t* height);

// With the session (Teardown): the state and the pictures are forgotten (the swapchains go with the session).
void Teardown();
// The plugin's unload: the listener goes, the worker is joined.
void Shutdown();

}  // namespace wriststatus
