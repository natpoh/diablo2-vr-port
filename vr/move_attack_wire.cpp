// move_attack_wire.cpp - [gameplay] move_while_attacking on the game: see move_attack.h for what and why, and
// docs/notes/move_while_attacking.md for what was read in the game's code.
//
// EXPERIMENTAL, the user's own opt-in (off by default): it changes how the game plays - in Diablo II the hero stands out the rest of an
// attack's animation; with this on he walks on as soon as the blow or the shot is out. The attack stays the game's from the press to the
// action frame: its command, its gate, its animation up to the blow, the hit roll, damage, mana, the arrow or javelin, and the attack rate
// (the next attack waits until the cut animation would have ended). What is changed, on the game's own server: its attack-mode gate lets the
// stick's walk in after the action frame instead of only at the animation's last frame. That is a server change, so it acts where the server
// runs in this process - single player, or a TCP/IP game we host, where it applies to every hero in the game (the author: the host decides).
// As a joined TCP/IP client our client sends nothing extra and predicts nothing: the host's server decides. Never a server call from a client.
//
// The game functions: the attack-mode gate 0x42D840, the general mode gate 0x42BFB0 and the frame trigger 0x42A940 (the server's, hooked:
// they pass through unless the option is on and the unit is a hero of the game), the controller's per-player update 0x14C540 (hooked: after
// the game's own update it may send the stick's walk), and, called: the unit's event frame 0x48B230 (a read), a unit's weapon 0x34B000 (the
// getter the server's own Attack uses), the walk command 0xFEFE0 (the command the controller sends for the stick), the command flags 0x8AFD0
// (a read), the client's player 0x9A480; read: the game's type (gnGameType 0x2A233F4, geGameType 0x32EF888) for the log's role. Every call
// and read behind an SEH guard: a fault turns the option off for the session.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "move_attack.h"
#include "move_attack_wire.h"
#include "sigscan.h"
#include "view_gate.h"

namespace gamestate { uint32_t WeaponType(); bool MenuOpen(); uint32_t KindOfItemClass(uint32_t classId); }

namespace moveattack {
namespace {

// Addresses in 3.3.93787 (the keys of game_sigs.h).
constexpr uint64_t RVA_GATE = 0x42D840, RVA_MODE_ALLOWED = 0x42BFB0, RVA_TRIGGER = 0x42A940, RVA_EVENT_FRAME = 0x48B230, RVA_UNIT_WEAPON = 0x34B000;
constexpr uint64_t RVA_CTRL_UPDATE = 0x14C540, RVA_WALK_DIR = 0xFEFE0, RVA_CMD_FLAGS = 0x8AFD0, RVA_CLIENT_PLAYER = 0x9A480;
constexpr uint64_t RVA_CLIENT_GAME_TYPE = 0x2A233F4, RVA_LAUNCH_GAME_TYPE = 0x32EF888;   // gnGameType, geGameType (move_attack.h RoleOf)
// mov [rsp+8],rbx; mov [rsp+10h],rbp; mov [rsp+18h],rsi (the two gates and the trigger begin alike)
const uint8_t kSigGate[15] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18};
const uint8_t kSigModeAllowed[15] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18};
const uint8_t kSigTrigger[15] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18};
// mov [rsp+10h],rbx; mov [rsp+18h],rbp; push rdi; push r14; push r15
const uint8_t kSigEventFrame[15] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x57, 0x41, 0x56, 0x41, 0x57};
// push rbx; push rbp; push rsi; push rdi; push r14; push r15; sub rsp,38h
const uint8_t kSigUnitWeapon[13] = {0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x38};
// mov rax,rsp; push rbp; push rbx; push rsi; push r12; lea rbp,[rax-58h]; sub rsp,138h
const uint8_t kSigCtrlUpdate[19] = {0x48, 0x8B, 0xC4, 0x55, 0x53, 0x56, 0x41, 0x54, 0x48, 0x8D, 0x68, 0xA8, 0x48, 0x81, 0xEC, 0x38, 0x01, 0x00, 0x00};
// push rbx; sub rsp,20h; mov rbx,rcx; movzx r8d,r9b
const uint8_t kSigWalkDir[13] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x45, 0x0F, 0xB6, 0xC1};
// push rbx; sub rsp,20h; xor eax,eax; mov ebx,1
const uint8_t kSigCmdFlags[13] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x33, 0xC0, 0xBB, 0x01, 0x00, 0x00, 0x00};
// mov [rsp+8],rbx; push rdi; sub rsp,20h; cmp ecx,8; jae ... (vr/belt_drop.cpp's proof of the same function)
const uint8_t kSigClientPlayer[15] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x83, 0xF9, 0x08, 0x0F, 0x83};

