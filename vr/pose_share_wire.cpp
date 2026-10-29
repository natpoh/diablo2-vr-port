// pose_share_wire.cpp - [multiplayer] pose_share's network (vr/pose_share_wire.h): the channel, the client's sends, the host's relays, the
// measurements in the log.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "pose_share_wire.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#include "ui_ticket.h"

using namespace D2RL;
namespace nw = D2RL::Network;
namespace ps = poseshare;

namespace posewire {
namespace {

const PluginContext* g_ctx = nullptr;
const NetworkService* g_net = nullptr;
const ThreadService* g_threads = nullptr;
LogFn g_log = nullptr;
char g_name[48] = "pose share";
nw::ChannelHandle g_channel = nw::InvalidChannelHandle;
std::atomic<bool> g_dead{false};   // unloading: no job does anything

std::atomic<bool> g_on{false}, g_echo{false}, g_burst{false};
std::atomic<int> g_hz{ps::kSendHzDefault};
std::atomic<uint32_t> g_delayMs{ps::kDelayMsDefault};
std::atomic<bool> g_inGame{false};
uiticket::Ticket g_job;

LARGE_INTEGER g_qpf{};

void Line(const char* fmt, ...) {
    if (!g_log) return;
    char b[1400];
    int n = snprintf(b, sizeof b, "%s: ", g_name);
    if (n < 0 || n >= (int)sizeof b) n = 0;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b + n, sizeof b - n, fmt, ap);
    va_end(ap);
    g_log(b);
}

const char* ResultName(nw::Result r) {
    static const char* const k[] = {"Success", "InvalidArgument", "Unsupported", "Unavailable", "Conflict", "NotFound", "Busy", "OwnerInactive",
                                    "OwnerMismatch", "StaleHandle", "CallbackFault", "PayloadTooLarge", "NotConnected", "WrongRole", "Incompatible",
                                    "RateLimited"};
    const uint32_t i = static_cast<uint32_t>(r);
    return i < sizeof k / sizeof k[0] ? k[i] : "?";
}
const char* StateName(nw::ConnectionState s) {
    static const char* const k[] = {"Disconnected", "Connecting", "Connected", "Rejected"};
    const uint32_t i = static_cast<uint32_t>(s);
    return i < 4 ? k[i] : "?";
}
const char* ReasonName(nw::RejectionReason r) {
    static const char* const k[] = {"None", "UnknownPlugin", "UnknownChannel", "ScopeMismatch", "Incompatible", "ProtocolMismatch", "HostUnavailable",
                                    "RegistrationClosed"};
    const uint32_t i = static_cast<uint32_t>(r);
    return i < 8 ? k[i] : "?";
}

// The first few answers of each kind of a call, then only the counts in the 5 s line.
struct Told {
    std::array<std::atomic<int>, 16> n{};
    bool Once(nw::Result r, int cap = 3) {
        const uint32_t i = static_cast<uint32_t>(r);
        return i < n.size() && n[i].fetch_add(1) < cap;
    }
};
Told g_toldConnect, g_toldSend, g_toldRelay, g_toldPong;

// ---- the state (under g_lock) -------------------------------------------------------------------------------------------------------

std::mutex g_lock;

struct Client {
    nw::ConnectionState state = nw::ConnectionState::Disconnected;
    nw::RejectionReason reason = nw::RejectionReason::None;
    uint64_t triedAt = 0, connectedAt = 0;
    bool burstDone = false;
    ps::Pose own;
    bool ownFresh = false;
    uint32_t ownUnit = 0;
    uint16_t seq = 0, pingSeq = 0;
    uint64_t pingAt = 0;
    ps::RttStats rtt;
    ps::RateWindow sends, pings;
    uint32_t burstGot = 0;   // the host's burst messages that came
};
Client g_cl;

struct RemoteSlot {
    uint32_t unit = 0;   // 0 = free
    ps::Track track;
    ps::SeqStats seq;
    uint64_t loggedAt = 0;
    int64_t ageMin = INT64_MAX, ageMax = INT64_MIN;   // arrival - sent over the 5 s line (our clock - theirs: on one PC the real delay)
};
std::array<RemoteSlot, ps::kMaxPeers> g_remotes;

struct Host {
    bool is = false;   // a hostMessage came: we are the host
    ps::HostTable table;
    ps::RateWindow relays;
    uint32_t inOk = 0, inOld = 0, inWrongUnit = 0, inFull = 0, inBad = 0;
    std::array<uint64_t, ps::kMaxPeers> resolveQ{};   // new peers whose player is to be asked on the game thread
    std::array<uint64_t, ps::kMaxPeers> burstQ{};     // new peers to burst to ([debug] pose_share_burst)
    std::array<uint64_t, ps::kMaxPeers> seen{};       // every peer ever heard of (each is resolved once)
    std::array<uint32_t, ps::kMaxPeers> burstGot{};   // the burst messages each peer's burst got through, by seen[] slot
};
Host g_host;

struct ThreadIds { std::atomic<DWORD> ui{0}, hostCb{0}, clientCb{0}, connCb{0}, tick{0}; };
ThreadIds g_tid;
void NoteThread(std::atomic<DWORD>& slot, const char* what) {
    DWORD expected = 0;
    if (slot.compare_exchange_strong(expected, GetCurrentThreadId()))
        Line("the %s runs on thread %lu (UI job's %lu, the plugin's tick %lu)", what, slot.load(), g_tid.ui.load(), g_tid.tick.load());
}

int SeenSlot(uint64_t peer, bool add) {   // (under g_lock)
    for (int i = 0; i < ps::kMaxPeers; ++i) if (g_host.seen[i] == peer) return i;
    if (!add) return -1;
    for (int i = 0; i < ps::kMaxPeers; ++i) if (!g_host.seen[i]) { g_host.seen[i] = peer; return i; }
    return -1;
}
void Queue(std::array<uint64_t, ps::kMaxPeers>& q, uint64_t peer) {
    for (auto& s : q) if (s == peer) return;
    for (auto& s : q) if (!s) { s = peer; return; }
}

RemoteSlot* RemoteFor(uint32_t unit) {   // (under g_lock)
    RemoteSlot* free = nullptr;
    for (auto& r : g_remotes) {
        if (r.unit == unit) return &r;
        if (!r.unit && !free) free = &r;
    }
    if (free) { *free = RemoteSlot{}; free->unit = unit; }
    return free;
}

// The weapons of a pose for its once-a-second line: each one's kind, hand, length and radius.
std::string WeaponNote(const ps::Pose& p) {
    std::string o;
    for (int w = 0; w < 2; ++w) {
        if (!(p.flags & (w ? ps::kWeapon1 : ps::kWeapon0))) continue;
        const ps::Pose::Blade& bl = p.weapon[w];
        const float d[3] = {bl.tip[0] - bl.butt[0], bl.tip[1] - bl.butt[1], bl.tip[2] - bl.butt[2]};
        char b[120];
        snprintf(b, sizeof b, "; weapon %d: kind %u in the %s hand, %.2f m long, %.1f cm thick", w, bl.kind, bl.hand ? "left" : "right",
                 std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), bl.radius * 100.0f);
        o += b;
    }
    // formats 5 / 6: the attach bones as drawn, the hands that hold the weapon, the grips; the hands as drawn (from the eye) and the sender's units a metre
    char b[300];
    snprintf(b, sizeof b, "; attach%s%s%s%s, held by%s%s%s", p.flags2 & ps::kAttach0 ? " R" : "", p.flags2 & ps::kMirror0 ? " (mirrored)" : "",
             p.flags2 & ps::kAttach1 ? " L" : "", p.flags2 & ps::kMirror1 ? " (mirrored)" : "", p.flags2 & ps::kHoldR ? " right" : "",
             p.flags2 & ps::kHoldL ? " left" : "", (p.flags2 & (ps::kHoldR | ps::kHoldL)) ? "" : " none");
    o += b;
    if (p.flags2 & ps::kGrips) { snprintf(b, sizeof b, ", grips %.2f %.2f", p.grip[0], p.grip[1]); o += b; }
    if (p.flags2 & ps::kHands) {
        snprintf(b, sizeof b, "; hands as drawn (from the eye): right (%.2f %.2f %.2f) left (%.2f %.2f %.2f), head bone (%.2f %.2f %.2f) m, %.2f units a metre",
                 p.hand[0].p[0] - p.head.p[0], p.hand[0].p[1] - p.head.p[1], p.hand[0].p[2] - p.head.p[2], p.hand[1].p[0] - p.head.p[0], p.hand[1].p[1] - p.head.p[1],
                 p.hand[1].p[2] - p.head.p[2], p.headBone[0] - p.head.p[0], p.headBone[1] - p.head.p[1], p.headBone[2] - p.head.p[2], p.modelUpm);
        o += b;
    }
    for (int s = 0; s < 2; ++s) {   // format 7: what each hand has
        if (!(p.flags3 & (s ? ps::kHeldL : ps::kHeldR))) continue;
        const ps::Pose::Held& h = p.held[s];
        const char* state = h.state == ps::kInHand ? "in the hand" : h.state == ps::kFlying ? "flying" : "at rest";
        if (h.what == ps::kHeldProp)
            snprintf(b, sizeof b, "; %s hand: a prop (name %04X) %s at (%.2f %.2f %.2f) m", s ? "left" : "right", h.nameHash, state, h.pose.p[0], h.pose.p[1], h.pose.p[2]);
        else
            snprintf(b, sizeof b, "; %s hand: a '%.4s' x%.2f %s at (%.2f %.2f %.2f) m", s ? "left" : "right", (const char*)&h.code, h.scale, state, h.pose.p[0], h.pose.p[1],
                     h.pose.p[2]);
        o += b;
    }
    return o;
}

