#pragma once
// A render size for the headset that is not the monitor's window ([render] vr_resolution,
// docs: the plan in the session notes, "vr-resolution").
//
// The idea, and the shape of the pieces, are bearstonem/d2r-pcvr's (MIT, steamvr/VR_RESOLUTION.md,
// render_resolution.cpp, render_window.h, vr_resolution.h): the game is asked for a size through its own
// WM_SIZE while its window keeps the size it has, and the cursor the game reads is mapped between the two.
// The code here is written again for this plugin.
//
// This header is the part with no Windows in it: which size a choice makes, how a point maps from the
// window to the game's pixels and back, and WHEN to ask - the policy (the hero has to be in a world, the
// headset away for a while is not an end, the game's own Apply asks for a second look, a size the game
// did not take is asked for again twice and then given up). Time is handed in, so tests drive it.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

#include "render_size.h"   // the ini's two keys, read the same way by D2R VR Settings (shared/)

namespace vrres {

struct Size {
    uint32_t w = 0, h = 0;
    bool operator==(const Size&) const = default;
    explicit operator bool() const { return w && h; }
};
struct Pt { int x = 0, y = 0; };

inline uint64_t Pack(Size s) { return (uint64_t)s.w << 32 | s.h; }
inline Size Unpack(uint64_t v) { return {(uint32_t)(v >> 32), (uint32_t)v}; }

// [render] vr_resolution (the older mode): 0 Desktop (the window's own size - as before), 1 Headset (the runtime's
// recommendation), 2..6 a square. [render] vr_resolution_px: a square's side, any (render_size.h).
inline constexpr int kChoices = 7;
inline constexpr const char* kChoiceNames[kChoices] = {"Desktop", "Headset", "1536 x 1536", "2048 x 2048", "2560 x 2560", "3072 x 3072", "4096 x 4096"};
inline constexpr uint32_t kMaxSide = rendersize::kSideMax;   // WM_SIZE carries 16 bits; a texture past this is no use to a headset's eye here
inline constexpr uint32_t kMinW = 640, kMinH = 480;

// `want` within what the runtime holds (`maximum`: both eyes' limits and the system's swapchain limit, 0 = unknown - then
// none, the window's size stays). Squares keep their shape, the headset's recommendation keeps its aspect; both only shrink to
// fit (never past 4096 a side). Even sides, not under 640 x 480. {} = Desktop.
inline Size Fit(Size want, Size maximum) {
    if (!want || !maximum) return {};
    maximum.w = (std::min)(maximum.w, kMaxSide);
    maximum.h = (std::min)(maximum.h, kMaxSide);
    if (want.w > maximum.w || want.h > maximum.h) {   // shrunk to fit, the shape kept (whole numbers: no rounding to chance)
        if ((uint64_t)maximum.w * want.h <= (uint64_t)maximum.h * want.w) want = {maximum.w, (uint32_t)((uint64_t)want.h * maximum.w / want.w)};
        else want = {(uint32_t)((uint64_t)want.w * maximum.h / want.h), maximum.h};
    }
    want = {want.w & ~1u, want.h & ~1u};
    return want.w >= kMinW && want.h >= kMinH ? want : Size{};
}
// The size an older mode ([render] vr_resolution) makes.
inline Size Select(int choice, Size recommended, Size maximum) {
    if (choice <= 0 || choice >= kChoices) return {};
    const uint32_t side = (uint32_t)rendersize::kModeSides[choice >= 2 ? choice - 2 : 0];
    return Fit(choice == 1 ? recommended : Size{side, side}, maximum);
}
// The size the ini's two keys make (render_size.h): Desktop {}, the headset's recommendation, or a square of a side.
inline Size Select(const rendersize::Pick& pick, Size recommended, Size maximum) {
    switch (pick.kind) {
    case rendersize::Kind::Headset: return Fit(recommended, maximum);
    case rendersize::Kind::Square: { const uint32_t s = (uint32_t)rendersize::ClampSide(pick.side); return Fit({s, s}, maximum); }
    default: return {};
    }
}

// One axis: a pixel of a `from` pixels long axis on a `to` pixels long one, the first and the last pixel on
// the first and the last (so the corner of the window is the corner of the game's picture).
inline int Coordinate(int point, uint32_t from, uint32_t to) {
    if (from < 2 || to < 2) return point;
    return (int)std::clamp(std::round((double)point * (double)(to - 1) / (double)(from - 1)), -32768.0, 32767.0);
}
// A point in the window's client area (`client`, real pixels) -> the game's picture (`game`), and back.
inline Pt ToGame(Pt p, Size client, Size game) { return {Coordinate(p.x, client.w, game.w), Coordinate(p.y, client.h, game.h)}; }
inline Pt ToClient(Pt p, Size client, Size game) { return {Coordinate(p.x, game.w, client.w), Coordinate(p.y, game.h, client.h)}; }

// -- the game's own client-size query ([render] vr_resolution_ui; render_resolution.cpp) ------------------------------
// The game asks the window's client size for itself (a function that calls GetClientRect, not through WM_SIZE): its interface
// root, safe area and layout globals, and the clamp of its cursor position. With a render size in force those must see the game's
// picture, not the window: a 3072-wide picture laid out for a 3840-wide desktop loses the interface's right edge, and a cursor
// clamped to a 1440-high window never reaches a button at y 1650 (the main menu's bottom row). bearstonem/d2r-pcvr (MIT) found
// it (steamvr/render_resolution.cpp UiSize / CursorSize, panel_input_mode.h; commits 5107575, de595a6, 448d545, 4c37eb5).
// The interface and the cursor get the whole picture - the game's own layout at the picture's edges, as it had with a window of
// the picture's shape (the square window before the render size). d2r-pcvr gives its interface a 16:9 band at the top instead
// (its toolbar is hidden in VR); with ours shown, the band put the toolbar - orbs and belt - in the MIDDLE of a square picture
// (test-33, 2026-10-15: 2048x1152 of 2048x2048). d2r-pcvr saw the game's text crash once with the full height of a 3072 square
// in a first try; ours had the 1920x1920 window's square for months. Every other caller (movies, the renderer) its own answer.
// [render] ui_band_16x9 (TEST, default off): the interface gets d2r-pcvr's band again - as wide as the picture, 16:9 high, at its top - so on a picture taller
// than 16:9 (a headset's own eye size, 2112x2304) the game lays its panels out as on a wide monitor: the stash or a trader beside the inventory fit across
// (a player's widened inventory ran past both edges of the whole-picture layout, 0.212.3). The toolbar then sits at the band's bottom (test-33's reason).
enum class Asker : uint8_t { Other, Ui, Cursor };
// [render] ui_band_16x9: the panel showing the whole canvas is lowered by this share of its own height so the band at the canvas's top is centred where the
// canvas's centre would have been (d2r-pcvr's native_panels.h UpperBandOffset): the menus stay where the player looks. 0 for a canvas not taller than 16:9.
inline float BandLowerShare(Size canvas) {
    if (!canvas.w || !canvas.h) return 0.0f;
    const float band = std::min((float)canvas.h, (float)canvas.w * 9.0f / 16.0f);
    return 0.5f * ((float)canvas.h - band) / (float)canvas.h;
}
// [render] ui_band_16x9 (0.214.2, Serg: "can the black backing be hidden?"): the menu's panel in front shows only the band - the canvas's top
// rows, as wide as it, 16:9 high (the canvas's own height if it is not taller) - hung where the band hung on the lowered whole canvas (its centre at
// the whole canvas's unlowered one), so nothing of it moves; below it nothing but the game's veil was ever drawn. 0 rows: no canvas.
inline uint32_t BandRows(Size canvas) {
    if (!canvas.w || !canvas.h) return 0;
    return std::clamp((uint32_t)((float)canvas.w * 9.0f / 16.0f + 0.5f), 1u, canvas.h);
}
// ... and the veil the game lays under a menu (a near-black at one coverage, drawn before the menus) taken out by the HUD shader (xr.cpp kHudShader,
// VeilOf / Unveil, g_unveil). The idea is d2r-pcvr's (bearstonem, ui_blit.cpp Undim - its leather sheet); the code here is ours. A pixel of the
// game's interface layer is premultiplied colour over its clear (alpha = the world's share): over the veil (colour B, share Tb) what the game drew
// (C, T) reads C + B*T, share T*Tb. The veil alone is the most see-through sample that is veil at all - near black, neither clear nor solid -
// of the band's corners, side middles and top / bottom middles and, on a canvas taller than the band, 5 points along the last row (the game lays
// nothing out there). The game's veil may not reach all of them (d2r-pcvr saw it cover only the band's left 1920 px of a 3072 canvas): the clear
// ones never count, and what the game drew over the veil is never more see-through than the veil alone. With the square layout ([render]
// menu_unveil) the "band" is the whole canvas: its corners and middles. CPU mirrors of the shader, for the tests.
constexpr int kVeilBandSamples = 8, kVeilBelowSamples = 5, kVeilSamples = kVeilBandSamples + kVeilBelowSamples;
constexpr float kVeilBlack = 0.02f, kVeilLeast = 0.05f, kVeilMost = 0.98f;
// Where sample i is, in pixels of a w x h canvas whose band is `rows` high (BandRows); false for none (no row below a band as tall as the canvas).
inline bool VeilSampleAt(int i, uint32_t w, uint32_t h, uint32_t rows, int* x, int* y) {
    if (!w || !h || !rows) return false;
    const int r = (int)w - 1, b = (int)rows - 1, mid = (int)rows / 2, cx = (int)w / 2;
    const int bx[kVeilBandSamples] = {0, r, 0, r, 0, r, cx, cx}, by[kVeilBandSamples] = {0, 0, b, b, mid, mid, 0, b};
    if (i >= 0 && i < kVeilBandSamples) { *x = bx[i]; *y = by[i]; return true; }
    if (i >= kVeilBandSamples && i < kVeilSamples && rows < h) { *x = (int)(w * (uint32_t)(i - kVeilBandSamples + 1) / 6u); *y = (int)h - 1; return true; }
    return false;
}
// Which rows of a menu's panel the veil is looked for in (the shader's g_unveil), 0 for none: the band in force (on a canvas taller than 16:9) -
// the band's rows; the square layout - the whole canvas, only with [render] menu_unveil (0.214.4, Serg: "the backing off on the square too").
inline uint32_t UnveilRows(Size canvas, bool bandActive, bool squareKey) {
    const uint32_t rows = BandRows(canvas);
    if (!rows) return 0;
    if (bandActive && rows < canvas.h) return rows;
    return squareKey ? canvas.h : 0;
}
// The veil from `n` samples (premultiplied rgb, alpha the world's share): out = its colour and share; false (out untouched) if none is veil.
inline bool VeilOf(const float px[][4], int n, float out[4]) {
    float best[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    for (int i = 0; i < n; ++i) {
        const float through = std::clamp(px[i][3], 0.0f, 1.0f);
        const bool veil = through > kVeilLeast && through < kVeilMost && std::max(px[i][0], std::max(px[i][1], px[i][2])) < kVeilBlack;
        if (veil && through > best[3])
            for (int k = 0; k < 4; ++k) best[k] = k < 3 ? std::max(px[i][k], 0.0f) : through;
    }
    if (best[3] < kVeilLeast) return false;
    for (int k = 0; k < 4; ++k) out[k] = best[k];
    return true;
}
inline float Smooth(float e0, float e1, float x) { const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }
// A pixel (premultiplied rgb, share) with the veil taken out: what the game drew alone - nothing at all where only the veil was. The panels'
// soft drop shadows (black at most ~55 % cover) go with it, as d2r-pcvr found on its sheet: solid black art and anything coloured stay.
inline void Unveil(const float c[4], const float veil[4], float out[4]) {
    const float t = std::clamp(std::clamp(c[3], 0.0f, 1.0f) / veil[3], 0.0f, 1.0f);
    if (t > 1.0f - 1.0f / 512.0f) { out[0] = out[1] = out[2] = 0.0f; out[3] = 1.0f; return; }
    const float opacity = 1.0f - t;
    float rgb[3], top = 0.0f;
    for (int k = 0; k < 3; ++k) { rgb[k] = std::clamp(c[k] - veil[k] * t, 0.0f, opacity); top = std::max(top, rgb[k]); }
    const float keep = std::max(Smooth(0.5f, 0.6f, opacity), Smooth(0.04f, 0.08f, top / std::max(opacity, 1e-6f)));
    for (int k = 0; k < 3; ++k) out[k] = rgb[k] * keep;
    out[3] = 1.0f - opacity * keep;
}
inline Size UiCanvas(Size render, bool band = false) { return band ? Size{render.w, std::min(render.h, render.w * 9u / 16u)} : render; }
// The query's own answer packing: width in the low 32 bits, height in the high (not Pack's).
inline uint64_t NativeSize(Size s) { return (uint64_t)s.w | (uint64_t)s.h << 32; }
inline Size FromNative(uint64_t v) { return {(uint32_t)v, (uint32_t)(v >> 32)}; }
// What the query answers `asker`: `native` is its own answer, `game` the size the game was told (none: no size in force).
inline uint64_t CanvasAnswer(Asker asker, Size game, uint64_t native, bool on, bool band = false) {
    if (!on || !game || asker == Asker::Other) return native;
    return NativeSize(asker == Asker::Ui ? UiCanvas(game, band) : game);   // (the cursor: always the whole picture)
}

// -- the hover label over a world object (its name + the controller button's glyph) -----------------------------------------
// The game's label panel (3.3.93787: 0x87FF00-0x881FD2) keeps a side margin made from the interface's size (0x881380, through the
// UI caller ClientSizeUiRet4: the picture) and lays its parts out across GetWidth() 0x7F510. GetWidth tail-jumps to the query, so
// the query sees the label code's own return address - not one of the interface's - and answered the WINDOW's width. With a big
// square picture in a small window the two parts stacked got max width (window - 2*margin*s - gap*s)/2 - padding < 0, and the
// game's text wrap (D2RLoader+0x906620) never ends with a negative width: every line "does not fit", forever (crash reports
// 230, 247, 259, 260, 269; dumps of 259/260/269, 2026-11-06). The label code's GetWidth callers are interface callers now
// (vr/vrcam.cpp kLabelWidthRets): the margin and the width come from the same picture.
//
// The game's arithmetic for the stacked parts' max width (0x880F7E-0x88100A), here to be tested: the margin and the gap scaled
// by the interface's scale and truncated, the half rounded toward zero, the part's left and right padding taken off.
inline int32_t HoverLabelPartMaxWidth(int32_t width, int32_t margin, int32_t gap, float scale, int32_t padLeft, int32_t padRight) {
    const int32_t m = (int32_t)((float)margin * scale), g = (int32_t)((float)gap * scale);
    const int32_t half = (width - 2 * m - g) / 2;
    return half - padRight - padLeft;
}
// The safety net at the label part's SetMaxSize (0x8DACE0, vr/vrcam.cpp HookLabelSetMaxSize): a width the wrap cannot end with
// (<= 0) becomes "none" - what the part's constructor sets (0x7FFFFFFF) - the label is laid out on one line instead of hanging
// the game. View only: nothing of the game's state but this label's layout.
inline constexpr int32_t kLabelNoMaxWidth = 0x7FFFFFFF;
inline int32_t LabelMaxWidth(int32_t width) { return width <= 0 ? kLabelNoMaxWidth : width; }

// -- when to ask ------------------------------------------------------------------------------

inline constexpr uint64_t kAwayMs = 30000;      // the headset away this long: the window's size is given back
inline constexpr uint64_t kApplyMs = 500;       // the game's own Apply: asked again this long after the last one
inline constexpr uint64_t kSettleMs = 4000;     // a size the game's output has not taken in this time: asked again
inline constexpr int kMaxTries = 2;             // ... this many times, then given up until something changes
inline constexpr uint64_t kDebounceMs = 700;    // a wish (the settings' slider moving) must stay the same this long before the game is asked

struct Inputs {
    uint64_t now = 0;      // ms
    bool running = false;  // the headset's session runs
    bool hero = false;     // the hero has been in a world a while (the game dies when its screen grows while it builds one)
    Size want;             // Select(...) - {} = Desktop, or the limits not known, or no hooks to map the pointer by
    Size output;           // the size of the game's output (back buffer) now, {} = not known
};
enum class Note { None, Retry, GaveUp };
struct Decision {
    Size want;             // the size the game is to have now, {} = the window's own
    bool force = false;    // send it again even if it is the one the game was told last (Apply reset things behind our back)
    Note note = Note::None;
};

class Controller {
    std::atomic<uint64_t> inForce_{0};     // the size asked for and kept
    std::atomic<uint64_t> reapplyAt_{0};   // ms, 0 = none
    std::atomic<bool> reset_{false};
    uint64_t awaySince_ = 0;               // (the draw thread's)
    uint64_t cand_ = 0, candSince_ = 0, stable_ = 0;   // the wish as it is now, since when, and the one that has stayed
    uint64_t watched_ = 0, since_ = 0;
    int tries_ = 0;
    bool gaveUp_ = false;

public:
    // Every present, on the draw thread.
    Decision Step(const Inputs& in) {
        Decision d;
        Size want;
        if (reset_.exchange(false)) { awaySince_ = 0; cand_ = stable_ = candSince_ = 0; watched_ = 0; since_ = 0; tries_ = 0; gaveUp_ = false; }
        // The wish counts once it has stayed the same for kDebounceMs: the settings' slider writes the ini at every step of a drag,
        // and the game's picture (and ReShade's effects) are rebuilt once, after the user stops - not at every step.
        if (Pack(in.want) != cand_) { cand_ = Pack(in.want); candSince_ = in.now; }
        if (in.now - candSince_ >= kDebounceMs) stable_ = cand_;
        const Size wish = Unpack(stable_);
        if (!wish) {
            inForce_.store(0);
        } else if (in.running) {
            awaySince_ = 0;
            if (inForce_.load() || in.hero) { inForce_.store(Pack(wish)); want = wish; }
        } else if (inForce_.load()) {   // the headset is away: not an end of it - taking it off must not cost two resizes
            if (!awaySince_) awaySince_ = in.now;
            if (in.now - awaySince_ >= kAwayMs) inForce_.store(0);
            else want = Unpack(inForce_.load());
        }
        d.want = want;
        uint64_t at = reapplyAt_.load();
        if (!want) {
            reapplyAt_.store(0);
        } else if (at && in.now >= at && reapplyAt_.compare_exchange_strong(at, 0)) {
            d.force = true;
            tries_ = 0; gaveUp_ = false; since_ = in.now;
        }
        if (!want) {
            watched_ = 0; tries_ = 0; gaveUp_ = false;
        } else {
            if (Pack(want) != watched_) { watched_ = Pack(want); since_ = in.now; tries_ = 0; gaveUp_ = false; }
            if (in.output == want) { tries_ = 0; since_ = in.now; gaveUp_ = false; }
            else if (in.output && !gaveUp_ && in.now - since_ >= kSettleMs) {
                since_ = in.now;
                if (tries_ < kMaxTries) { ++tries_; d.force = true; d.note = Note::Retry; }
                else { gaveUp_ = true; d.note = Note::GaveUp; }
            }
        }
        return d;
    }
    // The game's own graphics Apply (any thread): the size is asked for again once it has settled - after the
    // LAST of several Applies. Not while no size is kept.
    void OptionsApplied(uint64_t now) { if (inForce_.load()) reapplyAt_.store(now + kApplyMs); }
    // The session is gone (or the game is closing; any thread): no size is kept, and the next Step starts afresh.
    void Ended() { inForce_.store(0); reapplyAt_.store(0); reset_.store(true); }
    Size InForce() const { return Unpack(inForce_.load()); }
};

// -- [render] menu_wide: the game's own menus at 16:9 -------------------------------------------------------------------
// The game lays its menus out for the shape of its picture. A square picture (the square window, or a square / the headset's
// render size) got the main menu squeezed: its two side panels pushed into the middle over the hero, the bottom buttons cut off
// behind them (Serg, 2026-10-28). bearstonem/d2r-pcvr (MIT) gives its interface a 16:9 band at the top of the square
// (steamvr/vr_resolution.h UiCanvas) and shows the whole square; ours lays the interface over the whole picture in a game
// (UiCanvas above, test-33), so here the PICTURE is 16:9 while the game shows its menus: as wide as the render size (or the
// window), 9/16 of that high - the flat picture in the headset takes the picture's shape, a 16:9 panel. In a game the size is the
// one picked, as before.
// When: the menus' scene (its biome, `frontend` from vrcam) has stood a while - never while the game builds a screen; that size
// is kept through the load into a game (the biome goes at once, the hero is not there yet) until the hero has stood in the world,
// as the render size always was. Off: as before.
// Not under ReShade: a player's 0.186 (ReShade, SteamVR, 2026-10-29 00:19:02) froze on the character screen at this resize - the game's
// ResizeBuffers to 2240x1260 went in on the draw thread and never came back (ReShade.log: no "Destroyed runtime environment" after it,
// 0 presents from then on). The render size's own resize in a game (the hero stood 3 s) has run under ReShade for many releases; one of
// the menus' scene had killed the game before (2026-10-11, ReShade then always on). So the 16:9 menus only with [render] no_reshade
// (the plugin's own D3D12 hooks, Serg's way), where they were tried.
inline bool MenuWideApplies(bool key, bool reshade) { return key && !reshade; }
inline constexpr uint32_t kWideW = 16, kWideH = 9;
// `base` (the render size picked, else the window's) as a 16:9 picture of its width; {} when it is not higher than 16:9.
inline Size Wide(Size base) {
    if (!base || (uint64_t)base.h * kWideW <= (uint64_t)base.w * kWideH) return {};
    const Size s{base.w & ~1u, (uint32_t)((uint64_t)base.w * kWideH / kWideW) & ~1u};
    return s.w >= kMinW && s.h >= kMinH ? s : Size{};
}
// What the Controller is given: the size wished for and whether it may be asked for now (its `hero`).
struct Ask {
    Size want;
    bool now = false;
};
class MenuWide {
    Size held_;              // the menus' size, kept through a load into a game
    uint64_t heldSince_ = 0;  // ms: wished for since

public:
    // `now` ms (the Controller's), `on` the key, `frontend` the menus' scene has stood a while, `hero` the hero has stood in a world a
    // while, `sel` the size picked ({} = the window's own), `wide` Wide(...) of it, `inForce` the size the Controller keeps now.
    Ask Step(uint64_t now, bool on, bool frontend, bool hero, Size sel, Size wide, Size inForce) {
        if (!on || hero) { held_ = {}; return {sel, hero}; }
        if (frontend && wide) {
            if (held_ != wide) { held_ = wide; heldSince_ = now; }
            // asked for only once it has stayed the Controller's kDebounceMs: before that the Controller still holds the wish of
            // before as its settled one, and "ask now" let THAT through - the headset's 2496x2688 asked for in the main menu,
            // 0.7 s before its 16:9 (Serg's log 2026-10-28 15:01:54), a tall picture while the menus stand
            return {wide, now - heldSince_ >= kDebounceMs};
        }
        // between the two (a load into a game, the title screen): the menus' size only while it is the one in force - one
        // still waiting out the Controller's 0.7 s is not asked for once the menus are gone
        if (held_ && held_ == inForce) return {held_, true};
        held_ = {};
        return {sel, false};
    }
    Size Held() const { return held_; }
};

// A size the runtime refuses (its swapchains could not be made): the last size it took instead - or the window's own
// size when there was none. Told once; remembered until the session ends.
class Fallback {
    Size good_, refused_;

public:
    void Took(Size s) { if (s) good_ = s; }
    // True the first time a size is refused (to be said once).
    bool Refused(Size s) {
        if (!s || s == refused_) return false;
        refused_ = s;
        return true;
    }
    Size Apply(Size want) const { return want && want == refused_ ? good_ : want; }
    Size Good() const { return good_; }
    void Reset() { good_ = refused_ = {}; }
};

}  // namespace vrres
