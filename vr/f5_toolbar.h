// f5_toolbar.h - VR F5's toolbar hung low in front ([hud] f5_toolbar; docs/notes/f5_ar_view.md). Pure: no game, no settings object; the unit
// test is cleanroom/tests/f5_toolbar_test.cpp, the wiring vr/hud.cpp (SetViewBar: the strip cut out, the look sent as "low in front") and
// vr/frame/frame_order.cpp (each tick, from the settings).
//
// Serg (2026-11-19), looking at "Toolbar in the headset" > Where = Low in front: "для вида ф5 надо добавить толбар внизу" - "или на экране
// внизу" - "лучше под телом спереди с ползунками по высоте и расстоянию".
//
//   f5_toolbar=0 (default)  F5 as before: the toolbar in the picture, where flat F3 has it.
//   f5_toolbar=1            in F5 the game's toolbar (life, mana, belt, skills) is cut out of the picture - only that strip, the map and the rest
//                           stay in it - and hung low in front of you as F4's "Low in front", wherever [hud] bar_place hangs F4's: its height
//                           under the eyes and its distance ahead are F5's own two sliders (f5_toolbar_below_cm, f5_toolbar_ahead_cm).
// A menu (inventory, trader...) puts it back in the picture as in every view.
//
// "Low in front" is placed the same way by FlatVR (flat_vr_game_hud.cpp GameHudPose) and by native OpenXR (vr/xr.cpp HudPose): from the
// head, turned by its yaw only, at (0, -0.32, -0.55) m (x right, y up, z back), tipped 35 degrees back to face the eyes; then moved in that
// tipped frame by the look's bar_side_cm (its y) and bar_lift_cm (its z). F5's height and distance go to FlatVR as those two numbers, so
// FlatVR needs no change: the strip lands exactly where the sliders say on both routes.
#pragma once

#include <cmath>

namespace f5bar {

constexpr float kBelowCm = 32.0f;   // "Low in front": its middle under the eyes, cm (HudPose's -0.32)
constexpr float kAheadCm = 55.0f;   // ... and ahead of them (HudPose's -0.55)
constexpr float kTipDeg = -35.0f;   // ... tipped back about x to face the eyes
constexpr float kBelowMin = 5.0f, kBelowMax = 90.0f;   // the sliders' ends
// The sliders' defaults (Serg 2026-11-19, tuned in the headset: "эти настройки по умолчанию для всех"): 33 cm under the eyes, 32 cm ahead - nearer
// than "Low in front" (32 / 55).
constexpr float kDefaultBelowCm = 33.0f, kDefaultAheadCm = 32.0f;
constexpr float kAheadMin = 15.0f, kAheadMax = 120.0f;

inline float Clamp(float v, float lo, float hi) { return v != v ? lo : v < lo ? lo : v > hi ? hi : v; }

// The look's two offsets that move "Low in front" to `belowCm` under the eyes and `aheadCm` ahead of them.
struct Offset { float sideCm, liftCm; };
inline Offset OffsetFor(float belowCm, float aheadCm) {
    belowCm = Clamp(belowCm, kBelowMin, kBelowMax);
    aheadCm = Clamp(aheadCm, kAheadMin, kAheadMax);
    const float up = kBelowCm - belowCm;   // cm higher than "Low in front"
    const float fwd = aheadCm - kAheadCm;  // cm farther ahead
    const float t = kTipDeg * 3.14159265358979f / 180.0f;
    // the tipped frame's y = (0, cos t, sin t), its z = (0, -sin t, cos t); the move wanted is (0, up, -fwd) in the head's turn
    const float c = std::cos(t), s = std::sin(t);
    return {up * c - fwd * s, -up * s - fwd * c};
}

// Where the strip's middle comes out, cm from the eyes in the head's turn (y up, z back) - what HudPose does with OffsetFor's numbers.
struct At { float upCm, backCm; };
inline At Placed(Offset o) {
    const float t = kTipDeg * 3.14159265358979f / 180.0f;
    const float c = std::cos(t), s = std::sin(t);
    return {-kBelowCm + o.sideCm * c - o.liftCm * s, -kAheadCm + o.sideCm * s + o.liftCm * c};
}

// The anchor FlatVR / native get for the toolbar in F5 with the box ticked: 3, low in front (FlatVRGameHudLook.bar_anchor).
constexpr unsigned kAnchor = 3;

// F5's toolbar hung now? The box, VR F5 (vr/ar_view.h IsArView).
inline bool Wanted(bool f5Toolbar, bool arView) { return f5Toolbar && arView; }

// [hud_third] bar_low (Serg 2026-11-25: "толбар с хп давай сделаем как на виде ф5 внизу под камерой … выбор: либо внизу под камерой либо просто перед
// экраном как сейчас"): VR F2's toolbar hung the same way - cut out of the picture, low in front at F2's own height and distance (bar_low_below_cm,
// bar_low_ahead_cm, the same defaults as F5's). 0 = in front, in the picture, as before. Only while F2's interface is in the picture ([hud_third]
// classic 0): "Hung in the room" has F4's places already. vrF2: VR, our camera on, behind the hero - not from above, not the floor, not F5.
inline bool WantedF2(int barLow, bool vrF2, bool inPicture) { return barLow == 1 && vrF2 && inPicture; }

// Which height and distance the strip takes: F2's own sliders while F2 wants it, else F5's.
inline Offset OffsetNow(bool f2, float f2Below, float f2Ahead, float f5Below, float f5Ahead) {
    return f2 ? OffsetFor(f2Below, f2Ahead) : OffsetFor(f5Below, f5Ahead);
}

}  // namespace f5bar
