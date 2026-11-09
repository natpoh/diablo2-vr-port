// hit_share_wire.h - [multiplayer] share_hit_outcomes on the game (vr/hit_share_wire.cpp; the protocol and why: vr/hit_share.h).
#pragma once

#include <cstdint>

namespace D2RL {
struct PluginContext;
}

namespace hitshare {

// At the plugin's load: the channel "HS" registered (always: D2RLoader takes channels at the load only; the box is live).
void Init(const D2RL::PluginContext* ctx);
void Shutdown();

// [multiplayer] share_hit_outcomes, live (vrcam's LoadSettings): as the host, send each joined player his own blows' outcomes. Only the
// host's box counts; a joined player's own box means nothing in that game.
void SetWanted(bool on);

// vrcam's update loop, every round (it paces itself): the host's outcomes sent to its peers; as a joined client with its swing timer's
// numbers on, the connection to the host's channel and the hello once a second. `heroId`: our hero's unit id (0 not known yet).
void Tick(uint64_t nowMs, bool inWorld, uint32_t heroId);

}  // namespace hitshare
