// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed vr/game_patches.cpp - commits 853607f (transparent_filter), 133465a (profiler_accounting), 30083a1 (draw_debug_names), 9b9e28e (selection_walk, shadows_once, cull_once), 1289b9a (cull volume), at tag v0.3.33-windows-preview.
// Ported to D2R VR Authentic: our logging, memory reads, ini keys and pair marks (vr/frame/frame_order.cpp); present_once left out.
// This file stays MPL 2.0.
//
// [game_patches]: see vr/unleashed_game_patches.h. Everything here but Configure and the hooks of the once-per-pair patches
// runs on the draw thread, between frames; those hooks run inside an eye pass and say which thread each is on.
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>
#include "unleashed_game_patches.h"
#include "unleashed_game_patch_signatures.h"
#include "sigscan.h"   // cleanroom/sigscan: the loader's checked hook and patch (an RVA not in its table is used as it is)
#include "d2rcam.h"    // cleanroom/camera: the game camera's look-at point
#include "unleashed_game_patch_plan.h"
#include "unleashed_cull_volume.h"

namespace gamepatches {
namespace {
using namespace abi;
using gamepatch::SigMatches;

enum class State : uint8_t { Off, Waiting, Applied, Refused };
const char* Name(State s) {
    return s == State::Off ? "off" : s == State::Applied ? "applied" : s == State::Refused ? "refused" : "waiting for the game's code";
}

void (*g_log)(const char*) = nullptr;
void Say(const std::string& text) { if (g_log) g_log(("vrcam: game patches - " + text).c_str()); }

// The game's memory, read and written without a fault taking the game down (a page not there reads as "no").
namespace framevr {
bool ReadMemory(void* to, const void* from, size_t n) {
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), from, to, n, &got) && got == n;
}
bool WriteMemory(void* to, const void* from, size_t n) {   // data only (the debug event level): code goes through the loader's checked patch
    SIZE_T put = 0;
    return WriteProcessMemory(GetCurrentProcess(), to, from, n, &put) && put == n;
}
}  // namespace framevr

// The game's code is decrypted as it is first run. A place whose bytes differ is looked at again on later
// frames, and given up after this many looks (the code of a frame's draw has run after the first frames).
constexpr uint32_t MaxLooks = 40, LookEvery = 16;

struct Patch {
    const char* key;                    // the name in [game_patches]
    std::atomic<bool> wanted{false};
    State state = State::Off;           // draw thread only
    uint32_t looks = 0;
    explicit Patch(const char* k) : key(k) {}
};

// One stretch of the game's code as it is now. False (and `differs` set) when it cannot be read or is not
// what the fingerprint says.
template <size_t N>
bool Holds(uint64_t rva, const uint16_t (&sig)[N], uint8_t* live, uint64_t& differs) {
    const uintptr_t at = d2rsig::Addr(rva);
    if (at && framevr::ReadMemory(live, reinterpret_cast<const void*>(at), N) && SigMatches(live, sig, N)) return true;
    differs = rva;
    return false;
}

std::string Hex(uint64_t v) {
    char text[24];
    snprintf(text, sizeof text, "0x%llX", static_cast<unsigned long long>(v));
    return text;
}

// A place that did not hold: looked at again later, or given up (said once).
void NotYet(Patch& p, uint64_t differs) {
    if (++p.looks < MaxLooks) { p.state = State::Waiting; return; }
    p.state = State::Refused;
    Say(std::string(p.key) + " refused: bytes differ at RVA " + Hex(differs) + " (the game's code there is not what this mod knows; the game is left as it is)");
}
void Refuse(Patch& p, const std::string& why) {
    p.state = State::Refused;
    Say(std::string(p.key) + " refused: " + why + " (nothing more is written)");
}

// --- transparent_filter -----------------------------------------------------------------------------------
// One page for the cave, within a relative jump of `near`: free address space is looked for above the
// place, then below it, as hook libraries do. Never freed: a few bytes, for the life of the game.
void* PageNear(uintptr_t around) {
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    const uintptr_t gran = si.dwAllocationGranularity ? si.dwAllocationGranularity : 0x10000;
    const uintptr_t reach = 0x7FE00000;   // 2 GB less 2 MB: the jumps are a few bytes from `around` and the page's start
    const uintptr_t lowest = std::max<uintptr_t>(reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress), around > reach ? around - reach : 0);
    const uintptr_t highest = std::min<uintptr_t>(reinterpret_cast<uintptr_t>(si.lpMaximumApplicationAddress), around + reach);
    const SIZE_T size = 0x1000;
    const auto up = [gran](uintptr_t v) { return (v + gran - 1) / gran * gran; };
    MEMORY_BASIC_INFORMATION mbi{};
    int steps = 0;
    for (uintptr_t at = up(around); at + size <= highest && steps < 4096; ++steps) {
        if (!VirtualQuery(reinterpret_cast<void*>(at), &mbi, sizeof mbi)) break;
        if (mbi.State == MEM_FREE) {
            if (void* p = VirtualAlloc(reinterpret_cast<void*>(at), size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) return p;
            at += gran;   // refused here: the next place an allocation may start
            continue;
        }
        at = up(reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize);   // past this used stretch
    }
    steps = 0;
    for (uintptr_t at = around / gran * gran; at >= lowest + gran && steps < 4096; ++steps) {
        at -= gran;
        if (!VirtualQuery(reinterpret_cast<void*>(at), &mbi, sizeof mbi)) break;
        if (mbi.State == MEM_FREE) {
            if (void* p = VirtualAlloc(reinterpret_cast<void*>(at), size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) return p;
            continue;
        }
        const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.AllocationBase ? mbi.AllocationBase : mbi.BaseAddress);
        at = base / gran * gran;   // below this used stretch next
    }
    return nullptr;
}

