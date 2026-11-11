#pragma once
// app_thread_pump.h - work for the game's APP thread, queued from any other thread (2026-11-11, the co-op JOIN crash).
//
// D2RLoader's network SDK asserts that connectToHost / getChannelInfo / sendToHost of a TCP/IP client are called on the game's app
// thread (ASSERT(Thread::IsAppThread()), client_sdk\src\export.cpp line 0x102; a failed assert ends the game with exit code
// 0xE0000001). The plugin's own 1 ms timer thread is not that thread. D2RLoader's ThreadService::runOnUiThread runs a callback "during
// a later root-widget update" - on the UI thread, which is the app thread (pose share and melee direct client have always made their
// channel calls inside such jobs and never asserted).
//
// The pump is the scheduling only (no windows, no D2RLoader - tested alone: cleanroom/tests/app_thread_pump_test.cpp):
//   - Post(slot, fn): from ANY thread. The closure is only stored (the latest one per slot wins: a state machine's tick asked again before the
//     app thread came is the same tick, newer) and one app-thread job is asked for through the submit function. fn is NEVER run by Post.
//   - Drain(token, tid): the job's body, on the app thread. Runs what is stored, records tid as the app thread's id.
//   - A job that never comes (D2RLoader drops queued work on a session change or an unload) is taken for lost after kLease and another is
//     queued (the same token rule as vr/ui_ticket.h: a late one finds its token gone and does nothing).

#include <cstdint>
#include <functional>
#include <mutex>

#include "ui_ticket.h"

namespace appthread {

enum Slot : int { kHostRules = 0, kHitShare, kAttackPathJoined, kAttackPathFront, kSlotCount };

class Pump {
public:
    // Queue "run Drain(token, thread id) on the app thread"; false when it could not be queued.
    using SubmitFn = bool (*)(uint32_t token);

    void SetSubmit(SubmitFn f) {
        std::lock_guard<std::mutex> lock(mu_);
        submit_ = f;
    }

    // Any thread. now: a millisecond clock (the lease's). true: the closure is stored and a job is queued or already on its way.
    bool Post(Slot slot, std::function<void()> fn, uint64_t now, uint32_t posterTid) {
        if (slot < 0 || slot >= kSlotCount || !fn) return false;
        uint32_t token = 0;
        SubmitFn submit = nullptr;
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (!submit_) return false;
            slots_[slot] = std::move(fn);
            have_ |= 1u << slot;
            if (!poster_) poster_ = posterTid;
            token = ticket_.Start(now);
            submit = submit_;
        }
        if (token && !submit(token)) {
            std::lock_guard<std::mutex> lock(mu_);
            ticket_.Claim(token);   // (not queued: the next Post asks again at once)
        }
        return true;
    }

    // The app thread's job. false: this token is not the one wanted (a late or a doubled run).
    bool Drain(uint32_t token, uint32_t tid) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (!ticket_.Claim(token)) return false;
            appTid_ = tid;
        }
        for (int i = 0; i < kSlotCount; ++i) {
            std::function<void()> fn;
            {
                std::lock_guard<std::mutex> lock(mu_);
                if (!(have_ & (1u << i))) continue;
                have_ &= ~(1u << i);
                fn = std::move(slots_[i]);
                slots_[i] = nullptr;
            }
            if (fn) fn();
        }
        return true;
    }

    // Unloading: nothing stored runs any more.
    void Clear() {
        std::lock_guard<std::mutex> lock(mu_);
        have_ = 0;
        for (auto& s : slots_) s = nullptr;
        submit_ = nullptr;
    }

    uint32_t AppTid() const {
        std::lock_guard<std::mutex> lock(mu_);
        return appTid_;
    }
    uint32_t PosterTid() const {
        std::lock_guard<std::mutex> lock(mu_);
        return poster_;
    }
    bool OnAppThread(uint32_t tid) const {
        std::lock_guard<std::mutex> lock(mu_);
        return appTid_ && tid == appTid_;
    }

private:
    mutable std::mutex mu_;
    SubmitFn submit_ = nullptr;
    uiticket::Ticket ticket_;   // (Start and Claim under mu_)
    std::function<void()> slots_[kSlotCount];
    uint32_t have_ = 0;
    uint32_t appTid_ = 0, poster_ = 0;
};

}  // namespace appthread
