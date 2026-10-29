// tiles_paint.cpp - the skill panel's tiles look (vr/tiles_paint.h).
//
// Drawn after the game's own controller skill-rebind dialog (the user's reference): a heavy dark-metal frame with a recessed title
// plate cut with silver small capitals, a near-black panel, two rows of six square plates - copper-brown scuffed metal with a rivet in
// each corner and a dark inner bevel; a slot that holds a skill is red (a red glowing plate, a pale pinkish-red picture), an empty
// one stays copper - the pad's button mark under each (green A, blue X, red B, yellow Y discs; RB and RT chips with a bite out of a
// corner), an LT chip with swap arrows at the left end of the second row (the left trigger's row), the state in red small capitals
// under it all. A gold frame is the slot the trigger casts, a bright rim and a glow the tile under the beam.
//
// Everything is drawn by our own code, by the pixel (bevels, rivets, grain, scuffs, the red and the glyphs); no picture of the game's
// art is in the plugin - only the skills' own icons are laid in, from the cache the player makes from their own game (vr/wheel_icons.h).
// The window's frame is built once and cached; every picture starts as a copy of it. The tile rows, the grid and the buttons are laid
// over from the View at the places vr/tiles_geom.h gives (the hit test reads the same numbers). The drawing primitives are shared with
// the wheel's painter (vr/paint_kit.h). Every colour, shape and number here is ours.

#include "tiles_paint.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <string>

#include "hset_scene.h"
#include "paint_kit.h"
#include "tiles_geom.h"
#include "tiles_labels.h"
#include "ui_text.h"   // the window's words in the player's language ([hud] headset_language)
#include "wheel_icons.h"

