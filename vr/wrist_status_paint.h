#pragma once
// The wrist status panel's picture ([hud] wrist_status, vr/wrist_status.h): a grid of small chips, one for each companion (its icon and, in a corner, how
// many are alive - a dead mercenary shows 0 in red) and each effect (its icon, under a coloured rim: green for what helps, red for what hurts, gold
// for the rest). The icons are the skill icons of the cache made on the player's own machine (vr/wheel_icons.h); a chip with no icon shows the
// effect's initials. Drawn by our own code with the paint kit (vr/paint_kit.h), by the pixel; the text through GDI. Premultiplied pixels, R in each
// one's low byte, as xr.cpp's quad layers take them.

#include <cstdint>
#include <vector>

#include "wrist_status.h"

namespace wriststatuspaint {

// The panel for this look: some milliseconds, not on the headset's frame thread (vr/wrist_status_wire.cpp paints it on a worker).
void Render(const wriststatus::Look& look, std::vector<uint32_t>& px);

// The initials that stand in for a chip's icon: the first letters of the words of the name (up to three), or its first three letters; upper case.
void Initials(const char* name, char out[5]);

}  // namespace wriststatuspaint
