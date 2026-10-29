#pragma once
// log_async.h - [debug] log_async: the plugin's log lines through vr/log_queue.h. Info(ctx, text) is what every module writes a line with in place
// of ctx->LogInfo(text):
//   off (the default): ctx->LogInfo(text) on the calling thread, exactly as before;
//   on: the line is copied into a ring and written by a thread of its own (the host's LogInfo, in the order the lines were posted), so the
//       game's thread does not wait for the host's file. A full ring, or a pump that is not running: the caller writes the line itself.
// Live: Enable() switches it; Shutdown() (the plugin's unload) writes what waits and joins the thread; Drain() (a crash) writes what waits on
// the crashing thread, so the lines before the crash are in the log.
// [debug] log_async_auto (SetAuto): off, the lines a thread writes itself are timed; when the host's file turns out slow on this PC (vr/log_slow.h:
// half of the last 16 lines 5 ms or more - a player's 0.181 log had 29 ms a line), the writer thread takes them over for the rest of the run, as
// log_async on would. Off = as before.

#include <atomic>
#include <cstdio>
#include <mutex>

#include <D2RLPlugin/api.h>

#include "log_queue.h"
#include "log_slow.h"

namespace logasync {
namespace detail {
struct State {
    logqueue::Pump pump;
    std::atomic<bool> on{false};
    std::atomic<const D2RL::PluginContext*> ctx{nullptr};
    std::mutex switchLock;           // sw
    logslow::AutoSwitch sw;
    std::atomic<bool> timing{false};   // sw.Timing(), read without the lock on every line
};
// Never destroyed: at the process's end the writer may have been stopped in the middle of a line, with its lock held.
inline State& S() {
    static State* s = new State;
    return *s;
}
inline void Sink(void*, const char* text) {
    if (const D2RL::PluginContext* c = S().ctx.load()) c->LogInfo(text);
}
inline bool PumpOn(State& s, const D2RL::PluginContext* ctx) {
    if (!ctx) return false;
    s.ctx.store(ctx);
    if (!s.pump.Running() && !s.pump.Start(&Sink, nullptr)) return false;
    s.on.store(true);
    return true;
}
inline void PumpOff(State& s) {
    s.on.store(false);
    if (s.pump.Running()) s.pump.Drain(200);   // what waits is written; the thread stays, idle
}
// A line this thread wrote itself took `ms`: the verdict, and the writer thread on when it says slow.
inline void Timed(const D2RL::PluginContext* ctx, double ms) {
    State& s = S();
    logslow::Verdict v;
    bool fire = false;
    {
        std::lock_guard<std::mutex> lock(s.switchLock);
        fire = s.sw.Feed(ms, &v);
        s.timing.store(s.sw.Timing());
    }
    if (!fire) return;
    char line[400];
    if (PumpOn(s, ctx))
        std::snprintf(line, sizeof line, "vrcam: log_async on by itself - the log's file is slow on this PC: %d of the last %d lines took %.0f ms or more "
                      "to write (median %.1f ms, the slowest %.1f; about 1 ms is usual), each on the thread that wrote it - the game's among them. From now on a "
                      "thread of its own writes them, in order ([debug] log_async_auto; log_async=1 does it from the start)",
                      v.slowLines, logslow::kWindow, logslow::kSlowMs, v.medianMs, v.worstMs);
    else std::snprintf(line, sizeof line, "vrcam: log_async_auto - the log's file is slow on this PC (median %.1f ms a line), but the writer thread could not start: "
                       "the lines are written as before", v.medianMs);
    if (!(s.on.load() && s.pump.Post(line))) ctx->LogInfo(line);
}
}   // namespace detail

inline void Info(const D2RL::PluginContext* ctx, const char* text) {
    if (!ctx || !text) return;
    detail::State& s = detail::S();
    if (s.on.load(std::memory_order_relaxed) && s.pump.Post(text)) return;
    if (!s.timing.load(std::memory_order_relaxed)) {
        ctx->LogInfo(text);
        return;
    }
    const int64_t t0 = logqueue::NowNs();
    ctx->LogInfo(text);
    detail::Timed(ctx, (double)(logqueue::NowNs() - t0) * 1e-6);
}

// [debug] log_async. true: on or off as asked (a thread that would not start leaves it off - the lines go on being written by their own
// threads). Off while log_async_auto found the log slow: the writer thread stays (it is on by the verdict).
inline bool Enable(const D2RL::PluginContext* ctx, bool on) {
    detail::State& s = detail::S();
    bool wanted = false;
    {
        std::lock_guard<std::mutex> lock(s.switchLock);
        wanted = s.sw.SetExplicit(on);
    }
    if (wanted) return detail::PumpOn(s, ctx);
    detail::PumpOff(s);
    return true;
}
// [debug] log_async_auto: on = the lines a thread writes itself are timed until the verdict; off = no timing, and a writer thread that only the
// verdict had on stops.
inline void SetAuto(bool on) {
    detail::State& s = detail::S();
    bool stop = false;
    {
        std::lock_guard<std::mutex> lock(s.switchLock);
        stop = s.sw.SetAuto(on);
        s.timing.store(s.sw.Timing());
    }
    if (stop) detail::PumpOff(s);
}
inline bool Enabled() { return detail::S().on.load(std::memory_order_relaxed); }
inline bool AutoFired() {
    detail::State& s = detail::S();
    std::lock_guard<std::mutex> lock(s.switchLock);
    return s.sw.Fired();
}
inline void Drain(int tryMs) {
    detail::State& s = detail::S();
    if (s.pump.Running()) s.pump.Drain(tryMs);
}
inline void Shutdown() {
    detail::State& s = detail::S();
    {
        std::lock_guard<std::mutex> lock(s.switchLock);   // no verdict starts the thread again during the unload
        s.sw.SetAuto(false);
        s.timing.store(false);
    }
    s.on.store(false);
    s.pump.Stop();
    s.ctx.store(nullptr);
}

}   // namespace logasync
