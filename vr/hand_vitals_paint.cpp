// hand_vitals_paint.cpp - the hand vitals panel's picture (vr/hand_vitals_paint.h). Our own drawing; nothing of anyone else's.
//
// The look is taken from what the player knows of the game's own life and mana orbs - a dark metal rim, a red and a blue liquid
// that fills from the bottom behind glass, a soft highlight on the upper left - and drawn from scratch: every pixel is a few
// distance fields. The numbers are GDI text (Segoe UI) into a coverage mask, fitted to the width under each globe.

#include "hand_vitals_paint.h"

#include "paint_kit.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace handvitalspaint {
namespace {

using wheelpaint::kit::Canvas;
using wheelpaint::kit::Clamp01;
using wheelpaint::kit::Col;
using wheelpaint::kit::Mix;

constexpr float kPi = 3.14159265358979f;

constexpr float kRout = 78.0f, kRim = 8.0f, kRin = kRout - kRim;   // the globe: outer radius, the rim's width, the glass
constexpr float kGlobeY = 100.0f, kLifeX = 96.0f, kManaX = 288.0f;
constexpr float kTextTop = 188.0f, kTextH = 50.0f, kTextW = 166.0f;

float Smooth(float t) { t = Clamp01(t); return t * t * (3.0f - 2.0f * t); }

Col Scale(Col c, float k) { return {c.r * k, c.g * k, c.b * k}; }

// One globe. `fill` 0 - 1 from the bottom; `top` / `bottom` the liquid's colours there; `alert` a red glow round the rim.
void Globe(Canvas& cv, float cx, float cy, float fill, Col top, Col bottom, bool alert) {
    const float pad = alert ? 16.0f : 2.0f;
    const int x0 = std::max(0, (int)(cx - kRout - pad)), x1 = std::min(cv.w, (int)(cx + kRout + pad) + 1);
    const int y0 = std::max(0, (int)(cy - kRout - pad)), y1 = std::min(cv.h, (int)(cy + kRout + pad) + 1);
    const float level = cy + kRin - 2.0f * kRin * fill;   // the liquid is below this line
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            const float d = std::sqrt(dx * dx + dy * dy);
            if (alert && d > kRout - 1.0f) {   // the glow: red, fading in ~6 px
                const float g = 0.62f * std::exp(-(d - kRout) / 5.5f) * Clamp01((kRout + pad - d) / 6.0f);
                cv.Over(x, y, Col{255.0f, 36.0f, 28.0f}, g);
            }
            const float outer = Clamp01(kRout + 0.5f - d);
            if (outer <= 0.0f) continue;
            const float glass = Clamp01(kRin + 0.5f - d);

            // -- the metal rim: a bevel lit from the upper left, darker where it meets the glass
            const float rimA = outer * (1.0f - glass);
            if (rimA > 0.0f) {
                const float ang = std::atan2(dy, dx);
                const float light = 0.5f + 0.5f * std::cos(ang + 0.75f * kPi);   // 1 toward the upper left
                const float t = Clamp01((d - kRin) / kRim);
                const float ridge = 1.0f - std::fabs(2.0f * t - 1.0f);            // 1 along the middle of the rim
                float k = 0.12f + 0.62f * light * (0.55f + 0.45f * ridge);
                k *= 0.55f + 0.45f * Smooth(t * 3.0f);                             // a shade where it meets the glass
                Col rim = Mix(Col{34.0f, 29.0f, 26.0f}, Col{204.0f, 186.0f, 142.0f}, Clamp01(k));
                cv.Over(x, y, rim, rimA);
            }

            // -- the glass, the liquid behind it
            if (glass <= 0.0f) continue;
            const float nz = std::sqrt(std::max(0.0f, 1.0f - (d / kRin) * (d / kRin)));   // the sphere's normal toward us
            Col c = Scale(Col{20.0f, 15.0f, 17.0f}, 0.55f + 0.45f * nz);                  // empty glass
            const float depth = y + 0.5f - level;
            const float liquidA = fill > 0.0f ? Clamp01(depth + 0.5f) : 0.0f;
            if (liquidA > 0.0f) {
                const float v = Clamp01((y + 0.5f - (cy - kRin)) / (2.0f * kRin));
                Col l = Mix(top, bottom, v);
                l = Scale(l, 0.52f + 0.48f * std::pow(nz, 0.6f));
                const float meniscus = 0.38f * Clamp01(1.0f - depth / 3.5f);              // a brighter edge where the liquid ends
                l = Mix(l, Col{255.0f, 236.0f, 226.0f}, meniscus);
                c = Mix(c, l, liquidA);
            }
            // the soft highlight on the upper left, over the glass and what is behind it
            const float hx = (dx + 0.34f * kRin) / (0.50f * kRin), hy = (dy + 0.42f * kRin) / (0.30f * kRin);
            const float hl = std::exp(-(hx * hx + hy * hy) * 1.3f);
            c = Mix(c, Col{255.0f, 250.0f, 245.0f}, 0.42f * hl);
            // a faint light along the bottom edge of the glass
            const float bx = dx / (0.7f * kRin), by = (dy - 0.92f * kRin) / (0.14f * kRin);
            c = Mix(c, Col{255.0f, 255.0f, 255.0f}, 0.10f * std::exp(-(bx * bx + by * by) * 1.6f));
            cv.Over(x, y, c, glass);
        }
}

