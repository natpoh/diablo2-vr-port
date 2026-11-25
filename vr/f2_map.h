// f2_map.h - F2's corner map moved and sized in the picture ([hud_third] map_x / map_y / map_size). Pure: no game, no settings object; the unit
// test is cleanroom/tests/f2_map_test.cpp, the wiring vr/frame/frame_order.cpp (the shift and size, hud::SetPictureMapOffset / SetPictureZoom) and
// vr/hud.cpp Publish (kept on the screen, SetPictureMapKeepOn).
//
// A player (Discord, relayed by Serg 2026-11-25): "is it possible to move map in third person? its always too far in top right corner". In F2 (third
// person, behind the hero; VR and the flat game alike) the game's corner map sits at the picture's top corner - in the headset at the edge of the view.
//
//   map_x = 0, map_y = 0, map_size = 100 (default)   the map as before: left in the game's picture, not taken out at all.
//   map_x                                            % of the picture's width toward the middle (a map in the right corner moves left, one in the
//                                                    left corner right - as [hud] f5_map_x; hud.cpp turns x round for the left corner).
//   map_y                                            % of the picture's height down.
//   map_size                                         % of its size, grown from its own corner (as [hud_floor] map_size).
// Any of them off its default: the map is taken out of the game's interface and drawn back by the effect there, as F3's floor map and F5's are; the
// box is then held inside the picture (KeepOnScreen), whatever the sliders say.
#pragma once

namespace f2map {

// The sliders' ends (%; the ini is held to the same).
constexpr float kShiftMin = -10.0f, kShiftMax = 90.0f;
constexpr float kSizeDefault = 100.0f, kSizeMin = 30.0f, kSizeMax = 300.0f;

inline float Clamp(float v, float lo, float hi) { return v != v ? 0.0f : v < lo ? lo : v > hi ? hi : v; }

// The shift as the interface's uv for a map in the RIGHT corner (+x right, +y down): toward the middle = left. hud.cpp turns x round for a map in
// the left corner (SetPictureMapInward).
struct Uv { float x, y; };
inline Uv ShiftUv(float inPct, float downPct) {
    return {-0.01f * Clamp(inPct, kShiftMin, kShiftMax), 0.01f * Clamp(downPct, kShiftMin, kShiftMax)};
}

// map_size as the effect's zoom (hud::SetPictureZoom's map: 0.3 - 3 times the game's).
inline float Zoom(float sizePct) {
    const float pct = sizePct != sizePct ? kSizeDefault : sizePct < kSizeMin ? kSizeMin : sizePct > kSizeMax ? kSizeMax : sizePct;
    return pct / 100.0f;
}

// Anything off its default: the map is taken out and drawn back (else the game's own, untouched).
inline bool Moved(float inPct, float downPct, float sizePct) {
    const Uv u = ShiftUv(inPct, downPct);
    const float z = Zoom(sizePct);
    return u.x > 0.0005f || u.x < -0.0005f || u.y > 0.0005f || u.y < -0.0005f || z > 1.005f || z < 0.995f;
}

// Each axis's shift held so the box [lo, hi] (uv, already grown) stays inside 0 - 1; a box larger than the picture is centred on it.
inline float KeepAxis(float lo, float hi, float d) {
    if (d != d) d = 0.0f;
    const float dMin = -lo, dMax = 1.0f - hi;
    if (dMin > dMax) return 0.5f * (dMin + dMax);
    return d < dMin ? dMin : d > dMax ? dMax : d;
}
// box: l, t, r, b in uv (as hud.cpp's g_pic.box), before the shift; dx, dy: the shift as it goes on the screen. Held in place.
inline void KeepOnScreen(const float box[4], float* dx, float* dy) {
    *dx = KeepAxis(box[0], box[2], *dx);
    *dy = KeepAxis(box[1], box[3], *dy);
}

// [hud_third] allies_x / allies_y (Serg 2026-11-25: "иконки союзников надо тоже двигать на ф2 ... логику оттуда перенеси"): VR F2's allies' portraits
// (the mercenary, the party - the game's top left) moved right and down, % of the picture, as VR F5's [hud] f5_allies_x / _y do. Both 0 (default): the
// portraits as before - [hud] allies_x / allies_y, every view's (0 = where the game puts them). Either off 0: F2's own shift in place of those.
constexpr float kAlliesMin = -20.0f, kAlliesMax = 80.0f;
inline bool AlliesMoved(float rightPct, float downPct) {
    const float r = Clamp(rightPct, kAlliesMin, kAlliesMax), d = Clamp(downPct, kAlliesMin, kAlliesMax);
    return r > 0.05f || r < -0.05f || d > 0.05f || d < -0.05f;
}
// The shift as the effect takes it (AlliesOff, as f5hud::AlliesOff): x + right, y + UP, fractions of the picture.
inline Uv AlliesOff(float rightPct, float downPct) {
    return {0.01f * Clamp(rightPct, kAlliesMin, kAlliesMax), -0.01f * Clamp(downPct, kAlliesMin, kAlliesMax)};
}

// F2 now: our camera on, third person, neither VR F1 through our camera, nor VR F3 (run as view 2), nor VR F5 (vrcam's BarOffsetNow order).
inline bool IsF2(bool enabled, bool topPersp, bool tableView, bool thirdPerson, bool arView) {
    return enabled && !topPersp && !tableView && thirdPerson && !arView;
}

}  // namespace f2map
