#pragma once
// The hand vitals panel's picture ([hud] hand_vitals, vr/hand_vitals.h): two round globes in the Diablo style, a red one for life
// and a blue one for mana, each filling from the bottom behind a dark glass with a thin metal rim and a soft highlight, with
// "now / max" under it, on a faint dark plate. Drawn by our own code, by the pixel (distance fields; the numbers through GDI):
// no picture of the game's or of anyone else's. Premultiplied pixels, R in each one's low byte, as xr.cpp's quad layers take them.

#include <cstdint>
#include <vector>

#include "hand_vitals.h"

namespace handvitalspaint {

constexpr int kW = 384, kH = 256;   // the picture; vr/hand_vitals.h kPanelAspect = kH / kW

// The panel for this look. Some milliseconds: not on the headset's frame thread (vr/hand_vitals_wire.cpp paints it on a worker).
void Render(const handvitals::Look& look, std::vector<uint32_t>& px);

}  // namespace handvitalspaint
