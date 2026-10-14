// mob_bars.cpp - [hud] ordinary_mob_bars: the plate (name and health bar) the game hangs over the
// monster you target is taken off the screen for an ordinary monster and kept for the rest
// (vr/mob_bars.h: which is which, and what the plate does). Presentation only - the game's target,
// attack, hover and tooltip texts are not touched, and nothing of the game's is written.
//
// Idea and layout: bearstonem/d2r-pcvr (MIT, a fork of D2R VR 0.152: cleanroom/camera/d2rcam.cpp
// HookUnitToScreen and monster_bars.h); written again here on our signatures (game_sigs.h, area
// "Monster plates"), so another build finds them by pattern.
//
// One hook, UnitToScreen (0x76A7D0: unit -> a point on the screen, "above" = at its head), and only for
// the one call of the plate's anchor helper (0xCE850, the return address 0xCE87C). Its other callers - the
// item labels, the controller's pick-up and target code, the "HP: n/n" text (0x6D7AC0) - stay the game's own:
// an answer changed there could change what the game targets.
//
// What answer takes the plate off the screen - read in the image (3.3.93787), not seen running: the helper
// turns the point into the plate's anchor, (y << 32 | x), or -1 when UnitToScreen says "not on the screen".
// The plate's widget update (0x1F84E0) leaves a -1 anchor alone - it does not move the plate - and goes on
// to update and show it, so "not on the screen" may leave the plate where it was (d2r-pcvr's answer, kept as
// [debug] mob_bars_style 1). The default answers "on the screen" at a point far outside it: the anchor goes
// to the widget's own placement (0x857050, which does not clamp to the screen) and the plate is drawn where
// nobody looks. Only a headset can say which of the two takes the plate away; the settings page and the note
// say so.
//
// Only while the setting is on is the hook even installed; off or on, the call passes straight through
// unless the unit is read and found ordinary.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <intrin.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <D2RLPlugin/api.h>

#include "log_async.h"   // [debug] log_async: the plugin's log lines through a ring and a writer thread (vr/log_queue.h); off = ctx->LogInfo as ever
#include "mob_bars.h"
#include "sigscan.h"
#include "view_gate.h"

#pragma intrinsic(_ReturnAddress)

using namespace D2RL;

namespace mobbars {
namespace {

// Addresses in 3.3.93787 (the keys of game_sigs.h) and the bytes each starts with there.
constexpr uint64_t RVA_UNIT_TO_SCREEN = 0x76A7D0, RVA_PLATE_RET = 0xCE87C;
// mov r11, rsp; push rbp; push rsi; push rdi; push r15; lea rbp, [r11-58h]; sub rsp, 138h
const uint8_t kSigUnitToScreen[19] = {0x4C,0x8B,0xDC,0x55,0x56,0x57,0x41,0x57,0x49,0x8D,0x6B,0xA8,0x48,0x81,0xEC,0x38,0x01,0x00,0x00};
// after the helper's call: test al, al; je short (no anchor)
const uint8_t kSigPlateRet[3] = {0x84,0xC0,0x74};

using UnitToScreenFn = bool (*)(void* context, void* unit, float* out, uint8_t above);

std::atomic<bool> g_on{false};
std::atomic<int> g_style{0};   // [debug] mob_bars_style: 0 = on the screen far away, 1 = "not on the screen" (d2r-pcvr's)
std::atomic<bool> g_in{false};
std::atomic<uintptr_t> g_plateRet{0};
UnitToScreenFn g_orig = nullptr;
bool g_failed = false;
const PluginContext* g_ctx = nullptr;

void Log(const char* text) { if (g_ctx) logasync::Info(g_ctx, text); }

#pragma optimize("", off)
bool SafeRead(void* d, const void* s, size_t n) noexcept { __try { memcpy(d, s, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
#pragma optimize("", on)

struct GameRead {
    template <class T> bool operator()(uintptr_t address, T& value) const { return SafeRead(&value, (const void*)address, sizeof value); }
};

// The first answer of each kind, once, for the log (what a headset test will want to see).
void TellOnce(std::atomic<bool>& told, bool hidden, uintptr_t unit) {
    if (told.exchange(true)) return;
    uint32_t type = 0, classId = 0;
    GameRead()(unit + kUnitType, type);
    GameRead()(unit + kUnitClass, classId);
    char b[200];
    snprintf(b, sizeof b, "vrcam: ordinary_mob_bars - the first plate %s: unit type %u, class %u",
             hidden ? "taken off the screen (an ordinary monster)" : "kept (not an ordinary monster, or not read)", type, classId);
    Log(b);
}

bool HookUnitToScreen(void* context, void* unit, float* out, uint8_t above) {
    // (F4 only, vr/view_gate.h: the game's own plates in every other view)
    if (IsPlateCall(g_on.load(std::memory_order_relaxed) && viewgate::F4(), above, out, (uintptr_t)_ReturnAddress(), g_plateRet.load(std::memory_order_relaxed))) {
        static std::atomic<bool> toldHide{false}, toldKeep{false};
        if (HidesPlate((uintptr_t)unit, GameRead())) {
            TellOnce(toldHide, true, (uintptr_t)unit);
            return CullAnswer(g_style.load(std::memory_order_relaxed), out);
        }
        if (unit) TellOnce(toldKeep, false, (uintptr_t)unit);
    }
    return g_orig(context, unit, out, above);
}

}  // namespace

void SetEnabled(bool on, int style) {
    g_style.store(style);
    g_on.store(on);
}

bool Hooked() { return g_in.load(); }

void Install(const PluginContext* ctx) {
    if (!ctx || !g_on.load() || g_in.load() || g_failed) return;
    g_ctx = ctx;
    static ULONGLONG since = 0;
    static bool told = false;
    if (!since) since = GetTickCount64();
    if (d2rsig::NotInBuild(RVA_UNIT_TO_SCREEN) || d2rsig::NotInBuild(RVA_PLATE_RET)) {
        g_failed = true;
        Log("vrcam: ordinary_mob_bars - the game's plate code is not in this build (another game version?): the plates stay as they are");
        return;
    }
    // The code is decrypted page by page the first time the game runs it: not readable yet is tried again.
    if (!d2rsig::Check(RVA_UNIT_TO_SCREEN, kSigUnitToScreen, sizeof kSigUnitToScreen) ||
        !d2rsig::Check(RVA_PLATE_RET, kSigPlateRet, sizeof kSigPlateRet) || !d2rsig::Addr(RVA_PLATE_RET)) {
        if (!told && GetTickCount64() - since > 30000) {
            told = true;
            Log("vrcam: ordinary_mob_bars - the game's plate code not found (yet) after 30 s: another build, or code the game has not run");
        }
        return;
    }
    g_plateRet.store(d2rsig::Addr(RVA_PLATE_RET));   // before the hook: it only acts for this one caller
    const bool in = d2rsig::Hook(RVA_UNIT_TO_SCREEN, kSigUnitToScreen, sizeof kSigUnitToScreen, (void*)&HookUnitToScreen, (void**)&g_orig);
    g_failed = !in;
    g_in.store(in);
    Log(in ? "vrcam: ordinary_mob_bars - the plate's anchor call hooked: an ordinary monster's plate is taken off the screen"
           : "vrcam: ordinary_mob_bars - the hook would not go in: the plates stay as they are");
}

}  // namespace mobbars
