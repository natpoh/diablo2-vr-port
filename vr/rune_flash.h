#pragma once
// rune_flash.h - the glyph's "done right" flash (Serg, 2026-11-08: "if the glyph is drawn right, it lights up with a bright glow for 100 ms and gets a
// little thicker"): when a drawn gesture is recognised - a cast in play (the gesture engine's PollCasts, or BodyWalk's cast from a gesture), or a take
// accepted while recording (the Gestures tab's SAVED) - every trail and picture the runes layer shows lights up for kFlashMs: its own colour brightened
// with its glow full, its lines kThicker as wide, opaque; then it fades as it would. A stroke not recognised gets nothing. Drawn by vr/xr_runes.h over
// whatever source the runes come from (BodyWalk's VrRunes or the engine's Trail); F4 only as the layer is. No tick: part of the gestures' feedback.
//
// Pure apart from the one shared moment (Trigger / the layer's Apply may be on different threads: an atomic).

#include <atomic>
#include <cmath>
#include <cstdint>

#include "skill_link_abi.h"   // (bodywalk/game_link.h: BW_LinkVrRune)

namespace runeflash {

constexpr uint64_t kFlashMs = 100;
constexpr float kThicker = 1.4f;
constexpr float kBright = 1.7f;     // the colour's light times this (in the bytes' scale, then clamped)
constexpr float kWhite = 0.25f;     // ... and a quarter of the way to white

// A colour (R in the low byte, alpha in the high) brightened in its own hue, opaque.
inline uint32_t Brighter(uint32_t rgba) {
    uint32_t out = 0xFF000000u;
    for (int k = 0; k < 3; ++k) {
        float c = float((rgba >> (8 * k)) & 0xFF) * kBright;
        c = c + (255.0f - c) * kWhite;
        if (c > 255.0f) c = 255.0f;
        out |= uint32_t(c + 0.5f) << (8 * k);
    }
    return out;
}
inline float Wider(float w) { return std::isfinite(w) && w > 0.0f ? w * kThicker : w; }

// The flash's moment: 0 none. now - at < kFlashMs: on.
class Flash {
    std::atomic<uint64_t> at_{0};

public:
    void Trigger(uint64_t nowMs) { at_.store(nowMs ? nowMs : 1, std::memory_order_relaxed); }
    bool On(uint64_t nowMs) const {
        const uint64_t at = at_.load(std::memory_order_relaxed);
        return at && nowMs >= at && nowMs - at < kFlashMs;
    }
    void Reset() { at_.store(0, std::memory_order_relaxed); }
    // A rune as the layer draws it this moment: lit up while the flash is on, untouched otherwise.
    void Apply(BW_LinkVrRune& r, uint64_t nowMs) const {
        if (!On(nowMs)) return;
        r.trailRgba = Brighter(r.trailRgba);
        r.litRgba = Brighter(r.litRgba);
        r.runeRgba = Brighter(r.runeRgba);
        r.glow = 1.0f;
        r.fade = 1.0f;
        r.trailWidth = Wider(r.trailWidth);
        r.lineWidth = Wider(r.lineWidth);
    }
};

// The one the plugin uses (Trigger from the casts' and the tab's threads, Apply in the runes layer).
inline Flash& Shared() {
    static Flash f;
    return f;
}

}  // namespace runeflash
