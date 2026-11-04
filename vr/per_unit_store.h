// per_unit_store.h - one memory per rig, behind one lock (header only; cleanroom/tests/per_unit_store_test.cpp).
//
// The skeleton hook runs on the game's job threads, several rigs at once. [body] walk_back / legs_rear kept ONE memory "for our own hero" and reset it
// whenever another rig came in (`if (m.self != self) m = WalkBackMem{}`): two rigs taking turns in it - the hero and the one posed along with him, or a co-op
// partner taken as ours for a moment (the 0.201.0 log's eyes 7.05 / 6.89 by turns) - on two threads freed its vectors under each other (0xC0000374 heap
// corruption in WalkBackMem::operator=, 0.193.2 and 0.201.0). Here each rig keeps its own, and With() runs the caller's work on it under the lock; a rig
// not seen for idleMs goes, and past cap the longest unseen.
#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <mutex>

template <class T>
class PerUnitStore {
public:
    explicit PerUnitStore(size_t cap = 8, uint64_t idleMs = 2000) : cap_(cap < 1 ? 1 : cap), idleMs_(idleMs) {}

    // fn(T&, bool fresh) on key's memory (made new - fresh - the first time, or after it was let go), under the lock. nowMs: the caller's clock.
    template <class Fn>
    void With(uintptr_t key, uint64_t nowMs, Fn&& fn) {
        std::lock_guard<std::mutex> lock(mu_);
        // (a time read before the lock may be older than one stamped since by another thread: not idle - unsigned, it read as ages)
        for (auto it = units_.begin(); it != units_.end();)
            it = it->first != key && nowMs > it->second.lastMs && nowMs - it->second.lastMs > idleMs_ ? units_.erase(it) : std::next(it);
        auto it = units_.find(key);
        const bool fresh = it == units_.end();
        if (fresh) {
            while (units_.size() >= cap_) {   // the longest unseen goes
                auto oldest = units_.begin();
                for (auto j = units_.begin(); j != units_.end(); ++j)
                    if (j->second.lastMs < oldest->second.lastMs) oldest = j;
                units_.erase(oldest);
            }
            it = units_.emplace(key, Entry{}).first;
        }
        if (nowMs > it->second.lastMs) it->second.lastMs = nowMs;
        fn(it->second.value, fresh);
    }

    size_t Size() {
        std::lock_guard<std::mutex> lock(mu_);
        return units_.size();
    }

private:
    struct Entry {
        T value{};
        uint64_t lastMs = 0;
    };
    std::mutex mu_;
    std::map<uintptr_t, Entry> units_;
    size_t cap_;
    uint64_t idleMs_;
};