namespace tilespaint {
namespace {

using namespace wheelpaint::kit;
using wheel::Cell;
using wheel::Page;
using wheel::Skill;
using wheel::View;
using tiles::kH;
using tiles::kW;

constexpr Col kSilver{228, 230, 236}, kSilverDim{148, 150, 160}, kRed{208, 46, 36}, kRedBright{240, 84, 62}, kGold{204, 160, 78},
    kGoldBright{248, 210, 116}, kInk{14, 14, 16}, kBlack{6, 6, 7};

inline Col Scale(Col c, float k) { return {std::min(255.0f, std::max(0.0f, c.r * k)), std::min(255.0f, std::max(0.0f, c.g * k)), std::min(255.0f, std::max(0.0f, c.b * k))}; }
inline Col Grey(Col c, float t) {
    const float g = (c.r + c.g + c.b) / 3.0f;
    return Mix(c, Col{g, g, g}, t);
}
inline float Smooth(float t) { t = Clamp01(t); return t * t * (3.0f - 2.0f * t); }
inline float Sgn(float v) { return v < 0.0f ? -1.0f : 1.0f; }

// A hash of a pixel in 0..1 (the grain, the scuffs).
inline float Hash(int x, int y) {
    uint32_t h = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return float((h ^ (h >> 16)) & 0xffffu) / 65535.0f;
}

inline uint32_t Pack(Col c, float a) {   // premultiplied
    return uint32_t(std::min(255.0f, std::max(0.0f, c.r) * a + 0.5f)) | uint32_t(std::min(255.0f, std::max(0.0f, c.g) * a + 0.5f)) << 8 |
           uint32_t(std::min(255.0f, std::max(0.0f, c.b) * a + 0.5f)) << 16 | uint32_t(std::min(255.0f, 255.0f * a + 0.5f)) << 24;
}

// -- the frame: a heavy dark-metal window with cut corners -----------------------------------------------------------------------

constexpr float kInset = 3.0f;       // the window's edge from the picture's edge
constexpr float kChamfer = 26.0f;    // the cut corners

struct Octagon {
    float cx = kW * 0.5f, cy = kH * 0.5f, hw = kW * 0.5f - kInset, hh = kH * 0.5f - kInset, c = kChamfer;
    // Signed distance (negative inside) and the outward direction there.
    float Sd(float x, float y, float* nx, float* ny) const {
        const float px = x - cx, py = y - cy, ax = std::fabs(px), ay = std::fabs(py);
        const float dx = ax - hw, dy = ay - hh;
        const float box = (dx > 0.0f && dy > 0.0f) ? std::hypot(dx, dy) : std::max(dx, dy);
        const float cut = (ax + ay - (hw + hh - c)) * 0.70710678f;
        if (cut > box) { *nx = Sgn(px) * 0.70710678f; *ny = Sgn(py) * 0.70710678f; return cut; }
        if (dx > dy) { *nx = Sgn(px); *ny = 0.0f; } else { *nx = 0.0f; *ny = Sgn(py); }
        return box;
    }
};

inline Col Band(Col under, Col over, float d, float a, float b) {
    return Mix(under, over, Clamp01(d - a + 0.5f) * Clamp01(b - d + 0.5f));
}

void Bar(Canvas& cv, float l, float t, float r, float b, Col c, float a) {
    for (int y = std::max(0, int(t)); y < std::min(cv.h, int(std::ceil(b))); ++y)
        for (int x = std::max(0, int(l)); x < std::min(cv.w, int(std::ceil(r))); ++x) cv.Over(x, y, c, a);
}

// A line segment with round ends, anti-aliased.
void Seg(Canvas& cv, float x0, float y0, float x1, float y1, float width, Col c, float alpha = 1.0f) {
    const float pad = width * 0.5f + 1.5f;
    const float dx = x1 - x0, dy = y1 - y0, len2 = std::max(1e-6f, dx * dx + dy * dy);
    for (int y = std::max(0, int(std::min(y0, y1) - pad)); y < std::min(cv.h, int(std::max(y0, y1) + pad) + 1); ++y)
        for (int x = std::max(0, int(std::min(x0, x1) - pad)); x < std::min(cv.w, int(std::max(x0, x1) + pad) + 1); ++x) {
            const float t = Clamp01(((x + 0.5f - x0) * dx + (y + 0.5f - y0) * dy) / len2);
            const float d = std::hypot(x + 0.5f - (x0 + dx * t), y + 0.5f - (y0 + dy * t));
            cv.Over(x, y, c, alpha * Clamp01(width * 0.5f + 0.5f - d));
        }
}

// A small domed rivet: a dark rim, a lit dome with a highlight.
void Rivet(Canvas& cv, float cx, float cy, float radius, Col dome, Col rim) {
    for (int y = int(cy - radius - 2); y <= int(cy + radius + 2); ++y)
        for (int x = int(cx - radius - 2); x <= int(cx + radius + 2); ++x) {
            const float dx = x + 0.5f - cx, dy = y + 0.5f - cy, r = std::hypot(dx, dy);
            if (r > radius + 1.5f) continue;
            cv.Over(x, y, rim, 0.9f * Clamp01(radius + 1.0f - r + 0.5f));   // (the dark ring and its soft shadow)
            const float k = Clamp01(1.0f - r / (radius * 0.86f));
            const float hl = Clamp01(1.0f - std::hypot(dx + radius * 0.32f, dy + radius * 0.34f) / (radius * 0.62f));
            Col c = Mix(Scale(dome, 0.55f), Scale(dome, 1.15f), k);
            c = Mix(c, Col{255, 244, 224}, hl * 0.6f);
            cv.Over(x, y, c, Clamp01(radius * 0.86f - r + 0.5f));
        }
}

// The window without any content, premultiplied. Made once.
const std::vector<uint32_t>& Frame() {
    static const std::vector<uint32_t> frame = [] {
        std::vector<uint32_t> px((size_t)kW * kH, 0u);
        const Octagon oct;
        for (int y = 0; y < kH; ++y)
            for (int x = 0; x < kW; ++x) {
                float nx, ny;
                const float sd = oct.Sd(x + 0.5f, y + 0.5f, &nx, &ny), d = -sd;   // d: how far inside the edge
                const float cover = Clamp01(d + 0.5f);
                if (cover <= 0.0f) continue;
                const float light = -(nx * 0.58f + ny * 0.82f);   // the light comes from the upper left: + faces it
                const float grain = 0.55f * Hash(x >> 1, y >> 1) + 0.45f * Hash(x, y);
                const float brushed = Hash(x >> 5, y) - 0.5f;     // long faint horizontal streaks, as brushed metal
                const float g = (grain - 0.5f) * 8.0f + brushed * 5.0f;
                const float lift = 1.0f - float(y) / kH * 0.25f;
                Col body = Scale(Col{47.0f, 48.0f, 53.0f}, lift);
                body = {body.r + g, body.g + g, body.b + g * 1.1f};
                Col steel = Scale(Col{128.0f, 130.0f, 138.0f}, 1.0f + 0.55f * light);   // the lit and shaded bevel of the rim
                steel = {steel.r + g, steel.g + g, steel.b + g};
                Col c = body;
                c = Band(c, Col{8, 8, 9}, d, 0.0f, 2.5f);
                c = Band(c, steel, d, 2.5f, 7.0f);
                c = Band(c, Scale(body, 1.0f + 0.10f * light), d, 7.0f, 25.0f);
                c = Band(c, Col{11, 11, 13}, d, 25.0f, 28.0f);
                c = Band(c, Scale(Col{104, 106, 116}, 1.0f + 0.35f * light), d, 28.0f, 30.5f);
                c = Band(c, Col{9, 9, 11}, d, 30.5f, 33.0f);
                px[(size_t)y * kW + x] = Pack(c, 0.985f * cover);
            }
        Canvas cv{px, kW, kH};
        // the title plate, recessed in the frame: dark metal with a bevel (shadow above and left, light below and right)
        const tiles::PRect tp = tiles::kTitlePlate;
        RoundRect(cv, tp, 9.0f, Col{26, 26, 30}, 1.0f, Col{96, 98, 108}, 1.0f, 2.5f);
        Bar(cv, tp.l + 4.0f, tp.t + 4.0f, tp.r - 4.0f, tp.t + 9.0f, Col{0, 0, 0}, 0.38f);
        Bar(cv, tp.l + 4.0f, tp.b - 7.0f, tp.r - 4.0f, tp.b - 4.0f, Col{200, 204, 214}, 0.07f);
        // the near-black panel under it
        const tiles::PRect pn = tiles::kPanel;
        RoundRect(cv, pn, 18.0f, Col{5, 5, 6}, 0.985f, Col{84, 86, 96}, 1.0f, 3.5f);
        Bar(cv, pn.l + 6.0f, pn.t + 6.0f, pn.r - 6.0f, pn.t + 13.0f, Col{0, 0, 0}, 0.55f);
        Bar(cv, pn.l + 6.0f, pn.b - 9.0f, pn.r - 6.0f, pn.b - 5.0f, Col{200, 204, 214}, 0.06f);
        // the little corner brackets: a crossed pair of bars with a rivet, at the plate's two ends and the panel's two bottom corners
        auto bracket = [&](float cx, float cy) {
            Seg(cv, cx - 15.0f, cy - 15.0f, cx + 15.0f, cy + 15.0f, 5.5f, Col{70, 72, 80}, 1.0f);
            Seg(cv, cx - 15.0f, cy + 15.0f, cx + 15.0f, cy - 15.0f, 5.5f, Col{70, 72, 80}, 1.0f);
            Seg(cv, cx - 14.0f, cy - 15.0f, cx + 14.0f, cy + 13.0f, 2.0f, Col{136, 138, 148}, 0.8f);
            Rivet(cv, cx, cy, 5.5f, Col{150, 152, 162}, Col{10, 10, 12});
        };
        bracket(tp.l + 26.0f, tp.t + 22.0f);
        bracket(tp.r - 26.0f, tp.t + 22.0f);
        bracket(pn.l + 30.0f, pn.b - 30.0f);
        bracket(pn.r - 30.0f, pn.b - 30.0f);
        return px;
    }();
    return frame;
}

// -- small capitals: the game's titles are capitals of two sizes, letter-spaced ------------------------------------------------------
//
// A letter that was typed as a capital is full size, a lower-case one is drawn as a capital of 80 % of it (so "Press button to rebind
// skill" is the game's "PRESS BUTTON..." with big first letters), each followed by `tracking` pixels. GDI draws the letters one by one on
// a common baseline into a coverage mask.

constexpr float kSmallCap = 0.80f;

std::wstring Wide(const char* utf8) {
    return lang::FromUtf8(utf8 ? utf8 : "");   // (any length: a translated line is longer than the English)
}

// The width the line takes (and, when `draw`, draws it with its left at x and `box.t` .. `box.b` for the line's height).
float SmallCaps(Canvas& cv, Fonts& fonts, const char* utf8, float x, Rect box, int size, float tracking, Col color, float alpha, bool draw) {
    const std::wstring text = Wide(utf8);
    if (text.empty()) return 0.0f;
    const int smallSize = std::max(8, int(size * kSmallCap + 0.5f));
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return 0.0f;
    HGDIOBJ oldFont = GetCurrentObject(dc, OBJ_FONT);
    std::vector<float> adv(text.size());
    std::vector<wchar_t> glyph(text.size());
    float total = 0.0f;
    for (size_t i = 0; i < text.size(); ++i) {
        const wchar_t c = text[i];
        const bool lower = IsCharLowerW(c) != FALSE;   // (a-z as before; an accented letter, Cyrillic too)
        glyph[i] = lower ? (wchar_t)towupper(c) : c;
        SelectObject(dc, fonts.Get(lower ? smallSize : size, true));
        SIZE sz = {};
        GetTextExtentPoint32W(dc, &glyph[i], 1, &sz);
        adv[i] = float(sz.cx) + (i + 1 < text.size() ? tracking : 0.0f);
        total += adv[i];
    }
    if (!draw) {
        SelectObject(dc, oldFont);
        DeleteDC(dc);
        return total;
    }
    const int bw = int(std::ceil(total)) + 12, bh = std::max(1, int(box.b - box.t));
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
        SelectObject(dc, oldFont);
        DeleteDC(dc);
        return total;
    }
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    std::memset(bits, 0, (size_t)bw * bh * 4);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    SetTextAlign(dc, TA_BASELINE | TA_LEFT);
    const int baseline = int(0.5f * (bh + 0.70f * size));   // (the capitals' height is about 0.7 of the size)
    float pen = 4.0f;
    for (size_t i = 0; i < text.size(); ++i) {
        const bool lower = IsCharLowerW(text[i]) != FALSE;
        SelectObject(dc, fonts.Get(lower ? smallSize : size, true));
        TextOutW(dc, int(pen + 0.5f), baseline, &glyph[i], 1);
        pen += adv[i];
    }
    GdiFlush();
    const uint8_t* p = static_cast<const uint8_t*>(bits);
    const int left = int(x) - 4, top = int(box.t);
    for (int yy = 0; yy < bh; ++yy)
        for (int xx = 0; xx < bw; ++xx) {
            const uint8_t* s = p + ((size_t)yy * bw + xx) * 4;
            const float cover = (s[0] + s[1] + s[2]) / (3.0f * 255.0f);
            if (cover > 0.0f) cv.Over(left + xx, top + yy, color, cover * alpha);
        }
    SelectObject(dc, oldBmp);
    SelectObject(dc, oldFont);
    DeleteObject(bmp);
    DeleteDC(dc);
    return total;
}
float CapsWidth(Fonts& fonts, const char* text, int size, float tracking) {
    std::vector<uint32_t> none;
    Canvas cv{none, 0, 0};
    return SmallCaps(cv, fonts, text, 0, Rect{0, 0, 0, 0}, size, tracking, Col{}, 0.0f, false);
}
// Centred on cx, with a dark shadow.
void CapsCentred(Canvas& cv, Fonts& fonts, const char* text, float cx, Rect box, int size, float tracking, Col color, bool shadow = true) {
    const float w = CapsWidth(fonts, text, size, tracking), x = cx - 0.5f * w;
    if (shadow) SmallCaps(cv, fonts, text, x + 2.0f, Rect{box.l, box.t + 2.0f, box.r, box.b + 2.0f}, size, tracking, Col{0, 0, 0}, 0.8f, true);
    SmallCaps(cv, fonts, text, x, box, size, tracking, color, 1.0f, true);
}

// -- the plates: scuffed copper, or the red of a slot that holds a skill --------------------------------------------------------------

struct PlateLook {
    bool red = false;     // a slot that holds a skill (the game's red glowing plate)
    bool dim = false;     // ... of a skill the player does not have now
    bool hot = false;     // under the beam
    bool active = false;  // the slot the trigger casts
    bool dead = false;    // a slot this cannot place (not known): a dull plate
};

void DrawPlate(Canvas& cv, Rect r, const PlateLook& k) {
    const float w = r.r - r.l, h = r.b - r.t;
    for (int y = std::max(0, int(r.t) - 1); y < std::min(cv.h, int(r.b) + 1); ++y)
        for (int x = std::max(0, int(r.l) - 1); x < std::min(cv.w, int(r.r) + 1); ++x) {
            const float px = x + 0.5f, py = y + 0.5f;
            const float dl = px - r.l, dr = r.r - px, dt = py - r.t, db = r.b - py;
            const float de = std::min(std::min(dl, dr), std::min(dt, db));   // pixels to the nearest edge
            const float cover = Clamp01(de + 0.5f);
            if (cover <= 0.0f) continue;
            const float u = dl / w, v = dt / h;
            const float grain = 0.5f * Hash(x / 5, y / 5) + 0.3f * Hash(x / 2, y / 2) + 0.2f * Hash(x, y);
            float scuff = 0.0f;
            if (Hash((x + y * 3) / 11, y / 37) > 0.92f) scuff = 16.0f;                // a long light scratch
            if (Hash(x / 3, (y - x) / 29) > 0.95f) scuff -= 12.0f;                     // a dark one
            const float g = (grain - 0.5f) * 30.0f + scuff;
            Col face;
            if (k.red) {   // the red glowing plate: bright in the middle, darker toward the rim
                const float rad = std::hypot(u - 0.5f, v - 0.5f) * 1.6f;
                face = Mix(Col{214.0f, 36.0f, 30.0f}, Col{104.0f, 10.0f, 10.0f}, Smooth(rad));
                face = {face.r + g * 0.7f, face.g + g * 0.25f, face.b + g * 0.25f};
            } else {       // copper-brown, lighter at the top
                face = Mix(Col{134.0f, 86.0f, 54.0f}, Col{92.0f, 58.0f, 38.0f}, Smooth(v));
                if (k.dead) face = Mix(face, Col{54.0f, 42.0f, 36.0f}, 0.6f);
                face = {face.r + g, face.g + g * 0.8f, face.b + g * 0.6f};
            }
            if (k.dim) face = Grey(Scale(face, 0.52f), 0.35f);
            if (k.hot) face = Scale(face, 1.10f);
            // the bevel: light along the top and the left, shade along the bottom and the right, then a dark inner line
            Col c = face;
            const bool lit = std::min(dl, dt) < std::min(dr, db);
            if (de < 6.5f) c = Scale(face, lit ? 1.38f : 0.62f);
            if (de >= 6.5f && de < 9.5f) c = Scale(face, 0.34f);                          // the dark inner bevel
            if (de < 1.8f) c = Col{16, 8, 6};                                             // the outline
            cv.Over(x, y, c, cover);
        }
    // a rivet in each corner
    const Col dome = k.red ? Col{176, 46, 38} : Col{156, 106, 64}, rim = k.red ? Col{42, 6, 6} : Col{26, 13, 8};
    const float o = 17.0f, rad = std::max(5.0f, 0.04f * w);
    for (float fx : {r.l + o, r.r - o})
        for (float fy : {r.t + o, r.b - o}) Rivet(cv, fx, fy, rad, k.dim ? Grey(Scale(dome, 0.6f), 0.4f) : dome, rim);
}

// The rim under the beam and the gold frame of the picked slot.
void DrawTileMarks(Canvas& cv, Rect r, const PlateLook& k) {
    if (k.hot) {
        RoundRect(cv, Rect{r.l - 7.0f, r.t - 7.0f, r.r + 7.0f, r.b + 7.0f}, 10.0f, kGoldBright, 0.20f, Col{0, 0, 0}, 0.0f, 0.0f);   // the glow
        RoundRect(cv, Rect{r.l - 1.0f, r.t - 1.0f, r.r + 1.0f, r.b + 1.0f}, 4.0f, kGold, 0.0f, Col{255, 238, 178}, 1.0f, 3.5f);
    }
    if (k.active) {
        RoundRect(cv, Rect{r.l - 3.0f, r.t - 3.0f, r.r + 3.0f, r.b + 3.0f}, 5.0f, kGold, 0.0f, kGoldBright, 1.0f, 6.0f);
        RoundRect(cv, Rect{r.l + 3.0f, r.t + 3.0f, r.r - 3.0f, r.b - 3.0f}, 3.0f, kGold, 0.0f, Col{255, 226, 140}, 0.75f, 2.0f);
    }
}

// The first letters of a name's words (up to three; of a single word, its first two), UTF-8 aware: the stand-in for an icon when the
// cache has none.
void Initials(const char* name, char* out, int n) {
    auto isSep = [](unsigned char c) { return c == ' ' || c == '-' || c == '/' || c == '\''; };
    // the byte after the code point at p (never past the terminator, whatever a broken sequence says)
    auto next = [](const unsigned char* p) {
        const int len = *p < 0x80 ? 1 : *p < 0xE0 ? 2 : *p < 0xF0 ? 3 : 4;
        for (int i = 0; i < len && *p; ++i) ++p;
        return p;
    };
    const unsigned char* word[3] = {};
    int words = 0;
    bool start = true;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(name); p && *p;) {
        if (isSep(*p)) { start = true; ++p; continue; }
        if (start && words < 3) word[words++] = p;
        start = false;
        p = next(p);
    }
    int o = 0;
    auto put = [&](const unsigned char* p) {
        const unsigned char* e = next(p);
        for (; p < e && o < n - 1; ++p) out[o++] = char(e - p == 1 && *p >= 'a' && *p <= 'z' ? *p - 32 : *p);
    };
    for (int w = 0; w < words; ++w) put(word[w]);
    if (words == 1) {   // one word: its second letter too
        const unsigned char* second = next(word[0]);
        if (*second && !isSep(*second)) put(second);
    }
    out[o] = 0;
}

