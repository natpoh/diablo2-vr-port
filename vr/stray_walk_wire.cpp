// stray_walk_wire.cpp - the hero never runs off on a stray click or command in VR first person: see stray_walk.h for what is
// dropped and why; docs/move_recon.md sections 4 and 5 for the funnel 0xFABE0 and the map click 0xFE3B0.
//
// Nothing here calls into the game except the original of the funnel it hooks, and a drop is simply not calling it: no
// prediction and no packet are made, so nothing is left half done. The decision reads no game memory (a thread-local, two
// atomics and the view gate), so it cannot fault.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <intrin.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>

#include "direct_input.h"
#include "shot_trace_wire.h"
#include "sigscan.h"
#include "stray_walk.h"
#include "stray_walk_wire.h"
#include "view_gate.h"

namespace straywalk {
namespace {

// the game's command funnel (3.3.93787): cmd in ecx, the player's unit in rdx, a1 in r8d, a2 in r9d; no result is used
constexpr uint64_t RVA_PLAYER_COMMAND = 0xFABE0;
// push rbp; push rbx; push r13; push r14; push r15; lea rbp, [rsp-0x37]; sub rsp, 0xA0 (REX-prefixed push, no rip, no rel32)
const uint8_t kSigPlayerCommand[16] = {0x40, 0x55, 0x53, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xC9, 0x48, 0x81};
using CommandFn = void (*)(int cmd, void* unit, int a1, int a2);

void (*g_log)(const char*) = nullptr;
CommandFn g_orig = nullptr;
std::atomic<int> g_hook{0};   // 0 not in yet, 1 in, 2 failed
std::atomic<bool> g_walkOn{true}, g_clickOn{true};

struct ClickCtx { bool active = false; Ctx ctx = Ctx::None; int type = 0; };
thread_local ClickCtx t_click;

Limiter g_dropSay, g_noteSay, g_clickSay;
std::atomic<uint32_t> g_dropped{0}, g_noted{0}, g_clicks{0};
std::atomic<bool> g_attackRunOn{false};
bool (*g_attackHeld)() = nullptr;
SRWLOCK g_sayLock = SRWLOCK_INIT;

void Say(const char* fmt, ...) {
    if (!g_log) return;
    char b[384];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    g_log(b);
}

uintptr_t CallerRva(void* ret) {
    static const uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
    return (uintptr_t)ret - base;
}

void HookPlayerCommand(int cmd, void* unit, int a1, int a2) {
    const Ctx c = t_click.active ? t_click.ctx : Ctx::None;
    shottrace::OnCommand(cmd, unit, a1, a2, CtxName(c), t_click.active ? t_click.type : -1);   // [debug] shot_trace: told, then judged as before
    const bool ours = g_attackRunOn.load() && g_attackHeld && g_attackHeld();   // an attack of ours is pressed with Stand Still
    const Verdict v = WalkVerdict(g_walkOn.load(), viewgate::F4(), cmd, c, ours);
    if (v != Verdict::Pass) {
        const bool drop = v == Verdict::Drop;
        uint32_t count = 0;
        bool say = false;
        const uint32_t total = (drop ? g_dropped : g_noted).fetch_add(1) + 1;
        AcquireSRWLockExclusive(&g_sayLock);
        say = (drop ? g_dropSay : g_noteSay).Say(GetTickCount64(), &count);
        ReleaseSRWLockExclusive(&g_sayLock);
        if (say) {
            if (drop && c != Ctx::GroundClick)
                Say("vrcam: stray walk - DROPPED a %s to (%d, %d) [command %d] out of %s, caller 0x%llX, while an attack of ours is pressed with Stand Still: "
                    "the shot or the blow does not run the hero off ([gameplay] block_attack_run) - %u so far%s", cmd == kRunToPoint ? "run" : "walk", a1, a2, cmd,
                    CtxName(c), (unsigned long long)CallerRva(_ReturnAddress()), total, count > 1 ? " (several since the last line)" : "");
            else if (drop)
                Say("vrcam: stray walk - DROPPED a %s to (%d, %d) [command %d] out of %s, caller 0x%llX: in VR first person a click on bare ground never "
                    "walks the hero ([gameplay] block_stray_walk) - %u so far%s", cmd == kRunToPoint ? "run" : "walk", a1, a2, cmd, CtxName(c),
                    (unsigned long long)CallerRva(_ReturnAddress()), total, count > 1 ? " (several since the last line)" : "");
            else
                Say("vrcam: stray walk - noted, NOT dropped: a %s to (%d, %d) [command %d] out of %s, caller 0x%llX - %u so far",
                    cmd == kRunToPoint ? "run" : "walk", a1, a2, cmd, CtxName(c), (unsigned long long)CallerRva(_ReturnAddress()), total);
        }
        if (drop) return;
    }
    g_orig(cmd, unit, a1, a2);
}

ClickFilter g_filter;   // the window's thread only

const char* DeviceName(INPUT_MESSAGE_DEVICE_TYPE t) {
    switch (t) {
    case IMDT_UNAVAILABLE: return "unknown";
    case IMDT_KEYBOARD: return "keyboard";
    case IMDT_MOUSE: return "mouse";
    case IMDT_TOUCH: return "touch";
    case IMDT_PEN: return "pen";
    case IMDT_TOUCHPAD: return "touchpad";
    default: return "other";
    }
}

}  // namespace

void SetLogger(void (*log)(const char*)) { g_log = log; }

void SetWalkWanted(bool on) {
    if (g_walkOn.exchange(on) != on)
        Say(on ? "vrcam: stray walk - [gameplay] block_stray_walk=1: in VR first person a walk or run to a point out of a click on bare ground is dropped"
               : "vrcam: stray walk - [gameplay] block_stray_walk=0: commands as they were");
}
void SetClickWanted(bool on) {
    if (g_clickOn.exchange(on) != on) {
        Say(on ? "vrcam: stray click - [gameplay] block_stray_clicks=1: in the VR first person world with no panel open the game gets no mouse button"
               : "vrcam: stray click - [gameplay] block_stray_clicks=0: mouse clicks as they were");
    }
}
bool WalkWanted() { return g_walkOn.load(); }
void SetAttackRunWanted(bool on) {
    if (g_attackRunOn.exchange(on) != on)
        Say(on ? "vrcam: stray walk - [gameplay] block_attack_run=1: a run to a point out of the pad's cast of an attack of ours (Stand Still held) is dropped"
               : "vrcam: stray walk - [gameplay] block_attack_run=0: such a run is only noted");
}
void SetAttackFn(bool (*attackHeld)()) { g_attackHeld = attackHeld; }

void Install() {
    if (g_hook.load() != 0 || !(g_walkOn.load() || shottrace::Wanted())) return;   // [debug] shot_trace tells the funnel's commands too
    if (!d2rsig::Check(RVA_PLAYER_COMMAND, kSigPlayerCommand, sizeof kSigPlayerCommand)) return;   // its page is not decrypted yet, or another build
    const bool in = d2rsig::Hook(RVA_PLAYER_COMMAND, kSigPlayerCommand, sizeof kSigPlayerCommand, (void*)&HookPlayerCommand, (void**)&g_orig);
    g_hook.store(in ? 1 : 2);
    Say(in ? "vrcam: stray walk - the game's command funnel 0xFABE0 hooked: a walk or run to a point out of a click on bare ground is dropped in VR first person"
           : "vrcam: stray walk - the hook on the game's command funnel FAILED: stray walks are not blocked");
}

void EnterClick(int type, bool bare) {
    t_click.active = true;
    t_click.type = type;
    t_click.ctx = ContextOf(type, bare);
}
void LeaveClick() { t_click.active = false; t_click.ctx = Ctx::None; }

bool DropWindowMouse(unsigned msg, bool menuOpen, bool inWorld, bool mouseLook, bool ownClicks) {
    if (ButtonOf(msg) < 0) return false;
    World w;
    w.on = g_clickOn.load();
    w.f4 = viewgate::F4();
    w.inWorld = inWorld;
    w.menuOpen = menuOpen;
    w.mouseLook = mouseLook;
    w.ownClicks = ownClicks;
    INPUT_MESSAGE_SOURCE src{};
    const bool known = GetCurrentInputMessageSource(&src) != FALSE;
    const bool injected = known && src.originId == IMO_INJECTED;
    const bool drop = g_filter.Drop(msg, w, injected);
    if (drop && !IsUp(msg)) {
        const uint32_t total = g_clicks.fetch_add(1) + 1;
        uint32_t count = 0;
        AcquireSRWLockExclusive(&g_sayLock);
        const bool say = g_clickSay.Say(GetTickCount64(), &count);
        ReleaseSRWLockExclusive(&g_sayLock);
        if (say) {
            directinput::Frame f;
            char pads[160] = "the controllers are not known";
            if (directinput::Latest(&f))
                snprintf(pads, sizeof pads, "right trigger %.2f, grip %.2f, left trigger %.2f, grip %.2f", f.hand[1].trigger, f.hand[1].squeeze,
                         f.hand[0].trigger, f.hand[0].squeeze);
            const char* names[] = {"left", "right", "middle", "side"};
            const char* origin = !known ? "origin unknown" : injected ? "injected by a program" : src.originId == IMO_HARDWARE ? "from a device" : "from Windows";
            Say("vrcam: stray click - a %s button press DROPPED in the VR first person world (no panel open): %s, device type %s; %s - %u so far%s "
                "([gameplay] block_stray_clicks)", names[ButtonOf(msg)], origin, known ? DeviceName(src.deviceType) : "unknown", pads, total,
                count > 1 ? " (several since the last line)" : "");
        }
    }
    return drop;
}

}  // namespace straywalk
