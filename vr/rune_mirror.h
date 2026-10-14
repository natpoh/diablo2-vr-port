// rune_mirror.h - the CPU mirror of vr/rune_shader.h's pixel shader: the same sums in C++, pixel by pixel. Not used by the plugin; the test
// (cleanroom/tests/rune_test.cpp) compares it with the shader itself run on a WARP device, and draws pictures of a rune and a trail with
// it. Keep it in step with the HLSL text: the numbers and the order of the operations are the same on purpose.
#pragma once

#include <cmath>
#include <vector>

#include "rune_shader.h"

namespace runes {
namespace mirror {

struct F4 { float r, g, b, a; };

inline float Saturate(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }
inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
// HLSL smoothstep(lo, hi, x)
inline float Smooth(float lo, float hi, float x) {
    const float t = Saturate((x - lo) / (hi - lo));
    return t * t * (3.0f - 2.0f * t);
}
inline F4 Over(F4 top, F4 bottom) {
    const float k = 1.0f - top.a;
    return {top.r + bottom.r * k, top.g + bottom.g * k, top.b + bottom.b * k, top.a + bottom.a * k};
}

struct F4x { float x, y, z, w; };
inline F4x Max4(F4x a, F4x b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z), std::max(a.w, b.w)}; }

// A line from what the loop found: acc = (cover, halo, deep, rim). col: linear rgb, alpha.
inline F4 Stroke(F4x acc, const float col[4], float glow) {
    const float white = kWhiteCore * glow * acc.z * acc.z;
    const float a = col[3];
    const F4 c{Lerp(col[0], 1.0f, white) * acc.x * a, Lerp(col[1], 1.0f, white) * acc.x * a, Lerp(col[2], 1.0f, white) * acc.x * a, acc.x * a};
    const float h = acc.y * (0.15f + 0.5f * glow) * a;
    return Over(c, F4{col[0] * h, col[1] * h, col[2] * h, h * kHaloAdd});
}

inline F4x Reach(float sd, float ta, float hw, float hs, float px, float w) {
    const float beyond = std::max(sd, 0.0f);
    const float reach = 4.0f * hs;
    const float rimFall = 1.0f - Smooth(0.0f, kRimReach * w, sd);
    return {Saturate(0.5f - sd / px) * ta, std::exp(-beyond / hs) * (1.0f - Smooth(0.5f * reach, reach, beyond)) * ta, Saturate(-sd / hw),
            Smooth(-0.5f * px, 0.5f * px, sd) * rimFall * rimFall * ta};
}

inline float Luma(const float c[4]) { return c[0] * 0.2126f + c[1] * 0.7152f + c[2] * 0.0722f; }

// SV_Position (the pixel's centre, in the atlas' pixels) -> the pixel shader's premultiplied linear colour.
inline F4 Pixel(const CellCb& cb, float posX, float posY) {
    const float qx = ((posX - cb.cell[0]) / cb.cell[2] - 0.5f) * cb.ext[0];
    const float qy = -(((posY - cb.cell[1]) / cb.cell[2] - 0.5f) * cb.ext[0]);
    const float px = cb.ext[0] / cb.cell[2];
    const float w = cb.ext[1];
    const float hw = 0.5f * w;
    const float hs = w * (0.6f + 1.2f * cb.ext[2]);
    const float reach = 4.0f * hs;
    const uint32_t n = (uint32_t)cb.ext[3];
    const uint32_t nl = (uint32_t)cb.misc[0];
    const bool trail = cb.misc[1] > 0.5f;
    F4x accL{0, 0, 0, 0}, accU{0, 0, 0, 0};
    for (uint32_t i = 0; i < n; ++i) {
        const float* s = cb.seg[i];
        const float ax = s[0], ay = s[1], abx = s[2] - s[0], aby = s[3] - s[1];
        const float t = Saturate(((qx - ax) * abx + (qy - ay) * aby) / std::max(abx * abx + aby * aby, 1e-10f));
        const float dx = qx - (ax + abx * t), dy = qy - (ay + aby * t);
        const float d = std::sqrt(dx * dx + dy * dy);
        const float along = trail ? ((float)i + t) / (float)n : 1.0f;
        const float wf = trail ? Lerp(kTailWidth, 1.0f, along) : 1.0f;
        const float sd = d - hw * wf;
        if (sd > reach) continue;
        const F4x v = Reach(sd, trail ? Lerp(kTailAlpha, 1.0f, along) : 1.0f, hw * wf, hs, px, w);
        if (i < nl) accL = Max4(accL, v); else accU = Max4(accU, v);
    }
    const float* cl = cb.lit;
    const float* cu = cb.unlit;
    const float rim = kRimAlpha * std::max(accL.w * cl[3] * (0.5f + 1.2f * Luma(cl)), accU.w * cu[3] * (0.5f + 1.2f * Luma(cu)));
    F4 res{0.0f, 0.0f, 0.0f, rim};
    res = Over(Stroke(accU, cu, cb.ext[2]), res);
    res = Over(Stroke(accL, cl, cb.ext[2]), res);
    if (cb.blob[2] > 0.0f) {
        const float rc = cb.blob[2] * w;
        const float bx = qx - cb.blob[0], by = qy - cb.blob[1];
        const float db = std::sqrt(bx * bx + by * by) - rc;
        res = Over(Stroke(Reach(db, 1.0f, rc, kBlobHalo * w, px, w), cb.unlit, std::max(cb.ext[2], 0.8f)), res);
    }
    return {Saturate(res.r), Saturate(res.g), Saturate(res.b), Saturate(res.a)};
}

// The whole cell, kCell x kCell premultiplied linear floats (rgba), the way the shader fills its cell.
inline void RenderCell(const CellCb& cb, std::vector<F4>* out) {
    const uint32_t size = (uint32_t)cb.cell[2];
    out->assign((size_t)size * size, F4{0, 0, 0, 0});
    for (uint32_t y = 0; y < size; ++y)
        for (uint32_t x = 0; x < size; ++x) (*out)[(size_t)y * size + x] = Pixel(cb, cb.cell[0] + (float)x + 0.5f, cb.cell[1] + (float)y + 0.5f);
}

}  // namespace mirror
}  // namespace runes
