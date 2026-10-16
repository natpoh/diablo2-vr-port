#pragma once
// haptic_stop.h - [openxr] haptics_stop: every pulse is stopped by an explicit xrStopHapticFeedback when its time is up.
//
// Some runtimes keep a controller vibrating past the duration a pulse asked for (Steam Frame's controllers buzzed on after a
// 10 ms tap). A thread of its own waits for each hand's deadline and calls `stop(hand)` then - also while the frame loop is held
// up (a loading screen, a stall). A new pulse on a hand replaces its deadline, so the timeout of an older one never cuts a newer
// one short; Cancel() forgets every deadline (the session going); Stop() ends the thread. The callback runs on the timer's
// thread with no lock of ours held. A plugin keeps its Timer for the process's life (never destroyed under the loader's lock);
// Stop() is for tests and an orderly end. From bearstonem/d2r-pcvr (MIT): steamvr/haptic_pulse.h `Pulses`, rewritten here.
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

namespace hapticstop {

class Timer {
    using Clock = std::chrono::steady_clock;
    struct Hand { bool armed = false; Clock::time_point end{}; };
    std::array<Hand, 2> hands_{};
    std::mutex mutex_;
    std::condition_variable wake_;
    std::thread worker_;
    std::function<void(int)> stop_;
    bool stopping_ = false;

    void Run() {
        std::unique_lock<std::mutex> lock(mutex_);
        while (!stopping_) {
            Clock::time_point next = Clock::time_point::max();
            for (const Hand& h : hands_) if (h.armed && h.end < next) next = h.end;
            if (next == Clock::time_point::max()) wake_.wait(lock);
            else wake_.wait_until(lock, next);
            if (stopping_) break;
            const Clock::time_point now = Clock::now();
            for (int i = 0; i < 2; ++i) {
                if (!hands_[i].armed || now < hands_[i].end) continue;
                hands_[i].armed = false;
                lock.unlock();   // (the runtime's call without our lock: Arm may come meanwhile)
                stop_(i);
                lock.lock();
            }
        }
    }

public:
    ~Timer() { Stop(); }
    // Starts the thread once; `stop(hand)` is what a deadline calls (0 left, 1 right).
    bool Start(std::function<void(int)> stop) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (worker_.joinable()) return true;
        stop_ = std::move(stop);
        stopping_ = false;
        hands_ = {};
        try { worker_ = std::thread([this] { Run(); }); } catch (...) { return false; }
        return true;
    }
    // A pulse of `durationMs` was just played on `hand`: stop it then (replacing an earlier deadline of that hand).
    void Arm(int hand, uint32_t durationMs) {
        if (hand < 0 || hand > 1) return;
        std::lock_guard<std::mutex> lock(mutex_);
        if (!worker_.joinable() || stopping_) return;
        hands_[hand] = {true, Clock::now() + std::chrono::milliseconds(durationMs)};
        wake_.notify_one();
    }
    bool Armed(int hand) {
        std::lock_guard<std::mutex> lock(mutex_);
        return hand >= 0 && hand < 2 && hands_[hand].armed;
    }
    // Forget every deadline, calling nothing (the session is going: its handles must not be used any more).
    void Cancel() {
        std::lock_guard<std::mutex> lock(mutex_);
        hands_ = {};
        wake_.notify_one();
    }
    void Stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
            hands_ = {};
            wake_.notify_one();
        }
        if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id()) worker_.join();
    }
};

}  // namespace hapticstop