// ---- D2RLoader's callbacks ------------------------------------------------------------------------------------------------------------

void __cdecl ResolveJob(const PluginContext* ctx, void* user) noexcept;

void __cdecl OnHost(const PluginContext* ctx, nw::ChannelHandle, nw::PeerHandle peer, uint16_t id, const void* data, uint32_t size, void*) noexcept {
    if (g_dead.load()) return;
    NoteThread(g_tid.hostCb, "host's message callback");
    const uint64_t now = NowMs();
    {   // whatever it sent, the peer is one to relay to from now on (it may send no pose of its own: not in VR F4)
        std::lock_guard<std::mutex> lock(g_lock);
        g_host.is = true;
        g_host.table.Heard(peer, now);
    }
    if (id == ps::kMsgPing) {   // a pong at once: the round trip is the transport's, not our tick's
        uint16_t seq;
        uint32_t clientMs;
        if (!ps::ParsePing(data, size, &seq, &clientMs)) return;
        uint8_t b[ps::kPongBytes];
        const uint32_t n = ps::BuildPong(seq, clientMs, (uint32_t)now, b, sizeof b);
        const nw::Result r = g_net->sendToClient(ctx, g_channel, peer, ps::kMsgPong, b, n);
        if (r != nw::Result::Success && g_toldPong.Once(r)) Line("host: a pong to peer %llu - %s", (unsigned long long)peer, ResultName(r));
        return;
    }
    bool newPeer = false;
    const char* refused = nullptr;
    uint32_t unit = 0;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        g_host.is = true;
        newPeer = SeenSlot(peer, false) < 0;
        const int slot = SeenSlot(peer, true);
        if (newPeer) {
            Queue(g_host.resolveQ, peer);
            if (g_burst.load()) Queue(g_host.burstQ, peer);
        }
        if (id == ps::kMsgBurst) {
            if (slot >= 0) ++g_host.burstGot[slot];
            return;
        }
        if (id != ps::kMsgPose) { ++g_host.inBad; return; }
        ps::Pose p;
        if (!ps::ParsePoseMsg(data, size, &unit, &p)) { ++g_host.inBad; return; }
        switch (g_host.table.Take(peer, unit, p, now)) {
            case ps::Accept::Ok: ++g_host.inOk; break;
            case ps::Accept::Old: ++g_host.inOld; break;
            case ps::Accept::WrongUnit: if (g_host.inWrongUnit++ < 3) refused = "it named another hero than before (or none) - dropped"; break;
            case ps::Accept::Full: if (g_host.inFull++ < 3) refused = "the table is full - dropped"; break;
        }
    }
    if (newPeer) Line("host: peer %llu's first message (unit %u)", (unsigned long long)peer, unit);
    if (refused) Line("host: a pose from peer %llu (unit %u): %s", (unsigned long long)peer, unit, refused);
}

