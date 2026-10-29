// belt_glow.cpp - see belt_glow.h.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "belt_glow.h"
#include "log_async.h"
#include "potion_hide.h"
#include "sigscan.h"

using namespace D2RL;

namespace beltglow {
namespace {

// Addresses in 3.3.93787 (the keys of game_sigs.h).
constexpr uint64_t RVA_UPDATE_HIGHLIGHT = 0x784D60, RVA_HIGHLIGHT_STORAGE = 0x66CE30, RVA_TRANSPARENT_STORAGE = 0x66D730, RVA_ECS_REGISTRY = 0x2677170;
// mov rax,rsp; mov [rax+8],rcx; push rbp; push rbx; push rsi; push rdi; lea rbp,[rax-0B8h]
const uint8_t kSigUpdate[18] = {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x48, 0x08, 0x55, 0x53, 0x56, 0x57, 0x48, 0x8D, 0xA8, 0x48, 0xFF, 0xFF, 0xFF};
// the two storage getters: mov [rsp+20h],rbx; push rsi; push rdi; push r14; sub rsp,40h; mov rdi,rcx; call type_seq<T> (the call tells them apart)
const uint8_t kSigHighlight[21] = {0x48, 0x89, 0x5C, 0x24, 0x20, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B, 0xF9, 0xE8, 0xBB, 0x0E, 0x01, 0x00};
const uint8_t kSigTransparent[21] = {0x48, 0x89, 0x5C, 0x24, 0x20, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B, 0xF9, 0xE8, 0x7B, 0x07, 0x01, 0x00};
constexpr size_t kHighlightSize = 4;      // one float
constexpr size_t kTransparentSize = 0x2C; // opacity = min(+0, +4)

using UpdateFn = uintptr_t (*)(void* layer, void* a, void* b, void* c);   // (this; whatever else is passed goes on untouched, the result back)
using StorageFn = uintptr_t (*)(uintptr_t registry);

const PluginContext* g_ctx = nullptr;
UpdateFn g_orig = nullptr;
std::atomic<bool> g_on{false}, g_emptyOn{false}, g_faulted{false}, g_trace{false};
std::atomic<float> g_value{1.0f}, g_dark{-0.35f}, g_alpha{0.45f};
std::atomic<int> g_hook{0};          // 0 not tried, 1 in, -1 failed
std::atomic<int> g_storages{0};      // 0 not checked, 1 the game's, -1 not
// the entries the hook writes after the game's reset (highlight only)
std::atomic<uint32_t> g_ent[kMax] = {};
std::atomic<float> g_h[kMax] = {};
std::atomic<int> g_count{0};
// [debug] knock_props_glow: the props' entities, written by the hook too
std::atomic<uint32_t> g_propEnt[kMaxProps] = {};
std::atomic<int> g_propCount{0};
std::atomic<float> g_propH{0.0f};
std::atomic<bool> g_propsWanted{false};
std::atomic<unsigned> g_propWritten{0};
// the log's counts
std::atomic<unsigned> g_hookRuns{0}, g_hookWritten{0};
unsigned g_frames = 0, g_seenOurs = 0, g_seenZero = 0, g_seenOther = 0, g_noHighlight = 0, g_opacityWritten = 0, g_noOpacity = 0;   // the registry thread's
float g_seenOtherValue = 0.0f;

void Log(const char* text) { if (g_ctx) logasync::Info(g_ctx, text); }
template <typename... A> void LogF(const char* fmt, A... a) {
    char b[640];
    snprintf(b, sizeof b, fmt, a...);
    Log(b);
}

bool ReadAt(uintptr_t at, void* out, size_t n) noexcept {
    if (at < 0x10000) return false;
    __try { std::memcpy(out, reinterpret_cast<const void*>(at), n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool Storages() {
    if (g_storages.load() == 0) {
        const bool ok = d2rsig::Addr(RVA_HIGHLIGHT_STORAGE) && d2rsig::Addr(RVA_TRANSPARENT_STORAGE) && d2rsig::Addr(RVA_ECS_REGISTRY) &&
                        d2rsig::Check(RVA_HIGHLIGHT_STORAGE, kSigHighlight, sizeof kSigHighlight) &&
                        d2rsig::Check(RVA_TRANSPARENT_STORAGE, kSigTransparent, sizeof kSigTransparent);
        g_storages.store(ok ? 1 : -1);
        if (!ok) Log("belt glow: the game's highlight / transparency storages are not as in 3.3.93787 - no glow, no see-through emptied bottle");
    }
    return g_storages.load() == 1;
}

// The hook: after the game's reset and hover, the highlights again. -1 a fault.
int WriteHighlights(const uint32_t* ents, const float* h, int n) noexcept {
    __try {
        const uintptr_t storage = reinterpret_cast<StorageFn>(d2rsig::Addr(RVA_HIGHLIGHT_STORAGE))(d2rsig::Addr(RVA_ECS_REGISTRY));
        if (!storage) return 0;
        int done = 0;
        for (int i = 0; i < n; ++i) {
            uintptr_t el = 0;
            if (h[i] == 0.0f || !potionhide::StorageElement(&ReadAt, storage, ents[i], kHighlightSize, &el)) continue;
            *reinterpret_cast<volatile float*>(el) = h[i];
            ++done;
        }
        return done;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

uintptr_t HookUpdate(void* layer, void* a, void* b, void* c) {
    const uintptr_t r = g_orig(layer, a, b, c);
    if (g_faulted.load(std::memory_order_relaxed)) return r;
    const int np = std::min(g_propCount.load(std::memory_order_acquire), kMaxProps);
    if (np > 0) {   // [debug] knock_props_glow
        uint32_t pe[kMaxProps];
        float ph[kMaxProps];
        const float v = g_propH.load(std::memory_order_relaxed);
        for (int i = 0; i < np; ++i) { pe[i] = g_propEnt[i].load(std::memory_order_relaxed); ph[i] = v; }
        const int w = WriteHighlights(pe, ph, np);
        if (w < 0) {
            if (!g_faulted.exchange(true)) Log("belt glow: a fault writing the props' highlight - off for this session");
            return r;
        }
        g_propWritten.fetch_add((unsigned)w, std::memory_order_relaxed);
    }
    const int n = std::min(g_count.load(std::memory_order_acquire), kMax);
    if (n <= 0) return r;
    uint32_t ents[kMax];
    float h[kMax];
    for (int i = 0; i < n; ++i) { ents[i] = g_ent[i].load(std::memory_order_relaxed); h[i] = g_h[i].load(std::memory_order_relaxed); }
    const int w = WriteHighlights(ents, h, n);
    if (w < 0) {
        if (!g_faulted.exchange(true)) Log("belt glow: a fault writing the highlight - off for this session (the ball lights a bottle in reach again)");
        return r;
    }
    g_hookRuns.fetch_add(1, std::memory_order_relaxed);
    g_hookWritten.fetch_add((unsigned)w, std::memory_order_relaxed);
    return r;
}

// The registry's thread: read back what each carries (the log), then write. -1 a fault.
int WriteNow(const Entry* e, int n) noexcept {
    __try {
        const uintptr_t reg = d2rsig::Addr(RVA_ECS_REGISTRY);
        const uintptr_t hs = reinterpret_cast<StorageFn>(d2rsig::Addr(RVA_HIGHLIGHT_STORAGE))(reg);
        const uintptr_t ts = reinterpret_cast<StorageFn>(d2rsig::Addr(RVA_TRANSPARENT_STORAGE))(reg);
        for (int i = 0; i < n; ++i) {
            uintptr_t el = 0;
            if (hs && potionhide::StorageElement(&ReadAt, hs, e[i].entity, kHighlightSize, &el)) {
                const float was = *reinterpret_cast<volatile float*>(el);
                if (e[i].highlight != 0.0f) {
                    if (was == e[i].highlight) ++g_seenOurs;
                    else if (was == 0.0f) ++g_seenZero;
                    else { ++g_seenOther; g_seenOtherValue = was; }
                }
                *reinterpret_cast<volatile float*>(el) = e[i].highlight;
            } else if (e[i].highlight != 0.0f) {
                ++g_noHighlight;
            }
            if (e[i].opacity >= 0.0f) {
                uintptr_t t = 0;
                if (ts && potionhide::StorageElement(&ReadAt, ts, e[i].entity, kTransparentSize, &t)) {
                    reinterpret_cast<volatile float*>(t)[0] = e[i].opacity;
                    reinterpret_cast<volatile float*>(t)[1] = e[i].opacity;
                    ++g_opacityWritten;
                } else {
                    ++g_noOpacity;
                }
            }
        }
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

}  // namespace

void Init(const PluginContext* ctx) { g_ctx = ctx; }

void Configure(bool on, float value) {
    const bool was = g_on.exchange(on);
    g_value.store(std::isfinite(value) ? value : 1.0f);
    if (on != was) LogF("belt glow: %s (h %.2f: the bottle's colour x %.2f)", on ? "on - a potion in reach glows with the game's own highlight" : "off - the ball", g_value.load(),
                        1.0f + g_value.load());
}

void ConfigureEmpty(bool on, float dark, float alpha) {
    const bool was = g_emptyOn.exchange(on);
    g_dark.store(std::isfinite(dark) ? dark : 0.0f);
    g_alpha.store(std::isfinite(alpha) ? alpha : 1.0f);
    if (on != was && on) LogF("belt glow: an emptied bottle is drawn see-through (opacity %.2f) and darker (h %.2f: x %.2f) - the potions' glass has its colour painted in, no grey can be had per bottle",
                              g_alpha.load(), g_dark.load(), 1.0f + g_dark.load());
}

bool Wanted() { return g_on.load() && !g_faulted.load() && g_hook.load() == 1; }
void SetTrace(bool on) { g_trace.store(on); }

void Install() {
    if (g_hook.load() != 0 || !(g_on.load() || (g_emptyOn.load() && g_dark.load() != 0.0f) || g_propsWanted.load())) return;
    const bool ok = d2rsig::Addr(RVA_UPDATE_HIGHLIGHT) && Storages() &&
                    d2rsig::Hook(RVA_UPDATE_HIGHLIGHT, kSigUpdate, sizeof kSigUpdate, reinterpret_cast<void*>(&HookUpdate), reinterpret_cast<void**>(&g_orig));
    g_hook.store(ok ? 1 : -1);
    Log(ok ? "belt glow: the hook on the game's selection highlight (0x784D60) is in - a bottle in reach glows, an emptied one is darker"
           : "belt glow: the game's selection highlight is not where 3.3.93787 has it - no glow; the ball lights a bottle in reach");
}

void SetProps(const uint32_t* entities, int n, float h) {
    n = std::clamp(n, 0, kMaxProps);
    if (n > 0 && !g_propsWanted.exchange(true)) Log("belt glow: the knocked props' glow asked ([debug] knock_props_glow) - the hook on the game's highlight goes in for it");
    g_propCount.store(0, std::memory_order_release);
    g_propH.store(std::isfinite(h) ? h : 0.0f, std::memory_order_relaxed);
    for (int i = 0; i < n; ++i) g_propEnt[i].store(entities[i], std::memory_order_relaxed);
    g_propCount.store(g_hook.load() == 1 && !g_faulted.load() ? n : 0, std::memory_order_release);
    static ULONGLONG toldAt = 0;
    const ULONGLONG now = GetTickCount64();
    if (n > 0 && now - toldAt >= 10000) {
        toldAt = now;
        LogF("belt glow: knock props glow - %d props lit at h %.2f; the hook %s, %u highlight values written after the game's reset in the last ten seconds (0: the props' entities have "
             "no highlight component)", n, g_propH.load(), g_hook.load() == 1 ? "in" : g_hook.load() < 0 ? "FAILED" : "not in yet", g_propWritten.exchange(0));
    }
}

void SetEntries(const Entry* entries, int n) {
    n = std::clamp(n, 0, kMax);
    // the hook's copy (highlights only): what the game's reset takes away is written again after it
    g_count.store(0, std::memory_order_release);
    for (int i = 0; i < n; ++i) {
        g_ent[i].store(entries[i].entity, std::memory_order_relaxed);
        g_h[i].store(g_hook.load() == 1 ? entries[i].highlight : 0.0f, std::memory_order_relaxed);
    }
    g_count.store(n, std::memory_order_release);
    if (!n || g_faulted.load() || !Storages()) return;
    // and now, on the registry's own thread, the highlight and the opacity
    if (WriteNow(entries, n) < 0) {
        if (!g_faulted.exchange(true)) Log("belt glow: a fault writing the highlight / opacity - off for this session");
        return;
    }
    ++g_frames;
    static ULONGLONG toldAt = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - toldAt >= (g_trace.load() ? 1000u : 5000u)) {   // what was found and changed: does the value survive to the next frame, does the entity have the components
        toldAt = now;
        int lit = 0, empties = 0;
        float hl = 0.0f, he = 0.0f, op = -1.0f;
        for (int i = 0; i < n; ++i) {
            if (entries[i].lit) { ++lit; hl = entries[i].highlight; }
            else { ++empties; he = entries[i].highlight; op = entries[i].opacity; }
        }
        LogF("belt glow: %d entities (%d of lit bottles at h %.2f, %d of emptied ones at h %.2f, opacity %.2f) over %u frames; their highlight at the frame's start was ours %u times, "
             "0 %u times (the game's reset came after our last write), another value %u times (last %.2f); %u with no highlight component; opacity written %u times, %u with "
             "no transparency component; the hook ran %u times after the game's reset and wrote %u values",
             n, lit, hl, empties, he, op, g_frames, g_seenOurs, g_seenZero, g_seenOther, g_seenOtherValue, g_noHighlight, g_opacityWritten, g_noOpacity, g_hookRuns.exchange(0),
             g_hookWritten.exchange(0));
        g_frames = g_seenOurs = g_seenZero = g_seenOther = g_noHighlight = g_opacityWritten = g_noOpacity = 0;
    }
}

}  // namespace beltglow