// The unit (client and server alike): +0 type (0 a player, 4 an item), +4 class (an item's Items row), +8 id, +0xC mode; a server unit's
// game at +0xD8. The server's game: +0x170 its frame.
constexpr uintptr_t kUnitType = 0x0, kUnitClass = 0x4, kUnitId = 0x8, kUnitMode = 0xC, kUnitGame = 0xD8, kGameFrame = 0x170;
// The controller manager's record of a player: manager + player * 0xB8 (docs/move_recon.md section 3).
constexpr uintptr_t kRecStride = 0xB8, kRecStamina = 0x1951, kRecForcedWalk = 0x1952, kRecGrid = 0x195C, kRecSkill = 0x1974, kMgrRunThreshold = 0x1F94;

using GateFn = int (*)(uintptr_t game, uintptr_t unit, int mode, uintptr_t target, int flag);
using TriggerFn = int (*)(uintptr_t game, uintptr_t unit, int code, int arg);
using EventFrameFn = int (*)(uintptr_t game, uintptr_t unit, int type, int sub);
using UnitWeaponFn = uintptr_t (*)(uintptr_t unit);
using CtrlUpdateFn = void (*)(uintptr_t mgr, uint32_t player, uint32_t dt);
using WalkDirFn = void (*)(uintptr_t unit, const float* dir, float mag, bool run);   // dir in rdx, mag in xmm2, run in r9b
using CmdFlagsFn = uint16_t (*)();
using ClientPlayerFn = uintptr_t (*)(uint32_t index);

void (*g_log)(const char*) = nullptr;
std::atomic<bool> g_on{false}, g_faulted{false};
std::atomic<int> g_state{0};   // 0 not tried, 1 in, 2 not possible
GateFn g_origGate = nullptr;
GateFn g_origModeAllowed = nullptr;
TriggerFn g_origTrigger = nullptr;
CtrlUpdateFn g_origCtrl = nullptr;
std::atomic<uint32_t> g_heroId{0};        // our hero's id, from the client's player (the same id on the server)
std::atomic<uint64_t> g_holdUntil{0};     // the rate of OUR hero: no attack of ours before this (our clock); a joined hero's is in g_heroes
std::atomic<uint64_t> g_ownSeenMs{0};     // when the server in this process last noted our hero's blow (our clock): our server is here
SRWLOCK g_lock = SRWLOCK_INIT;
Heroes g_heroes;                          // every hero's last blow and next-attack frame (the server's thread writes, the client's reads)
std::atomic<int> g_lines{0};
constexpr int kLogMax = 240;

void Say(const char* fmt, ...) {
    if (!g_log || g_lines.load(std::memory_order_relaxed) >= kLogMax) return;
    char b[320];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    if (g_lines.fetch_add(1) + 1 == kLogMax) { g_log(b); g_log("vrcam: move while attacking - 240 lines said, quiet from here on"); return; }
    g_log(b);
}

