// [hud] swing_timer: the picture (vr/swing_timer_paint.cpp) - the ring filled clockwise from the top in the gate's colour, the number inside; the bar
// along the bottom with the number over it; nothing at all when nothing is shown. Offline, no game.
// Run: swing_timer_paint_test.exe [--dump <prefix>] (the pictures on black as <prefix>_ring.bmp, <prefix>_bar.bmp, to look at).

#include "../../vr/swing_timer_paint.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_failures = 0;
static void Expect(bool ok, const char* what, double value = 0.0) {
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s (%g)\n", what, value);
    }
}

using swingtimerpaint::kH;
using swingtimerpaint::kW;

static uint32_t At(const std::vector<uint32_t>& px, int x, int y) { return px[static_cast<size_t>(y) * kW + x]; }
static bool Reddish(uint32_t p) { const int r = p & 255, g = (p >> 8) & 255, b = (p >> 16) & 255; return r > 120 && g < r / 2 && b < r / 2; }
static bool Greenish(uint32_t p) { const int r = p & 255, g = (p >> 8) & 255, b = (p >> 16) & 255; return g > 100 && r < g && b < g / 2; }
// Bright, near-white pixels (the number) in a box.
static int Bright(const std::vector<uint32_t>& px, int x0, int y0, int x1, int y1) {
    int n = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            const uint32_t p = At(px, x, y);
            if ((p & 255) > 200 && ((p >> 8) & 255) > 200 && ((p >> 16) & 255) > 180) ++n;
        }
    return n;
}

static void WriteBmp(const std::string& path, const std::vector<uint32_t>& px) {
    const int row = kW * 3, size = 54 + row * kH;
    std::vector<unsigned char> b(static_cast<size_t>(size), 0);
    const auto put32 = [&](int at, int v) { for (int i = 0; i < 4; ++i) b[static_cast<size_t>(at + i)] = static_cast<unsigned char>((v >> (8 * i)) & 255); };
    b[0] = 'B'; b[1] = 'M'; put32(2, size); put32(10, 54); put32(14, 40); put32(18, kW); put32(22, kH); b[26] = 1; b[28] = 24; put32(34, row * kH);
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) {
            const uint32_t p = px[static_cast<size_t>(kH - 1 - y) * kW + x];   // premultiplied over black: the colour as it is
            unsigned char* d = &b[static_cast<size_t>(54 + y * row + x * 3)];
            d[0] = static_cast<unsigned char>((p >> 16) & 255); d[1] = static_cast<unsigned char>((p >> 8) & 255); d[2] = static_cast<unsigned char>(p & 255);
        }
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "wb") == 0 && f) { std::fwrite(b.data(), 1, b.size(), f); std::fclose(f); std::printf("  the picture: %s\n", path.c_str()); }
}

int main(int argc, char** argv) {
    using namespace swingtimer;
    std::vector<uint32_t> px;

    swingtimerpaint::Render(Look{}, px);
    bool empty = px.size() == static_cast<size_t>(kW) * kH;
    for (uint32_t p : px) empty = empty && p == 0;
    Expect(empty, "nothing shown: a clear picture of the right size");

    // the ring, a quarter full, red, "123" inside
    Look ring;
    ring.visible = ring.timer = true;
    ring.style = Style::Ring;
    ring.step = kSteps / 4;
    ring.zone = Zone::Red;
    ring.timerAlpha = ring.textAlpha = kAlphaSteps;
    ring.text = Text::Damage;
    ring.value = 123;
    ring.count = 1;
    swingtimerpaint::Render(ring, px);
    const int cx = kW / 2, cy = kH / 2, mid = 86 - 5;   // the ring's band: outer radius 86, 11 wide
    Expect(Reddish(At(px, cx + 3, cy - mid)), "the fill starts at the top");
    Expect(Reddish(At(px, cx + mid / 2 + 8, cy - mid + 12)), "and runs clockwise");
    Expect(!Reddish(At(px, cx - mid, cy)) && (At(px, cx - mid, cy) >> 24) > 60, "the rest of the track: dark, not red");
    Expect(Bright(px, cx - 60, cy - 30, cx + 60, cy + 30) > 150, "the number inside", Bright(px, cx - 60, cy - 30, cx + 60, cy + 30));
    Expect(At(px, 4, 4) == 0 && At(px, kW - 4, kH - 4) == 0, "outside the ring: clear");
    if (argc > 2 && std::strcmp(argv[1], "--dump") == 0) WriteBmp(std::string(argv[2]) + "_ring.bmp", px);

    // full and green
    ring.step = kSteps;
    ring.zone = Zone::Green;
    swingtimerpaint::Render(ring, px);
    Expect(Greenish(At(px, cx - mid, cy)) && Greenish(At(px, cx + mid, cy)), "full: green all round");

    // the bar, half full, amber; the number over it
    Look bar = ring;
    bar.style = Style::Bar;
    bar.step = kSteps / 2;
    bar.zone = Zone::Amber;
    bar.rise = 0;
    swingtimerpaint::Render(bar, px);
    const uint32_t left = At(px, 40, kH - 20), right = At(px, kW - 40, kH - 20);
    Expect(((left & 255) > 180 && ((left >> 8) & 255) > 100 && ((left >> 16) & 255) < 90), "the bar's left half: amber", left);
    Expect(!Greenish(right) && (right & 255) < 60 && (right >> 24) > 60, "its right half: the dark track", right);
    Expect(Bright(px, 0, 60, kW, kH - 30) > 150, "the number over the bar");
    if (argc > 2 && std::strcmp(argv[1], "--dump") == 0) WriteBmp(std::string(argv[2]) + "_bar.bmp", px);
    bar.rise = kRiseSteps;
    swingtimerpaint::Render(bar, px);
    Expect(Bright(px, 0, 0, kW, 70) > 150 && Bright(px, 0, 100, kW, kH - 30) < 20, "risen: at the top");

    if (g_failures == 0) std::printf("swing_timer_paint_test: all passed\n");
    return g_failures == 0 ? 0 : 1;
}
