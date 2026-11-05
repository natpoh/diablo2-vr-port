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
//
// The flat key ([gameplay] flat_move_while_attacking, our hero in the monitor's F2 / F3 with W A S D, or VR F1 / F2 / F3 with the left stick):
// the same hooks, its own key. Its rate is
// kept on the mouse: vrcam's map click hook (vr/input/flat_click.cpp) asks HoldClick for every click, and a press held back in the hold is
// made again here, after the controller's update, through the game's own map click 0xFE3B0 (its hooked entry, so the plugin's own click
// rules apply to it as to any click).
//
// [gameplay] quick_blow (vr/quick_blow.h, its own tick, EXPERIMENTAL, off by default) shares these hooks and adds one: the server's scheduler
// of a mode's frame events 0x491240, which for a quick attack is handed - for that call only - a copy of the AnimData record with the blow
// moved to the first frame. Around a quick attack's blow the frame trigger opens melee_direct's reach window (the attack's target is in
// reach, the game's wall test stays), and with [gameplay] quick_blow_faster the attack-mode gate lets the next attack in once the blow is
// out and quick_blow_min_ms passed. The walk through the swing is vr/attack_path_wire.cpp's.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "attack_path_wire.h"
#include "melee_direct.h"
#include "move_attack.h"
#include "move_attack_wire.h"
#include "quick_blow.h"
#include "sigscan.h"
#include "view_gate.h"

namespace gamestate { uint32_t WeaponType(); bool MenuOpen(); uint32_t KindOfItemClass(uint32_t classId); }

