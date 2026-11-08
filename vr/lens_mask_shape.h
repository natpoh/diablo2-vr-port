// lens_mask_shape.h - [render] lens_mask: the runtime's hidden-area mesh of one eye (XR_KHR_visibility_mask) turned into the rectangles of the
// game's scene depth that the lens never shows (vr/lens_mask.h clears them to the nearest depth right after the game's own depth clear).
//
// The mesh (xrGetVisibilityMaskKHR, XR_VISIBILITY_MASK_TYPE_HIDDEN_TRIANGLE_MESH_KHR) is in the view's own space on the plane z = -1: x and y are
// the tangents of the angles right and up, the same units as the view's fov (tan(angleLeft) .. tan(angleRight)). Our picture of that eye is drawn
// with the frustum `drawn` (tangents l r u d, left and down negative) in the same orientation (not canted), so a tangent (x, y) is the picture's
// pixel ((x - l) / (r - l) * W, (u - y) / (u - d) * H), top-down. With `seen` (the part of the picture the headset is given: runtime_fov's box,
// fov_override_centred's hand view) everything outside it is never shown either.
//
// Never a visible pixel: a pixel is hidden only when its four corners are inside the hidden mesh (or outside `seen`), and then only when every
// pixel within `margin` of it is hidden too (an erosion; beyond the picture's border counts as hidden). The rectangles are bands of `band` rows,
// each band's runs the ones hidden in all its rows, runs repeated in the next band merged into one rectangle - fewer, all inside the mask.
//
// Pure: no D3D12, no OpenXR. Tested in tools/lens_mask_test.cpp.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace lensshape {

struct Tan {
    float l, r, u, d;   // tangents: left and down negative
};
struct Rect {
    int32_t x0, y0, x1, y1;   // pixels [x0, x1) x [y0, y1)
};
struct Stats {
    uint32_t tris = 0;
    double meshShare = 0.0;   // share of the picture's pixels inside the mesh or outside `seen` (corner test, before the margin)
    double rectShare = 0.0;   // share the rectangles cover
};

// A tangent of the view to the picture's pixel units (x right, y down)
inline float PxX(float x, const Tan& d, uint32_t w) { return (x - d.l) / (d.r - d.l) * (float)w; }
inline float PxY(float y, const Tan& d, uint32_t h) { return (d.u - y) / (d.u - d.d) * (float)h; }

