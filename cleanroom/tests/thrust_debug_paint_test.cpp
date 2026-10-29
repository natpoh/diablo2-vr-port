// [debug] thrust_debug: the readout's picture (vr/thrust_debug_paint.cpp) from the readings of vr/thrust_debug.h - a counted touch and a refused jab: the thrust's own BLUE text where the
// lines pass, red where they fail, the plate dark. Offline, no game. Run: thrust_debug_paint_test.exe [--dump <file.bmp>] (the picture on black, to look at).

#include "../../vr/thrust_debug_paint.h"

#include <cstdio>
#include <cstring>
#include <vector>

static int g_failures = 0;
static void Expect(bool ok, const char* what, double value = 0.0) {
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s (%g)\n", what, value);
    }
}

// Pixels whose colour is mostly the thrust's pass blue (or its fail orange), in a band of rows.
static int Count(const std::vector<uint32_t>& px, int y0, int y1, bool pass) {
    int n = 0;
    for (int y = y0; y < y1 && y < thrustdebugpaint::kH; ++y)
        for (int x = 0; x < thrustdebugpaint::kW; ++x) {
            const uint32_t p = px[static_cast<size_t>(y) * thrustdebugpaint::kW + x];
            const int r = p & 255, g = (p >> 8) & 255, b = (p >> 16) & 255;
            if (pass ? (b > 120 && r < b / 2) : (r > 120 && b < r / 3 && g < r)) ++n;
        }
    return n;
}

static void WriteBmp(const char* path, const std::vector<uint32_t>& px) {
    const int w = thrustdebugpaint::kW, h = thrustdebugpaint::kH, row = w * 3, size = 54 + row * h;
    std::vector<unsigned char> b(static_cast<size_t>(size), 0);
    const auto put32 = [&](int at, int v) { for (int i = 0; i < 4; ++i) b[static_cast<size_t>(at + i)] = static_cast<unsigned char>((v >> (8 * i)) & 255); };
    b[0] = 'B'; b[1] = 'M'; put32(2, size); put32(10, 54); put32(14, 40); put32(18, w); put32(22, h); b[26] = 1; b[28] = 24; put32(34, row * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const uint32_t p = px[static_cast<size_t>(h - 1 - y) * w + x];   // premultiplied over black: the colour as it is
            unsigned char* d = &b[static_cast<size_t>(54 + y * row + x * 3)];
            d[0] = static_cast<unsigned char>((p >> 16) & 255); d[1] = static_cast<unsigned char>((p >> 8) & 255); d[2] = static_cast<unsigned char>(p & 255);
        }
    FILE* f = nullptr;
    if (fopen_s(&f, path, "wb") == 0 && f) { std::fwrite(b.data(), 1, b.size(), f); std::fclose(f); std::printf("  the picture: %s\n", path); }
}

int main(int argc, char** argv) {
    using namespace thrustdbg;
    thrust::Rule r;
    r.minAccel = 12.0f;
    r.kindCm[strikedir::kSpear] = 3.0f;
    thrust::Verdict v;
    v.share = 0.9f;
    v.alongCm = 6.0f;
    v.accel = 30.0f;
    const Reading touch = ContactReading(r, strikedir::kSpear, v, 1.4f, strikedir::Why::Counts, 77, 5, 1000);
    const Reading jab = JabReading(r, strikedir::kSpear, 0.9f, 20.0f, 95.0f, 5.9f, 6, 1000);
    Line lines[thrustdebugpaint::kMaxLines];
    size_t n = Lines(touch, "TOUCH", lines, thrustdebugpaint::kMaxLines);
    n += Lines(jab, "JAB", lines + n, thrustdebugpaint::kMaxLines - n);
    std::vector<uint32_t> px;
    thrustdebugpaint::Render(lines, n, px);
    Expect(px.size() == static_cast<size_t>(thrustdebugpaint::kW) * thrustdebugpaint::kH, "the picture's size");
    const float lineH = (thrustdebugpaint::kH - 16.0f) / static_cast<float>(thrustdebugpaint::kMaxLines);
    const auto band = [&](size_t line) { return static_cast<int>(8.0f + lineH * static_cast<float>(line)); };
    // the touch block (lines 0-5: head, straight, turn, speed, along, accel) is all blue (pass); the jab's head line (6) and its accel line (11) orange (fail),
    // its speed line (9) blue
    Expect(n == 12, "two readings of six lines", static_cast<double>(n));
    Expect(Count(px, band(0), band(1), true) > 150 && Count(px, band(0), band(1), false) < 20, "TOUCH COUNTED: blue text", Count(px, band(0), band(1), true));
    Expect(Count(px, band(6), band(7), false) > 150, "JAB would fail: orange text", Count(px, band(6), band(7), false));
    Expect(Count(px, band(11), band(12), false) > 60 && Count(px, band(9), band(10), true) > 60, "the jab: accel orange, speed blue");
    const uint32_t corner = px[static_cast<size_t>(thrustdebugpaint::kH / 2) * thrustdebugpaint::kW + thrustdebugpaint::kW - 8];
    Expect((corner >> 24) > 150 && (corner & 255) < 40, "the plate: dark and nearly opaque", corner >> 24);
    if (argc > 2 && std::strcmp(argv[1], "--dump") == 0) WriteBmp(argv[2], px);
    if (g_failures == 0) std::printf("thrust_debug_paint_test: all passed\n");
    return g_failures == 0 ? 0 : 1;
}
