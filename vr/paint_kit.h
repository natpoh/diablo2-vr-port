#pragma once
// Drawing the skill panel's two looks share (vr/wheel_paint.cpp: the wheel, vr/tiles_paint.cpp: the tiles): a premultiplied
// canvas, a rounded rectangle by its distance field, text through GDI into a coverage mask, an icon scaled into a box. Moved here
// from wheel_paint.cpp unchanged, except that the canvas knows its own size (the tiles' window is not square).
// Our own drawing; nothing of anyone else's.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "wheel_geom.h"
#include "wheel_icons.h"

namespace wheelpaint {
namespace kit {

using wheel::Rect;

struct Col {
    float r, g, b;
};
inline float Clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }
inline Col Mix(Col a, Col b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }

// -- the canvas: premultiplied pixels, 0xAABBGGRR ------------------------------------------------------------------------------

struct Canvas {
    std::vector<uint32_t>& px;
    int w, h;   // the picture's size in pixels
    void Over(int x, int y, Col c, float a) {
        if (a <= 0.0f || x < 0 || y < 0 || x >= w || y >= h) return;
        a = Clamp01(a);
        uint32_t& d = px[(size_t)y * w + x];
        const float ia = 1.0f - a;
        const float r = c.r * a + float(d & 255) * ia, g = c.g * a + float((d >> 8) & 255) * ia, b = c.b * a + float((d >> 16) & 255) * ia,
                    o = 255.0f * a + float(d >> 24) * ia;
        d = uint32_t(std::min(255.0f, r + 0.5f)) | uint32_t(std::min(255.0f, g + 0.5f)) << 8 | uint32_t(std::min(255.0f, b + 0.5f)) << 16 |
            uint32_t(std::min(255.0f, o + 0.5f)) << 24;
    }
};

// A rounded rectangle by its distance field: fill and outline, anti-aliased.
inline void RoundRect(Canvas& cv, Rect r, float radius, Col fill, float fillA, Col edge, float edgeA, float edgeW) {
    const float cx = 0.5f * (r.l + r.r), cy = 0.5f * (r.t + r.b), hw = 0.5f * (r.r - r.l), hh = 0.5f * (r.b - r.t);
    for (int y = std::max(0, int(r.t) - 1); y < std::min(cv.h, int(r.b) + 2); ++y)
        for (int x = std::max(0, int(r.l) - 1); x < std::min(cv.w, int(r.r) + 2); ++x) {
            const float qx = std::fabs(x + 0.5f - cx) - hw + radius, qy = std::fabs(y + 0.5f - cy) - hh + radius;
            const float sd = std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - radius;
            const float cover = Clamp01(0.5f - sd);
            if (cover <= 0.0f) continue;
            cv.Over(x, y, fill, fillA * cover);
            if (edgeW > 0.0f) cv.Over(x, y, edge, edgeA * cover * Clamp01(edgeW + 0.5f + sd));
        }
}

// -- text through GDI ------------------------------------------------------------------------------------------------------------

struct Fonts {
    HFONT f[16] = {};
    int size[16] = {};
    bool bold[16] = {};
    HFONT Get(int px, bool b) {
        for (int i = 0; i < 16; ++i)
            if (f[i] && size[i] == px && bold[i] == b) return f[i];
        for (int i = 0; i < 16; ++i)
            if (!f[i]) {
                f[i] = CreateFontW(-px, 0, 0, 0, b ? FW_BOLD : FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                   CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Georgia");
                size[i] = px;
                bold[i] = b;
                return f[i];
            }
        return f[0];
    }
    ~Fonts() {
        for (HFONT h : f) if (h) DeleteObject(h);
    }
};

// `box` in pixels; the text centred in it (both ways) unless `left`; wrapped at the box's width.
inline void Text(Canvas& cv, Fonts& fonts, const char* utf8, Rect box, int size, bool bold, Col color, float alpha = 1.0f, bool left = false) {
    if (!utf8 || !*utf8) return;
    wchar_t w[256] = {};
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, 255);
    const int bw = std::max(1, int(box.r - box.l)), bh = std::max(1, int(box.b - box.t));
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = bw;
    bi.bmiHeader.biHeight = -bh;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    HDC dc = CreateCompatibleDC(nullptr);
    void* bits = nullptr;
    HBITMAP bmp = dc ? CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
    if (!dc || !bmp || !bits) {
        if (bmp) DeleteObject(bmp);
        if (dc) DeleteDC(dc);
        return;
    }
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    HGDIOBJ oldFont = SelectObject(dc, fonts.Get(size, bold));
    std::memset(bits, 0, (size_t)bw * bh * 4);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    const UINT flags = DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS | (left ? DT_LEFT : DT_CENTER);
    RECT measure = {0, 0, bw, 0};
    DrawTextW(dc, w, -1, &measure, flags | DT_CALCRECT);
    const int th = std::min(bh, int(measure.bottom - measure.top));
    RECT r = {0, (bh - th) / 2, bw, (bh - th) / 2 + th};
    DrawTextW(dc, w, -1, &r, flags);
    GdiFlush();
    const uint8_t* p = static_cast<const uint8_t*>(bits);
    for (int y = 0; y < bh; ++y)
        for (int x = 0; x < bw; ++x) {
            const uint8_t* s = p + ((size_t)y * bw + x) * 4;
            const float cover = (s[0] + s[1] + s[2]) / (3.0f * 255.0f);
            if (cover > 0.0f) cv.Over(int(box.l) + x, int(box.t) + y, color, cover * alpha);
        }
    SelectObject(dc, oldFont);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
}

// An icon scaled into a box, kept in proportion; `dim` for a skill the player does not have.
// How an icon's colours are turned as it is laid down: as they are, greyed (a skill the player does not have), or in the game's red -
// its luminance mapped to a dark red plate and a pale pinkish-red picture (the tile of a slot that holds a skill).
enum class IconTone { Normal, Dim, Red };
inline void Icon(Canvas& cv, const std::shared_ptr<const wheelicons::Image>& img, Rect box, IconTone tone) {
    if (!img || img->rgba.empty()) return;
    const float bw = box.r - box.l, bh = box.b - box.t;
    const float scale = std::min(bw / img->width, bh / img->height);
    const int w = std::max(1, int(img->width * scale)), h = std::max(1, int(img->height * scale));
    const int left = int(box.l + (bw - w) * 0.5f), top = int(box.t + (bh - h) * 0.5f);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const float sx = std::max(0.0f, std::min((x + 0.5f) / scale - 0.5f, img->width - 1.001f)),
                        sy = std::max(0.0f, std::min((y + 0.5f) / scale - 0.5f, img->height - 1.001f));
            const int x0 = std::max(0, int(sx)), y0 = std::max(0, int(sy));
            const float fx = sx - x0, fy = sy - y0;
            float c[4] = {};
            for (int k = 0; k < 4; ++k) {
                const int x1 = std::min<int>(x0 + 1, img->width - 1), y1 = std::min<int>(y0 + 1, img->height - 1);
                auto at = [&](int xx, int yy) { return float(img->rgba[((size_t)yy * img->width + xx) * 4 + k]); };
                c[k] = (at(x0, y0) * (1 - fx) + at(x1, y0) * fx) * (1 - fy) + (at(x0, y1) * (1 - fx) + at(x1, y1) * fx) * fy;
            }
            Col col{c[0], c[1], c[2]};
            if (tone == IconTone::Dim) {
                const float gray = (c[0] + c[1] + c[2]) / 3.0f * 0.5f;
                col = {gray, gray, gray};
            } else if (tone == IconTone::Red) {
                const float l = Clamp01((c[0] + c[1] + c[2]) / 765.0f);
                col = {std::min(255.0f, 34.0f + 235.0f * std::pow(l, 0.85f)), 4.0f + 96.0f * l * l, 8.0f + 132.0f * l * l};
            }
            cv.Over(left + x, top + y, col, c[3] / 255.0f);
        }
}
inline void Icon(Canvas& cv, const std::shared_ptr<const wheelicons::Image>& img, Rect box, bool dim) {
    Icon(cv, img, box, dim ? IconTone::Dim : IconTone::Normal);
}

}  // namespace kit
}  // namespace wheelpaint
