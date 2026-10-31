#pragma once
// players_cmd.h - the game's players count (/players N) from the headset: the Options page's Players row ([openxr] headset_players).
//
// What the game does (D2R 3.3.93787 under D2RLoader 1.3.1, read off the decrypted image of the running game, 2026-10-30): a chat line
// that is sent goes from the chat box (0x2E34C0) to the game's chat command parser 0x188700(text, text, 0, 0) - /fps, /fpsperf,
// /fpslogger, /framerate, /players, /nopickup ... For "/players" it asks the game's mode (0x8AF90: 0 a game of one's own offline, 5 / 7
// the TCP/IP host's; anything else and the line is not a command), takes the number after it (atoi, at least 1, at most 8), writes it to
// the game's setting "Offline Difficulty Scaling" (0xE19E0: the object, its value at +0xC; the one Options > Gameplay shows, kept in
// Settings.json) through the setting's own setter 0xD2F020 (its listener 0xE0220 hands the count to the game, 0x425A60, and says
// "players set to N" the game's own way: an event message 0x5A, sub 0x16 / 0x17), then calls 0xC7D30(5, 0) as after every command. The game starts a game
// from that setting too (0x8D046). So the row sends the very line, "/players N", into the parser - every check of the game's stays
// the game's - and reads the setting back to show it.
//
// Nothing here writes the game's memory; the count and the mode are read, the command is the game's to take or not.

#include <D2RLPlugin/context.h>

#include "hset_model.h"   // hset::Players

namespace playerscmd {

// From D2RLoaderLoadPlugin (next to gamecmd::Init).
void Init(const D2RL::PluginContext* ctx);
// Any thread: "/players N" (1 - 8) into the game's chat commands, on the UI thread soon after (Tick queues it). A newer ask replaces
// one not sent yet.
void Ask(int players);
// What the game says now: its players count (0 not known: the code not found, the setting not made yet) and whether it would take
// /players (its mode). Read from the game's memory, any thread, cheap.
hset::Players Now();
// vrcam's 1 ms timer (as gamecmd::Tick): an ask goes to the UI thread.
void Tick();

}  // namespace playerscmd