Patch filter{"transparent_filter"};
uint8_t* filterCave = nullptr;                              // the page; its code is written once
uint8_t filterWas[gamepatch::FilterSiteBytes] = {};         // the game's 8 bytes
uint8_t filterNow[gamepatch::FilterSiteBytes] = {};         // ours

void ApplyFilter() {
    uint8_t head[sizeof FilterHeadSig / 2], loop[sizeof FilterLoopSig / 2], skip[sizeof FilterSkipSig / 2];
    uint64_t differs = 0;
    if (!Holds(FilterHeadRva, FilterHeadSig, head, differs) || !Holds(FilterLoopRva, FilterLoopSig, loop, differs) ||
        !Holds(FilterSkipRva, FilterSkipSig, skip, differs)) return NotYet(filter, differs);
    // Nothing in the function may jump into the middle of the instruction the jump replaces.
    const uintptr_t function = d2rsig::Addr(FilterFunctionRva), site = d2rsig::Addr(FilterLoopRva), skipAt = d2rsig::Addr(FilterSkipRva);
    std::vector<uint8_t> body(size_t(FilterFunctionEnd - FilterFunctionRva));
    if (!function || !framevr::ReadMemory(body.data(), reinterpret_cast<const void*>(function), body.size())) return NotYet(filter, FilterFunctionRva);
    if (gamepatch::BranchesInto(body.data(), body.size(), function, site + 1, site + gamepatch::FilterLoadBytes - 1))
        return Refuse(filter, "a jump in the game's function at RVA " + Hex(FilterFunctionRva) + " may land inside the instruction at RVA " + Hex(FilterLoopRva));
    if (!filterCave) {
        auto* page = static_cast<uint8_t*>(PageNear(site));
        if (!page) return Refuse(filter, "no free memory within a relative jump of the game's code for the few bytes of new code");
        const gamepatch::FilterPlan plan = gamepatch::PlanFilter(site, skipAt, reinterpret_cast<uintptr_t>(page), loop[gamepatch::FilterLoadBytes]);
        DWORD old = 0;
        if (!plan.ok) { VirtualFree(page, 0, MEM_RELEASE); return Refuse(filter, "the new code would be out of a relative jump's reach"); }
        std::memset(page, 0xCC, 0x1000);
        std::memcpy(page, plan.cave, sizeof plan.cave);
        if (!VirtualProtect(page, 0x1000, PAGE_EXECUTE_READ, &old)) { VirtualFree(page, 0, MEM_RELEASE); return Refuse(filter, "the new code's page could not be made runnable"); }
        FlushInstructionCache(GetCurrentProcess(), page, 0x1000);
        std::memcpy(filterWas, loop, sizeof filterWas);
        std::memcpy(filterNow, plan.site, sizeof filterNow);
        filterCave = page;
    }
    // The loader's checked write: the 8 bytes must still be the game's. One aligned 8 bytes, between frames.
    if (!d2rsig::Patch(FilterLoopRva, filterWas, sizeof filterWas, filterNow, sizeof filterNow)) return Refuse(filter, "the loader refused the write at RVA " + Hex(FilterLoopRva));
    uint8_t back[sizeof filterNow] = {};
    if (!framevr::ReadMemory(back, reinterpret_cast<const void*>(site), sizeof back) || std::memcmp(back, filterNow, sizeof back))
        return Refuse(filter, "the loader took the write at RVA " + Hex(FilterLoopRva) + " but the bytes there are not what was written");
    filter.state = State::Applied;
    char text[320];
    snprintf(text, sizeof text, "transparent_filter applied: the transparent pass's Ghost filter tests a draw record's flags before it reads "
             "the record's constants (jump at RVA 0x%llX to %u bytes of new code at 0x%llX; %u bytes of the game's code proven first)",
             static_cast<unsigned long long>(FilterLoopRva), unsigned(gamepatch::FilterCaveBytes), static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(filterCave)),
             unsigned(sizeof head + sizeof loop + sizeof skip));
    Say(text);
}

