// chain_reaper.h - a swapchain of ours made again in a session (another size, a new pointer shape, a picture that failed) is not destroyed where it stops being used,
// but later, at a frame's start, once the game's queue was found drained. Pure C++ (tools/chain_reaper_test.cpp).
//
// 2026-10-22 17:48, 0.175 test-0176-touch on Virtual Desktop (VDXR 1.0.10, Quest 3): the game died with DXGI_ERROR_DEVICE_HUNG 377.9 s after load. At 17:47:54
// the world went away (a menu or an area change), the first flat picture since the back buffer had grown from 1920 to 2048 had its swapchain made again, and the
// draw thread - the game's present - never came back: the watchdog's stack (d2r_vr_hang.txt) has it inside xrDestroySwapchain (d2rl-vrcam.dll+0x250D75 is the
// loader's xrDestroySwapchain trampoline, called from EnsureChain). Virtual Desktop's xrDestroySwapchain flushes the app's queue first (flushD3D12CommandQueue:
// a Signal on the game's queue, then WaitForSingleObject(INFINITE)): it waits for everything the game has put on its queue. Our own fences were all done; the
// game's queue never got there, and the driver took the device 14 s later. The game's present thread must never sit on a wait for its own queue with no limit.
//
// So: the swapchain is handed here (Add) and the frame goes on without it. At the start of a later frame (Due), the caller puts a fence on the game's queue and
// waits a short, bounded time for it: drained - nothing is left for the runtime's flush to wait for, and the retired swapchains are destroyed then (Sweep);
// not drained - nothing is destroyed, the game goes on, and the next try is retryMs later. Retirements are gathered (batchMs / batchN), so a burst of new
// pointer shapes pays one drain, not one each. At the session's end everything left goes (All), as before.
//
// 2026-10-27 10:37:06, PC1 on Virtual Desktop (VDXR 1.0.10), an area change: the game made its mouse pointer anew every ~500 ms, a pointer swapchain made and
// one retired each time, and three, then two, destroyed after a drain - 500 ms after the youngest was retired. 188 ms after the last of them the device was
// removed (DXGI_ERROR_DEVICE_HUNG): DRED's page fault at an address with nothing there, five "Swap Chain" resources freed there just before - those five. The
// game's queue was drained, so what still read one was the runtime's own work after the destroy. Now each one has waited minAgeMs before it goes (a younger
// one is kept for a later drain), and vr/pointer_cache.h keeps a pointer image by what it shows, so the same picture never makes a swapchain again.
#pragma once

#include <cstddef>
#include <vector>

namespace chainreap {

// 2026-10-28, a player on SteamVR (Pico 4 over Virtual Desktop, RTX 5070 Ti, the headset's recommended 2764x2780 an eye): a 20 ms drain never came back
// (the GPU's frame is longer than that at this size), so nothing retired was ever destroyed - "2 retired swapchain(s) have waited 10 s ... kept" - and with
// every new pointer shape and menu panel another swapchain, until SteamVR said XR_ERROR_LIMIT_REACHED: the inventory opened with no panel, the pointer gone,
// once the main picture black. At 1536 a pair the drain came back. So the wait grows with each drain that ran out (DrainMs: doubled, up to drainMaxMs,
// [openxr] reap_drain_max_ms) and is drainMs again once one came back: still destroyed only when the queue drained, never under it.
struct Limits {
    double drainMs = 20.0;      // the first drain's wait for the game's queue
    double drainMaxMs = 20.0;   // the longest it grows to after drains that ran out (= drainMs: the same wait every time, as before)
    double batchMs = 1000.0;   // the oldest has waited this long: try (a picture's memory is not held long)
    std::size_t batchN = 8;    // or this many wait: try at once
    double retryMs = 250.0;    // after a drain that ran out, the next try (the game is not made to stand in every present)
    double retryAtMaxMs = 2000.0;   // ... after one that waited drainMaxMs (above drainMs) and ran out: a queue that does not drain is not waited on 4 times a second
    double slowMs = 10000.0;   // the oldest has waited this long: said once (the queue has not been idle for that long)
    double minAgeMs = 3000.0;  // none goes younger than this: what the runtime still does with its images after our last frame with it is long done
};

template <class H>
class Reaper {
  public:
    // A swapchain no longer used by us (a null handle is nothing).
    void Add(H h, double nowMs) {
        if (h) items_.push_back(Item{h, nowMs});
    }
    std::size_t Size() const { return items_.size(); }
    double OldestAgeMs(double nowMs) const { return items_.empty() ? 0.0 : nowMs - items_.front().at; }

    // Whether this frame should pay a drain: something waits, the oldest waited batchMs or batchN wait, the oldest is minAgeMs old, and the last drain that
    // ran out is retryMs old.
    bool Due(double nowMs, const Limits& l) const {
        if (items_.empty() || nowMs < nextTryAt_ || OldestAgeMs(nowMs) < l.minAgeMs) return false;
        return items_.size() >= l.batchN || OldestAgeMs(nowMs) >= l.batchMs;
    }

    // How long this drain waits for the game's queue: drainMs, doubled for each drain in a row that ran out, at most drainMaxMs (never under drainMs).
    double DrainMs(const Limits& l) const {
        const double top = l.drainMaxMs > l.drainMs ? l.drainMaxMs : l.drainMs;
        double ms = l.drainMs;
        for (unsigned i = 0; i < ranOut_ && ms < top; ++i) ms *= 2.0;
        return ms < top ? ms : top;
    }
    unsigned RanOut() const { return ranOut_; }

    // The drain's outcome. Drained: every one minAgeMs old is handed to destroy (oldest first) and the count returned; the younger wait on. Not: none, and no
    // try before retryMs.
    template <class F>
    std::size_t Sweep(bool drained, double nowMs, const Limits& l, F&& destroy) {
        if (!drained) {
            const bool atMost = l.drainMaxMs > l.drainMs && DrainMs(l) >= l.drainMaxMs;   // the longest wait ran out too: not paid every retryMs
            nextTryAt_ = nowMs + (atMost ? l.retryAtMaxMs : l.retryMs);
            ++ranOut_;
            return 0;
        }
        ranOut_ = 0;
        std::size_t n = 0, keep = 0;
        for (const Item& i : items_) {
            if (nowMs - i.at >= l.minAgeMs) { destroy(i.h); ++n; }
            else items_[keep++] = i;
        }
        items_.resize(keep);
        nextTryAt_ = 0.0;
        if (items_.empty()) told_ = false;
        return n;
    }

    // The session's end (the caller waited for the queue as it could): every one goes, drained or not.
    template <class F>
    std::size_t All(F&& destroy) {
        const std::size_t n = items_.size();
        for (const Item& i : items_) destroy(i.h);
        items_.clear();
        nextTryAt_ = 0.0;
        told_ = false;
        ranOut_ = 0;
        return n;
    }

    // True once when the oldest has waited slowMs (for the log), again only after the list was emptied.
    bool TellSlow(double nowMs, const Limits& l) {
        if (told_ || items_.empty() || OldestAgeMs(nowMs) < l.slowMs) return false;
        told_ = true;
        return true;
    }

  private:
    struct Item { H h; double at; };
    std::vector<Item> items_;
    double nextTryAt_ = 0.0;
    bool told_ = false;
    unsigned ranOut_ = 0;   // drains in a row that ran out
};

}  // namespace chainreap
