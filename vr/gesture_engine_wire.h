#pragma once
// [gestures] engine_in_game: BodyWalk's gesture engine in the game's process (vr/gesture_engine_lib.h loads it; vr/gesture_engine.h is the pure part).
// One listener on direct input's per-frame callback (the headset frame's thread): in VR first person (F4) it opens the engine, tells it the hero's
// class and skills when they change (skill_link_wire's list), feeds it the frame with the pens of this frame (the Gestures tab's take, else the
// [gestures] engine_pen triggers in play), hands what it recognised to skill_link_wire's caster (the game's own cast) and keeps its trail for the runes
// layer. The skill panel's Gestures tab reads the list and sends its commands through here instead of Game Link's Gestures / GestureCmds blocks.
// Off: nothing is loaded or fed; the tab uses BodyWalk over Game Link as before.

#include <cstdint>

#include "gesture_cmds.h"
#include "gesture_engine_abi.h"

namespace D2RL {
struct PluginContext;
}

namespace gengine {

void Init(const D2RL::PluginContext* ctx);
// The settings load: [gestures] engine_in_game, engine_pen (gesture_engine.h kPen*).
void Configure(bool on, int playPens);
bool On();
// On and the engine open: the Gestures tab is the engine's.
bool Active();
// On but the engine not there (the DLL missing or refused): the tab says so.
bool Missing();

// The headset frame's thread (the skill panel's frame) - the engine's list made the tab's (answers stamped), and a command done at once: its number.
bool List(BW_LinkGestures* out);
uint32_t Push(const gesturecmd::Cmd& c);
// A pose's zone from the engine (its optional calls; false / BW_LINK_GR_BAD without them). The same thread.
bool PoseZone(uint32_t gesture, BWGE_PoseZone* out);
int SetPoseZone(uint32_t gesture, float radiusM);
// A take's samples from the engine (its optional BWGE_GetTake): how many; 0 none.
int Take(uint32_t gesture, BWGE_TakeSample* out, int max);
// Game actions (vr/gesture_actions.h) through the engine's optional calls: whether it has them, bind one ("" none), read one ("" a skill / none).
bool HasActions();
int BindAction(uint32_t gesture, const char* action);
bool ActionOf(uint32_t gesture, char* out, uint32_t cap);
// A glyph's hand through the engine's optional call (LEFT / RIGHT / BOTH = either).
bool HasSetHand();
int SetHand(uint32_t gesture, uint8_t hand);
// A gesture's mode through the engine's optional calls (0 cast once, 1 select: the skill goes onto the right trigger).
bool HasModes();
int SetMode(uint32_t gesture, uint8_t mode);
uint8_t ModeOf(uint32_t gesture);

// The trail to draw (the last frame's copy). Any thread.
uint32_t Trail(BW_LinkVrRune* out, uint32_t max);

// The plugin's unload / the session's end: the engine closed.
void Shutdown();

}  // namespace gengine