void __cdecl OnClient(const PluginContext*, nw::ChannelHandle, uint16_t id, const void* data, uint32_t size, void*) noexcept {
    if (g_dead.load()) return;
    NoteThread(g_tid.clientCb, "client's message callback");
    const uint64_t now = NowMs();
    if (id == ps::kMsgPong) {
        uint16_t seq;
        uint32_t clientMs, hostMs;
        if (!ps::ParsePong(data, size, &seq, &clientMs, &hostMs)) return;
        std::lock_guard<std::mutex> lock(g_lock);
        g_cl.rtt.Add(clientMs, (uint32_t)now);
        return;
    }
    if (id == ps::kMsgBurst) { std::lock_guard<std::mutex> lock(g_lock); ++g_cl.burstGot; return; }
    if (id != ps::kMsgRelay) return;
    ps::RelayEntry e[ps::kRelayMax];
    const int n = ps::ParseRelay(data, size, e, ps::kRelayMax);
    char lines[ps::kRelayMax][1200];
    int nl = 0;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        for (int i = 0; i < n; ++i) {
            RemoteSlot* r = RemoteFor(e[i].unit);
            if (!r || !e[i].unit || !r->seq.Add(e[i].pose.seq)) continue;
            r->track.Add(now, e[i].pose);
            const int64_t age = (int64_t)(uint32_t)now - (int64_t)e[i].pose.ms;
            r->ageMin = std::min(r->ageMin, age);
            r->ageMax = std::max(r->ageMax, age);
            if (now - r->loggedAt >= 1000) {   // each player's pose once a second
                r->loggedAt = now;
                const ps::Pose& p = e[i].pose;
                snprintf(lines[nl++], sizeof lines[0],
                         "from unit %u: seq %u (%u bytes), %s%s%s%s%s%s%s heading %.1f deg, eye %.2f m; head (%.2f %.2f %.2f) right (%.2f %.2f %.2f) left (%.2f %.2f %.2f) m%s",
                         e[i].unit, p.seq, ps::PoseBytes(p), p.flags & ps::kF4 ? "F4" : "not F4", p.flags & ps::kHead ? " head" : "", p.flags & ps::kRight ? " right" : "",
                         p.flags & ps::kLeft ? " left" : "", p.flags & ps::kBody ? " body" : "", p.flags & ps::kWeapon0 ? " weapon" : "",
                         p.flags & ps::kWeapon1 ? " weapon2," : ",", p.heading * 57.2957795f, p.eyeM, p.head.p[0], p.head.p[1], p.head.p[2],
                         p.right.p[0], p.right.p[1], p.right.p[2], p.left.p[0], p.left.p[1], p.left.p[2], WeaponNote(p).c_str());
            }
        }
    }
    for (int i = 0; i < nl; ++i) Line("%s", lines[i]);
}

