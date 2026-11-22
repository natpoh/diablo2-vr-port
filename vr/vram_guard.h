#pragma once
// [render] vram_guard (off = as before): the render size lowered when the game's video memory runs over the budget Windows gives it.
//
// Crash report 400 (0.221.0, RTX 3070 Ti 8 GB, Oculus OpenXR, 2026-11-21): the slider at 4096 - the game asked for 4096x2304 in the menus
// ([render] menu_wide) and 4096x4096 in a game. Its 10 s lines read "video memory 5915 of 6259 MiB" at the first one in the world, then 7640 of
// 6131, 7854 of 6131 ... 8612 of 5972 - the process over its budget (QueryVideoMemoryInfo, the local segment) for a minute, the game asking its
// budget thousands of times every 10 s, 5-20 presents a second (paging), and then DXGI_ERROR_DEVICE_HUNG. The same card sent 270, 271 and 329
// before (329: 5733 of 5730). Nothing in the plugin looked at the budget: the size picked was asked for whatever the card held.
//
// The guard: the usage and the budget read every couple of seconds; over the budget kOverSamples readings in a row while a render size of ours
// is in force -> a cap on the picture's long side, one step under the size in force or more (as far as the reading says the pixels must
// shrink to bring the usage to kAim of the budget, on the steps players know from the slider), never under kFloor. After a step it waits
// kQuietMs (the game frees the old size's buffers, Windows moves the budget) before it judges again. The cap holds until the size picked
// changes (the player moves the slider or ticks the headset's size) - no stepping back up by itself: each resize is a rebuild of the game's
// picture, and a size that ran out once will run out again.
//
// No Windows in here: time and the readings are handed in, so tools/vram_guard_test.cpp drives it. vr/render_resolution.cpp keeps one,
// fed from the 10 s line's thread (vr/render/effects/effects.cpp), and applies Cap() to the size picked.

#include <cmath>
#include <cstdint>

#include "vr_resolution.h"

namespace vrres {
namespace vramguard {

inline constexpr uint32_t kSteps[] = {4096, 3584, 3072, 2816, 2560, 2304, 2048, 1792, 1536};   // a long side, high to low
inline constexpr uint32_t kFloor = 1536;          // never lower than this (the hang advice's lowest step too, shared/hang_advice.h)
inline constexpr int kOverSamples = 3;            // readings over the budget in a row before a step
inline constexpr uint64_t kSampleMs = 2000;       // how often the 10 s line's thread reads with the guard on
inline constexpr uint64_t kQuietMs = 15000;      // after a step: no judging for this long
inline constexpr double kAim = 0.85;              // a step aims the usage at this share of the budget
inline constexpr uint32_t kNear = 32;             // a side within this of a step is that step

inline uint32_t LongSide(Size s) { return s.w > s.h ? s.w : s.h; }

// The cap for a picture of long side `side` whose process uses `usage` of `budget`: the largest step under `side` (by more than kNear) that is
// not above side x sqrt(kAim x budget / usage) - the pixels shrunk by the share the usage must lose (all of it as if the picture made it: the
// rest does not shrink, so a second step may follow). Under kFloor: kFloor if that is still a step down; 0 = no step left.
inline uint32_t Target(uint32_t side, uint64_t usage, uint64_t budget) {
    if (!side || !budget || !usage) return 0;
    const double want = (double)side * std::sqrt(kAim * (double)budget / (double)usage);
    for (const uint32_t s : kSteps)
        if (s + kNear < side && (double)s <= want && s >= kFloor) return s;
    return kFloor + kNear < side ? kFloor : 0;
}

// The size picked with the cap on its long side (its shape kept, even sides); the size as it was with no cap.
inline Size Capped(Size sel, uint32_t cap) {
    if (!sel || !cap || LongSide(sel) <= cap) return sel;
    return Fit(sel, {cap, cap});
}

enum class Event { None, Lowered, AtFloor };
struct Step {
    Event event = Event::None;
    uint32_t from = 0, to = 0;     // the long side in force, and the cap now (Lowered)
    uint64_t usage = 0, budget = 0;
};

class Guard {
    uint32_t cap_ = 0;
    int over_ = 0;
    uint64_t quietUntil_ = 0;
    bool floorTold_ = false;

public:
    // A reading (bytes; `inForce` the size the game is asked for now by us, {} = none - the window's own size, nothing to lower).
    Step Sample(uint64_t now, uint64_t usage, uint64_t budget, Size inForce) {
        Step st;
        if (!budget || !inForce) { over_ = 0; return st; }
        if (now < quietUntil_) return st;
        if (usage > budget) ++over_;
        else { over_ = 0; return st; }
        if (over_ < kOverSamples) return st;
        over_ = 0;
        const uint32_t side = LongSide(inForce);
        const uint32_t to = Target(side, usage, budget);
        st.usage = usage;
        st.budget = budget;
        st.from = side;
        if (!to) {
            if (!floorTold_) { floorTold_ = true; st.event = Event::AtFloor; }
            quietUntil_ = now + kQuietMs;
            return st;
        }
        cap_ = to;
        quietUntil_ = now + kQuietMs;
        st.event = Event::Lowered;
        st.to = to;
        return st;
    }
    uint32_t Cap() const { return cap_; }
    // The size picked changed (or the guard was switched off): no cap, judged afresh.
    void Reset() { cap_ = 0; over_ = 0; quietUntil_ = 0; floorTold_ = false; }
};

}  // namespace vramguard
}  // namespace vrres
