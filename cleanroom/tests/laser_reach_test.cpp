// Offline checks of vr/laser_reach.h ([openxr] laser_fit_window): the laser reaches every pixel of the game's picture. The laser's whole way is
// simulated - u, v on the picture -> the client area's pixel (LaserStep) -> the screen -> where Windows lets the cursor go (never off its monitors)
// and which window a click there lands on (the taskbar over the work area's edge) -> the game's read of it, mapped to its picture while a render
// size is in force (vr_resolution.h ToGame) - and swept over the picture: every row and column, the corners, the top strip where the tabbed menu's
// tabs are (inventory ... options), the bottom one (the belt, the main menu's last row). Monitors and windows as players have them: the author's
// 4K with a 1920 square and a 2048 render size (all reachable as it was), a 1080p and a 1440p monitor with the same shipped 1920 square (rows out
// of reach before the fit, none after), the window the start cut leaves under the taskbar, a monitor left of the main one. Run: laser_reach_test.exe.
#include <cstdio>
#include <initializer_list>
#include <vector>

#include "../../vr/laser_reach.h"
#include "../../vr/vr_resolution.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

using laserreach::Fit;
using laserreach::Pt;
using laserreach::Rect;

struct Monitor { Rect screen, work; };

struct Setup {
    const char* name;
    std::vector<Monitor> monitors;
    Rect window, client;     // on the screen
    uint32_t gameW = 0, gameH = 0;   // the game's picture: a render size in force; 0: the client area itself
};

constexpr int32_t kFrameL = 8, kFrameT = 31, kFrameR = 8, kFrameB = 8;   // Windows 11's frame round a client area (the resize borders, the title bar)
static Rect WindowOf(const Rect& c) { return {c.l - kFrameL, c.t - kFrameT, c.r + kFrameR, c.b + kFrameB}; }
static Rect ClientAt(int32_t x, int32_t y, int32_t w, int32_t h) { return {x, y, x + w, y + h}; }

// Where Windows puts the cursor asked to `p`: on a monitor (the nearest, clamped into it).
static Pt CursorAt(Pt p, const std::vector<Monitor>& ms, const Monitor** on) {
    long long best = -1;
    Pt got = p;
    for (const Monitor& m : ms) {
        const Pt c{std::clamp(p.x, m.screen.l, m.screen.r - 1), std::clamp(p.y, m.screen.t, m.screen.b - 1)};
        const long long d = (long long)(c.x - p.x) * (c.x - p.x) + (long long)(c.y - p.y) * (c.y - p.y);
        if (best < 0 || d < best) { best = d; got = c; *on = &m; }
    }
    return got;
}

// The laser at u, v: does the game read the very pixel of its picture the laser points at, with a click there landing on the game?
static bool Reaches(const Setup& s, float u, float v) {
    const int32_t cw = s.client.W(), ch = s.client.H();
    const Pt p = laserreach::ClientPixel(u, v, cw, ch);
    const Pt want{s.client.l + p.x, s.client.t + p.y};
    const Monitor* m = nullptr;
    const Pt got = CursorAt(want, s.monitors, &m);
    if (got.x != want.x || got.y != want.y) return false;   // off the desktop: the cursor stopped at its edge
    if (!m || got.x < m->work.l || got.x >= m->work.r || got.y < m->work.t || got.y >= m->work.b) return false;   // under the taskbar: its click
    // the game's read: its client pixel, and the picture's while a render size is in force
    const vrres::Pt back{got.x - s.client.l, got.y - s.client.t};
    if (!s.gameW) return back.x == p.x && back.y == p.y;
    const vrres::Pt pic = vrres::ToGame(back, {(uint32_t)cw, (uint32_t)ch}, {s.gameW, s.gameH});
    const vrres::Pt aim = vrres::ToGame({p.x, p.y}, {(uint32_t)cw, (uint32_t)ch}, {s.gameW, s.gameH});
    return pic.x == aim.x && pic.y == aim.y;
}

struct Sweep { int total = 0, missed = 0, topMissed = 0, bottomMissed = 0, cornersMissed = 0; };

