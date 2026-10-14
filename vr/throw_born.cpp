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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>

#include <D2RLPlugin/api.h>

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

__declspec(noinline) uintptr_t HookCreate(uintptr_t game, uintptr_t params) {
    if (g_observe.load(std::memory_order_relaxed) && !g_faulted.load(std::memory_order_relaxed)) {
        __try { NoteCreate(params); } __except (EXCEPTION_EXECUTE_HANDLER) { Fault("the create look"); }
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
    if ((!g_wanted.load() && !g_observe.load()) || g_state.load() != 0 || !g_ctx || GetTickCount64() < retryAt) return;
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
                            : "bow latency: ready - the server's missile create is hooked, read only (it rewrites nothing)");
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
