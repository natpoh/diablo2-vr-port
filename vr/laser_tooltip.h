// laser_tooltip.h - the laser's item tooltips at once: the pure part (no game calls, no globals but the
// lease's own atomic). [openxr] laser_tooltips; docs/plan_laser_menus.md, step 4; vr/panel_input.cpp hooks.
//
// The idea is bearstonem/d2r-pcvr's (MIT; its steamvr/panel_input_mode.h ItemTooltipHover and
// ItemCompareKey, and vr/panel_input.cpp), written again here for our laser.
//
// In the pad's layout the game shows an item's tooltip 1000 ms after the focus came to it, and the
// equipped-item comparison next to it only while Shift is held. With the laser those two waits are
// the player's, not the game's: the pointer is on the item. Both are the game's own decisions made at
// two places in the item tooltip's producer, each by a query:
//   - the tooltip's style is queued with a "delayed" flag, which is the answer of IsControllerLayout
//     asked at one call site: false there and the tooltip is not delayed;
//   - the comparison asks the key-state query for VK_SHIFT at one call site: "down" there and the
//     comparison is shown.
// Nothing else changes: the game still decides whether there is an item, whether it has an equipped
// counterpart, whether the tooltip is suppressed. Other callers of both queries get the native answer,
// and so does every call when the laser does not hover (the lease below is out). No key is sent: Shift
// also buys stacks and moves items.
#pragma once

#include <atomic>
#include <cstdint>

namespace lasertip {

constexpr int kVkShift = 0x10;
// The hover is refreshed every laser frame; this long without one and it is over, whatever else
// happened (the draw thread stopped, the session ended).
constexpr uint64_t kLeaseMs = 300;

// This frame: the laser is the mouse (not parked for the pad), points at the picture and holds no
// button (a held button is a click or a drag, not a hover; the fork ends the hover on a drag too).
// Whether an item is under the pointer is the game's to say: it asks the two queries only for one.
// `wanted` is the tick box.
constexpr bool Hovering(bool wanted, bool laserActs, bool onPicture, bool dragging) {
    return wanted && laserActs && onPicture && !dragging;
}

// The hover's lease. The draw thread updates it every frame; the game's UI thread asks.
class HoverLease {
    std::atomic<uint64_t> until{0};
public:
    // A frame that does not hover ends it at once - it must not inherit the lease of a frame before.
    void Update(uint64_t now, bool hovering) { until.store(hovering ? now + kLeaseMs : 0, std::memory_order_release); }
    bool Active(uint64_t now) const {
        const uint64_t end = until.load(std::memory_order_acquire);
        return end && now < end;
    }
};

// The hooks tell the two sites apart by the caller's return address; a site not found (0) is never one.
constexpr bool AtSite(uintptr_t caller, uintptr_t site) { return site && caller == site; }

// IsControllerLayout asked for the tooltip's delay flag: "no" while the laser hovers.
constexpr bool NoDelay(bool hover, bool atDelaySite) { return hover && atDelaySite; }

// The key-state query asked by the comparison: Shift is "down" while the laser hovers; every other key,
// and Shift asked anywhere else, is the native state.
constexpr bool ShiftDown(bool hover, bool atCompareSite, int vk) { return hover && atCompareSite && vk == kVkShift; }

}  // namespace lasertip