// One tile: the plate, the skill's picture on it (the icon, or the initials), the marks.
void DrawTile(Canvas& cv, Fonts& fonts, Rect r, const PlateLook& k, const Skill* skill, bool plus) {
    DrawPlate(cv, r, k);
    if (skill && skill->known && skill->id >= 0) {
        const auto icon = wheelicons::Get(skill->id);
        if (icon) {
            Icon(cv, icon, r, k.red ? IconTone::Red : IconTone::Normal);   // (the game's icon is the plate and the picture: it fills the tile)
            if (k.dim) Bar(cv, r.l, r.t, r.r, r.b, Col{0, 0, 0}, 0.42f);
            if (k.hot) Bar(cv, r.l, r.t, r.r, r.b, Col{255, 220, 150}, 0.06f);
        } else {
            char mono[16];
            Initials(skill->name, mono, sizeof mono);
            Text(cv, fonts, mono, Rect{r.l + 14.0f, r.t + 24.0f, r.r - 14.0f, r.b - 24.0f}, int(0.30f * (r.r - r.l)), true, k.red ? Col{255, 170, 150} : Col{214, 218, 226}, k.dim ? 0.55f : 1.0f);
        }
    } else if (plus) {
        Text(cv, fonts, "+", Rect{r.l, r.t, r.r, r.b}, int(0.42f * (r.r - r.l)), false, Col{60, 34, 22}, 0.9f);
    }
    DrawTileMarks(cv, r, k);
}

