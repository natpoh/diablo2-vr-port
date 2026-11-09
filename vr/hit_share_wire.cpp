// hit_share_wire.cpp - [multiplayer] share_hit_outcomes on the game: the channel "HS", the host's sends, the joined client's hello and what it
// takes in. What and why: vr/hit_share.h. Only shown: nothing here touches the game's server, its packets or a blow.
//
// Threads: D2RLoader's callbacks (OnHost / OnClient / OnConnection) and vrcam's update loop (Tick) - the same two the host's rules channel
// (vr/flat_menu.cpp "HR") makes its calls on. The peers' table is under one lock; the host's ring (hitoutcome::ShareRing) has one reader,
// Tick; the client's LabelRing takes many writers (the callback is one).

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <D2RLPlugin/api.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <mutex>

#include "hit_outcome.h"
#include "hit_share.h"
#include "hit_share_wire.h"
#include "log_async.h"

namespace hitshare {
namespace {

using namespace D2RL;
namespace nw = D2RL::Network;

const PluginContext* g_ctx = nullptr;
const NetworkService* g_net = nullptr;
nw::ChannelHandle g_channel = nw::InvalidChannelHandle;
std::atomic<bool> g_dead{false};
uint32_t g_myNonce = 0;   // this process's number (Init)

void Say(const char* fmt, ...) {
    if (!g_ctx) return;
    char b[512];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    logasync::Info(g_ctx, b);
}

// ---- the host's side ----
std::mutex g_peersLock;
Peers g_peers;                                  // (under g_peersLock)
struct HostCount { uint32_t recs = 0, msgs = 0, failed = 0, noPeer = 0, old = 0, over = 0, lost = 0; nw::Result lastFail = nw::Result::Success; };
HostCount g_hc;                                 // (Tick only)

// ---- the joined client's side ----
std::atomic<uint32_t> g_hero{0};                // our hero's id (Tick writes, the callback reads)
std::atomic<uint32_t> g_hostNonce{0};
std::atomic<bool> g_hostSharing{false};
std::atomic<uint64_t> g_hostAt{0};              // when the host last answered a hello
std::atomic<uint32_t> g_gotOurs{0}, g_gotOthers{0}, g_gotOff{0};   // records taken / for another hero / with our timer's numbers off
std::atomic<int> g_failed{0};                   // asks of the host's channel that came to nothing since the last connection

// A joined client's next ask for the host's channel: 10 s while one is under way; else 3 s, then 10, 30 and 60 s after each that came to
// nothing (as vr/host_rules.h RetryMs: a host without this version refuses each with lines in D2RLoader's log).
uint64_t RetryMs(unsigned failed, bool connecting) {
    if (connecting) return 10000;
    static constexpr uint64_t kSteps[] = {3000, 10000, 30000, 60000};
    return kSteps[failed < 3 ? failed : 3];
}

void __cdecl OnHost(const PluginContext*, nw::ChannelHandle, nw::PeerHandle peer, uint16_t id, const void* data, uint32_t size, void*) noexcept {
    if (g_dead.load() || id != kMsgHello) return;
    uint32_t nonce = 0, hero = 0;
    if (!ParseHello(data, size, &nonce, &hero)) return;
    const uint64_t now = GetTickCount64();
    bool taken = false;
    {
        std::lock_guard<std::mutex> lock(g_peersLock);
        taken = g_peers.Hello(static_cast<uint64_t>(peer), nonce, hero, g_myNonce, now);
    }
    const bool sharing = hitoutcome::ShareWanted();
    uint8_t b[kStateBytes];
    if (const uint32_t n = BuildState(nonce, g_myNonce, sharing, b, sizeof b))
        if (g_net && g_ctx) g_net->sendToClient(g_ctx, g_channel, peer, kMsgState, b, n);
    static std::atomic<int> told{0};
    if (nonce != g_myNonce && told.fetch_add(1) < 6)
        Say("vrcam: hit share - host: a joined player's mod (hero %u) asked for its blows' outcomes: %s", hero,
            !sharing ? "our box is NOT ticked ([multiplayer] share_hit_outcomes=0) - it gets none"
            : taken  ? "sent as they come"
                     : "no room (eight players already) - it gets none");
}

void __cdecl OnClient(const PluginContext*, nw::ChannelHandle, uint16_t id, const void* data, uint32_t size, void*) noexcept {
    if (g_dead.load()) return;
    if (id == kMsgState) {
        uint32_t asker = 0, host = 0;
        bool sharing = false;
        if (!ParseState(data, size, &asker, &host, &sharing) || asker != g_myNonce) return;
        const uint32_t wasHost = g_hostNonce.exchange(host);
        const bool wasSharing = g_hostSharing.exchange(sharing);
        g_hostAt.store(GetTickCount64());
        if (wasHost != host || wasSharing != sharing) {
            if (host == g_myNonce)
                Say("vrcam: hit share - our own game (single player or we host): the swing timer reads our server's outcomes itself");
            else
                Say("vrcam: hit share - joined: the host %s", sharing ? "sends us our blows' outcomes (hit, miss, blocked, the life taken) for the swing timer"
                                                                       : "does not send outcomes (its [multiplayer] share_hit_outcomes is off): the timer alone");
        }
        return;
    }
    if (id != kMsgOutcomes) return;
    uint32_t host = 0;
    Rec recs[kRecsPerMsg];
    int n = 0;
    if (!ParseOutcomes(data, size, &host, recs, kRecsPerMsg, &n) || host == g_myNonce) return;
    if (!hitoutcome::LabelsWanted()) { g_gotOff.fetch_add(static_cast<uint32_t>(n)); return; }   // (our timer's numbers were unticked meanwhile)
    const uint32_t hero = g_hero.load();
    const uint64_t now = GetTickCount64();
    for (int i = 0; i < n; ++i) {
        if (hero && recs[i].attacker != hero) { g_gotOthers.fetch_add(1); continue; }   // (the host sorts by hero; a new game's hero not told yet)
        hitoutcome::LabelRing().Push(RecordOf(recs[i], now));
        g_gotOurs.fetch_add(1);
    }
    static std::atomic<int> told{0};
    if (told.fetch_add(1) < 4) {
        const Rec& r = recs[0];
        Say("vrcam: hit share - joined: %d outcome(s) from the host, the first %s (hero %u, ours %u)", n,
            r.kind == hitoutcome::Kind::Roll ? hitoutcome::RollText(r.flags) : (r.flags & hitoutcome::kCrit) ? "a landed blow, a crit" : "a landed blow",
            r.attacker, hero);
    }
}

void __cdecl OnConnection(const PluginContext*, const nw::ConnectionEvent* ev, void*) noexcept {
    if (!ev || g_dead.load()) return;
    if (ev->state == nw::ConnectionState::Disconnected || ev->state == nw::ConnectionState::Rejected) {
        {
            std::lock_guard<std::mutex> lock(g_peersLock);
            if (ev->peer != nw::InvalidPeerHandle) g_peers.Drop(static_cast<uint64_t>(ev->peer));
        }
        g_hostAt.store(0);
        if (ev->state == nw::ConnectionState::Rejected) g_failed.fetch_add(1);
    }
    static std::atomic<int> told{0};
    if (told.fetch_add(1) < 8) Say("vrcam: hit share - the channel: state %u, reason %u", (unsigned)ev->state, (unsigned)ev->reason);
}

// The host: what the observers saw since the last round, each peer's own hero's, sent to it.
void HostTick(uint64_t now) {
    static Batches batches;   // (Tick only)
    batches.Clear();
    hitoutcome::Record r;
    uint32_t lost = 0;
    bool any = false;
    {
        std::lock_guard<std::mutex> lock(g_peersLock);
        while (hitoutcome::ShareRing().Pop(&r, &lost)) {
            Rec x;
            if (!RecOf(r, &x)) continue;
            if (r.ms && now > r.ms && now - r.ms > 1500) { ++g_hc.old; continue; }   // (a stall: too late to belong to the swing on the other screen)
            if (batches.Add(g_peers, x, now)) any = true;
            else ++g_hc.noPeer;   // our own hero's, or a player without the mod / without the timer's numbers
        }
    }
    g_hc.lost += lost;
    if (!any || !g_net || !g_ctx) return;
    for (int p = 0; p < kMaxPeers; ++p) {
        const Batches::Batch& b = batches.All()[p];
        if (!b.handle) continue;
        g_hc.over += b.over;
        for (int at = 0; at < b.n; at += kRecsPerMsg) {
            const int k = b.n - at < kRecsPerMsg ? b.n - at : kRecsPerMsg;
            uint8_t buf[kMaxPayload];
            const uint32_t sz = BuildOutcomes(g_myNonce, b.r + at, k, buf, sizeof buf);
            if (!sz) continue;
            const nw::Result res = g_net->sendToClient(g_ctx, g_channel, static_cast<nw::PeerHandle>(b.handle), kMsgOutcomes, buf, sz);
            if (res == nw::Result::Success) { ++g_hc.msgs; g_hc.recs += static_cast<uint32_t>(k); }
            else { ++g_hc.failed; g_hc.lastFail = res; }
        }
    }
}

// A joined client (or our own process on its own channel): the connection, asked for again, and the hello once a second.
void ClientTick(uint64_t now, bool want, uint32_t hero) {
    static uint64_t triedAt = 0, helloAt = 0;
    static bool was = false;
    if (!want) {
        if (was) { triedAt = 0; g_failed.store(0); g_hostAt.store(0); g_hostNonce.store(0); g_hostSharing.store(false); }   // a new game: asked again at once
        was = false;
        return;
    }
    was = true;
    nw::ChannelInfo info{};
    info.structSize = nw::ChannelInfoSize;
    const bool got = g_net->getChannelInfo(g_ctx, g_channel, &info) == nw::Result::Success;
    const bool conn = got && info.connectionState == nw::ConnectionState::Connected;
    const bool connecting = got && info.connectionState == nw::ConnectionState::Connecting;
    if (conn) {
        g_failed.store(0);
        if (now - helloAt >= kHelloMs) {
            helloAt = now;
            uint8_t b[kHelloBytes];
            if (const uint32_t n = BuildHello(g_myNonce, hero, b, sizeof b)) g_net->sendToHost(g_ctx, g_channel, kMsgHello, b, n);
        }
        return;
    }
    if (triedAt && now - triedAt < RetryMs(static_cast<unsigned>(g_failed.load()), connecting)) return;
    if (triedAt && !connecting) g_failed.fetch_add(1);
    triedAt = now;
    const nw::Result r = g_net->connectToHost(g_ctx, g_channel);
    static int toldR = -1;
    if (static_cast<int>(r) != toldR) {
        toldR = static_cast<int>(r);
        Say("vrcam: hit share - asked the host's channel: %u (0 = asked; online, or a host without this mod version: no answer, asked again "
            "later, up to once a minute)", (unsigned)r);
    }
}

// Every 10 s while anything moved: what went over the channel.
void Report(uint64_t now) {
    static uint64_t at = 0;
    if (now - at < 10000) return;
    at = now;
    int peers = 0;
    {
        std::lock_guard<std::mutex> lock(g_peersLock);
        peers = g_peers.Count(now);
    }
    const uint32_t ours = g_gotOurs.exchange(0), others = g_gotOthers.exchange(0), off = g_gotOff.exchange(0);
    if (g_hc.recs || g_hc.failed || g_hc.lost || g_hc.over || g_hc.old)
        Say("vrcam: hit share - host, last 10 s: %d joined player(s) with the mod, %u outcome(s) sent in %u message(s), %u send(s) refused (last %u), "
            "%u too old, %u past a round's cap, %u lost (the ring was full)",
            peers, g_hc.recs, g_hc.msgs, g_hc.failed, (unsigned)g_hc.lastFail, g_hc.old, g_hc.over, g_hc.lost);
    if (ours || others || off)
        Say("vrcam: hit share - joined, last 10 s: %u outcome(s) of our hero shown, %u for another hero, %u with our timer's numbers off", ours, others, off);
    g_hc = HostCount{};
}

}  // namespace

void Init(const PluginContext* ctx) {
    g_ctx = ctx;
    {
        LARGE_INTEGER t{};
        QueryPerformanceCounter(&t);
        const uint32_t n = static_cast<uint32_t>(t.QuadPart ^ (t.QuadPart >> 32)) ^ (GetCurrentProcessId() * 2246822519u);
        g_myNonce = n ? n : 1;
    }
    if (!ctx) return;
    if (ctx->QueryService(&g_net) != ServiceQueryResult::Success || !HasNetworkServiceField(g_net, NetworkServiceRequiredSize)) {
        g_net = nullptr;
        ctx->LogInfo("vrcam: hit share - D2RLoader has no network service: a joined player's swing timer shows no outcomes");
        return;
    }
    nw::ChannelRegistration reg{};
    reg.structSize = nw::ChannelRegistrationSize;
    reg.localChannelId = kChannel;
    reg.compatibilityToken = kToken;
    reg.hostMessage = &OnHost;
    reg.clientMessage = &OnClient;
    reg.connectionState = &OnConnection;
    const nw::Result r = g_net->registerChannel(ctx, &reg, &g_channel);
    char b[220];
    snprintf(b, sizeof b, "vrcam: hit share - channel %u %s (the host sends each joined player his own blows' outcomes for the swing timer: only shown)",
             (unsigned)kChannel, r == nw::Result::Success ? "registered" : "NOT registered");
    ctx->LogInfo(b);
    if (r != nw::Result::Success) g_channel = nw::InvalidChannelHandle;
}

void Shutdown() {
    g_dead.store(true);
    hitoutcome::ShareFlag().store(false);
    if (g_net && g_ctx && g_channel != nw::InvalidChannelHandle) g_net->unregisterChannel(g_ctx, g_channel);
    g_channel = nw::InvalidChannelHandle;
}

void SetWanted(bool on) {
    if (hitoutcome::ShareFlag().exchange(on) == on) return;
    Say(on ? "vrcam: hit share - [multiplayer] share_hit_outcomes=1: as the host, each joined player with the mod gets his own blows' outcomes (hit, miss, "
             "blocked, dodged, avoided, the life taken, a crit) for his swing timer; read only, nothing changes in play"
           : "vrcam: hit share - [multiplayer] share_hit_outcomes=0: the joined players get no outcomes from us");
    if (!on) {
        std::lock_guard<std::mutex> lock(g_peersLock);
        hitoutcome::Record r;
        uint32_t lost = 0;
        while (hitoutcome::ShareRing().Pop(&r, &lost)) {}   // (what was pushed before the box went off: not sent later)
    }
}

void Tick(uint64_t /*nowMs*/, bool inWorld, uint32_t heroId) {
    if (g_dead.load() || !g_net || g_channel == nw::InvalidChannelHandle) return;
    const uint64_t now = GetTickCount64();   // (the observers' clock)
    static uint64_t at = 0;
    if (now - at < 30) return;   // ~30 rounds a second: an outcome reaches the other screen within a frame or two of the network
    at = now;
    g_hero.store(heroId);
    if (hitoutcome::ShareWanted()) HostTick(now);
    ClientTick(now, inWorld && heroId && hitoutcome::LabelsWanted(), heroId);
    Report(now);
}

}  // namespace hitshare
