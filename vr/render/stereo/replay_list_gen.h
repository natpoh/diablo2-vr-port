// replay_list_gen.h - [stereo] native_replay: was a command list the left pass submitted reset before its replay?
//
// Crash batch 2026-11-09 (rows 294-304, 0.208.0 and 0.209.0, the first releases with the replay on for everyone): seven of fourteen reports were the
// D3D12 device lost a few seconds to two minutes after the replay went on - 0x887A0006 DEVICE_HUNG with the GPU inside one of the game's early
// Forward Pass draws (rows 295, 297-299, 301, 302, 304), and once 0x887A0001 INVALID_CALL 90 ms after the replay's first pair (row 294; Serg's own
// PC once on 2026-11-08 09:26). The replay keeps the pointers of the lists the game submitted in its left pass and submits the same lists again
// after it. D3D12 lets a command list be Reset as soon as it has been submitted (only its allocator must wait for the GPU); a list the game took
// up again before our replay holds other commands by then - still open (ExecuteCommandLists on an open list removes the device: INVALID_CALL), or
// closed with a later recording whose upload data and resources are not the ones it was recorded with (a draw on garbage: a hang). Nothing checked.
//
// Each list's Reset is counted here (the plugin's own Reset hooks on the direct, compute and copy lists, vr/native_d3d12.cpp); the replay keeps the
// count each list had when the game submitted it and, before submitting it again, asks whether any count moved since. One did: that pair is not
// replayed (as a pass recorded across a resize, vr/resize_drain.h) and the log says so.
//
// A fixed table, lock-free (the game's five recording threads reset ~150 lists a pass): open addressing on the list's address, an entry claimed once
// with a compare-exchange and never given back (a list's address is the game's for its life; 4096 entries, the game keeps a few hundred lists). A
// table that is full counts every unseen list as reset (Full(): the guard then holds every pair back - safe, and told). Pure: tools/replay_list_gen_test.cpp.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace listgen {

constexpr uint32_t kTable = 4096;   // a power of two

struct Table {
    std::atomic<uintptr_t> key[kTable] = {};
    std::atomic<uint32_t> gen[kTable] = {};
    std::atomic<uint32_t> full{0};   // resets of lists that found no room

    static uint32_t Hash(uintptr_t p) { return (uint32_t)(((uint64_t)p >> 4) * 0x9E3779B97F4A7C15ull >> 52) & (kTable - 1); }

    // The entry of list p (claimed now if it has none); -1: the table is full.
    int Slot(uintptr_t p, bool claim) {
        if (!p) return -1;
        for (uint32_t h = Hash(p), i = 0; i < kTable; ++i, h = (h + 1) & (kTable - 1)) {
            uintptr_t k = key[h].load(std::memory_order_acquire);
            if (k == p) return (int)h;
            if (k) continue;
            if (!claim) return -1;
            if (key[h].compare_exchange_strong(k, p, std::memory_order_acq_rel)) return (int)h;
            if (k == p) return (int)h;   // another thread claimed it for the same list
        }
        return -1;
    }
    // A Reset of list p (any thread).
    void NoteReset(const void* list) {
        const int s = Slot((uintptr_t)list, true);
        if (s < 0) { if (list) full.fetch_add(1, std::memory_order_relaxed); return; }
        gen[s].fetch_add(1, std::memory_order_acq_rel);
    }
    // Its count of resets (0: never seen reset - a list made open by CreateCommandList and closed once). ~0u: no room for it.
    uint32_t Gen(const void* list) {
        const int s = Slot((uintptr_t)list, false);
        if (s >= 0) return gen[s].load(std::memory_order_acquire);
        return full.load(std::memory_order_relaxed) ? ~0u : 0u;
    }
    bool Full() const { return full.load(std::memory_order_relaxed) != 0; }
};

// The game's lists, one table for the plugin (the Reset hooks write it, the replay reads it).
inline Table& Lists() {
    static Table t;
    return t;
}

// How many of the captured lists were reset since they were captured: at(i) gives the list and the count it had then (gen ~0u = unknown: counted
// as reset). A list submitted twice in one pass is captured twice with the same count - it is reset only between passes, never within one.
template <class At>
size_t Stale(Table& t, size_t n, At at) {
    size_t stale = 0;
    for (size_t i = 0; i < n; ++i) {
        const void* list = nullptr;
        uint32_t then = 0;
        at(i, &list, &then);
        if (then == ~0u || t.Gen(list) != then) ++stale;
    }
    return stale;
}

}  // namespace listgen