// -- the buttons' marks -----------------------------------------------------------------------------------------------------------------

// A pad glyph centred in `area`: the game's own - a dark disc ringed and lettered in the button's colour (A green, X blue, B red,
// Y yellow), or a light chip with a bite out of its lower right corner (RB, RT).
void DrawGlyph(Canvas& cv, Fonts& fonts, const tiles::Glyph& g, Rect area) {
    const float cx = 0.5f * (area.l + area.r), cy = 0.5f * (area.t + area.b);
    if (g.kind == tiles::GlyphKind::Chip) {
        const Rect chip{cx - 34.0f, cy - 17.0f, cx + 34.0f, cy + 17.0f};
        RoundRect(cv, chip, 11.0f, Col{226, 228, 232}, 0.99f, Col{20, 20, 24}, 1.0f, 2.0f);
        RoundRect(cv, Rect{chip.l + 3.0f, chip.t + 3.0f, chip.r - 3.0f, chip.t + 12.0f}, 6.0f, Col{255, 255, 255}, 0.45f, Col{0, 0, 0}, 0.0f, 0.0f);
        Text(cv, fonts, g.text, chip, 21, true, Col{26, 26, 30});
        for (int y = int(chip.b) - 14; y < int(chip.b) + 1; ++y)   // the bite
            for (int x = int(chip.r) - 14; x < int(chip.r) + 1; ++x) {
                const float s = float(x - (int(chip.r) - 14)) + float(y - (int(chip.b) - 14));
                cv.Over(x, y, kBlack, Clamp01(s - 14.0f + 0.5f));
            }
        return;
    }
    Col ring{34, 190, 60};
    if (g.colour == tiles::GlyphColour::Blue) ring = {50, 124, 240};
    else if (g.colour == tiles::GlyphColour::Red) ring = {222, 40, 34};
    else if (g.colour == tiles::GlyphColour::Yellow) ring = {240, 200, 36};
    const Rect disc{cx - 21.0f, cy - 21.0f, cx + 21.0f, cy + 21.0f};
    RoundRect(cv, disc, 21.0f, Col{8, 8, 10}, 0.99f, ring, 1.0f, 3.2f);
    Text(cv, fonts, g.text, disc, 26, true, ring);
}

