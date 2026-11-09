// throw_born.cpp - [hands] throw_direct, the game's side: the server's missile create (CreateMissile 0x5371A0) hooked for ONE call, so a thrown potion's game missile
// is born on its way so that it explodes as our model lands (the pure part, the reasons and the numbers: vr/throw_born.h). The user's experimental, opt-in option,
// offline only (the create is the server's: with no local server the potion shooter never runs in our process and the hook never sees a potion).
//
// Safety, as with "Direct hit": every read and every call into the game is behind an SEH guard; a fault turns the option off for the session and the throw goes on
// the game's own way (its missile flies from the hero to the cast point); the hook rewrites nothing but the one params block of the armed throw's potion, and only
// after the hero's own unit, the potion shooter's flags, a potion missile class, the hero's cell and the game's own room test at the cast point have all agreed.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <intrin.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>

#include <D2RLPlugin/api.h>

#include "log_async.h"
#include "missile_probe.h"
#include "sigscan.h"
#include "throw_born.h"
#include "view_gate.h"

using namespace D2RL;

namespace throwborn {
namespace {

// Addresses in 3.3.93787 (the keys of game_sigs.h).
constexpr uint64_t RVA_CREATE = 0x5371A0, RVA_ROOM_OF = 0x34B440, RVA_ROOM_POINT = 0x38DFB0;
// push rbp; push rdi; push r14; lea rbp,[rsp-40h]; sub rsp,140h
const uint8_t kSigCreate[17] = {0x40, 0x55, 0x57, 0x41, 0x56, 0x48, 0x8D, 0x6C, 0x24, 0xC0, 0x48, 0x81, 0xEC, 0x40, 0x01, 0x00, 0x00};

using CreateFn = uintptr_t (*)(uintptr_t game, uintptr_t params);
using RoomOfFn = uintptr_t (*)(uintptr_t unit);
using RoomPointFn = uintptr_t (*)(uintptr_t room, int x, int y);

const PluginContext* g_ctx = nullptr;
std::atomic<bool> g_wanted{false}, g_faulted{false}, g_observe{false};
std::atomic<uint64_t> g_createdAt{0};   // GetTickCount64 of the armed throw's potion create (0: none since the last Arm)
std::atomic<int> g_state{0};   // 0 not tried, 1 the hook is in, 2 not possible (another build?)
CreateFn g_origCreate = nullptr;
Window g_window;
std::atomic<unsigned> g_seen{0}, g_relocated{0}, g_declined{0};

void Log(const char* text) { if (g_ctx) g_ctx->LogInfo(text); }
template <typename... A> void LogF(const char* fmt, A... a) {
    char b[400];
    snprintf(b, sizeof b, fmt, a...);
    Log(b);
}

bool ReadMem(void* d, uintptr_t s, size_t n) noexcept {
    if (s < 0x10000) return false;
    __try { memcpy(d, reinterpret_cast<const void*>(s), n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool WriteMem(uintptr_t d, const void* s, size_t n) noexcept {
    if (d < 0x10000) return false;
    __try { memcpy(reinterpret_cast<void*>(d), s, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallRoomOf(uintptr_t fn, uintptr_t unit, uintptr_t* out) noexcept {
    __try { *out = reinterpret_cast<RoomOfFn>(fn)(unit); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// The hero's cell: the unit's path (+0x38) holds the 16.16 coordinates, the same as the client's (vr/melee_touch.cpp GridOf).
bool UnitCell(uintptr_t unit, int* cx, int* cy) noexcept {
    uintptr_t path = 0;
    if (!ReadMem(&path, unit + 0x38, sizeof path) || !path) return false;
    uint16_t p[4];
    if (!ReadMem(p, path, sizeof p)) return false;
    *cx = p[1];
    *cy = p[3];
    return IsCell(*cx) && IsCell(*cy);
}
bool CallRoomPoint(uintptr_t fn, uintptr_t room, int x, int y, uintptr_t* out) noexcept {
    __try { *out = reinterpret_cast<RoomPointFn>(fn)(room, x, y); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// [debug] bow_latency: the hero's creates, for the log (written on the game's thread by the hook, read by the draw thread's tick).
SRWLOCK g_madeLock = SRWLOCK_INIT;
constexpr size_t kMadeRing = 32;
Made g_made[kMadeRing];
unsigned g_madeTotal = 0;

void Fault(const char* where) {
    if (g_faulted.exchange(true)) return;
    LogF("throw direct: a fault in %s - the option is off for this session, a thrown potion flies the game's own way again", where);
}

// The hook's whole job, on the game's thread, for the call that may be the armed throw's potion: false = pass the params as the game made them.
bool TryRelocate(uintptr_t params) noexcept {
    Block block;
    if (!ReadMem(block.data(), params, block.size())) return false;
    const Seen s = Read(block.data());
    ++g_seen;
    // the cheap pre-test first: only the potion shooter's block goes on to the unit and the room tests
    if (!IsPotionFlags(s.flags) || !IsPotionClass(s.missileClass)) return false;
    uint32_t head[4] = {};
    const bool ownerOk = s.owner != 0 && ReadMem(head, static_cast<uintptr_t>(s.owner), sizeof head);
    const bool hero = ownerOk && head[0] == 0;   // unit type 0 = a player
    const uintptr_t roomOf = d2rsig::Addr(RVA_ROOM_OF), roomPoint = d2rsig::Addr(RVA_ROOM_POINT);
    bool room = true;
    uintptr_t heroRoom = 0;
    if (hero && IsCell(s.tx) && IsCell(s.ty)) {
        uintptr_t r1 = 0;
        if (!roomOf || !roomPoint || !CallRoomOf(roomOf, static_cast<uintptr_t>(s.owner), &heroRoom) || !heroRoom || !CallRoomPoint(roomPoint, heroRoom, s.tx, s.ty, &r1)) {
            Fault("the room test");
            return false;
        }
        room = r1 != 0;
    }
    const uint64_t now = GetTickCount64();
    const uint64_t landAt = g_window.LandAt();
    const Verdict v = Decide(s, g_window.Armed(now), hero, room);
    if (v == Verdict::NotArmed || v == Verdict::NotPotion || v == Verdict::NotHero) return false;
    g_window.Take(now);   // this was the armed throw's potion whatever happens next: the window is spent
    g_createdAt.store(now);
    if (v != Verdict::Relocate) {
        ++g_declined;
        LogF("throw direct: the potion at cell %d %d is left to the game's own flight - %s", s.tx, s.ty, Why(v));
        return false;
    }
    int hx = 0, hy = 0;
    if (!UnitCell(static_cast<uintptr_t>(s.owner), &hx, &hy) || std::abs(s.tx - hx) > 50 || std::abs(s.ty - hy) > 50) {
        ++g_declined;
        LogF("throw direct: the potion at cell %d %d is left to the game's own flight - the hero's cell is not readable here (read %d %d)", s.tx, s.ty, hx, hy);
        return false;
    }
    const double remainingS = landAt > now ? (double)(landAt - now) * 0.001 : 0.0;
    Start plan = PlanStart(hx, hy, s.tx, s.ty, remainingS);
    if (plan.kind == Start::Kind::Shifted) {   // the game's own room test at the start cell too: on a wall or off the map it is born at the cast point instead
        uintptr_t r2 = 0;
        if (!CallRoomPoint(roomPoint, heroRoom, plan.sx, plan.sy, &r2) || !r2) plan = PlanStart(hx, hy, s.tx, s.ty, 0.0);
    }
    if (plan.kind == Start::Kind::Natural || plan.kind == Start::Kind::NoDirection) {
        ++g_declined;
        LogF("throw direct: the potion at cell %d %d (hero %d %d, the model lands in %.2f s) is left to the game's own flight - %s", s.tx, s.ty, hx, hy, remainingS, Why(plan.kind));
        return false;
    }
    Rewrite(block.data(), plan);
    if (!WriteMem(params, block.data(), block.size())) { Fault("the params write"); return false; }
    ++g_relocated;
    LogF("throw direct: the potion (missile class %d), hero cell %d %d, cast cell %d %d, the model lands in %.2f s - %s: start cell %d %d, a life of %d frames (%.2f s)",
         s.missileClass, hx, hy, s.tx, s.ty, remainingS, Why(plan.kind), plan.sx, plan.sy, plan.frames, plan.frames / kGameTickHz);
    return true;
}

// The look: the block read, the owner's unit type asked (0 = a player), the facts written down. Nothing written back.
void NoteCreate(uintptr_t params) noexcept {
    Block block;
    if (!ReadMem(block.data(), params, block.size())) return;
    const Seen s = Read(block.data());
    uint32_t head[4] = {};
    if (s.owner == 0 || !ReadMem(head, static_cast<uintptr_t>(s.owner), sizeof head) || head[0] != 0) return;
    Made m;
    m.at = NowS();
    m.missileClass = s.missileClass;
    m.flags = s.flags;
    m.tx = s.tx;
    m.ty = s.ty;
    uintptr_t rec = 0;   // the unit's animation record: where it is, and what it holds (read, never written)
    if (ReadMem(&rec, static_cast<uintptr_t>(s.owner) + 0x70, sizeof rec) && rec > 0x10000 && ReadMem(m.anim, rec, sizeof m.anim)) { m.animRead = true; m.animPtr = rec; }
    AcquireSRWLockExclusive(&g_madeLock);
    g_made[g_madeTotal++ % kMadeRing] = m;
    ReleaseSRWLockExclusive(&g_madeLock);
}

// [debug] missile_probe (vr/missile_probe.h): every create of a player's missile - the block decoded, the units it names, the game's call site - and the
// creates of one shot summed up (a Multiple Shot's fan). Read only; the lines go through the async log (the game's thread does not wait for the file).
std::atomic<bool> g_probe{false}, g_probeFaulted{false};   // (a fault of the probe's turns the probe off, not [hands] throw_direct)
bool (*g_heroId)(uint32_t*) = nullptr;
SRWLOCK g_probeLock = SRWLOCK_INIT;
missileprobe::Grouper g_probeGroups;
unsigned g_probeInShot = 0;   // the creates of the open shot so far (the first kProbeLines get a line each; the shot's line counts them all)
constexpr unsigned kProbeLines = 6;

void ProbeLog(const char* text) { if (g_ctx) logasync::Info(g_ctx, text); }

// A unit's type, id and (a player or a monster: its path) cell; false: not readable.
bool UnitFacts(uintptr_t unit, uint32_t* type, uint32_t* id, int* cx, int* cy, bool* cellOk) noexcept {
    uint32_t head[4] = {};
    if (!ReadMem(head, unit, sizeof head)) return false;
    *type = head[0];
    *id = head[2];
    *cellOk = (head[0] == 0 || head[0] == 1) && UnitCell(unit, cx, cy);
    return true;
}
void DescribeUnit(uintptr_t unit, char* out, size_t n) noexcept {
    if (!unit) { snprintf(out, n, "none"); return; }
    uint32_t type = 0, id = 0;
    int cx = 0, cy = 0;
    bool cellOk = false;
    if (!UnitFacts(unit, &type, &id, &cx, &cy, &cellOk)) { snprintf(out, n, "%p (not readable)", reinterpret_cast<void*>(unit)); return; }
    static const char* const kTypes[] = {"player", "monster", "object", "missile", "item", "tile"};
    const char* name = type < 6 ? kTypes[type] : "a unit of another type";
    if (cellOk) snprintf(out, n, "%s %u at %d %d", name, id, cx, cy);
    else snprintf(out, n, "%s %u (type %u)", name, id, type);
}

// The shot at release (vr/shot_release.h): a player's missile create is put to the caller (vrcam's flat shot), which may answer another target cell. Only the
// block's two target words are written; a fault turns this off for the session (the game's own aim again), not [hands] throw_direct or the probe.
std::atomic<bool> g_aim{false}, g_aimFaulted{false};
bool (*g_aimFn)(const AimAsk&, int*, int*) = nullptr;

void TryAim(uintptr_t params) noexcept {
    uint8_t block[missileprobe::kSize];
    if (!g_aimFn || !ReadMem(block, params, sizeof block)) return;
    const missileprobe::Params p = missileprobe::Decode(block);
    uint32_t ownerType = 0, ownerId = 0;
    int ox = 0, oy = 0;
    bool ownerCell = false;
    if (!p.owner || !UnitFacts(static_cast<uintptr_t>(p.owner), &ownerType, &ownerId, &ox, &oy, &ownerCell) || ownerType != 0) return;   // a player's only
    if (!(p.flags & (missileprobe::kFlagTargetRel | missileprobe::kFlagTargetAbs))) return;   // no target of its own: nothing to turn
    AimAsk a;
    a.ownerId = ownerId;
    a.cls = p.cls;
    int sx = p.sx, sy = p.sy;   // the start the create takes: the block's cell (0x1), else the source unit's place (as the probe reads it)
    if (!(p.flags & missileprobe::kFlagStart)) {
        uint32_t t = 0, i = 0;
        bool ok = false;
        if (!(p.source && UnitFacts(static_cast<uintptr_t>(p.source), &t, &i, &sx, &sy, &ok) && ok)) {
            if (!ownerCell) return;
            sx = ox;
            sy = oy;
        }
    }
    a.sx = sx;
    a.sy = sy;
    a.hasTarget = true;
    missileprobe::TargetCell(p, sx, sy, &a.tx, &a.ty);
    a.targetUnit = p.targetUnit != 0;
    int tx = 0, ty = 0;
    if (!g_aimFn(a, &tx, &ty)) return;
    const int32_t w[2] = {(p.flags & missileprobe::kFlagTargetRel) ? tx - sx : tx, (p.flags & missileprobe::kFlagTargetRel) ? ty - sy : ty};
    if (!WriteMem(params + missileprobe::kOffTargetX, w, sizeof w) && !g_aimFaulted.exchange(true))
        Log("shot at release: the create's target could not be written - off for this session, the game's own aim");
}

// The game's thread, inside the create's hook: the lines for one create of a player's missile. Nothing is written to the game.
void ProbeCreate(uintptr_t params, uint64_t caller) noexcept {
    uint8_t block[missileprobe::kSize];
    if (!ReadMem(block, params, sizeof block)) return;
    const missileprobe::Params p = missileprobe::Decode(block);
    uint32_t ownerType = 0, ownerId = 0;
    int ox = 0, oy = 0;
    bool ownerCell = false;
    if (!p.owner || !UnitFacts(static_cast<uintptr_t>(p.owner), &ownerType, &ownerId, &ox, &oy, &ownerCell) || ownerType != 0) return;   // a player's only
    // the start the create takes: the block's cell (0x1), else the source unit's place
    int sx = p.sx, sy = p.sy;
    const char* startFrom = "the block's";
    if (!(p.flags & missileprobe::kFlagStart)) {
        uint32_t t = 0, i = 0;
        bool ok = false;
        if (p.source && UnitFacts(static_cast<uintptr_t>(p.source), &t, &i, &sx, &sy, &ok) && ok) startFrom = "the source unit's";
        else if (ownerCell) { sx = ox; sy = oy; startFrom = "the owner's (the source's not read)"; }
        else { sx = sy = 0; startFrom = "not known"; }
    }
    int tx = 0, ty = 0;
    missileprobe::TargetCell(p, sx, sy, &tx, &ty);
    const float heading = missileprobe::HeadingDeg(sx, sy, tx, ty);
    const float cells = std::sqrt((float)(tx - sx) * (tx - sx) + (float)(ty - sy) * (ty - sy));
    uint32_t hero = 0;
    const bool heroKnown = g_heroId && g_heroId(&hero);
    missileprobe::Create c;
    c.t = NowS();
    c.ownerId = ownerId;
    c.caller = caller;
    c.cls = p.cls;
    c.headingDeg = heading;
    c.targetUnit = p.targetUnit != 0;
    missileprobe::Shot done;
    AcquireSRWLockExclusive(&g_probeLock);
    const bool flushed = g_probeGroups.Flush(c.t, &done);   // (the tick flushes too; whichever comes first)
    const bool wasOpen = g_probeGroups.Open();
    const bool cut = g_probeGroups.Add(c, &done);           // another owner, call site or a gap: the open shot finished (after a flush none is open)
    const bool finished = flushed || cut;
    const bool first = !wasOpen || cut;                     // this create opened a shot
    if (first) g_probeInShot = 0;
    const unsigned index = ++g_probeInShot;
    ReleaseSRWLockExclusive(&g_probeLock);
    char line[900];
    if (finished) {
        missileprobe::ShotLine(done, line, sizeof line);
        ProbeLog(line);
    }
    if (index > kProbeLines) return;   // (the shot's line counts it)
    char flags[160], target[96], source[96];
    missileprobe::FlagNames(p.flags, flags, sizeof flags);
    DescribeUnit(static_cast<uintptr_t>(p.targetUnit), target, sizeof target);
    DescribeUnit(static_cast<uintptr_t>(p.source), source, sizeof source);
    const char* name = missileprobe::ClassName(p.cls);
    snprintf(line, sizeof line,
             "missile probe: create %u of a shot by player %u%s (at %d %d) from 0x%llX: class %d%s%s%s, flags 0x%X (%s); start %d %d (%s) -> target %d %d "
             "(the block's %d %d), heading %.1f deg, %.1f cells; target unit %s; source %s; velocity %d, level %d, range per level %d, range off %d / %d, "
             "range %d; +0x3C %d, +0x58 %d, +0x64 %d, +0x68 %d, +0x70 %d",
             index, ownerId, heroKnown ? (hero == ownerId ? " (the hero)" : " (another player)") : "", ox, oy, (unsigned long long)caller, p.cls,
             name ? " (" : "", name ? name : "", name ? ")" : "", p.flags, flags, sx, sy, startFrom, tx, ty, p.tx, p.ty, heading, cells, target, source,
             p.velocity, p.level, p.rangeLevel, p.rangeOff, p.rangeOffOwn, p.range, p.f3c, p.f58, p.f64, p.f68, p.f70);
    ProbeLog(line);
    if (!first) return;   // the whole block once a shot: what the decode does not name
    for (size_t at = 0; at < missileprobe::kSize; at += 0x20) {
        char hex[160];
        missileprobe::HexLine(block, at, 0x20, hex, sizeof hex);
        snprintf(line, sizeof line, "missile probe:   %s", hex);
        ProbeLog(line);
    }
}

__declspec(noinline) uintptr_t HookCreate(uintptr_t game, uintptr_t params) {
    if (g_aim.load(std::memory_order_relaxed) && !g_aimFaulted.load(std::memory_order_relaxed)) {   // first: the probe and the look see what goes to the game
        __try { TryAim(params); } __except (EXCEPTION_EXECUTE_HANDLER) {
            if (!g_aimFaulted.exchange(true)) Log("shot at release: a fault in the create's look - off for this session, the game's own aim");
        }
    }
    if (g_observe.load(std::memory_order_relaxed) && !g_faulted.load(std::memory_order_relaxed)) {
        __try { NoteCreate(params); } __except (EXCEPTION_EXECUTE_HANDLER) { Fault("the create look"); }
    }
    if (g_probe.load(std::memory_order_relaxed) && !g_probeFaulted.load(std::memory_order_relaxed)) {
        const uint64_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress()) - reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        __try { ProbeCreate(params, caller); } __except (EXCEPTION_EXECUTE_HANDLER) {
            if (!g_probeFaulted.exchange(true)) Log("missile probe: a fault in the look - off for this session (nothing of the game's was changed)");
        }
    }
    if (g_wanted.load(std::memory_order_relaxed) && !g_faulted.load(std::memory_order_relaxed) && g_window.Armed(GetTickCount64())) {
        __try { TryRelocate(params); } __except (EXCEPTION_EXECUTE_HANDLER) { Fault("the create hook"); }
    }
    return g_origCreate(game, params);
}

}  // namespace

void Init(const PluginContext* ctx) { g_ctx = ctx; }

void Configure(bool wanted) {
    const bool was = g_wanted.exchange(wanted);
    if (wanted != was)
        Log(wanted ? "throw direct: on ([hands] throw_direct) - a thrown potion's game missile is born so that it explodes as the model lands (experimental, offline only)"
                   : "throw direct: off ([hands] throw_direct=0) - a thrown potion flies the game's own way");
}
bool Wanted() { return g_wanted.load(); }

double NowS() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
void Observe(bool on) {
    if (g_observe.exchange(on) != on) Log(on ? "bow latency: the server's missile create is watched, read only ([debug] bow_latency)" : "bow latency: off");
}
bool Observing() { return g_observe.load() && g_state.load() == 1 && !g_faulted.load(); }

void Probe(bool on) {
    if (g_probe.exchange(on) == on) return;
    Log(on ? "missile probe: on ([debug] missile_probe) - every create of a player's missile is logged with its params, read only (the server's create: offline and as the host)"
           : "missile probe: off");
}
void SetHeroIdSource(bool (*fn)(uint32_t* id)) { g_heroId = fn; }
void SetAimSource(bool (*fn)(const AimAsk& ask, int* tx, int* ty)) { g_aimFn = fn; }
void Aim(bool on) {
    if (g_aim.exchange(on) == on) return;
    Log(on ? "shot at release: on - the hero's arrow is turned at the server's create to the crosshair of that moment ([input] flat_shot_at_crosshair; offline and as the host)"
           : "shot at release: off");
}
void ProbeTick() {
    if (!g_probe.load(std::memory_order_relaxed)) return;
    missileprobe::Shot done;
    AcquireSRWLockExclusive(&g_probeLock);
    const bool finished = g_probeGroups.Flush(NowS(), &done);
    ReleaseSRWLockExclusive(&g_probeLock);
    if (!finished) return;
    char line[900];
    missileprobe::ShotLine(done, line, sizeof line);
    ProbeLog(line);
}
size_t CreatesSince(double sinceS, Made* out, size_t max) {
    size_t n = 0;
    AcquireSRWLockShared(&g_madeLock);
    const unsigned total = g_madeTotal;
    const unsigned first = total > kMadeRing ? total - (unsigned)kMadeRing : 0;
    for (unsigned i = first; i < total && n < max; ++i) {
        const Made& m = g_made[i % kMadeRing];
        if (m.at >= sinceS) out[n++] = m;
    }
    ReleaseSRWLockShared(&g_madeLock);
    return n;
}
bool Ready() { return g_wanted.load() && g_state.load() == 1 && !g_faulted.load(); }

void Install() {
    static ULONGLONG retryAt = 0;
    static unsigned tries = 0;
    if ((!g_wanted.load() && !g_observe.load() && !g_probe.load() && !g_aim.load()) || g_state.load() != 0 || !g_ctx || GetTickCount64() < retryAt) return;
    retryAt = GetTickCount64() + (tries < 30 ? 2000 : 30000);   // the server's code is decrypted as it runs: tried again
    if (!d2rsig::Addr(RVA_CREATE) || !d2rsig::Addr(RVA_ROOM_OF) || !d2rsig::Addr(RVA_ROOM_POINT)) return;   // not all found (yet)
    if (!d2rsig::Check(RVA_CREATE, kSigCreate, sizeof kSigCreate)) {
        if (++tries == 30) Log("throw direct: the game's missile create is not where it was (another build?) - a thrown potion flies the game's own way");
        return;
    }
    const bool in = d2rsig::Hook(RVA_CREATE, kSigCreate, sizeof kSigCreate, reinterpret_cast<void*>(&HookCreate), reinterpret_cast<void**>(&g_origCreate));
    if (in) {
        g_state.store(1);
        Log(g_wanted.load() ? "throw direct: ready - the server's missile create is hooked (it touches one call: the armed throw's potion)"
                            : "bow latency / missile probe: ready - the server's missile create is hooked, read only (it rewrites nothing)");
    } else if (++tries == 30) {
        g_state.store(2);
        Log("throw direct: the hook could not be put in - a thrown potion flies the game's own way");
    }
}

bool Arm(uint64_t nowMs, uint64_t landAtMs, double flightS) {
    if (!Ready() || !viewgate::F4()) return false;
    g_createdAt.store(0);
    g_window.Arm(nowMs, ArmTtlMs(flightS), landAtMs);
    return true;
}
void Disarm() { g_window.Cancel(); }
bool ActionFramePassed() {
    const uint64_t at = g_createdAt.load();
    return at != 0 && GetTickCount64() - at < 4000;
}

}  // namespace throwborn
