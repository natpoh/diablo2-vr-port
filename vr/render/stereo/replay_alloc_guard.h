// replay_alloc_guard.h - [stereo] native_replay: the game's command allocators, kept from a reset while our replay of their lists still runs.
//
// Crash batch 2026-11-10 (row 318, 0.210.1, RTX 4090, SteamVR): the first player report with [stereo] replay_reset_guard on. The guard's lead was
// right - from the first replayed pair the game had taken 2-5 of its ~40 left-pass lists up again before our replay (24 passes held back in 4 s) -
// and still the device hung (DXGI_ERROR_DEVICE_HUNG, the GPU in a draw of the game's Transparent Pass) 5 s after the last pass the guard held back:
// the passes in between had no list reset and were replayed.
//
// What the list guard cannot see: a list's commands live in its ALLOCATOR. The game resets an allocator as soon as its own frame fence says the GPU
// is done with the lists recorded in it - and that fence is signalled after the game's own submission, BEFORE our replay submits the same lists
// again (the replay runs at the game's present, later on the same queues). From that moment the game may reset the allocator and record into its
// memory while the GPU still reads the old commands from it for our replay: the replay runs on commands written over under it (a draw on garbage:
// the early-draw hangs of group A). The replay waited for its last replay before the next left pass (BeginLeft, WaitForReplayedAllocs) - right only
// if the game resets those allocators inside that pass, on the draw thread; not for a reset at the frame's start or on one of its recording threads.
//
// So, exactly where it matters - in front of the game's own ID3D12CommandAllocator::Reset (vr/native_d3d12.cpp hooks it for every list type):
//   - listAlloc: each list of the game's -> the allocator its last Reset (or CreateCommandList) recorded it into;
//   - gens: each allocator's count of resets, counted in front of the reset (the replay keeps the count when the game submits a list and holds the pass back, as the list guard
//     does, if the allocator was reset before the replay - its list's commands are not there any more);
//   - busy: each allocator -> the value our replay's fence reaches once the GPU is done with the replayed lists recorded in it. While the replay is
//     being submitted it is kPending (set before the first ExecuteCommandLists, the real value right after the last); a reset of the game's that
//     finds the GPU not there yet WAITS for it ([stereo] replay_alloc_guard; off: counted and told only). Bounded: a GPU that is not done in 2 s
//     is hung, the device is lost anyway.
//
// Tables as replay_list_gen.h: fixed, lock-free, open addressing on the address, an entry claimed once and never given back (lists and allocators
// live as long as the game; D3D12 handing an address out again reuses its entry - the values are overwritten by the new object's first Reset).
// Pure: tools/replay_alloc_guard_test.cpp.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "replay_list_gen.h"

namespace allocguard {

constexpr uint32_t kTable = 4096;     // a power of two
constexpr uint64_t kPending = ~0ull;  // busy: the replay is submitting lists recorded in it this moment (the draw thread, a few ms at most)

// address -> 64-bit value, lock-free.
struct Map {
    std::atomic<uintptr_t> key[kTable] = {};
    std::atomic<uint64_t> val[kTable] = {};
    std::atomic<uint32_t> full{0};   // sets that found no room