// The LT chip at the left end of the left trigger's row, with the swap arrows under it.
void DrawLtMark(Canvas& cv, Fonts& fonts) {
    const Rect chip = tiles::LtChipRect();
    RoundRect(cv, chip, 12.0f, Col{226, 228, 232}, 0.99f, Col{20, 20, 24}, 1.0f, 2.0f);
    RoundRect(cv, Rect{chip.l + 3.0f, chip.t + 3.0f, chip.r - 3.0f, chip.t + 13.0f}, 7.0f, Col{255, 255, 255}, 0.45f, Col{0, 0, 0}, 0.0f, 0.0f);
    Text(cv, fonts, "LT", chip, 26, true, Col{26, 26, 30});
    for (int y = int(chip.t); y < int(chip.t) + 16; ++y)   // the bite, top right
        for (int x = int(chip.r) - 16; x < int(chip.r) + 1; ++x) {
            const float s = float(x - (int(chip.r) - 16)) - float(y - int(chip.t)) + 16.0f;
            cv.Over(x, y, kBlack, Clamp01(s - 16.0f + 0.5f));
        }
    // the swap arrows: two half circles, each ending in an arrow head, in worn gold
    const Rect a = tiles::LtArrowsRect();
    const float cx = 0.5f * (a.l + a.r), cy = 0.5f * (a.t + a.b), rad = 15.0f;
    const Col gold{176, 130, 58};
    auto arc = [&](float from, float to) {
        float px = 0, py = 0;
        for (int i = 0; i <= 10; ++i) {
            const float t = (from + (to - from) * i / 10.0f) * 3.14159265f / 180.0f;
            const float x = cx + rad * std::cos(t), y = cy + rad * std::sin(t);
            if (i) Seg(cv, px, py, x, y, 3.4f, gold, 1.0f);
            px = x; py = y;
        }
        // an arrow head at the end, along the tangent
        const float t = to * 3.14159265f / 180.0f, dir = to > from ? 1.0f : -1.0f;
        const float tx = -std::sin(t) * dir, ty = std::cos(t) * dir, nx = std::cos(t), ny = std::sin(t);
        Seg(cv, px, py, px - tx * 9.0f + nx * 6.0f, py - ty * 9.0f + ny * 6.0f, 3.2f, gold, 1.0f);
        Seg(cv, px, py, px - tx * 9.0f - nx * 6.0f, py - ty * 9.0f - ny * 6.0f, 3.2f, gold, 1.0f);
    };
    arc(205.0f, 335.0f);    // over the top, to the right
    arc(25.0f, 155.0f);     // under the bottom, to the left
}

// -- the page -------------------------------------------------------------------------------------------------------------------------------

const char* ButtonText(Cell c) {
    switch (c) {
    case wheel::kEdit: return uitext::Tr("vr.tiles.edit", "Edit skills");
    case wheel::kClose: return uitext::Tr("vr.tiles.close", "Close");
    case wheel::kBack: return uitext::Tr("vr.tiles.back", "Back");
    case wheel::kPrev: return uitext::Tr("vr.tiles.previous", "Previous");
    case wheel::kNext: return uitext::Tr("vr.tiles.next", "Next");
    case wheel::kCentre: return uitext::Tr("vr.tiles.clear_slot", "Clear slot");
    default: return "";
    }
}

void DrawButtons(Canvas& cv, Fonts& fonts, const View& v) {
    for (int i = 0; i < tiles::ButtonCount(v.page); ++i) {
        const Cell c = tiles::ButtonCell(v.page, i);
        bool enabled = true;
        if (c == wheel::kEdit) enabled = v.editable;
        if (c == wheel::kPrev) enabled = v.editPage > 0;
        if (c == wheel::kNext) enabled = v.editPage + 1 < v.editPages;
        const bool hot = v.hover == c && enabled;
        const Rect r = tiles::ButtonRect(v.page, i);
        if (hot) RoundRect(cv, Rect{r.l - 6.0f, r.t - 6.0f, r.r + 6.0f, r.b + 6.0f}, 12.0f, kGoldBright, 0.20f, Col{0, 0, 0}, 0.0f, 0.0f);
        RoundRect(cv, r, 8.0f, hot ? Col{58, 46, 28} : Col{26, 26, 30}, 0.99f, hot ? kGoldBright : (enabled ? Col{104, 106, 116} : Col{50, 50, 56}), 1.0f, hot ? 3.5f : 2.5f);
        Bar(cv, r.l + 4.0f, r.t + 4.0f, r.r - 4.0f, r.t + 9.0f, Col{255, 255, 255}, enabled ? 0.06f : 0.02f);
        Bar(cv, r.l + 4.0f, r.b - 8.0f, r.r - 4.0f, r.b - 4.0f, Col{0, 0, 0}, 0.30f);
        CapsCentred(cv, fonts, ButtonText(c), 0.5f * (r.l + r.r), Rect{r.l, r.t, r.r, r.b}, 32, 2.0f, enabled ? (hot ? kGoldBright : kSilver) : Col{92, 92, 100});
    }
}

// The two tabs in the title plate: the chosen one gold on a warm plate, the one under the beam lit.
void DrawTabs(Canvas& cv, Fonts& fonts, const View& v) {
    const char* const names[tiles::kTabCount] = {uitext::Tr("vr.tiles.tab_skills", "Skills"), uitext::Tr("vr.tiles.tab_settings", "Settings")};
    for (int i = 0; i < tiles::kTabCount; ++i) {
        const Cell c = tiles::TabCell(i);
        const bool on = (i == 0) == (v.tab == wheel::Tab::Skills);
        const bool hot = v.hover == c && !on;
        const Rect r = tiles::TabRect(i);
        if (hot) RoundRect(cv, Rect{r.l - 6.0f, r.t - 6.0f, r.r + 6.0f, r.b + 6.0f}, 12.0f, kGoldBright, 0.20f, Col{0, 0, 0}, 0.0f, 0.0f);
        RoundRect(cv, r, 9.0f, on ? Col{64, 50, 28} : hot ? Col{52, 42, 26} : Col{22, 22, 26}, 0.99f, on ? kGoldBright : hot ? kGold : Col{96, 98, 108}, 1.0f,
                  on ? 3.5f : 2.5f);
        Bar(cv, r.l + 4.0f, r.t + 4.0f, r.r - 4.0f, r.t + 9.0f, Col{255, 255, 255}, on ? 0.10f : 0.05f);
        Bar(cv, r.l + 4.0f, r.b - 8.0f, r.r - 4.0f, r.b - 4.0f, Col{0, 0, 0}, 0.30f);
        CapsCentred(cv, fonts, names[i], 0.5f * (r.l + r.r), r, 34, 3.0f, on ? kGoldBright : hot ? kGold : kSilverDim);
    }
}

// The flip hints (the window flips like pages): an arrow at each edge where there is a place back / on - lit under the beam - and the places as dots at
// the foot, the one the window stands at gold.
void DrawFlipHints(Canvas& cv, const View& v) {
    const int places = wheel::kFlipPlaces;
    for (int i = 0; i < places; ++i) {
        const float x = tiles::DotX(i, places), y = tiles::kDotY, r = i == v.flipPlace ? 8.0f : 6.0f;
        const bool on = i == v.flipPlace;
        RoundRect(cv, Rect{x - r - 2.0f, y - r - 2.0f, x + r + 2.0f, y + r + 2.0f}, r + 2.0f, Col{8, 8, 10}, 0.85f, Col{0, 0, 0}, 0.0f, 0.0f);
        RoundRect(cv, Rect{x - r, y - r, x + r, y + r}, r, on ? kGoldBright : Col{66, 68, 76}, 0.98f, on ? Col{255, 238, 178} : Col{104, 106, 116}, 1.0f, 1.6f);
    }
    for (int i = 0; i < 2; ++i) {
        if (i == 0 ? v.flipPlace <= 0 : v.flipPlace >= places - 1) continue;   // nothing that way
        const bool hot = v.hover == tiles::FlipCell(i);
        const Rect z = tiles::FlipRect(i);
        const float cx = 0.5f * (z.l + z.r), cy = 0.5f * (z.t + z.b), dir = i == 0 ? -1.0f : 1.0f;
        const float tipX = cx + dir * 11.0f, armX = cx - dir * 11.0f, h = 34.0f;
        if (hot) RoundRect(cv, Rect{z.l + 6.0f, cy - 64.0f, z.r - 6.0f, cy + 64.0f}, 14.0f, kGoldBright, 0.14f, Col{0, 0, 0}, 0.0f, 0.0f);
        const Col c = hot ? kGoldBright : kGold;
        Seg(cv, tipX, cy, armX, cy - h, 7.0f, Col{0, 0, 0}, 0.55f);
        Seg(cv, tipX, cy, armX, cy + h, 7.0f, Col{0, 0, 0}, 0.55f);
        Seg(cv, tipX, cy, armX, cy - h, 4.6f, c, hot ? 1.0f : 0.85f);
        Seg(cv, tipX, cy, armX, cy + h, 4.6f, c, hot ? 1.0f : 0.85f);
    }
}

