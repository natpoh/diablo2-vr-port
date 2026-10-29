// melee_direct_client.cpp - Direct hit for a joined TCP/IP player (melee_direct_client.h): the game's side.
//
// What it calls of the game, all on the game's UI thread (the thread the pad's casts and the player's commands run on; a joined
// client has no game thread of its own - D2RLoader's runOnGameThread is Unavailable there):
//   - the player's command funnel PlayerCommand 0xFABE0 (cmd 6, the hero's client unit, 1, the monster's id) - the ONE call that
//     does something: the client's own prediction and the packet to the host, exactly as the game's click on a monster makes them;
//   - [gameplay] melee_direct_client_cleave: D2RLoader's plugin network (D2RLPlugin/network.h): the client sends "strike monster M for hero
//     H" to the host; the host queues it for melee_touch, which hands it to melee_direct.cpp (the server's attack, the host's side).
//   - read-only: LocalIndex 0x8B2D0 and ClientPlayer 0x9A480 (the hero's client unit), FindClientUnit 0x9A5D0 (the monster),
//     SlotSkill 0x117D20 (the A slot's skill - through vrcam's hook when it is in, which answers its two call sites only),
//     the hero's left skill node from the game's own getter UnitLeftSkill 0x34A540 (its row at +0, the row's id at +0 - proven by
//     the game's code: SkillNodeLayout), and the two game-type globals (the role).
// Every call and read is behind an SEH guard; a fault turns the option off for the session and the touch goes the key path.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <utility>

#include <D2RLPlugin/api.h>

#include "log_async.h"   // [debug] log_async: the plugin's log lines through a ring and a writer thread (vr/log_queue.h); off = ctx->LogInfo as ever
#include "melee_direct_client.h"
#include "sigscan.h"
#include "ui_ticket.h"
#include "view_gate.h"

using namespace D2RL;

