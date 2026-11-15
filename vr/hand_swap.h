#pragma once
// hand_swap.h - [hands] left_handed: the two thumbsticks traded - their movement (the LEFT stick turns, the RIGHT stick walks), their clicks and
// long presses - and nothing else.
//
// Serg, 2026-11-15: on the 0.218.0 candidate "когда я включаю левая раскладка руки меняются местами ... а должен поменяться только оутпут на
// стики"; on 0.218.1 (sticks and buttons traded) "руки вернулись и не смешиваются но я думаю что кнопки не надо путать, пусть будут такие же как
// на правой раскладке, а менять местами только стики" - "и зажатия стиков". So with left_handed=1 (VR F4) the two sticks trade
// jobs in the game: what the right stick did - its push (the turn), its click and long press (the skill panel) - the left stick does, and the
// right stick walks with the left stick's click. Every other button is as with left_handed=0 - the triggers, the grips, X / Y, A / B, the menu.
// Each controller stays its own hand: the arms, the weapon, grabs, the bow, the laser, the panels, the HUD on the hands and the vibration are
// untouched.
//
// (0.202 - 0.218 traded the whole controllers and 0.217.2 drew the hero left-handed on top, [hands] left_handed_mirror; 0.218.1 traded the
// sticks and the buttons; 0.218.2's first cut the sticks' axes alone. All of that is gone.)
//
// Where: direct input's layout - the two sticks traded right before the layout reads them (direct_input.cpp Feed: the game's own OpenXR
// session and FlatVR's raw controllers alike); and BodyWalk's copy of the controllers when its Mapping is the input (link.cpp PublishXr). The
// dead zones ([direct] stick_deadzone_left/right_pct) are the physical stick's. VR F4 only
// (vr/view_gate.h). Pure apart from two atomics; the unit test is cleanroom/tests/hand_swap_test.cpp.

#include <atomic>
#include <cstdint>
#include <utility>

#include "d2r_vr_shared.h"   // D2RVR_XrHand
#include "direct_layout.h"   // directinput::layout::Controls

namespace handswap {

// The ini's wish ([hands] left_handed), set by load_settings.
inline std::atomic<bool>& WantedSlot() {
    static std::atomic<bool> s{false};
    return s;
}
inline void SetWanted(bool on) { WantedSlot().store(on, std::memory_order_relaxed); }
inline bool Wanted() { return WantedSlot().load(std::memory_order_relaxed); }

// Traded this frame: the wish, in VR F4.
constexpr bool Decide(bool wanted, bool f4) { return wanted && f4; }

// The verdict of direct input's last frame (Feed): a change of it is a new context there - a stick held across it is held until let go.
inline std::atomic<bool>& NowSlot() {
    static std::atomic<bool> s{false};
    return s;
}
inline void SetNow(bool swapped) { NowSlot().store(swapped, std::memory_order_relaxed); }
inline bool Now() { return NowSlot().load(std::memory_order_relaxed); }

// The layout's two sticks traded: its left stick (the walk) reads the right stick's push, its right stick (the turn) the left one's, and each
// stick's click with it - the click's value, the finger's touch on the stick, the stick's lean (what [direct] hold_stick_max_pct judges a click
// by) and a gesture's take of the click (`taken`, may be null: hidden wherever it stands now). Every other button stays its own.
inline void SwapSticks(directinput::layout::Controls* c, bool taken[directinput::layout::kCtlCount]) {
    using namespace directinput::layout;
    std::swap(c->stick[0][0], c->stick[1][0]);
    std::swap(c->stick[0][1], c->stick[1][1]);
    std::swap(c->v[kLStick], c->v[kRStick]);
    std::swap(c->touch[kLStick], c->touch[kRStick]);
    std::swap(c->lean[0], c->lean[1]);
    if (taken) std::swap(taken[kLStick], taken[kRStick]);
}

// The same for BodyWalk's copy of the controllers: each hand's stick X / Y and its click bit (`clickBit`) traded, everything else of each its own.
inline void SwapStickAxes(D2RVR_XrHand hand[2], uint32_t clickBit) {
    std::swap(hand[0].stickX, hand[1].stickX);
    std::swap(hand[0].stickY, hand[1].stickY);
    const uint32_t c0 = hand[0].buttons & clickBit, c1 = hand[1].buttons & clickBit;
    hand[0].buttons = (hand[0].buttons & ~clickBit) | c1;
    hand[1].buttons = (hand[1].buttons & ~clickBit) | c0;
}

}  // namespace handswap
