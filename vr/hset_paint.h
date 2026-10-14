#pragma once
// The in-headset settings panel painted with GDI into a kW x kH picture (vr/hset_model.h's layout):
// the hit test works on the same rectangles. Premultiplied RGBA, R in each pixel's low byte, alpha 255.
// Not the game's art and nothing of BodyWalk's: plain dark panels, own colours.

#include <cstdint>
#include <vector>

#include "hset_model.h"
#include "hset_scene.h"   // what is drawn (also StatusText); this file renders it with GDI

namespace hset {

// `status` (optional) replaces the line under the rows; `hover` is the button the laser is on.
bool Paint(const Model& m, const Hit& hover, std::vector<uint32_t>& rgba, const char* status = nullptr);

}  // namespace hset