void __cdecl OnConnection(const PluginContext*, const nw::ConnectionEvent* ev, void*) noexcept {
    if (g_dead.load() || !nw::HasConnectionEventField(ev, nw::ConnectionEventRequiredSize)) return;
    NoteThread(g_tid.connCb, "connection callback");
    static std::atomic<int> told{0};
    const int k = told.fetch_add(1);
    if (k < 40)
        Line("connection event: peer %llu %s (reason %s, flags 0x%X)%s", (unsigned long long)ev->peer, StateName(ev->state), ReasonName(ev->reason),
             ev->flags, k == 39 ? " - no more of these lines this run" : "");
    // A peer gone: out of the host's table. Our own connection's state is read from getChannelInfo in the UI job (whether an event is
    // the host's view of a peer or our own is not said in the event).
    if (ev->peer != nw::InvalidPeerHandle && (ev->state == nw::ConnectionState::Disconnected || ev->state == nw::ConnectionState::Rejected)) {
        std::lock_guard<std::mutex> lock(g_lock);
        g_host.table.Remove(ev->peer);
    }
}

// The host's game thread: whose is this peer (proof it is a player; the handle is D2RLoader's own, it names no unit).
void __cdecl ResolveJob(const PluginContext* ctx, void* user) noexcept {
    if (g_dead.load()) return;
    const nw::PeerHandle peer = static_cast<nw::PeerHandle>(reinterpret_cast<uintptr_t>(user));
    PlayerHandle player = InvalidPlayerHandle;
    const nw::Result r = g_net->getPeerPlayer(ctx, g_channel, peer, &player);
    Line("host: peer %llu is player handle 0x%llX (getPeerPlayer %s, on the game thread %lu)", (unsigned long long)peer, (unsigned long long)player,
         ResultName(r), GetCurrentThreadId());
}