    static uint32_t Hash(uintptr_t p) { return (uint32_t)(((uint64_t)p >> 4) * 0x9E3779B97F4A7C15ull >> 52) & (kTable - 1); }
    int Slot(uintptr_t p, bool claim) {
        if (!p) return -1;
        for (uint32_t h = Hash(p), i = 0; i < kTable; ++i, h = (h + 1) & (kTable - 1)) {
            uintptr_t k = key[h].load(std::memory_order_acquire);
            if (k == p) return (int)h;
            if (k) continue;
            if (!claim) return -1;
            if (key[h].compare_exchange_strong(k, p, std::memory_order_acq_rel)) return (int)h;
            if (k == p) return (int)h;
        }
        return -1;
    }
    void Set(const void* k, uint64_t v) {
        const int s = Slot((uintptr_t)k, true);
        if (s < 0) { if (k) full.fetch_add(1, std::memory_order_relaxed); return; }
        val[s].store(v, std::memory_order_seq_cst);   // (seq_cst: the replay's mark and its look at the counts, against the reset's count and its look here)
    }
    uint64_t Get(const void* k) {   // 0: none
        const int s = Slot((uintptr_t)k, false);
        return s < 0 ? 0 : val[s].load(std::memory_order_seq_cst);
    }
    bool Full() const { return full.load(std::memory_order_relaxed) != 0; }
};

struct State {
    Map listAlloc;          // a game list -> the allocator it was last recorded into
    listgen::Table gens;    // a game allocator -> its count of resets
    Map busy;               // a game allocator -> our replay's fence value that frees it (kPending: being submitted)
    // what the gate found (all along; the replay's 10 s line takes the differences)
    std::atomic<uint32_t> resets{0};     // the game's allocator resets seen
    std::atomic<uint32_t> busyFound{0};  // ... of them while a replay of their lists had not finished on the GPU
    std::atomic<uint32_t> waited{0};     // ... waited for (the guard on) and freed in time
    std::atomic<uint32_t> timedOut{0};   // ... waited for and NOT freed in the limit (gone on anyway)
    std::atomic<uint32_t> waitMaxUs{0};  // the longest wait
};
inline State& Get() {
    static State s;
    return s;
}

// The list hooks: list l (re)recorded into allocator a - its Reset, or its making by CreateCommandList.
inline void NoteListRecorded(State& s, const void* l, const void* a) {
    if (l && a) s.listAlloc.Set(l, (uint64_t)(uintptr_t)a);
}
inline const void* AllocOf(State& s, const void* l) { return (const void*)(uintptr_t)s.listAlloc.Get(l); }
// The allocator's count of resets (0: none seen; ~0u: no room - counted as reset by Stale)
inline uint32_t AllocGen(State& s, const void* a) { return a ? s.gens.Gen(a) : 0u; }

// How many of the captured lists' allocators were reset since the capture: at(i) gives the allocator and the count it had then (nullptr: the list's
// allocator not known - not counted, the list guard alone speaks for it).
template <class At>
size_t StaleAllocs(State& s, size_t n, At at) {
    size_t stale = 0;
    for (size_t i = 0; i < n; ++i) {
        const void* a = nullptr;
        uint32_t then = 0;
        at(i, &a, &then);
        if (!a) continue;
        if (then == ~0u || s.gens.Gen(a) != then) ++stale;
    }
    return stale;
}

// The replay, about to submit lists again: their allocators marked pending (each once) - and THEN their counts looked at again (StaleAllocs): a
// reset counts first and looks at `busy` after (Before), so either the replay sees the count moved and holds the pass back, or the reset sees the
// mark and waits. Neither order lets a reset slip between the replay's look and its submission. `out` gets the allocators marked (the caller frees them with
// Release below); returns how many. at(i) gives the list.
template <class At, class Out>
size_t MarkPending(State& s, size_t n, At at, Out& out) {
    for (size_t i = 0; i < n; ++i) {
        const void* a = AllocOf(s, at(i));
        if (!a) continue;
        bool seen = false;
        for (const void* x : out)
            if (x == a) { seen = true; break; }
        if (seen) continue;
        out.push_back(a);
        s.busy.Set(a, kPending);
    }
    return out.size();
}
// ... submitted: the fence value that tells the GPU is done with them (0: nothing to wait for - the submission did not happen).
template <class Out>
void Release(State& s, const Out& marked, uint64_t doneAt) {
    for (const void* a : marked) s.busy.Set(a, doneAt);
}

enum class Gate {
    Free,      // no replay of ours ever ran a list recorded in it
    Done,      // our replay of its lists is done on the GPU
    Waited,    // it was not: waited for, and done within the limit
    TimedOut,  // waited for the limit: the reset goes on (the device is likely hung)
    Busy,      // it was not, and the guard is off: told, not waited
};

// In front of the game's ID3D12CommandAllocator::Reset (any thread). completed(v): the replay's fence reached v. wait(v, ms): wait for it, true if it
// came. pause(): a moment (the replay is submitting this allocator's lists right now). nowUs(): a clock. *waitedUs: how long it stood here.
template <class Completed, class Wait, class Pause, class NowUs>
Gate Before(State& s, const void* a, bool on, unsigned limitMs, Completed completed, Wait wait, Pause pause, NowUs nowUs, double* waitedUs) {
    if (waitedUs) *waitedUs = 0;
    s.resets.fetch_add(1, std::memory_order_relaxed);
    s.gens.NoteReset(a);   // counted FIRST (a locked add: a full fence), the mark read after - see MarkPending
    uint64_t v = s.busy.Get(a);
    if (!v) return Gate::Free;
    if (v != kPending && completed(v)) return Gate::Done;
    s.busyFound.fetch_add(1, std::memory_order_relaxed);
    if (!on) return Gate::Busy;
    const double t0 = nowUs(), limitUs = limitMs * 1000.0;
    while (v == kPending) {   // the draw thread is between the replay's first and last ExecuteCommandLists: its fence value comes next
        if (nowUs() - t0 > limitUs) { s.timedOut.fetch_add(1, std::memory_order_relaxed); return Gate::TimedOut; }
        pause();
        v = s.busy.Get(a);
    }
    bool ok = completed(v);
    if (!ok) {
        const double left = limitUs - (nowUs() - t0);
        ok = left > 0 && wait(v, (unsigned)(left / 1000.0) + 1);
    }
    const double took = nowUs() - t0;
    if (waitedUs) *waitedUs = took;
    uint32_t m = s.waitMaxUs.load(std::memory_order_relaxed);
    while (took > m && !s.waitMaxUs.compare_exchange_weak(m, (uint32_t)took, std::memory_order_relaxed)) {}
    if (!ok) { s.timedOut.fetch_add(1, std::memory_order_relaxed); return Gate::TimedOut; }
    s.waited.fetch_add(1, std::memory_order_relaxed);
    return Gate::Waited;
}
}  // namespace allocguard
