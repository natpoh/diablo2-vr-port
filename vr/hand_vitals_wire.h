#pragma once
// The hand vitals panel's wiring ([hud] hand_vitals): it listens to direct_input.h's per-frame hands and head, runs the palm test
// (vr/hand_vitals.h) on them, keeps the hero's life and mana (vr/hand_vitals_game.h), paints the panel on a worker thread
// (vr/hand_vitals_paint.h), and hands xr.cpp what to hang: ONE quad layer over the hand. F4 only (vr/view_gate.h).
//
// Unticked: nothing exists beyond a listener that returns at once - no read, no thread, no picture, no layer.

#include <cstdint>
#include <vector>

#include "hand_vitals.h"

namespace D2RL {
struct PluginContext;
}

namespace handvitals {

// Once, from the plugin's start (next to lowhp::Init): the services, and the frame listener.
void Init(const D2RL::PluginContext* ctx);

// On every read of d2r_vr.ini, live (vrcam's LoadSettings). Switching off lets everything go at the next frame.
void Configure(const Config& config);
bool Enabled();      // the box: either look
bool PanelOn();      // the box with the palm panel's look (a quad layer, vr/xr_hand_vitals.h)
bool OrbsOn();       // the box with a wrist look the effect draws: the glass orbs or the game's own globes (vr/hand_orbs.h)
Config Settings();   // the settings as last read (any thread)

// The hero's life and mana read lately (known, fresh); false otherwise. The read runs while the box is ticked, in F4, in a game (Tick).
bool Snapshot(Vitals* out);

// vrcam's timer: `inGame` = the camera is on and the hero is in a game area. Queues the read of the hero's numbers (5 a second) when
// the panel is switched on, in F4 and in a game; a no-op otherwise.
void Tick(bool inGame);

// What xr.cpp hangs: the panel as a quad in the room's space (the space the frames' poses are in), `colorScale` the layer's colour
// and alpha scale 0 - 1 (the fade and the soft pulse; the opacity setting is in the picture).
struct Layer {
    hset::Pose pose;
    float widthM = 0.0f, heightM = 0.0f;
    float colorScale = 0.0f;
    bool visible = false;   // false: the panel is coming (the palm is turned but not held yet): upload the picture, hang nothing
};

// xr.cpp CloseFrame (the draw thread), every frame: true while the panel is wanted (coming, up or going), with the revision of
// the picture it must not be shown before (an older picture is the last time's). False: nothing to do - no cost beyond this call.
bool Draw(Layer* layer, uint32_t* needRevision);

// A finished picture (premultiplied pixels, R in the low byte, vr/hand_vitals_paint.h's size) newer than the last fetched, with its revision.
bool Picture(std::vector<uint32_t>* px, uint32_t* revision, uint32_t* width, uint32_t* height);

// With the session (Teardown): the state and the picture are forgotten (the swapchain goes with the session).
void Teardown();
// The plugin's unload: the listener goes, the worker is joined.
void Shutdown();

}  // namespace handvitals