void DrawTitle(Canvas& cv, Fonts& fonts, const View& v) {
    const tiles::PRect tp = tiles::kTitlePlate;
    const Rect line{tp.l, tp.t, tp.r, tp.b};
    const float cx = 0.5f * (tp.l + tp.r);
    if (v.page == Page::PickSkill && v.editSlot >= 0 && v.editSlot < wheel::kSlotCount) {
        // "Choose a skill for [LT] [A]   now: Fire Bolt" - the title and the slot's mark side by side, centred as a whole
        const char* title = uitext::Tr("vr.tiles.choose_skill_for", "Choose a skill for");
        const tiles::Glyph g = tiles::GlyphOf(v.editSlot);
        const float tw = CapsWidth(fonts, title, 42, 3.0f), gap = 22.0f, gwOne = g.kind == tiles::GlyphKind::Chip ? 68.0f : 42.0f, gw = (g.lt ? 76.0f : 0.0f) + gwOne;
        std::string now;
        if (v.slot[v.editSlot].known)
            now = uitext::TrF("vr.tiles.now", "now: {skill}", {{"skill", v.slot[v.editSlot].id >= 0 ? v.slot[v.editSlot].name : uitext::Tr("vr.skills.nothing", "nothing")}});
        const float nw = !now.empty() ? 26.0f + 8.5f * float(std::min(uitext::Letters(now), 28)) : 0.0f;
        float x = cx - 0.5f * (tw + gap + gw + nw);
        SmallCaps(cv, fonts, title, x + 2.0f, Rect{line.l, line.t + 2.0f, line.r, line.b + 2.0f}, 42, 3.0f, Col{0, 0, 0}, 0.8f, true);
        SmallCaps(cv, fonts, title, x, line, 42, 3.0f, kSilver, 1.0f, true);
        x += tw + gap;
        const float midY = 0.5f * (tp.t + tp.b);
        if (g.lt) {
            const Rect chip{x, midY - 17.0f, x + 56.0f, midY + 17.0f};
            RoundRect(cv, chip, 11.0f, Col{226, 228, 232}, 0.99f, Col{20, 20, 24}, 1.0f, 2.0f);
            Text(cv, fonts, "LT", chip, 21, true, Col{26, 26, 30});
            x += 76.0f;
        }
        DrawGlyph(cv, fonts, g, Rect{x, midY - 23.0f, x + gwOne, midY + 23.0f});
        if (!now.empty()) Text(cv, fonts, now.c_str(), Rect{x + gwOne + 24.0f, tp.t + 8.0f, tp.r - 40.0f, tp.b - 8.0f}, 26, false, kSilverDim, 1.0f, true);
        return;
    }
    if (v.tabs && v.page == Page::Wheel) {   // the Settings tab is there: the plate holds the strip, SKILLS | SETTINGS
        DrawTabs(cv, fonts, v);
        return;
    }
    const char* title = v.page == Page::Wheel ? uitext::Tr("vr.tiles.tab_skills", "Skills") : uitext::Tr("vr.skills.choose_slot", "Choose the slot to change");
    CapsCentred(cv, fonts, title, cx, line, 44, 3.5f, kSilver);
}

// The state in red small capitals - as the game prints under its dialog - and what to do under it, dim.
void DrawStatus(Canvas& cv, Fonts& fonts, const View& v) {
    std::string line;
    if (v.status[0]) {
        line = v.status;
    } else if (!v.ready) {
        line = uitext::Tr("vr.skills.unavailable", "Skills unavailable");
    } else if (v.page == Page::Wheel) {
        const int ts = wheel::TriggerSlot(v);   // (the picked slot; with the slot controls the RT slot too, when it holds a skill)
        if (ts != 8 && v.slot[ts].known && v.slot[ts].id >= 0)
            line = uitext::TrF("vr.skills.trigger_casts", "Trigger casts: {skill}  ({slot})", {{"skill", v.slot[ts].name}, {"slot", wheel::kSlotDefs[ts].label}});
        else
            line = uitext::Tr("vr.tiles.trigger_basic", "Trigger: the basic attack  (A)");
    } else if (v.page == Page::PickSkill && v.editPages > 1) {
        line = uitext::TrF("vr.tiles.page", "Page {page} of {pages}", {{"page", std::to_string(v.editPage + 1)}, {"pages", std::to_string(v.editPages)}});
    }
    const Col red = v.result == wheel::Result::Refused ? kRedBright : kRed;
    const tiles::PRect rl = tiles::kRedLine;
    CapsCentred(cv, fonts, line.c_str(), 0.5f * (rl.l + rl.r), Rect{rl.l, rl.t, rl.r, rl.b}, 32, 2.5f, red);
    const char* hint = v.page == Page::Wheel ? (v.editFirst ? uitext::Tr("vr.tiles.hint_first", "Tap a slot to change its skill - hold the trigger on it and the trigger casts it")
                                                            : uitext::Tr("vr.tiles.hint_wheel", "Point at a slot and pull the trigger"))
                       : v.page == Page::PickSlot ? uitext::Tr("vr.tiles.hint_pick_slot", "Point at the slot to change and pull the trigger")
                                                  : uitext::Tr("vr.tiles.hint_pick_skill", "Point at a skill and pull the trigger to put it on the slot");
    const tiles::PRect hl = tiles::kHintLine;
    CapsCentred(cv, fonts, hint, 0.5f * (hl.l + hl.r), Rect{hl.l, hl.t, hl.r, hl.b}, 22, 1.5f, kSilverDim, false);
}

