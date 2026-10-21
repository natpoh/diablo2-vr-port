// thrust_debug_paint.cpp - [debug] thrust_debug: the readout's picture (vr/thrust_debug_paint.h). Our own drawing.

#include "paint_kit.h"   // (windows.h, the canvas and GDI text)
#include "thrust_debug_paint.h"

namespace thrustdebugpaint {

void Render(const thrustdbg::Line* lines, size_t n, std::vector<uint32_t>& px) {
    using namespace wheelpaint::kit;
    px.assign(static_cast<size_t>(kW) * kH, 0u);
    Canvas cv{px, kW, kH};
    static Fonts fonts;   // (the painter's thread alone)
    RoundRect(cv, {2.0f, 2.0f, kW - 2.0f, kH - 2.0f}, 16.0f, {12, 12, 16}, 0.82f, {120, 120, 130}, 0.9f, 2.0f);
    const float lineH = (kH - 16.0f) / static_cast<float>(kMaxLines);
    for (size_t i = 0; lines && i < n && i < kMaxLines; ++i) {
        const thrustdbg::Line& l = lines[i];
        // the thrust's own tint, as the blade's (vr/melee_boxes.h kCThrustOk / kCThrustNo), lightened for text on the dark plate
        const Col c = l.colour == thrustdbg::kPass ? Col{90, 165, 255} : l.colour == thrustdbg::kFail ? Col{255, 140, 30} : l.colour == thrustdbg::kGrey ? Col{150, 150, 150} : Col{235, 235, 235};
        const bool head = l.text[0] != ' ';   // (a head line starts a block; its numbers are indented)
        const float y = 8.0f + lineH * static_cast<float>(i);
        Text(cv, fonts, l.text, {14.0f, y, kW - 10.0f, y + lineH}, head ? 22 : 20, head, c, 1.0f, true);
    }
}

}  // namespace thrustdebugpaint
