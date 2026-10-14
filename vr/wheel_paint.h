#pragma once
// The skill wheel's picture: the whole panel painted into a 1024 x 1024 image, from a View (vr/wheel_model.h). Our own
// drawing - a dark ring cut into eight wedges with a gold rim, drawn by the pixel; text through GDI into a coverage mask; the
// skills' icons from the local cache when there is one (vr/wheel_icons.h). No picture of anyone else's, none of the game's
// in the plugin. Premultiplied pixels, R in each one's low byte, as xr.cpp's quad layers take them.

#include <cstdint>
#include <vector>

#include "wheel_model.h"

namespace wheelpaint {

constexpr int kSize = 1024;   // the wheel's picture (square); the tiles' window is wide: SizeOf

// The picture's size for a look: the wheel's is kSize square, the tiles' is vr/tiles_geom.h's kW x kH.
void SizeOf(wheel::Style style, int* width, int* height);

// The panel for this view (in the look the view names). Slow-ish (some milliseconds): not on the headset's frame thread - see Async.
void Render(const wheel::View& v, std::vector<uint32_t>& px);

// A worker thread that paints the newest view submitted, whenever it (or an icon) changed. The thread exists from the first
// Submit until Stop.
class Async {
public:
    Async();
    ~Async();
    // The draw thread, every frame the wheel is up: cheap when nothing changed.
    void Submit(const wheel::View& v);
    // The thread started and its tables made, before the first Submit (the first picture is not late by them).
    void Warm();
    // A finished picture newer than the last fetched: moved into `out`, its view revision in *revision and its size in *width, *height
    // (SizeOf the view's look). False if none.
    bool Fetch(std::vector<uint32_t>& out, uint32_t* revision, int* width = nullptr, int* height = nullptr);
    void Stop();

private:
    struct Impl;
    Impl* impl_;
};

}  // namespace wheelpaint
