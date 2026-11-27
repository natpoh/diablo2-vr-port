// keyboard_game_look.cpp - the VR keyboard's pictures in the game's own look (vr/keyboard_game_look.h).

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "game_ui.h"
#include "keyboard_game_look.h"
#include "vr_keyboard_unleashed.h"

namespace kblook {
namespace {

namespace kb = framevr::keyboard;

struct Col { float r, g, b; };
// The game's button text (vr/tiles_paint.cpp kGameGold / kGameGoldBright): gold, brighter on the lit key.
constexpr Col kGold{199.0f, 179.0f, 119.0f}, kGoldBright{240.0f, 220.0f, 160.0f};
// The option button's end caps in its own pixels (the frame's corners; the middle between them stretches), as vr/tiles_paint.cpp's Bar3 takes it.
constexpr float kCap = 30.0f;
// The slab's plain middle (inside its stone frame), as fractions of the sheet: the board's ground.
constexpr float kSlabL = 0.08f, kSlabT = 0.16f, kSlabR = 0.92f, kSlabB = 0.84f;
constexpr float kGroundAlpha = 0.94f;

// One frame's pixel at (x, y) in its own pixels, bilinear, clamped to the frame: premultiplied, 0..255.
void Sample(const gameui::Sheet& s, int f, float x, float y, float out[4]) {
    x = std::clamp(x - 0.5f, 0.0f, float(s.frameWidth - 1));
    y = std::clamp(y - 0.5f, 0.0f, float(s.height - 1));
    const int x0 = int(x), y0 = int(y), x1 = std::min(x0 + 1, s.frameWidth - 1), y1 = std::min(y0 + 1, s.height - 1);
    const float fx = x - float(x0), fy = y - float(y0);
    const float w[4] = {(1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy};
    const uint8_t* p[4] = {s.At(f, x0, y0), s.At(f, x1, y0), s.At(f, x0, y1), s.At(f, x1, y1)};
    out[0] = out[1] = out[2] = out[3] = 0.0f;
    for (int i = 0; i < 4; ++i) {
        const float a = float(p[i][3]) / 255.0f * w[i];
        out[0] += float(p[i][0]) * a; out[1] += float(p[i][1]) * a; out[2] += float(p[i][2]) * a; out[3] += 255.0f * a;
    }
}

// The source box (sx0..sx1, sy0..sy1 of frame f) averaged and laid over one texel (premultiplied "over").
void Lay(uint8_t* dst, const gameui::Sheet& s, int f, float sx0, float sx1, float sy0, float sy1, float alpha) {
    constexpr int N = 4;
    float acc[4] = {};
    for (int j = 0; j < N; ++j)
        for (int i = 0; i < N; ++i) {
            float c[4];
            Sample(s, f, sx0 + (float(i) + 0.5f) / N * (sx1 - sx0), sy0 + (float(j) + 0.5f) / N * (sy1 - sy0), c);
            for (int k = 0; k < 4; ++k) acc[k] += c[k];
        }
    const float n = float(N * N), a = acc[3] / n / 255.0f * alpha;
    for (int k = 0; k < 3; ++k) dst[k] = uint8_t(std::clamp(acc[k] / n * alpha + float(dst[k]) * (1.0f - a), 0.0f, 255.0f) + 0.5f);
    dst[3] = uint8_t(std::clamp(255.0f * a + float(dst[3]) * (1.0f - a), 0.0f, 255.0f) + 0.5f);
}

// The board's ground: the slab's plain dark stone over the whole picture.
void Ground(std::vector<uint8_t>& px, int W, int H, const gameui::Sheet& slab) {
    const float l = kSlabL * float(slab.frameWidth), r = kSlabR * float(slab.frameWidth), t = kSlabT * float(slab.height), b = kSlabB * float(slab.height);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            Lay(&px[(size_t(y) * W + x) * 4], slab, 0, l + (r - l) * float(x) / W, l + (r - l) * float(x + 1) / W, t + (b - t) * float(y) / H,
                t + (b - t) * float(y + 1) / H, kGroundAlpha);
}

// A key: the button's frame f over the key's rectangle - its height fitted, its end caps kept in shape, its middle stretched.
void Key(std::vector<uint8_t>& px, int W, const kb::Rect& r, const gameui::Sheet& s, int f) {
    if (r.w <= 0 || r.h <= 0) return;
    const float k = float(r.h) / float(s.height);                      // texels a source pixel
    const float cap = std::min(kCap * k, 0.5f * float(r.w));           // the cap on the key, texels
    const float capS = cap / k, fw = float(s.frameWidth);
    const auto srcX = [&](float dx) {
        if (dx <= cap) return dx / k;
        if (dx >= float(r.w) - cap) return fw - (float(r.w) - dx) / k;
        return capS + (dx - cap) / std::max(1e-3f, float(r.w) - 2.0f * cap) * (fw - 2.0f * capS);
    };
    for (int y = 0; y < r.h; ++y)
        for (int x = 0; x < r.w; ++x)
            Lay(&px[(size_t(r.y + y) * W + r.x + x) * 4], s, f, srcX(float(x)), srcX(float(x + 1)), float(y) / k, float(y + 1) / k, 1.0f);
}

// A label's coverage (0-255 a texel, w x h): the face as large as fits the key with an inset, centred. Empty: GDI failed.
std::vector<uint8_t> Coverage(const wchar_t* text, int w, int h, const wchar_t* face, int weight, int startPx) {
    std::vector<uint8_t> coverage;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = w; info.bmiHeader.biHeight = -h; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = dc ? CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
    if (!dc || !bitmap || !bits) {
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
        return coverage;
    }
    HGDIOBJ old = SelectObject(dc, bitmap);
    std::memset(bits, 0, size_t(w) * h * 4);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    bool fits = false;
    for (int px = startPx; !fits; px -= 2) {
        const bool last = px - 2 < h / 4;
        HFONT font = CreateFontW(px, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, face);
        HGDIOBJ before = SelectObject(dc, font);
        RECT measured{0, 0, 0, 0};
        DrawTextW(dc, text, -1, &measured, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
        fits = measured.right <= w - h * 6 / 16 || last;
        if (fits) {
            RECT bounds{0, 0, w, h};
            DrawTextW(dc, text, -1, &bounds, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        SelectObject(dc, before);
        DeleteObject(font);
    }
    GdiFlush();
    coverage.resize(size_t(w) * h);
    const uint8_t* b = static_cast<const uint8_t*>(bits);
    for (size_t i = 0; i < coverage.size(); ++i) coverage[i] = std::max({b[4 * i], b[4 * i + 1], b[4 * i + 2]});
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    return coverage;
}

// The label over the key: its shadow a texel right and two down, then the letters in the game's gold.
void Stamp(std::vector<uint8_t>& px, int W, const kb::Rect& r, const std::vector<uint8_t>& coverage, Col c) {
    const auto lay = [&](int ox, int oy, Col col, float strength) {
        for (int y = 0; y < r.h; ++y)
            for (int x = 0; x < r.w; ++x) {
                const int tx = x + ox, ty = y + oy;
                if (tx < 0 || ty < 0 || tx >= r.w || ty >= r.h) continue;
                const float a = float(coverage[size_t(y) * r.w + x]) / 255.0f * strength;
                if (a <= 0.0f) continue;
                uint8_t* p = &px[(size_t(r.y + ty) * W + r.x + tx) * 4];
                const float under = float(p[3]) / 255.0f;   // (premultiplied: the letters no more solid than the key under them)
                p[0] = uint8_t(std::clamp(float(p[0]) * (1.0f - a) + col.r * a * under, 0.0f, 255.0f) + 0.5f);
                p[1] = uint8_t(std::clamp(float(p[1]) * (1.0f - a) + col.g * a * under, 0.0f, 255.0f) + 0.5f);
                p[2] = uint8_t(std::clamp(float(p[2]) * (1.0f - a) + col.b * a * under, 0.0f, 255.0f) + 0.5f);
            }
    };
    lay(1, 2, Col{0.0f, 0.0f, 0.0f}, 0.75f);
    lay(0, 0, c, 1.0f);
}

}  // namespace

std::vector<uint8_t> PaintAtlas() {
    const gameui::Sheet* plain = gameui::SpriteAlone(gameui::Sprite::Option);
    const gameui::Sheet* chosen = gameui::SpriteAlone(gameui::Sprite::OptionChosen);
    const gameui::Sheet* slab = gameui::SpriteAlone(gameui::Sprite::Slab);
    if (!plain || !chosen || !slab || plain->frames < 2 || chosen->frames < 2) return {};
    // The game's fonts: Exocet (its titles and buttons); a label it has no letters for in Formal436 (its text), else ours.
    const wchar_t* title = gameui::TitleFaceAlone();
    const wchar_t* text = gameui::TextFaceAlone();
    const int W = kb::TexelWidth, H = kb::TexelHeight;
    std::vector<uint8_t> ground(size_t(W) * H * 4, 0);
    Ground(ground, W, H, *slab);
    std::vector<uint8_t> atlas(size_t(W) * H * 4 * 4, 0);
    for (int picture = 0; picture < 4; ++picture) {
        const bool lit = (picture & 1) != 0, capitals = picture >= 2;
        std::vector<uint8_t> board = ground;
        for (int i = 0; i < kb::KeyCount; ++i) {
            const kb::Rect r = kb::KeyTexelRect(i);
            const bool waiting = capitals && kb::Keys[i].action == kb::Action::Shift;   // the waiting Shift: the game's "chosen" button
            Key(board, W, r, waiting ? *chosen : *plain, lit ? 1 : 0);
            const std::wstring label = kb::Label(i, capitals);
            const bool word = label.size() > 1;
            // (a digit or a mark in the game's text font: Exocet's 0 is a struck circle and its 1 an I - an address is read off these keys)
            const bool letters = word || (label[0] >= L'a' && label[0] <= L'z') || (label[0] >= L'A' && label[0] <= L'Z');
            const wchar_t* face = letters && title && gameui::Covers(title, label) ? title : text && gameui::Covers(text, label) ? text : L"Georgia";
            const std::vector<uint8_t> cover = Coverage(label.c_str(), r.w, r.h, face, face == title ? FW_NORMAL : FW_BOLD, word ? r.h * 28 / 58 : letters ? r.h * 40 / 58 : r.h * 44 / 58);
            if (cover.empty()) return {};
            Stamp(board, W, r, cover, lit || waiting ? kGoldBright : kGold);
        }
        std::memcpy(&atlas[size_t(picture) * W * H * 4], board.data(), board.size());
    }
    return atlas;
}

}  // namespace kblook