// Back to back until RateLimited (or 400): how many go out, in how long. sendFn sends one.
template <class F> void Burst(const char* who, F&& sendFn) {
    uint8_t b[ps::kBurstBytes] = {};
    LARGE_INTEGER t0, t1;
    QueryPerformanceCounter(&t0);
    int ok = 0;
    nw::Result last = nw::Result::Success;
    for (int i = 0; i < 400; ++i) {
        ps::BuildPing((uint16_t)i, (uint32_t)NowMs(), b, sizeof b);
        last = sendFn(b, (uint32_t)sizeof b);
        if (last != nw::Result::Success) break;
        ++ok;
    }
    QueryPerformanceCounter(&t1);
    const double us = g_qpf.QuadPart ? (double)(t1.QuadPart - t0.QuadPart) * 1e6 / (double)g_qpf.QuadPart : 0.0;
    Line("burst %s: %d messages of %u bytes went out back to back in %.0f us, then %s", who, ok, (unsigned)sizeof b, us,
         ok == 400 ? "stopped (400, no limit met)" : ResultName(last));
}

// ---- the UI thread's job, kSendHz times a second ------------------------------------------------------------------------------------

void __cdecl UiJob(const PluginContext* ctx, void* user) noexcept {
    if (!g_job.Claim(uiticket::FromUser(user)) || g_dead.load() || !g_on.load()) return;
    {
        DWORD expected = 0;
        g_tid.ui.compare_exchange_strong(expected, GetCurrentThreadId());
    }
    const uint64_t now = NowMs();
    // the connection as D2RLoader has it; asked for in a game, again after a refusal or a drop
    nw::ChannelInfo info{};
    info.structSize = nw::ChannelInfoSize;
    const bool infoOk = g_net->getChannelInfo(ctx, g_channel, &info) == nw::Result::Success;
    nw::ConnectionState st;
    nw::RejectionReason why;
    uint64_t triedAt;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        if (infoOk) {
            if (info.connectionState == nw::ConnectionState::Connected && g_cl.state != nw::ConnectionState::Connected) {
                g_cl.connectedAt = now;
                g_cl.burstDone = false;
            }
            g_cl.state = info.connectionState;
            g_cl.reason = info.rejectionReason;
        }
        st = g_cl.state;
        why = g_cl.reason;
        triedAt = g_cl.triedAt;
    }
    {
        static nw::ConnectionState told = nw::ConnectionState::Disconnected;
        static nw::RejectionReason toldWhy = nw::RejectionReason::None;
        if (st != told || why != toldWhy) {
            Line("our connection: %s -> %s (reason %s)", StateName(told), StateName(st), ReasonName(why));
            told = st;
            toldWhy = why;
        }
    }
    // (Connecting for 10 s: asked again - D2RLoader says to retry after a time-out)
    if (g_inGame.load() && st != nw::ConnectionState::Connected && now - triedAt >= (st == nw::ConnectionState::Connecting ? 10000u : 3000u)) {
        const nw::Result r = g_net->connectToHost(ctx, g_channel);
        {
            std::lock_guard<std::mutex> lock(g_lock);
            g_cl.triedAt = now;
            if (r == nw::Result::Success && g_cl.state != nw::ConnectionState::Connected) g_cl.state = nw::ConnectionState::Connecting;
        }
        if (g_toldConnect.Once(r)) Line("connectToHost: %s", ResultName(r));
    }
    // ours to the host, a ping a second, a burst once
    uint8_t b[ps::kMaxPayload];
    uint32_t n = 0;
    bool ping = false, burst = false;
    uint16_t pingSeq = 0;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        if (g_cl.state == nw::ConnectionState::Connected) {
            if (g_cl.ownFresh && g_cl.ownUnit) {
                n = ps::BuildPoseMsg(g_cl.ownUnit, g_cl.own, b, sizeof b);
                g_cl.ownFresh = false;
            }
            if (now - g_cl.pingAt >= 1000) { ping = true; g_cl.pingAt = now; pingSeq = ++g_cl.pingSeq; }
            if (g_burst.load() && !g_cl.burstDone && now - g_cl.connectedAt >= 2000) { burst = true; g_cl.burstDone = true; }
        }
    }
    if (n) {
        const nw::Result r = g_net->sendToHost(ctx, g_channel, ps::kMsgPose, b, n);
        {
            std::lock_guard<std::mutex> lock(g_lock);
            if (r == nw::Result::Success) ++g_cl.sends.ok;
            else if (r == nw::Result::RateLimited) ++g_cl.sends.limited;
            else ++g_cl.sends.other;
        }
        if (r != nw::Result::Success && g_toldSend.Once(r)) Line("sendToHost (pose): %s", ResultName(r));
    }
    if (ping) {
        uint8_t pb[ps::kPingBytes];
        const nw::Result r = g_net->sendToHost(ctx, g_channel, ps::kMsgPing, pb, ps::BuildPing(pingSeq, (uint32_t)NowMs(), pb, sizeof pb));
        std::lock_guard<std::mutex> lock(g_lock);
        if (r == nw::Result::Success) ++g_cl.pings.ok;
        else if (r == nw::Result::RateLimited) ++g_cl.pings.limited;
        else ++g_cl.pings.other;
    }
    if (burst) Burst("to the host", [&](const uint8_t* m, uint32_t sz) { return g_net->sendToHost(ctx, g_channel, ps::kMsgBurst, m, sz); });

    // the host: the others' poses to each peer, each new peer's player asked, a burst to each new peer
    std::array<uint64_t, ps::kMaxPeers> resolve{}, burstTo{};
    std::array<uint64_t, ps::kMaxPeers> dests{};
    int nEntries[ps::kMaxPeers] = {};
    static ps::RelayEntry perDest[ps::kMaxPeers][ps::kMaxPeers];   // (the UI thread only)
    int nd = 0;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        if (g_host.is) {
            resolve = g_host.resolveQ; g_host.resolveQ = {};
            burstTo = g_host.burstQ; g_host.burstQ = {};
            g_host.table.Forget(now, 5000);   // silent for 5 s (no pose, no ping): no longer relayed to or from
            for (const auto& s : g_host.table.p) {
                if (!s.peer) continue;
                nEntries[nd] = g_host.table.Build(s.peer, g_echo.load(), perDest[nd], ps::kMaxPeers);
                dests[nd++] = s.peer;
            }
            g_host.table.Flush();
        }
    }
    for (int d = 0; d < nd; ++d)
        for (int at = 0; at < nEntries[d];) {   // as few relays as the poses fit (two with bodies and weapons, four bare)
            int taken = 0;
            const uint32_t sz = ps::BuildRelay(perDest[d] + at, nEntries[d] - at, b, sizeof b, &taken);
            if (!sz || !taken) break;
            at += taken;
            const nw::Result res = g_net->sendToClient(ctx, g_channel, dests[d], ps::kMsgRelay, b, sz);
            {
                std::lock_guard<std::mutex> lock(g_lock);
                if (res == nw::Result::Success) ++g_host.relays.ok;
                else if (res == nw::Result::RateLimited) ++g_host.relays.limited;
                else ++g_host.relays.other;
            }
            if (res != nw::Result::Success && g_toldRelay.Once(res)) Line("host: sendToClient (relay) to peer %llu: %s", (unsigned long long)dests[d], ResultName(res));
        }
    for (const uint64_t peer : resolve) {
        if (!peer) continue;
        const Threads::Result r = g_threads->runOnGameThread ? g_threads->runOnGameThread(ctx, &ResolveJob, reinterpret_cast<void*>(static_cast<uintptr_t>(peer)))
                                                             : Threads::Result::Unavailable;
        if (r != Threads::Result::Success) Line("host: peer %llu's player could not be asked (runOnGameThread %u)", (unsigned long long)peer, (unsigned)r);
    }
    for (const uint64_t peer : burstTo)
        if (peer) {
            char who[48];
            snprintf(who, sizeof who, "to peer %llu", (unsigned long long)peer);
            Burst(who, [&](const uint8_t* m, uint32_t sz) { return g_net->sendToClient(ctx, g_channel, peer, ps::kMsgBurst, m, sz); });
        }
}