// The hidden corners: (w + 1) x (h + 1), 1 = inside the mesh (closed triangles, a hair outward) or outside `seen`
inline std::vector<uint8_t> Corners(const float* xy, uint32_t nVerts, const uint32_t* idx, uint32_t nIdx, const Tan& drawn, const Tan* seen,
                                    uint32_t w, uint32_t h, uint32_t* tris) {
    const uint32_t cw = w + 1, ch = h + 1;
    std::vector<uint8_t> c((size_t)cw * ch, 0);
    uint32_t n = 0;
    for (uint32_t t = 0; t + 2 < nIdx; t += 3) {
        if (idx[t] >= nVerts || idx[t + 1] >= nVerts || idx[t + 2] >= nVerts) continue;
        float px[3], py[3];
        bool finite = true;
        for (int k = 0; k < 3; ++k) {
            px[k] = PxX(xy[2 * idx[t + k]], drawn, w);
            py[k] = PxY(xy[2 * idx[t + k] + 1], drawn, h);
            finite = finite && std::isfinite(px[k]) && std::isfinite(py[k]);
        }
        if (!finite) continue;
        ++n;
        const float ymin = std::min({py[0], py[1], py[2]}), ymax = std::max({py[0], py[1], py[2]});
        const int j0 = std::max(0, (int)std::ceil(ymin - 1e-3f)), j1 = std::min((int)ch - 1, (int)std::floor(ymax + 1e-3f));
        for (int j = j0; j <= j1; ++j) {
            // the row y = j across the triangle: the x of its crossings with the three edges
            float xa = 1e30f, xb = -1e30f;
            const float y = (float)j;
            for (int e = 0; e < 3; ++e) {
                const float ax = px[e], ay = py[e], bx = px[(e + 1) % 3], by = py[(e + 1) % 3];
                const float lo = std::min(ay, by), hi = std::max(ay, by);
                if (y < lo - 1e-3f || y > hi + 1e-3f) continue;
                if (hi - lo < 1e-6f) { xa = std::min({xa, ax, bx}); xb = std::max({xb, ax, bx}); continue; }   // (an edge along the row)
                const float s = std::clamp((y - ay) / (by - ay), 0.0f, 1.0f);
                const float x = ax + s * (bx - ax);
                xa = std::min(xa, x);
                xb = std::max(xb, x);
            }
            if (xa > xb) continue;
            const int i0 = std::max(0, (int)std::ceil(xa - 1e-3f)), i1 = std::min((int)cw - 1, (int)std::floor(xb + 1e-3f));
            for (int i = i0; i <= i1; ++i) c[(size_t)j * cw + i] = 1;
        }
    }
    if (seen) {   // outside the part shown: hidden (a corner on its edge is not)
        const float x0 = PxX(seen->l, drawn, w), x1 = PxX(seen->r, drawn, w), y0 = PxY(seen->u, drawn, h), y1 = PxY(seen->d, drawn, h);
        for (uint32_t j = 0; j < ch; ++j)
            for (uint32_t i = 0; i < cw; ++i)
                if ((float)i < x0 - 1e-3f || (float)i > x1 + 1e-3f || (float)j < y0 - 1e-3f || (float)j > y1 + 1e-3f) c[(size_t)j * cw + i] = 1;
    }
    if (tris) *tris = n;
    return c;
}

// The hidden pixels: w x h, all four corners hidden
inline std::vector<uint8_t> Pixels(const std::vector<uint8_t>& c, uint32_t w, uint32_t h) {
    const uint32_t cw = w + 1;
    std::vector<uint8_t> p((size_t)w * h, 0);
    for (uint32_t j = 0; j < h; ++j)
        for (uint32_t i = 0; i < w; ++i)
            p[(size_t)j * w + i] = c[(size_t)j * cw + i] & c[(size_t)j * cw + i + 1] & c[(size_t)(j + 1) * cw + i] & c[(size_t)(j + 1) * cw + i + 1];
    return p;
}

// Hidden only where every pixel within `m` (a square) is hidden; beyond the picture counts as hidden
inline std::vector<uint8_t> Erode(const std::vector<uint8_t>& p, uint32_t w, uint32_t h, int m) {
    if (m <= 0) return p;
    std::vector<uint8_t> a((size_t)w * h), b((size_t)w * h);
    std::vector<int> pre(std::max(w, h) + 1);
    const int need = 2 * m + 1;
    for (uint32_t j = 0; j < h; ++j) {   // along the rows
        pre[0] = 0;
        for (uint32_t i = 0; i < w; ++i) pre[i + 1] = pre[i] + p[(size_t)j * w + i];
        for (int i = 0; i < (int)w; ++i) {
            const int lo = i - m, hi = i + m;
            const int out = std::max(0, -lo) + std::max(0, hi - ((int)w - 1));
            const int in = pre[std::min(hi, (int)w - 1) + 1] - pre[std::max(lo, 0)];
            a[(size_t)j * w + i] = (uint8_t)(in + out == need);
        }
    }
    for (uint32_t i = 0; i < w; ++i) {   // along the columns
        pre[0] = 0;
        for (uint32_t j = 0; j < h; ++j) pre[j + 1] = pre[j] + a[(size_t)j * w + i];
        for (int j = 0; j < (int)h; ++j) {
            const int lo = j - m, hi = j + m;
            const int out = std::max(0, -lo) + std::max(0, hi - ((int)h - 1));
            const int in = pre[std::min(hi, (int)h - 1) + 1] - pre[std::max(lo, 0)];
            b[(size_t)j * w + i] = (uint8_t)(in + out == need);
        }
    }
    return b;
}

