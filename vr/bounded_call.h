// bounded_call.h - a call that may block for good (the runtime's xrWaitFrame while another app holds the headset; xrBeginSession) made on a thread of its own and
// waited for with a limit, so that the game's draw thread never blocks on it. Pure C++ (tools/xr_busy_test.cpp).
//
// 2026-10-18, a player (0.168, PS VR2 on SteamVR): FlatVR through BodyWalk Lite, then back to Native: the headset stays empty and the game hangs until Task Manager.
// The draw thread sat in xrWaitFrame, which has no time limit, with the session lock held.
//
// Run(fn, ms, &out): if no call is running and no result waits, fn is started on the worker; then up to `ms` is waited for the call. Done: out has its result (taken: the next
// Run starts a new call). Pending: the call goes on in the background and a later Run - with any fn: it is not started while one runs - collects it. The worker is never joined: a
// call that does not return costs a thread and nothing else, and the runner can be dropped (a new session gets a new one).
#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace bounded {

enum class Wait { Done, Pending };

template <class T>
class Runner {
  public:
    Runner() : s_(std::make_shared<Shared>()) { std::thread(&Runner::Work, s_).detach(); }
    ~Runner() {
        { std::lock_guard<std::mutex> l(s_->m); s_->quit = true; }
        s_->cv.notify_all();   // (the worker leaves when its call returns, or at once if it is idle; Shared lives as long as the worker)
    }
    Runner(const Runner&) = delete;
    Runner& operator=(const Runner&) = delete;

    Wait Run(const std::function<T()>& fn, unsigned ms, T* out) {
        std::unique_lock<std::mutex> l(s_->m);
        if (!s_->hasJob && !s_->running && !s_->finished) {
            s_->fn = fn;
            s_->hasJob = true;
            s_->startedAt = std::chrono::steady_clock::now();
            s_->cv.notify_all();
        }
        s_->cv.wait_for(l, std::chrono::milliseconds(ms), [this] { return s_->finished; });
        if (!s_->finished) return Wait::Pending;
        if (out) *out = std::move(s_->result);
        s_->finished = false;
        return Wait::Done;
    }
    // A call has been started and its result not collected yet.
    bool Busy() const {
        std::lock_guard<std::mutex> l(s_->m);
        return s_->hasJob || s_->running || s_->finished;
    }
    // The call is still running (not finished): it could be stuck.
    bool Running() const {
        std::lock_guard<std::mutex> l(s_->m);
        return s_->hasJob || s_->running;
    }
    // ms since the running call was started (0 when none runs).
    double RunningMs() const {
        std::lock_guard<std::mutex> l(s_->m);
        if (!(s_->hasJob || s_->running)) return 0.0;
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s_->startedAt).count();
    }

  private:
    struct Shared {
        mutable std::mutex m;
        std::condition_variable cv;
        std::function<T()> fn;
        bool hasJob = false, running = false, finished = false, quit = false;
        T result{};
        std::chrono::steady_clock::time_point startedAt;
    };
    static void Work(std::shared_ptr<Shared> s) {
        std::unique_lock<std::mutex> l(s->m);
        for (;;) {
            s->cv.wait(l, [&s] { return s->hasJob || s->quit; });
            if (!s->hasJob) return;   // quit, nothing to run
            std::function<T()> f = std::move(s->fn);
            s->hasJob = false;
            s->running = true;
            l.unlock();
            T r = f();
            l.lock();
            s->result = std::move(r);
            s->running = false;
            s->finished = true;
            s->cv.notify_all();
            if (s->quit) return;   // nobody will collect it
        }
    }
    std::shared_ptr<Shared> s_;
};

}  // namespace bounded