void RemoveFilter() {
    if (d2rsig::Patch(FilterLoopRva, filterNow, sizeof filterNow, filterWas, sizeof filterWas)) {
        filter.state = State::Off;
        Say("transparent_filter taken out: the game's own bytes are back at RVA " + Hex(FilterLoopRva));
    } else {
        filter.state = State::Refused;   // stays as it is; not tried again
        Say("transparent_filter could NOT be taken out: the loader refused the write at RVA " + Hex(FilterLoopRva) + "; it stays in until the game is closed");
    }
}

// --- profiler_accounting ---------------------------------------------------------------------------------------
Patch account{"profiler_accounting"};

void ApplyAccount() {
    uint8_t call[sizeof AccountCallSig / 2], body[sizeof AccountSig / 2];
    uint64_t differs = 0;
    if (!Holds(AccountCallRva, AccountCallSig, call, differs) || !Holds(AccountRva, AccountSig, body, differs)) return NotYet(account, differs);
    const uint8_t was[2] = {call[AccountJumpAt], call[AccountJumpAt + 1]}, now[2] = {AccountJumpAlways, call[AccountJumpAt + 1]};
    if (!d2rsig::Patch(AccountCallRva + AccountJumpAt, was, sizeof was, now, sizeof now)) return Refuse(account, "the loader refused the write at RVA " + Hex(AccountCallRva + AccountJumpAt));
    account.state = State::Applied;
    Say("profiler_accounting applied: a draw no longer takes the game's GPU profiler lock to be counted (the jump at RVA " + Hex(AccountCallRva + AccountJumpAt) +
        " is taken always; " + std::to_string(sizeof call + sizeof body) + " bytes of the game's code proven first). The game's own profiling windows would show no draws or primitives per scope");
}

void RemoveAccount() {
    const uint8_t now[2] = {AccountJumpAlways, uint8_t(AccountCallSig[AccountJumpAt + 1])}, was[2] = {uint8_t(AccountCallSig[AccountJumpAt]), uint8_t(AccountCallSig[AccountJumpAt + 1])};
    if (d2rsig::Patch(AccountCallRva + AccountJumpAt, now, sizeof now, was, sizeof was)) {
        account.state = State::Off;
        Say("profiler_accounting taken out: the game's own bytes are back at RVA " + Hex(AccountCallRva + AccountJumpAt));
    } else {
        account.state = State::Refused;
        Say("profiler_accounting could NOT be taken out: the loader refused the write at RVA " + Hex(AccountCallRva + AccountJumpAt) + "; it stays in until the game is closed");
    }
}

// --- draw_debug_names ------------------------------------------------------------------------------------------
Patch names{"draw_debug_names"};
int32_t levelWas = 0;          // the game's level before it was lowered
bool levelWritten = false;

void ApplyNames() {
    uint8_t get[sizeof LevelGetSig / 2], set[sizeof LevelSetSig / 2], begin[sizeof DrawNameBeginSig / 2], end[sizeof DrawNameEndSig / 2],
        eventBegin[sizeof EventBeginSig / 2], eventEnd[sizeof EventEndSig / 2];
    uint64_t differs = 0;
    if (!Holds(LevelGetRva, LevelGetSig, get, differs) || !Holds(LevelSetRva, LevelSetSig, set, differs) ||
        !Holds(DrawNameBeginRva, DrawNameBeginSig, begin, differs) || !Holds(DrawNameEndRva, DrawNameEndSig, end, differs) ||
        !Holds(EventBeginRva, EventBeginSig, eventBegin, differs) || !Holds(EventEndRva, EventEndSig, eventEnd, differs)) return NotYet(names, differs);
    const uintptr_t at = d2rsig::Addr(LevelRva);
    int32_t level = 0;
    if (!at || !framevr::ReadMemory(&level, reinterpret_cast<const void*>(at), sizeof level)) return NotYet(names, LevelRva);
    switch (gamepatch::LevelDecision(level)) {
    case gamepatch::Level::Refuse:
        return Refuse(names, "the number at RVA " + Hex(LevelRva) + " is " + std::to_string(level) + ", not a debug event level");
    case gamepatch::Level::Leave:
        names.state = State::Applied;
        levelWritten = false;
        Say("draw_debug_names applied: nothing to change, the game's debug event level is " + std::to_string(level) + " - it does not name its draws");
        return;
    case gamepatch::Level::Lower: break;
    }
    const int32_t lowered = gamepatch::LevelWithoutDrawNames;
    int32_t back = 0;
    if (!framevr::WriteMemory(reinterpret_cast<void*>(at), &lowered, sizeof lowered) ||
        !framevr::ReadMemory(&back, reinterpret_cast<const void*>(at), sizeof back) || back != lowered)
        return Refuse(names, "the debug event level at RVA " + Hex(LevelRva) + " could not be written");
    levelWas = level;
    levelWritten = true;
    names.state = State::Applied;
    Say("draw_debug_names applied: the game's debug event level " + std::to_string(level) + " -> " + std::to_string(lowered) +
        " (the int at RVA " + Hex(LevelRva) + "): a draw is no longer given a formatted name and its own debug event, the passes keep theirs");
}

