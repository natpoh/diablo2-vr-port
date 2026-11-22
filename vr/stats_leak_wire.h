// stats_leak_wire.h - [debug] stats_leak_trace (vr/stats_leak.h): the game's side, vr/stats_leak.cpp.
#pragma once

namespace D2RL {
struct PluginContext;
}

namespace statsleak {

// At the plugin's start: the option (read once - a game restart changes it) and d2r_vr.ini's path (the report file goes to ..\logs).
void Init(const D2RL::PluginContext* ctx, bool wanted, const wchar_t* iniPath);
// The update loop, often: the two hooks go in once the game's code is decrypted (twice a second until then). Off: nothing.
void Tick();

}   // namespace statsleak
