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
namespace gs = grabshare;

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
std::atomic<ShotSink> g_shotSink{nullptr};   // (vr/shot_share.h) the shots that come

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
    uint32_t relaysGot = 0;  // relay messages from the host, all along (the ping's receipt: the host knows what arrives)
    uint16_t pongsGot = 0;
    shotshare::Shot shot;    // our latest bow shot (ShotOut), sent once
    uint32_t shotUnit = 0;
    bool shotFresh = false;
    uint32_t shotsOut = 0, shotsIn = 0;   // for the 5 s line
};
Client g_cl;

struct RemoteSlot {
    uint32_t unit = 0;   // 0 = free
    ps::Track track;
    ps::SeqStats seq;
    uint64_t loggedAt = 0;
    int64_t ageMin = INT64_MAX, ageMax = INT64_MIN;   // arrival - sent over the 5 s line (our clock - theirs: on one PC the real delay)
    // the playback delay (Track::FitDelay, eased: up within a third of a second, down over two - so the shown pose never jumps) and the arrivals for the log
    float delayMs = 0.0f;
    uint64_t delayAt = 0;
    uint32_t arrived = 0;   // poses taken since the last 5 s line
    uint64_t arrivedSince = 0;
};
std::array<RemoteSlot, ps::kMaxPeers> g_remotes;

// [multiplayer] pose_share_grab (vr/grab_share.h): our holds to send (SetOwnGrabs), each holder's hand as it came (played back by RemoteGrabs), the counts
struct OwnGrabs { gs::Hold h[gs::kGrabMsgMax]; int n = 0; bool fresh = false; };
gs::KnockQueue g_knocks;   // [multiplayer] pose_share_grab: the others' knocks, each played once (RemoteKnocks)
OwnGrabs g_ownGrabs;
struct GrabSlot {
    uint32_t unit = 0;   // 0 = free
    uint8_t side = 0;
    gs::GrabTrack track;
};
std::array<GrabSlot, 2 * ps::kMaxPeers> g_grabSlots;
constexpr uint64_t kGrabForgetMs = 4000;   // a holder's hand heard of no more this long: its slot is free (its hold played out long before)
struct GrabCounts { uint32_t sent = 0, got = 0, relayed = 0, refused = 0; };
GrabCounts g_grabCounts;   // (since the last 5 s line)

