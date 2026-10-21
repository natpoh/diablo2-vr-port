// melee_boxes.cpp - the state behind melee_boxes.h: one scene, fed by melee_touch's timer, read by the render thread.
// Nothing in here touches the game; with [debug] melee_boxes off nothing is kept and every call returns at once.

#include "melee_boxes.h"

#include <atomic>
#include <mutex>

namespace meleeboxes {
namespace {

std::atomic<bool> g_wanted{false};
std::atomic<LogSink> g_sink{nullptr};
std::atomic<uint32_t> g_markMs{kDefaultMarkMs};
std::atomic<bool> g_dirRed{true};
std::atomic<bool> g_describe{true};
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
void SetDescribeWanted(bool on) { g_describe.store(on); }

void SetMarkMs(uint32_t ms) { g_markMs.store(std::min(ms, kMaxMarkMs)); }

void Update(const Frame& f) {
    if (!Wanted()) return;
    char line[768];
    bool say = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        Frame g = f;
        g.markMs = g_markMs.load();   // the speed marks' time is the module's setting, not the feeder's
        g.directionColour = DirectionColour();   // ... and so is the colour of "not toward the monster"
        g_scene.Set(g);
        if (g_describe.load(std::memory_order_relaxed) && f.nowMs - g_lastLogMs >= kLogMs) {
            g_lastLogMs = f.nowMs;
            say = g_scene.Describe(line, sizeof line, f.nowMs);
        }
    }
    if (say)
        if (const LogSink sink = g_sink.load()) sink(line);
}

void Struck(uint32_t id, Kind how, uint64_t nowMs) {
    if (!Wanted()) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_scene.Struck(id, how, nowMs);
}

void Touched(const ContactMark& mark) {
    if (!Wanted()) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_scene.Touched(mark);
}

void SetDirectionRed(bool on) { g_dirRed.store(on); }
int DirectionColour() { return g_dirRed.load(std::memory_order_relaxed) ? kCRed : kCBlue; }

int Take(uint64_t nowMs, const float eyeWorld[3], float (*a)[4], float (*b)[4], int maxOut) {
    if (!Wanted() || maxOut <= 0) return 0;
    Capsule caps[kMaxCapsules];
    float upm;
    int n;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        n = g_scene.Capsules(nowMs, caps, std::min(maxOut, kMaxCapsules));
        upm = g_scene.Upm();
    }
    return n > 0 ? Pack(caps, n, upm, eyeWorld, a, b) : 0;
}

}  // namespace meleeboxes