void RemoveNames() {
    names.state = State::Off;
    if (!levelWritten) { Say("draw_debug_names off: nothing had been changed"); return; }
    levelWritten = false;
    const uintptr_t at = d2rsig::Addr(LevelRva);
    int32_t level = 0;
    if (at && framevr::ReadMemory(&level, reinterpret_cast<const void*>(at), sizeof level) && level == gamepatch::LevelWithoutDrawNames &&
        framevr::WriteMemory(reinterpret_cast<void*>(at), &levelWas, sizeof levelWas))
        Say("draw_debug_names taken out: the game's debug event level is " + std::to_string(levelWas) + " again");
    else
        Say("draw_debug_names off: the game's debug event level is no longer what this mod wrote (" + std::to_string(level) + "); left as it is");
}

// --- selection_walk --------------------------------------------------------------------------------------------
Patch selection{"selection_walk"};
uint8_t selectionWas[gamepatch::CallBytes] = {}, selectionNow[gamepatch::CallBytes] = {};

void ApplySelection() {
    uint8_t live[sizeof SelectionCallSig / 2];
    uint64_t differs = 0;
    if (!Holds(SelectionCallRva, SelectionCallSig, live, differs)) return NotYet(selection, differs);
    // Nothing in the function may jump into the middle of the call.
    const uint64_t callRva = SelectionCallRva + SelectionCallAt;
    const uintptr_t function = d2rsig::Addr(SelectionFunctionRva), site = d2rsig::Addr(callRva);
    std::vector<uint8_t> body(size_t(SelectionFunctionEnd - SelectionFunctionRva));
    if (!function || !site || !framevr::ReadMemory(body.data(), reinterpret_cast<const void*>(function), body.size())) return NotYet(selection, SelectionFunctionRva);
    if (gamepatch::BranchesIntoInstruction(body.data(), body.size(), function, size_t(site - function), gamepatch::CallBytes))
        return Refuse(selection, "a jump in the game's function at RVA " + Hex(SelectionFunctionRva) + " may land inside the call at RVA " + Hex(callRva));
    const gamepatch::CallNop plan = gamepatch::PlanCallNop(live + SelectionCallAt);
    if (!plan.ok) return Refuse(selection, "no call at RVA " + Hex(callRva));
    std::memcpy(selectionWas, live + SelectionCallAt, sizeof selectionWas);
    std::memcpy(selectionNow, plan.bytes, sizeof selectionNow);
    if (!d2rsig::Patch(callRva, selectionWas, sizeof selectionWas, selectionNow, sizeof selectionNow)) return Refuse(selection, "the loader refused the write at RVA " + Hex(callRva));
    selection.state = State::Applied;
    Say("selection_walk applied: the debug selection tool no longer walks every drawn entity in each eye pass (the call at RVA " + Hex(callRva) +
        " is five bytes that do nothing; " + std::to_string(sizeof live) + " bytes of the game's code proven first). The game's own selection marquee would mark nothing");
}

void RemoveSelection() {
    const uint64_t callRva = SelectionCallRva + SelectionCallAt;
    if (d2rsig::Patch(callRva, selectionNow, sizeof selectionNow, selectionWas, sizeof selectionWas)) {
        selection.state = State::Off;
        Say("selection_walk taken out: the game's own bytes are back at RVA " + Hex(callRva));
    } else {
        selection.state = State::Refused;
        Say("selection_walk could NOT be taken out: the loader refused the write at RVA " + Hex(callRva) + "; it stays in until the game is closed");
    }
}

// --- once per pair: which pass, and the hooks ---------------------------------------------------------------------
// The pass is written on the draw thread between a pair's draws (EyePass) and read by hooks during one. The
// two cull kicks and the shadow update run on the draw thread; the shadow collect and the two shadow draws
// are render tasks on any thread, and read only `shadowsKept` and `keptPasses`, which the update hook wrote
// before the pass's tasks were started and nothing writes again until the pass has ended.
std::atomic<uint8_t> passNow{uint8_t(gamepatch::Pass::Alone)};
static_assert(uint8_t(Pass::Alone) == uint8_t(gamepatch::Pass::Alone) && uint8_t(Pass::First) == uint8_t(gamepatch::Pass::First) &&
              uint8_t(Pass::Second) == uint8_t(gamepatch::Pass::Second), "the two Pass enums are one");

using Fn4 = uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
using Fn5 = uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
using Fn6 = uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);

// The loader switches its detour on just before it publishes the way back to the game's function; a call
// that arrives in between waits for it (as vr/render_task_hooks.cpp does).
template <class Fn>
inline Fn Original(Fn& published) noexcept {
    auto& slot = reinterpret_cast<Fn const volatile&>(published);
    Fn call = slot;
    for (unsigned i = 0; !call; ++i) {
        if (i < 4096) YieldProcessor(); else SwitchToThread();
        call = slot;
    }
    return call;
}