// Rectangles inside the hidden pixels: bands of `band` rows, a band's runs hidden in all its rows, a run the band before had too made one taller
inline std::vector<Rect> Rects(const std::vector<uint8_t>& p, uint32_t w, uint32_t h, int band) {
    band = std::max(1, band);
    std::vector<Rect> done, open;
    std::vector<uint8_t> all(w);
    for (uint32_t y = 0; y < h; y += (uint32_t)band) {
        const uint32_t y1 = std::min(h, y + (uint32_t)band);
        std::fill(all.begin(), all.end(), (uint8_t)1);
        for (uint32_t j = y; j < y1; ++j)
            for (uint32_t i = 0; i < w; ++i) all[i] &= p[(size_t)j * w + i];
        std::vector<Rect> next;
        for (uint32_t i = 0; i < w;) {
            if (!all[i]) { ++i; continue; }
            uint32_t e = i;
            while (e < w && all[e]) ++e;
            bool grown = false;
            for (Rect& o : open)
                if (o.x0 == (int32_t)i && o.x1 == (int32_t)e && o.y1 == (int32_t)y) { o.y1 = (int32_t)y1; next.push_back(o); o.y1 = -1; grown = true; break; }
            if (!grown) next.push_back({(int32_t)i, (int32_t)y, (int32_t)e, (int32_t)y1});
            i = e;
        }
        for (const Rect& o : open)
            if (o.y1 >= 0) done.push_back(o);
        open.swap(next);
    }
    for (const Rect& o : open) done.push_back(o);
    return done;
}

// The whole way: mesh -> rectangles of a w x h picture (st: what it covers)
inline std::vector<Rect> Plan(const float* xy, uint32_t nVerts, const uint32_t* idx, uint32_t nIdx, const Tan& drawn, const Tan* seen, uint32_t w,
                              uint32_t h, int margin, int band, Stats* st) {
    if (!w || !h || !(drawn.r > drawn.l) || !(drawn.u > drawn.d)) return {};
    uint32_t tris = 0;
    const std::vector<uint8_t> c = Corners(xy, nVerts, idx, nIdx, drawn, seen, w, h, &tris);
    const std::vector<uint8_t> px = Pixels(c, w, h);
    std::vector<Rect> r = Rects(Erode(px, w, h, margin), w, h, band);
    if (st) {
        size_t hidden = 0;
        for (uint8_t v : px) hidden += v;
        uint64_t area = 0;
        for (const Rect& q : r) area += (uint64_t)(q.x1 - q.x0) * (uint64_t)(q.y1 - q.y0);
        st->tris = tris;
        st->meshShare = (double)hidden / ((double)w * h);
        st->rectShare = (double)area / ((double)w * h);
    }
    return r;
}

// The rectangles of the w x h picture in the part of the scene's target the game draws into (an upscaler's share sx x sy of the top left; 1 x 1
// without one): only the target's pixels wholly inside a rectangle (rounded inward)
inline std::vector<Rect> Scaled(const std::vector<Rect>& r, float sx, float sy) {
    if (sx >= 0.9999f && sy >= 0.9999f) return r;
    std::vector<Rect> o;
    o.reserve(r.size());
    for (const Rect& q : r) {
        const Rect s{(int32_t)std::ceil(q.x0 * sx - 1e-4f), (int32_t)std::ceil(q.y0 * sy - 1e-4f), (int32_t)std::floor(q.x1 * sx + 1e-4f),
                     (int32_t)std::floor(q.y1 * sy + 1e-4f)};
        if (s.x1 > s.x0 && s.y1 > s.y0) o.push_back(s);
    }
    return o;
}

}  // namespace lensshape