// The 5 s line of numbers.
void Report() {
    char b[1400];
    int n = 0;
    auto add = [&](const char* fmt, auto... a) {
        if (n < (int)sizeof b - 1) {
            const int k = snprintf(b + n, sizeof b - n, fmt, a...);
            if (k > 0) n = std::min<int>(n + k, (int)sizeof b - 1);
        }
    };
    {
        std::lock_guard<std::mutex> lock(g_lock);
        add("%s (%s); poses sent %u/s, RateLimited %u/s, other %u/s; pings %u/s (limited %u); round trip ", StateName(g_cl.state), ReasonName(g_cl.reason),
            g_cl.sends.lastOk, g_cl.sends.lastLimited, g_cl.sends.lastOther, g_cl.pings.lastOk, g_cl.pings.lastLimited);
        if (g_cl.rtt.n) add("%.0f ms (least %.0f, average %.0f, %u pongs)", g_cl.rtt.last, g_cl.rtt.least, g_cl.rtt.avg, g_cl.rtt.n);
        else add("none yet");
        if (g_cl.burstGot) add("; the host's burst: %u came", g_cl.burstGot);
        int players = 0;
        for (auto& r : g_remotes) {
            if (!r.unit) continue;
            ++players;
            add("; unit %u: got %u lost %u late %u", r.unit, r.seq.got, r.seq.lost, r.seq.late);
            if (r.ageMin != INT64_MAX) add(", arrival - sent %lld..%lld ms", (long long)r.ageMin, (long long)r.ageMax);
            r.ageMin = INT64_MAX; r.ageMax = INT64_MIN;
        }
        if (!players) add("; no other player's pose");
        if (g_host.is) {
            add(" | host: %d peers (%d sending poses), relays sent %u/s, RateLimited %u/s, other %u/s; poses in ok %u old %u wrong-unit %u full %u bad %u", g_host.table.Count(), g_host.table.Senders(),
                g_host.relays.lastOk, g_host.relays.lastLimited, g_host.relays.lastOther, g_host.inOk, g_host.inOld, g_host.inWrongUnit, g_host.inFull, g_host.inBad);
            for (int i = 0; i < ps::kMaxPeers; ++i)
                if (g_host.burstGot[i]) add("; peer %llu's burst: %u came", (unsigned long long)g_host.seen[i], g_host.burstGot[i]);
        }
    }
    Line("%s", b);
}

}  // namespace

