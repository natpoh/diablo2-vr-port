// pose_share_wire.h - [multiplayer] pose_share in the game: D2RLoader's plugin network (D2RLPlugin/network.h) carrying the poses of
// vr/pose_share.h. No vrcam globals: the plugin hands in its pose and its hero's id (Tick) and reads back the others' (Remotes), so the
// same file builds into vrcam and into the stand-alone transport probe (tools/pose_netprobe).
//
//   - the channel (poseshare::kChannel, kToken) is registered at the plugin's load only, as D2RLoader asks: [multiplayer] pose_share is read
//     then, a change needs a restart;
//   - every network call is made on the game's UI thread (a job queued kSendHz times a second) or inside D2RLoader's own callbacks;
//   - client side: connectToHost on entering a game (again after a refusal or a drop, every 3 s), our pose to the host, a ping a second;
//   - host side (the callbacks say so: only a host gets hostMessage): each peer's latest pose kept (poseshare::HostTable), the others'
//     sent to each peer in relays, a pong at once for each ping, each new peer's player asked of D2RLoader on the game thread
//     (getPeerPlayer, logged: it proves the peer is a player; the handle cannot name a unit, see pose_share.h);
//   - the log: what D2RLoader answered (every connection event with its reason, the first refusals of each kind), a line of numbers every
//     5 s (sends a second, how many RateLimited, the round trip, each player's poses got / lost / late and their age), each player's pose
//     once a second, and with [debug] pose_share_burst the back-to-back count before RateLimited.
// Visual only: nothing here touches the game's server or its packets - the plugin network is D2RLoader's own, beside the game's.
#pragma once

#include <cstddef>
#include <cstdint>

#include <D2RLPlugin/api.h>

#include "pose_share.h"

namespace posewire {

using LogFn = void (*)(const char* text);

struct Config {
    bool on = false;          // send ours, take the others' (the channel must have been registered at the load)
    int hz = poseshare::kSendHzDefault;
    bool echo = false;        // as host: a player's own pose relayed back to it too (one PC: markers on your own hands)
    bool burst = false;       // once a connection is up: messages back to back until RateLimited, counted on both ends
    uint32_t delayMs = poseshare::kDelayMsDefault;   // the playback's delay behind the newest pose
};

// At the plugin's load (D2RLoaderLoadPlugin): the channel registered when wanted. False: not wanted, no network service, or refused
// (logged). `name` is the log lines' prefix ("vrcam: pose share").
bool Init(const D2RL::PluginContext* ctx, LogFn log, bool wanted, const char* name);
bool Registered();
void Configure(const Config& c);

// The plugin's own thread, often: our pose now (nullptr: none to send - not tracked, not first person) and our hero's unit id (0 unknown),
// and whether a game's world is loaded (the connection is asked for then).
void Tick(bool inGame, const poseshare::Pose* own, uint32_t ownUnit);

// The others' poses to show now (any thread): played back delayMs behind. echo: our own too (unit = ours).
struct Remote {
    uint32_t unit = 0;
    poseshare::Pose pose;
};
size_t Remotes(Remote* out, size_t max);

// The wire's own clock, milliseconds (QueryPerformanceCounter): poses are stamped with it.
uint64_t NowMs();

void Shutdown();

}  // namespace posewire