// The numbers: GDI text into a coverage mask, shrunk until it fits `maxW`, drawn centred on `cx` with a dark halo.
void Numbers(Canvas& cv, const char* text, float cx, float top, float boxH, float maxW) {
    if (!text || !*text) return;
    wchar_t w[64] = {};
    MultiByteToWideChar(CP_UTF8, 0, text, -1, w, 63);
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return;
    HFONT font = nullptr;
    SIZE sz{0, 0};
    for (int px = 34; px >= 14; px -= 2) {
        if (font) DeleteObject(font);
        font = CreateFontW(-px, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        if (!font) break;
        HGDIOBJ old = SelectObject(dc, font);
        GetTextExtentPoint32W(dc, w, (int)wcslen(w), &sz);
        SelectObject(dc, old);
        if ((float)sz.cx <= maxW) break;
    }
    if (!font || sz.cx <= 0 || sz.cy <= 0) {
        if (font) DeleteObject(font);
        DeleteDC(dc);
        return;
    }
    const int bw = std::min((int)maxW + 8, (int)sz.cx + 8), bh = (int)sz.cy + 4;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = bw;
    bi.bmiHeader.biHeight = -bh;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp || !bits) {
        if (bmp) DeleteObject(bmp);
        DeleteObject(font);
        DeleteDC(dc);
        return;
    }
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    HGDIOBJ oldFont = SelectObject(dc, font);
    std::memset(bits, 0, (size_t)bw * bh * 4);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    RECT r = {0, 2, bw, bh - 2};
    DrawTextW(dc, w, -1, &r, DT_CENTER | DT_NOPREFIX | DT_SINGLELINE | DT_VCENTER);
    GdiFlush();
    const uint8_t* p = static_cast<const uint8_t*>(bits);
    auto cover = [&](int x, int y) -> float {
        if (x < 0 || y < 0 || x >= bw || y >= bh) return 0.0f;
        const uint8_t* s = p + ((size_t)y * bw + x) * 4;
        return (s[0] + s[1] + s[2]) / (3.0f * 255.0f);
    };
    const int left = (int)(cx - bw * 0.5f), topY = (int)(top + (boxH - bh) * 0.5f);
    for (int y = 0; y < bh; ++y)
        for (int x = 0; x < bw; ++x) {
            float halo = 0.0f;   // the dark halo: the text dilated by two pixels
            for (int oy = -2; oy <= 2; ++oy)
                for (int ox = -2; ox <= 2; ++ox)
                    if (ox * ox + oy * oy <= 5) halo = std::max(halo, cover(x + ox, y + oy));
            if (halo > 0.0f) cv.Over(left + x, topY + y, Col{6.0f, 4.0f, 4.0f}, 0.85f * halo);
        }
    for (int y = 0; y < bh; ++y)
        for (int x = 0; x < bw; ++x) {
            const float c = cover(x, y);
            if (c > 0.0f) cv.Over(left + x, topY + y, Col{244.0f, 236.0f, 218.0f}, c);
        }
    SelectObject(dc, oldFont);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteObject(font);
    DeleteDC(dc);
}

// The faint plate behind the whole panel, so the numbers read over a bright hand or floor: a rounded rectangle, dark and mostly clear.
void Plate(Canvas& cv) {
    const float l = 6.0f, t = 6.0f, r = (float)kW - 6.0f, b = (float)kH - 6.0f, radius = 28.0f;
    for (int y = 0; y < cv.h; ++y)
        for (int x = 0; x < cv.w; ++x) {
            const float cx = 0.5f * (l + r), cy = 0.5f * (t + b), hw = 0.5f * (r - l), hh = 0.5f * (b - t);
            const float qx = std::fabs(x + 0.5f - cx) - hw + radius, qy = std::fabs(y + 0.5f - cy) - hh + radius;
            const float d = std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - radius;
            const float cover = Clamp01(0.5f - d);
            if (cover <= 0.0f) continue;
            cv.Over(x, y, Col{12.0f, 9.0f, 9.0f}, 0.52f * cover);
            const float edge = Clamp01(1.5f + 0.5f + d) * cover;   // a thin brass line along the plate's edge
            cv.Over(x, y, Col{120.0f, 104.0f, 74.0f}, 0.45f * edge);
        }
}

}  // namespace

void Render(const handvitals::Look& look, std::vector<uint32_t>& px) {
    px.assign((size_t)kW * kH, 0u);
    Canvas cv{px, kW, kH};
    Plate(cv);
    const float lifeFill = (float)look.lifeFill / (float)handvitals::kFillSteps;
    const float manaFill = (float)look.manaFill / (float)handvitals::kFillSteps;
    Globe(cv, kLifeX, kGlobeY, lifeFill, Col{232.0f, 28.0f, 32.0f}, Col{146.0f, 8.0f, 14.0f}, look.alert);
    Globe(cv, kManaX, kGlobeY, manaFill, Col{84.0f, 138.0f, 255.0f}, Col{22.0f, 44.0f, 166.0f}, false);
    Numbers(cv, look.lifeText, kLifeX, kTextTop, kTextH, kTextW);
    Numbers(cv, look.manaText, kManaX, kTextTop, kTextH, kTextW);
    const float o = std::clamp((float)look.opacity / 20.0f, 0.0f, 1.0f);   // the whole picture, premultiplied: every channel
    if (o < 0.999f)
        for (uint32_t& p : px) {
            const uint32_t r = (uint32_t)((p & 255u) * o + 0.5f), g = (uint32_t)(((p >> 8) & 255u) * o + 0.5f), b = (uint32_t)(((p >> 16) & 255u) * o + 0.5f),
                           a = (uint32_t)((p >> 24) * o + 0.5f);
            p = r | g << 8 | b << 16 | a << 24;
        }
}

}  // namespace handvitalspaint
