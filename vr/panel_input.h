// panel_input.h - the laser's mouse in the game's controller layout (native OpenXR,
// [openxr] laser; docs/plan_laser_menus.md). vr/panel_input.cpp says how.
#pragma once

namespace D2RL {
struct PluginContext;
}

namespace panelinput {

// Before anything else: the log.
void Init(const D2RL::PluginContext* ctx);
// Twice a second while the laser is on: the hooks go in once every address is found and as
// expected (code the game has not run yet is not readable: tried again). Once in, they stay;
// they pass everything through while the laser does not own the pointer.
void Install();
bool Ready();

// The draw thread, every frame the laser is the mouse (before it moves or clicks): the pad's
// layout is kept, and the UI's pointer handling sees the mouse, for 300 ms more - past the last
// button-up (SendInput is queued). The focus follows the pointer.
void OwnPointer();
// The pad navigates the panel (a stick, a face button): the focus is the pad's again.
void UseController();

// [openxr] laser_tooltips (the ini, live): the game's item tooltip and its equipped-item comparison at once
// under the laser (vr/laser_tooltip.h, panel_input.cpp). Install() hooks the three queries after the laser's own
// hooks, and only while this is on; what is not found leaves the game's own delay.
void WantTooltips(bool on);
// The draw thread, every laser frame: the laser is the mouse, points at the picture and holds no button
// (lasertip::Hovering). A lease of 300 ms; a frame that does not hover ends it.
void ItemHover(bool hovering);

// The UI thread (gamestate's read, 5 times a second): a game entered or a panel closed in the
// keyboard-and-mouse layout right after the laser was the mouse - the controller layout asked
// for once, so the pad's targeting (attack, pick-up) is there before the next pad press.
void UiTick(bool inGame, bool menu);

}  // namespace panelinput
