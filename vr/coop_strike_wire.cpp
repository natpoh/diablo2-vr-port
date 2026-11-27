// coop_strike_wire.cpp - [gameplay] melee_direct_client_host on the game: what and why in vr/coop_strike.h. Off by default; who hosts decides.
//
// The idea of the host striking a joined player's touch: D2R VR Unleashed PR #519 (geekstreetstudios; Bear / Berk, bearstonem/d2r-pcvr,
// MPL 2.0). Written anew here - none of their code.
//
// Threads.
//   - D2RLoader's channel callbacks (melee_direct_client.cpp OnHost / OnClient / OnConnection hand the messages here): the host binds a
//     peer to its hero, answers the hello with its rule, takes the pongs (the round trip) and queues the swings; the client takes the rule,
//     the pings and the verdicts.
//   - The game's thread on the host (runOnGameThread, every kSampleMs while a joined player is bound): the history of the server's units
//     (the joined heroes and the monsters near them), the hero's attack time from his server mode, the checks of the queued swings on the
//     same units, and the strike through melee_direct.cpp (Submit: the server's Attack for the joined hero). Only this thread touches the
//     history and the heroes' pace.
//   - The game's app thread (vr/app_thread.h): every network SDK call outside the callbacks - the host's verdicts and pings, the client's
//     pongs (the SDK ends the game for a send from another thread: 0.212.3). The swing itself goes from melee_direct_client's UI job.
//   - vrcam's update loop (Tick): posts the above, the report.
//
// What it calls of the game (the host): the server's unit lookup (melee_touch's original), Relation 0x48E460 and Line 0x3502A0 (read-only
// checks, as Direct hit makes them), the server's unit hash (game + 0x2630: rows of 128 chains, next at +0x158 - d2r-pcvr's reading, proven
// here by each unit's own pointer back to the game before it is used) and the units' paths (+0x38, 16.16 cells). The one call that strikes
// is melee_direct.cpp's. Every read and call is behind an SEH guard.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

#include <D2RLPlugin/api.h>

#include "app_thread.h"
#include "coop_strike.h"
#include "coop_strike_wire.h"
#include "log_async.h"
#include "melee_direct.h"
#include "melee_direct_client.h"
#include "melee_touch.h"
#include "sigscan.h"
#include "ui_ticket.h"
#include "unit_scan.h"

namespace moveattack { uint32_t HeroId(); }

using namespace D2RL;