namespace moveattack {
namespace {

// Addresses in 3.3.93787 (the keys of game_sigs.h).
constexpr uint64_t RVA_GATE = 0x42D840, RVA_MODE_ALLOWED = 0x42BFB0, RVA_TRIGGER = 0x42A940, RVA_EVENT_FRAME = 0x48B230, RVA_UNIT_WEAPON = 0x34B000;
constexpr uint64_t RVA_CTRL_UPDATE = 0x14C540, RVA_WALK_DIR = 0xFEFE0, RVA_CMD_FLAGS = 0x8AFD0, RVA_CLIENT_PLAYER = 0x9A480;
constexpr uint64_t RVA_CLIENT_GAME_TYPE = 0x2A233F4, RVA_LAUNCH_GAME_TYPE = 0x32EF888;   // gnGameType, geGameType (move_attack.h RoleOf)
constexpr uint64_t RVA_MAP_CLICK = 0xFE3B0;   // the game's map click (hooked by vrcam: its entry leads through the plugin's click rules)
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
// [gameplay] quick_blow: the scheduler of a unit's frame events at a mode's start (game, unit) - push rbx; push rsi; push r13; push r14;
// sub rsp,78h; mov r14d,[rcx+170h]
constexpr uint64_t RVA_FRAME_EVENTS = 0x491240;
const uint8_t kSigFrameEvents[18] = {0x40, 0x53, 0x56, 0x41, 0x55, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x78, 0x44, 0x8B, 0xB1, 0x70, 0x01, 0x00, 0x00};

// The unit (client and server alike): +0 type (0 a player, 4 an item), +4 class (an item's Items row), +8 id, +0xC mode; a server unit's
// game at +0xD8. The server's game: +0x170 its frame.
constexpr uintptr_t kUnitType = 0x0, kUnitClass = 0x4, kUnitId = 0x8, kUnitMode = 0xC, kUnitGame = 0xD8, kGameFrame = 0x170;
// A server unit's sequence (+0x40: Zeal, Fury - its events are the sequence's) and its AnimData record (+0x70, the getter 0x349480).
constexpr uintptr_t kUnitSequence = 0x40, kUnitAnimData = 0x70;
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
using MapClickFn = uint64_t (*)(void* unit, int type, int x, int y, uint64_t flags);
using FrameEventsFn = uint64_t (*)(uintptr_t game, uintptr_t unit);

void (*g_log)(const char*) = nullptr;
std::atomic<bool> g_on{false}, g_faulted{false};
std::atomic<bool> g_quick{false}, g_faster{false};   // [gameplay] quick_blow, quick_blow_faster (vr/quick_blow.h)
std::atomic<uint64_t> g_tallyOwn{0}, g_tallyJoined{0}, g_tallyReach{0};   // [gameplay] quick_blow: the session's counts (QuickTally)
std::atomic<uint32_t> g_quickMinMs{quickblow::kMinMsDefault};   // [gameplay] quick_blow_min_ms
std::atomic<int> g_quickState{0};          // the scheduler's hook: 0 not tried, 1 in, 2 not possible
FrameEventsFn g_origFrameEvents = nullptr;
quickblow::QuickNotes g_quickNotes;        // the server's thread only (the scheduler notes, the trigger and the gate ask)
std::atomic<uint8_t> g_flatScope{kScopeNone};   // [gameplay] flat_move_while_attacking (ScopeOfKey)
std::atomic<int (*)()> g_flatViewFn{nullptr};      // vrcam: the monitor game's view (1 F1, 2 F2, 3 F3; 0 in VR) - for the log
std::atomic<bool (*)()> g_flatCameraFn{nullptr};   // vrcam: one of the flat key's views (the monitor's F2 / F3, VR F1 / F2 / F3)
ClickHold g_clicks;                       // the flat game's mouse in the hold (the game's thread: its click and its controller update)
SRWLOCK g_clickLock = SRWLOCK_INIT;
uintptr_t g_clickUnit = 0;                // the unit a held-back press was made for (under g_clickLock)
std::atomic<int> g_state{0};   // 0 not tried, 1 in, 2 not possible
GateFn g_origGate = nullptr;
GateFn g_origModeAllowed = nullptr;
TriggerFn g_origTrigger = nullptr;
CtrlUpdateFn g_origCtrl = nullptr;
std::atomic<uint32_t> g_heroId{0};        // our hero's id, from the client's player (the same id on the server)
std::atomic<uint64_t> g_holdUntil{0};     // the rate of OUR hero: no attack of ours before this (our clock); a joined hero's is in g_heroes
std::atomic<uint32_t> g_ownCuts{0};      // walks our server let in for our hero (the per-attack log)
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
bool Live() {
    return (g_on.load(std::memory_order_relaxed) || g_flatScope.load(std::memory_order_relaxed) != kScopeNone || g_quick.load(std::memory_order_relaxed)) &&
           !g_faulted.load(std::memory_order_relaxed);
}
bool QuickLive() { return g_quick.load(std::memory_order_relaxed) && !g_faulted.load(std::memory_order_relaxed); }
bool FlatCamera() {
    const auto fn = g_flatCameraFn.load(std::memory_order_acquire);
    return fn && fn();
}
// Our own hero's scope in the view he is in now (move_attack.h OwnScope).
uint8_t OwnScopeNow() { return OwnScope(g_on.load(std::memory_order_relaxed), viewgate::F4(), FlatCamera(), g_flatScope.load(std::memory_order_relaxed)); }
bool CallMapClick(uintptr_t unit, const Click& c) noexcept {
    const uintptr_t fn = d2rsig::Addr(RVA_MAP_CLICK);
    if (!fn) return false;
    __try { reinterpret_cast<MapClickFn>(fn)(reinterpret_cast<void*>(unit), c.type, c.x, c.y, c.flags); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

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

// [gameplay] quick_blow: is the attack this hero is in a quick one (its frames moved by the scheduler's hook)?
bool QuickAttackOf(uintptr_t game, uintptr_t unit, uint32_t* hero) {
    uint32_t id = 0, mode = 0;
    bool own = false;
    int32_t end = 0;
    if (!ServerPlayer(game, unit, &id, &mode, &own) || !quickblow::QuickMode(mode)) return false;
    if (!CallEventFrame(game, unit, &end)) { Fault("the unit's event frame (0x48B230)"); return false; }
    *hero = id;
    return g_quickNotes.Is(id, end);
}

int HookTrigger(uintptr_t game, uintptr_t unit, int code, int arg) {
    int own = 0;
    if (attackpath::OnTick(game, unit, code, &own)) return own;   // [gameplay] flat_attack_path: its step of the path, never the game's handler
    const bool blow = attackpath::BeforeAction(game, unit, code);
    if (ActionCode(code) && Live()) NoteTrigger(game, unit, code);
    // [gameplay] quick_blow: a quick attack's blow - melee_direct's reach window for the hero and the attack's own target, around the game's action
    uint32_t quickHero = 0;
    const bool reach = ActionCode(code) && QuickLive() && QuickAttackOf(game, unit, &quickHero) && meleedirect::QuickReachBegin(game, unit);
    const int r = g_origTrigger(game, unit, code, arg);
    if (reach) {
        meleedirect::QuickReachEnd();
        g_tallyReach.fetch_add(1, std::memory_order_relaxed);
        static std::atomic<int> told{0};
        if (told.fetch_add(1) < 8) Say("vrcam: quick blow - hero %u's blow: the attack's target counted in reach (the game's wall test, roll and damage)", quickHero);
    }
    if (blow) attackpath::AfterAction(game, unit);
    return r;
}

// [gameplay] quick_blow: the scheduler of the frame events at an attack's start - a quick attack's record with its blow on the first frame.
bool WritePtr(uintptr_t at, uintptr_t v) noexcept {
    if (at < 0x10000) return false;
    __try { *reinterpret_cast<uintptr_t*>(at) = v; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
uint64_t HookFrameEvents(uintptr_t game, uintptr_t unit) {
    if (!QuickLive()) return g_origFrameEvents(game, unit);
    quickblow::Ask a;
    a.on = true;
    uint32_t id = 0;
    a.player = ServerPlayer(game, unit, &id, &a.mode, &a.own);
    if (!a.player || !quickblow::QuickMode(a.mode)) return g_origFrameEvents(game, unit);   // (monsters, walks, casts: at once)
    a.f4 = viewgate::F4();
    uintptr_t seq = 0, rec = 0;
    a.sequence = Read(&seq, unit + kUnitSequence, sizeof seq) && seq != 0;
    a.weapon = ServerWeaponKind(unit);
    alignas(16) uint8_t record[quickblow::kRecordBytes], moved[quickblow::kRecordBytes];
    a.record = Read(&rec, unit + kUnitAnimData, sizeof rec) && rec && Read(record, rec, sizeof record);
    const quickblow::Why why = quickblow::Judge(a);
    if (why != quickblow::Why::Shift) {
        static std::atomic<int> told{0};
        if (told.fetch_add(1) < 8) Say("vrcam: quick blow - hero %u, mode %u: the game's own frames (%s)", id, a.mode, quickblow::WhyText(why));
        return g_origFrameEvents(game, unit);
    }
    const int shift = quickblow::ShiftEvents(record, moved);
    uint64_t r = 0;
    // the record is the game's shared row: the hero's pointer goes to our copy for this one call, and back
    if (shift > 0 && WritePtr(unit + kUnitAnimData, reinterpret_cast<uintptr_t>(moved))) {
        r = g_origFrameEvents(game, unit);
        if (!WritePtr(unit + kUnitAnimData, rec)) Fault("putting the hero's AnimData record back");
    } else {
        r = g_origFrameEvents(game, unit);
    }
    int32_t end = 0;
    if (shift >= 0 && CallEventFrame(game, unit, &end)) g_quickNotes.Note(id, end);
    if (shift > 0) (a.own ? g_tallyOwn : g_tallyJoined).fetch_add(1, std::memory_order_relaxed);
    static std::atomic<int> told{0};
    if (told.fetch_add(1) < 16) {
        if (shift > 0)
            Say("vrcam: quick blow - %s hero %u, mode %u, weapon kind %u: the blow moved from frame %d to the first (the animation's end, server frame %d, is the game's)",
                a.own ? "our" : "a joined", id, a.mode, a.weapon, shift, end);
        else
            Say("vrcam: quick blow - hero %u, mode %u: %s", id, a.mode, shift == 0 ? "the blow is on the first frame already" : "no blow in this animation's frames - the game's own");
    }
    return r;
}

int Judge(uintptr_t game, uintptr_t unit, int newMode, uintptr_t target, int original) {
    GateAsk a;
    a.original = original;
    a.on = g_on.load(std::memory_order_relaxed);
    a.f4 = viewgate::F4();
    a.flatCamera = FlatCamera();
    a.flatScope = g_flatScope.load(std::memory_order_relaxed);
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
            g_ownCuts.fetch_add(1, std::memory_order_relaxed);
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

// [gameplay] quick_blow_faster: a quick attack whose blow is out lets the next attack in once quick_blow_min_ms passed (the animation cut).
bool Faster(uintptr_t game, uintptr_t unit, int newMode, uintptr_t target, int original) {
    quickblow::FasterAsk a;
    a.on = true;
    a.original = original;
    a.newMode = (uint32_t)newMode;
    a.target = target != 0;
    a.minMs = g_quickMinMs.load(std::memory_order_relaxed);
    a.f4 = viewgate::F4();
    uint32_t id = 0;
    a.player = ServerPlayer(game, unit, &id, &a.mode, &a.own);
    if (!a.player || !quickblow::QuickMode(a.mode)) return false;
    if (!CallEventFrame(game, unit, &a.endFrame)) { Fault("the unit's event frame (0x48B230)"); return false; }
    Read(&a.nowFrame, game + kGameFrame, sizeof a.nowFrame);
    a.quickAttack = g_quickNotes.Is(id, a.endFrame);
    const Fired f = FiredOf(id);
    a.blowOut = f.valid && f.heroId == id && f.mode == a.mode && f.endFrame == a.endFrame;
    a.blowFrame = f.atFrame;
    const quickblow::FastWhy why = quickblow::JudgeFaster(a);
    if (why == quickblow::FastWhy::Let) {
        static std::atomic<int> told{0};
        if (told.fetch_add(1) < 12)
            Say("vrcam: quick blow - faster: %s hero %u's next attack let in %d frame(s) after the blow, %d frame(s) of the animation cut (the least wait %u ms)",
                a.own ? "our" : "a joined", id, (int)(a.nowFrame - a.blowFrame), (int)(a.endFrame - a.nowFrame), a.minMs);
        return true;
    }
    if (why != quickblow::FastWhy::Game) {
        static std::atomic<int> told{0};
        if (told.fetch_add(1) < 8) Say("vrcam: quick blow - faster: hero %u's next attack waits as in the game (%s)", id, quickblow::FastWhyText(why));
    }
    return false;
}

int HookGate(uintptr_t game, uintptr_t unit, int mode, uintptr_t target, int flag) {
    const int r = g_origGate(game, unit, mode, target, flag);
    if (target && (r == quickblow::kGateNotYet || r == quickblow::kGateRefused) && QuickLive() && g_faster.load(std::memory_order_relaxed) &&
        Faster(game, unit, mode, target, r))
        return quickblow::kGateIn;
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
AttackLog g_story;          // the per-attack log (the controller's thread)
uint32_t g_stories = 0;
constexpr uint32_t kStoriesMax = 40;

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

// The flat game's mouse: a press held back in the hold whose button was let go before its end is made now, through the game's own click.
void ReplayClick(uintptr_t unit, uint64_t now) {
    AcquireSRWLockExclusive(&g_clickLock);
    if (!g_clicks.Pending()) { ReleaseSRWLockExclusive(&g_clickLock); return; }
    const Click c = g_clicks.Due(now, g_holdUntil.load(std::memory_order_acquire));
    const uintptr_t was = g_clickUnit;
    ReleaseSRWLockExclusive(&g_clickLock);
    if (!c.valid || was != unit) return;   // nothing due yet, or another hero now (a new game)
    if (!CallMapClick(unit, c)) { Fault("the map click (0xFE3B0)"); return; }
    static int told = 0;
    if (++told <= 12) Say("vrcam: move while attacking - flat: the %s click pressed in the cut animation's rest is made now, as the game would have taken it", c.type == 0 ? "left" : "right");
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
    a.on = g_on.load(std::memory_order_relaxed);
    a.nowMs = GetTickCount64();
    a.clientMode = head[kUnitMode / 4];
    a.role = JudgeRole(unit, a.clientMode, a.nowMs);
    a.f4 = viewgate::F4();
    ReplayClick(unit, a.nowMs);   // the flat key's views: a mouse press held back in the hold, once the hold is over
    const bool flatViews = !a.f4 && FlatCamera();
    const uint8_t flatScope = g_flatScope.load(std::memory_order_relaxed);
    const uint32_t weapon = gamestate::WeaponType();
    a.weapon = WeaponQualifies(weapon);
    a.flatOn = flatViews && flatScope != kScopeNone;
    a.flatWeapon = a.flatOn && WeaponIn(flatScope, weapon);
    a.menu = gamestate::MenuOpen();
    a.heroId = head[kUnitId / 4];
    a.fired = FiredOf(a.heroId);
    float grid[2] = {};
    int32_t skill = -1;
    uint8_t stamina = 0, forced = 0;
    float threshold = 0.0f;
    const bool recRead = Read(grid, rec + kRecGrid, sizeof grid) && Read(&skill, rec + kRecSkill, sizeof skill) && Read(&stamina, rec + kRecStamina, 1) &&
                         Read(&forced, rec + kRecForcedWalk, 1) && Read(&threshold, mgr + kMgrRunThreshold, sizeof threshold);
    a.skillHeld = skill != -1;
    a.stick = grid[0] * grid[0] + grid[1] * grid[1] > 1e-8f;
    if (a.fired.atMs != g_sent.noteMs || a.fired.endFrame != g_sent.noteEnd) g_sent = {a.fired.atMs, a.fired.endFrame, 0, 0};
    a.sentForThis = g_sent.count;
    a.lastSentMs = g_sent.lastMs;
    // the per-attack log, and when the client's hero began this attack: only this attack's own blow lets a walk out
    const uint32_t cutsNow = g_ownCuts.load(std::memory_order_relaxed);
    AttackStory st;
    const bool ended = g_story.Begin(a.clientMode, cutsNow, a.nowMs, &st);
    a.attackSinceMs = g_story.Since();
    // (a joined client: nothing extra is ever sent - Role; nor with the gate that lets the walk in missing: a walk the server dropped would leave
    // our client's hero out of step)
    NudgeWhy why = recRead ? JudgeNudge(a) : NudgeWhy::NotAttack;
    bool sentNow = false;
    const bool hooksIn = g_state.load(std::memory_order_relaxed) == 1;
    if (why == NudgeWhy::Send && !hooksIn) why = NudgeWhy::Off;
    if (why == NudgeWhy::Send) {
        uint16_t flags = 0;
        if (!CallCmdFlags(&flags)) { Fault("the command flags (0x8AFD0)"); return; }
        const Walk w = MakeWalk(grid[0], grid[1], (flags >> 8) != 0, stamina != 0, forced != 0, threshold);
        if (w.ok) {
            const bool flatPath = !(a.on && a.f4 && a.weapon);
            // the flat key: the rate hold from now (the pad's attack button off at the next poll, the mouse's clicks at once) - with the button
            // held the game's controller would repeat the attack as soon as the hero walks; the server's answer below arms it as well
            if (flatPath) {
                const uint64_t until = NoteEndMs(a.fired);
                uint64_t was = g_holdUntil.load(std::memory_order_relaxed);
                while (until > was && !g_holdUntil.compare_exchange_weak(was, until)) {}
            }
            if (!CallWalkDir(unit, w.dir, w.mag, w.run)) { Fault("the walk command (0xFEFE0)"); return; }
            ++g_sent.count;
            sentNow = true;
            g_sent.lastMs = a.nowMs;
            static int told = 0;
            if (++told <= 12)
                Say("vrcam: move while attacking - the %s walk sent during the attack's rest (mode %u, %s, strength %.2f, grid %.2f %.2f, attack button %s, try %u)",
                    flatPath ? "flat key's" : "stick's", a.clientMode, w.run ? "run" : "walk", w.mag, grid[0], grid[1], a.skillHeld ? "held" : "free", g_sent.count);
            if (g_sent.count == kMaxSends) {
                static int toldMiss = 0;
                if (++toldMiss <= 6)
                    Say("vrcam: move while attacking - %u walks sent and the client still shows the attack: either it only plays the animation out while the hero walks, "
                        "or the server did not take them (see 'the walk cut' lines above)", kMaxSends);
            }
        }
    }
    g_story.Note(why, sentNow, cutsNow, a.fired.valid && a.fired.heroId == a.heroId ? a.fired.atMs : 0);
    // one line an attack of ours, the first kStoriesMax of a session with the flat key on: walked on, or why not; and whether its blow came
    if (ended && flatScope != kScopeNone && ++g_stories <= kStoriesMax) {   // (only with the flat key on: the F4 box is on for everyone, and a line costs)
        const int vr = viewgate::Number();
        char view[40];
        if (a.f4) snprintf(view, sizeof view, "VR F4");
        else if (vr > 0) snprintf(view, sizeof view, "VR F%d", vr);
        else {
            const auto fv = g_flatViewFn.load(std::memory_order_acquire);
            const int n = fv ? fv() : 0;
            if (n > 0) snprintf(view, sizeof view, "monitor F%d%s", n, flatViews ? "" : ", no camera");
            else snprintf(view, sizeof view, flatViews ? "monitor, our camera" : "monitor, no camera");
        }
        const bool serverHere = a.role == Role::Single || a.role == Role::Host;
        Say("vrcam: move while attacking - attack %u (%s, mode %u, weapon kind %u, %llu ms on the client): %s; blow noted by our server: %s; walks sent %u, let in by "
            "our server %u (keys: F4 %d, flat %u; hooks %s; role %s)",
            g_stories, view, st.mode, weapon, (unsigned long long)st.ms, StoryText(st),
            st.noted ? "yes" : serverHere ? "NO - the attack ended before its blow" : "no (no server here)", st.sent, st.cuts, a.on ? 1 : 0, (unsigned)flatScope,
            hooksIn ? "in" : "NOT in", RoleText(a.role));
        if (serverHere && !st.noted && st.sent) Say("vrcam: move while attacking - a walk of ours ended attack %u before its blow: a bug, please send this log", g_stories);
    }
}

void HookCtrlUpdate(uintptr_t mgr, uint32_t player, uint32_t dt) {
    g_origCtrl(mgr, player, dt);
    attackpath::AfterCtrl(player, mgr);
    if (Live()) AfterCtrl(mgr, player);
}

}  // namespace

void SetLogger(void (*log)(const char*)) { g_log = log; }

void SetFlatCameraFn(bool (*fn)()) { g_flatCameraFn.store(fn, std::memory_order_release); }
void SetFlatViewFn(int (*fn)()) { g_flatViewFn.store(fn, std::memory_order_release); }

void SetFlatWanted(int key) {
    const uint8_t s = ScopeOfKey(key);
    if (g_flatScope.exchange(s) != s && g_log)
        g_log(s == kScopeNone ? "vrcam: move while attacking - flat: [gameplay] flat_move_while_attacking=0: in the flat game the hero stands out the attack's animation, as in the game"
              : s == kScopeReach ? "vrcam: move while attacking - flat: [gameplay] flat_move_while_attacking=1 (changes the game): in the monitor's F2 / F3 and VR F1 / F2 / F3 with a spear, polearm, javelin, "
                                   "bow or crossbow W A S D / the left stick walk on as soon as the blow or the shot is out; the next attack goes when the animation would have ended"
                                 : "vrcam: move while attacking - flat: [gameplay] flat_move_while_attacking=2 (changes the game): in the monitor's F2 / F3 and VR F1 / F2 / F3 with any weapon W A S D / the left stick walk on as soon "
                                   "as the blow or the shot is out; the next attack goes when the animation would have ended");
}

bool PathViewNow() { return !viewgate::F4() && FlatCamera(); }

bool HoldClick(void* unit, int type, int x, int y, uint64_t flags) {
    if (attackpath::SwallowStop(unit, type)) return true;   // [gameplay] flat_attack_path: the stick's stop would end the attack on the client
    if (!unit || !MouseAttackClick(type)) return false;
    const bool mine = Live() && g_state.load(std::memory_order_relaxed) == 1 && !viewgate::F4() && FlatCamera() &&
                      g_flatScope.load(std::memory_order_relaxed) != kScopeNone;
    const uint64_t now = GetTickCount64(), until = g_holdUntil.load(std::memory_order_acquire);
    AcquireSRWLockExclusive(&g_clickLock);
    if (!mine) {
        g_clicks.Reset();
        ReleaseSRWLockExclusive(&g_clickLock);
        return false;
    }
    const bool held = g_clicks.Take(now, until, type, x, y, flags);
    if (held) g_clickUnit = (uintptr_t)unit;
    ReleaseSRWLockExclusive(&g_clickLock);
    if (held) {
        static std::atomic<int> told{0};
        if (MousePress(type) && told.fetch_add(1) < 12)
            Say("vrcam: move while attacking - flat: a %s click in the cut animation's rest waits %u ms for the game's attack rate", type == 0 ? "left" : "right",
                (unsigned)(until - now));
    }
    return held;
}

void SetWanted(bool on) {
    if (g_on.exchange(on) != on && g_log)
        g_log(on ? "vrcam: move while attacking - [gameplay] move_while_attacking=1 (EXPERIMENTAL): a spear, polearm, javelin, bow or crossbow - the hero walks on as soon "
                   "as the blow or the shot is out; the next attack still waits for the animation's end. Single player (VR first person), or a TCP/IP game we host: "
                   "for every hero in it. Joined to another's game: the host's setting applies"
                 : "vrcam: move while attacking - [gameplay] move_while_attacking=0: the hero stands out the attack's animation, as in the game");
}
bool Wanted() { return g_on.load(); }

void SetQuick(bool on, bool faster, int minMs) {
    const uint32_t ms = quickblow::ClampMinMs(minMs);
    const bool was = g_quick.exchange(on), wasFaster = g_faster.exchange(faster);
    const uint32_t wasMs = g_quickMinMs.exchange(ms);
    meleedirect::SetQuickWanted(on);
    if ((was != on || wasFaster != faster || wasMs != ms) && g_log) {
        char b[420];
        if (!on) snprintf(b, sizeof b, "vrcam: quick blow - [gameplay] quick_blow=0: the blow on the game's frame, the swing stops the hero, the touch as before");
        else
            snprintf(b, sizeof b, "vrcam: quick blow - [gameplay] quick_blow=1 (EXPERIMENTAL, changes the game): VR F4 - the blow on the first frame, the hero walks through "
                     "the swing, the touch is the game's attack (By the rules' cleave extras stay direct), the attack's target counted in reach; faster than the "
                     "animation: %s (the least wait %u ms). A TCP/IP game we host: every hero in it", faster ? "on" : "off", ms);
        g_log(b);
    }
}
void QuickTally(uint64_t* movedOwn, uint64_t* movedJoined, uint64_t* reach) {
    *movedOwn = g_tallyOwn.load(std::memory_order_relaxed);
    *movedJoined = g_tallyJoined.load(std::memory_order_relaxed);
    *reach = g_tallyReach.load(std::memory_order_relaxed);
}

bool QuickWanted() { return g_quick.load() && !g_faulted.load(); }
bool QuickReady() { return g_quickState.load() == 1; }
bool QuickAttack(uint32_t hero, int32_t endFrame) { return QuickLive() && g_quickNotes.Is(hero, endFrame); }

// [gameplay] quick_blow: the scheduler's hook, on its own (the other hooks may be in already from move_while_attacking, on by default);
// only with them in - a moved blow without the trigger's reach and the gate would still be the game's, but noted nowhere.
void InstallQuick() {
    static ULONGLONG retryAt = 0;
    static unsigned tries = 0;
    if (!g_quick.load() || g_quickState.load() != 0 || g_state.load() != 1 || GetTickCount64() < retryAt) return;
    retryAt = GetTickCount64() + (tries < 30 ? 2000 : 30000);
    if (!d2rsig::Addr(RVA_FRAME_EVENTS)) return;
    if (!d2rsig::Check(RVA_FRAME_EVENTS, kSigFrameEvents, sizeof kSigFrameEvents)) {
        if (++tries == 30 && g_log) g_log("vrcam: quick blow - the server's frame scheduler is not as expected (another build?): the blow lands on the game's frame");
        return;
    }
    const bool in = d2rsig::Hook(RVA_FRAME_EVENTS, kSigFrameEvents, sizeof kSigFrameEvents, reinterpret_cast<void*>(&HookFrameEvents), reinterpret_cast<void**>(&g_origFrameEvents));
    g_quickState.store(in ? 1 : 2);
    if (g_log)
        g_log(in ? "vrcam: quick blow - ready: the server's frame scheduler is hooked (a quick attack's blow on its first frame)"
                 : "vrcam: quick blow - the frame scheduler's hook could not go in: the blow lands on the game's frame");
}

void Install() {
    static ULONGLONG retryAt = 0;
    static unsigned tries = 0;
    InstallQuick();
    if (!(g_on.load() || g_flatScope.load() != kScopeNone || attackpath::Wanted() || g_quick.load()) || g_state.load() != 0 || GetTickCount64() < retryAt) return;
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
    if (!Live() || OwnScopeNow() == kScopeNone) {   // VR first person, or the flat key's views with a pad / VR controller ([gameplay] flat_move_while_attacking)
        hold.Reset();
        return {};
    }
    hold.Arm(g_holdUntil.load(std::memory_order_acquire));
    return hold.Step(nowMs, attackBits);
}

}  // namespace moveattack
