// flat_menu_wire.h - [hud] game_menu_settings on the game (vr/flat_menu.cpp; the pure part and what it does: vr/flat_menu.h, vr/host_rules.h).
#pragma once

#include <cstdint>

#include "host_rules.h"

namespace D2RL { struct PluginContext; }

namespace flatmenu {

// D2RLoaderLoadPlugin: the host's rules channel "HR" (D2RLoader takes channels at the load only: always registered - its hello and answer
// change nothing in play) and the console's "vrrules". `iniPath`: the player's d2r_vr.ini.
void Init(const D2RL::PluginContext* ctx, const wchar_t* iniPath);
void Shutdown();
// vrcam's LoadSettings, every read: [hud] game_menu_settings, [hud] game_menu_key (flat_menu.h PanelKey: 0 none, 1 F10, 2 Insert,
// 3 Delete, 4 Page Up, 5 Page Down), [hud] host_rules_key (0 none, 1 Home, 2 End, 3 Insert), the platform (flat: the monitor, platform 0)
// and our own values of the host's rules (what we answer as the host, what we show in our own game).
void Configure(bool on, int panelKey, int listKey, bool flat);
// True: a plain press of this key opens / closes the panel now (the box on, flat, game_menu_key on it) - the plugin's own polled use of
// the key (F10: the skeleton dump, vr/frame/frame_order.cpp) stands aside; Ctrl + F10 (the frame trace) is not the panel's.
bool TakesKey(unsigned vk);
void SetOwnRules(const hostrules::Values& v);
// The 1 ms update thread: the overlay's callback put in / taken out with the key, the connection to the host's channel and the hello
// (only with the key on, in a game), the panel's values read again while it is up.
void Tick(uint64_t nowMs, bool inWorld);
// The game window's procedure (vr/hud/window.cpp), first: true = the message is ours (*result is the answer), the game does not see it.
bool OnWindowMessage(void* hwnd, unsigned msg, uintptr_t wParam, intptr_t lParam, intptr_t* result);

}  // namespace flatmenu