// A hook over the first `bytes` of a function whose fingerprint was proven just before. It stays for the
// life of the game: a patch that is off makes its hooks pass every call through.
template <size_t N, class Fn>
bool HookIn(uint64_t rva, const uint16_t (&sig)[N], uint32_t bytes, Fn detour, Fn* original) {
    uint8_t expected[N];
    for (size_t i = 0; i < N; ++i) expected[i] = uint8_t(sig[i]);
    return bytes <= N && d2rsig::Hook(rva, expected, bytes, reinterpret_cast<void*>(detour), reinterpret_cast<void**>(original));
}

// --- shadows_once ----------------------------------------------------------------------------------------------
Patch shadows{"shadows_once"};
std::atomic<bool> shadowsOn{false};           // applied and wanted
std::atomic<bool> shadowsKept{false};         // this pass keeps the first pass's views, shadow map and culled casters
std::atomic<uintptr_t> keptPasses{0};         // the shadow passes object that is so
gamepatch::ShadowPair shadowPair;             // draw thread only
Fn6 originalShadowUpdate = nullptr;
Fn4 originalShadowCollect = nullptr, originalShadowKick = nullptr;
Fn5 originalShadowRender = nullptr;
Fn4 shadowSpot = nullptr;                     // the game's spot light update
Fn4 shadowBucket = nullptr;                   // the game's bucket lookup: (Frame*, name*) -> bucket*
uintptr_t shadowBucketName[2] = {};
bool shadowHooksIn = false;
std::atomic<uint32_t> shadowsKeptPasses{0}, shadowsOwnPasses{0}, shadowCollectsOut{0}, shadowDrawsOut{0}, shadowKicksOut{0};

uintptr_t ShadowUpdateHook(uintptr_t passes, uintptr_t settings, uintptr_t context, uintptr_t registry, uintptr_t camera, uintptr_t bounds) {
    const Fn6 call = Original(originalShadowUpdate);
    const bool on = shadowsOn.load(std::memory_order_relaxed);
    const auto pass = gamepatch::Pass(passNow.load(std::memory_order_relaxed));
    // The camera's "look-at point set" byte: the game's fit reads the same byte of the same object next.
    const bool lookAt = on && pass != gamepatch::Pass::Alone && camera && *reinterpret_cast<const uint8_t*>(camera + CameraHasLookAt) != 0;
    if (gamepatch::ShadowUpdate(on, pass, passes, lookAt, shadowPair) == gamepatch::Shadow::Keep) {
        // The two views stay as the first pass fitted them; the spot lights are this pass's own.
        shadowSpot(passes + ShadowSpotOffset, settings, context, camera);
        keptPasses.store(passes, std::memory_order_relaxed);
        shadowsKept.store(true, std::memory_order_release);
        shadowsKeptPasses.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }
    if (on && pass == gamepatch::Pass::Second) shadowsOwnPasses.fetch_add(1, std::memory_order_relaxed);
    return call(passes, settings, context, registry, camera, bounds);
}

uintptr_t ShadowKickHook(uintptr_t registry, uintptr_t list, uintptr_t matrix, uintptr_t d) {
    const Fn4 call = Original(originalShadowKick);
    if (!shadowsKept.load(std::memory_order_acquire)) return call(registry, list, matrix, d);
    shadowKicksOut.fetch_add(1, std::memory_order_relaxed);   // the list stays empty: the game's own test leaves the wait out
    return 0;
}

uintptr_t ShadowCollectHook(uintptr_t passes, uintptr_t settings, uintptr_t frame, uintptr_t d) {
    const Fn4 call = Original(originalShadowCollect);
    if (!shadowsKept.load(std::memory_order_acquire) || passes != keptPasses.load(std::memory_order_relaxed) || !frame) return call(passes, settings, frame, d);
    // What the game does first, and all it does when both views are off: the two buckets, emptied.
    for (const uintptr_t name : shadowBucketName)
        if (const uintptr_t bucket = shadowBucket(frame, name, 0, 0)) *reinterpret_cast<uint64_t*>(bucket + ShadowBucketCount) = 0;
    shadowCollectsOut.fetch_add(1, std::memory_order_relaxed);
    return 0;
}

uintptr_t ShadowRenderHook(uintptr_t view, uintptr_t frame, uintptr_t camera, uintptr_t flag, uintptr_t bucket) {
    const Fn5 call = Original(originalShadowRender);
    if (shadowsKept.load(std::memory_order_acquire)) {
        const uintptr_t passes = keptPasses.load(std::memory_order_relaxed);
        if (view == passes || view == passes + ShadowViewSize) {   // the slice keeps what the first pass drew
            shadowDrawsOut.fetch_add(1, std::memory_order_relaxed);
            return 0;
        }
    }
    return call(view, frame, camera, flag, bucket);
}

