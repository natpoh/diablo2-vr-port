// skill_triggers.cpp - the state behind skill_triggers.h: one scene, fed by shield_smite's timer, read by the render thread.
// Nothing in here touches the game; with [debug] skill_triggers off nothing is kept and every call returns at once.

#include "skill_triggers.h"

#include <atomic>
#include <mutex>

namespace skilltriggers {
namespace {

std::atomic<bool> g_wanted{false};
std::atomic<LogSink> g_sink{nullptr};
std::atomic<uint32_t> g_markMs{kDefaultMarkMs};
std::mutex g_mutex;
Scene g_scene;
uint64_t g_lastLogMs = 0;

}  // namespace

void SetLogSink(LogSink sink) { g_sink.store(sink); }

void SetWanted(bool on) {
    if (g_wanted.exchange(on) == on) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_scene = Scene{};   // either way: a clean start, nothing left over from the last time
    g_lastLogMs = 0;
}

bool Wanted() { return g_wanted.load(std::memory_order_relaxed); }

void SetMarkMs(uint32_t ms) { g_markMs.store(std::min(ms, kMaxMarkMs)); }

void UpdateBash(const BashFrame& f) {
    if (!Wanted()) return;
    char line[1200];
    bool say = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        BashFrame g = f;
        g.markMs = g_markMs.load();   // the marks' time is the module's setting, not the feeder's
        g_scene.SetBash(g);
        if (f.nowMs - g_lastLogMs >= kLogMs) {
            g_lastLogMs = f.nowMs;
            say = g_scene.DescribeBash(line, sizeof line, f.nowMs);
        }
    }
    if (say)
        if (const LogSink sink = g_sink.load()) sink(line);
}

void StruckBody(uint32_t id, meleeboxes::Kind how, uint64_t nowMs, const stroke::Body& body) {
    if (!Wanted()) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_scene.StruckBody(id, how, nowMs, body);
}

int Take(uint64_t nowMs, const float eyeWorld[3], float (*a)[4], float (*b)[4], int maxOut) {
    if (!Wanted() || maxOut <= 0) return 0;
    Capsule caps[meleeboxes::kMaxCapsules];
    float upm;
    int n;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        n = g_scene.Capsules(nowMs, caps, std::min(maxOut, kShare));
        upm = g_scene.Upm();
    }
    return n > 0 ? meleeboxes::Pack(caps, n, upm, eyeWorld, a, b) : 0;
}

}  // namespace skilltriggers
