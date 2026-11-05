// aura_view.cpp - [render] aura_centre, the game's side: the Paladin's aura rings centred under the hero in VR (vr/aura_view.h says why and what is read).
// The hook of the game's view.direction function for particle scripts runs the original first, always; with the option off, or no fresh game camera (the flat game,
// menus, a view the game makes itself), it returns at once. PopcornFX calls it from its worker threads: no allocation, no lock of ours on the way, a log line at most
// every 5 s and 12 in a session.
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <D2RLPlugin/api.h>

#include "aura_view.h"
#include "log_async.h"
#include "sigscan.h"

using namespace D2RL;

namespace auraview {
namespace {

// D2R 3.3.93787 under D2RLoader 1.3.1 (the same addresses bearstonem/d2r-pcvr checked; tools/make_sigs.py "AuraViewDir" ...)
constexpr uint64_t RVA_VIEW_DIRECTION = 0xC04BB0;        // the script's view.direction (out stream, view-index stream, scene context), hooked
constexpr uint64_t RVA_VIEW_DIRECTION_VIEWS = 0xC04BE5;  // ... inside it: the views' list it reads (+0x310, count +0x318) - checked only
constexpr uint64_t RVA_INSTANCE_LOOKUP = 0xA91150;       // effect.position's instance lookup: the scene's pool at +0x230 - checked only (the layout we read)
const uint8_t kSigViewDirection[15] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18};
const uint8_t kSigViewDirectionViews[14] = {0x49, 0x8B, 0x80, 0x10, 0x03, 0x00, 0x00, 0x41, 0x8B, 0xA8, 0x18, 0x03, 0x00, 0x00};
const uint8_t kSigInstanceLookup[14] = {0x85, 0xD2, 0x78, 0x26, 0x48, 0x8B, 0x81, 0x30, 0x02, 0x00, 0x00, 0x44, 0x8B, 0xC2};

using DirectionFn = void (*)(void* out, const void* in, const void* context);
DirectionFn g_orig = nullptr;

const PluginContext* g_ctx = nullptr;
std::atomic<bool> g_on{false};
std::atomic<float> g_lift{0.0f};   // [render] aura_lift_pct / 100
std::atomic<int> g_state{0};   // 0 not in, 1 in, 2 not possible (another build?)
uint64_t g_base = 0;
// The game camera's direction (vrcam's view function, the render thread) - three floats and when; a torn read mixes two frames' directions of a camera that turns slowly.
std::atomic<float> g_fwd[3];
std::atomic<uint64_t> g_fwdAt{0};
std::atomic<uint32_t> g_written{0}, g_layoutMisses{0};
std::atomic<uint64_t> g_logAt{0};
std::atomic<int> g_logBudget{12};
std::atomic<bool> g_toldFirst{false}, g_toldMiss{false};

void Log(const char* text) { if (g_ctx) logasync::Info(g_ctx, text); }
template <typename... A> void LogF(const char* fmt, A... a) {
    char b[512];
    snprintf(b, sizeof b, fmt, a...);
    Log(b);
}

bool Rd(uint64_t at, void* out, size_t n) noexcept {
    if (at < 0x10000 || at >= 0x0000800000000000ull || n >= 0x10000) return false;
    __try { std::memcpy(out, reinterpret_cast<const void*>(at), n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Wr(uint64_t at, const void* in, size_t n) noexcept {
    if (at < 0x10000 || at >= 0x0000800000000000ull) return false;
    __try { std::memcpy(reinterpret_cast<void*>(at), in, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool IsAuraDef(uint64_t def) noexcept {
    char path[kPathCap];
    if (!DefinitionPath(&Rd, def, g_base, path)) {
        g_layoutMisses.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return Asset(path);
}

void Correct(void* output, const void* context) noexcept {
    const uint64_t at = g_fwdAt.load(std::memory_order_acquire), now = GetTickCount64();
    if (!Fresh(now, at)) return;
    Vec value;
    if (!Upright(Vec{g_fwd[0].load(std::memory_order_relaxed), g_fwd[1].load(std::memory_order_relaxed), g_fwd[2].load(std::memory_order_relaxed)}, g_lift.load(std::memory_order_relaxed), &value)) return;
    Stream out;
    Context c;
    if (!Rd(reinterpret_cast<uint64_t>(output), &out, sizeof out) || !StreamOk(out) || !Rd(reinterpret_cast<uint64_t>(context), &c, sizeof c)) return;
    thread_local Kinds kinds;
    // the native direction of the first lane written, for the log (read before it is replaced, only when a line is due)
    Vec native{};
    bool logDue = false;
    if (g_logBudget.load(std::memory_order_relaxed) > 0) {
        uint64_t last = g_logAt.load(std::memory_order_relaxed);
        logDue = now - last >= 5000 && g_logAt.compare_exchange_strong(last, now);
    }
    uint64_t firstDef = 0;
    auto rd = [](uint64_t a, void* o, size_t n) { return Rd(a, o, n); };
    auto wr = [&](uint64_t a, const void* in, size_t n) {
        if (logDue && !firstDef) Rd(a, &native, sizeof native);
        return Wr(a, in, n);
    };
    const uint32_t n = CorrectBatch(rd, wr, out, c, g_base, value, &kinds, now, [](uint64_t def) { return IsAuraDef(def); }, &firstDef);
    if (!n) return;
    g_written.fetch_add(n, std::memory_order_relaxed);
    if (!g_toldFirst.exchange(true))
        Log("aura view: the Paladin's aura effects get the game camera's vertical part without the sideways push toward the camera ([render] aura_centre, aura_lift_pct) - "
            "their rings sit at the hero's feet in VR (visual only)");
    if (logDue && g_logBudget.fetch_sub(1) > 0) {
        char path[kPathCap] = "?";
        if (!DefinitionPath(&Rd, firstDef, g_base, path)) std::snprintf(path, sizeof path, "?");
        LogF("aura view: view.direction (%.3f, %.3f, %.3f) -> (0, %.3f, 0) on %u lane%s of %s (%u lanes so far)", (double)native.x, (double)native.y, (double)native.z, (double)value.y, n,
             n == 1 ? "" : "s", path, g_written.load());
    }
}

void HookDirection(void* out, const void* in, const void* context) {
    g_orig(out, in, context);
    if (!g_on.load(std::memory_order_relaxed) || !out || !context) return;
    Correct(out, context);
}

}  // namespace

void Init(const PluginContext* ctx) { g_ctx = ctx; }

void Configure(bool on, float liftPct) {
    const float pct = ClampLiftPct(liftPct);
    const bool liftChanged = std::fabs(g_lift.exchange(pct / 100.0f) - pct / 100.0f) > 1e-4f;
    const bool was = g_on.exchange(on);
    if (on != was || (on && liftChanged))
        on ? LogF("aura view: on ([render] aura_centre) - the Paladin's aura rings are centred under the hero in VR: the game's push toward the camera loses its sideways part, "
                  "%.0f %% of its upward part kept (aura_lift_pct; 0 = the ring at the feet, where the flat game shows it)", (double)pct)
           : Log("aura view: off ([render] aura_centre=0) - the aura effects get the camera's direction as the game gives it");
    if (!on && was && g_layoutMisses.load() && !g_toldMiss.exchange(true))
        LogF("aura view: %u effect definitions did not read as 3.3.93787's (another build?) - those were left as the game made them", g_layoutMisses.load());
}

void SetGameView(const float gameView[16]) {
    if (!gameView) return;
    const Vec f = ForwardOfView(gameView);
    g_fwd[0].store(f.x, std::memory_order_relaxed);
    g_fwd[1].store(f.y, std::memory_order_relaxed);
    g_fwd[2].store(f.z, std::memory_order_relaxed);
    g_fwdAt.store(GetTickCount64(), std::memory_order_release);
}

void Install() {
    static ULONGLONG retryAt = 0;
    static unsigned tries = 0;
    if (!g_ctx || g_state.load() != 0 || !g_on.load()) return;
    const ULONGLONG now = GetTickCount64();
    if (now < retryAt) return;
    retryAt = now + (tries < 30 ? 2000 : 30000);   // the signature search may still be waiting for the code
    if (!d2rsig::Check(RVA_VIEW_DIRECTION, kSigViewDirection, sizeof kSigViewDirection) ||
        !d2rsig::Check(RVA_VIEW_DIRECTION_VIEWS, kSigViewDirectionViews, sizeof kSigViewDirectionViews) ||
        !d2rsig::Check(RVA_INSTANCE_LOOKUP, kSigInstanceLookup, sizeof kSigInstanceLookup)) {
        if (++tries == 30) {
            g_state.store(2);
            Log("aura view: the game's view.direction function is not where 3.3.93787 has it (another build?) - the aura rings are drawn as the game places them");
        }
        return;
    }
    g_base = reinterpret_cast<uint64_t>(GetModuleHandleW(nullptr));
    if (d2rsig::Hook(RVA_VIEW_DIRECTION, kSigViewDirection, sizeof kSigViewDirection, reinterpret_cast<void*>(&HookDirection), reinterpret_cast<void**>(&g_orig))) {
        g_state.store(1);
        Log("aura view: ready - the game's view.direction for particle scripts is hooked (the original always runs; only the Paladin's aura effects, "
            "data/hd/vfx/particles/overlays/paladin/, are changed, and only in VR views of the world)");
    } else if (++tries == 30) {
        g_state.store(2);
        Log("aura view: the hook could not be put in - the aura rings are drawn as the game places them");
    }
}

}  // namespace auraview
