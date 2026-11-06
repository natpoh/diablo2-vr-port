// own_labels_paint.cpp - [hud] labels_own: one item label's picture (vr/own_labels_paint.h). Our own drawing.

#include "paint_kit.h"   // (windows.h, the canvas, the rounded box)
#include "own_labels_paint.h"
#include "game_ui.h"   // Covers: every letter in the face

#include <algorithm>
#include <cmath>
#include <string>

namespace ownlabels {
namespace {

using wheelpaint::kit::Canvas;
using wheelpaint::kit::Col;
using wheelpaint::kit::RoundRect;

// The name's coverage, w x h, drawn white on black by GDI with `font`, its left edge at x0, centred up and down.
bool Coverage(const std::wstring& text, HFONT font, int w, int h, int x0, std::vector<float>* cover) {
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    HDC dc = CreateCompatibleDC(nullptr);
    void* bits = nullptr;
    HBITMAP bmp = dc ? CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
    if (!dc || !bmp || !bits) {
        if (bmp) DeleteObject(bmp);
        if (dc) DeleteDC(dc);
        return false;
    }
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    HGDIOBJ oldFont = SelectObject(dc, font);
    std::memset(bits, 0, (size_t)w * h * 4);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    TEXTMETRICW tm = {};
    GetTextMetricsW(dc, &tm);
    const int y0 = (h - (int)(tm.tmAscent + tm.tmDescent)) / 2;
    TextOutW(dc, x0, y0, text.c_str(), (int)text.size());
    GdiFlush();
    cover->assign((size_t)w * h, 0.0f);
    const uint8_t* p = static_cast<const uint8_t*>(bits);
    for (size_t i = 0; i < (size_t)w * h; ++i) (*cover)[i] = (p[i * 4] + p[i * 4 + 1] + p[i * 4 + 2]) / (3.0f * 255.0f);
    SelectObject(dc, oldFont);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
    return true;
}

int TextWidth(const std::wstring& text, HFONT font) {
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return 0;
    HGDIOBJ old = SelectObject(dc, font);
    SIZE s = {};
    GetTextExtentPoint32W(dc, text.c_str(), (int)text.size(), &s);
    // an italic or a swash may reach past the advance: room for it
    ABC abc = {};
    int over = 0;
    if (!text.empty() && GetCharABCWidthsW(dc, text.back(), text.back(), &abc) && abc.abcC < 0) over = -abc.abcC;
    SelectObject(dc, old);
    DeleteDC(dc);
    return (int)s.cx + over;
}

Col Byte(const float c[3]) { return {std::clamp(c[0], 0.0f, 1.0f) * 255.0f, std::clamp(c[1], 0.0f, 1.0f) * 255.0f, std::clamp(c[2], 0.0f, 1.0f) * 255.0f}; }

}  // namespace

bool Paint(const Want& w, const wchar_t* face, std::vector<uint32_t>* px, int* outW, int* outH) {
    if (!px || !outW || !outH || !w.text[0]) return false;
    const std::wstring text = lang::FromUtf8(w.text);
    if (text.empty()) return false;
    // a letter the face has not (a game icon, a script our face lacks) would come out as a box: not painted at all - the game's own text stays
    if (!gameui::Covers(face ? face : uitext::Face(L"Georgia"), text)) return false;
    const int h = std::clamp(w.heightPx, kMinPx, kMaxPx);
    const int fontPx = w.bold ? NameFontPx(h) : FontPx(h), pad = PadX(h);
    const int edge = std::max(1, (int)std::lround(h / 22.0f));   // the dark edge round the letters
    HFONT font = CreateFontW(-fontPx, 0, 0, 0, face ? FW_NORMAL : w.bold ? FW_BOLD : FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, DEFAULT_PITCH, face ? face : uitext::Face(L"Georgia"));
    if (!font) return false;
    const int tw = TextWidth(text, font);
    const int cw = std::min(kAtlasW - 2 * kGap, std::max(h, tw + 2 * pad + 2 * edge));
    std::vector<float> cover;
    const bool ok = tw > 0 && Coverage(text, font, cw, h, std::max(pad + edge, (cw - tw) / 2), &cover);
    DeleteObject(font);
    if (!ok) return false;

    px->assign((size_t)cw * h, 0u);
    Canvas cv{*px, cw, h};
    const wheel::Rect all{0.0f, 0.0f, (float)cw, (float)h};
    const float radius = std::max(1.0f, h * 0.10f);
    if (w.lit) {   // the pointed item (point_pickup): a dark shade of its colour behind, a rim in it (red: out of reach)
        const float shade[3] = {w.rgb[0] * 0.3f, w.rgb[1] * 0.3f, w.rgb[2] * 0.3f};
        const float red[3] = {1.0f, 0.22f, 0.18f};
        RoundRect(cv, all, radius, Byte(shade), 0.92f, Byte(w.inReach ? w.rgb : red), 1.0f, std::max(1.5f, h / 16.0f));
    } else if (w.bg[3] > 0.0f) {   // the game's own box colour (below 0 the game draws no box: none here either)
        RoundRect(cv, all, radius, Byte(w.bg), std::clamp(w.bg[3], 0.0f, 1.0f), Col{0, 0, 0}, 0.0f, 0.0f);
    }
    // the dark edge: the letters' coverage spread by `edge` pixels, under them
    const Col dark{0, 0, 0};
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < cw; ++x) {
            float m = 0.0f;
            for (int dy = -edge; dy <= edge && m < 1.0f; ++dy)
                for (int dx = -edge; dx <= edge; ++dx) {
                    const int xx = x + dx, yy = y + dy;
                    if (xx < 0 || yy < 0 || xx >= cw || yy >= h || dx * dx + dy * dy > edge * edge + edge) continue;
                    m = std::max(m, cover[(size_t)yy * cw + xx]);
                }
            if (m > 0.0f) cv.Over(x, y, dark, 0.85f * m);
        }
    const Col ink = Byte(w.rgb);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < cw; ++x)
            if (const float c = cover[(size_t)y * cw + x]; c > 0.0f) cv.Over(x, y, ink, c);
    *outW = cw;
    *outH = h;
    return true;
}

}  // namespace ownlabels
