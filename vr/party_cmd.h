#pragma once
// party_cmd.h - the headset's Party page ([openxr] headset_party, vr/hset_party.h): the game's party roster read, and the game's own Party panel
// buttons pressed without the panel - the very functions its message handler calls (vr/party_cmd_core.h has the places), on the UI thread.
//
// Nothing here writes the game's memory or makes a packet: the roster is read; a press is the panel's ProcessInviteButton (invite / accept / cancel,
// as the game decides by the state it keeps) or its LeaveParty, and Open Party panel is the game's Party key command (P, vr/gamecmd.cpp).

#include <D2RLPlugin/context.h>

#include "hset_party.h"

namespace partycmd {

// From D2RLoaderLoadPlugin (next to gamecmd::Init).
void Init(const D2RL::PluginContext* ctx);
// The Party page is up (any thread, every frame it is): the roster is read again, twice a second, on the UI thread. Not called: nothing is read.
void Watch();
// What was read last (not known: not in a multiplayer game, the game's party code not found, nothing read yet).
hset::party::View Now();
// A press of the page (any thread): done on the UI thread soon after - a player's button only while the game still keeps the state it was pressed in.
void Ask(const hset::party::Request& r);
// vrcam's 1 ms timer (as gamecmd::Tick): a read or a press due goes to the UI thread.
void Tick();

}  // namespace partycmd
