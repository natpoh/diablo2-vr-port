#pragma once
// The skill panel's TILES look: the whole window painted into a 2048 x 640 image from a View (vr/wheel_model.h, style Tiles).
// A window in the style of the game's vendor windows - dark stone with a bevelled metal frame, a thin gold line and gold corner
// studs, square inset tiles like an item grid - drawn entirely by our own code, by the pixel (distance fields, a hash for the
// stone's grain; text through GDI, the skills' icons from the local cache when there is one). No picture of anyone else's and
// none of the game's. Premultiplied pixels, R in each one's low byte, as xr.cpp's quad layers take them.
// vr/wheel_paint.cpp calls this for a view whose style is Tiles (wheelpaint::Render); the worker thread is the wheel's.

#include <cstdint>
#include <vector>

#include "wheel_model.h"

namespace tilespaint {

// The picture's size: vr/tiles_geom.h's kW x kH.
void Size(int* width, int* height);

// The window for this view. Some milliseconds: not on the headset's frame thread (wheelpaint::Async paints it).
void Render(const wheel::View& v, std::vector<uint32_t>& px);

}  // namespace tilespaint