namespace coopstrikewire {
namespace {

using namespace coopstrike;

const PluginContext* g_ctx = nullptr;
const ThreadService* g_threads = nullptr;
std::atomic<bool> g_box{false};   // [gameplay] melee_direct_client_host: this PC's rule as the host

void Log(const char* text) { if (g_ctx) logasync::Info(g_ctx, text); }
template <class... A> void LogF(const char* fmt, A... a) {
    char b[420];
    snprintf(b, sizeof b, fmt, a...);
    Log(b);
}

bool Read(void* d, uintptr_t s, size_t n) noexcept {
    if (s < 0x10000) return false;
    __try { memcpy(d, reinterpret_cast<const void*>(s), n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// ================================================= the host ===========================================================================

constexpr uint64_t RVA_RELATION = 0x48E460, RVA_LINE = 0x3502A0;
constexpr uintptr_t kServerHash = 0x4C6 * 8;   // the server game's unit hash (rows of 128 chains, one per type)
constexpr uintptr_t kUnitGame = 0xD8;          // a server unit's pointer back to its game
constexpr uint64_t kSampleMs = 25, kPingMs = 500, kKeepMs = 3000, kGameFreshMs = 2000;
constexpr float kNearCells = 40.0f;            // the monsters within this of a joined hero are kept in the history

using LookupFn = uintptr_t (*)(uintptr_t game, int type, uint32_t id);
using RelationFn = int (*)(uintptr_t game, uintptr_t player, uintptr_t unit);
using LineFn = int (*)(uintptr_t a, uintptr_t b, int flags);
bool CallLookup(uintptr_t fn, uintptr_t game, int type, uint32_t id, uintptr_t* out) noexcept {
    __try { *out = reinterpret_cast<LookupFn>(fn)(game, type, id); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallRelation(uintptr_t fn, uintptr_t game, uintptr_t player, uintptr_t unit, int* out) noexcept {
    __try { *out = reinterpret_cast<RelationFn>(fn)(game, player, unit); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallLine(uintptr_t fn, uintptr_t a, uintptr_t b, int* out) noexcept {
    __try { *out = reinterpret_cast<LineFn>(fn)(a, b, 0x804); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// A server unit's place in cells: its path's 16.16 coordinates (as unit_scan.h Place, which gives twice the cell).
bool CellOf(uintptr_t unit, float* x, float* y) {
    uintptr_t path = 0;
    uint16_t p[4] = {};
    if (!Read(&path, unit + unitscan::kOffPath, sizeof path) || !Read(p, path, sizeof p) || (!p[1] && !p[3])) return false;
    *x = p[1] + p[0] / 65536.0f;
    *y = p[3] + p[2] / 65536.0f;
    return true;
}
// The server's unit `id` of `type`, its header proven (type, id; a hero also by its pointer back to the game). *mode its mode.
uintptr_t ServerUnit(uintptr_t lookup, uintptr_t game, int type, uint32_t id, uint32_t* mode) {
    uintptr_t u = 0, back = 0;
    uint32_t head[4] = {};
    if (!id || !CallLookup(lookup, game, type, id, &u) || !u || !Read(head, u, sizeof head) || head[0] != static_cast<uint32_t>(type) || head[2] != id) return 0;
    if (type == 0 && (!Read(&back, u + kUnitGame, sizeof back) || back != game)) return 0;
    if (mode) *mode = head[3];
    return u;
}
inline bool Alive(uint32_t mode) { return mode != 0 && mode != 12; }                                     // a monster: not dead, not dying
inline bool HeroOk(uint32_t mode) { return mode != 0 && mode != 17 && mode != 5 && mode != 6; }          // a hero: as melee_direct.h Check

// ---- under g_lock: the peers (round trip, ping), the swings waiting for the game's thread ----
std::mutex g_lock;
Peers g_peers;
struct Queued { uint64_t peer = 0; Swing s; uint64_t recvAt = 0; bool peerHero = true; uint32_t rtt = 0; bool rttKnown = false; };
std::array<Queued, 16> g_q;
size_t g_qn = 0;

// ---- under g_outLock: what goes out on the app thread ----
struct Out { uint64_t peer = 0; uint16_t id = 0; uint32_t n = 0; uint8_t b[48] = {}; };
std::mutex g_outLock;
std::array<Out, 48> g_out;
size_t g_outN = 0;
void Send(uint64_t peer, uint16_t id, const uint8_t* b, uint32_t n) {
    if (!n || n > sizeof(Out::b)) return;
    std::lock_guard<std::mutex> lock(g_outLock);
    if (g_outN >= g_out.size()) return;   // (the app thread is behind: dropped - a verdict not answered is a touch the client lets go)
    Out& o = g_out[g_outN++];
    o.peer = peer;
    o.id = id;
    o.n = n;
    memcpy(o.b, b, n);
}
void DrainOut() {   // the app thread
    std::array<Out, 48> out;
    size_t n = 0;
    {
        std::lock_guard<std::mutex> lock(g_outLock);
        n = g_outN;
        for (size_t i = 0; i < n; ++i) out[i] = g_out[i];
        g_outN = 0;
    }
    for (size_t i = 0; i < n; ++i) {
        appthread::NoteCall("coop strike (host)", "sendToClient");
        meleedirectclient::NetSendToClient(out[i].peer, out[i].id, out[i].b, out[i].n);
    }
}

// ---- the game's thread only: the history, each joined hero's attack clock and pace, the monsters asked about ----
History g_hist;
struct HeroState { uint32_t id = 0; meleedirectclient::AttackClock clock; Pace pace; uint64_t seenAt = 0; bool toldClock = false; };
std::array<HeroState, Peers::kSlots> g_heroes;
HeroState* HeroOf(uint32_t id, bool make) {
    HeroState* stalest = &g_heroes[0];
    for (HeroState& h : g_heroes) {
        if (h.id == id && id) return &h;
        if (!h.id || h.seenAt < stalest->seenAt) stalest = &h;
    }
    if (!make || !id) return nullptr;
    *stalest = HeroState{};
    stalest->id = id;
    return stalest;
}
std::array<uint32_t, 16> g_watch{};   // monsters the joined players touched lately: kept in the history by id (the hash walk may be off)
size_t g_watchNext = 0;
void Watch(uint32_t id) {
    for (const uint32_t w : g_watch) if (w == id) return;
    g_watch[g_watchNext++ % g_watch.size()] = id;
}
std::atomic<int> g_hashState{0};      // the server's unit hash: 0 not proven yet, 1 walked, 2 not as expected (the watched monsters only)
std::atomic<uint64_t> g_gameAt{0};    // the game's thread last found the server's game

struct HostCount {
    unsigned taken = 0, struck = 0, refused[static_cast<int>(Why::kCount)] = {}, pings = 0, pongs = 0, samples = 0;
    unsigned rewindSum = 0, rewindN = 0, rewindMax = 0;
} g_hc;   // racy between threads and harmless: counts for a log line
unsigned g_toldVerdicts = 0;

uiticket::Ticket g_jobTicket;
std::atomic<uint64_t> g_jobAt{0};

// The game's thread: one round of the history, then the swings waiting.
void SampleAndJudge(uintptr_t game, uintptr_t lookup, uint64_t now) {
    uint32_t heroIds[Peers::kSlots] = {};
    int nh = 0;
    Queued q[16];
    size_t nq = 0;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        for (int i = 0; i < Peers::kSlots; ++i) if (g_peers[i].peer && g_peers[i].hero) heroIds[nh++] = g_peers[i].hero;
        nq = g_qn;
        for (size_t i = 0; i < nq; ++i) q[i] = g_q[i];
        g_qn = 0;
    }
    // the joined heroes: their places, their modes (the attack clock)
    float hx[Peers::kSlots] = {}, hy[Peers::kSlots] = {};
    bool hok[Peers::kSlots] = {};
    for (int i = 0; i < nh; ++i) {
        uint32_t mode = 0;
        const uintptr_t u = ServerUnit(lookup, game, 0, heroIds[i], &mode);
        HeroState* h = HeroOf(heroIds[i], true);
        if (!u || !h) continue;
        h->seenAt = now;
        const bool was = h->clock.Measured();
        h->clock.Sample(true, mode, now);
        if (h->clock.Measured() && !was) LogF("coop strike (host): hero %u's attack takes %llu ms on this server - his touches are not struck faster", heroIds[i],
                                             static_cast<unsigned long long>(h->clock.Ms()));
        if (CellOf(u, &hx[i], &hy[i])) {
            hok[i] = true;
            g_hist.Push(heroIds[i], {now, hx[i], hy[i], HeroOk(mode)});
        }
    }
    // the monsters near them: the server's hash, each unit proven by its pointer back to the game
    if (nh && g_hashState.load() != 2) {
        unsigned seen = 0, proven = 0, foreign = 0;
        unitscan::EachUnit([](void* d, uintptr_t s, size_t n) { return Read(d, s, n); }, game + kServerHash, unitscan::kMonster,
                           [&](uintptr_t at, const unitscan::Unit& u) {
                               ++seen;
                               if (unitscan::Field64(u, kUnitGame) != game) { ++foreign; return foreign < 4; }
                               ++proven;
                               float x = 0.0f, y = 0.0f;
                               if (!CellOf(at, &x, &y)) return true;
                               bool nearHero = false;
                               for (int i = 0; i < nh && !nearHero; ++i) nearHero = hok[i] && Dist(x, y, hx[i], hy[i]) <= kNearCells;
                               if (nearHero) g_hist.Push(unitscan::Field32(u, unitscan::kOffId), {now, x, y, Alive(unitscan::Field32(u, unitscan::kOffMode))});
                               return true;
                           });
        if (g_hashState.load() == 0 && seen) {
            const bool ok = proven > 0 && foreign == 0;
            g_hashState.store(ok ? 1 : 2);
            LogF(ok ? "coop strike (host): the server's unit hash read (%u monsters, each pointing back to its game) - the monsters near the joined heroes are kept for %u ms"
                    : "coop strike (host): the server's unit hash is not as expected (%u read, not of this game) - only the monsters the joined players touch are kept (%u ms)",
                 seen, static_cast<unsigned>(Track::kN * kSampleMs));
        }
    }
    // the monsters the joined players touched lately, by id (always: a hash walk that missed one, or none)
    for (const uint32_t id : g_watch) {
        if (!id) continue;
        uint32_t mode = 0;
        float x = 0.0f, y = 0.0f;
        const uintptr_t u = ServerUnit(lookup, game, 1, id, &mode);
        if (u && CellOf(u, &x, &y)) g_hist.Push(id, {now, x, y, Alive(mode)});
    }
    ++g_hc.samples;
    if ((g_hc.samples & 63) == 0) g_hist.Prune(now, kKeepMs);
    // the swings
    const uintptr_t relation = d2rsig::Addr(RVA_RELATION), line = d2rsig::Addr(RVA_LINE);
    const Limits lim;
    for (size_t i = 0; i < nq; ++i) {
        const Queued& w = q[i];
        const Swing& s = w.s;
        const Times t = RewindOf(w.recvAt, w.rtt, s.ageMs);
        Facts f;
        f.off = !g_box.load() || !meleedirect::Ready() || !relation || !line;
        f.peerHero = w.peerHero;
        uint32_t heroMode = 0, monMode = 0;
        const uintptr_t player = ServerUnit(lookup, game, 0, s.hero, &heroMode);
        const uintptr_t monster = player ? ServerUnit(lookup, game, 1, s.monster, &monMode) : 0;
        f.heroOk = player && HeroOk(heroMode);
        if (const Track* ht = g_hist.Find(s.hero)) f.heroTrack = ht->Nearest(s.heroX, s.heroY, t.heroFrom, t.heroTo, &f.heroDist, &f.heroAt);
        if (const Track* mt = g_hist.Find(s.monster)) f.monsterTrack = mt->At(t.monsterAt, &f.monAt);
        if (!f.monsterTrack) Watch(s.monster);   // (from now on: the next touch on it finds its history)
        f.monsterNow = monster != 0;
        int rel = 0, wall = 1;
        f.hostile = player && monster && Alive(monMode) && CallRelation(relation, game, player, monster, &rel) && rel != 0;
        f.wall = !(f.hostile && CallLine(line, player, monster, &wall) && wall == 0);
        float reach = -1.0f;
        Why why = Judge(s, t, f, lim, &reach);
        HeroState* h = HeroOf(s.hero, why == Why::Struck);
        const uint64_t attackMs = h && h->clock.Measured() ? h->clock.Ms() : kDefaultAttackMs;
        const bool cleave = s.extra && s.twoHanded && s.twoHandsOn && meleedirectclient::CleaveBox();
        if (why == Why::Struck && h) why = h->pace.Check(t.swingAt, s.monster, cleave, attackMs);
        if (why == Why::Struck) {
            meleedirect::Request req;
            req.game = game;
            req.heroId = s.hero;
            req.monsterId = s.monster;
            req.lookup = lookup;
            req.remote = true;
            req.checked = true;
            if (meleedirect::Submit(req, now)) {
                if (h) h->pace.Note(t.swingAt, s.monster, cleave, attackMs);
            } else {
                why = Why::Busy;
            }
        }
        if (why == Why::Struck) ++g_hc.struck;
        else ++g_hc.refused[static_cast<int>(why)];
        g_hc.rewindSum += t.rewindMs;
        ++g_hc.rewindN;
        g_hc.rewindMax = std::max(g_hc.rewindMax, t.rewindMs);
        Verdict v;
        v.seq = s.seq;
        v.hand = s.hand;
        v.why = why;
        v.rewindMs = static_cast<uint16_t>(t.rewindMs);
        uint8_t b[kVerdictBytes];
        if (const uint32_t n = BuildVerdict(v, b, sizeof b)) Send(w.peer, kMsgVerdict, b, n);
        if (g_toldVerdicts < 40 || (why != Why::Struck && g_toldVerdicts < 80)) {
            ++g_toldVerdicts;
            LogF("coop strike (host): hero %u, touch #%u on monster %u (%c hand%s) - %s | rewind %u ms (round trip %u ms%s, age %u ms), hero %.1f cells from where he says, "
                 "reach %.1f of %.1f cells, attack %llu ms (%s)",
                 s.hero, static_cast<unsigned>(s.seq), s.monster, s.hand ? 'L' : 'R', s.extra ? (cleave ? ", a cleave's further monster" : ", extra - no cleave here") : "",
                 WhyName(why), t.rewindMs, w.rtt, w.rttKnown ? "" : ", not measured yet", static_cast<unsigned>(s.ageMs), f.heroTrack ? f.heroDist : -1.0f, reach,
                 lim.reachCells + lim.tolCells, static_cast<unsigned long long>(attackMs), h && h->clock.Measured() ? "measured on his server unit" : "not measured yet - the default");
        }
    }
}

void __cdecl GameJob(const PluginContext*, void* user) noexcept {
    if (!g_jobTicket.Claim(uiticket::FromUser(user))) return;
    const uint64_t now = GetTickCount64();
    uintptr_t game = 0, lookup = 0;
    if (!meleetouch::ServerLookup(&game, &lookup)) {   // no server game learned (yet): the waiting swings are refused - off
        Queued q[16];
        size_t nq = 0;
        {
            std::lock_guard<std::mutex> lock(g_lock);
            nq = g_qn;
            for (size_t i = 0; i < nq; ++i) q[i] = g_q[i];
            g_qn = 0;
        }
        for (size_t i = 0; i < nq; ++i) {
            Verdict v{q[i].s.seq, q[i].s.hand, Why::Off, 0};
            uint8_t b[kVerdictBytes];
            if (const uint32_t n = BuildVerdict(v, b, sizeof b)) Send(q[i].peer, kMsgVerdict, b, n);
            ++g_hc.refused[static_cast<int>(Why::Off)];
        }
        return;
    }
    g_gameAt.store(now ? now : 1);
    SampleAndJudge(game, lookup, now);
}

// ================================================= the joined client ==================================================================

std::atomic<bool> g_hostOn{false};       // the host's rule: it strikes the joined players' touches ...
std::atomic<uint64_t> g_hostAt{0};       // ... said then (0: never, or the connection lost)
std::atomic<uint32_t> g_myHero{0};
std::atomic<uint32_t> g_seq{0};
std::mutex g_cliLock;                    // the pending touches and what came back
Pending g_pending;
struct Back { uint32_t monster = 0; int hand = 0; };
std::array<int, 8> g_confirm{};
size_t g_confirmN = 0;
std::array<Back, 8> g_refusedQ{};
size_t g_refusedN = 0;
struct ClientCount { unsigned sent = 0, failed = 0, struck = 0, refused = 0, late = 0, pings = 0, rewindSum = 0, rewindN = 0; } g_cc;
unsigned g_toldSent = 0, g_toldBack = 0;

void Report(uint64_t now) {
    static uint64_t at = 0;
    if (now - at < 10000) return;
    at = now;
    if (g_hc.taken || g_hc.pongs) {
        unsigned refused = 0;
        for (const unsigned r : g_hc.refused) refused += r;
        LogF("coop strike (host): %u touches taken, %u struck, %u refused (%u hero moved, %u far, %u too fast, %u again, %u not hostile, %u wall, %u no monster, %u dead, "
             "%u no hero, %u too old, %u off, %u peer); rewind %u ms on average, %u at most; %u pings, %u pongs",
             g_hc.taken, g_hc.struck, refused, g_hc.refused[static_cast<int>(Why::HeroMoved)], g_hc.refused[static_cast<int>(Why::Far)],
             g_hc.refused[static_cast<int>(Why::TooFast)], g_hc.refused[static_cast<int>(Why::Again)], g_hc.refused[static_cast<int>(Why::NotHostile)],
             g_hc.refused[static_cast<int>(Why::Wall)], g_hc.refused[static_cast<int>(Why::NoMonster)], g_hc.refused[static_cast<int>(Why::MonsterDead)],
             g_hc.refused[static_cast<int>(Why::NoHero)], g_hc.refused[static_cast<int>(Why::TooOld)], g_hc.refused[static_cast<int>(Why::Off)],
             g_hc.refused[static_cast<int>(Why::PeerHero)], g_hc.rewindN ? g_hc.rewindSum / g_hc.rewindN : 0u, g_hc.rewindMax, g_hc.pings, g_hc.pongs);
        g_hc = HostCount{};
    }
    if (g_cc.sent || g_cc.pings) {
        LogF("coop strike (client): %u touches sent to the host (%u could not be), %u struck there, %u refused (the normal attack pressed), %u answered too late; "
             "rewind %u ms on average; %u pings answered",
             g_cc.sent, g_cc.failed, g_cc.struck, g_cc.refused, g_cc.late, g_cc.rewindN ? g_cc.rewindSum / g_cc.rewindN : 0u, g_cc.pings);
        g_cc = ClientCount{};
    }
}

}  // namespace

// ================================================= the interface ======================================================================

void Init(const PluginContext* ctx) {
    g_ctx = ctx;
    const ThreadService* t = nullptr;
    g_threads = ctx && ctx->QueryService(&t) == ServiceQueryResult::Success ? t : nullptr;
}

void SetHostBox(bool on) {
    if (g_box.exchange(on) != on)
        Log(on ? "coop strike: on - as the host, the joined players' touches are struck here with the time put back (the server's attack for their hero; "
                 "their hero's attack rate read on this server; any view, no headset needed)"
               : "coop strike: off - as the host, the joined players' touches are the game's own command (or what their own options send)");
}
bool HostBox() { return g_box.load(); }
bool HostCanStrike(uint64_t nowMs) {
    const uint64_t at = g_gameAt.load();
    return g_box.load() && meleedirect::Ready() && at && nowMs >= at && nowMs - at <= kGameFreshMs;
}

void OnHello(uint64_t peer, uint32_t hero) {
    const uint32_t own = moveattack::HeroId();
    bool bound = false;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        Peer* p = g_peers.Bind(peer, hero, own);
        if (p) { p->seenAt = GetTickCount64(); bound = true; }
    }
    const uint64_t now = GetTickCount64();
    const bool on = bound && HostCanStrike(now);
    uint8_t b[kSwingRuleBytes];
    if (const uint32_t n = BuildSwingRule(on, b, sizeof b)) meleedirectclient::NetSendToClient(peer, kMsgSwingRule, b, n);   // (in the callback, as the cleave's rule)
    static std::atomic<int> toldState{-1};
    const int state = (g_box.load() ? 1 : 0) | (on ? 2 : 0) | (bound ? 4 : 0);
    if (toldState.exchange(state) != state)
        LogF("coop strike (host): hero %u said hello - %s", hero,
             !g_box.load() ? "off here (the box is unticked): his touches are the game's own command"
             : !bound      ? "NOT bound (the peer named a hero not its own, or all slots taken): off for him"
             : on          ? "on: his touches are struck here with the time put back"
                           : "ticked, but this PC cannot strike yet (the server's game not learned, or Direct hit's hooks not in) - off for him until it can");
}

void HostMessage(uint64_t peer, uint16_t id, const void* data, uint32_t size) {
    const uint64_t now = GetTickCount64();
    if (id == kMsgPong) {
        uint32_t stamp = 0, hero = 0, rtt = 0;
        if (!ParsePong(data, size, &stamp, &hero)) return;
        std::lock_guard<std::mutex> lock(g_lock);
        Peer* p = g_peers.Bind(peer, hero, moveattack::HeroId());
        if (p && RttOf(static_cast<uint32_t>(now), stamp, &rtt)) {
            const bool was = p->rtt.Known();
            p->rtt.Add(rtt);
            p->seenAt = now;
            ++g_hc.pongs;
            if (!was && p->rtt.Known()) LogF("coop strike (host): hero %u's round trip %u ms - his touches are judged that much back (plus their age)", hero, p->rtt.Ms());
        }
        return;
    }
    if (id != kMsgSwing) return;
    Swing s;
    if (!ParseSwing(data, size, &s)) return;
    Queued w;
    w.peer = peer;
    w.s = s;
    w.recvAt = now;
    const bool off = !g_box.load();
    bool busy = false;
    if (!off) {
        std::lock_guard<std::mutex> lock(g_lock);
        Peer* p = g_peers.Bind(peer, s.hero, moveattack::HeroId());
        w.peerHero = p != nullptr;
        if (p) { w.rtt = p->rtt.Ms(); w.rttKnown = p->rtt.Known(); p->seenAt = now; }
        if (g_qn < g_q.size()) g_q[g_qn++] = w;
        else busy = true;
    }
    ++g_hc.taken;
    if (busy || off) {   // answered at once: too many waiting, or the box off here
        const Verdict v{s.seq, s.hand, busy ? Why::Busy : Why::Off, 0};
        uint8_t b[kVerdictBytes];
        if (const uint32_t n = BuildVerdict(v, b, sizeof b)) meleedirectclient::NetSendToClient(peer, kMsgVerdict, b, n);
        ++g_hc.refused[static_cast<int>(v.why)];
    }
}

void PeerGone(uint64_t peer) {
    std::lock_guard<std::mutex> lock(g_lock);
    g_peers.Forget(peer);
}

void HostGone() {
    g_hostAt.store(0);
    g_hostOn.store(false);
    std::lock_guard<std::mutex> lock(g_cliLock);
    g_pending.Clear();
}

void ClientMessage(uint16_t id, const void* data, uint32_t size) {
    const uint64_t now = GetTickCount64();
    if (id == kMsgSwingRule) {
        bool on = false;
        if (!ParseSwingRule(data, size, &on)) return;
        g_hostAt.store(now ? now : 1);
        if (g_hostOn.exchange(on) != on)
            Log(on ? "coop strike (client): the host strikes our touches with the time put back - a direct hit goes to it, not as the game's command (no walk-up)"
                   : "coop strike (client): the host does not strike our touches (its box unticked, or it cannot yet) - a direct hit goes as the game's command");
        return;
    }
    if (id == kMsgPing) {
        uint32_t stamp = 0;
        if (!ParsePing(data, size, &stamp)) return;
        const uint32_t hero = g_myHero.load();
        if (!hero) return;
        ++g_cc.pings;
        appthread::Post(appthread::kCoopStrikeClient, [stamp, hero] {   // (the SDK's client sends: the app thread only)
            uint8_t b[kPongBytes];
            if (const uint32_t n = BuildPong(stamp, hero, b, sizeof b)) {
                appthread::NoteCall("coop strike (client)", "sendToHost (pong)");
                meleedirectclient::NetSendToHost(kMsgPong, b, n);
            }
        });
        return;
    }
    if (id != kMsgVerdict) return;
    Verdict v;
    if (!ParseVerdict(data, size, &v)) return;
    Pending::Item it;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(g_cliLock);
        found = g_pending.Take(v.seq, now, &it);
        if (found && v.why == Why::Struck && g_confirmN < g_confirm.size()) g_confirm[g_confirmN++] = it.hand;
        if (found && v.why != Why::Struck && g_refusedN < g_refusedQ.size()) g_refusedQ[g_refusedN++] = {it.monster, it.hand};
    }
    if (v.why == Why::Off) g_hostOn.store(false);   // (the host's box went off: back to the game's command until it says on again)
    if (!found) { ++g_cc.late; return; }
    if (v.why == Why::Struck) ++g_cc.struck;
    else ++g_cc.refused;
    g_cc.rewindSum += v.rewindMs;
    ++g_cc.rewindN;
    if (g_toldBack < 40 || (v.why != Why::Struck && g_toldBack < 80)) {
        ++g_toldBack;
        LogF("coop strike (client): touch #%u on monster %u - the host %s%s | rewind %u ms, answered %llu ms after the touch", static_cast<unsigned>(v.seq), it.monster,
             v.why == Why::Struck ? "struck it" : "refused it: ", v.why == Why::Struck ? "" : WhyName(v.why), static_cast<unsigned>(v.rewindMs),
             static_cast<unsigned long long>(now - it.at));
    }
}

bool HostStrikes(uint64_t nowMs) {
    const uint64_t at = g_hostAt.load();
    return g_hostOn.load() && at && nowMs >= at && nowMs - at <= 3000 && meleedirectclient::Connected();
}

bool SendSwing(Swing s, uint64_t touchAt, uint64_t nowMs) {
    if (!s.hero || !s.monster) return false;
    g_myHero.store(s.hero);
    const uint64_t age = nowMs >= touchAt ? nowMs - touchAt : 0;
    s.ageMs = static_cast<uint16_t>(std::min<uint64_t>(age, 60000));
    s.seq = static_cast<uint16_t>(g_seq.fetch_add(1) + 1);
    uint8_t b[kSwingBytes];
    const uint32_t n = BuildSwing(s, b, sizeof b);
    appthread::NoteCall("coop strike (client)", "sendToHost (swing)");
    if (!n || !meleedirectclient::NetSendToHost(kMsgSwing, b, n)) {
        ++g_cc.failed;
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(g_cliLock);
        g_pending.Add(s.seq, s.monster, s.hand, touchAt);
    }
    ++g_cc.sent;
    if (g_toldSent < 30) {
        ++g_toldSent;
        LogF("coop strike (client): touch #%u on monster %u sent to the host (%c hand%s, %u ms after the touch) - our hero at %.1f, %.1f, the monster at %.1f, %.1f (%.1f cells) | "
             "%.1f m/s, %.2f N s, %.2f m",
             static_cast<unsigned>(s.seq), s.monster, s.hand ? 'L' : 'R', s.extra ? ", a further monster of the swing" : "", static_cast<unsigned>(s.ageMs), s.heroX, s.heroY,
             s.monX, s.monY, s.monX >= 0.0f ? Dist(s.heroX, s.heroY, s.monX, s.monY) : -1.0f, s.speed, s.force, s.length);
    }
    return true;
}

bool TakeConfirm(int* hand) {
    std::lock_guard<std::mutex> lock(g_cliLock);
    if (!g_confirmN) return false;
    *hand = g_confirm[0];
    for (size_t i = 1; i < g_confirmN; ++i) g_confirm[i - 1] = g_confirm[i];
    --g_confirmN;
    return true;
}

bool TakeRefused(uint32_t* monster, int* hand) {
    std::lock_guard<std::mutex> lock(g_cliLock);
    if (!g_refusedN) return false;
    *monster = g_refusedQ[0].monster;
    *hand = g_refusedQ[0].hand;
    for (size_t i = 1; i < g_refusedN; ++i) g_refusedQ[i - 1] = g_refusedQ[i];
    --g_refusedN;
    return true;
}

void Tick(uint64_t nowMs, bool inWorld) {
    Report(nowMs);
    const bool box = g_box.load();
    static bool wasBox = false;
    if (box != wasBox) {
        wasBox = box;
        meleedirect::SetClientHostWanted(box);   // the server's attack for a joined hero (its hooks go in for it)
        meleetouch::SetCoopWanted(box);          // the server's game learned from its own lookups (melee_touch's observer goes in for it)
    }
    if (!box || !g_ctx || !g_threads || !g_threads->runOnGameThread) return;
    meleedirect::Install();   // (tried again until in: the server's code is decrypted as it runs; nothing once in)
    int peers = 0;
    uint32_t learn = 0;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        for (int i = 0; i < Peers::kSlots; ++i) {
            Peer& p = g_peers[i];
            if (!p.peer) continue;
            ++peers;
            if (!learn) learn = p.hero;
            if (nowMs - p.pingAt >= kPingMs) {   // the round trip: the host's own measure
                p.pingAt = nowMs;
                uint8_t b[kPingBytes];
                if (const uint32_t n = BuildPing(static_cast<uint32_t>(GetTickCount64()), b, sizeof b)) {
                    Send(p.peer, kMsgPing, b, n);
                    ++g_hc.pings;
                }
            }
        }
    }
    meleetouch::SetCoopHero(learn);   // (a joined hero's lookups teach melee_touch the server's game on a host that plays flat)
    if (peers && inWorld && nowMs - g_jobAt.load() >= kSampleMs) {
        g_jobAt.store(nowMs);
        if (const uint32_t token = g_jobTicket.Start(nowMs))
            if (g_threads->runOnGameThread(g_ctx, &GameJob, uiticket::ToUser(token)) != Threads::Result::Success) g_jobTicket.Claim(token);
    }
    bool any = false;
    {
        std::lock_guard<std::mutex> lock(g_outLock);
        any = g_outN != 0;
    }
    if (any && !appthread::Post(appthread::kCoopStrikeHost, [] { DrainOut(); })) {
        static std::atomic<bool> told{false};
        if (!told.exchange(true)) Log("coop strike (host): no way onto the game's app thread - the verdicts and pings are NOT sent (from another thread the network code ends the game)");
    }
}

}  // namespace coopstrikewire
