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
// [hud] monster_height (VR F4, 2026-10-16, a player: "the monster's health bar is too high and cannot be moved"):
// the same call, the game's own answer, then its y moved up (+) or down (-) by a percent of the viewport's
// height. The helper adds the plate's own offset above the head after this call, so the plate keeps its look and
// its place relative to the monster, only the whole of it sits higher or lower on the screen. Units of type 1
// only (monsters and NPCs: the plates of a monster; a chest's or a player's name is not touched).
//
// Only while a setting asks for it is the hook even installed; off or on, the call passes straight through
// unless the unit is read and found ordinary (ordinary_mob_bars) or is a monster (monster_height).

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

#include "d2rcam.h"   // PickRay: the ray through the plate's anchor, in the world
#include "log_async.h"   // [debug] log_async: the plugin's log lines through a ring and a writer thread (vr/log_queue.h); off = ctx->LogInfo as ever
#include "mob_bars.h"
#include "plate_world.h"
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
std::atomic<float> g_height{0.0f};      // [hud] monster_height, percent of the screen's height, + up
std::atomic<float> g_viewportW{0.0f}, g_viewportH{0.0f};   // the game's viewport, px (NoteViewport)
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

// [hud] monster_bar_room (vr/plate_world.h): the game has answered where the monster's head is on the screen (out). Record it - the
// monster's place on the grid, that point, the ray through it - and give the game a FIXED point instead, so its plate is drawn there,
// the same place every frame, to be cut out of the interface and hung over the monster in the room. Hung: done; any other answer
// (the reason it could not): nothing recorded, out untouched - the game's own plate stays over the monster as it was.
plateworld::Why TakePlate(void* unit, float* out) {
    plateworld::Capture c;
    if (!plateworld::FixedPoint(g_viewportW.load(std::memory_order_relaxed), g_viewportH.load(std::memory_order_relaxed), c.fixed)) return plateworld::Why::NoViewport;
    c.view[0] = g_viewportW.load(std::memory_order_relaxed);
    c.view[1] = g_viewportH.load(std::memory_order_relaxed);
    if (!plateworld::FootOf((uintptr_t)unit, GameRead(), &c.foot[0], &c.foot[1])) return plateworld::Why::NoFoot;
    plateworld::RayFn ray = plateworld::RaySlot().load();
    if (!ray) ray = &d2rcam::PickRay;
    if (!ray(out[0], out[1], c.rayO, c.rayD)) return plateworld::Why::NoRay;
    GameRead()((uintptr_t)unit + kUnitClass, c.cls);
    c.unit = (uintptr_t)unit;
    c.px[0] = out[0];
    c.px[1] = out[1];
    c.atMs = GetTickCount64();
    c.valid = true;
    plateworld::SetCapture(c);
    out[0] = c.fixed[0];
    out[1] = c.fixed[1];
    return plateworld::Why::Hung;
}

// What became of this plate in the hook, for the tick's line in the log (it adds what only it knows: was the plate then where it was put).
void NoteSeen(void* unit, plateworld::Why why, const float* px) {
    uint32_t cls = 0;
    GameRead()((uintptr_t)unit + kUnitClass, cls);
    float foot[2];
    const bool footOk = plateworld::FootOf((uintptr_t)unit, GameRead(), &foot[0], &foot[1]);   // (its distance: monster_far, monster_depth_body)
    plateworld::NoteSeen((uintptr_t)unit, cls, why, px, GetTickCount64(), footOk ? foot : nullptr);
}

