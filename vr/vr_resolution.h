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
enum class Asker : uint8_t { Other, Ui, Cursor };
inline Size UiCanvas(Size render) { return render; }
// The query's own answer packing: width in the low 32 bits, height in the high (not Pack's).
inline uint64_t NativeSize(Size s) { return (uint64_t)s.w | (uint64_t)s.h << 32; }
inline Size FromNative(uint64_t v) { return {(uint32_t)v, (uint32_t)(v >> 32)}; }
// What the query answers `asker`: `native` is its own answer, `game` the size the game was told (none: no size in force).
inline uint64_t CanvasAnswer(Asker asker, Size game, uint64_t native, bool on) {
    if (!on || !game || asker == Asker::Other) return native;
    return NativeSize(asker == Asker::Ui ? UiCanvas(game) : game);   // (the same today: the whole picture for both)
}

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
