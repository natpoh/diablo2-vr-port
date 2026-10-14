#pragma once
// The settings' game view buttons (F1 - F4): a press changes the view the way the controller's own view bindings do. The view actions of
// vr/direct_layout.h's view1 .. view5 (D2RVR_ACT_VIEW_n) reach vrcam's update thread through link::ActionsHeld, which also adds what
// directinput::Action holds - so a press holds the view's action for 120 ms (a few polls of that thread, which acts on the press) and lets
// go, and vrcam's SetView then does what the F-keys do (the ini's [mode] vr_mode, the camera, the flat picture's mode).
//
// The numbering is the actions' old one (vrcam's kVrOf): F1 = VIEW_1 (from above), F2 = VIEW_2 (third person), F3 = VIEW_5 (the table: the
// game on your floor), F4 = VIEW_4 (the body). Held only while the view is F4 (directinput::Action does nothing elsewhere), and let go
// whatever the view is, so a stale hold cannot press again when F4 comes back. Used by the settings panel of its own
// (vr/xr_headset_settings.h) and by the skill panel's Settings tab (vr/skill_wheel.cpp). Any thread; the draw thread's in practice.

namespace hsetview {

// 1 = F1 ... 4 = F4. False if the number is not a view.
bool Press(int view);
// Every headset frame: lets go of a press that is due.
void Tick();
// Lets go at once (the panel is switched off, the session ends).
void LetGo();

}  // namespace hsetview
