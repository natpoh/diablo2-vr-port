#pragma once
// hand_swap.h - [hands] left_handed (Serg, 2026-11-05: "в меню управления для вр должна быть галочка поменять контроллеры местами для левшей").
//
// For a left-handed player the two controllers trade places, ONCE, where they enter the plugin: xr.cpp's Publish (the game's own OpenXR session),
// vr/input/pad.cpp's FlatVR raw feed and link.cpp's BodyWalk tracking under FlatVR. Hand 1 ("right") is then the LEFT controller: it carries the
// game's main (right-hand) weapon and strikes with it, its trigger, stick and buttons do what the right controller's did; hand 0 ("left", the off
// hand: shield, second weapon, the bow's hand) is the RIGHT controller. Everything after that point - direct input's layout and gestures, touch
// strikes, the weight model, the bow and crossbow, throws, grips, the belt, the inventory on the hand, the map in the hand, the wrist orbs, the
// haptics' events, BodyWalk's walking and skill gestures - reads the swapped hands and needs nothing of its own. The game's mechanics are not
// touched: it still has its right and left weapon, only which physical controller drives which.
//
// What stays the PHYSICAL controller's:
//   - the menu button (Kept): on Quest / Touch it is only on the left controller, on PS VR2 the runtime's "left menu" is the RIGHT Options button
//     (memory psvr2-touch-menu-is-right-options). Mirrored it would land on a controller with none; so it stays where the runtime put it.
//   - the plugin's own XrSpace lookups (the laser, the map's laser, the inventory and the big map on the hand, the headset panel's pointers) and
//     the haptic output: Phys(logical) names the controller.
//   - a controller frame's handedness: an OpenXR grip's +X is out of the LEFT palm but into the RIGHT one, so an offset or a palm normal read
//     from the controller's own axes takes the physical side (xr.cpp ForearmFromHand / the orb fallback; the wrist panels' palm test).
//     The hero's own bones keep the logical side: his right hand in the left controller is posed as a right hand (thumb up, edge forward).
//   - the dead zones ([direct] stick_deadzone_left/right_pct): stick drift is the stick's own.
//
// VR F4 only (vr/view_gate.h): in F1 - F3, the flat game and the menus outside F4 the hands are as before (BodyWalk's Mapping has its own).
// Pure apart from two atomics; the unit test is cleanroom/tests/hand_swap_test.cpp.

#include <atomic>
#include <cstdint>
#include <utility>

#include "d2r_vr_shared.h"   // D2RVR_XrHand, D2RVR_Shared

namespace handswap {

// The ini's wish ([hands] left_handed), set by load_settings.
inline std::atomic<bool>& WantedSlot() {
    static std::atomic<bool> s{false};
    return s;
}
inline void SetWanted(bool on) { WantedSlot().store(on, std::memory_order_relaxed); }
inline bool Wanted() { return WantedSlot().load(std::memory_order_relaxed); }

// Swapped this frame: the wish, in VR F4.
constexpr bool Decide(bool wanted, bool f4) { return wanted && f4; }

// The verdict of the last frame that came in (xr.cpp Publish / the FlatVR raw feed), for the readers that name a physical controller after it.
inline std::atomic<bool>& NowSlot() {
    static std::atomic<bool> s{false};
    return s;
}
inline void SetNow(bool swapped) { NowSlot().store(swapped, std::memory_order_relaxed); }
inline bool Now() { return NowSlot().load(std::memory_order_relaxed); }

// BodyWalk's tracking (link.cpp Head): under FlatVR BodyWalk has the headset and sends the controllers as they are, so it is traded there; under
// the game's own session BodyWalk's input is OUR XrInput ("Game Link", its game_link.cpp), already traded - not a second time. xr.cpp's Publish
// notes when it last ran (GetTickCount64 ms); a note younger than half a second is a native session.
inline std::atomic<uint64_t>& NativeAtSlot() {
    static std::atomic<uint64_t> s{0};
    return s;
}
inline void NoteNative(uint64_t nowMs) { NativeAtSlot().store(nowMs, std::memory_order_relaxed); }
constexpr bool NativeFresh(uint64_t atMs, uint64_t nowMs) { return atMs != 0 && nowMs >= atMs && nowMs - atMs < 500; }
constexpr bool MirrorBodyWalk(bool wanted, bool f4, bool nativeFresh) { return Decide(wanted, f4) && !nativeFresh; }

// The physical controller (0 left, 1 right) a logical hand reads.
constexpr int PhysOf(int logical, bool swapped) { return swapped ? 1 - (logical & 1) : (logical & 1); }
inline int Phys(int logical) { return PhysOf(logical, Now()); }

// The two hands of a frame traded (poses, trigger, grip, stick, buttons, active), and their touch bits with them; `kept` button bits
// (the menu button) stay on the controller that pressed them. `touch` may be null.
inline void MirrorHands(D2RVR_XrHand hand[2], uint32_t touch[2], uint32_t kept) {
    const uint32_t k0 = hand[0].buttons & kept, k1 = hand[1].buttons & kept;
    std::swap(hand[0], hand[1]);
    hand[0].buttons = (hand[0].buttons & ~kept) | k0;
    hand[1].buttons = (hand[1].buttons & ~kept) | k1;
    if (touch) std::swap(touch[0], touch[1]);
}

// The same for the shared block's hands (BodyWalk's tracking under FlatVR): right <-> left place, turn, grip and their valid bits
// (bit 0 right, bit 1 left).
inline void MirrorShared(D2RVR_Shared* s) {
    if (!s) return;
    for (int i = 0; i < 3; ++i) std::swap(s->rightHand[i], s->leftHand[i]);
    for (int i = 0; i < 4; ++i) std::swap(s->rightRot[i], s->leftRot[i]);
    std::swap(s->rightGrip, s->leftGrip);
    const uint32_t v = s->handsValid;
    s->handsValid = (v & ~3u) | ((v & 1u) << 1) | ((v & 2u) >> 1);
}

}  // namespace handswap
