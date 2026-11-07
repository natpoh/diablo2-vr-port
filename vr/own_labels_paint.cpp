// own_labels_paint.cpp - [hud] labels_own: one item label's picture (vr/own_labels_paint.h). Our own drawing.

#include "paint_kit.h"   // (windows.h, the canvas, the rounded box)
#include "own_labels_paint.h"
#include "game_ui.h"   // Covers: every letter in the face; PlateSprite: the game's plate pieces
#include "game_ui_files.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace ownlabels {
namespace {

using wheelpaint::kit::Canvas;
using wheelpaint::kit::Col;
using wheelpaint::kit::RoundRect;

// The name's coverage, w x h, drawn white on black by GDI with `font`, its left edge at x0, centred up and down.
bool Coverage(const std::wstring& text, HFONT font, int w, int h, int x0, std::vector<float>* cover, int extra = 0) {
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
    SetTextCharacterExtra(dc, extra);   // (the game's letter spacing: Want::gameWPerH)
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

int TextWidth(const std::wstring& text, HFONT font, int extra = 0) {
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return 0;
    HGDIOBJ old = SelectObject(dc, font);
    SetTextCharacterExtra(dc, extra);
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

// How far the first n letters reach (their advance): where the next letter starts.
int Advance(const std::wstring& text, size_t n, HFONT font, int extra = 0) {
    if (!n) return 0;
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return 0;
    HGDIOBJ old = SelectObject(dc, font);
    SetTextCharacterExtra(dc, extra);
    SIZE s = {};
    GetTextExtentPoint32W(dc, text.c_str(), (int)std::min(n, text.size()), &s);
    SelectObject(dc, old);
    DeleteDC(dc);
    return (int)s.cx;
}

Col Byte(const float c[3]) { return {std::clamp(c[0], 0.0f, 1.0f) * 255.0f, std::clamp(c[1], 0.0f, 1.0f) * 255.0f, std::clamp(c[2], 0.0f, 1.0f) * 255.0f}; }

}  // namespace

// A sheet's pixel at (u, v) in 0..1 of its first frame, bilinear, premultiplied RGBA 0..1.
void SampleSheet(const gameui::Sheet& s, float u, float v, float out[4]) {
    const float x = std::clamp(u * s.frameWidth - 0.5f, 0.0f, (float)(s.frameWidth - 1)), y = std::clamp(v * s.height - 0.5f, 0.0f, (float)(s.height - 1));
    const int x0 = (int)x, y0 = (int)y, x1 = std::min(x0 + 1, s.frameWidth - 1), y1 = std::min(y0 + 1, s.height - 1);
    const float fx = x - x0, fy = y - y0;
    for (int c = 0; c < 4; ++c) out[c] = 0.0f;
    const int xs[2] = {x0, x1}, ys[2] = {y0, y1};
    const float wx[2] = {1.0f - fx, fx}, wy[2] = {1.0f - fy, fy};
    for (int j = 0; j < 2; ++j)
        for (int i = 0; i < 2; ++i) {
            const uint8_t* p = s.At(0, xs[i], ys[j]);
            const float a = p[3] / 255.0f, k = wx[i] * wy[j];
            out[0] += p[0] / 255.0f * a * k; out[1] += p[1] / 255.0f * a * k; out[2] += p[2] / 255.0f * a * k; out[3] += a * k;
        }
}
uint32_t Pack(const float c[4]) {
    auto b = [](float v) { return (uint32_t)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f); };
    return b(c[0]) | (b(c[1]) << 8) | (b(c[2]) << 16) | (b(c[3]) << 24);
}
// A piece of our plate (Want::piece): the game's frame strip - left cap, the middle stretched, right cap, all scaled to the height - or its life bar;
// from the game's own sprites (gameui::PlateSprite), else a plain one of ours (a dark strip with a rim; a red bar).
bool PaintPiece(const Want& w, std::vector<uint32_t>* px, int* outW, int* outH) {
    const int h = std::clamp(w.heightPx, kMinPx, kMaxPx), cw = std::clamp(w.widthPx, h, kAtlasW - 2 * kGap);
    px->assign((size_t)cw * h, 0u);
    float c[4];
    if (w.piece == kPieceLife) {
        const gameui::Sheet* bar = gameui::PlateSprite(gameuifiles::kPlateConsole);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < cw; ++x) {
                if (bar) SampleSheet(*bar, (x + 0.5f) / cw, (y + 0.5f) / h, c);
                else { const float t = (y + 0.5f) / h; c[0] = 0.62f - 0.3f * t; c[1] = 0.02f; c[2] = 0.02f; c[3] = 1.0f; }
                (*px)[(size_t)y * cw + x] = Pack(c);
            }
    } else {
        const bool unique = w.piece == kPieceFrameUnique;
        const gameui::Sheet* L = gameui::PlateSprite(unique ? gameuifiles::kPlateUniqueLeft : gameuifiles::kPlateNormalLeft);
        const gameui::Sheet* M = gameui::PlateSprite(unique ? gameuifiles::kPlateUnique : gameuifiles::kPlateNormal);
        const gameui::Sheet* R = gameui::PlateSprite(unique ? gameuifiles::kPlateUniqueRight : gameuifiles::kPlateNormalRight);
        const int cap = std::max(1, (int)std::lround(h * (8.0f / 72.0f)));
        const float rim[3] = {unique ? 0.85f : 0.75f, unique ? 0.72f : 0.75f, unique ? 0.45f : 0.75f};
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < cw; ++x) {
                const float v = (y + 0.5f) / h;
                if (L && M && R) {
                    if (x < cap) SampleSheet(*L, (x + 0.5f) / cap, v, c);
                    else if (x >= cw - cap) SampleSheet(*R, (x - (cw - cap) + 0.5f) / cap, v, c);
                    else SampleSheet(*M, 0.5f, v, c);   // (the middle is the same all along: its middle column, stretched)
                } else {
                    const bool edge = y < std::max(1, h / 10) || y >= h - std::max(1, h / 10) || x < cap / 2 || x >= cw - cap / 2;
                    for (int k = 0; k < 3; ++k) c[k] = edge ? rim[k] : 0.02f;
                    c[3] = 0.95f;
                    for (int k = 0; k < 3; ++k) c[k] *= c[3];
                }
                (*px)[(size_t)y * cw + x] = Pack(c);
            }
    }
    *outW = cw;
    *outH = h;
    return true;
}

