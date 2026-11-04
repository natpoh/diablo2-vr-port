// attack_path_wire.cpp - [gameplay] flat_attack_path on the game: see attack_path.h for what and why. EXPERIMENTAL, off by default. Addresses read in 3.3.94995 under D2RLoader 1.3.1; all of it was written from reading, what ran is in the log.
//
// The server (its thread): a walk the client asks for while the hero attacks reaches 0x42CED0 and would be dropped by the attack gate. Here its path
// is set as the walk's own start 0x429860 sets it, without the mode change 0x491130 at the end of 0x42E920, and the game's "not yet" (4) is answered.
// The path's target is the same object as the attack's (path +0x10 / +0x12 the point, +0x70 the unit; 0x342A50 clears the unit), so at the action
// frame the target is set again: for a melee blow to who stands before the camera now, or to nobody; for a shot or throw back to what it was started on.
// An attack has no event a frame, so one of ours steps the path (kTickCode, answered in the trigger's hook, gone with the mode's events at 0x48F820).
//
// The client (the game's thread, after the controller's update): the walk is sent as the stick's (0xFEFE0); its prediction (0xFF010 with 0x1E) would take
// the used skill away and change the mode, so it is replaced by the path alone (as 0xFB370 sets it), and 0xF9A00 steps it, as 0xFFBB0 does for a mode of
// type 1. The skills' own flag 1 is left alone on both sides: at a path's end it would make the action early.
//
// A TCP/IP game: who hosts decides, a joined player's own box means nothing. The joined client always connects to the host's channel, says who its hero is once
// a second and who stands before its camera; the host answers with its key, and its server acts for the heroes that spoke lately. Their game sends the other clients
// nothing for a path in an attack, so the host tells them the place every few frames with the game's own reassign 0x486B20.
//
// Every call behind an SEH guard: a fault turns the option off for the session.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <D2RLPlugin/api.h>

#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "attack_path.h"
#include "attack_path_wire.h"
#include "move_attack.h"
#include "move_attack_wire.h"
#include "sigscan.h"
#include "unit_scan.h"

namespace gamestate { bool MenuOpen(); uint32_t KindOfItemClass(uint32_t classId); }

