// ar_view.h - VR F5, the view for AR glasses ([mode] f5_ar_view; docs/notes/f5_ar_view.md). Pure: no game, no settings object; the unit
// test is cleanroom/tests/ar_view_test.cpp, the wiring vr/view/view_state.cpp (ArView, FlatRules, FlatRulesView), vr/view/views.cpp
// (ApplyView) and vr/link.cpp (FlatVR's Head Lock and the Mapping tab).
//
// Serg (2026-11-18): "ещё я хочу добавить режим ф5 он будет аналогичен режиму ф3 на плоском экране, т.е. это будет вид изнутри но без рук,
// для того чтобы люди с хреал и другими ар очками могли поиграть" - "для вывода будет использоваться флатвр".
//
// The output is the one "3D in the headset" choice, as for F1 - F3 (Serg 2026-11-19: "все режимы могут работать в нативе или через флатвр"):
// native OpenXR - the headset's own eyes (with [mode] f5_head_look, as F4's); FlatVR stereo / FlatVR depth - the picture
// (a pair, or one picture and its depth) to FlatVR, whose own Output then picks the headset or AR glasses (XREAL as a display, SBS).
// F5 is a fifth VR view ([mode] platform=1, vr_mode=5) that plays exactly as the flat game's F3: our camera at the hero's eyes turned by the
// mouse (and a pad's right stick), W A S D, the interface in the picture, no VR hands, arms, held-weapon pieces or gestures. Only the output
// is VR's: the picture (and its depth) goes to FlatVR as in every VR view, and FlatVR shows it in the glasses on its screen. The head does
// not turn the camera (flat F3 has no head); FlatVR's Head Lock is off, so the screen stays where FlatVR puts it.
//
//   f5_ar_view=0 (default)  as before: VR has F1 - F4, a vr_mode=5 left in the ini is read as F4, F5 stays the game's skill key.
//   f5_ar_view=1            VR has F1 - F5: the F5 key (Shift + F5 with [view] keys=1, none with keys=2), F12 round all five.
// The flat game ([mode] platform=0) is never touched: it keeps F1 - F3.
#pragma once

namespace arview {

constexpr int kView = 5;   // vr_mode of F5

// How many views the platform has: flat 3, VR 4, VR with F5 5.
inline int ViewCount(int platform, bool f5On) { return platform == 1 ? (f5On ? 5 : 4) : 3; }

// [mode] vr_mode read from the ini (already carried over from the old keys): 1 - 4 as they are, 5 only while F5 is on (else F4, where
// the view last was before F5 came), anything else 1.
inline int ClampVrMode(int v, bool f5On) {
    if (v == kView) return f5On ? kView : 4;
    return v >= 1 && v <= 4 ? v : 1;
}

// VR F5 now?
inline bool IsArView(int platform, int vrView, bool f5On) { return f5On && platform == 1 && vrView == kView; }

// The flat game's rules (mouse look, W A S D, the crosshair, flat F3's body and pick-up, the interface in the picture): the flat game,
// and VR F5.
inline bool FlatRules(int platform, int vrView, bool f5On) { return platform == 0 || IsArView(platform, vrView, f5On); }

// Under the flat game's rules, the flat view they stand for: the flat game's own, F5 = flat F3. 0 elsewhere.
inline int FlatRulesView(int platform, int flatView, int vrView, bool f5On) {
    if (platform == 0) return flatView;
    return IsArView(platform, vrView, f5On) ? 3 : 0;
}

// The pure helpers that take (platform, view) - the pose writers (vr/body/pose_views.h), the hand's aim (vr/attack_view.h) - get F5 as
// the flat game's F3.
struct Rules { int platform, view; };
inline Rules RulesFor(int platform, int view, int vrView, bool f5On) {
    if (IsArView(platform, vrView, f5On)) return {0, 3};
    return {platform, view};
}

// ---- the head ([mode] f5_head_look, Serg 2026-11-19: "на виде ф5 у меня камера так же должна быть привязана к голове как на ф4 только
// управление джойстиками как на ф3") ----
// 1 (default): F5's camera follows the head as F4's does - native OpenXR draws it with the headset's own eyes (the head camera, xr.h
// kViewsHead), FlatVR gets the head's turn on top of the mouse's and its Head Lock on - while the controls stay flat F3's (mouse / keyboard,
// a pad's sticks), no hands. 0: the head turns nothing - native OpenXR shows F5 as the flat picture (one image to both eyes), FlatVR's
// screen stays put (Head Lock off). (A "window of the room" camera for native, 82c5d283 / 630dc3d0, was taken out again: Serg wants the
// head's camera, and through it the game's dome came out cut in half.)
enum class NativeF5 { HeadCamera, Flat };
inline NativeF5 NativeF5Mode(bool headLook) { return headLook ? NativeF5::HeadCamera : NativeF5::Flat; }

// Game Link's view mode for F5 (shared/d2r_vr_state.h viewMode). BodyWalk does not read the number; vr/link.cpp turns it into FlatVR's
// Head Lock and the Mapping tab ("main": no body actions). 5: the head turns nothing (Head Lock off); 6: the head turns the camera (on).
constexpr unsigned kLinkViewMode = 5;
constexpr unsigned kLinkViewModeHead = 6;
inline unsigned LinkViewMode(bool headLook) { return headLook ? kLinkViewModeHead : kLinkViewMode; }
inline bool IsLinkF5(unsigned viewMode) { return viewMode == kLinkViewMode || viewMode == kLinkViewModeHead; }
inline bool LinkHeadLock(unsigned viewMode, bool flatMenu) {
    return !flatMenu && (viewMode == 1 || viewMode == 3 || viewMode == 4 || viewMode == kLinkViewModeHead);   // (0 - 4 as before)
}

}  // namespace arview