void ApplyShadows() {
    if (!shadowHooksIn) {
        uint8_t live[128];
        uint64_t differs = 0;
        static_assert(sizeof ShadowBucketsSig / 2 <= sizeof live && sizeof ShadowKickCallSig / 2 <= sizeof live && sizeof ShadowRenderNegativeCallSig / 2 <= sizeof live);
        if (!Holds(ShadowUpdateRva, ShadowUpdateSig, live, differs) || !Holds(ShadowSpotCallRva, ShadowSpotCallSig, live, differs) ||
            !Holds(ShadowSpotRva, ShadowSpotSig, live, differs) || !Holds(ShadowFitCentreRva, ShadowFitCentreSig, live, differs) ||
            !Holds(ShadowFitExtentRva, ShadowFitExtentSig, live, differs) || !Holds(ShadowCollectRva, ShadowCollectSig, live, differs) ||
            !Holds(ShadowBucketsRva, ShadowBucketsSig, live, differs) || !Holds(ShadowBucketLookupRva, ShadowBucketLookupSig, live, differs) ||
            !Holds(ShadowRenderRva, ShadowRenderSig, live, differs) || !Holds(ShadowRenderPassRva, ShadowRenderPassSig, live, differs) ||
            !Holds(ShadowRenderEndRva, ShadowRenderEndSig, live, differs) || !Holds(ShadowRenderSunCallRva, ShadowRenderSunCallSig, live, differs) ||
            !Holds(ShadowRenderNegativeCallRva, ShadowRenderNegativeCallSig, live, differs) || !Holds(ShadowKickRva, CullKickSig, live, differs) ||
            !Holds(ShadowKickCallRva, ShadowKickCallSig, live, differs) || !Holds(ShadowWaitRva, ShadowWaitSig, live, differs))
            return NotYet(shadows, differs);
        shadowSpot = reinterpret_cast<Fn4>(d2rsig::Addr(ShadowSpotRva));
        shadowBucket = reinterpret_cast<Fn4>(d2rsig::Addr(ShadowBucketLookupRva));
        for (int i = 0; i < 2; ++i) shadowBucketName[i] = d2rsig::Addr(ShadowBucketNameRva[i]);
        if (!shadowSpot || !shadowBucket || !shadowBucketName[0] || !shadowBucketName[1]) return NotYet(shadows, ShadowSpotRva);
        // Four hooks, each over whole instructions of a proven prologue. One refused leaves the others in, passing
        // every call through: nothing is kept for a second pass unless all four are in.
        if (!originalShadowUpdate && !HookIn(ShadowUpdateRva, ShadowUpdateSig, ShadowUpdateHookBytes, &ShadowUpdateHook, &originalShadowUpdate))
            return Refuse(shadows, "the hook at RVA " + Hex(ShadowUpdateRva) + " was refused");
        if (!originalShadowKick && !HookIn(ShadowKickRva, CullKickSig, CullKickHookBytes, &ShadowKickHook, &originalShadowKick))
            return Refuse(shadows, "the hook at RVA " + Hex(ShadowKickRva) + " was refused");
        if (!originalShadowCollect && !HookIn(ShadowCollectRva, ShadowCollectSig, ShadowCollectHookBytes, &ShadowCollectHook, &originalShadowCollect))
            return Refuse(shadows, "the hook at RVA " + Hex(ShadowCollectRva) + " was refused");
        if (!originalShadowRender && !HookIn(ShadowRenderRva, ShadowRenderSig, ShadowRenderHookBytes, &ShadowRenderHook, &originalShadowRender))
            return Refuse(shadows, "the hook at RVA " + Hex(ShadowRenderRva) + " was refused");
        shadowHooksIn = true;
    }
    shadowPair = {};
    shadowsOn.store(true);
    shadows.state = State::Applied;
    Say("shadows_once applied: the second eye pass of a pair keeps the first's sun and negative-light shadow views, shadow map and culled casters "
        "(hooks at RVA " + Hex(ShadowUpdateRva) + " the fit, " + Hex(ShadowKickRva) + " the casters' cull, " + Hex(ShadowCollectRva) + " the collect, " +
        Hex(ShadowRenderRva) + " the draw; 16 stretches of the game's code proven first). Every other draw, and a pair whose camera has no look-at point, runs as the game does");
}

void RemoveShadows() {
    shadowsOn.store(false);
    shadows.state = State::Off;
    Say("shadows_once off: its four hooks stay in the game's code and pass every call through");
}

// --- cull_once -------------------------------------------------------------------------------------------------
Patch cullOnce{"cull_once"};
std::atomic<bool> cullOn{false};
Fn4 originalCullKick = nullptr;
bool cullHookIn = false;
constexpr double CullMargin = 0.03;           // each side of the volume wider by this tangent (1.7 degrees), the apex set back by this share
struct CullPair {                             // draw thread only
    bool first = false;                       // the first pass's own matrix is in `left`
    bool combined = false;                    // the first pass culled with `volume`
    float left[16] = {}, volume[16] = {};
} cullPair;
bool cullRelationOk = false;                  // the second eye's matrix in terms of the first's, from the pair before
double cullRelation[16] = {};
std::atomic<uint32_t> cullSkipped{0}, cullOutside{0}, cullNoVolume{0};

