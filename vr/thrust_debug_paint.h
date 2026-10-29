#pragma once
// thrust_debug_paint.h - [debug] thrust_debug: the readout's picture (vr/thrust_debug_wire.cpp paints it on its worker thread). A dark rounded plate and the lines of
// vr/thrust_debug.h in their colours - the thrust's own: blue passes, orange fails, grey nothing yet - through GDI (vr/paint_kit.h). Premultiplied pixels, R in each one's low byte, as
// xr.cpp's quad layers take them. Some milliseconds: never on the headset's frame thread.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "thrust_debug.h"

namespace thrustdebugpaint {

constexpr int kW = 512, kH = 336;   // the picture
constexpr size_t kMaxLines = 12;    // its lines: the touch's six and the jab's six (head, straight, turn, speed, along, accel)

void Render(const thrustdbg::Line* lines, size_t n, std::vector<uint32_t>& px);

}  // namespace thrustdebugpaint