int SeenSlot(uint64_t peer, bool add);   // (below)
struct Host {
    bool is = false;   // a hostMessage came: we are the host
    ps::HostTable table;
    ps::RateWindow relays;
    uint32_t inOk = 0, inOld = 0, inWrongUnit = 0, inFull = 0, inBad = 0;
    std::array<uint64_t, ps::kMaxPeers> resolveQ{};   // new peers whose player is to be asked on the game thread
    std::array<uint64_t, ps::kMaxPeers> burstQ{};     // new peers to burst to ([debug] pose_share_burst)
    std::array<uint64_t, ps::kMaxPeers> seen{};       // every peer ever heard of (each is resolved once)
    std::array<uint32_t, ps::kMaxPeers> burstGot{};   // the burst messages each peer's burst got through, by seen[] slot
    std::array<ps::RelayPace, ps::kMaxPeers> pace{};  // the relays' pace to each peer by its receipts, by seen[] slot
    gs::HostGrabs grabs;                              // [multiplayer] pose_share_grab: each peer's hands' latest holds, for the others
    // what D2RLoader answered our sends to each peer since the last 5 s line (relays and pongs), by seen[] slot: ok, RateLimited, Busy, other (the last other's code)
    struct Sends { uint32_t ok = 0, limited = 0, busy = 0, other = 0; nw::Result lastOther = nw::Result::Success; };
    std::array<Sends, ps::kMaxPeers> sends{};
    // the pongs to send, from the UI job - not from inside D2RLoader's host message callback (0.193.3: the downstream stalled after the first second; a send
    // made inside its own callback was always an open question)
    struct Pong { uint64_t peer = 0; uint16_t seq = 0; uint32_t clientMs = 0; uint64_t at = 0; };
    std::array<Pong, 16> pongQ{};
    int nPong = 0;
    // the players' shots to hand on to all the others, from the UI job (vr/shot_share.h): as they came, with the peer they came from
    struct ShotFwd { uint64_t from = 0; uint32_t n = 0; uint8_t b[shotshare::kShotMsgBytes] = {}; };
    std::array<ShotFwd, 16> shotQ{};
    int nShot = 0;
    uint32_t shotsIn = 0, shotsOut = 0;
};
Host g_host;
void CountSend(uint64_t peer, nw::Result r) {   // (under g_lock)
    const int slot = SeenSlot(peer, false);
    if (slot < 0) return;
    Host::Sends& s = g_host.sends[slot];
    if (r == nw::Result::Success) ++s.ok;
    else if (r == nw::Result::RateLimited) ++s.limited;
    else if (r == nw::Result::Busy) ++s.busy;
    else { ++s.other; s.lastOther = r; }
}

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
        uint32_t relaysGot = 0;
        uint16_t pongsGot = 0;
        ps::RelayPace::Event ev = ps::RelayPace::kNone;
        int hz = 0;
        uint32_t lastGot = 0, lastSent = 0, had = 0, sent = 0, stalledAt = 0, stalls = 0;
        {
            std::lock_guard<std::mutex> lock(g_lock);
            if (ps::ParsePingReceipt(data, size, &relaysGot, &pongsGot)) {   // what of ours it has had: the relays' pace to it
                const int slot = SeenSlot(peer, true);
                if (slot >= 0) {
                    ps::RelayPace& pc = g_host.pace[slot];
                    ev = pc.Receipt(relaysGot, now, g_hz.load());
                    hz = pc.Rate(now, g_hz.load());
                    lastGot = pc.lastGot; lastSent = pc.lastSent;
                    had = pc.got; sent = pc.sent; stalledAt = pc.stalledAt; stalls = pc.stalls;
                }
            }
            if (g_host.nPong < (int)g_host.pongQ.size()) g_host.pongQ[g_host.nPong++] = {peer, seq, clientMs, now};   // (sent from the UI job)
        }
        if (ev == ps::RelayPace::kFirst)
            Line("host: peer %llu's first receipt (it had %u relays and %u pongs of ours): relays to it start at %d a second and go faster as it has them", (unsigned long long)peer,
                 relaysGot, (unsigned)pongsGot, hz);
        else if (ev == ps::RelayPace::kSlower || ev == ps::RelayPace::kFaster)
            Line("host: relays to peer %llu now %d a second (it had %u of the last %u sent to it)", (unsigned long long)peer, hz, lastGot, lastSent);
        else if (ev == ps::RelayPace::kStalled)
            Line("host: peer %llu has had NOTHING more of ours for %.0f s (it had %u relays and %u pongs of %u relays sent; stall %u): relays to it paused %.0f s, then held "
                 "at %d a second (three quarters of the most it had)", (unsigned long long)peer, ps::kStallMs / 1000.0, had, (unsigned)pongsGot, stalledAt, stalls,
                 ps::kPauseMs / 1000.0, hz);
        else if (ev == ps::RelayPace::kProbe)
            Line("host: the link to peer %llu held %.0f s at its ceiling: tried at %d a second", (unsigned long long)peer, ps::kProbeMs / 1000.0, hz);
        (void)sent;
        return;
    }
    if (id == ps::kMsgShot) {   // a player's bow shot (vr/shot_share.h): ours for the create of his arrows, and handed on to every other player
        uint32_t unit = 0;
        shotshare::Shot s;
        if (!shotshare::ParseShotMsg(data, size, &unit, &s)) { std::lock_guard<std::mutex> lock(g_lock); ++g_host.inBad; return; }
        if (const ShotSink sink = g_shotSink.load()) sink(unit, s, true);
        std::lock_guard<std::mutex> lock(g_lock);
        ++g_host.shotsIn;
        if (g_host.nShot < (int)g_host.shotQ.size()) {
            Host::ShotFwd& f = g_host.shotQ[g_host.nShot++];
            f.from = peer;
            f.n = std::min<uint32_t>(size, shotshare::kShotMsgBytes);
            std::memcpy(f.b, data, f.n);
        }
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
        if (id == gs::kMsgGrab) {   // [multiplayer] pose_share_grab: a peer's holds, for the others (only as the hero its poses say it is)
            gs::Hold h[gs::kGrabMsgMax];
            uint32_t gu = 0;
            const int nh = gs::ParseGrabMsg(data, size, &gu, h, gs::kGrabMsgMax);
            const ps::HostPeer* hp = g_host.table.Find(peer);
            if (!nh || !hp || !hp->unit || hp->unit != gu) { ++g_grabCounts.refused; return; }
            for (int i = 0; i < nh; ++i)
                if (!g_host.grabs.Take(peer, gu, h[i])) ++g_grabCounts.refused;
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
        ++g_cl.pongsGot;
        return;
    }
    if (id == ps::kMsgBurst) { std::lock_guard<std::mutex> lock(g_lock); ++g_cl.burstGot; return; }
    if (id == gs::kMsgGrabRelay) {   // [multiplayer] pose_share_grab: the others' holds, into each holder's hand's track
        gs::RelayEntry e[gs::kGrabRelayMax];
        const int n = gs::ParseGrabRelay(data, size, e, gs::kGrabRelayMax);
        std::lock_guard<std::mutex> lock(g_lock);
        for (int i = 0; i < n; ++i) {
            if (e[i].hold.state == gs::kKnock) {   // a knock: played once, not a hand's track
                if (g_knocks.Add(now, e[i].unit, e[i].hold)) ++g_grabCounts.got;
                continue;
            }
            GrabSlot* free = nullptr;
            GrabSlot* at = nullptr;
            for (auto& s : g_grabSlots) {
                if (s.unit == e[i].unit && s.side == e[i].hold.side) { at = &s; break; }
                if (!s.unit && !free) free = &s;
            }
            if (!at && free) { *free = GrabSlot{}; free->unit = e[i].unit; free->side = e[i].hold.side; at = free; }
            if (at && at->track.Add(now, e[i].hold)) ++g_grabCounts.got;
        }
        return;
    }
    if (id == ps::kMsgShot) {   // another player's bow shot, handed on by the host (vr/shot_share.h)
        uint32_t unit = 0;
        shotshare::Shot s;
        if (!shotshare::ParseShotMsg(data, size, &unit, &s)) return;
        { std::lock_guard<std::mutex> lock(g_lock); ++g_cl.shotsIn; }
        if (const ShotSink sink = g_shotSink.load()) sink(unit, s, false);
        return;
    }
    if (id != ps::kMsgRelay) return;
    { std::lock_guard<std::mutex> lock(g_lock); ++g_cl.relaysGot; }
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
            if (!r->arrivedSince) r->arrivedSince = now;
            ++r->arrived;
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
        g_host.grabs.Remove(ev->peer);
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
    uint8_t gb[gs::kGrabMsgMaxBytes];   // [multiplayer] pose_share_grab: our holds, beside the pose
    uint32_t gn = 0;
    bool ping = false, burst = false;
    uint16_t pingSeq = 0;
    uint32_t relaysGot = 0;
    uint16_t pongsGot = 0;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        relaysGot = g_cl.relaysGot;
        pongsGot = g_cl.pongsGot;
        if (g_cl.state == nw::ConnectionState::Connected) {
            if (g_cl.ownFresh && g_cl.ownUnit) {
                n = ps::BuildPoseMsg(g_cl.ownUnit, g_cl.own, b, sizeof b);
                g_cl.ownFresh = false;
            }
            if (g_ownGrabs.fresh && g_ownGrabs.n && g_cl.ownUnit) gn = gs::BuildGrabMsg(g_cl.ownUnit, g_ownGrabs.h, g_ownGrabs.n, gb, sizeof gb);
            g_ownGrabs.fresh = false;
            if (now - g_cl.pingAt >= 1000) { ping = true; g_cl.pingAt = now; pingSeq = ++g_cl.pingSeq; }
            if (g_burst.load() && !g_cl.burstDone && now - g_cl.connectedAt >= 2000) { burst = true; g_cl.burstDone = true; }
        }
    }
    {   // our latest bow shot, once (vr/shot_share.h)
        uint8_t sb[shotshare::kShotMsgBytes];
        uint32_t sn = 0;
        {
            std::lock_guard<std::mutex> lock(g_lock);
            if (g_cl.state == nw::ConnectionState::Connected && g_cl.shotFresh) {
                sn = shotshare::BuildShotMsg(g_cl.shotUnit, g_cl.shot, sb, sizeof sb);
                g_cl.shotFresh = false;
            }
        }
        if (sn) {
            const nw::Result r = g_net->sendToHost(ctx, g_channel, ps::kMsgShot, sb, sn);
            std::lock_guard<std::mutex> lock(g_lock);
            if (r == nw::Result::Success) ++g_cl.shotsOut;
            else if (g_toldSend.Once(r)) Line("sendToHost (shot): %s", ResultName(r));
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
    if (gn) {
        const nw::Result r = g_net->sendToHost(ctx, g_channel, gs::kMsgGrab, gb, gn);
        if (r == nw::Result::Success) { std::lock_guard<std::mutex> lock(g_lock); ++g_grabCounts.sent; }
        else if (g_toldSend.Once(r)) Line("sendToHost (grab): %s", ResultName(r));
    }
    if (ping) {
        uint8_t pb[ps::kPingBytes + ps::kReceiptBytes];
        const nw::Result r = g_net->sendToHost(ctx, g_channel, ps::kMsgPing, pb, ps::BuildPingReceipt(pingSeq, (uint32_t)NowMs(), relaysGot, pongsGot, pb, sizeof pb));
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
    static gs::RelayEntry grabsTo[ps::kMaxPeers][gs::HostGrabs::kMax];   // [multiplayer] pose_share_grab: the others' fresh holds for each peer (the UI thread only)
    std::array<uint64_t, ps::kMaxPeers> grabDests{};
    int nGrabs[ps::kMaxPeers] = {};
    int nd = 0, ngd = 0;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        if (g_host.is) {
            resolve = g_host.resolveQ; g_host.resolveQ = {};
            burstTo = g_host.burstQ; g_host.burstQ = {};
            g_host.table.Forget(now, 5000);   // silent for 5 s (no pose, no ping): no longer relayed to or from
            for (const auto& s : g_host.table.p) {
                if (!s.peer) continue;
                const int slot = SeenSlot(s.peer, false);
                if (slot >= 0 && !g_host.pace[slot].Due(now)) continue;   // (its pace: what its receipts say arrives)
                nEntries[nd] = g_host.table.Build(s.peer, g_echo.load(), perDest[nd], ps::kMaxPeers);
                dests[nd++] = s.peer;
            }
            g_host.table.Flush();
            if (g_host.grabs.AnyFresh()) {   // the holds go to every peer heard, at once (a few while a hand holds; not paced as the poses are)
                for (const auto& s : g_host.table.p) {
                    if (!s.peer || ngd >= ps::kMaxPeers) continue;
                    nGrabs[ngd] = g_host.grabs.Build(s.peer, g_echo.load(), grabsTo[ngd], gs::HostGrabs::kMax);
                    if (nGrabs[ngd]) grabDests[ngd++] = s.peer;
                }
                g_host.grabs.Flush();
            }
        }
    }
    {   // the pongs queued by the host callback (their host time now: the round trip is still the transport's within a frame or two)
        Host::Pong q[16];
        int nq = 0;
        {
            std::lock_guard<std::mutex> lock(g_lock);
            nq = g_host.nPong;
            for (int i = 0; i < nq; ++i) q[i] = g_host.pongQ[i];
            g_host.nPong = 0;
        }
        for (int i = 0; i < nq; ++i) {
            uint8_t pb[ps::kPongBytes];
            const uint32_t pn = ps::BuildPong(q[i].seq, q[i].clientMs, (uint32_t)now, pb, sizeof pb);
            const nw::Result r = g_net->sendToClient(ctx, g_channel, q[i].peer, ps::kMsgPong, pb, pn);
            { std::lock_guard<std::mutex> lock(g_lock); CountSend(q[i].peer, r); }
            if (r != nw::Result::Success && g_toldPong.Once(r)) Line("host: a pong to peer %llu - %s", (unsigned long long)q[i].peer, ResultName(r));
        }
    }
    {   // the players' shots, each to every other player (not back to the one it came from), as they came - before the poses: a shot is wanted at once
        Host::ShotFwd q[16];
        int nq = 0;
        std::array<uint64_t, ps::kMaxPeers> to{};
        int nt = 0;
        {
            std::lock_guard<std::mutex> lock(g_lock);
            nq = g_host.nShot;
            for (int i = 0; i < nq; ++i) q[i] = g_host.shotQ[i];
            g_host.nShot = 0;
            if (nq)
                for (const auto& s : g_host.table.p)
                    if (s.peer && nt < ps::kMaxPeers) to[nt++] = s.peer;
        }
        for (int i = 0; i < nq; ++i)
            for (int t = 0; t < nt; ++t) {
                if (to[t] == q[i].from) continue;
                const nw::Result res = g_net->sendToClient(ctx, g_channel, to[t], ps::kMsgShot, q[i].b, q[i].n);
                std::lock_guard<std::mutex> lock(g_lock);
                CountSend(to[t], res);
                if (res == nw::Result::Success) ++g_host.shotsOut;
                else if (g_toldRelay.Once(res)) Line("host: sendToClient (shot) to peer %llu: %s", (unsigned long long)to[t], ResultName(res));
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
                if (const int slot = SeenSlot(dests[d], false); slot >= 0 && res == nw::Result::Success) g_host.pace[slot].Sent(now, g_hz.load());
                CountSend(dests[d], res);
                if (res == nw::Result::Success) ++g_host.relays.ok;
                else if (res == nw::Result::RateLimited) ++g_host.relays.limited;
                else ++g_host.relays.other;
            }
            if (res != nw::Result::Success && g_toldRelay.Once(res)) Line("host: sendToClient (relay) to peer %llu: %s", (unsigned long long)dests[d], ResultName(res));
        }
    for (int d = 0; d < ngd; ++d)
        for (int at = 0; at < nGrabs[d];) {
            int taken = 0;
            const uint32_t sz = gs::BuildGrabRelay(grabsTo[d] + at, nGrabs[d] - at, b, sizeof b, &taken);
            if (!sz || !taken) break;
            at += taken;
            const nw::Result res = g_net->sendToClient(ctx, g_channel, grabDests[d], gs::kMsgGrabRelay, b, sz);
            std::lock_guard<std::mutex> lock(g_lock);
            CountSend(grabDests[d], res);
            if (res == nw::Result::Success) ++g_grabCounts.relayed;
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
        if (g_cl.relaysGot || g_cl.pongsGot) add("; from the host %u relays and %u pongs all along", g_cl.relaysGot, (unsigned)g_cl.pongsGot);
        int players = 0;
        for (auto& r : g_remotes) {
            if (!r.unit) continue;
            ++players;
            add("; unit %u: got %u lost %u late %u", r.unit, r.seq.got, r.seq.lost, r.seq.late);
            {   // how they come, and how far behind they are played (Track::FitDelay)
                const uint64_t t = NowMs();
                const float secs = r.arrivedSince && t > r.arrivedSince ? (float)(t - r.arrivedSince) / 1000.0f : 0.0f;
                add(", arriving %.1f a second (%u ms apart), played %.0f ms behind", secs > 0.0f ? (float)r.arrived / secs : 0.0f, r.track.Spacing(), r.delayMs);
                r.arrived = 0;
                r.arrivedSince = t;
            }
            if (r.ageMin != INT64_MAX) add(", arrival - sent %lld..%lld ms", (long long)r.ageMin, (long long)r.ageMax);
            r.ageMin = INT64_MAX; r.ageMax = INT64_MIN;
        }
        if (!players) add("; no other player's pose");
        if (g_grabCounts.sent + g_grabCounts.got + g_grabCounts.relayed + g_grabCounts.refused)   // [multiplayer] pose_share_grab
            add("; holds sent %u, got %u, relayed %u, refused %u", g_grabCounts.sent, g_grabCounts.got, g_grabCounts.relayed, g_grabCounts.refused);
        g_grabCounts = GrabCounts{};
        if (g_cl.shotsOut || g_cl.shotsIn) add("; bow shots: %u ours sent, %u of the others' came (all along)", g_cl.shotsOut, g_cl.shotsIn);
        if (g_host.is && (g_host.shotsIn || g_host.shotsOut)) add(" | host: %u shots came, %u handed on", g_host.shotsIn, g_host.shotsOut);
        if (g_host.is) {
            add(" | host: %d peers (%d sending poses), relays sent %u/s, RateLimited %u/s, other %u/s; poses in ok %u old %u wrong-unit %u full %u bad %u", g_host.table.Count(), g_host.table.Senders(),
                g_host.relays.lastOk, g_host.relays.lastLimited, g_host.relays.lastOther, g_host.inOk, g_host.inOld, g_host.inWrongUnit, g_host.inFull, g_host.inBad);
            for (int i = 0; i < ps::kMaxPeers; ++i)
                if (g_host.burstGot[i]) add("; peer %llu's burst: %u came", (unsigned long long)g_host.seen[i], g_host.burstGot[i]);
            for (int i = 0; i < ps::kMaxPeers; ++i) {   // what D2RLoader answered our sends to each peer (relays and pongs), since the last line
                Host::Sends& s = g_host.sends[i];
                if (!g_host.seen[i] || !(s.ok + s.limited + s.busy + s.other)) continue;
                add("; sends to peer %llu: ok %u, RateLimited %u, Busy %u, other %u", (unsigned long long)g_host.seen[i], s.ok, s.limited, s.busy, s.other);
                if (s.other) add(" (%s)", ResultName(s.lastOther));
                s = Host::Sends{};
            }
            for (int i = 0; i < ps::kMaxPeers; ++i) {   // what each peer says it had of the relays sent to it (its pings' receipts)
                const ps::RelayPace& pc = g_host.pace[i];
                if (!g_host.seen[i] || !pc.sent) continue;
                const uint64_t t = NowMs();
                if (pc.told) add("; to peer %llu: %u relays sent, it had %u (%.0f %%), %d a second (the link held %.1f a second at most, ceiling %d)%s%s",
                                 (unsigned long long)g_host.seen[i], pc.sent, pc.got, 100.0 * pc.got / pc.sent, pc.Rate(t, g_hz.load()), pc.bestHz, pc.ceil,
                                 pc.stalls ? ", stalls " : "", pc.stalls ? std::to_string(pc.stalls).c_str() : "");
                else add("; to peer %llu: %u relays sent, no receipt from it (an older build)", (unsigned long long)g_host.seen[i], pc.sent);
                if (t < pc.pauseUntil) add(" (paused)");
            }
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
        for (auto& s : g_grabSlots) s = GrabSlot{};
        g_ownGrabs = OwnGrabs{};
        g_knocks = gs::KnockQueue{};
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
            if (!inGame) {
                g_host = Host{};
                for (auto& r : g_remotes) r = RemoteSlot{};
                for (auto& s : g_grabSlots) s = GrabSlot{};
                g_ownGrabs = OwnGrabs{};
                g_knocks = gs::KnockQueue{};
            }
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
    const uint32_t floor = g_delayMs.load();
    size_t k = 0;
    std::lock_guard<std::mutex> lock(g_lock);
    for (auto& r : g_remotes) {
        if (!r.unit || k >= max) continue;
        {   // the playback delay these poses need, eased (Track::FitDelay): a pose after the shown one when the playback gets there - smooth at 4 a second too
            const float want = (float)r.track.FitDelay(floor);
            const float dt = r.delayAt ? (float)std::min<uint64_t>(now - r.delayAt, 250) / 1000.0f : 1.0f;
            if (r.delayMs <= 0.0f) r.delayMs = want;
            else r.delayMs += (want - r.delayMs) * (1.0f - std::exp(-dt / (want > r.delayMs ? 0.3f : 2.0f)));
            r.delayAt = now;
        }
        ps::Pose p;
        if (!r.track.Sample(now, (uint32_t)std::lround(r.delayMs), &p) || !ps::Plausible(p)) continue;
        out[k].unit = r.unit;
        out[k].pose = p;
        ++k;
    }
    return k;
}

void SetOwnGrabs(const gs::Hold* h, int n) {
    if (!g_on.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> lock(g_lock);
    g_ownGrabs.n = h ? std::clamp(n, 0, gs::kGrabMsgMax) : 0;
    for (int i = 0; i < g_ownGrabs.n; ++i) g_ownGrabs.h[i] = h[i];
    g_ownGrabs.fresh = g_ownGrabs.n > 0;
}

size_t RemoteGrabs(RemoteGrab* out, size_t max) {
    if (!out || !max || !g_on.load(std::memory_order_relaxed)) return 0;
    const uint64_t now = NowMs();
    const uint32_t floor = g_delayMs.load();
    size_t k = 0;
    std::lock_guard<std::mutex> lock(g_lock);
    for (auto& s : g_grabSlots) {
        if (!s.unit) continue;
        if (now > s.track.NewestRecv() && now - s.track.NewestRecv() > kGrabForgetMs) { s = GrabSlot{}; continue; }   // (one that came after `now` was read is fresh)
        if (k >= max) continue;
        uint32_t delay = floor;   // as far behind as the holder's poses are played (its hand drawn there)
        for (const auto& r : g_remotes)
            if (r.unit == s.unit && r.delayMs > 0.0f) delay = (uint32_t)std::lround(r.delayMs);
        RemoteGrab g;
        if (!s.track.Sample(now, delay, &g.hold, &g.lost)) continue;
        g.unit = s.unit;
        out[k++] = g;
    }
    return k;
}

size_t RemoteKnocks(RemoteKnock* out, size_t max) {
    if (!out || !max || !g_on.load(std::memory_order_relaxed)) return 0;
    const uint64_t now = NowMs();
    const uint32_t floor = g_delayMs.load();
    std::lock_guard<std::mutex> lock(g_lock);
    gs::KnockQueue::Knock k[16];
    const int n = g_knocks.Due(now, [&](uint32_t unit) {   // as far behind as the striker's poses are played (his swing drawn there)
        for (const auto& r : g_remotes)
            if (r.unit == unit && r.delayMs > 0.0f) return (uint32_t)std::lround(r.delayMs);
        return floor;
    }, k, (int)std::min<size_t>(max, 16));
    for (int i = 0; i < n; ++i) { out[i].unit = k[i].unit; out[i].knock = k[i].h; }
    return (size_t)n;
}

void ShotOut(uint32_t ownUnit, const shotshare::Shot& s) {
    if (!ownUnit || !g_on.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> lock(g_lock);
    const bool press = g_cl.shotFresh && (g_cl.shot.flags & shotshare::kPress) && g_cl.shot.seq == s.seq;   // (a press not sent yet keeps its flag)
    g_cl.shot = s;
    if (press) g_cl.shot.flags |= shotshare::kPress;
    g_cl.shotUnit = ownUnit;
    g_cl.shotFresh = true;
}
void SetShotSink(ShotSink fn) { g_shotSink.store(fn); }
bool Joined() {
    if (!g_on.load(std::memory_order_relaxed)) return false;
    std::lock_guard<std::mutex> lock(g_lock);
    return g_cl.state == nw::ConnectionState::Connected && !g_host.is;
}

void Shutdown() {
    g_dead.store(true);
    if (g_net && g_ctx && Registered()) g_net->unregisterChannel(g_ctx, g_channel);
    g_channel = nw::InvalidChannelHandle;
}

}  // namespace posewire
