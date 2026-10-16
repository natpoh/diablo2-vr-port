#pragma once
// anchor_gate.h - [openxr] menus_wait_tracking: the flat picture (menus, loading, the title) and a menu's panel are placed in the
// room only once the headset is really tracked, and placed again in front of you when the input focus comes back after a while.
//
// A runtime can hand out views before the headset is tracked - its reference space's origin, or a pose it only guesses - and a
// picture placed then stays at the floor or behind the player (2026-10-10: "it came up behind the player"; until now the first
// 45 frames simply followed the gaze). Now the placing waits for views whose position AND orientation are tracked, with the
// session focused; until then the picture follows the gaze. A runtime that never says "tracked" (a 3-DoF headset) is given up on
// after kFallbackMs and placed as before. After the focus has been away for kRefocusMs or more (the headset taken off, the
// runtime's own menu) the picture and the panel come up in front of the head again - for a player who sat down or stood up
// meanwhile; a shorter loss (PS VR2's runtime takes the focus for a moment, again and again) moves nothing.
// Pure: no OpenXR call, no clock of its own. After bearstonem/d2r-pcvr (MIT): steamvr/native_panels.h CanAnchor,
// docs/startup_menu_placement.md (b0d0be9).
#include <cstdint>

namespace anchorgate {

constexpr uint64_t kFallbackMs = 3000, kRefocusMs = 2000;

// May a picture be placed (anchored) in the room now? Step once a frame with what the runtime says.
class Ready {
    uint64_t since_ = 0;   // when the views were first seen not tracked / not focused (0: they are)
public:
    bool Step(bool tracked, bool focused, uint64_t nowMs) {
        if (tracked && focused) { since_ = 0; return true; }
        if (!since_) since_ = nowMs ? nowMs : 1;
        return nowMs >= since_ && nowMs - since_ >= kFallbackMs;
    }
    void Reset() { since_ = 0; }
};

// True once, on the frame the focus comes back after having been away for kRefocusMs or more (not at the first focus).
class Refocus {
    bool was_ = false, ever_ = false;
    uint64_t lostAt_ = 0;
public:
    bool Step(bool focused, uint64_t nowMs) {
        bool again = false;
        if (focused && !was_ && ever_ && lostAt_ && nowMs >= lostAt_ && nowMs - lostAt_ >= kRefocusMs) again = true;
        if (!focused && was_) lostAt_ = nowMs ? nowMs : 1;
        if (focused) { ever_ = true; lostAt_ = 0; }
        was_ = focused;
        return again;
    }
    void Reset() { *this = Refocus{}; }
};

}  // namespace anchorgate