uintptr_t CullKickHook(uintptr_t registry, uintptr_t list, uintptr_t matrix, uintptr_t d) {
    const Fn4 call = Original(originalCullKick);
    const auto pass = gamepatch::Pass(passNow.load(std::memory_order_relaxed));
    if (!cullOn.load(std::memory_order_relaxed) || pass == gamepatch::Pass::Alone || !matrix) { cullPair = {}; return call(registry, list, matrix, d); }
    float own[16], point[3];
    std::memcpy(own, reinterpret_cast<const void*>(matrix), sizeof own);
    // Without a look-at point the game's shadow fit goes by the visible set's bounds, which a pass that does
    // not cull does not have: such a camera is culled per eye, as the game does.
    const bool lookAt = d2rcam::LookAtNow(point);
    if (pass == gamepatch::Pass::First) {
        cullPair = {};
        cullPair.first = true;
        std::memcpy(cullPair.left, own, sizeof own);
        if (lookAt && cullRelationOk) {
            float other[16];
            alignas(16) float both[16];
            cullvolume::Apply(own, cullRelation, other);
            const cullvolume::Pyramid mine = cullvolume::Decompose(own);
            if (cullvolume::Combine(mine, cullvolume::Decompose(other), CullMargin, both) && cullvolume::Contains(both, mine)) {
                std::memcpy(cullPair.volume, both, sizeof both);
                cullPair.combined = true;
                return call(registry, list, reinterpret_cast<uintptr_t>(both), d);   // the kick copies it into its jobs
            }
        }
        return call(registry, list, matrix, d);
    }
    // Second: left out only when this eye's own pyramid is inside the volume the first pass culled with.
    const CullPair pair = cullPair;
    cullPair = {};
    cullRelationOk = pair.first && cullvolume::Relation(pair.left, own, cullRelation);
    if (pair.combined && lookAt && cullvolume::Contains(pair.volume, cullvolume::Decompose(own))) {
        cullSkipped.fetch_add(1, std::memory_order_relaxed);   // the list stays empty: the game's own test leaves the wait out
        return 0;
    }
    (pair.combined ? cullOutside : cullNoVolume).fetch_add(1, std::memory_order_relaxed);
    return call(registry, list, matrix, d);
}

void ApplyCull() {
    if (!cullHookIn) {
        uint8_t live[128];
        uint64_t differs = 0;
        static_assert(sizeof CullJobSig / 2 <= sizeof live && sizeof CullKickCallSig / 2 <= sizeof live);
        if (!Holds(CullKickRva, CullKickSig, live, differs) || !Holds(CullKickCallRva, CullKickCallSig, live, differs) ||
            !Holds(CullWaitRva, CullWaitSig, live, differs) || !Holds(CullJobRva, CullJobSig, live, differs) ||
            !Holds(CullJobCopyRva, CullJobCopySig, live, differs) || !Holds(CullFrustumCopyRva, CullFrustumCopySig, live, differs) ||
            !Holds(CullJobTestRva, CullJobTestSig, live, differs) || !Holds(CullBoxRva, CullBoxSig, live, differs) ||
            !Holds(CullBoxFrontRva, CullBoxFrontSig, live, differs) || !Holds(CullBoxEndRva, CullBoxEndSig, live, differs) ||
            !Holds(ShadowFitCentreRva, ShadowFitCentreSig, live, differs) || !Holds(ShadowFitExtentRva, ShadowFitExtentSig, live, differs))
            return NotYet(cullOnce, differs);
        if (!HookIn(CullKickRva, CullKickSig, CullKickHookBytes, &CullKickHook, &originalCullKick))
            return Refuse(cullOnce, "the hook at RVA " + Hex(CullKickRva) + " was refused");
        cullHookIn = true;
    }
    cullPair = {};
    cullRelationOk = false;
    cullOn.store(true);
    cullOnce.state = State::Applied;
    Say("cull_once applied: the first eye pass of a pair culls with one volume that holds both eyes' views, and the second, when its own view is "
        "inside that volume, does not cull again (hook at RVA " + Hex(CullKickRva) + "; 12 stretches of the game's code proven first). The first pair "
        "after this line, every other draw, and a second eye outside the volume are culled as the game does");
}

void RemoveCull() {
    cullOn.store(false);
    cullOnce.state = State::Off;
    Say("cull_once off: its hook stays in the game's code and passes every call through");
}

