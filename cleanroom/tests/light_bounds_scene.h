#pragma once
// The picture and the reference for the light-bounds tests (light_bounds_test.cpp on a software device,
// tools/light_bounds_reshade_test.cpp through ReShade on the GPU): a deterministic depth picture, and a scan of every
// pixel the shader is meant to read, per tile.

#include <algorithm>
#include <cstdint>
#include <vector>

namespace lbscene {

struct Pair { float low, high; };

inline constexpr uint32_t kCols = 32, kRows = 20, kPitch = 36;

struct Case {
    const char* name;
    uint32_t w, h;        // the lit area, pixels
    bool atlas;           // the area drawn into the middle of a texture twice as big
    bool squareTiles;     // 16:10: the game's own shader is right
};

// A deterministic picture: depths 0.05 .. 0.95 with 12% background (0), some tiles empty, and a spike at each
// tile's bottom right corner (the part a wrong row misses).
inline std::vector<float> MakeDepth(const Case& c, uint32_t tw, uint32_t th, uint32_t ox, uint32_t oy) {
    std::vector<float> d((size_t)tw * th, 0.0f);
    uint32_t s = 12345;
    auto rnd = [&]() { s = s * 1664525u + 1013904223u; return (s >> 8) / 16777216.0f; };
    for (uint32_t y = 0; y < c.h; ++y)
        for (uint32_t x = 0; x < c.w; ++x) {
            const uint32_t tx = x * kCols / c.w, ty = y * kRows / c.h;
            const uint32_t tile = ty * kCols + tx;
            float v = 0.05f + 0.9f * rnd();
            if (rnd() < 0.12f || tile % 11 == 0) v = 0.0f;
            d[(size_t)(y + oy) * tw + x + ox] = v;
        }
    // each tile: a small far value and a near one in its last two rows and columns
    for (uint32_t ty = 0; ty < kRows; ++ty)
        for (uint32_t tx = 0; tx < kCols; ++tx) {
            if ((ty * kCols + tx) % 11 == 0) continue;
            const uint32_t x1 = (tx + 1) * c.w / kCols, y1 = (ty + 1) * c.h / kRows;
            d[(size_t)(y1 - 3 + oy) * tw + (x1 - 3) + ox] = 0.011f;    // the smallest
            d[(size_t)(y1 - 4 + oy) * tw + (x1 - 4) + ox] = 0.987f;    // the largest
        }
    return d;
}

// The reference: every pixel of the tile's 2x2 blocks (a tile of odd size is read one pixel past its edge,
// as the gather does), clamped to the texture.
inline std::vector<Pair> Scan(const Case& c, const std::vector<float>& depth, uint32_t tw, uint32_t th, uint32_t ox, uint32_t oy) {
    std::vector<Pair> out((size_t)kPitch * kRows, Pair{0, 0});
    for (uint32_t ty = 0; ty < kRows; ++ty)
        for (uint32_t tx = 0; tx < kCols; ++tx) {
            const uint32_t x0 = tx * c.w / kCols, x1 = (tx + 1) * c.w / kCols;
            const uint32_t y0 = ty * c.h / kRows, y1 = (ty + 1) * c.h / kRows;
            const uint32_t bw = (x1 - x0 + 1) / 2, bh = (y1 - y0 + 1) / 2;
            Pair p{1.0f, 0.0f};
            for (uint32_t y = y0; y < y0 + 2 * bh; ++y)
                for (uint32_t x = x0; x < x0 + 2 * bw; ++x) {
                    const float v = depth[(size_t)std::min(y + oy, th - 1) * tw + std::min(x + ox, tw - 1)];
                    if (v == 0.0f) continue;
                    p.low = std::min(p.low, v);
                    p.high = std::max(p.high, v);
                }
            out[(size_t)ty * kPitch + tx] = (p.low == 1.0f && p.high == 0.0f) ? Pair{0, 0} : p;
        }
    return out;
}

}  // namespace lbscene