bool Read(void* d, uintptr_t s, size_t n) noexcept {
    if (s < 0x10000) return false;
    __try { memcpy(d, reinterpret_cast<const void*>(s), n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallEventFrame(uintptr_t game, uintptr_t unit, int32_t* out) noexcept {
    const uintptr_t fn = d2rsig::Addr(RVA_EVENT_FRAME);
    if (!fn) return false;
    __try { *out = reinterpret_cast<EventFrameFn>(fn)(game, unit, 1, 0); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallUnitWeapon(uintptr_t unit, uintptr_t* out) noexcept {
    const uintptr_t fn = d2rsig::Addr(RVA_UNIT_WEAPON);
    if (!fn) return false;
    __try { *out = reinterpret_cast<UnitWeaponFn>(fn)(unit); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallClientPlayer(uint32_t index, uintptr_t* out) noexcept {
    const uintptr_t fn = d2rsig::Addr(RVA_CLIENT_PLAYER);
    if (!fn) return false;
    __try { *out = reinterpret_cast<ClientPlayerFn>(fn)(index); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallCmdFlags(uint16_t* out) noexcept {
    const uintptr_t fn = d2rsig::Addr(RVA_CMD_FLAGS);
    if (!fn) return false;
    __try { *out = reinterpret_cast<CmdFlagsFn>(fn)(); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallWalkDir(uintptr_t unit, const float* dir, float mag, bool run) noexcept {
    const uintptr_t fn = d2rsig::Addr(RVA_WALK_DIR);
    if (!fn) return false;
    __try { reinterpret_cast<WalkDirFn>(fn)(unit, dir, mag, run); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void Fault(const char* where) {
    if (!g_faulted.exchange(true)) Say("vrcam: move while attacking - %s faulted: the option is OFF for this session (the game's own stop again)", where);
}
bool Live() { return g_on.load(std::memory_order_relaxed) && !g_faulted.load(std::memory_order_relaxed); }

Fired FiredOf(uint32_t id) {
    AcquireSRWLockShared(&g_lock);
    const Fired f = g_heroes.Find(id);
    ReleaseSRWLockShared(&g_lock);
    return f;
}

// A hero of this game on our server: a player unit whose game is this one. *own: it is ours.
bool ServerPlayer(uintptr_t game, uintptr_t unit, uint32_t* id, uint32_t* mode, bool* own) {
    uint32_t head[4] = {};
    uintptr_t back = 0;
    if (!Read(head, unit, sizeof head) || head[kUnitType / 4] != 0 || !head[kUnitId / 4]) return false;
    if (!Read(&back, unit + kUnitGame, sizeof back) || back != game) return false;
    *id = head[kUnitId / 4];
    *mode = head[kUnitMode / 4];
    *own = *id == g_heroId.load(std::memory_order_relaxed);
    return true;
}

// The kind of the weapon a server unit attacks with: the game's own getter (the one its Attack takes), the item's class, its Items row's type.
uint32_t ServerWeaponKind(uintptr_t unit) {
    uintptr_t item = 0;
    uint32_t head[2] = {};
    if (!CallUnitWeapon(unit, &item)) { Fault("a hero's weapon on the server (0x34B000)"); return 0; }
    if (!item || !Read(head, item, sizeof head) || head[kUnitType / 4] != 4) return 0;
    return gamestate::KindOfItemClass(head[kUnitClass / 4]);
}

// ---- the server ---------------------------------------------------------------------------------------------------------------------

// At the action frame of a hero's mode (before the game makes the action: the mode is still the attack's).
void NoteTrigger(uintptr_t game, uintptr_t unit, int code) {
    uint32_t id = 0, mode = 0;
    bool own = false;
    if (!ServerPlayer(game, unit, &id, &mode, &own) || !NoteWorth(code, mode)) return;
    int32_t end = 0, now = 0;
    if (!CallEventFrame(game, unit, &end)) { Fault("the unit's event frame (0x48B230)"); return; }
    if (!Read(&now, game + kGameFrame, sizeof now)) return;
    Fired f;
    f.valid = true;
    f.heroId = id;
    f.weapon = ServerWeaponKind(unit);
    f.mode = mode;
    f.atFrame = now;
    f.endFrame = end;
    f.atMs = GetTickCount64();
    AcquireSRWLockExclusive(&g_lock);
    g_heroes.Note(f);
    ReleaseSRWLockExclusive(&g_lock);
    if (own) g_ownSeenMs.store(f.atMs, std::memory_order_release);
    static std::atomic<int> told{0};
    if (told.fetch_add(1) < 12)
        Say("vrcam: move while attacking - %s blow is out (hero %u, mode %u, weapon kind %u, trigger %d, server frame %d): %d frame(s) of the animation left",
            own ? "our hero's" : "a joined hero's", id, mode, f.weapon, code, now, (int)FramesLeft(end, now));
}

int HookTrigger(uintptr_t game, uintptr_t unit, int code, int arg) {
    if (ActionCode(code) && Live()) NoteTrigger(game, unit, code);
    return g_origTrigger(game, unit, code, arg);
}

int Judge(uintptr_t game, uintptr_t unit, int newMode, uintptr_t target, int original) {
    GateAsk a;
    a.original = original;
    a.on = true;
    a.f4 = viewgate::F4();
    a.player = ServerPlayer(game, unit, &a.heroId, &a.mode, &a.own);
    a.newMode = (uint32_t)newMode;
    a.target = target != 0;
    if (a.player) {
        a.fired = FiredOf(a.heroId);
        if (!CallEventFrame(game, unit, &a.endFrame)) { Fault("the unit's event frame (0x48B230)"); return original; }
        Read(&a.nowFrame, game + kGameFrame, sizeof a.nowFrame);
    }
    const GateWhy why = JudgeGate(a);
    if (why == GateWhy::Let) {
        const int32_t left = FramesLeft(a.endFrame, a.nowFrame);
        if (a.own) {
            g_holdUntil.store(GetTickCount64() + (uint64_t)left * kFrameMs, std::memory_order_release);   // our pad holds our next attack
        } else {
            AcquireSRWLockExclusive(&g_lock);
            g_heroes.HoldUntil(a.heroId, a.endFrame);   // our server holds his
            ReleaseSRWLockExclusive(&g_lock);
        }
        static std::atomic<int> told{0};
        if (told.fetch_add(1) < 20)
            Say("vrcam: move while attacking - the walk cut %s attack's last %d frame(s) (hero %u, mode %u -> %d); the next attack waits %u ms, as the game would have",
                a.own ? "our hero's" : "a joined hero's", (int)left, a.heroId, a.mode, newMode, (unsigned)(left * kFrameMs));
        return 0;
    }
    if (a.player && (why == GateWhy::NotFired || why == GateWhy::Weapon || why == GateWhy::NotAttack)) {
        static std::atomic<int> told{0};
        if (told.fetch_add(1) < 8) Say("vrcam: move while attacking - the walk waits for the animation as in the game: %s (hero %u, mode %u)", GateWhyText(why), a.heroId, a.mode);
    }
    return original;
}

int HookGate(uintptr_t game, uintptr_t unit, int mode, uintptr_t target, int flag) {
    const int r = g_origGate(game, unit, mode, target, flag);
    if (r != kGateNotYet || !Live()) return r;
    return Judge(game, unit, mode, target, r);
}

// The general mode gate: a joined hero whose attack a walk cut starts his next attack only when that animation would have ended.
int HookModeAllowed(uintptr_t game, uintptr_t unit, int mode, uintptr_t target, int flag) {
    const int r = g_origModeAllowed(game, unit, mode, target, flag);
    if (r != 0 || !Live() || !StartsAttack((uint32_t)mode)) return r;
    uint32_t id = 0, cur = 0;
    bool own = false;
    if (!ServerPlayer(game, unit, &id, &cur, &own) || own) return r;
    int32_t frame = 0;
    Read(&frame, game + kGameFrame, sizeof frame);
    AcquireSRWLockShared(&g_lock);
    const bool waits = g_heroes.AttackWaits(id, frame);
    ReleaseSRWLockShared(&g_lock);
    if (!HoldAttack(true, true, own, r, (uint32_t)mode, waits)) return r;
    static std::atomic<int> told{0};
    if (told.fetch_add(1) < 12) Say("vrcam: move while attacking - a joined hero's attack waits for the end of his cut animation (hero %u, mode %u -> %d)", id, cur, mode);
    return 1;   // the game's own "no" for an attack that comes too early
}

// ---- the client ---------------------------------------------------------------------------------------------------------------------

struct Sent {
    uint64_t noteMs = 0;
    int32_t noteEnd = 0;
    uint32_t count = 0;
    uint64_t lastMs = 0;
};
Sent g_sent;   // the controller's thread only

// The role, for the log and for whether our client may send the early walk: decided once our server has noted our hero (single player or
// host), or - with none - 0.6 s after our hero's first attack of the game (someone else hosts, or online).
struct RoleState {
    uintptr_t game = 0;          // our client's player unit: new with every game
    uint64_t gameMs = 0;         // when it appeared
    uint64_t attackMs = 0;       // our hero's first attack mode of this game (our clock)
    Role said = Role::Unknown;
} g_role;   // the controller's thread only

Role JudgeRole(uintptr_t unit, uint32_t clientMode, uint64_t now) {
    if (unit != g_role.game) g_role = {unit, now, 0, Role::Unknown};
    if (!g_role.attackMs && AttackMode(clientMode)) g_role.attackMs = now;
    uint32_t client = 0, launch = 0;
    const uintptr_t c = d2rsig::Addr(RVA_CLIENT_GAME_TYPE), l = d2rsig::Addr(RVA_LAUNCH_GAME_TYPE);
    const bool read = c && l && Read(&client, c, sizeof client) && Read(&launch, l, sizeof launch);
    const bool serverHere = g_ownSeenMs.load(std::memory_order_acquire) >= g_role.gameMs;
    Role r = RoleOf(serverHere, read, client, launch);
    // without our server: not known until an attack of ours had the time to be noted by a server here
    if (!serverHere && (!g_role.attackMs || now - g_role.attackMs < 600)) r = Role::Unknown;
    if (r != Role::Unknown && r != g_role.said) {
        g_role.said = r;
        Say("vrcam: move while attacking - role: %s (game type %u, launch type %u%s)", RoleText(r), client, launch, read ? "" : ", not read");
    }
    return r;
}

// After the controller's own update of a player: the hero's id for the server's side and, while a cut attack plays, the stick's walk.
void AfterCtrl(uintptr_t mgr, uint32_t player) {
    uintptr_t unit = 0;
    if (!CallClientPlayer(player, &unit)) { Fault("the client's player (0x9A480)"); return; }
    uint32_t head[4] = {};
    if (!unit || !Read(head, unit, sizeof head) || head[kUnitType / 4] != 0) return;
    g_heroId.store(head[kUnitId / 4], std::memory_order_relaxed);
    const uintptr_t rec = mgr + player * kRecStride;
    NudgeAsk a;
    a.on = true;
    a.nowMs = GetTickCount64();
    a.clientMode = head[kUnitMode / 4];
    a.role = JudgeRole(unit, a.clientMode, a.nowMs);
    a.f4 = viewgate::F4();
    if (!a.f4 || !ClientMaySend(a.role) || g_state.load(std::memory_order_relaxed) != 1) return;   // a joined client: nothing extra is ever sent (nor
    // with the gate that lets the walk in missing - a walk the server dropped would leave our client's hero out of step)
    a.weapon = WeaponQualifies(gamestate::WeaponType());
    a.menu = gamestate::MenuOpen();
    a.heroId = head[kUnitId / 4];
    a.fired = FiredOf(a.heroId);
    float grid[2] = {};
    int32_t skill = -1;
    uint8_t stamina = 0, forced = 0;
    float threshold = 0.0f;
    if (!Read(grid, rec + kRecGrid, sizeof grid) || !Read(&skill, rec + kRecSkill, sizeof skill) || !Read(&stamina, rec + kRecStamina, 1) ||
        !Read(&forced, rec + kRecForcedWalk, 1) || !Read(&threshold, mgr + kMgrRunThreshold, sizeof threshold))
        return;
    a.skillHeld = skill != -1;
    a.stick = grid[0] * grid[0] + grid[1] * grid[1] > 1e-8f;
    if (a.fired.atMs != g_sent.noteMs || a.fired.endFrame != g_sent.noteEnd) g_sent = {a.fired.atMs, a.fired.endFrame, 0, 0};
    a.sentForThis = g_sent.count;
    a.lastSentMs = g_sent.lastMs;
    if (!Nudge(a)) return;
    uint16_t flags = 0;
    if (!CallCmdFlags(&flags)) { Fault("the command flags (0x8AFD0)"); return; }
    const Walk w = MakeWalk(grid[0], grid[1], (flags >> 8) != 0, stamina != 0, forced != 0, threshold);
    if (!w.ok) return;
    if (!CallWalkDir(unit, w.dir, w.mag, w.run)) { Fault("the walk command (0xFEFE0)"); return; }
    ++g_sent.count;
    g_sent.lastMs = a.nowMs;
    static int told = 0;
    if (++told <= 12)
        Say("vrcam: move while attacking - the stick's walk sent during the attack's rest (mode %u, %s, strength %.2f, try %u)", a.clientMode, w.run ? "run" : "walk",
            w.mag, g_sent.count);
    if (g_sent.count == kMaxSends) {
        static int toldMiss = 0;
        if (++toldMiss <= 6)
            Say("vrcam: move while attacking - %u walks sent and the client still shows the attack: either it only plays the animation out while the hero walks, "
                "or the server did not take them (see 'the walk cut' lines above)", kMaxSends);
    }
}

void HookCtrlUpdate(uintptr_t mgr, uint32_t player, uint32_t dt) {
    g_origCtrl(mgr, player, dt);
    if (Live()) AfterCtrl(mgr, player);
}

}  // namespace

void SetLogger(void (*log)(const char*)) { g_log = log; }

void SetWanted(bool on) {
    if (g_on.exchange(on) != on && g_log)
        g_log(on ? "vrcam: move while attacking - [gameplay] move_while_attacking=1 (EXPERIMENTAL): a spear, polearm, javelin, bow or crossbow - the hero walks on as soon "
                   "as the blow or the shot is out; the next attack still waits for the animation's end. Single player (VR first person), or a TCP/IP game we host: "
                   "for every hero in it. Joined to another's game: the host's setting applies"
                 : "vrcam: move while attacking - [gameplay] move_while_attacking=0: the hero stands out the attack's animation, as in the game");
}
bool Wanted() { return g_on.load(); }

void Install() {
    static ULONGLONG retryAt = 0;
    static unsigned tries = 0;
    if (!g_on.load() || g_state.load() != 0 || GetTickCount64() < retryAt) return;
    retryAt = GetTickCount64() + (tries < 30 ? 2000 : 30000);   // the server's code is decrypted as it runs: tried again
    if (!d2rsig::Addr(RVA_GATE) || !d2rsig::Addr(RVA_MODE_ALLOWED) || !d2rsig::Addr(RVA_TRIGGER) || !d2rsig::Addr(RVA_EVENT_FRAME) || !d2rsig::Addr(RVA_UNIT_WEAPON) ||
        !d2rsig::Addr(RVA_CTRL_UPDATE) || !d2rsig::Addr(RVA_WALK_DIR) || !d2rsig::Addr(RVA_CMD_FLAGS) || !d2rsig::Addr(RVA_CLIENT_PLAYER))
        return;   // not all found (yet)
    if (!d2rsig::Check(RVA_EVENT_FRAME, kSigEventFrame, sizeof kSigEventFrame) || !d2rsig::Check(RVA_UNIT_WEAPON, kSigUnitWeapon, sizeof kSigUnitWeapon) ||
        !d2rsig::Check(RVA_WALK_DIR, kSigWalkDir, sizeof kSigWalkDir) || !d2rsig::Check(RVA_CMD_FLAGS, kSigCmdFlags, sizeof kSigCmdFlags) ||
        !d2rsig::Check(RVA_CLIENT_PLAYER, kSigClientPlayer, sizeof kSigClientPlayer) || !d2rsig::Check(RVA_GATE, kSigGate, sizeof kSigGate) ||
        !d2rsig::Check(RVA_MODE_ALLOWED, kSigModeAllowed, sizeof kSigModeAllowed) || !d2rsig::Check(RVA_TRIGGER, kSigTrigger, sizeof kSigTrigger) ||
        !d2rsig::Check(RVA_CTRL_UPDATE, kSigCtrlUpdate, sizeof kSigCtrlUpdate)) {
        if (++tries == 30 && g_log) g_log("vrcam: move while attacking - the game's code is not as expected (another build?): the hero stops as in the game");
        return;
    }
    // The trigger and the controller first: they only note and send. Then the general gate (it only holds a joined hero's attack back); the
    // attack-mode gate, which lets the walks in, last and only with all the others in - a cut is never let in without its rate kept.
    const bool tIn = d2rsig::Hook(RVA_TRIGGER, kSigTrigger, sizeof kSigTrigger, reinterpret_cast<void*>(&HookTrigger), reinterpret_cast<void**>(&g_origTrigger));
    const bool cIn = tIn && d2rsig::Hook(RVA_CTRL_UPDATE, kSigCtrlUpdate, sizeof kSigCtrlUpdate, reinterpret_cast<void*>(&HookCtrlUpdate), reinterpret_cast<void**>(&g_origCtrl));
    const bool mIn = cIn && d2rsig::Hook(RVA_MODE_ALLOWED, kSigModeAllowed, sizeof kSigModeAllowed, reinterpret_cast<void*>(&HookModeAllowed), reinterpret_cast<void**>(&g_origModeAllowed));
    const bool gIn = mIn && d2rsig::Hook(RVA_GATE, kSigGate, sizeof kSigGate, reinterpret_cast<void*>(&HookGate), reinterpret_cast<void**>(&g_origGate));
    g_state.store(gIn ? 1 : 2);
    if (g_log)
        g_log(gIn ? "vrcam: move while attacking - ready: the server's attack gate, mode gate and frame trigger and the controller's update are hooked (they act for the heroes "
                    "of a game whose server runs here - single player or a game we host - with the option on)"
                  : "vrcam: move while attacking - the hooks could not all be put in: the hero stops as in the game");
}

PadOut Pad(uint64_t nowMs, uint16_t attackBits) {
    static RateHold hold;   // the pad poll's thread only
    if (!Live() || !viewgate::F4()) {
        hold.Reset();
        return {};
    }
    hold.Arm(g_holdUntil.load(std::memory_order_acquire));
    return hold.Step(nowMs, attackBits);
}

}  // namespace moveattack
