// calibration_panel.cpp - the measurement's prompt as pictures: a dark rounded panel with the steps, the one line that
// says what is wrong or right now, and the empty track of a progress bar (the fill is a quad of xr.cpp's, grown with
// the hold). One image per message, painted once with GDI (grayscale-antialiased text as a coverage mask) and put in a
// static swapchain by xr.cpp when it first shows. Premultiplied RGBA, R in the low byte, as MakeStaticImage takes it.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "calibration.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace calib {
namespace {

struct Rgb { float r, g, b; };   // 0..255

// One message of the bottom line: what to say, in which colour.
struct Message { const wchar_t* text; Rgb color; };
const Rgb kInfo{150, 205, 255}, kGood{125, 232, 145}, kWarn{255, 170, 95}, kBad{255, 115, 115};
const Message kMessages[kPanelMessages] = {
    {L"Let go of both triggers.", kInfo},                                                            // 0 release
    {L"Good. Now squeeze both triggers and hold still.", kGood},                                   // 1 ready
    {L"Hold still...", kGood},                                                                       // 2 hold
    {L"The headset gives no floor (no room set up), so the height cannot be measured.", kBad},      // 3 floor
    {L"Your eyes read outside 0.8 - 2.3 m over the floor. Is the floor level set up?", kWarn},       // 4 height
    {L"The headset or a controller is not tracked. Hold the controllers where the headset sees them.", kWarn},   // 5 tracking
    {L"Stand up straight and look straight ahead.", kWarn},                                          // 6 upright
    {L"Arms out to the sides, level with the shoulders, hands not ahead of you.", kWarn},            // 7 arms
    {L"You moved. Let go of both triggers and hold still.", kWarn},                                  // 8 still
    {L"Saved, and in use. Let go of the triggers.", kGood},                                          // 9 saved
    {L"d2r_vr.ini could not be written (see the log). Let go and try again.", kBad},                 // 10 failed
};
// Seated ([calibration] seated=1, images kPanelMessages and on): the same, but the head is not asked to stand.
const wchar_t* SeatedText(int message) {
    return message == 6 ? L"Sit up straight and look straight ahead." : kMessages[message].text;
}

struct Canvas {
    uint32_t w, h;
    std::vector<float> px;   // premultiplied r g b a, 0..1
    Canvas(uint32_t w_, uint32_t h_) : w(w_), h(h_), px((size_t)w_ * h_ * 4, 0.0f) {}
    float* at(uint32_t x, uint32_t y) { return &px[((size_t)y * w + x) * 4]; }
};

// src over dst, premultiplied
void Over(float* dst, float r, float g, float b, float a) {
    const float k = 1.0f - a;
    dst[0] = r * a + dst[0] * k;   // (r, g, b are straight colour; a the cover)
    dst[1] = g * a + dst[1] * k;
    dst[2] = b * a + dst[2] * k;
    dst[3] = a + dst[3] * k;
}

// A rounded rectangle, its edge antialiased; `fill` straight colour, `alpha` its opacity.
void RoundRect(Canvas& c, float x0, float y0, float x1, float y1, float radius, Rgb fill, float alpha) {
    const float cx = 0.5f * (x0 + x1), cy = 0.5f * (y0 + y1), hx = 0.5f * (x1 - x0) - radius, hy = 0.5f * (y1 - y0) - radius;
    const uint32_t xa = (uint32_t)std::max(0.0f, std::floor(x0) - 1), xb = (uint32_t)std::min((float)c.w, std::ceil(x1) + 1);
    const uint32_t ya = (uint32_t)std::max(0.0f, std::floor(y0) - 1), yb = (uint32_t)std::min((float)c.h, std::ceil(y1) + 1);
    for (uint32_t y = ya; y < yb; ++y)
        for (uint32_t x = xa; x < xb; ++x) {
            const float dx = std::fabs((float)x + 0.5f - cx) - hx, dy = std::fabs((float)y + 0.5f - cy) - hy;
            const float sd = std::sqrt(std::max(dx, 0.0f) * std::max(dx, 0.0f) + std::max(dy, 0.0f) * std::max(dy, 0.0f)) +
                             std::min(std::max(dx, dy), 0.0f) - radius;
            const float cover = std::clamp(0.5f - sd, 0.0f, 1.0f);
            if (cover > 0.0f) Over(c.at(x, y), fill.r / 255.0f, fill.g / 255.0f, fill.b / 255.0f, alpha * cover);
        }
}

// Text through GDI into a scratch black DIB; the gray it leaves is the coverage, laid on the canvas in `color`.
struct Scratch {
    HDC dc = nullptr;
    HBITMAP dib = nullptr, old = nullptr;
    void* bits = nullptr;
    uint32_t w = 0, h = 0;
    bool Make(uint32_t w_, uint32_t h_) {
        w = w_; h = h_;
        dc = CreateCompatibleDC(nullptr);
        if (!dc) return false;
        BITMAPINFO bi{};
        bi.bmiHeader = {sizeof(BITMAPINFOHEADER), (LONG)w, -(LONG)h, 1, 32, BI_RGB};
        dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!dib || !bits) return false;
        old = (HBITMAP)SelectObject(dc, dib);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        return true;
    }
    ~Scratch() {
        if (dc && old) SelectObject(dc, old);
        if (dib) DeleteObject(dib);
        if (dc) DeleteDC(dc);
    }
};