// [hud] monster_over_head: the game's own answer (the head's point, out) given another y, so that the plate it hangs on has its lower edge the player's gap
// over the head (plateworld::LiftAnchor); the x is the game's. False: nothing changed. One line in the log for each target saying what became of its plate.
bool LiftPlate(void* unit, float* out) {
    static plateworld::Tracker told;
    static ULONGLONG lastMs = 0;
    const ULONGLONG now = GetTickCount64();
    if (lastMs && now - lastMs > 400) told.Gone();   // (nothing targeted for a moment: the next one is a target of its own)
    lastMs = now;
    plateworld::LiftIn in;
    in.px[0] = out[0];
    in.px[1] = out[1];
    in.viewportH = g_viewportH.load(std::memory_order_relaxed);
    in.footOk = plateworld::FootOf((uintptr_t)unit, GameRead(), &in.foot[0], &in.foot[1]);
    in.floorM = plateworld::S().floorM.load();
    in.upm = plateworld::S().upm.load();
    in.gapCm = plateworld::S().gapCm.load();
    in.gameGapPx = plateworld::S().gameGapPx.load();
    plateworld::RayFn ray = plateworld::RaySlot().load();
    in.ray = ray ? ray : &d2rcam::PickRay;
    plateworld::LiftOut lo;
    const plateworld::LiftWhy why = plateworld::LiftAnchor(in, &lo);
    if (told.Should((uintptr_t)unit, (int)why, now)) {
        uint32_t cls = 0;
        GameRead()((uintptr_t)unit + kUnitClass, cls);
        char b[260];
        if (why == plateworld::LiftWhy::Lifted)
            snprintf(b, sizeof b, "vrcam: monster_over_head - target class %u (unit %p): the plate %s (%.0f px %s than the game puts it, its lower edge %.0f cm over the head, %.1f m away; the game's gap %.0f px)",
                     cls, unit, plateworld::LiftWhyText(why), std::fabs(lo.movedPx), lo.movedPx <= 0.0f ? "higher" : "lower", in.gapCm, lo.distM, in.gameGapPx);
        else
            snprintf(b, sizeof b, "vrcam: monster_over_head - target class %u (unit %p): the plate %s", cls, unit, plateworld::LiftWhyText(why));
        Log(b);
    }
    if (why != plateworld::LiftWhy::Lifted) return false;
    out[1] = lo.anchorY;
    return true;
}

bool HookUnitToScreen(void* context, void* unit, float* out, uint8_t above) {
    // (F4 only, vr/view_gate.h: the game's own plates in every other view)
    const bool hide = g_on.load(std::memory_order_relaxed);
    const float height = g_height.load(std::memory_order_relaxed);
    const bool world = plateworld::Active();
    const bool track = plateworld::Tracking();   // [hud] monster_far / monster_depth_body: which monster is targeted, for the plate left in the picture
    const bool lift = plateworld::LiftWanted() && !world;   // [hud] monster_over_head (the plate cut out and hung in the room, if asked, has its own way)
    if (IsPlateCall((hide || height != 0.0f || world || track || lift) && viewgate::F4(), above, out, (uintptr_t)_ReturnAddress(), g_plateRet.load(std::memory_order_relaxed))) {
        static std::atomic<bool> toldHide{false}, toldKeep{false}, toldShift{false}, toldWorld{false};
        if (hide && HidesPlate((uintptr_t)unit, GameRead())) {
            TellOnce(toldHide, true, (uintptr_t)unit);
            if (world) NoteSeen(unit, plateworld::Why::Hidden, nullptr);   // (for the log: ordinary_mob_bars comes before the world bar)
            return CullAnswer(g_style.load(std::memory_order_relaxed), out);
        }
        if (hide && unit) TellOnce(toldKeep, false, (uintptr_t)unit);
        if (world && IsMonsterUnit((uintptr_t)unit, GameRead())) {   // [hud] monster_bar_room: the plate goes to the fixed point, the monster's place is recorded
            const bool shown = g_orig(context, unit, out, above);
            if (!shown) {
                NoteSeen(unit, plateworld::Why::NotShown, nullptr);
                return shown;
            }
            const float was[2] = {out[0], out[1]};
            const plateworld::Why why = TakePlate(unit, out);
            NoteSeen(unit, why, was);
            if (why == plateworld::Why::Hung && !toldWorld.exchange(true)) {
                char b[200];
                snprintf(b, sizeof b, "vrcam: monster_bar_room - the first monster plate taken off its monster (head at %.0f,%.0f px) to the fixed point %.0f,%.0f px to be cut out",
                         was[0], was[1], out[0], out[1]);
                Log(b);
            }
            return shown;
        }
        if ((height != 0.0f || track || lift) && IsMonsterUnit((uintptr_t)unit, GameRead())) {   // monster_over_head / monster_height: the game's own answer, moved; monster_far / monster_depth_body: noted
            const bool shown = g_orig(context, unit, out, above);
            if (shown && (track || lift)) NoteSeen(unit, plateworld::Why::Hung, nullptr);   // (the monster's place; the plate stays where the game puts it)
            if (shown && lift) {
                LiftPlate(unit, out);   // (over its head by the player's gap; else the game's own place)
            } else if (shown && height != 0.0f) {
                const float was = out[1];
                out[1] = ShiftedY(was, height, g_viewportH.load(std::memory_order_relaxed));
                if (!toldShift.exchange(true)) {
                    char b[160];
                    snprintf(b, sizeof b, "vrcam: monster_height - the first monster plate moved: y %.0f -> %.0f px (%.0f %% of a %.0f px screen)", was, out[1], height,
                             g_viewportH.load(std::memory_order_relaxed));
                    Log(b);
                }
            }
            return shown;
        }
    }
    return g_orig(context, unit, out, above);
}

}  // namespace