namespace attackpath {
namespace {

// the server
constexpr uint64_t RVA_MODE_BY_DIR = 0x42CED0, RVA_POINT_OF_DIR = 0x3ACC60, RVA_SERVER_STEP = 0x491AC0, RVA_SET_EVENT = 0x48B720, RVA_EVENT_FRAME = 0x48B230;
constexpr uint64_t RVA_SET_TARGET = 0x34E0C0, RVA_LOOKUP = 0x48FE80, RVA_RELATION = 0x48E460, RVA_UNIT_WEAPON = 0x34B000, RVA_CLIENT_OF = 0x48FDE0, RVA_REASSIGN = 0x486B20;
// the client
constexpr uint64_t RVA_PREDICT = 0xFF010, RVA_DECODE_DIR = 0x3B2F20, RVA_CLIENT_STEP = 0xF9A00, RVA_AFTER_STEP = 0x100AE0, RVA_CLIENT_PLAYER = 0x9A480, RVA_WALK_DIR = 0xFEFE0;
constexpr uint64_t RVA_CMD_FLAGS = 0x8AFD0, RVA_PATH_FLAG = 0x34F8D0, RVA_PATH_X = 0x2EF880, RVA_PATH_Y = 0x3419F0, RVA_UNITS = 0x2A23910;
constexpr uint64_t RVA_CLIENT_GAME_TYPE = 0x2A233F4, RVA_LAUNCH_GAME_TYPE = 0x32EF888;
// both
constexpr uint64_t RVA_PATH_OF = 0x34AE80, RVA_PATH_TARGET = 0x342A50, RVA_PATH_TYPE = 0x342A70, RVA_PATH_VECTOR = 0x342750, RVA_PATH_VELOCITY = 0x342AE0, RVA_PATH_MAKE = 0x340E70;
constexpr uint64_t RVA_USED_SKILL = 0x34BA40, RVA_SKILL_FLAGS = 0x33CC00;

const uint8_t kSigModeByDir[15] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18};
const uint8_t kSigPredict[15] = {0x44, 0x89, 0x4C, 0x24, 0x20, 0x89, 0x4C, 0x24, 0x08, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54};

constexpr uintptr_t kUnitId = 0x8, kUnitMode = 0xC, kUnitGame = 0xD8, kGameFrame = 0x170;
constexpr uintptr_t kPathX = 0x10, kPathY = 0x12, kPathUnit = 0x70, kPathVelocity = 0xA0;
constexpr uintptr_t kRecStride = 0xB8, kRecStamina = 0x1951, kRecForcedWalk = 0x1952, kRecGrid = 0x195C, kMgrRunThreshold = 0x1F94;
constexpr int kPathVectorType = 0x11, kPathFindType = 7, kGateNotYet = 4, kPredictWalk = 0x1E;
constexpr uint32_t kStepMs = 120, kSendMs = 40, kScanMs = 20;

using ModeByDirFn = int (*)(uintptr_t game, uintptr_t unit, int mode, const float* dir, float mag, uint16_t x, uint16_t y);
using PredictFn = int (*)(int type, uintptr_t unit, const uint32_t* data, int flag);

void (*g_log)(const char*) = nullptr;
std::atomic<int> g_key{0};
std::atomic<float> g_cosHalf{0.7071f};
std::atomic<bool> g_faulted{false};
std::atomic<int> g_state{0};
std::atomic<bool (*)(float*, float*)> g_forward{nullptr};
ModeByDirFn g_origModeByDir = nullptr;
PredictFn g_origPredict = nullptr;

std::atomic<uintptr_t> g_clientUnit{0};
std::atomic<uint32_t> g_heroId{0};
std::atomic<bool> g_clientActs{false};       // our hero's verdict for our own server: the key, the view, the menu, the game type
std::atomic<int> g_percent{100};             // the speed bonuses, seen on the client's path while the hero walked or ran
std::atomic<int> g_clientKey{0};             // the key the client goes by: ours with the server here, the host's joined (ClientKey)
std::atomic<uint64_t> g_serverSeenMs{0};     // when the server's hook last ran in this process: we host, or play alone
std::atomic<uint64_t> g_gameSinceMs{0};      // when our client's hero of this game appeared
std::atomic<uint64_t> g_allowMs{0};          // when the host last said yes
std::atomic<int> g_allowKey{0};
namespace nw = D2RL::Network;
const D2RL::PluginContext* g_ctx = nullptr;
const D2RL::NetworkService* g_net = nullptr;
nw::ChannelHandle g_channel = nw::InvalidChannelHandle;
std::atomic<bool> g_netDead{false};
std::atomic<uint64_t> g_lastAskMs{0};        // the client's last walk of ours
std::atomic<uint64_t> g_predicted{0}, g_asked{0}, g_steps{0}, g_blows{0}, g_air{0};

SRWLOCK g_frontLock = SRWLOCK_INIT;
uint32_t g_front[kCandidates] = {};          // who stands before OUR camera, the nearest first (the client writes, our server reads)
size_t g_frontN = 0;
// The heroes of the joined clients that spoke lately (under g_frontLock; the channel's callback writes, the server reads).
struct Peer {
    uint64_t peer = 0;
    uint32_t hero = 0;
    uint64_t helloMs = 0, frontMs = 0;
    uint32_t ids[kCandidates] = {};
    uint32_t n = 0;
};
constexpr int kPeers = 8;
Peer g_peers[kPeers];

// the server's thread only
struct Attack {
    uintptr_t unit = 0;
    uint32_t hero = 0;
    int32_t end = 0;
    bool valid = false, ticking = false;
    bool walked = false;       // a walk of ours in this attack: the target is chosen again at the action (else the game's own blow)
    uint16_t x = 0, y = 0;     // what the attack was started on
    uint32_t targetType = 0, targetId = 0;
    bool hasTarget = false;
    int32_t lastAsk = 0, lastSync = 0;
    int percent = 100;         // this hero's speed bonuses, seen on the server's path while he walked or ran
};
Attack g_attacks[kPeers];
Attack* AttackOf(uint32_t hero) {
    Attack* free = nullptr;
    for (Attack& a : g_attacks) {
        if (a.hero == hero) return &a;
        if (!a.hero && !free) free = &a;
    }
    if (!free) free = &g_attacks[hero % kPeers];   // (eight heroes at most in a game: an old one's place)
    *free = {};
    free->hero = hero;
    return free;
}

std::atomic<int> g_lines{0};
void Say(const char* fmt, ...) {
    if (!g_log || g_lines.fetch_add(1) >= 400) return;
    char b[320];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    g_log(b);
}
void Fault(const char* where) {
    if (!g_faulted.exchange(true)) Say("vrcam: attack path - %s faulted: the option is OFF for this session", where);
}
bool On() { return g_key.load(std::memory_order_relaxed) > 0 && !g_faulted.load(std::memory_order_relaxed); }

bool Read(void* d, uintptr_t s, size_t n) noexcept {
    if (s < 0x10000) return false;
    __try { memcpy(d, reinterpret_cast<const void*>(s), n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template <class T> T Fn(uint64_t rva) { return reinterpret_cast<T>(d2rsig::Addr(rva)); }

// ---- the game's functions, guarded ---------------------------------------------------------------------------------------------------
uintptr_t PathOf(uintptr_t unit) noexcept {
    __try { return Fn<uintptr_t (*)(uintptr_t)>(RVA_PATH_OF)(unit); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
uint32_t SkillFlagsOf(uintptr_t unit) noexcept {
    __try {
        const uintptr_t s = Fn<uintptr_t (*)(uintptr_t)>(RVA_USED_SKILL)(unit);
        return s ? Fn<uint32_t (*)(uintptr_t)>(RVA_SKILL_FLAGS)(s) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
bool SetPathAndMake(uintptr_t path, uintptr_t unit, int type, const float* vec, float mag, int tx, int ty, int velocity, int makeArg) noexcept {
    __try {
        Fn<void (*)(uintptr_t, int, int)>(RVA_PATH_TARGET)(path, tx, ty);
        Fn<void (*)(uintptr_t, int)>(RVA_PATH_TYPE)(path, type);
        if (vec) Fn<void (*)(uintptr_t, const float*, float)>(RVA_PATH_VECTOR)(path, vec, mag);
        Fn<void (*)(uintptr_t, int, const char*, int)>(RVA_PATH_VELOCITY)(path, velocity, "attack_path", 0);
        Fn<int (*)(uintptr_t, uintptr_t, int, int)>(RVA_PATH_MAKE)(path, unit, makeArg, 1);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool PointOfDir(uintptr_t unit, uint32_t packed, const float* dir, float* vec, float* point, bool* direct) noexcept {
    __try { *direct = Fn<bool (*)(uintptr_t, uint32_t, const float*, float*, float*)>(RVA_POINT_OF_DIR)(unit, packed, dir, vec, point); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool ClientPlace(uintptr_t path, float* x, float* y) noexcept {
    __try {
        *x = (float)Fn<int (*)(uintptr_t)>(RVA_PATH_X)(path) / 65536.0f;
        *y = (float)Fn<int (*)(uintptr_t)>(RVA_PATH_Y)(path) / 65536.0f;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Decode(uint32_t packed, float* vec, float* mag, uint8_t* run) noexcept {
    __try { Fn<void (*)(uint32_t, float*, float*, uint8_t*)>(RVA_DECODE_DIR)(packed & 0xFFFF, vec, mag, run); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool SetEvent(uintptr_t game, uintptr_t unit, int32_t frame) noexcept {
    __try { Fn<void (*)(uintptr_t, uintptr_t, int, int, int, int, int)>(RVA_SET_EVENT)(game, unit, 0, frame, kTickCode, 0, 0); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool ServerStep(uintptr_t game, uintptr_t unit) noexcept {
    __try { Fn<int (*)(uintptr_t, uintptr_t)>(RVA_SERVER_STEP)(game, unit); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool ClientStep(uintptr_t unit) noexcept {
    __try {
        Fn<int (*)(uintptr_t, int)>(RVA_CLIENT_STEP)(unit, 1);
        Fn<void (*)(uintptr_t)>(RVA_AFTER_STEP)(unit);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool SetTarget(uintptr_t unit, uintptr_t target) noexcept {
    __try { Fn<void (*)(uintptr_t, uintptr_t)>(RVA_SET_TARGET)(unit, target); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool SetPoint(uintptr_t path, int x, int y) noexcept {
    __try { Fn<void (*)(uintptr_t, int, int)>(RVA_PATH_TARGET)(path, x, y); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
uintptr_t Lookup(uintptr_t game, uint32_t type, uint32_t id) noexcept {
    __try { return Fn<uintptr_t (*)(uintptr_t, int, uint32_t)>(RVA_LOOKUP)(game, (int)type, id); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
bool Hostile(uintptr_t game, uintptr_t hero, uintptr_t unit) noexcept {
    __try { return Fn<int (*)(uintptr_t, uintptr_t, uintptr_t)>(RVA_RELATION)(game, hero, unit) != 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
int32_t EventFrame(uintptr_t game, uintptr_t unit) noexcept {
    __try { return Fn<int (*)(uintptr_t, uintptr_t, int, int)>(RVA_EVENT_FRAME)(game, unit, 1, 0); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
uint32_t WeaponKind(uintptr_t unit) noexcept {
    __try {
        const uintptr_t item = Fn<uintptr_t (*)(uintptr_t)>(RVA_UNIT_WEAPON)(unit);
        uint32_t head[2] = {};
        if (!item || !Read(head, item, sizeof head) || head[0] != 4) return 0;
        return gamestate::KindOfItemClass(head[1]);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
bool CallClientPlayer(uint32_t index, uintptr_t* out) noexcept {
    __try { *out = Fn<uintptr_t (*)(uint32_t)>(RVA_CLIENT_PLAYER)(index); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallWalkDir(uintptr_t unit, const float* dir, float mag, bool run) noexcept {
    __try { Fn<void (*)(uintptr_t, const float*, float, bool)>(RVA_WALK_DIR)(unit, dir, mag, run); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallCmdFlags(uint16_t* out) noexcept {
    __try { *out = Fn<uint16_t (*)()>(RVA_CMD_FLAGS)(); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool PeerFresh(uint32_t hero, uint64_t now) {
    bool yes = false;
    AcquireSRWLockShared(&g_frontLock);
    for (const Peer& p : g_peers) if (p.hero == hero && Fresh(now, p.helloMs, kPeerFreshMs)) yes = true;
    ReleaseSRWLockShared(&g_frontLock);
    return yes;
}

// ---- the server -------------------------------------------------------------------------------------------------------------------
// A hero we act for on this game's server, in a plain attack: ours as our client says, a joined one's who spoke lately.
bool OurAttacker(uintptr_t game, uintptr_t unit, uint32_t* mode, uint32_t* hero) {
    if (!On()) return false;
    uint32_t head[4] = {};
    uintptr_t back = 0;
    if (!Read(head, unit, sizeof head) || head[0] != 0 || !head[kUnitId / 4]) return false;
    if (!Read(&back, unit + kUnitGame, sizeof back) || back != game) return false;
    const uint32_t id = head[kUnitId / 4];
    const bool own = id == g_heroId.load(std::memory_order_relaxed);
    if (own ? !g_clientActs.load(std::memory_order_acquire) : !PeerFresh(id, GetTickCount64())) return false;
    if (!PlainAttackMode(head[kUnitMode / 4]) || (SkillFlagsOf(unit) & kSkillMovesItself)) return false;
    *mode = head[kUnitMode / 4];
    *hero = id;
    return true;
}

// The clients of the other heroes we know are told where this one is: their game sends nothing for a path in an attack.
void TellOthers(uintptr_t game, uintptr_t unit, uint32_t hero, uintptr_t path) {
    uint32_t others[kPeers + 1];
    int n = 0;
    AcquireSRWLockShared(&g_frontLock);
    for (const Peer& p : g_peers) if (p.hero && p.hero != hero) others[n++] = p.hero;
    ReleaseSRWLockShared(&g_frontLock);
    const uint32_t own = g_heroId.load(std::memory_order_relaxed);
    if (own && own != hero && n <= kPeers) others[n++] = own;
    if (!n) return;
    uint16_t place[4] = {};
    if (!Read(place, path, sizeof place)) return;
    __try {
        for (int i = 0; i < n; ++i) {
            const uintptr_t other = Fn<uintptr_t (*)(uintptr_t, int, uint32_t)>(RVA_LOOKUP)(game, 0, others[i]);
            if (!other) continue;
            const uintptr_t client = Fn<uintptr_t (*)(uintptr_t)>(RVA_CLIENT_OF)(other);
            if (client) Fn<void (*)(uintptr_t, uintptr_t, int, int, int, int)>(RVA_REASSIGN)(client, unit, place[1], place[3], 0, 0);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { Fault("telling the other clients (0x486B20)"); }
}

// What the attack was started on is noted before the first walk of ours overwrites it.
Attack* NoteAttack(uintptr_t game, uintptr_t unit, uintptr_t path, uint32_t hero) {
    Attack* at = AttackOf(hero);
    const int32_t end = EventFrame(game, unit);
    if (at->valid && at->unit == unit && at->end == end) return at;
    const int percent = at->percent;
    *at = {};
    at->hero = hero;
    at->percent = percent;
    at->valid = true;
    at->unit = unit;
    at->end = end;
    Read(&at->x, path + kPathX, 2);
    Read(&at->y, path + kPathY, 2);
    uintptr_t target = 0;
    if (Read(&target, path + kPathUnit, sizeof target) && target) {
        uint32_t head[3] = {};
        if (Read(head, target, sizeof head)) { at->hasTarget = true; at->targetType = head[0]; at->targetId = head[2]; }
    }
    return at;
}

bool ServerWalk(uintptr_t game, uintptr_t unit, uint32_t hero, int mode, const float* dir, float mag, uint16_t x, uint16_t y) {
    const uintptr_t path = PathOf(unit);
    if (!path) { Fault("the server's path (0x34AE80)"); return false; }
    Attack* const at = NoteAttack(game, unit, path, hero);
    float vec[2] = {}, point[2] = {};
    bool direct = false;
    if (!PointOfDir(unit, (uint32_t)x | (uint32_t)y << 16, dir, vec, point, &direct)) { Fault("the point of a direction (0x3ACC60)"); return false; }
    const bool run = mode == 3 && RunAllowed(g_key.load(std::memory_order_relaxed));
    const int velocity = VelocityFor(at->percent, run);
    if (!SetPathAndMake(path, unit, direct ? kPathVectorType : kPathFindType, direct ? vec : nullptr, mag, (int)point[0], (int)point[1], velocity, 0)) { Fault("the server's path"); return false; }
    at->walked = true;
    int32_t now = 0;
    Read(&now, game + kGameFrame, sizeof now);
    at->lastAsk = now;
    if (!at->ticking) {
        if (!SetEvent(game, unit, now + 1)) { Fault("the event (0x48B720)"); return false; }
        at->ticking = true;
    }
    if (g_asked.fetch_add(1) < 6) Say("vrcam: attack path - the server took a walk in the attack (hero %u, mode kept, asked %d, velocity %d, %s, point %.0f %.0f)", hero, mode, velocity, direct ? "by vector" : "by search", point[0], point[1]);
    return true;
}

int HookModeByDir(uintptr_t game, uintptr_t unit, int mode, const float* dir, float mag, uint16_t x, uint16_t y) {
    g_serverSeenMs.store(GetTickCount64(), std::memory_order_relaxed);
    if (On() && (mode == 2 || mode == 3 || mode == 6)) {
        uint32_t head[4] = {};
        if (Read(head, unit, sizeof head) && head[0] == 0 && (head[kUnitMode / 4] == 2 || head[kUnitMode / 4] == 3 || head[kUnitMode / 4] == 6)) {   // this hero's speed bonuses, as the game has them
            int v = 0;
            const uintptr_t path = PathOf(unit);
            if (path && Read(&v, path + kPathVelocity, sizeof v) && v > 0) AttackOf(head[kUnitId / 4])->percent = PercentOf(v, head[kUnitMode / 4] == 3);
        } else {
            uint32_t cur = 0, hero = 0;
            if (OurAttacker(game, unit, &cur, &hero) && ServerWalk(game, unit, hero, mode, dir, mag, x, y)) return kGateNotYet;
        }
    }
    return g_origModeByDir(game, unit, mode, dir, mag, x, y);
}

// ---- the client -------------------------------------------------------------------------------------------------------------------
bool ClientWalk(uintptr_t unit, uint32_t packed) {
    const uintptr_t path = PathOf(unit);
    if (!path) return false;
    float vec[2] = {}, mag = 0.0f, x = 0.0f, y = 0.0f;
    uint8_t run = 0;
    if (!Decode(packed, vec, &mag, &run)) { Fault("the walk's decoding (0x3B2F20)"); return false; }
    if (!ClientPlace(path, &x, &y)) { Fault("the client's place"); return false; }
    int flag = 0;
    __try { flag = Fn<int (*)(uintptr_t)>(RVA_PATH_FLAG)(unit); } __except (EXCEPTION_EXECUTE_HANDLER) { Fault("0x34F8D0"); return false; }
    const bool running = run && RunAllowed(g_clientKey.load(std::memory_order_relaxed));
    const int velocity = VelocityFor(g_percent.load(std::memory_order_relaxed), running);
    if (!SetPathAndMake(path, unit, kPathVectorType, vec, mag, (int)(x + vec[0] * 2.0f), (int)(y + vec[1] * 2.0f), velocity, flag)) { Fault("the client's path"); return false; }
    return true;
}

int HookPredict(int type, uintptr_t unit, const uint32_t* data, int flag) {
    if (type == kPredictWalk && data && !g_faulted.load(std::memory_order_relaxed) && g_clientActs.load(std::memory_order_acquire) && unit == g_clientUnit.load(std::memory_order_relaxed)) {
        uint32_t mode = 0;
        uint32_t packed = 0;
        if (Read(&mode, unit + kUnitMode, sizeof mode) && PlainAttackMode(mode) && Read(&packed, (uintptr_t)data, sizeof packed) && ClientWalk(unit, packed)) {
            g_lastAskMs.store(GetTickCount64(), std::memory_order_release);
            if (g_predicted.fetch_add(1) < 6) Say("vrcam: attack path - the client set the path in the attack (mode %u kept)", mode);
            return 1;
        }
    }
    return g_origPredict(type, unit, data, flag);
}

void ScanFront(uintptr_t unit, bool toHost) {
    static uint64_t last = 0;
    const uint64_t now = GetTickCount64();
    if (now - last < kScanMs) return;
    last = now;
    uint32_t ids[kCandidates] = {};
    size_t n = 0;
    float sx = 0.0f, sy = 0.0f, fx = 0.0f, fz = 0.0f;
    const auto fn = g_forward.load(std::memory_order_acquire);
    const bool known = fn && fn(&sx, &sy);
    if (known) {
        ScreenToGrid(sx, sy, &fx, &fz);
        const float len = std::sqrt(fx * fx + fz * fz);
        auto read = [](void* d, uintptr_t s, size_t c) { return Read(d, s, c); };
        unitscan::Unit self;
        float hx = 0.0f, hz = 0.0f;
        if (len > 1e-4f && read(self.b, unit, sizeof self.b) && unitscan::Place(read, self, &hx, &hz)) {
            fx /= len; fz /= len;
            Candidate all[64];
            size_t count = 0;
            unitscan::EachUnit(read, d2rsig::Addr(RVA_UNITS), unitscan::kMonster, [&](uintptr_t, const unitscan::Unit& u) {
                const uint32_t mode = unitscan::Field32(u, unitscan::kOffMode);
                if (mode == 0 || mode == 12) return true;   // dying, dead
                float x, z;
                if (!unitscan::Place(read, u, &x, &z)) return true;
                if ((x - hx) * (x - hx) + (z - hz) * (z - hz) > 4.0f * kPickReachCells * kPickReachCells) return true;
                if (count < 64) all[count++] = {unitscan::Field32(u, unitscan::kOffId), x, z};
                return count < 64;
            });
            n = PickFront(hx, hz, fx, fz, g_cosHalf.load(std::memory_order_relaxed), 2.0f * kPickReachCells, all, count, ids, kCandidates);
        }
    } else {
        static std::atomic<int> told{0};
        if (told.fetch_add(1) < 3) Say("vrcam: attack path - the camera's forward is not known: every melee blow goes into the air until it is");
    }
    AcquireSRWLockExclusive(&g_frontLock);
    std::memcpy(g_front, ids, sizeof ids);
    g_frontN = n;
    ReleaseSRWLockExclusive(&g_frontLock);
    if (toHost) {   // a joined client: when it changed, and so that the host's copy stays fresh
        static uint32_t sent[kCandidates] = {};
        static size_t sentN = 0;
        static uint64_t sentAt = 0;
        constexpr uint64_t kAgainMs = 200, kLeastMs = 80;
        if ((now - sentAt >= kAgainMs || !SameFront(ids, n, sent, sentN)) && now - sentAt >= kLeastMs && g_net && g_channel != nw::InvalidChannelHandle && !g_netDead.load()) {
            FrontMsg m{};
            m.hero = g_heroId.load(std::memory_order_relaxed);
            m.n = (uint32_t)n;
            std::memcpy(m.ids, ids, sizeof m.ids);
            g_net->sendToHost(g_ctx, g_channel, kMsgFront, &m, sizeof m);
            std::memcpy(sent, ids, sizeof sent);
            sentN = n;
            sentAt = now;
        }
    }
}

// The channel: both ends. The host's answer to a hello is its key; a host without the tick says 0.
void __cdecl OnHost(const D2RL::PluginContext*, nw::ChannelHandle, nw::PeerHandle peer, uint16_t id, const void* data, uint32_t size, void*) noexcept {
    if (g_netDead.load()) return;
    const uint64_t now = GetTickCount64();
    const auto slot = [&](uint32_t hero) -> Peer* {   // (under the lock) a peer speaks for one hero; a new game's hero takes its place
        Peer* free = nullptr;
        for (Peer& p : g_peers) {
            if (p.peer == peer) {
                if (p.hero != hero) { p = {}; p.peer = peer; p.hero = hero; }
                return &p;
            }
            if (p.hero == hero && Fresh(now, p.helloMs, kPeerFreshMs)) return nullptr;   // another peer's, and alive
            if (!free && (!p.peer || !Fresh(now, p.helloMs, kPeerFreshMs))) free = &p;
        }
        if (free) { *free = {}; free->peer = peer; free->hero = hero; }
        return free;
    };
    if (id == kMsgHello) {
        HelloMsg m{};
        if (!ParseHello(data, size, &m)) return;
        AcquireSRWLockExclusive(&g_frontLock);
        Peer* p = g_key.load() > 0 ? slot(m.hero) : nullptr;
        if (p) p->helloMs = now;
        ReleaseSRWLockExclusive(&g_frontLock);
        const AllowMsg a{p ? (uint32_t)g_key.load() : 0u};
        if (g_net && g_ctx) g_net->sendToClient(g_ctx, g_channel, peer, kMsgAllow, &a, sizeof a);
        static std::atomic<int> told{0};
        if (told.fetch_add(1) < 4) Say("vrcam: attack path - host: hero %u spoke on the channel, answered %u", m.hero, a.key);
    } else if (id == kMsgFront) {
        FrontMsg m{};
        if (!ParseFront(data, size, &m)) return;
        AcquireSRWLockExclusive(&g_frontLock);
        for (Peer& p : g_peers)
            if (p.peer == peer && p.hero == m.hero) { std::memcpy(p.ids, m.ids, sizeof p.ids); p.n = m.n; p.frontMs = now; }
        ReleaseSRWLockExclusive(&g_frontLock);
    }
}
void __cdecl OnClient(const D2RL::PluginContext*, nw::ChannelHandle, uint16_t id, const void* data, uint32_t size, void*) noexcept {
    AllowMsg m{};
    if (g_netDead.load() || id != kMsgAllow || !ParseAllow(data, size, &m)) return;
    const int was = g_allowKey.exchange((int)m.key);
    g_allowMs.store(m.key ? GetTickCount64() : 0, std::memory_order_release);
    if (was != (int)m.key) Say("vrcam: attack path - joined: the host's key is %u (%s)", m.key, m.key ? "the host decides: the swing does not stop the hero" : "the host decides: not ticked, the hero stops as in the game");
}
void __cdecl OnConnection(const D2RL::PluginContext*, const nw::ConnectionEvent* ev, void*) noexcept {
    if (!ev || g_netDead.load()) return;
    if (ev->state == nw::ConnectionState::Disconnected || ev->state == nw::ConnectionState::Rejected) {
        g_allowMs.store(0, std::memory_order_release);
        AcquireSRWLockExclusive(&g_frontLock);
        for (Peer& p : g_peers) if (p.peer == ev->peer && p.peer) p = {};
        ReleaseSRWLockExclusive(&g_frontLock);
    }
    static std::atomic<int> told{0};
    if (told.fetch_add(1) < 12) Say("vrcam: attack path - the channel: state %u, reason %u", (unsigned)ev->state, (unsigned)ev->reason);
}

// A joined client: the connection, asked for again, and its hello once a second.
void JoinedTick(uint32_t heroId, uint64_t now) {
    if (!g_net || g_channel == nw::InvalidChannelHandle || g_netDead.load()) return;
    static uint64_t triedAt = 0, helloAt = 0;
    nw::ChannelInfo info{};
    info.structSize = nw::ChannelInfoSize;
    const bool got = g_net->getChannelInfo(g_ctx, g_channel, &info) == nw::Result::Success;
    if (!got || info.connectionState != nw::ConnectionState::Connected) {
        if (now - triedAt >= (got && info.connectionState == nw::ConnectionState::Connecting ? 10000u : 3000u)) {
            triedAt = now;
            g_net->connectToHost(g_ctx, g_channel);
        }
        return;
    }
    if (now - helloAt >= 1000) {
        helloAt = now;
        const HelloMsg m{heroId};
        g_net->sendToHost(g_ctx, g_channel, kMsgHello, &m, sizeof m);
    }
}

// Which game: online never; single player has the server here; in a TCP/IP game we host if the server's hook ran in this game.
struct GameKind { bool online = true, serverHere = false, single = false; };
GameKind KindOfGame() {
    GameKind g;
    uint32_t client = 1, launch = 1;
    const uintptr_t c = d2rsig::Addr(RVA_CLIENT_GAME_TYPE), l = d2rsig::Addr(RVA_LAUNCH_GAME_TYPE);
    if (!c || !l || !Read(&client, c, sizeof client) || !Read(&launch, l, sizeof launch)) return g;   // not read: as online, nothing happens
    g.online = launch == moveattack::kLaunchBnet;
    g.single = client == 0 && launch == moveattack::kLaunchSingle;
    g.serverHere = g.single || g_serverSeenMs.load(std::memory_order_relaxed) >= g_gameSinceMs.load(std::memory_order_relaxed);
    return g;
}

}  // namespace

void SetLogger(void (*log)(const char*)) { g_log = log; }
void SetForwardFn(bool (*fn)(float*, float*)) { g_forward.store(fn, std::memory_order_release); }
// Ticked, or in a TCP/IP game, where the host may have ticked it: the hooks then pass through until someone did.
bool Wanted() {
    if (g_key.load() > 0) return true;
    uint32_t client = 0, launch = 0;
    const uintptr_t c = d2rsig::Addr(RVA_CLIENT_GAME_TYPE), l = d2rsig::Addr(RVA_LAUNCH_GAME_TYPE);
    return c && l && Read(&client, c, sizeof client) && Read(&launch, l, sizeof launch) && launch != moveattack::kLaunchBnet && (client != 0 || launch == moveattack::kLaunchTcpIp);
}

void SetWanted(int key, float coneDeg) {
    key = key < 0 ? 0 : key > 2 ? 2 : key;
    const float deg = coneDeg < 5.0f ? 5.0f : coneDeg > 180.0f ? 180.0f : coneDeg;
    g_cosHalf.store(std::cos(deg * 0.0174532925f));
    if (g_key.exchange(key) != key && g_log)
        g_log(key == 0 ? "vrcam: attack path - [gameplay] flat_attack_path=0: as in the game"
                       : key == 1 ? "vrcam: attack path - [gameplay] flat_attack_path=1 (changes the game): the swing does not stop the hero, at a walk; a melee blow goes to who stands before the camera at its frame. F2 / F3; TCP/IP: the host ticks it too"
                                  : "vrcam: attack path - [gameplay] flat_attack_path=2 (changes the game): the swing does not stop the hero, at a walk or a run as the toggle says; a melee blow goes to who stands before the camera at its frame. F2 / F3; TCP/IP: the host ticks it too");
}

bool OnTick(uintptr_t game, uintptr_t unit, int code, int* ret) {
    if (code != kTickCode) return false;
    *ret = 1;
    uint32_t mode = 0, hero = 0, head[3] = {};
    int32_t now = 0;
    if (OurAttacker(game, unit, &mode, &hero)) {
        Attack* const at = AttackOf(hero);
        if (at->valid && at->unit == unit && Read(&now, game + kGameFrame, sizeof now) && StepOn(now, at->lastAsk)) {
            if (!ServerStep(game, unit)) { Fault("the server's step (0x491AC0)"); return true; }
            g_steps.fetch_add(1, std::memory_order_relaxed);
            if (!SetEvent(game, unit, now + 1)) { Fault("the event (0x48B720)"); return true; }
            if (SyncNow(now, at->lastSync)) {
                at->lastSync = now;
                if (const uintptr_t path = PathOf(unit)) TellOthers(game, unit, hero, path);
            }
            return true;
        }
        at->ticking = false;   // the next walk starts it again
    } else if (Read(head, unit, sizeof head) && head[0] == 0) {
        AttackOf(head[2])->ticking = false;
    }
    return true;
}

bool BeforeAction(uintptr_t game, uintptr_t unit, int code) {
    if (code != 1 && code != 2) return false;
    uint32_t mode = 0, hero = 0;
    if (!OurAttacker(game, unit, &mode, &hero)) return false;
    const uintptr_t path = PathOf(unit);
    if (!path) return false;
    Attack* const at = NoteAttack(game, unit, path, hero);
    if (!at->walked) return false;   // no walk in this attack: the path still has what was clicked, the blow is the game's own
    const uint32_t weapon = WeaponKind(unit);
    if (Ranged(mode, weapon)) {   // what it was started on
        uintptr_t target = 0;
        if (at->hasTarget) target = Lookup(game, at->targetType, at->targetId);
        if (!SetPoint(path, at->x, at->y) || !SetTarget(unit, target)) { Fault("the shot's target"); return false; }
        return true;
    }
    uint32_t ids[kCandidates] = {};
    size_t n = 0;
    AcquireSRWLockShared(&g_frontLock);
    if (hero == g_heroId.load(std::memory_order_relaxed)) {
        n = g_frontN;
        std::memcpy(ids, g_front, sizeof ids);
    } else {
        const uint64_t nowMs = GetTickCount64();
        for (const Peer& p : g_peers)
            if (p.hero == hero && Fresh(nowMs, p.frontMs, kFrontFreshMs)) { n = p.n; std::memcpy(ids, p.ids, sizeof ids); }
    }
    ReleaseSRWLockShared(&g_frontLock);
    if (at->hasTarget && at->targetType == 1) StartedFirst(ids, n, at->targetId);   // the monster clicked, while it still stands before the camera
    uintptr_t target = 0;
    uint32_t id = 0;
    for (size_t i = 0; i < n && !target; ++i) {
        const uintptr_t t = Lookup(game, 1, ids[i]);
        if (t && Hostile(game, unit, t)) { target = t; id = ids[i]; }
    }
    if (!SetTarget(unit, target)) { Fault("the blow's target (0x34E0C0)"); return false; }
    if (!target) g_air.fetch_add(1, std::memory_order_relaxed);
    if (g_blows.fetch_add(1) < 60) {
        if (target) Say("vrcam: attack path - blow %llu (hero %u, mode %u, weapon kind %u): at monster %u, started on %u; the game's reach and roll decide", (unsigned long long)g_blows.load(), hero, mode, weapon, id, at->hasTarget ? at->targetId : 0);
        else Say("vrcam: attack path - blow %llu (hero %u, mode %u, weapon kind %u): nobody hostile before the camera (%u looked at), into the air", (unsigned long long)g_blows.load(), hero, mode, weapon, (unsigned)n);
    }
    return true;
}

void AfterAction(uintptr_t game, uintptr_t unit) {
    (void)game;
    // A target left on the path would turn the walk towards it (0x3813DD), so it goes; the next action looks again.
    if (!SetTarget(unit, 0)) Fault("the blow's target (0x34E0C0)");
}

bool SwallowStop(void* unit, int type) {
    if (type != 6 || !unit || g_faulted.load(std::memory_order_relaxed) || !g_clientActs.load(std::memory_order_acquire) || (uintptr_t)unit != g_clientUnit.load(std::memory_order_relaxed)) return false;
    uint32_t mode = 0;
    return Read(&mode, (uintptr_t)unit + kUnitMode, sizeof mode) && PlainAttackMode(mode);
}

void AfterCtrl(uint32_t player, uintptr_t mgr) {
    const GameKind game = KindOfGame();
    if (g_state.load(std::memory_order_relaxed) != 1 || g_faulted.load(std::memory_order_relaxed) || game.online || (game.single && g_key.load(std::memory_order_relaxed) == 0)) {
        g_clientActs.store(false, std::memory_order_release);   // nothing to do here: off, and nobody else decides
        return;
    }
    uintptr_t unit = 0;
    if (!CallClientPlayer(player, &unit)) { Fault("the client's player (0x9A480)"); return; }
    uint32_t head[4] = {};
    if (!unit || !Read(head, unit, sizeof head) || head[0] != 0) return;
    if (g_clientUnit.exchange(unit, std::memory_order_relaxed) != unit) {   // a new game
        g_gameSinceMs.store(GetTickCount64(), std::memory_order_relaxed);
        g_allowMs.store(0, std::memory_order_relaxed);
    }
    g_heroId.store(head[kUnitId / 4], std::memory_order_relaxed);
    const uint32_t mode = head[kUnitMode / 4];
    const uintptr_t path = PathOf(unit);
    if (path && (mode == 2 || mode == 3 || mode == 6)) {   // the speed bonuses, as the game has them now
        int v = 0;
        if (Read(&v, path + kPathVelocity, sizeof v) && v > 0) g_percent.store(PercentOf(v, mode == 3), std::memory_order_relaxed);
    }
    const uint64_t now = GetTickCount64();
    if (!game.serverHere) JoinedTick(head[kUnitId / 4], now);
    const bool hostYes = !game.serverHere && Fresh(now, g_allowMs.load(std::memory_order_acquire), kPeerFreshMs);
    const int key = ClientKey(game.online, game.serverHere, g_key.load(std::memory_order_relaxed), hostYes, g_allowKey.load(std::memory_order_relaxed));
    g_clientKey.store(key, std::memory_order_relaxed);
    Ask a;
    a.key = key;
    a.flatView = moveattack::PathViewNow();
    a.menu = gamestate::MenuOpen();
    a.allowed = Allowed(game.online, game.serverHere, hostYes);
    a.mode = mode;
    a.skillFlags = PlainAttackMode(mode) ? SkillFlagsOf(unit) : 0;
    const bool acts = Acts(a);
    // Between attacks the server must not be told "no": the verdict stays with the view, the menu and the game until the mode says otherwise.
    g_clientActs.store(a.key > 0 && a.flatView && !a.menu && a.allowed, std::memory_order_release);
    if (!acts) return;
    ScanFront(unit, !game.serverHere);
    static uint64_t lastSend = 0;
    if (now - lastSend >= kSendMs) {
        float grid[2] = {}, threshold = 0.0f;
        uint8_t stamina = 0, forced = 0;
        const uintptr_t rec = mgr + player * kRecStride;
        uint16_t flags = 0;
        if (Read(grid, rec + kRecGrid, sizeof grid) && Read(&stamina, rec + kRecStamina, 1) && Read(&forced, rec + kRecForcedWalk, 1) &&
            Read(&threshold, mgr + kMgrRunThreshold, sizeof threshold) && grid[0] * grid[0] + grid[1] * grid[1] > 1e-8f) {
            if (!CallCmdFlags(&flags)) { Fault("the command flags (0x8AFD0)"); return; }
            const moveattack::Walk w = moveattack::MakeWalk(grid[0], grid[1], (flags >> 8) != 0, stamina != 0, forced != 0, threshold);
            if (w.ok) {
                lastSend = now;
                if (!CallWalkDir(unit, w.dir, w.mag, w.run && RunAllowed(g_clientKey.load(std::memory_order_relaxed)))) { Fault("the walk command (0xFEFE0)"); return; }
            }
        }
    }
    if (now - g_lastAskMs.load(std::memory_order_acquire) <= kStepMs && !ClientStep(unit)) Fault("the client's step (0xF9A00)");
    static uint64_t lastTell = 0;
    if (now - lastTell >= 3000) {
        lastTell = now;
        Say("vrcam: attack path - so far: %llu walks predicted, %llu taken by the server, %llu server steps, %llu blows, %llu into the air; speed %d%%",
            (unsigned long long)g_predicted.load(), (unsigned long long)g_asked.load(), (unsigned long long)g_steps.load(), (unsigned long long)g_blows.load(), (unsigned long long)g_air.load(), g_percent.load());
    }
}

void InitNet(const D2RL::PluginContext* ctx) {
    if (!ctx) return;
    g_ctx = ctx;
    if (ctx->QueryService(&g_net) != D2RL::ServiceQueryResult::Success || !D2RL::HasNetworkServiceField(g_net, D2RL::NetworkServiceRequiredSize)) {
        g_net = nullptr;
        ctx->LogInfo("vrcam: attack path - D2RLoader has no network service: single player and hosting only");
        return;
    }
    nw::ChannelRegistration reg{};
    reg.structSize = nw::ChannelRegistrationSize;
    reg.localChannelId = kChannel;
    reg.compatibilityToken = kToken;
    reg.hostMessage = &OnHost;
    reg.clientMessage = &OnClient;
    reg.connectionState = &OnConnection;
    if (g_net->registerChannel(ctx, &reg, &g_channel) != nw::Result::Success) {
        g_channel = nw::InvalidChannelHandle;
        ctx->LogInfo("vrcam: attack path - the channel is NOT registered: a joined player stops as in the game");
    }
}
void ShutdownNet() {
    g_netDead.store(true);
    if (g_net && g_ctx && g_channel != nw::InvalidChannelHandle) g_net->unregisterChannel(g_ctx, g_channel);
    g_channel = nw::InvalidChannelHandle;
}

void Install() {
    static ULONGLONG retryAt = 0;
    if (g_state.load() != 0 || GetTickCount64() < retryAt || !Wanted()) return;
    retryAt = GetTickCount64() + 2000;
    const uint64_t need[] = {RVA_CLIENT_OF, RVA_REASSIGN, RVA_MODE_BY_DIR, RVA_POINT_OF_DIR, RVA_SERVER_STEP, RVA_SET_EVENT, RVA_EVENT_FRAME, RVA_SET_TARGET, RVA_LOOKUP, RVA_RELATION, RVA_UNIT_WEAPON, RVA_PREDICT, RVA_DECODE_DIR,
                             RVA_CLIENT_STEP, RVA_AFTER_STEP, RVA_CLIENT_PLAYER, RVA_WALK_DIR, RVA_CMD_FLAGS, RVA_PATH_FLAG, RVA_PATH_X, RVA_PATH_Y, RVA_UNITS, RVA_CLIENT_GAME_TYPE, RVA_LAUNCH_GAME_TYPE,
                             RVA_PATH_OF, RVA_PATH_TARGET, RVA_PATH_TYPE, RVA_PATH_VECTOR, RVA_PATH_VELOCITY, RVA_PATH_MAKE, RVA_USED_SKILL, RVA_SKILL_FLAGS};
    for (const uint64_t r : need) if (!d2rsig::Addr(r)) return;
    if (!d2rsig::Check(RVA_MODE_BY_DIR, kSigModeByDir, sizeof kSigModeByDir) || !d2rsig::Check(RVA_PREDICT, kSigPredict, sizeof kSigPredict)) return;   // (its pages are decrypted as they run: tried again)
    const bool in = d2rsig::Hook(RVA_PREDICT, kSigPredict, sizeof kSigPredict, reinterpret_cast<void*>(&HookPredict), reinterpret_cast<void**>(&g_origPredict)) &&
                    d2rsig::Hook(RVA_MODE_BY_DIR, kSigModeByDir, sizeof kSigModeByDir, reinterpret_cast<void*>(&HookModeByDir), reinterpret_cast<void**>(&g_origModeByDir));
    g_state.store(in ? 1 : 2);
    if (g_log) g_log(in ? "vrcam: attack path - ready: the server's start by direction and the client's prediction are hooked" : "vrcam: attack path - the hooks could not go in: the hero stops as in the game");
}

}  // namespace attackpath