bool Paint(const Want& w, const wchar_t* face, std::vector<uint32_t>* px, int* outW, int* outH) {
    if (px && outW && outH && w.piece) return PaintPiece(w, px, outW, outH);
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
    int extra = 0;   // the game's letter spacing: our line as wide as the game measured it (Want::gameWPerH)
    if (w.gameWPerH > 0.0f && text.size() > 1)
        extra = ExtraSpacing(w.gameWPerH * h, (float)TextWidth(text, font), (int)text.size(), w.padSpaces, (float)TextWidth(L" ", font), fontPx * 0.6f);
    const int tw = TextWidth(text, font, extra) - extra;   // (GDI adds the spacing after the last letter too)
    const int cw = std::min(kAtlasW - 2 * kGap, std::max(h, tw + 2 * pad + 2 * edge));
    std::vector<float> cover, keyCover;
    const int x0 = std::max(pad + edge, (cw - tw) / 2);
    bool ok = tw > 0 && Coverage(text, font, cw, h, x0, &cover, extra);
    // the letters that stand for the game's button icon (Want::keys): each drawn alone where it stands in the line, to be inked in the game's green
    if (ok && w.keys) {
        keyCover.assign(cover.size(), 0.0f);
        std::vector<float> one;
        for (size_t i = 0; i < text.size() && i < 32; ++i)
            if ((w.keys >> i) & 1u && Coverage(text.substr(i, 1), font, cw, h, x0 + Advance(text, i, font, extra), &one))
                for (size_t p = 0; p < one.size(); ++p) keyCover[p] = std::max(keyCover[p], one[p]);
    }
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
    float green[3] = {0.0f, 1.0f, 0.0f};
    CodeColour(kKeyCode, green);
    const Col keyInk = Byte(green);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < cw; ++x)
            if (const float c = cover[(size_t)y * cw + x]; c > 0.0f) {
                const float k = keyCover.empty() ? 0.0f : std::clamp(keyCover[(size_t)y * cw + x] / c, 0.0f, 1.0f);
                cv.Over(x, y, k > 0.0f ? wheelpaint::kit::Mix(ink, keyInk, k) : ink, c);
            }
    *outW = cw;
    *outH = h;
    return true;
}

}  // namespace ownlabels