static Sweep SweepAll(const Setup& s) {
    Sweep r;
    const int32_t cw = s.client.W(), ch = s.client.H();
    // every client row and column through the middle, the edges and a grid; the picture's pixels follow (ClientPixel is onto)
    std::vector<std::pair<float, float>> at;
    for (int32_t y = 0; y < ch; ++y) at.push_back({0.5f, (y + 0.5f) / ch});
    for (int32_t x = 0; x < cw; ++x) at.push_back({(x + 0.5f) / cw, 0.5f});
    for (int i = 0; i <= 64; ++i)
        for (int j = 0; j <= 64; ++j) at.push_back({std::min(i / 64.0f, 0.9999f), std::min(j / 64.0f, 0.9999f)});
    for (const auto& [u, v] : at) {
        ++r.total;
        if (Reaches(s, u, v)) continue;
        ++r.missed;
        if (v < 0.08f) ++r.topMissed;          // the tabbed menu's tab bar (inventory ... options), a panel's title
        if (v > 0.92f) ++r.bottomMissed;       // the belt, the main menu's last row
    }
    const float corners[4][2] = {{0.0f, 0.0f}, {0.9999f, 0.0f}, {0.0f, 0.9999f}, {0.9999f, 0.9999f}};
    for (const auto& c : corners) if (!Reaches(s, c[0], c[1])) ++r.cornersMissed;
    return r;
}

// The setup after the fit, as vrcam's tick puts the window (its client area keeps its frame).
static Setup Fitted(const Setup& s, bool mayResize, Fit* out) {
    const Monitor* m = &s.monitors[0];
    const Fit f = laserreach::FitWindow(s.window, s.client, m->work, mayResize);
    *out = f;
    Setup n = s;
    n.window = f.window;
    n.client = {f.window.l + kFrameL, f.window.t + kFrameT, f.window.r - kFrameR, f.window.b - kFrameB};
    return n;
}

static void Report(const char* when, const Setup& s, const Sweep& r) {
    std::printf("  %-44s %-6s client %dx%d at %d,%d: %d of %d points out of reach (top strip %d, bottom strip %d, corners %d)\n", s.name, when,
                s.client.W(), s.client.H(), s.client.l, s.client.t, r.missed, r.total, r.topMissed, r.bottomMissed, r.cornersMissed);
}

static void TestClientPixel() {
    CHECK(laserreach::ClientPixel(0.0f, 0.0f, 1920, 1080).x == 0);
    const Pt e = laserreach::ClientPixel(0.99999f, 0.99999f, 1920, 1080);
    CHECK(e.x == 1919 && e.y == 1079);                     // the last pixel
    const Pt o = laserreach::ClientPixel(1.0f, 1.2f, 1920, 1080);
    CHECK(o.x == 1919 && o.y == 1079);                     // never past it
    const Pt n = laserreach::ClientPixel(-0.1f, 0.5f, 1920, 1080);
    CHECK(n.x == 0 && n.y == 540);
    // onto: every pixel of a row has a u that lands on it
    int hit = 0;
    for (int32_t x = 0; x < 1536; ++x) hit += laserreach::ClientPixel((x + 0.5f) / 1536.0f, 0.5f, 1536, 1536).x == x;
    CHECK(hit == 1536);
}

static void TestAuthor4k() {
    // the author's: 3840x2160, the taskbar 48 high; the window 1920x1920 (the square), a 2048 render size: all of it reachable, nothing moved
    Setup s{"4K, 1920 square, render 2048", {{{0, 0, 3840, 2160}, {0, 0, 3840, 2112}}}, {}, ClientAt(960, 40, 1920, 1920), 2048, 2048};
    s.window = WindowOf(s.client);
    const Sweep before = SweepAll(s);
    Report("as is", s, before);
    CHECK(before.missed == 0);
    Fit f;
    Fitted(s, true, &f);
    CHECK(!f.change && f.fits);
}

static void TestSmallMonitors() {
    struct Case { const char* name; int32_t mw, mh, cx, cy; };
    const Case cases[] = {
        {"1080p, 1920 square below the top", 1920, 1080, 0, 31},          // the window's top at the screen's: its bottom 870 rows off it
        {"1080p, 1920 square centred", 1920, 1080, 0, (1080 - 1920) / 2},  // the game centring it: the top (the tabs) off it too
        {"1440p, 1920 square", 2560, 1440, 320, 31},
    };
    for (const Case& c : cases) {
        Setup s{c.name, {{{0, 0, c.mw, c.mh}, {0, 0, c.mw, c.mh - 48}}}, {}, ClientAt(c.cx, c.cy, 1920, 1920), 1536, 1536};
        s.window = WindowOf(s.client);
        const Sweep before = SweepAll(s);
        Report("before", s, before);
        CHECK(before.missed > 0);                 // the players' report: parts of the screen out of the laser's reach
        CHECK(before.bottomMissed > 0 || before.topMissed > 0);
        Fit f;
        const Setup n = Fitted(s, true, &f);      // a render size in force: shrunk and moved onto the monitor
        const Sweep after = SweepAll(n);
        Report("after", n, after);
        CHECK(f.change && f.resized && f.fits);
        CHECK(after.missed == 0 && after.cornersMissed == 0);
        CHECK(n.client.W() == n.client.H());      // its shape kept (the square)
        CHECK(s.monitors[0].work.Holds(n.window)); // frame and all on the screen, above the taskbar
    }
}

