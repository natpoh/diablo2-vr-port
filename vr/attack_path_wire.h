// attack_path_wire.h - [gameplay] flat_attack_path on the game (vr/attack_path_wire.cpp; the decisions: vr/attack_path.h).
#pragma once

#include <cstdint>

namespace D2RL { struct PluginContext; }

namespace attackpath {

// While the plugin loads: the channel to the host (a joined player's game only, D2RLoader's network service). Always: the tick is live.
void InitNet(const D2RL::PluginContext* ctx);
void ShutdownNet();

void SetLogger(void (*log)(const char*));
// Live, from vrcam's LoadSettings. key: 0 off, 1 walk, 2 walk or run; for our own game and the one we host (joined, the host's decides). coneDeg: the half angle of "before the hero".
void SetWanted(int key, float coneDeg);
// The hooks are wanted: ticked, or a TCP/IP game, where the host may have ticked it.
bool Wanted();
// vrcam, once: the camera's forward on the game's screen (x right, y up); false = not known now.
void SetForwardFn(bool (*fn)(float* sx, float* sy));
// Its own hooks: the server's start of a mode by direction 0x42CED0 and the client's prediction 0xFF010. Tried from the install round.
void Install();

// Called from the hooks of vr/move_attack_wire.cpp.
bool OnTick(uintptr_t game, uintptr_t unit, int code, int* ret);          // true: our step, the game's handler is not called
bool BeforeAction(uintptr_t game, uintptr_t unit, int code);              // true: AfterAction is due once the game made the action
void AfterAction(uintptr_t game, uintptr_t unit);
void AfterCtrl(uint32_t player, uintptr_t mgr);
bool SwallowStop(void* unit, int type);                                   // the stick's stop (a map click of type 6) while we walk in the attack

}  // namespace attackpath
