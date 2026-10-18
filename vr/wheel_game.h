#pragma once
// The skill wheel's side in the game (vr/wheel_game.cpp): reads the controller's skill slots and the player's skills on the
// game's UI thread, publishes a snapshot, and carries an assignment out through the game's own setter. Everything else of the
// wheel is pure (vr/wheel_*.h) and never touches the game.

#include <cstdint>

#include "wheel_types.h"

namespace D2RL {
struct PluginContext;
}

namespace wheelgame {

// Once, from the plugin's start: the loader's services (the UI thread, the game's text).
void Init(const D2RL::PluginContext* ctx);

// vrcam's timer, every millisecond or so; cheap when `on` is false. Queues the UI thread's reads (every 400 ms, every 100 ms
// while the wheel is `open`) and a waiting request.
void Tick(bool on, bool open);

// [openxr] skill_rt_a: the editor's list offers the basic attack (wheel::kSkillAttack) beside Interact, the way to put a slot back to the plain action.
void SetOfferAttack(bool on);

// The draw thread: a request for the UI thread (the one in flight is replaced by a newer one only after its answer).
void Submit(const wheel::Request& r);
wheel::Reply ReplyNow();

// The newest snapshot, copied when its revision is not `*revision`; `at` and `session` of `out` are always the newest.
// False before the first one.
bool Latest(wheel::Snapshot* out, uint32_t* revision);

// Everything let go: pending request dropped, snapshot forgotten (the session ended or the wheel was switched off).
void Reset();

}  // namespace wheelgame