static void TestUnderTheTaskbar() {
    // the start's cut (SafeGameWindow: the main monitor's work area less 45 - 995 on a 1080p one) where Windows cascades a new window (26, 26):
    // its bottom rows under the taskbar. The window is the game's picture (no render size yet): only moved
    Setup s{"1080p, 995 square cascaded (no render size)", {{{0, 0, 1920, 1080}, {0, 0, 1920, 1032}}}, {}, ClientAt(26 + kFrameL, 26 + kFrameT, 995, 995), 0, 0};
    s.window = WindowOf(s.client);
    const Sweep before = SweepAll(s);
    Report("before", s, before);
    CHECK(before.bottomMissed > 0 && before.topMissed == 0);
    Fit f;
    const Setup n = Fitted(s, false, &f);
    const Sweep after = SweepAll(n);
    Report("after", n, after);
    CHECK(f.change && !f.resized && f.fits);
    CHECK(n.client.W() == 995 && n.client.H() == 995);   // the game's picture untouched
    CHECK(after.missed == 0);
}

static void TestTooBigNotResizable() {
    // the plain square before a game (its window is the picture): bigger than the monitor - moved so its top left is on the screen: the top strip
    // (the tabs, the titles) reachable; what does not fit stays out (vrcam's square-window tick makes it fit once in a game)
    Setup s{"1080p, 1920x1035 not resizable", {{{0, 0, 1920, 1080}, {0, 0, 1920, 1032}}}, {}, ClientAt(-200, -300, 1920, 1035), 0, 0};
    s.window = WindowOf(s.client);
    const Sweep before = SweepAll(s);
    Fit f;
    const Setup n = Fitted(s, false, &f);
    const Sweep after = SweepAll(n);
    Report("before", s, before);
    Report("after", n, after);
    CHECK(f.change && !f.resized && !f.fits);
    CHECK(n.window.l == 0 && n.window.t == 0);
    CHECK(before.topMissed > 0 && after.topMissed < before.topMissed / 10);   // the top strip back, but for the columns past the right edge
    CHECK(Reaches(n, 0.0f, 0.0f) && !Reaches(s, 0.0f, 0.0f));               // the top left corner on the screen
    CHECK(after.missed < before.missed);
}

static void TestLeftMonitor() {
    // a second monitor left of the main one (negative x), the window hanging off its left edge: put back inside
    Setup s{"left monitor at -1920, window off its edge", {{{-1920, 0, 0, 1080}, {-1920, 0, 0, 1040}}}, {}, ClientAt(-2100, 100, 1000, 800), 1536, 1536};
    s.window = WindowOf(s.client);
    CHECK(SweepAll(s).missed > 0);
    Fit f;
    const Setup n = Fitted(s, true, &f);
    CHECK(f.change && !f.resized && f.fits);
    CHECK(n.window.l == -1920);
    CHECK(SweepAll(n).missed == 0);
}

static void TestNothingToDo() {
    const Rect work{0, 0, 1920, 1040};
    const Rect c = ClientAt(100, 100, 800, 800);
    const Fit f = laserreach::FitWindow(WindowOf(c), c, work, true);
    CHECK(!f.change && !f.resized && f.fits);
    // a degenerate client area: left alone
    const Fit g = laserreach::FitWindow({0, 0, 10, 10}, {5, 5, 5, 5}, work, true);
    CHECK(!g.change);
    // a work area too small to shrink into (under kMinClient): moved only
    const Rect tiny{0, 0, 300, 300};
    const Fit h = laserreach::FitWindow(WindowOf(c), c, tiny, true);
    CHECK(!h.resized);
}

