// sticker_atlas.h - the small pictures painted once a session (the laser's beam and dot, the inventory's two buttons, the map compass's strip, the big
// map's button) in ONE image, for one swapchain (vr/xr.cpp g_stickers) instead of a static swapchain each. Pure C++ (tools/sticker_atlas_test.cpp).
//
// SteamVR gives a session 16 swapchains (vr/chain_count.h): six of them went to these. Each picture is put in shelves, left to right, top to bottom, with
// kPad pixels round it that repeat its own edge pixels - a quad sampling its rectangle of the atlas reads at the edge what a swapchain of its own read there
// (clamped to its edge), never a neighbour. Pictures are only added, never moved: a rectangle handed out stays where it is for the session.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace stickers {

constexpr int kPad = 2;

struct Rect { int32_t x = 0, y = 0, w = 0, h = 0; };

class Atlas {
  public:
    Atlas(int w, int h) : w_(w), h_(h), px_((std::size_t)w * h, 0u) {}
    int W() const { return w_; }
    int H() const { return h_; }
    const std::vector<uint32_t>& Pixels() const { return px_; }
    // Bumped by each picture put in: the swapchain holds them all once an upload of this generation is done.
    uint32_t Gen() const { return gen_; }
    // A w x h picture (rows top first) copied in, its edges repeated round it. False: no room left (nothing changed).
    bool Put(int w, int h, const uint32_t* px, Rect* out) {
        if (w <= 0 || h <= 0 || !px) return false;
        const int cw = w + 2 * kPad, ch = h + 2 * kPad;
        if (cw > w_) return false;
        if (x_ + cw > w_) { y_ += row_; x_ = 0; row_ = 0; }   // the next shelf
        if (y_ + ch > h_) return false;
        const int ox = x_ + kPad, oy = y_ + kPad;
        for (int y = -kPad; y < h + kPad; ++y) {
            const int sy = y < 0 ? 0 : y >= h ? h - 1 : y;
            uint32_t* dst = &px_[(std::size_t)(oy + y) * w_ + ox];
            const uint32_t* src = px + (std::size_t)sy * w;
            for (int x = -kPad; x < w + kPad; ++x) dst[x] = src[x < 0 ? 0 : x >= w ? w - 1 : x];
        }
        *out = Rect{ox, oy, w, h};
        x_ += cw;
        if (ch > row_) row_ = ch;
        ++gen_;
        return true;
    }
    // A new session: empty again (its rectangles are void).
    void Clear() {
        px_.assign(px_.size(), 0u);
        x_ = y_ = row_ = 0;
        gen_ = 0;
    }

  private:
    int w_, h_;
    std::vector<uint32_t> px_;
    int x_ = 0, y_ = 0, row_ = 0;
    uint32_t gen_ = 0;
};

}  // namespace stickers