void SetEnabled(bool on, int style) {
    g_style.store(style);
    g_on.store(on);
}

void SetHeight(float pct) { g_height.store(ClampHeight(pct)); }
void NoteViewport(float widthPx, float heightPx) {
    g_viewportW.store(widthPx, std::memory_order_relaxed);
    g_viewportH.store(heightPx, std::memory_order_relaxed);
}

bool Hooked() { return g_in.load(); }

void Install(const PluginContext* ctx) {
    if (!ctx || !(g_on.load() || g_height.load() != 0.0f || plateworld::Wanted() || plateworld::Tracking() || plateworld::LiftWanted()) || g_in.load() || g_failed) return;
    g_ctx = ctx;
    static ULONGLONG since = 0;
    static bool told = false;
    if (!since) since = GetTickCount64();
    if (d2rsig::NotInBuild(RVA_UNIT_TO_SCREEN) || d2rsig::NotInBuild(RVA_PLATE_RET)) {
        g_failed = true;
        Log("vrcam: plates (ordinary_mob_bars, monster_height, monster_bar_room) - the game's plate code is not in this build (another game version?): the plates stay as they are");
        return;
    }
    // The code is decrypted page by page the first time the game runs it: not readable yet is tried again.
    if (!d2rsig::Check(RVA_UNIT_TO_SCREEN, kSigUnitToScreen, sizeof kSigUnitToScreen) ||
        !d2rsig::Check(RVA_PLATE_RET, kSigPlateRet, sizeof kSigPlateRet) || !d2rsig::Addr(RVA_PLATE_RET)) {
        if (!told && GetTickCount64() - since > 30000) {
            told = true;
            Log("vrcam: plates (ordinary_mob_bars, monster_height, monster_bar_room) - the game's plate code not found (yet) after 30 s: another build, or code the game has not run");
        }
        return;
    }
    g_plateRet.store(d2rsig::Addr(RVA_PLATE_RET));   // before the hook: it only acts for this one caller
    const bool in = d2rsig::Hook(RVA_UNIT_TO_SCREEN, kSigUnitToScreen, sizeof kSigUnitToScreen, (void*)&HookUnitToScreen, (void**)&g_orig);
    g_failed = !in;
    g_in.store(in);
    Log(in ? "vrcam: plates (ordinary_mob_bars, monster_height, monster_bar_room) - the plate's anchor call hooked (VR F4 only)"
           : "vrcam: plates (ordinary_mob_bars, monster_height, monster_bar_room) - the hook would not go in: the plates stay as they are");
}

}  // namespace mobbars
