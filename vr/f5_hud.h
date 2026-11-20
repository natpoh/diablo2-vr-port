// f5_hud.h - VR F5's map and allies' portraits in the picture ([hud] f5_map, f5_allies; docs/notes/f5_ar_view.md). Pure: no game, no settings
// object; the unit test is cleanroom/tests/f5_toolbar_test.cpp, the wiring vr/hud/hud_ticks.cpp (MapRotateFlatTick: the corner),
// vr/frame/frame_order.cpp (the map's shift), vr/hud/looks.cpp (its depth, the allies' shift) and vr/render/effects/effects.cpp (the effect).
//
// Serg (2026-11-19): "а ещё надо сдвиг карты для ф5 проработать чтобы она в углу была" - "тоже её пододвинуть к экрану" - "и сдвиг союзников как в ф4
// в верхнем левом углу". F5's picture follows the head, so what the game draws at the picture's corners - its corner map (top right or left), the
// allies' portraits (top left) - sits at the edge of the headset's view, half outside the lenses.
//
//   f5_map=0 (default)     the map as before: flat F3's own rules ([hud] flat_map_place / flat_map_corner / map_rotate_flat apply in F5 as in flat F3).
//   f5_map=1               the map kept in a corner (flat_map_place "as in the game's options" reads as flat_map_corner's corner; Left / Centre /
//                          Right as chosen - the place key's Centre too), moved f5_map_x % of the picture's width toward the middle and f5_map_y %
//                          of its height down, and f5_map_near nearer in stereo (+ nearer, as F3's [hud_floor] map_near); f5_map_size % of its size
//                          now (100 = as [hud_inside] map_zoom, flat F3's "Corner map size", has it; Serg 2026-11-20: "ползунок размер карты на ф5").
//   f5_allies=0 (default)  the portraits as before ([hud] allies_x / allies_y, every view).
//   f5_allies=1            in F5 they are moved f5_allies_x % right and f5_allies_y % down from the top left instead.
// The map and the portraits are taken out of the game's interface and drawn back by the effect, as F3's floor map and allies_x / allies_y do.
#pragma once

namespace f5hud {

// The sliders' defaults and ends (%; the depth in [hud_floor] map_near's units).
constexpr float kMapInDefault = 12.0f, kMapDownDefault = 10.0f, kMapNearDefault = 2.0f;
constexpr float kAlliesRightDefault = 12.0f, kAlliesDownDefault = 10.0f;
constexpr float kShiftMin = -20.0f, kShiftMax = 60.0f, kNearMin = -10.0f, kNearMax = 10.0f;

inline float Clamp(float v, float lo, float hi) { return v != v ? 0.0f : v < lo ? lo : v > hi ? hi : v; }

// [hud] flat_map_place as F5 reads it: 0 the game's own, 1 left, 2 the middle, 3 right. With f5_map the game's own becomes a corner
// (flat_map_corner: 0 left, else right); what was chosen stays.
inline int MapPlace(int flatMapPlace, int flatMapCorner, bool f5Map) {
    if (!f5Map || flatMapPlace != 0) return flatMapPlace;
    return flatMapCorner == 0 ? 1 : 3;
}

// The map's shift as the interface's uv for a map in the RIGHT corner (+x right, +y down): toward the middle = left. hud.cpp turns x round for a map
// in the left corner (SetPictureMapInward).
struct Uv { float x, y; };
inline Uv MapShiftUv(float inPct, float downPct) {
    return {-0.01f * Clamp(inPct, kShiftMin, kShiftMax), 0.01f * Clamp(downPct, kShiftMin, kShiftMax)};
}
// For a map in the corner `left` (true) or right: the x as it goes on the screen.
inline float MapShiftX(Uv u, bool left) { return left ? -u.x : u.x; }

// The depth as the effect takes it (D2R_DepthFog.fx GameMapShift: uv each eye, + nearer): [hud_floor] map_near's 0.005 a step.
inline float MapNearUv(float depth) { return 0.005f * Clamp(depth, kNearMin, kNearMax); }   // ("near" is a Windows macro)

// The allies' shift as [hud] allies_x / allies_y give it to the effect (AlliesOff: x + right, y + up, fractions of the picture).
inline Uv AlliesOff(float rightPct, float downPct) {
    return {0.01f * Clamp(rightPct, kShiftMin, kShiftMax), -0.01f * Clamp(downPct, kShiftMin, kShiftMax)};
}

// [hud] f5_map_size: % of the corner map's size now - flat F3's [hud_inside] map_zoom (`inside`); 100 leaves it as it is. As the effect takes it
// (hud::SetPictureZoom's map: 0.3 - 3 times the game's).
constexpr float kMapSizeDefault = 100.0f, kMapSizeMin = 30.0f, kMapSizeMax = 300.0f;
inline float MapZoom(float inside, float sizePct) {
    const float pct = sizePct != sizePct ? kMapSizeDefault : sizePct < kMapSizeMin ? kMapSizeMin : sizePct > kMapSizeMax ? kMapSizeMax : sizePct;
    const float z = inside * pct * 0.01f;
    return z < 0.3f ? 0.3f : z > 3.0f ? 3.0f : z;
}

// Each wanted now: its box ticked, VR F5 (vr/ar_view.h IsArView).
inline bool Wanted(bool box, bool arView) { return box && arView; }

}  // namespace f5hud