uint64_t NowMs() {
    if (!g_qpf.QuadPart) QueryPerformanceFrequency(&g_qpf);
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (uint64_t)(c.QuadPart / (g_qpf.QuadPart / 1000));
}

bool Init(const PluginContext* ctx, LogFn log, bool wanted, const char* name) {
    g_ctx = ctx;
    g_log = log;
    if (name) snprintf(g_name, sizeof g_name, "%s", name);
    QueryPerformanceFrequency(&g_qpf);
    if (!wanted || !ctx) return false;
    if (ctx->QueryService(&g_net) != ServiceQueryResult::Success || !HasNetworkServiceField(g_net, NetworkServiceRequiredSize)) {
        g_net = nullptr;
        Line("D2RLoader has no network service (an older D2RLoader?) - off");
        return false;
    }
    if (ctx->QueryService(&g_threads) != ServiceQueryResult::Success || !HasThreadServiceField(g_threads, ThreadServiceRequiredSize)) {
        g_threads = nullptr;
        Line("D2RLoader has no thread service - off");
        return false;
    }
    nw::ChannelRegistration reg{};
    reg.structSize = nw::ChannelRegistrationSize;
    reg.localChannelId = ps::kChannel;
    reg.compatibilityToken = ps::kToken;
    reg.hostMessage = &OnHost;
    reg.clientMessage = &OnClient;
    reg.connectionState = &OnConnection;
    const nw::Result r = g_net->registerChannel(ctx, &reg, &g_channel);
    if (r != nw::Result::Success) {
        g_channel = nw::InvalidChannelHandle;
        Line("registerChannel: %s - off for this run", ResultName(r));
        return false;
    }
    nw::ChannelInfo info{};
    info.structSize = nw::ChannelInfoSize;
    const nw::Result ri = g_net->getChannelInfo(ctx, g_channel, &info);
    Line("channel %u registered (token 0x%016llX; getChannelInfo %s: state %u, connection %s) - [multiplayer] pose_share; F4 only",
         (unsigned)ps::kChannel, (unsigned long long)ps::kToken, ResultName(ri), (unsigned)info.state, StateName(info.connectionState));
    return true;
}