// What the once-per-pair ones did, every ten seconds while any is on (draw thread, between frames).
void Tally() {
    static ULONGLONG last = 0;
    const ULONGLONG now = GetTickCount64();
    if (!last) { last = now; return; }
    if (now - last < 10000) return;
    last = now;
    if (shadowsOn.load(std::memory_order_relaxed)) {
        const uint32_t kept = shadowsKeptPasses.exchange(0), own = shadowsOwnPasses.exchange(0), kicks = shadowKicksOut.exchange(0),
                       collects = shadowCollectsOut.exchange(0), draws = shadowDrawsOut.exchange(0);
        char text[320];
        snprintf(text, sizeof text, "shadows_once 10 s: %u second passes kept the first's shadows (left out: %u caster culls, %u collects, %u shadow draws), "
                 "%u fitted and drew their own (no first pass of the pair seen, or a camera without a look-at point)", kept, kicks, collects, draws, own);
        Say(text);
    }
    if (cullOn.load(std::memory_order_relaxed)) {
        const uint32_t skipped = cullSkipped.exchange(0), outside = cullOutside.exchange(0), none = cullNoVolume.exchange(0);
        char text[320];
        snprintf(text, sizeof text, "cull_once 10 s: %u second passes not culled (inside the first's volume), %u culled their own "
                 "(%u outside the volume, %u with no volume from the first pass)", skipped, outside + none, outside, none);
        Say(text);
    }
}

// --- all of them ----------------------------------------------------------------------------------------------
struct Entry { Patch* patch; void (*apply)(); void (*remove)(); };
const Entry entries[] = {
    {&filter, &ApplyFilter, &RemoveFilter},
    {&account, &ApplyAccount, &RemoveAccount},
    {&names, &ApplyNames, &RemoveNames},
    {&selection, &ApplySelection, &RemoveSelection},
    {&shadows, &ApplyShadows, &RemoveShadows},
    {&cullOnce, &ApplyCull, &RemoveCull},
};

std::atomic<bool> dirty{false};       // the settings changed, or something still waits for the game's code
std::atomic<bool> configured{false};
bool firstFrame = true;
uint32_t frames = 0;

std::string Summary() {
    std::string all;
    for (const Entry& e : entries) {
        if (!all.empty()) all += ", ";
        all += std::string(e.patch->key) + " " + Name(e.patch->state);
    }
    return all;
}
}  // namespace

void SetLogger(void (*log)(const char*)) { g_log = log; }

void Configure(const Wanted& wanted) {
    const bool want[] = {wanted.transparentFilter, wanted.profilerAccounting, wanted.drawDebugNames,
                         wanted.selectionWalk, wanted.shadowsOnce, wanted.cullOnce};
    static_assert(std::size(want) == std::size(entries), "one wish per patch, in the entries' order");
    bool changed = false;
    for (size_t i = 0; i < std::size(entries); ++i)
        if (entries[i].patch->wanted.exchange(want[i]) != want[i]) changed = true;
    if (!configured.exchange(true)) {
        std::string all;
        for (size_t i = 0; i < std::size(entries); ++i) all += std::string(i ? ", " : "") + entries[i].patch->key + "=" + (want[i] ? "1" : "0");
        Say("[game_patches] " + all + " - what is 1 goes in between frames, from the first frame the game draws; each says so here (0 = the game's code as it is)");
        changed = true;
    }
    if (changed) dirty.store(true);
}

void EyePass(Pass pass) {
    passNow.store(uint8_t(pass), std::memory_order_relaxed);
    shadowsKept.store(false, std::memory_order_release);   // each pass decides anew, in its shadow update
    // What a first pass left is for the second of the same pair only. A draw that stands alone in between (it
    // runs the game's own update and cull) clears it in the hooks themselves.
    if (pass == Pass::First) { shadowPair = {}; cullPair = {}; }
}

void BetweenFrames(bool inWorld) {
    if (shadowsOn.load(std::memory_order_relaxed) || cullOn.load(std::memory_order_relaxed)) Tally();
    if (!dirty.load(std::memory_order_relaxed)) return;
    if (!configured.load()) return;
    // What waits for the game's code is looked for only in frames drawn in the world (code the world's draw runs may not be decrypted
    // before), and not every frame; what is taken out goes at once, in a menu too.
    const bool look = inWorld && (firstFrame || frames++ % LookEvery == 0);
    bool waiting = false;
    dirty.store(false);   // before the settings are read: a change after this point is seen on the next frame
    for (const Entry& e : entries) {
        Patch& p = *e.patch;
        const bool want = p.wanted.load();
        if (want && p.state == State::Off) { p.looks = 0; p.state = State::Waiting; }
        if (want && p.state == State::Waiting && look) e.apply();
        else if (!want && p.state == State::Applied) e.remove();
        else if (!want && p.state == State::Waiting) p.state = State::Off;
        if (p.state == State::Waiting) waiting = true;
    }
    if (firstFrame && inWorld) {
        firstFrame = false;
        Say("first frame in the world: " + Summary());
    }
    if (waiting || firstFrame) dirty.store(true);
}

}  // namespace gamepatches
