#pragma once
// log_async.h - [debug] log_async: the plugin's log lines through vr/log_queue.h. Info(ctx, text) is what every module writes a line with in place
// of ctx->LogInfo(text):
//   off (the default): ctx->LogInfo(text) on the calling thread, exactly as before;
//   on: the line is copied into a ring and written by a thread of its own (the host's LogInfo, in the order the lines were posted), so the
//       game's thread does not wait for the host's file. A full ring, or a pump that is not running: the caller writes the line itself.
// Live: Enable() switches it; Shutdown() (the plugin's unload) writes what waits and joins the thread; Drain() (a crash) writes what waits on
// the crashing thread, so the lines before the crash are in the log.

#include <atomic>

#include <D2RLPlugin/api.h>

#include "log_queue.h"

namespace logasync {
namespace detail {
struct State {
    logqueue::Pump pump;
    std::atomic<bool> on{false};
    std::atomic<const D2RL::PluginContext*> ctx{nullptr};
};
// Never destroyed: at the process's end the writer may have been stopped in the middle of a line, with its lock held.
inline State& S() {
    static State* s = new State;
    return *s;
}
inline void Sink(void*, const char* text) {
    if (const D2RL::PluginContext* c = S().ctx.load()) c->LogInfo(text);
}
}   // namespace detail

inline void Info(const D2RL::PluginContext* ctx, const char* text) {
    if (!ctx || !text) return;
    detail::State& s = detail::S();
    if (s.on.load(std::memory_order_relaxed) && s.pump.Post(text)) return;
    ctx->LogInfo(text);
}

// true: on or off as asked (a thread that would not start leaves it off - the lines go on being written by their own threads).
inline bool Enable(const D2RL::PluginContext* ctx, bool on) {
    detail::State& s = detail::S();
    if (on) {
        if (!ctx) return false;
        s.ctx.store(ctx);
        if (!s.pump.Running() && !s.pump.Start(&detail::Sink, nullptr)) return false;
        s.on.store(true);
        return true;
    }
    s.on.store(false);
    if (s.pump.Running()) s.pump.Drain(200);   // what waits is written; the thread stays, idle
    return true;
}
inline bool Enabled() { return detail::S().on.load(std::memory_order_relaxed); }
inline void Drain(int tryMs) {
    detail::State& s = detail::S();
    if (s.pump.Running()) s.pump.Drain(tryMs);
}
inline void Shutdown() {
    detail::State& s = detail::S();
    s.on.store(false);
    s.pump.Stop();
    s.ctx.store(nullptr);
}

}   // namespace logasync