bool Registered() { return g_channel != nw::InvalidChannelHandle; }

void Configure(const Config& c) {
    const bool was = g_on.exchange(c.on && Registered());
    g_hz.store(std::clamp(c.hz, 5, 90));
    g_echo.store(c.echo);
    g_burst.store(c.burst);
    g_delayMs.store(std::min<uint32_t>(c.delayMs, 500));
    if (c.on && !Registered() && g_net == nullptr && g_ctx) {
        static bool told = false;
        if (!told) { told = true; Line("[multiplayer] pose_share is ticked, but it takes effect at the game's next start (the channel is registered only then)"); }
    }
    if (was != g_on.load())
        Line("%s (%d a second, echo %d, burst %d, playback %u ms behind)", g_on.load() ? "on" : "off", g_hz.load(), (int)c.echo, (int)c.burst, g_delayMs.load());
    if (!g_on.load()) {
        std::lock_guard<std::mutex> lock(g_lock);
        for (auto& r : g_remotes) r = RemoteSlot{};
    }
}

void Tick(bool inGame, const ps::Pose* own, uint32_t ownUnit) {
    if (!g_on.load(std::memory_order_relaxed) || g_dead.load()) return;
    {
        DWORD expected = 0;
        g_tid.tick.compare_exchange_strong(expected, GetCurrentThreadId());
    }
    const uint64_t now = NowMs();
    const bool wasInGame = g_inGame.exchange(inGame);
    if (inGame != wasInGame) {   // a game entered or left: the connection's numbers start afresh (the 2026-10-26 test read a left game's pongs as a live connection)
        {
            std::lock_guard<std::mutex> lock(g_lock);
            g_cl = Client{};   // (disconnected, asked again at once)
            if (!inGame) { g_host = Host{}; for (auto& r : g_remotes) r = RemoteSlot{}; }
        }
        Line("%s", inGame ? "in a game: the channel is asked for" : "out of the game: the connection let go, its numbers start afresh");
    }
    static uint64_t nextSend = 0, nextReport = 0;
    if (now >= nextSend) {
        nextSend = now + 1000 / (uint64_t)std::max(1, g_hz.load());
        {
            std::lock_guard<std::mutex> lock(g_lock);
            if (own) {
                g_cl.own = *own;
                g_cl.own.seq = ++g_cl.seq;
                g_cl.own.ms = (uint32_t)now;
                g_cl.ownFresh = true;
                g_cl.ownUnit = ownUnit;
            }
            g_cl.sends.Roll(now); g_cl.pings.Roll(now); g_host.relays.Roll(now);
        }
        if (g_threads && g_threads->runOnUiThread)
            if (const uint32_t token = g_job.Start(now))
                if (g_threads->runOnUiThread(g_ctx, &UiJob, uiticket::ToUser(token)) != Threads::Result::Success) g_job.Claim(token);
    }
    if (now >= nextReport) {
        if (nextReport) Report();
        nextReport = now + 5000;
    }
}

size_t Remotes(Remote* out, size_t max) {
    if (!out || !max || !g_on.load(std::memory_order_relaxed)) return 0;
    const uint64_t now = NowMs();
    const uint32_t delay = g_delayMs.load();
    size_t k = 0;
    std::lock_guard<std::mutex> lock(g_lock);
    for (const auto& r : g_remotes) {
        if (!r.unit || k >= max) continue;
        ps::Pose p;
        if (!r.track.Sample(now, delay, &p) || !ps::Plausible(p)) continue;
        out[k].unit = r.unit;
        out[k].pose = p;
        ++k;
    }
    return k;
}

void Shutdown() {
    g_dead.store(true);
    if (g_net && g_ctx && Registered()) g_net->unregisterChannel(g_ctx, g_channel);
    g_channel = nw::InvalidChannelHandle;
}

}  // namespace posewire
