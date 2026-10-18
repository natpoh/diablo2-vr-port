#pragma once
// The skill wheel in the headset ([openxr] skill_wheel, D2R VR Settings > Stereo > Native OpenXR): a ring of the controller's
// eight skill slots hung in front of the head. The left grip held and the right thumbstick clicked opens it; the laser picks a
// wedge; the right trigger then casts that slot - the game's own pad button, nothing else; EDIT SKILLS assigns a learned skill
// to a slot through the game's own setter. Unticked: nothing exists - no listener, no thread, no picture.
//
// The parts: vr/wheel_*.h (pure, tested), vr/wheel_game.cpp (the game's side on its UI thread), vr/wheel_paint.cpp (the picture),
// vr/wheel_icons.cpp (the local icon cache), vr/xr_skill_wheel.h (the OpenXR layer), and this: the wiring between them and
// vr/direct_input.h. docs/skill_wheel.md says how it plays.

#include <cstdint>
#include <vector>

#include "hset_model.h"
#include "wheel_controller.h"

namespace D2RL {
struct PluginContext;
}

namespace skillwheel {

// Once, from the plugin's start (next to gamecmd::Init).
void Init(const D2RL::PluginContext* ctx);

// xr.cpp LoadSettings, live: the box, the pointing hand ([openxr] laser_hand), and the panel's distance and width in metres.
// `style` is [openxr] skill_panel_style (0 the wheel, 1 the tiles - the window after the game's own skill dialog) and `tilesWidthM` the
// tiles' width ([openxr] skill_tiles_width_m); `ini` the settings file: from it come how the panel opens ([openxr] skill_wheel_open: the
// long press of the right stick click, or the chord), how long a long press is ([direct] hold_ms), and whether the tiles' button hints
// are shown (only while direct input is on and its [direct] layout is not rebound). Switching on registers the per-frame listener;
// switching off lets everything go. The panel acts in VR first person (F4) only: vr/view_gate.h.
// `settingsTab` is [openxr] headset_settings: in the tiles' look the window gets a second tab, Settings (the game view F1 - F4, the render size, the
// T-pose measurement: vr/hset_*.h hosted by vr/wheel_controller.h), opened by the same long press, or by the old settings panel's chord (both grips,
// then a thumbstick click); the panel of its own (vr/xr_headset_settings.h) stands down then.
void Configure(bool on, int pointerHand, float distanceM, float widthM, int style = 0, float tilesWidthM = 0.9f, const wchar_t* ini = nullptr,
               bool settingsTab = false);
// The Settings tab is in force: the panel is on, in the tiles' look, with the key. (Any thread.)
bool SettingsTabOn();
// xr.cpp, every headset frame while SettingsTabOn(): what the Video page shows of the picture (the size the game draws, what the headset recommends
// and holds - the numbers only xr.cpp has).
void SetSettingsHost(const hset::video::Env& env);
// xr.cpp ReadProfiles: the interaction profile path each hand runs now ("" / null = none). The tiles name the controller's own buttons by it.
void SetProfiles(const char* left, const char* right);
bool Enabled();
// [openxr] skill_rt_a: this frame the right trigger is the game's RT slot and that slot holds a skill (the panel's slot controls own the trigger). The melee rule
// (vr/melee_trigger.h) asks, so that only the plain attack on the trigger is withheld. False where the key is off or the panel is not in charge. (Any thread.)
bool TriggerCastsSkill();

// vrcam's timer, every millisecond or so: cheap when off; queues the UI thread's reads.
void Tick();

// xr.cpp CloseFrame (the draw thread): the wheel is up -> its layer (the panel, the beam), and the revision its picture must
// reach before the panel is shown (an older picture is the last time's). The painter is told what to paint.
bool Draw(wheel::Layer* layer, uint32_t* needRevision);
// A finished picture (premultiplied pixels, R in the low byte, `width` x `height`: the wheel's is wheelpaint::kSize square, the tiles' is
// wide) newer than the last, with its revision.
bool Picture(std::vector<uint32_t>* px, uint32_t* revision, uint32_t* width, uint32_t* height);

// With the session (Teardown): the wheel closes, the controls it held are let go, the painter and the icons stop.
void Teardown();
// The plugin's unload (vrcam's D2RLoaderUnloadPlugin): the listener goes, every thread is joined. Nothing is left for a static destructor.
void Shutdown();

}  // namespace skillwheel
