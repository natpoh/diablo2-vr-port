// vr/swapchain_pick.h - which D3D12 swap chain is the game's, and the game's queue held by a counted reference (crash report 396, 2026-11-21).
//
// Report 396: a player with seven other D2RLoader plugins. One of them made a 64x64 swap chain on a queue of its own at the start - an overlay
// plugin's probe for the Present address, released right after. vr/native_d3d12.cpp's Took() took EVERY swap chain made as the game's: the probe
// came second, so the game's queue pointer became the probe's (a raw pointer, no reference). The next resize drain (vr/render/stereo/replay.cpp,
// resizedrain::QueueDrain) signalled on that freed queue: an access violation in QueueDrain::Drain (the queue's vtable gone).
// Two guards, either one alone would have kept 396 alive:
//   1. Adopt(): a swap chain no bigger than a probe (both sides under kProbeSide) is never taken as the game's, whether the game's came first or
//      not - the game's swap chain is its window's size (1920x1057 in 396, 1347x1347 and up with D2R VR's square window).
//   2. HeldRef: the game's queue held with a reference of ours (AddRef), the one before it released only at the next change after that - so a
//      thread that read the pointer a moment before a change still has a live queue for its call.
// Pure (the reference counting goes through the T's own AddRef / Release): tools/swapchain_pick_test.cpp.
#pragma once

#include <atomic>

namespace swapchainpick {

// A swap chain's back buffers both under this many pixels a side: a probe (overlays make 1x1 .. 100x100 ones on a hidden window), never the game's.
constexpr unsigned kProbeSide = 256;

enum class Pick {
    Adopt,       // the game's (the first real one, or the game's own made again: a mode change, a new window)
    LeaveProbe,  // another program's probe: its functions hooked as any (shared vtable), but never the one told, its queue never kept
};

inline bool IsProbe(unsigned w, unsigned h) { return w < kProbeSide && h < kProbeSide; }

// w, h: its back buffers as DXGI says after the creation (a 0 in the description becomes its window's client size). A probe is left alone whether
// or not the game's came first.
inline Pick Adopt(unsigned w, unsigned h) { return IsProbe(w, h) ? Pick::LeaveProbe : Pick::Adopt; }

// The game's queue with a reference of ours. Store(): AddRef the new one; the one it replaces is kept until the next Store() (released then), so a
// pointer read by another thread just before a change stays valid through that thread's call. Get(): the current one (no reference given out).
// T: anything with AddRef() / Release() (ID3D12CommandQueue; a counting fake in the test). Store() from one thread at a time (the swap chain's
// creation runs on the game's thread); Get() from any.
template <class T>
class HeldRef {
public:
    HeldRef() = default;
    HeldRef(const HeldRef&) = delete;
    HeldRef& operator=(const HeldRef&) = delete;
    ~HeldRef() = default;   // (left held at the process's end: its device goes with the process)

    void Store(T* p) {
        T* const now = cur_.load();
        if (p == now) return;
        if (p) p->AddRef();
        cur_.store(p);
        if (prev_) prev_->Release();   // the one before the one replaced now: nobody can still be inside a call on it
        prev_ = now;
    }
    T* Get() const { return cur_.load(); }
    // Both released (tests; a device made again from scratch could call it)
    void Clear() {
        T* const now = cur_.exchange(nullptr);
        if (now) now->Release();
        if (prev_) prev_->Release();
        prev_ = nullptr;
    }

private:
    std::atomic<T*> cur_{nullptr};
    T* prev_ = nullptr;
};

}  // namespace swapchainpick
