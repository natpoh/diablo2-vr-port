#pragma once
// The allies' portraits' wiring ([hud] allies_place, vr/ally_portraits.h, shared/allies_place.h): the settings, the reads queued
// (vr/ally_portraits_game.h), the picture painted on a worker thread (vr/ally_portraits_paint.h) only when a tile changed, the right wrist's glance, and
// what xr.cpp hangs: ONE quad layer - under the map held in the hand (or under the big map), or on the right wrist - vr/xr_ally_portraits.h. While the
// allies are shown in one of those places the game's own row at the top is hidden (each place shows them there only). F4 only (vr/view_gate.h).
//
// The game's row (place 0, the default): nothing exists beyond a listener that returns at once - no read, no thread, no picture, no layer.

#include <cstdint>
#include <vector>

#include "ally_portraits.h"
#include "hset_ray.h"   // hset::Pose

namespace D2RL {
struct PluginContext;
}

namespace allyportraits {

// Once, from the plugin's start: the services, and the frame listener (the wrist's glance).
void Init(const D2RL::PluginContext* ctx);

// On every read of d2r_vr.ini, live (vrcam's LoadSettings).
void Configure(const Config& config);
Config Settings();      // as last read (any thread)
bool Enabled();         // the allies are not the game's row: under the map or on the wrist (the reads run, the game's row is hidden)
bool GridWanted();      // ... under the map
bool WristWanted();     // ... on the right wrist
bool UnderMapWanted();  // something to hang under the map: the allies there, or [hud] skill_under_map (the armed skills' row)

// The two pictures: under the map (the armed skills' row, then the allies when they are there) and on the right wrist (the allies).
constexpr int kUnderMap = 0, kOnWrist = 1;

// vrcam's timer: `inGame` = the camera is on and the hero is in a game area. Queues the reads (5 a second) while wanted, in F4 and in a game.
void Tick(bool inGame);

// xr.cpp (the draw thread), each frame the place is there (a map hung, the wrist turned): true when there are portraits to hang now (a fresh read with a
// tile or more), with the revision of the picture it must not be shown before. False: nothing to do.
bool Want(int where, uint32_t* needRevision);

// On the wrist: where the wrist status panel would stand over the right wrist now (its centre and turn, in the room's space), its height, whether that
// panel is up there too, the tiles' width, and the fade (grow: the size factor, alpha 0 - 1). False: the wrist is not turned to the eyes.
struct WristLayer {
    hset::Pose pose;
    float panelHM = 0.0f, tileWidthM = 0.0f, grow = 1.0f, alpha = 0.0f;
    bool panelUp = false;
};
bool WristPanel(WristLayer* layer);

// While the allies are shown elsewhere the game's own row (top left) is hidden: true then (in F4), with the width of the game's corner to hide (layout
// units, vr/ally_portraits.h HideUnits: what the game draws now by the reads, its whole row without one; false when it draws none). The effect
// (vr/render/effects/effects.cpp) moves that corner off the picture, leaving its place empty. Any thread.
bool GameCornerToHide(float* units);

// A finished picture (premultiplied pixels, R in the low byte, width x height: PicW(cols) x PicH(n, cols)) newer than the last fetched, with its revision.
bool Picture(int where, std::vector<uint32_t>* px, uint32_t* revision, uint32_t* width, uint32_t* height);

// With the session (Teardown): the picture is forgotten (the swapchain goes with the session).
void Teardown();
// The plugin's unload: the listener goes, the worker is joined.
void Shutdown();

}  // namespace allyportraits
