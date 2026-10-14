// Controller haptics: the mailbox the events post into, the frame loop's flush, and the observer of
// a landed hit. Logic that needs no game is in haptics_pattern.h (tested); the output action and
// xrApplyHapticFeedback are vr/xr.cpp's.
//
// The hit observer is READ-ONLY. The game's hit-flags function (0x44B720, "Flags" in
// bearstonem/d2r-pcvr, MIT: vr/melee.cpp) is called with every attack roll of the game's server;
// ours passes all six arguments on, takes what the game returned and hands it back untouched. It
// only looks at bit 0 of the answer (the blow landed - a block clears it) and at whether the
// attacker is a player unit; then it posts a pulse. No roll, range, target or damage is touched.

#include "haptics.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdio>

#include "sigscan.h"
#include "hit_latency.h"
#include "view_gate.h"
#include "frame_trace.h"   // [debug] frame_trace: what this observer costs a pair, and when a hit landed

// The gesture code's pulse (directinput::Haptic) is forwarded to Sink below once this branch meets the
// direct-input one: the header is there, the sink is registered; before that nothing refers to it.
#if __has_include("direct_input.h")
#include "direct_input.h"
#define HAPTICS_DIRECT_INPUT 1
#endif

namespace haptics {
namespace {

void (*g_log)(const char*) = nullptr;
void Log(const char* s) { if (g_log) g_log(s); }
template <class... A> void LogF(const char* fmt, A... a) {
    char b[512];
    snprintf(b, sizeof b, fmt, a...);
    Log(b);
}

std::atomic<bool> g_on{false};
std::atomic<float> g_scale{1.0f};
Mailbox g_box[2];
Limiter g_limiter;                       // the frame loop's (Flush)
Ratchet g_ratchet;                       // the bow's; guarded by g_ratchetLock (one thread in practice)
std::atomic_flag g_ratchetLock = ATOMIC_FLAG_INIT;
constexpr uint32_t kMaxAgeMs = 150;      // a pulse not played within this is a ghost

std::atomic<uint32_t> g_posted{0};       // bit per Event: posted at least once (the log tells it once)
std::atomic<uint32_t> g_hits{0};
uint32_t g_told = 0;                     // (the frame loop's)
uint32_t g_played = 0, g_limited = 0, g_dropped = 0, g_refused = 0, g_hitsTold = 0;
uint64_t g_statsAt = 0;

struct RatchetGuard {
    RatchetGuard() { while (g_ratchetLock.test_and_set(std::memory_order_acquire)) {} }
    ~RatchetGuard() { g_ratchetLock.clear(std::memory_order_release); }
};

// ---- the observer of a landed hit ----------------------------------------------------------------
// 0x44B720 in 3.3.93787 / D2RLoader 1.3.1 (found again by tools/make_sigs.py "HitFlags"): the server's
// (game, attacker, defender, accuracy, force, offset) -> hit flags. The call in the basic attack
// (0x42FEB0, returns to 0x430084) and in every skill that rolls to hit; bit 0 = the hit landed.
constexpr uint64_t RVA_HIT_FLAGS = 0x44B720;
const uint8_t kSigHitFlags[16] = {0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x56,0x57,0x41,0x55,0x41,0x56};
using HitFlagsFn = uint16_t (*)(uintptr_t game, uintptr_t attacker, uintptr_t defender, int accuracy, bool force, int offset);
HitFlagsFn OrigHitFlags = nullptr;
std::atomic<bool> g_hitIn{false};

// A unit's first dword is its type: 0 is a player (the SDK's UnitType::Player). The game reads the
// attacker itself right before, so the pointer is good; the guard is for a build that is not ours.
bool IsPlayerUnit(uintptr_t unit) noexcept {
    uint32_t type = 1;
    __try {
        type = *reinterpret_cast<volatile uint32_t*>(unit);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return type == 0;
}

uint16_t HookHitFlags(uintptr_t game, uintptr_t attacker, uintptr_t defender, int accuracy, bool force, int offset) {
    const uint16_t r = OrigHitFlags(game, attacker, defender, accuracy, force, offset);
    frametrace::Scope trace(frametrace::kHitHook);   // [debug] frame_trace: from here on it is ours
    const bool pulse = g_on.load(std::memory_order_relaxed), timed = hitlatency::Wanted(), traced = frametrace::On();
    if ((r & 1) && (pulse || timed || traced) && attacker > 0x10000 && IsPlayerUnit(attacker)) {
        if (pulse) {
            g_hits.fetch_add(1, std::memory_order_relaxed);
            Play(Event::Hit);
        }
        frametrace::Note(frametrace::kHit);   // [debug] frame_trace: a player's blow landed
        if (timed) hitlatency::Landed();   // the press -> hit times of hit by touch and the shield's Smite (vr/hit_latency.h): a clock read and atomics
    }
    return r;
}

}   // namespace

void SetLogger(void (*log)(const char*)) { g_log = log; }

void Sink(int hand, float amplitude, float durationMs, float /*frequencyHz*/) {
    Pulse(hand, amplitude, std::isfinite(durationMs) ? (int)std::clamp(durationMs, 0.0f, 1000.0f) : 0);
}

void Configure(bool on, float scale) {
#ifdef HAPTICS_DIRECT_INPUT
    static bool sinkSet = false;
    if (!sinkSet) { sinkSet = true; directinput::SetHapticSink(&Sink); }
#endif
    const float s = std::isfinite(scale) ? std::clamp(scale, 0.1f, 2.0f) : 1.0f;
    g_scale.store(s);
    if (on != g_on.exchange(on)) {
        if (!on) { g_box[0].Clear(); g_box[1].Clear(); }
        LogF("vrcam: haptics - %s ([openxr] haptics, strength %.2f)", on ? "on: pulses in the controllers" : "off", s);
    }
}
bool Enabled() { return g_on.load(std::memory_order_relaxed); }

void Pulse(int hand, float amplitude, int durationMs) {
    // F4 only (vr/view_gate.h): the controllers do not buzz in the other views (every event and the gestures' pulses come through here)
    if (!g_on.load(std::memory_order_relaxed) || hand < 0 || hand > 1 || !viewgate::F4()) return;
    const float a = Scaled(amplitude, g_scale.load(std::memory_order_relaxed));
    if (a <= 0.0f) return;
    g_box[hand].Post(a, ClampDuration(durationMs), (uint32_t)GetTickCount64());
}

void Play(Event e, int hand) {
    if (!g_on.load(std::memory_order_relaxed)) return;
    Resolved r[2];
    const int n = Expand(e, hand, r);
    for (int i = 0; i < n; ++i) Pulse(r[i].hand, r[i].amplitude, (int)r[i].durationMs);
    g_posted.fetch_or(1u << (int)e, std::memory_order_relaxed);
    if (e == Event::BowNock || e == Event::BowRelease) {
        RatchetGuard g;
        g_ratchet.Rearm(0.0f, (uint32_t)GetTickCount64());   // the count starts again from the string at rest
    }
}

void BowDraw(float draw, int stringHand) {
    if (!g_on.load(std::memory_order_relaxed) || !viewgate::F4()) return;
    float s = 0.0f, b = 0.0f;
    bool tick;
    {
        RatchetGuard g;
        tick = g_ratchet.Step(draw, (uint32_t)GetTickCount64(), &s, &b);
    }
    if (!tick) return;
    Pulse(stringHand == kLeft ? kLeft : kRight, s, 25);
    Pulse(Other(stringHand == kLeft ? kLeft : kRight), b, 20);
}

void Flush(uint64_t nowMs, bool canPlay, ApplyFn apply) {
    const uint32_t now32 = (uint32_t)nowMs;
    canPlay = canPlay && viewgate::F4();   // (a pulse that waits when the view changes is dropped, not played late)
    for (int h = 0; h < 2; ++h) {
        Request r;
        if (!g_box[h].Take(now32, kMaxAgeMs, &r)) continue;
        if (!canPlay || !apply || !g_on.load(std::memory_order_relaxed)) { ++g_dropped; continue; }
        if (!g_limiter.Allow(h, r.amplitude, r.durationMs, nowMs)) { ++g_limited; continue; }
        if (apply(h, r.amplitude, (int)r.durationMs)) ++g_played;
        else ++g_refused;
    }
    // The log: the first pulse of each kind, and a count every 10 s while there are any.
    if (const uint32_t fresh = g_posted.load(std::memory_order_relaxed) & ~g_told) {
        g_told |= fresh;
        for (int e = 0; e < kEventCount; ++e)
            if (fresh & (1u << e)) {
                const Pattern& p = PatternOf((Event)e);
                LogF("vrcam: haptics - the first \"%s\" pulse (%.2f for %u ms, %s hand by default)", p.name, p.part[0].amplitude, p.part[0].durationMs,
                     p.hand ? "right" : "left");
            }
    }
    if (nowMs - g_statsAt >= 10000) {
        const uint32_t hits = g_hits.load(std::memory_order_relaxed);
        if (g_played || g_limited || g_dropped || g_refused || hits != g_hitsTold)
            LogF("vrcam: haptics - last 10 s: %u pulses played, %u held back by the limiter, %u dropped (no input focus), %u refused by the runtime, %u hits landed",
                 g_played, g_limited, g_dropped, g_refused, hits - g_hitsTold);
        g_played = g_limited = g_dropped = g_refused = 0;
        g_hitsTold = hits;
        g_statsAt = nowMs;
    }
}

// Quiet: tried twice a second until the page is decrypted, says so once it is in (or never will be).
void Install() {
    static bool failed = false;
    if (g_hitIn.load() || failed || !(g_on.load() || hitlatency::Wanted() || frametrace::On())) return;   // haptics, or the press -> hit log of hit by touch / the shield (shared, one hook)
    if (!d2rsig::Check(RVA_HIT_FLAGS, kSigHitFlags, sizeof kSigHitFlags)) {
        if (d2rsig::NotInBuild(RVA_HIT_FLAGS)) {
            failed = true;
            Log("vrcam: haptics - the game's hit function was not found in this build: no pulse for a landed hit (the other pulses work)");
        }
        return;
    }
    const bool in = d2rsig::Hook(RVA_HIT_FLAGS, kSigHitFlags, sizeof kSigHitFlags, (void*)&HookHitFlags, (void**)&OrigHitFlags);
    g_hitIn.store(in);
    failed = !in;
    Log(in ? "vrcam: haptics - hit observer in (read-only: the game's hit function passes through untouched)"
           : "vrcam: haptics - hit observer FAILED to install: no pulse for a landed hit (the other pulses work)");
}

}   // namespace haptics
