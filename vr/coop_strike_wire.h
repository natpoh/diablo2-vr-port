// coop_strike_wire.h - [gameplay] melee_direct_client_host on the game (vr/coop_strike.h says what and why): the joined client's sends,
// the host's history, round trips, checks and strikes. Its messages ride melee_direct_client's channel (kChannel "DH"), whose callbacks
// hand them here.
#pragma once

#include <cstdint>

#include "coop_strike.h"

namespace D2RL {
struct PluginContext;
}

namespace coopstrikewire {

// D2RLoaderLoadPlugin.
void Init(const D2RL::PluginContext* ctx);
// [gameplay] melee_direct_client_host (every ini read): this PC's rule as the host - who hosts decides; joined, the host's box is the rule.
void SetHostBox(bool on);
bool HostBox();
// The host's box on and able to strike now (its server game learned, Direct hit's hooks in): what the answer to a hello says.
bool HostCanStrike(uint64_t nowMs);

// ---- from melee_direct_client's channel callbacks (D2RLoader's threads) ----
void OnHello(uint64_t peer, uint32_t hero);                                   // the host: a joined client's hello (its hero): bound, answered
void HostMessage(uint64_t peer, uint16_t id, const void* data, uint32_t size);  // the host: a swing or a pong
void ClientMessage(uint16_t id, const void* data, uint32_t size);              // joined: the rule, a ping, a verdict
void PeerGone(uint64_t peer);                                                  // the host: a peer left
void HostGone();                                                               // joined: the connection to the host lost

// ---- the joined client ----
// The host said lately that it strikes the joined players' touches (and the channel is up): the touches go to it, not as the game's command.
bool HostStrikes(uint64_t nowMs);
// The game's UI thread (melee_direct_client's send): the touch to the host. `touchAt` the touch's time (GetTickCount64). True: sent - the
// caller sends no command for it; false: not sent, the caller goes on as without the option.
bool SendSwing(coopstrike::Swing s, uint64_t touchAt, uint64_t nowMs);
// The timer's thread: the host struck a touch of ours - the hand to buzz (skeleton side). False: none waiting.
bool TakeConfirm(int* hand);
// The timer's thread: the host refused a touch of ours in time - the normal attack is pressed for it. False: none waiting.
bool TakeRefused(uint32_t* monster, int* hand);

// ---- the host ----
// vrcam's update loop, every round: the history's job on the game's thread, the pings, the verdicts out, the report. `inWorld` a game area.
void Tick(uint64_t nowMs, bool inWorld);

}  // namespace coopstrikewire
