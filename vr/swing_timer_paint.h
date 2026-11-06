#pragma once
// swing_timer_paint.h - [hud] swing_timer: the picture (vr/swing_timer_wire.cpp paints it on its worker thread when the look changes). One size for
// both styles: the ring in its middle with the number inside, or the bar along its bottom with the number rising over it. Premultiplied
// pixels, R in each one's low byte, as xr.cpp's quad layers and D2R_DepthFog.fx's SwingTex take them. Through vr/paint_kit.h (GDI for the text).

#include <cstdint>
#include <vector>

#include "swing_timer.h"

namespace swingtimerpaint {

constexpr int kW = 384, kH = 192;   // (D2R_DepthFog.fx's SwingTex is this size)

void Render(const swingtimer::Look& look, std::vector<uint32_t>& px);

}  // namespace swingtimerpaint