HFONT Font(int px, int weight) {
    return CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

// text in `box` (word-wrapped, centred if asked), coloured, over the canvas.
bool Text(Canvas& c, Scratch& s, const wchar_t* text, RECT box, int fontPx, int weight, Rgb color, bool centre) {
    HFONT font = Font(fontPx, weight);
    if (!font) return false;
    HGDIOBJ old = SelectObject(s.dc, font);
    memset(s.bits, 0, (size_t)s.w * s.h * 4);
    DrawTextW(s.dc, text, -1, &box, DT_WORDBREAK | DT_NOPREFIX | (centre ? DT_CENTER : DT_LEFT));
    GdiFlush();
    SelectObject(s.dc, old);
    DeleteObject(font);
    const uint8_t* src = (const uint8_t*)s.bits;
    const int y0 = std::max(0L, box.top - 4), y1 = std::min((LONG)c.h, box.bottom + 4);
    for (int y = y0; y < y1; ++y)
        for (uint32_t x = 0; x < c.w; ++x) {
            const uint8_t cov = src[((size_t)y * s.w + x) * 4 + 1];   // the green of a white-on-black pixel
            if (cov) Over(c.at(x, (uint32_t)y), color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, cov / 255.0f);
        }
    return true;
}

}  // namespace

bool PaintPanel(int image, uint32_t* pixels) {
    if (image < 0 || image >= kPanelImages || !pixels) return false;
    Canvas c(kPanelW, kPanelH);
    Scratch s;
    if (!s.Make(kPanelW, kPanelH)) return false;
    // the panel and its edge
    RoundRect(c, 0, 0, (float)kPanelW, (float)kPanelH, 30, Rgb{90, 98, 120}, 0.95f);
    RoundRect(c, 3, 3, (float)kPanelW - 3, (float)kPanelH - 3, 27, Rgb{16, 18, 26}, 0.94f);
    // the track of the bar (xr.cpp grows a fill quad on it)
    RoundRect(c, (float)kTrackX, (float)kTrackY, (float)(kTrackX + kTrackW), (float)(kTrackY + kTrackH), 11, Rgb{54, 60, 78}, 1.0f);

    bool ok = true;
    const bool seated = image >= kPanelMessages;
    const int message = image % kPanelMessages;
    ok = ok && Text(c, s, seated ? L"Seated: measure my arm span" : L"Measure my height and arm span", RECT{60, 22, (LONG)kPanelW - 60, 78}, 40,
                    FW_SEMIBOLD, Rgb{255, 205, 100}, false);
    ok = ok && Text(c, s,
                    seated ? L"Sit upright and look straight ahead. Stretch both arms out sideways at shoulder height.\n"
                             L"Let go of both triggers, then squeeze both and hold still. Only the arm span is read: "
                             L"your eye height is not measured seated (the stored one stays)."
                           : L"Stand upright and look straight ahead. Stretch both arms out sideways at shoulder height.\n"
                             L"Let go of both triggers, then squeeze both and hold still.",
                    RECT{60, 86, (LONG)kPanelW - 60, 244}, seated ? 26 : 29, FW_NORMAL, Rgb{236, 236, 242}, false);
    const Message& m = kMessages[message];
    ok = ok && Text(c, s, seated ? SeatedText(message) : m.text, RECT{60, 252, (LONG)kPanelW - 60, 340}, 30, FW_SEMIBOLD, m.color, false);
    ok = ok && Text(c, s, L"Right B cancels. The game goes on behind this - measure somewhere safe.",
                    RECT{60, 392, (LONG)kPanelW - 60, 430}, 22, FW_NORMAL, Rgb{150, 156, 172}, false);
    if (!ok) return false;

    for (size_t i = 0; i < (size_t)kPanelW * kPanelH; ++i) {
        const float* p = &c.px[i * 4];
        auto q = [](float v) { return (uint32_t)std::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f); };
        pixels[i] = q(p[0]) | (q(p[1]) << 8) | (q(p[2]) << 16) | (q(p[3]) << 24);
    }
    return true;
}

}  // namespace calib
