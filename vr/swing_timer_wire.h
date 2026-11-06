#pragma once
// swing_timer_wire.h - [hud] swing_timer on the game (vr/swing_timer.h is its logic, vr/swing_timer_paint.cpp its picture). It gathers what the
// timer needs - our server's attack starts (vr/move_attack_wire.cpp), our hero's client mode (vrcam's update loop), the touches (vr/melee_touch.cpp),
// the outcomes ([debug] hit_outcome's observers, vr/hit_outcome.h's LabelRing) - paints the picture on a worker thread when it changes, and hands
// it to the headset (vr/xr_swing_timer.h: one quad before the eyes, VR first person) and to the monitor (vr/render/effects/effects.cpp:
// D2R_DepthFog.fx's SwingTex, the monitor's F2 / F3). Unticked: listeners that return at once - no thread, no picture, no layer.

#include <cstdint>
#include <vector>

#include "hset_ray.h"   // hset::Pose
#include "swing_timer.h"
#include "swing_timer_paint.h"

namespace D2RL {
struct PluginContext;
}

namespace swingtimer {

constexpr int kPicW = swingtimerpaint::kW, kPicH = swingtimerpaint::kH;
// In the headset: the picture's quad this far ahead of the eyes, held to the head. The ring ~4 degrees across in its middle; the bar ~10
// degrees wide, ~13 degrees over the middle of the view.
constexpr float kDistM = 1.2f;
constexpr float kRingQuadM = 0.19f;   // the quad's width (the ring is 172 of its 384 pixels)
constexpr float kBarQuadM = 0.24f;
constexpr float kBarUpM = 0.28f;      // (1.2 m x tan 13 degrees)
// On the monitor: the ring's quad this share of the screen's height high, in the middle; the bar's this share of its width, at the top.
constexpr float kMonRingH = 0.12f, kMonBarW = 0.32f, kMonBarTop = 0.03f;

// Once, from the plugin's start: the headset's frame listener, the log.
void Init(const D2RL::PluginContext* ctx);
// [hud] swing_timer, swing_timer_style (0 ring, 1 bar), swing_timer_numbers - on every read of the ini (live).
void SetWanted(bool on, int style, bool numbers);
bool Wanted();
// [gameplay] swing_pace_joined (live): the joined client's pace for the touches (PaceTouch). The timer's clock runs for it with the picture off too.
void SetPace(bool on);
// Either: the clock is fed (the client mode, the server's starts).
bool Active();
// vr/melee_touch.cpp, a touch it is about to ask the game to attack for, with no server of ours here: send it, hold it, or drop it (State::PaceAt).
// Send whenever the tick is off.
Pace PaceTouch(uint64_t nowMs);

// Our server's thread (the scheduler's hook): our hero's attack started now, `frames` to its end, the next one in at `greenFrame`. Lock-free.
void NoteServerStart(uint64_t nowMs, int32_t frames, int32_t greenFrame);
// vrcam's update loop (~every 10 ms): our hero's client mode (-1 / known false: not read).
void NoteClientMode(bool known, uint32_t mode, uint64_t nowMs);
// vr/melee_touch.cpp: a touch it asks the game to attack for (VR).
void NoteTouch(uint64_t nowMs);

// The headset (the draw thread): true while the quad is up; *needRevision the picture it must not be shown before.
struct Layer {
    hset::Pose pose;   // in the room's (stage) space
    float widthM = kRingQuadM, heightM = kRingQuadM * kPicH / kPicW;
};
bool Draw(Layer* layer, uint32_t* needRevision);
// The monitor (the render thread), every frame in its F2 / F3: steps the timer; true while something is shown, *box where (uv l, t, r, b, for a
// screen `aspect` wide per high), *needRevision as above.
bool Monitor(uint64_t nowMs, float aspect, float box[4], uint32_t* needRevision);
// A finished picture (premultiplied, R in the low byte, kPicW x kPicH) newer than `after`: copied out (both the headset and the monitor take it).
bool Picture(std::vector<uint32_t>* px, uint32_t* revision, uint32_t after);
// With the headset's session: its quad forgotten.
void Teardown();
// The plugin's unload: the listener goes, the worker is joined.
void Shutdown();

}  // namespace swingtimer
