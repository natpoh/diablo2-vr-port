#pragma once
// thrust_debug_wire.h - [debug] thrust_debug: the thrust's readout in the headset (vr/thrust_debug.h is its pure part). hit by touch (vr/melee_touch.cpp) hands over the
// lines of the last touch and the last jab; this paints them on a small dark plate on a worker thread (only when they change) and hands xr.cpp ONE quad layer over the
// weapon hand (vr/xr_thrust_debug.h). F4 only (vr/view_gate.h). Unticked: a listener that returns at once - no thread, no picture, no layer.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "hand_vitals.h"   // hset::Pose
#include "thrust_debug.h"
#include "thrust_debug_paint.h"

namespace D2RL {
struct PluginContext;
}

namespace thrustdebugwire {

constexpr int kW = thrustdebugpaint::kW, kH = thrustdebugpaint::kH;   // the picture (vr/thrust_debug_paint.h)
constexpr float kWidthM = 0.20f;             // the panel in the headset: 20 cm wide ...
constexpr float kHeightM = kWidthM * kH / kW;
constexpr float kUpCm = 16.0f;               // ... its middle this far over the weapon hand, turned to the head
constexpr size_t kMaxLines = thrustdebugpaint::kMaxLines;

// Once, from the plugin's start: the frame listener.
void Init(const D2RL::PluginContext* ctx);
// [debug] thrust_debug, on every read of the ini (live).
void SetWanted(bool on);
bool Wanted();
// [debug] thrust_debug_only (live): with thrust_debug, ONLY the thrusts are shown - the swing's colours on the blade, its contact marks, its log lines and the movements that are
// not straight enough to be a thrust (a swing, an overhead blow) are left out of the debug. Only() = both ticks.
void SetOnly(bool on);
bool Only();

// hit by touch (the update thread): the lines to show and the hand they are about (0 right, 1 left - the skeleton's side). n 0: nothing to show.
void SetLines(const thrustdbg::Line* lines, size_t n, int side);

// What xr.cpp hangs (the draw thread).
struct Layer {
    hset::Pose pose;
    float widthM = kWidthM, heightM = kHeightM;
};
// True while the panel is wanted and up; *needRevision: the picture it must not be shown before.
bool Draw(Layer* layer, uint32_t* needRevision);
// A finished picture (premultiplied, R in the low byte, kW x kH) newer than the last fetched.
bool Picture(std::vector<uint32_t>* px, uint32_t* revision);
// With the session: the state and the picture are forgotten.
void Teardown();
// The plugin's unload: the listener goes, the worker is joined.
void Shutdown();

}  // namespace thrustdebugwire