// The RUN / WALK tile, right of the first row: the game's own run / walk switch. A copper plate that goes red when the game says it is on RUN,
// the state in big letters on it, the control's name under it.
void DrawRunTile(Canvas& cv, Fonts& fonts, const View& v) {
    const Rect r = tiles::kRunRect;
    PlateLook k;
    k.red = v.run == wheel::kRunning;
    k.hot = v.hover == wheel::kRunWalk;
    DrawPlate(cv, r, k);
    const char* state = v.run == wheel::kRunning ? uitext::Tr("vr.tiles.run", "RUN") : v.run == wheel::kWalking ? uitext::Tr("vr.tiles.walk", "WALK")
                                                 : uitext::Tr("vr.tiles.run_walk_unknown", "RUN /\nWALK");
    Text(cv, fonts, state, Rect{r.l + 10.0f, r.t + 22.0f, r.r - 10.0f, r.b - 22.0f}, v.run == wheel::kRunUnknown ? 30 : 42, true,
         k.red ? Col{255, 214, 200} : Col{238, 232, 224});
    DrawTileMarks(cv, r, k);
    Text(cv, fonts, uitext::Tr("vr.wheel.run_walk", "RUN / WALK"), tiles::kRunNameRect, 22, false, kSilver);
}

// Two rows of six: the game's twelve slots, with the mark under each tile, the skill's name and the hint for the controller.
void DrawRows(Canvas& cv, Fonts& fonts, const View& v) {
    const bool pickSlot = v.page == Page::PickSlot;
    DrawLtMark(cv, fonts);
    for (int s = 0; s < wheel::kSlotCount; ++s) {
        const Rect r = tiles::SlotRect(s);
        const Skill& sk = v.slot[s];
        PlateLook k;
        const bool placed = v.ready && sk.known;
        k.red = placed && sk.id >= 0;
        const bool off = v.rowOff && wheel::kSlotDefs[s].alt;   // the second row of buttons is off: the left trigger has its own action
        k.dim = (k.red && !sk.available) || off;
        k.dead = v.ready && !sk.known;
        k.hot = v.hover == s;
        k.active = v.page == Page::Wheel && wheel::TriggerSlot(v) == s;
        DrawTile(cv, fonts, r, k, placed ? &sk : nullptr, pickSlot && placed && sk.id < 0);
        DrawGlyph(cv, fonts, tiles::GlyphOf(s), tiles::GlyphRect(s));
        if (placed && sk.id >= 0) Text(cv, fonts, sk.name, tiles::NameRect(s), 22, false, sk.available && !off ? kSilver : kSilverDim);
        else if (placed) Text(cv, fonts, uitext::Tr("vr.tiles.empty", "Empty"), tiles::NameRect(s), 22, false, Col{110, 112, 120});
        else if (k.dead) Text(cv, fonts, uitext::Tr("vr.tiles.not_found", "Not found"), tiles::NameRect(s), 22, false, Col{110, 112, 120});
        if (off) Text(cv, fonts, uitext::Tr("vr.tiles.lt_row_off", "LT row off"), tiles::HintRect(s), 21, false, Col{150, 120, 110});
        else if (v.hints.text[s][0]) Text(cv, fonts, v.hints.text[s], tiles::HintRect(s), 21, false, Col{128, 156, 120});
    }
}

// The category strip over the grid: All, the class's three trees, General / Items - the one shown gold on a warm plate, the one under the beam lit,
// a tab with no skill in it dim. Each shows how many skills it holds.
void DrawStrip(Canvas& cv, Fonts& fonts, const View& v) {
    for (int c = 0; c < wheel::kCategories; ++c) {
        const Rect r = tiles::StripRect(c);
        const int count = v.catCount[c];
        const bool on = v.category == c, enabled = count > 0 || on, hot = v.hover == wheel::CategoryCell(c) && count > 0 && !on;
        if (hot) RoundRect(cv, Rect{r.l - 6.0f, r.t - 6.0f, r.r + 6.0f, r.b + 6.0f}, 12.0f, kGoldBright, 0.20f, Col{0, 0, 0}, 0.0f, 0.0f);
        RoundRect(cv, r, 9.0f, on ? Col{64, 50, 28} : hot ? Col{52, 42, 26} : Col{22, 22, 26}, 0.99f, on ? kGoldBright : hot ? kGold : enabled ? Col{96, 98, 108} : Col{52, 52, 58}, 1.0f,
                  on ? 3.5f : 2.5f);
        Bar(cv, r.l + 4.0f, r.t + 4.0f, r.r - 4.0f, r.t + 8.0f, Col{255, 255, 255}, on ? 0.10f : 0.05f);
        Bar(cv, r.l + 4.0f, r.b - 7.0f, r.r - 4.0f, r.b - 4.0f, Col{0, 0, 0}, 0.30f);
        const char* cat = c == wheel::kCatAll ? uitext::Tr("vr.skills.all", "All")
                        : c == wheel::kCatGeneral ? uitext::Tr("vr.skills.general", "General / Items")
                        : v.treeName[c - 1][0] ? v.treeName[c - 1] : "-";
        const std::string name = std::string(cat) + "  (" + std::to_string(count) + ")";
        Text(cv, fonts, name.c_str(), Rect{r.l + 8.0f, r.t + 2.0f, r.r - 8.0f, r.b - 6.0f}, 21, true, on ? kGoldBright : hot ? kGold : enabled ? kSilver : Col{92, 92, 100});
    }
}

// The skills the player can put on the slot: tiles of the same make, red where the skill is already on a button, the gold frame on the one
// this slot holds now.
void DrawGrid(Canvas& cv, Fonts& fonts, const View& v) {
    if (v.catStrip) DrawStrip(cv, fonts, v);
    else if (v.catWanted)   // the key is on but the game side does not know the trees: say so where the strip would be, instead of an empty band
        Text(cv, fonts, uitext::Tr("vr.tiles.no_trees", "Skill categories need the icon cache's skill trees - run tools/skill_icons/prepare_skill_icons.py again"), Rect{tiles::kStripLeft, tiles::kStripTop, kW - tiles::kStripLeft, tiles::kStripBottom},
             21, false, kSilverDim);
    if (!v.options) {
        CapsCentred(cv, fonts, uitext::Tr("vr.skills.none", "No skills to choose from"), 0.5f * kW, Rect{200, 380, 1400, 460}, 40, 3.0f, kSilverDim, false);
        return;
    }
    const Skill* now = v.editSlot >= 0 && v.editSlot < wheel::kSlotCount ? &v.slot[v.editSlot] : nullptr;
    for (int i = 0; i < v.options; ++i) {
        const Skill& sk = v.option[i];
        const Rect r = tiles::GridTileRect(i);
        bool bound = false;
        for (int s = 0; s < wheel::kSlotCount; ++s)
            if (v.slot[s].known && v.slot[s].id == sk.id && v.slot[s].owner == sk.owner && sk.id >= 0) bound = true;
        PlateLook k;
        k.red = bound;
        k.hot = v.hover == i;
        k.active = now && now->known && now->id >= 0 && now->id == sk.id && now->owner == sk.owner;
        DrawTile(cv, fonts, r, k, &sk, false);
        Text(cv, fonts, sk.name, tiles::GridNameRect(i), 20, false, k.hot ? kGoldBright : kSilver);
    }
}