// [render] ui_band_16x9: the interface's answer - the whole picture, or a 16:9 band of its width at most as high as the picture; the cursor always the whole picture
static void TestUiBand() {
    using namespace vrres;
    CHECK((UiCanvas({2112, 2304}) == Size{2112, 2304}));
    CHECK((UiCanvas({2112, 2304}, true) == Size{2112, 1188}));
    CHECK((UiCanvas({3840, 2160}, true) == Size{3840, 2160}));   // already 16:9
    CHECK((UiCanvas({3840, 1600}, true) == Size{3840, 1600}));   // wider than 16:9: as it is
    const uint64_t native = NativeSize({1920, 1080});
    CHECK((FromNative(CanvasAnswer(Asker::Ui, {2112, 2304}, native, true, true)) == Size{2112, 1188}));
    CHECK((FromNative(CanvasAnswer(Asker::Cursor, {2112, 2304}, native, true, true)) == Size{2112, 2304}));
    CHECK(CanvasAnswer(Asker::Ui, {2112, 2304}, native, false, true) == native);   // vr_resolution_ui off: the window's own
    CHECK(BandLowerShare({3840, 2160}) == 0.0f);                      // 16:9: nothing to lower
    CHECK(std::fabs(BandLowerShare({2112, 2304}) - 0.5f * (2304 - 1188) / 2304.0f) < 1e-6f);   // the band's centre where the canvas's was
    CHECK(BandLowerShare({0, 0}) == 0.0f);
    // 0.214.2: the panel in front shows only the band - its rows, centred where the lowered whole canvas put the band (the canvas's centre)
    CHECK(BandRows({2112, 2304}) == 1188u);
    CHECK(BandRows({3840, 2160}) == 2160u);                               // 16:9: the whole canvas (no crop)
    CHECK(BandRows({3840, 2000}) == 2000u);                               // wider than 16:9: never more rows than it has
    CHECK(BandRows({0, 0}) == 0u);
    CHECK(BandRows({1, 1}) == 1u);
    for (Size c : {Size{2112, 2304}, Size{2048, 2048}, Size{1920, 1920}, Size{2016, 2240}}) {
        const float H = 1.0f, B = (float)BandRows(c) / (float)c.h;      // heights as shares of the whole canvas's
        const float bandCentre = -BandLowerShare(c) * H + 0.5f * (H - B);   // on the lowered whole canvas, from the unlowered centre
        CHECK(std::fabs(bandCentre) < 0.5f / (float)c.h);                 // the cropped quad centred there: nothing moves (half a row)
    }
    // the veil's samples: the band's corners and side middles, then 5 along the last row below a band shorter than the canvas
    int x = -1, y = -1;
    CHECK(VeilSampleAt(0, 2112, 2304, 1188, &x, &y) && x == 0 && y == 0);
    CHECK(VeilSampleAt(3, 2112, 2304, 1188, &x, &y) && x == 2111 && y == 1187);
    CHECK(VeilSampleAt(5, 2112, 2304, 1188, &x, &y) && x == 2111 && y == 594);
    CHECK(VeilSampleAt(6, 2112, 2304, 1188, &x, &y) && x == 1056 && y == 0);          // the top middle
    CHECK(VeilSampleAt(7, 2112, 2304, 1188, &x, &y) && x == 1056 && y == 1187);       // the band's bottom middle
    CHECK(VeilSampleAt(8, 2112, 2304, 1188, &x, &y) && x == 352 && y == 2303);
    CHECK(VeilSampleAt(12, 2112, 2304, 1188, &x, &y) && x == 1760 && y == 2303);
    CHECK(!VeilSampleAt(13, 2112, 2304, 1188, &x, &y));
    CHECK(!VeilSampleAt(8, 3840, 2160, 2160, &x, &y));                    // 16:9: nothing below the band
    CHECK(!VeilSampleAt(0, 0, 0, 0, &x, &y));
    // the square layout ([render] menu_unveil): the whole canvas's corners and middles, nothing "below"
    CHECK(VeilSampleAt(3, 2112, 2304, 2304, &x, &y) && x == 2111 && y == 2303);
    CHECK(VeilSampleAt(5, 2112, 2304, 2304, &x, &y) && x == 2111 && y == 1152);
    CHECK(VeilSampleAt(7, 2112, 2304, 2304, &x, &y) && x == 1056 && y == 2303);
    CHECK(!VeilSampleAt(8, 2112, 2304, 2304, &x, &y));
    // which rows the veil is looked for in: the band in force - its rows (key or not); the square layout - the whole canvas with the key, else none
    CHECK(UnveilRows({2112, 2304}, true, false) == 1188u);
    CHECK(UnveilRows({2112, 2304}, true, true) == 1188u);
    CHECK(UnveilRows({2112, 2304}, false, false) == 0u);                   // menu_unveil 0: as the game draws it (0.214.3)
    CHECK(UnveilRows({2112, 2304}, false, true) == 2304u);
    CHECK(UnveilRows({3840, 2160}, true, false) == 0u);                    // a 16:9 canvas: no band crop, the key decides
    CHECK(UnveilRows({3840, 2160}, true, true) == 2160u);
    CHECK(UnveilRows({0, 0}, true, true) == 0u);
    // the veil: the most see-through near-black sample, neither clear nor solid; the clear ones (the veil not reaching there) never count
    float px[kVeilSamples][4] = {};
    auto set = [&](int i, float r, float a) { px[i][0] = r; px[i][1] = 0.0f; px[i][2] = 0.0f; px[i][3] = a; };
    for (int i = 0; i < kVeilSamples; ++i) set(i, 0.0f, 0.45f);
    float veil[4] = {};
    CHECK(VeilOf(px, kVeilSamples, veil) && std::fabs(veil[3] - 0.45f) < 1e-6f);
    set(1, 0.0f, 1.0f); set(3, 0.0f, 1.0f); set(5, 0.0f, 1.0f);            // the right edge clear (the veil only on the left)
    set(0, 0.0f, 0.0f);                                                    // a corner under solid black art
    set(2, 0.6f, 0.3f);                                                    // a coloured pixel (art) there
    set(4, 0.0f, 0.3f);                                                    // the veil under a translucent panel's edge: less see-through
    CHECK(VeilOf(px, kVeilSamples, veil) && std::fabs(veil[3] - 0.45f) < 1e-6f);
    for (int i = 0; i < kVeilSamples; ++i) set(i, 0.0f, 1.0f);
    CHECK(!VeilOf(px, kVeilSamples, veil));                                // no veil anywhere: left as it is
    for (int i = 0; i < kVeilSamples; ++i) set(i, 0.3f, 0.45f);
    CHECK(!VeilOf(px, kVeilSamples, veil));                                // not black: never a veil
    // a pixel unveiled (veil black, 45 % see-through)
    const float v[4] = {0.0f, 0.0f, 0.0f, 0.45f};
    float out[4];
    const float empty[4] = {0.0f, 0.0f, 0.0f, 0.45f};
    Unveil(empty, v, out); CHECK(out[3] == 1.0f && out[0] == 0.0f);        // only the veil: nothing at all
    const float art[4] = {0.5f, 0.4f, 0.3f, 0.0f};
    Unveil(art, v, out); CHECK(out[3] == 0.0f && out[0] == 0.5f);          // solid art: as it is
    const float tip[4] = {0.05f, 0.05f, 0.05f, 0.09f};                     // a dark 80 % tooltip over the veil
    Unveil(tip, v, out); CHECK(std::fabs(out[3] - 0.2f) < 1e-5f && std::fabs(out[0] - 0.05f) < 1e-6f);   // 80 % again, its colour kept
    const float shadow[4] = {0.0f, 0.0f, 0.0f, 0.27f};                     // a soft black drop shadow, 40 % over the veil
    Unveil(shadow, v, out); CHECK(out[3] > 0.999f);                        // goes with the veil
    const float blackArt[4] = {0.0f, 0.0f, 0.0f, 0.045f};                  // solid-ish black art (90 %)
    Unveil(blackArt, v, out); CHECK(std::fabs(out[3] - 0.1f) < 1e-5f);     // stays
    const float greyVeil[4] = {0.01f, 0.01f, 0.01f, 0.5f};                 // a veil not quite black: its colour taken out too
    const float onGrey[4] = {0.31f, 0.01f, 0.01f, 0.0f};
    Unveil(onGrey, greyVeil, out); CHECK(std::fabs(out[0] - 0.31f) < 1e-6f && out[3] == 0.0f);   // (solid: t 0, nothing of the veil in it)
    const float halfOnGrey[4] = {0.255f, 0.005f, 0.005f, 0.25f};          // 50 % red 0.5 straight over the grey veil
    Unveil(halfOnGrey, greyVeil, out); CHECK(std::fabs(out[0] - 0.25f) < 1e-5f && std::fabs(out[3] - 0.5f) < 1e-5f);
}

int main() {
    TestUiBand();
    TestClientPixel();
    TestAuthor4k();
    TestSmallMonitors();
    TestUnderTheTaskbar();
    TestTooBigNotResizable();
    TestLeftMonitor();
    TestNothingToDo();
    std::printf("%s: %d checks, %d failures\n", g_failures ? "FAILED" : "passed", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