namespace meleedirectclient {
namespace {

// Addresses in 3.3.93787 (the keys of game_sigs.h).
constexpr uint64_t RVA_PLAYER_COMMAND = 0xFABE0, RVA_FIND_UNIT = 0x9A5D0, RVA_LOCAL_INDEX = 0x8B2D0, RVA_CLIENT_PLAYER = 0x9A480,
                   RVA_SLOT_SKILL = 0x117D20, RVA_CLIENT_GAME_TYPE = 0x2A233F4, RVA_LAUNCH_GAME_TYPE = 0x32EF888, RVA_UNIT_LEFT_SKILL = 0x34A540;
// code whose bytes prove the layout read below (found = the offsets are the build's): a skill node's row at +0, the row's id at +0
constexpr uint64_t RVA_SKILL_NODE_LAYOUT = 0x33DD09;
// push rbp; push rbx; push r13; push r14; push r15; lea rbp, [rsp-0x37]; sub rsp, 0xA0 (as vr/stray_walk_wire.cpp hooks it)
const uint8_t kSigPlayerCommand[16] = {0x40, 0x55, 0x53, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xC9, 0x48, 0x81};

using CommandFn = void (*)(int cmd, uintptr_t unit, int a1, int a2);
using FindUnitFn = uintptr_t (*)(uint32_t id, int type);
using IndexFn = int (*)();
using PlayerFn = uintptr_t (*)(int index);
using SlotSkillFn = int (*)(int slot);
using LeftSkillFn = uintptr_t (*)(uintptr_t unit);

const PluginContext* g_ctx = nullptr;
const ThreadService* g_threads = nullptr;
std::atomic<bool> g_wanted{false}, g_faulted{false};
std::atomic<int> g_mode{0};   // [gameplay] melee_direct_client: 0 off, 1 by the rules, 2 every accepted touch
std::atomic<int> g_state{0};   // 0 not proven yet, 1 ready, 2 not possible (another build?)
std::atomic<bool> g_joined{false};   // Joined(...) and Settled: a joined TCP/IP game for kJoinSettleMs
uint64_t g_joinedSince = 0;          // the timer's thread only
std::atomic<bool> g_leftIsSlot{false};
std::atomic<float> g_minSpeed{2.5f}, g_minForce{1.5f}, g_minLength{0.2f};   // [gameplay] melee_direct_client_min_speed (m/s) / _min_force (N s) / _min_length (m)
std::atomic<bool> g_cleave{false};      // [gameplay] melee_direct_client_cleave
uint64_t g_lastCmdAt = 0;               // the last command (not an extra hit): when and to which monster (the timer's thread only)
uint32_t g_lastCmdMonster = 0;
// the plugin network
namespace nw = D2RL::Network;
const NetworkService* g_net = nullptr;
nw::ChannelHandle g_channel = nw::InvalidChannelHandle;
std::atomic<bool> g_netDead{false};
std::atomic<bool> g_connected{false};   // the client's connection to the host's channel (the UI thread's look)
uint64_t g_connectTriedAt = 0;          // the UI thread only
PeerHeroes g_peers;                     // the host: each peer's hero (under g_remoteLock)
std::mutex g_remoteLock;
std::array<std::pair<uint32_t, uint32_t>, 8> g_remoteQ{};   // the host: (hero, monster) to strike, oldest first
size_t g_remoteN = 0;
AttackClock g_clock;                    // the hero's attack time (the timer's thread only)
PerMonster g_perMonster;                // when each monster last had a command (the timer's thread only)
std::atomic<uint64_t> g_lookAt{0};   // when the UI thread last looked at the skills (GetTickCount64), 0 = never / forgotten

void Log(const char* text) {
    if (g_ctx) logasync::Info(g_ctx, text);
}
template <class... A> void LogF(const char* fmt, A... a) {
    char b[320];
    snprintf(b, sizeof b, fmt, a...);
    Log(b);
}

bool Read(void* d, uintptr_t s, size_t n) noexcept {
    if (s < 0x10000) return false;
    __try { memcpy(d, reinterpret_cast<const void*>(s), n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallIndex(int* out) noexcept {
    const uintptr_t fn = d2rsig::Addr(RVA_LOCAL_INDEX);
    if (!fn) return false;
    __try { *out = reinterpret_cast<IndexFn>(fn)(); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallPlayer(int index, uintptr_t* out) noexcept {
    const uintptr_t fn = d2rsig::Addr(RVA_CLIENT_PLAYER);
    if (!fn) return false;
    __try { *out = reinterpret_cast<PlayerFn>(fn)(index); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallFind(uint32_t id, int type, uintptr_t* out) noexcept {
    const uintptr_t fn = d2rsig::Addr(RVA_FIND_UNIT);
    if (!fn) return false;
    __try { *out = reinterpret_cast<FindUnitFn>(fn)(id, type); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallSlotSkill(int slot, int* out) noexcept {
    const uintptr_t fn = d2rsig::Addr(RVA_SLOT_SKILL);
    if (!fn) return false;
    __try { *out = reinterpret_cast<SlotSkillFn>(fn)(slot); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallLeftSkill(uintptr_t unit, uintptr_t* out) noexcept {
    const uintptr_t fn = d2rsig::Addr(RVA_UNIT_LEFT_SKILL);
    if (!fn || !unit) return false;
    __try { *out = reinterpret_cast<LeftSkillFn>(fn)(unit); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallCommand(int cmd, uintptr_t unit, int a1, int a2) noexcept {
    const uintptr_t fn = d2rsig::Addr(RVA_PLAYER_COMMAND);
    if (!fn) return false;
    __try { reinterpret_cast<CommandFn>(fn)(cmd, unit, a1, a2); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// The hero's client unit, its header proven: type 0 and (when known) the hero's id.
uintptr_t Hero(uint32_t heroId, uint32_t* mode) {
    int index = -1;
    uintptr_t unit = 0;
    uint32_t head[4] = {};
    if (!CallIndex(&index) || index < 0 || index >= 8 || !CallPlayer(index, &unit) || !unit || !Read(head, unit, sizeof head)) return 0;
    if (head[0] != 0 || (heroId && head[2] != heroId)) return 0;
    if (mode) *mode = head[3];
    return unit;
}

// The hero's left skill id and the A slot's skill id; false when either could not be read.
bool Skills(uintptr_t hero, int32_t* left, int32_t* slot) {
    uintptr_t node = 0, row = 0;
    int16_t id = 0;
    int bound = -1;
    if (!CallLeftSkill(hero, &node) || !node || !Read(&row, node, sizeof row) || !row || !Read(&id, row, sizeof id)) return false;
    if (!CallSlotSkill(0, &bound)) return false;
    *left = id;
    *slot = bound;
    return true;
}

// ---- the requests: the timer's thread queues, the UI thread sends ---------------------------------------------------------------

struct Request {
    uint32_t heroId = 0, monsterId = 0;
    int hand = 0;
    ULONGLONG at = 0;
    bool extra = false;   // [gameplay] melee_direct_client_cleave: asked of the host's server attack, not sent as the command
};
constexpr size_t kQueue = 6;
constexpr ULONGLONG kStaleMs = 250;   // a touch not sent within this is not sent late (never a backlog)
std::mutex g_mutex;
std::array<Request, kQueue> g_queue;
size_t g_count = 0;
uiticket::Ticket g_ticket, g_lookTicket;
std::atomic<uint64_t> g_fallback{0};   // a request handed back to the key path: (hand << 32) | monster id, 0 = none

struct Counters {
    unsigned asked = 0, sent = 0, stale = 0, faults = 0, back = 0;
    unsigned extra = 0, extraPlain = 0, remoteTaken = 0, remoteRefused = 0;   // extra hits asked of the host / sent plain (no connection); the host's side
    unsigned refused[8] = {};
    unsigned key[static_cast<int>(Why::kCount)] = {};   // the touches by Why (the timer's thread)
} g_n;   // racy between the two threads and harmless: counts for a log line
ULONGLONG g_reportAt = 0;

void Report(ULONGLONG now) {
    if (now - g_reportAt < 10000) return;
    g_reportAt = now;
    bool any = g_n.asked != 0 || g_n.remoteTaken != 0 || g_n.remoteRefused != 0;
    for (const unsigned k : g_n.key) any = any || k != 0;
    if (!any) return;
    LogF("melee direct (client): %u asked, %u sent as the game's command, %u too old, %u back to the key; not sent: %u no hero, %u hero's mode, %u no monster, "
         "%u dying, %u skills unread; the normal attack took %u (another skill on the left), %u (a skill armed), %u (too soon on the same monster), %u (too slow), "
         "%u (too weak), %u (too short), %u (one-handed extra); "
         "extra hits: %u asked of the host, %u sent plain (no connection); as the host: %u taken, %u refused",
         g_n.asked, g_n.sent, g_n.stale, g_n.back, g_n.refused[static_cast<int>(Verdict::NoHero)], g_n.refused[static_cast<int>(Verdict::HeroMode)],
         g_n.refused[static_cast<int>(Verdict::NoMonster)], g_n.refused[static_cast<int>(Verdict::MonsterDying)],
         g_n.refused[static_cast<int>(Verdict::NoSkills)], g_n.key[static_cast<int>(Why::OtherSkill)], g_n.key[static_cast<int>(Why::Armed)],
         g_n.key[static_cast<int>(Why::TooSoon)], g_n.key[static_cast<int>(Why::TooSlow)], g_n.key[static_cast<int>(Why::TooWeak)],
         g_n.key[static_cast<int>(Why::TooShort)], g_n.key[static_cast<int>(Why::OneHanded)], g_n.extra, g_n.extraPlain, g_n.remoteTaken, g_n.remoteRefused);
    g_n = {};
}

void SendOne(const Request& r) noexcept {
    ++g_n.asked;
    Found f;
    const uintptr_t hero = Hero(r.heroId, &f.heroMode);
    f.hero = hero != 0;
    uintptr_t monster = 0;
    uint32_t head[4] = {};
    if (f.hero && CallFind(r.monsterId, kUnitMonster, &monster) && monster && Read(head, monster, sizeof head) && head[0] == kUnitMonster && head[2] == r.monsterId) {
        f.monster = true;
        f.monsterMode = head[3];
    }
    if (f.monster) f.skills = Skills(hero, &f.left, &f.slot);
    const Verdict v = Check(f);
    if (v == Verdict::OtherSkill) {
        // another skill was cast with another button since the last look: the press of the A slot sets the left skill back - the key path
        g_leftIsSlot.store(false);
        g_fallback.store(static_cast<uint64_t>(static_cast<uint32_t>(r.hand)) << 32 | r.monsterId);
        ++g_n.back;
        return;
    }
    if (v != Verdict::Go) {
        ++g_n.refused[static_cast<int>(v)];
        static unsigned told = 0;
        if (++told <= 12) LogF("melee direct (client): a touch on monster %u was not sent - %s", r.monsterId, Name(v));
        return;
    }
    if (r.extra) {   // [gameplay] melee_direct_client_cleave: asked of the host's server attack (the game would drop a second command now)
        uint8_t b[kStrikeBytes];
        const uint32_t n = BuildStrike(r.heroId, r.monsterId, b, sizeof b);
        const nw::Result res = g_connected.load() && g_net && g_channel != nw::InvalidChannelHandle && n
                                   ? g_net->sendToHost(g_ctx, g_channel, kMsgStrike, b, n) : nw::Result::NotConnected;
        if (res == nw::Result::Success) {
            ++g_n.extra;
            return;
        }
        ++g_n.extraPlain;   // no connection to the host's channel: the plain command, as without the box (the game decides)
        static unsigned toldPlain = 0;
        if (++toldPlain <= 6) LogF("melee direct (client): an extra hit on monster %u could not be asked of the host (%u) - sent as the plain command", r.monsterId, static_cast<unsigned>(res));
    }
    if (!CallCommand(kCmdLeftOnUnit, hero, kUnitMonster, static_cast<int>(r.monsterId))) {
        ++g_n.faults;
        g_faulted.store(true);
        Log("melee direct (client): the game's command faulted - the option is OFF for this session (the touch goes on with the game's own key)");
        return;
    }
    ++g_n.sent;
    static unsigned toldSent = 0;
    if (++toldSent <= 12) LogF("melee direct (client): the game's command \"left skill (%d) on a unit\" sent for monster %u", f.left, r.monsterId);
}

void __cdecl Apply(const PluginContext*, void* user) noexcept {
    if (!g_ticket.Claim(uiticket::FromUser(user))) return;
    for (;;) {
        Request q;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (!g_count) return;
            q = g_queue[0];
            for (size_t i = 1; i < g_count; ++i) g_queue[i - 1] = g_queue[i];
            --g_count;
        }
        if (g_faulted.load() || !g_wanted.load() || !viewgate::F4()) continue;   // (F4 only: a request found in another view is not sent)
        if (GetTickCount64() - q.at > kStaleMs) { ++g_n.stale; continue; }
        SendOne(q);
    }
}

// The UI thread's look at the skills: is the hero's left skill the A slot's? Read-only.
std::atomic<uint32_t> g_lookHero{0};
void __cdecl Look(const PluginContext*, void* user) noexcept {
    if (!g_lookTicket.Claim(uiticket::FromUser(user))) return;
    int32_t left = -1, slot = -2;
    const uintptr_t hero = Hero(g_lookHero.load(), nullptr);
    const bool ok = hero && Skills(hero, &left, &slot);
    const bool same = ok && left == slot;
    static int toldLeft = -100, toldSlot = -100;
    if (ok && (left != toldLeft || slot != toldSlot)) {
        toldLeft = left;
        toldSlot = slot;
        LogF("melee direct (client): the hero's left skill %d, the attack slot's %d - %s", left, slot,
             same ? "a touch is sent as the game's command" : "a touch presses the attack (it sets the left skill back)");
    }
    g_leftIsSlot.store(same);
    g_lookAt.store(GetTickCount64());
    // [gameplay] melee_direct_client_cleave: the connection to the host's channel, asked for again after 3 s (10 s while connecting)
    if (g_cleave.load() && g_net && g_channel != nw::InvalidChannelHandle && !g_netDead.load()) {
        nw::ChannelInfo info{};
        info.structSize = nw::ChannelInfoSize;
        const bool got = g_net->getChannelInfo(g_ctx, g_channel, &info) == nw::Result::Success;
        const bool conn = got && info.connectionState == nw::ConnectionState::Connected;
        if (g_connected.exchange(conn) != conn)
            LogF("melee direct (client): the host's channel %s", conn ? "connected - the extra hits of a swing are asked of the host" :
                 "not connected - the extra hits are plain commands until it is");
        const uint64_t now = GetTickCount64();
        if (!conn && now - g_connectTriedAt >= (got && info.connectionState == nw::ConnectionState::Connecting ? 10000u : 3000u)) {
            g_connectTriedAt = now;
            const nw::Result r = g_net->connectToHost(g_ctx, g_channel);
            static int toldR = -1;
            if (static_cast<int>(r) != toldR) {
                toldR = static_cast<int>(r);
                LogF("melee direct (client): asked the host's channel - %u (0 = asked; a host without the box answers nothing)", static_cast<unsigned>(r));
            }
        }
    }
}

// ---- [gameplay] melee_direct_client_cleave: the host's side of the channel ----------------------------------------------------------

void __cdecl OnHost(const PluginContext*, nw::ChannelHandle, nw::PeerHandle peer, uint16_t id, const void* data, uint32_t size, void*) noexcept {
    if (g_netDead.load() || id != kMsgStrike) return;
    uint32_t hero = 0, monster = 0;
    if (!ParseStrike(data, size, &hero, &monster)) return;
    const char* refused = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_remoteLock);
        if (!g_cleave.load()) refused = "the cleave box is off on this PC";
        else if (!g_peers.Accept(peer, hero)) refused = "the peer named another hero than its own";
        else if (g_remoteN >= g_remoteQ.size()) refused = "too many at once";
        else g_remoteQ[g_remoteN++] = {hero, monster};
    }
    if (refused) {
        ++g_n.remoteRefused;
        static unsigned told = 0;
        if (++told <= 12) LogF("melee direct (host): a joined player's extra hit on monster %u (hero %u) refused - %s", monster, hero, refused);
        return;
    }
    ++g_n.remoteTaken;
}

// The rules' facts for a touch on `monsterId` now (the timer's thread): its measured values come in g.t already.
void FillRules(Gate& g, uint32_t monsterId, bool twoHanded, uint64_t nowMs, uint64_t* since) {
    g.mins = {g_minSpeed.load(), g_minForce.load(), g_minLength.load()};
    g.tooSoon = g_perMonster.TooSoon(monsterId, nowMs, g_clock.Ms(), since);
    g.first = !g_perMonster.Known(monsterId);
    g.extra = InAttack(g_lastCmdAt, g_lastCmdMonster, monsterId, nowMs, g_clock.Ms());
    g.cleave = g_cleave.load();
    g.twoHanded = twoHanded;
}
// A direct hit went to `monsterId` (the timer's thread): rule 1's and rule 2's memory; the attack's window is the first monster's (an extra
// hit does not stretch it).
void NoteDirect(uint32_t monsterId, uint64_t nowMs, bool extra) {
    g_perMonster.Sent(monsterId, nowMs);
    if (!extra) {
        g_lastCmdAt = nowMs ? nowMs : 1;
        g_lastCmdMonster = monsterId;
    }
}
// The touch's verdict, one short line: the way it went, the speed, force and length.
void Tell(const char* role, uint32_t monsterId, Why w, uint64_t since, const Gate& g) {
    char soon[48] = "";
    if (w == Why::TooSoon) snprintf(soon, sizeof soon, " (%llu of %llu ms)", static_cast<unsigned long long>(since), static_cast<unsigned long long>(g_clock.Ms()));
    char two[40] = "";
    if (g.twoHanded) snprintf(two, sizeof two, ", two-handed, two hands: %s", g.twoHandsOn ? "yes" : "no");
    LogF("melee direct (%s): monster %u - %s%s | %.1f m/s, %.2f N s, hand R %.2f m L %.2f m (least %.1f m/s, %.2f N s, hand %.2f m)%s", role, monsterId, WhyName(w), soon,
         g.t.speed, g.t.force, g.t.hand[0], g.t.hand[1], g.mins.speed, g.mins.force, g.mins.length, two);
}

void __cdecl OnClient(const PluginContext*, nw::ChannelHandle, uint16_t, const void*, uint32_t, void*) noexcept {}

void __cdecl OnConnection(const PluginContext*, const nw::ConnectionEvent* ev, void*) noexcept {
    if (g_netDead.load() || !nw::HasConnectionEventField(ev, nw::ConnectionEventRequiredSize)) return;
    if (ev->peer != nw::InvalidPeerHandle && (ev->state == nw::ConnectionState::Disconnected || ev->state == nw::ConnectionState::Rejected)) {
        std::lock_guard<std::mutex> lock(g_remoteLock);
        g_peers.Forget(ev->peer);
    }
}

bool ReadU32(uint64_t rva, uint32_t* out) {
    const uintptr_t a = d2rsig::Addr(rva);
    return a && Read(out, a, sizeof *out);
}

}  // namespace

void Init(const PluginContext* ctx) {
    g_ctx = ctx;
    const ThreadService* t = nullptr;
    g_threads = ctx && ctx->QueryService(&t) == ServiceQueryResult::Success ? t : nullptr;
}

void SetMode(int mode) {
    mode = mode < 0 || mode > 2 ? 0 : mode;
    g_wanted.store(mode != 0);
    if (g_mode.exchange(mode) != mode)
        Log(mode == 1 ? "melee direct (client): by the rules - as a joined TCP/IP player a touch that passes them sends the game's own \"attack this monster\" command"
            : mode == 2 ? "melee direct (client): fully on - as a joined TCP/IP player every accepted touch sends the game's own \"attack this monster\" command"
                        : "melee direct (client): off");
}
bool Wanted() { return g_wanted.load(); }
void SetMins(const Mins& m) {
    g_minSpeed.store(m.speed < 0.0f ? 0.0f : m.speed);
    g_minForce.store(m.force < 0.0f ? 0.0f : m.force);
    g_minLength.store(m.length < 0.0f ? 0.0f : m.length);
}
bool WantsForce() { return g_wanted.load() && g_joined.load(); }
void SetCleave(bool on) {
    if (g_cleave.exchange(on) != on)
        Log(on ? "melee direct (client): cleave on - an extra hit of a swing is the server's attack, asked of the host (past the game's one attack at a time)"
               : "melee direct (client): cleave off");
}

bool InitNet(const PluginContext* ctx, bool wanted) {
    if (!wanted || !ctx) return false;
    if (ctx->QueryService(&g_net) != ServiceQueryResult::Success || !HasNetworkServiceField(g_net, NetworkServiceRequiredSize)) {
        g_net = nullptr;
        ctx->LogInfo("vrcam: melee direct (client): D2RLoader has no network service - the extra hits are plain commands");
        return false;
    }
    nw::ChannelRegistration reg{};
    reg.structSize = nw::ChannelRegistrationSize;
    reg.localChannelId = kChannel;
    reg.compatibilityToken = kToken;
    reg.hostMessage = &OnHost;
    reg.clientMessage = &OnClient;
    reg.connectionState = &OnConnection;
    const nw::Result r = g_net->registerChannel(ctx, &reg, &g_channel);
    char b[200];
    snprintf(b, sizeof b, "vrcam: melee direct (client): channel %u %s ([gameplay] melee_direct_client_cleave: the extra hits of a swing)",
             static_cast<unsigned>(kChannel), r == nw::Result::Success ? "registered" : "NOT registered - the extra hits are plain commands");
    ctx->LogInfo(b);
    if (r != nw::Result::Success) { g_channel = nw::InvalidChannelHandle; return false; }
    return true;
}

void ShutdownNet() {
    g_netDead.store(true);
    if (g_net && g_ctx && g_channel != nw::InvalidChannelHandle) g_net->unregisterChannel(g_ctx, g_channel);
    g_channel = nw::InvalidChannelHandle;
}

bool TakeRemote(uint32_t* hero, uint32_t* monster) {
    std::lock_guard<std::mutex> lock(g_remoteLock);
    if (!g_remoteN) return false;
    *hero = g_remoteQ[0].first;
    *monster = g_remoteQ[0].second;
    for (size_t i = 1; i < g_remoteN; ++i) g_remoteQ[i - 1] = g_remoteQ[i];
    --g_remoteN;
    return true;
}

void Tick(uint64_t nowMs, bool serverHere, bool f4, bool heroModeKnown, uint32_t heroMode) {
    Report(nowMs);
    if (!g_wanted.load() || !g_ctx) return;
    {   // the hero's attack time, from his own client unit's mode (his attack speed as the game plays it)
        const bool was = g_clock.Measured();
        const uint64_t before = g_clock.Ms();
        g_clock.Sample(heroModeKnown, heroMode, nowMs);
        static uint64_t toldMs = 0;
        if (g_clock.Measured() && (!was || (g_clock.Ms() != before && (g_clock.Ms() > toldMs + toldMs / 10 || g_clock.Ms() + toldMs / 10 < toldMs)))) {
            toldMs = g_clock.Ms();
            LogF("melee direct (client): the hero's attack takes %llu ms - a touch on the same monster sooner than that is the normal attack",
                 static_cast<unsigned long long>(toldMs));
        }
    }
    // the proofs, tried again until the code is found (its pages are decrypted as the game runs them)
    static uint64_t retryAt = 0;
    static unsigned tries = 0;
    if (g_state.load() == 0 && nowMs >= retryAt) {
        retryAt = nowMs + (tries < 30 ? 2000 : 30000);
        const uint64_t needed[] = {RVA_PLAYER_COMMAND, RVA_FIND_UNIT, RVA_LOCAL_INDEX, RVA_CLIENT_PLAYER, RVA_SLOT_SKILL, RVA_CLIENT_GAME_TYPE,
                                   RVA_LAUNCH_GAME_TYPE, RVA_UNIT_LEFT_SKILL, RVA_SKILL_NODE_LAYOUT};
        bool all = true;
        for (const uint64_t rva : needed) all = all && d2rsig::Addr(rva) != 0;
        if (all && d2rsig::Check(RVA_PLAYER_COMMAND, kSigPlayerCommand, sizeof kSigPlayerCommand)) {
            if (g_threads && g_threads->runOnUiThread) {
                g_state.store(1);
                Log("melee direct (client): ready - the game's command funnel and the skill reads are found");
            } else {
                g_state.store(2);
                Log("melee direct (client): no UI thread service from the loader - the key path stays");
            }
        } else if (++tries == 30) {
            Log("melee direct (client): the game's command funnel or the skill reads are not where they were (another build?) - the key path stays for now");
        }
    }
    // the role: a joined TCP/IP game (no server of ours here); said when it changes
    uint32_t client = 0, launch = 0;
    const bool read = ReadU32(RVA_CLIENT_GAME_TYPE, &client) && ReadU32(RVA_LAUNCH_GAME_TYPE, &launch);
    const bool types = Joined(serverHere, read, client, launch);
    if (!types) g_joinedSince = 0;
    else if (!g_joinedSince) g_joinedSince = nowMs ? nowMs : 1;
    const bool joined = types && Settled(g_joinedSince, nowMs);
    if (g_joined.exchange(joined) != joined)
        LogF("melee direct (client): %s (game type %u, launch type %u%s)", joined ? "a TCP/IP game someone else hosts - touches go as the game's command" :
             "not a joined game - Direct hit as a client does nothing here", client, launch, read ? "" : ", not read");
    if (!joined || !f4 || g_state.load() != 1 || g_faulted.load()) {
        g_lookAt.store(0);
        return;
    }
    // the look at the skills, every kLookMs
    if (nowMs - g_lookAt.load() >= kLookMs)
        if (const uint32_t token = g_lookTicket.Start(nowMs))
            if (g_threads->runOnUiThread(g_ctx, &Look, uiticket::ToUser(token)) != Threads::Result::Success) g_lookTicket.Claim(token);
}

bool Send(uint32_t heroId, uint32_t monsterId, int hand, bool melee, bool armed, const Touch& t, bool twoHanded, bool twoHandsOn, bool tell, uint64_t nowMs) {
    g_lookHero.store(heroId);
    uint64_t since = 0;
    Gate g;
    g.wanted = g_wanted.load();
    g.ready = g_state.load() == 1 && !g_faulted.load() && g_threads && g_threads->runOnUiThread && g_ctx;
    g.f4 = viewgate::F4();
    g.joined = g_joined.load();
    g.melee = melee;
    g.armed = armed;
    g.leftIsSlot = g_leftIsSlot.load() && Fresh(g_lookAt.load(), nowMs);
    g.t = t;
    g.mins = {g_minSpeed.load(), g_minForce.load(), g_minLength.load()};
    FillRules(g, monsterId, twoHanded, nowMs, &since);
    g.twoHandsOn = twoHandsOn;
    if (g_mode.load() == 2) { g.tooSoon = false; g.first = true; g.extra = false; }   // fully on: every accepted touch (no rules, no cleave)
    const Why w = WhyOf(g);
    const Route route = Decide(g);
    const bool extra = route == Route::Extra;
    if (g.wanted && g.joined) ++g_n.key[static_cast<int>(w)];
    if (tell && g.wanted && g.joined && g.ready && g.f4) Tell("client", monsterId, w, since, g);
    if (route == Route::Key) return false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_count >= kQueue) return false;
        g_queue[g_count] = {heroId, monsterId, hand, static_cast<ULONGLONG>(nowMs), extra};
        ++g_count;
    }
    NoteDirect(monsterId, nowMs, extra);
    if (const uint32_t token = g_ticket.Start(nowMs)) {   // (a run already queued will take this one too)
        if (g_threads->runOnUiThread(g_ctx, &Apply, uiticket::ToUser(token)) != Threads::Result::Success) g_ticket.Claim(token);
    }
    return true;
}

void Flush() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_count = 0;
    g_fallback.store(0);
}

bool HostRules(uint32_t monsterId, const Touch& t, bool twoHanded, bool twoHandsOn, bool armed, bool tell, uint64_t nowMs, bool* extra) {
    Gate g;   // the client's own gates are not this role's: the server's call needs none of them (its own checks stay: hostile, a wall, the modes)
    g.wanted = g.ready = g.f4 = g.joined = g.melee = g.leftIsSlot = true;
    g.armed = armed;   // (an armed melee skill is the press's: the plain server attack would not be it)
    g.t = t;
    uint64_t since = 0;
    FillRules(g, monsterId, twoHanded, nowMs, &since);
    g.twoHandsOn = twoHandsOn;
    const Why w = WhyOf(g);
    ++g_n.key[static_cast<int>(w)];
    if (tell) Tell("rules", monsterId, w, since, g);
    const Route r = Decide(g);
    if (r == Route::Key) return false;
    *extra = r == Route::Extra;
    NoteDirect(monsterId, nowMs, *extra);
    return true;
}

bool TakeFallback(uint32_t* monsterId, int* hand) {
    const uint64_t v = g_fallback.exchange(0);
    if (!v) return false;
    *monsterId = static_cast<uint32_t>(v);
    *hand = static_cast<int>(v >> 32);
    return true;
}

}  // namespace meleedirectclient
