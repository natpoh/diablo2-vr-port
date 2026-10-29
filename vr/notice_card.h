// notice_card.h - [hud] notice_card: the game's quest and level-up notice ("Quest log updated", "You have new skill points") on a card in the
// room in front of the player, for a few seconds, in VR first person (F4) on native OpenXR. Pure: the box, the card's timing and the state
// the parts share; no game calls (cleanroom/tests/notice_card_test.cpp).
//
// Why it was not seen at all. The game shows the notice in its NotificationPanel (data\global\ui\Layouts\NotificationPanelHD.json, read with
// tools\casc_extract): anchored at the interface area's bottom middle (anchor 0.5, 1), rect 0, -500, 1200 x 244 - a box 256 to 500 layout units
// over the area's bottom edge, its middle on the area's. [hud] hide = 1 empties the toolbar's strip, 464 units up from that edge (vr/hud_rects.h),
// so the box lost everything but a 36-unit rim at its top, text and all; the copy of the strip went to the toolbar in the room - nowhere at all
// with the wrists ([hud] bar_place 5). It is not a menu (gamestate IsMenuPanel), so [openxr] menu_3d never took it either.
//
// What this does, in the order a frame goes:
//   1. vr/gamestate.cpp, on the UI thread: the panel opening ('PanelManager' 'OpenPanel' "NotificationPanel") and each message sent to it
//      ('NotificationMessage' ..., e.g. 'QuestUpdate') count a poke (Pokes); whether the panel is open (PanelOpen); its rect from the widget service.
//   2. vr/vrcam.cpp, every frame: in force or not (ticked, F4, a game, no menu, native OpenXR eyes: SetActive) and, while the panel is open, where
//      its box is for the cut (hud::SetNoticeCut).
//   3. vr/hud.cpp, when the game leaves the interface layer: the box (BoxPx) copied out and emptied in the layer, before the toolbar's strip is -
//      the game's own picture of the notice, in its language, with its own button glyph.
//   4. vr/xr.cpp: that copy as a quad in the room (Card: when, how long, how faded; placed where the head looks when it first comes up).
// The game's panel is never closed, hidden or touched: its own timer (10 s) and its Start button stay the game's. Unticked: nothing is cut.
//
// The idea - the notice on a small card standing in the room for a few seconds, the game's panel kept open but not at the eyes - is
// bearstonem/d2r-pcvr's (MIT, its commit a76e12c, "Show quest and level-up notices on a card in the room"). His paints the game's string
// on a card of its own; this one shows the game's own picture of the notice.
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace noticecard {

// ---- where the notice is in the interface layer ------------------------------------------------------------------------------
// The layout's rect (x, y off the anchor; width, height), layout units; the box's middle is the anchor's x plus x.
constexpr float kLayoutRect[4] = {0.0f, -500.0f, 1200.0f, 244.0f};
constexpr float kMarginUnits = 8.0f;    // round the rect: the border's art and its shadow
constexpr uint32_t kStepPx = 32;        // the box grown to whole steps about its middle: one texture size while the layer's size and scale stay

struct Box { int32_t left = 0, top = 0, right = 0, bottom = 0; };

// A rect the widget service gave (x, y, width, height) is used when it is the layout's own - near its place, about its size (another game
// build may move it a little); anything else (not read, a stale or empty rect) gives the layout's.
inline bool SaneRect(const int32_t r[4]) {
    if (!r) return false;
    const float w = static_cast<float>(r[2]), h = static_cast<float>(r[3]);
    return std::fabs(static_cast<float>(r[0]) - kLayoutRect[0]) <= 200.0f && std::fabs(static_cast<float>(r[1]) - kLayoutRect[1]) <= 200.0f &&
           w >= 0.6f * kLayoutRect[2] && w <= 1.6f * kLayoutRect[2] && h >= 0.6f * kLayoutRect[3] && h <= 1.6f * kLayoutRect[3];
}

// The notice's box in pixels of a w x h layer. s: the game's interface scale, pixels a layout unit (0 or nonsense: the layer's own, h / 2160).
// The interface area is the layer scaled by the Safe Screen Percent (s * 2160 / h), in the middle; the panel hangs from its bottom middle.
// fromWidget: the widget's rect was used. False: no layer, or the box falls outside it.
inline bool BoxPx(const int32_t widget[4], float s, uint32_t w, uint32_t h, Box* out, bool* fromWidget = nullptr) {
    if (!out || !w || !h || w > 32768u || h > 32768u) return false;
    const float W = static_cast<float>(w), H = static_cast<float>(h);
    if (!(std::isfinite(s) && s > 0.05f && s < 20.0f)) s = H / 2160.0f;
    const float safe = std::clamp(s * 2160.0f / H, 0.3f, 1.0f);
    const bool use = SaneRect(widget);
    if (fromWidget) *fromWidget = use;
    const float rx = use ? static_cast<float>(widget[0]) : kLayoutRect[0], ry = use ? static_cast<float>(widget[1]) : kLayoutRect[1];
    const float rw = use ? static_cast<float>(widget[2]) : kLayoutRect[2], rh = use ? static_cast<float>(widget[3]) : kLayoutRect[3];
    const float cx = 0.5f * W + rx * s, bottom = H * (0.5f + 0.5f * safe);
    const float l = cx - (0.5f * rw + kMarginUnits) * s, r = cx + (0.5f * rw + kMarginUnits) * s;
    const float t = bottom + (ry - kMarginUnits) * s, b = bottom + (ry + rh + kMarginUnits) * s;
    float bw = std::ceil((r - l) / static_cast<float>(kStepPx)) * static_cast<float>(kStepPx);
    float bh = std::ceil((b - t) / static_cast<float>(kStepPx)) * static_cast<float>(kStepPx);
    bw = std::min(bw, W);
    bh = std::min(bh, H);
    const float x0 = std::clamp(0.5f * (l + r) - 0.5f * bw, 0.0f, W - bw), y0 = std::clamp(0.5f * (t + b) - 0.5f * bh, 0.0f, H - bh);
    if (!(bw >= 1.0f && bh >= 1.0f) || b <= 0.0f || t >= H) return false;
    *out = Box{static_cast<int32_t>(x0), static_cast<int32_t>(y0), static_cast<int32_t>(x0 + bw), static_cast<int32_t>(y0 + bh)};
    return true;
}

// ---- where the card stands ---------------------------------------------------------------------------------------------------
// Ahead of the eyes along the head's heading (level), a little under them, leant back to face them: read at a glance, out of the fight's line.
constexpr float kDistanceM = 1.0f, kDropM = 0.25f, kWidthM = 0.5f;
inline float LeanDeg() { return std::atan2(kDropM, kDistanceM) * 57.29578f; }   // about the card's own x: its face turned up to the eyes

// ---- when, and how faded ---------------------------------------------------------------------------------------------------
constexpr uint64_t kMergeMs = 1500;    // pokes this close are one notice (the message and the panel's opening come within 30 ms)
constexpr uint64_t kFadeMs = 800;      // the fade at the end: the game's own transitionSeconds
constexpr uint64_t kWaitMs = 10800;    // a card not shown by then is dropped: the game's notificationSeconds + its transition (the panel is gone)
constexpr uint64_t kFollowMs = 500;    // a card that follows another this soon keeps its place
constexpr float kShowMinS = 1.0f, kShowMaxS = 10.0f, kShowDefaultS = 4.0f;   // [hud] notice_card_seconds
inline uint64_t ShowMs(float seconds) {
    const float s = std::isfinite(seconds) ? std::clamp(seconds, kShowMinS, kShowMaxS) : kShowDefaultS;
    return static_cast<uint64_t>(s * 1000.0f + 0.5f);
}

struct Frame {
    bool show = false;     // draw the card this frame (the game's panel's copy, if it came)
    float alpha = 0.0f;    // 0..1
    bool place = false;    // put it where the head looks now (a new card, not one that follows another)
    uint32_t seq = 0;      // which card (for the log)
};

// One card at a time, on the draw thread. pokes: gamestate's count; open: the game's panel is open; active: the card may be drawn now.
class Card {
    uint32_t pokes_ = 0;
    bool known_ = false, on_ = false, shown_ = false, place_ = false, ended_ = false;
    uint32_t seq_ = 0;
    uint64_t pokedAt_ = 0, shownAt_ = 0, endedAt_ = 0;
    void End(uint64_t now) {
        if (on_ && shown_) { ended_ = true; endedAt_ = now; }
        on_ = shown_ = place_ = false;
    }

public:
    Frame Step(uint64_t now, uint32_t pokes, bool open, bool active, uint64_t showMs) {
        if (!known_) { known_ = true; pokes_ = pokes; }   // what came before is not a card
        if (pokes != pokes_) {
            pokes_ = pokes;
            if (!on_ || now - pokedAt_ > kMergeMs) {   // a new notice (the game's next message, or one after a gap)
                const bool follows = (on_ && shown_) || (ended_ && now - endedAt_ <= kFollowMs);
                if (on_) End(now);
                on_ = true;
                ++seq_;
                pokedAt_ = now;
                place_ = !follows;
            }
        }
        if (on_ && !open && now - pokedAt_ > kMergeMs) End(now);           // the game's panel closed: nothing to show
        if (on_ && shown_ && now - shownAt_ >= showMs + kFadeMs) End(now);  // its time is over
        if (on_ && !shown_ && now - pokedAt_ > kWaitMs) End(now);           // never shown (a menu the whole time)
        Frame f;
        f.seq = seq_;
        if (!on_) return f;
        const uint64_t t = shown_ ? now - shownAt_ : 0;
        f.alpha = t <= showMs ? 1.0f : std::clamp(1.0f - static_cast<float>(t - showMs) / static_cast<float>(kFadeMs), 0.0f, 1.0f);
        f.show = active && open && f.alpha > 0.0f;
        f.place = place_;
        return f;
    }
    // The card reached the frame: its time starts, its place is set.
    void Shown(uint64_t now) {
        if (on_ && !shown_) { shown_ = true; shownAt_ = now; }
        place_ = false;
    }
    bool On() const { return on_; }
    void Reset() { *this = Card{}; }
};

// ---- the state the parts share (any thread) -------------------------------------------------------------------------------------
struct State {
    std::atomic<bool> wanted{false};        // [hud] notice_card
    std::atomic<uint32_t> showMs{static_cast<uint32_t>(ShowMs(kShowDefaultS))};
    std::atomic<bool> active{false};        // vrcam: in force now
    std::atomic<uint32_t> pokes{0};         // gamestate: the panel opened, or a message came to it
    std::atomic<bool> open{false};          // gamestate: the game's panel is open
};
inline State& S() {
    static State s;
    return s;
}
inline void Configure(bool on, float seconds) {
    S().wanted.store(on);
    S().showMs.store(static_cast<uint32_t>(ShowMs(seconds)));
}
inline bool Wanted() { return S().wanted.load(); }
inline uint64_t ShowMsNow() { return S().showMs.load(); }
inline void SetActive(bool on) { S().active.store(on); }
inline bool Active() { return S().active.load(); }
inline void Poke() { S().pokes.fetch_add(1); }
inline uint32_t Pokes() { return S().pokes.load(); }
inline void SetOpen(bool open) { S().open.store(open); }
inline bool PanelOpen() { return S().open.load(); }

// The ui messages that count (gamestate's OnUiMessage): the panel opening, or any message to it.
inline bool IsPoke(const char* target, const char* command, const char* text) {
    if (!target || !command) return false;
    auto same = [](const char* a, const char* b) {
        for (; *a && *a == *b; ++a, ++b) {}
        return *a == *b;
    };
    if (same(target, "NotificationMessage")) return true;
    return same(target, "PanelManager") && (same(command, "OpenPanel") || same(command, "OpenExclusivePanel")) && text && same(text, "NotificationPanel");
}

}  // namespace noticecard