// -- the Settings tab ---------------------------------------------------------------------------------------------------------------------

// A line of text that always fits: the size is lowered (to 70 %) until the line is as wide as the box allows, then it is set left, centred or
// right in it. (The kit's Text wraps; a settings line must stay one line.)
void FitLine(Canvas& cv, Fonts& fonts, const std::string& text, Rect box, int size, bool bold, Col color, hset::Align align) {
    if (text.empty()) return;
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return;
    const std::wstring w = Wide(text.c_str());
    HGDIOBJ old = GetCurrentObject(dc, OBJ_FONT);
    const float room = box.r - box.l - 6.0f;
    // four sizes at most per base size (the font table holds sixteen): the full one, then 90, 80 and 70 %
    int px = size;
    float width = 0.0f;
    for (const float step : {1.0f, 0.9f, 0.8f, 0.7f}) {
        px = int(size * step + 0.5f);
        SelectObject(dc, fonts.Get(px, bold));
        SIZE sz = {};
        GetTextExtentPoint32W(dc, w.c_str(), (int)w.size(), &sz);
        width = float(sz.cx);
        if (width <= room) break;
    }
    SelectObject(dc, old);
    DeleteDC(dc);
    Rect at = box;
    if (align == hset::Align::Left) at.r = std::min(box.r, box.l + width + 12.0f);
    else if (align == hset::Align::Right) at.l = std::max(box.l, box.r - width - 12.0f);
    Text(cv, fonts, text.c_str(), at, px, bold, color, 1.0f, align != hset::Align::Center);
}

Col RoleColour(hset::Role r) {
    switch (r) {
    case hset::Role::Back: return Col{6, 6, 8};
    case hset::Role::RowA: return Col{24, 24, 28};
    case hset::Role::RowB: return Col{14, 14, 17};
    case hset::Role::Button: return Col{104, 106, 116};
    case hset::Role::Hot: return Col{255, 232, 160};
    case hset::Role::Selected: return Col{150, 112, 52};
    case hset::Role::Off: return Col{38, 38, 44};
    case hset::Role::Quiet: return kSilverDim;
    case hset::Role::Accent: return kGoldBright;
    case hset::Role::Warn: return Col{236, 160, 66};
    case hset::Role::Dim: return Col{92, 92, 100};
    default: return kSilver;
    }
}

// The settings pages (vr/hset_scene.h) on the window: the sheet's rectangles scaled and put under the title plate, painted in the window's own
// style - the same silver and gold, the same plates, the same font. What is drawn where is the scene's; the hit test reads the same rectangles.
void DrawSettings(Canvas& cv, Fonts& fonts, const View& v) {
    hset::ComposeOptions opt;
    opt.title = false;
    opt.window = true;
    const hset::Scene scene = hset::Compose(v.settings, v.settingsHover, opt);
    const float k = tiles::kSheetScale;
    auto box = [&](const hset::Rect& r) {
        return Rect{tiles::kSheetX + r.x * k, tiles::kSheetY + r.y * k, tiles::kSheetX + (r.x + r.w) * k, tiles::kSheetY + (r.y + r.h) * k};
    };
    for (const hset::Item& it : scene) {
        const Rect r = box(it.r);
        switch (it.type) {
        case hset::Item::Type::Fill:
            if (it.r.w >= tiles::kSheetW && it.r.h >= tiles::kSheetH) break;   // (the sheet's own background: the window's panel is there already)
            if (it.colour == hset::Role::RowA || it.colour == hset::Role::RowB) RoundRect(cv, r, 7.0f, RoleColour(it.colour), 0.80f, Col{0, 0, 0}, 0.0f, 0.0f);
            else Bar(cv, r.l, r.t, r.r, r.b, RoleColour(it.colour), 1.0f);
            break;
        case hset::Item::Type::Text: {
            const int size = it.face == hset::Face::Bold ? 34 : it.face == hset::Face::Small ? 25 : 29;
            FitLine(cv, fonts, it.text, r, size, it.face == hset::Face::Bold, RoleColour(it.colour), it.align);
            break;
        }
        case hset::Item::Type::Button: {
            const bool hot = it.hot, on = it.selected && it.enabled;
            if (hot) RoundRect(cv, Rect{r.l - 6.0f, r.t - 6.0f, r.r + 6.0f, r.b + 6.0f}, 12.0f, kGoldBright, 0.20f, Col{0, 0, 0}, 0.0f, 0.0f);
            RoundRect(cv, r, 8.0f, !it.enabled ? Col{18, 18, 21} : hot ? Col{58, 46, 28} : on ? Col{46, 38, 24} : Col{26, 26, 30}, 0.99f,
                      !it.enabled ? Col{50, 50, 56} : hot ? kGoldBright : on ? kGold : Col{104, 106, 116}, 1.0f, hot ? 3.5f : 2.5f);
            Bar(cv, r.l + 4.0f, r.t + 4.0f, r.r - 4.0f, r.t + 9.0f, Col{255, 255, 255}, it.enabled ? 0.06f : 0.02f);
            Bar(cv, r.l + 4.0f, r.b - 8.0f, r.r - 4.0f, r.b - 4.0f, Col{0, 0, 0}, 0.30f);
            const Col ink = !it.enabled ? Col{92, 92, 100} : hot ? kGoldBright : it.colour == hset::Role::Text ? kSilver : RoleColour(it.colour);
            FitLine(cv, fonts, it.text, Rect{r.l + 4.0f, r.t, r.r - 4.0f, r.b}, 34, true, ink, hset::Align::Center);
            break;
        }
        }
    }
}

}  // namespace

void Size(int* width, int* height) {
    *width = tiles::kW;
    *height = tiles::kH;
}

void Render(const View& v, std::vector<uint32_t>& px) {
    const std::vector<uint32_t>& base = Frame();
    px.assign(base.begin(), base.end());
    Canvas cv{px, tiles::kW, tiles::kH};
    Fonts fonts;
    DrawTitle(cv, fonts, v);
    if (v.tabs && v.page == Page::Wheel) DrawFlipHints(cv, v);
    if (v.tabs && v.tab == wheel::Tab::Settings && v.page == Page::Wheel) {   // the Settings tab: its pages, with their own status line and buttons
        DrawSettings(cv, fonts, v);
        return;
    }
    if (v.page == Page::PickSkill) DrawGrid(cv, fonts, v);
    else {
        DrawRows(cv, fonts, v);
        if (v.runWalk && v.page == Page::Wheel) DrawRunTile(cv, fonts, v);
    }
    DrawStatus(cv, fonts, v);
    DrawButtons(cv, fonts, v);
}

}  // namespace tilespaint
