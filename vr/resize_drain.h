// resize_drain.h - [render] resize_drain: the GPU drained before the game frees its swap chain's buffers or its DLSS features
// (crash group E: DXGI_ERROR_DEVICE_HUNG with a page fault in a freed "Swap Chain" right after the game's Video Options were applied in VR).
//
// What went wrong (crash reports 267 and 273, 2026-11-06; 229 before them; martin039383 11-06 23:50): every one ran F4 with the right eye
// replayed from the left pass's lists ([stereo] replay_right=1, replay_ahead=2) and DLSS on. The left pass's present is held; the game then
// applies its new video options (frees its back buffers in ResizeBuffers, its DLSS features, its targets) and only after that does the
// replay run the left pass's lists again - lists recorded before the Apply, whose render-target views name the freed back buffers. DRED in
// both reports: the GPU inside "Post Task" (the frame's last list, drawn into the back buffer) at a DrawIndexedInstanced, followed by our
// depth copy, xr copy and monitor eye lists and DXGI's present - the tail of a replayed pair - and a page fault at an address where "Swap
// Chain" was freed just before. 273 was removed 6 ms after AcceptChanges. tools/resize_drain_test.cpp does the same on a real device: the
// left pass's list run again after a resize removes the device (WARP) - and shows DXGI's own ResizeBuffers waiting for lists already on its
// queue (the plain runtime; under the debug layer it did not), so a drain alone would not have saved these: the stale replay is the cause.
// A second road to the same fault: the game frees after waiting for its own frame fence, which passes BEFORE our replay of that frame on
// its queue ([stereo] replay_hold_signals is off: a held signal hung area loads).
//
// The cure, each step only with the tick on and the replay on (unticked: as before):
//   1. Before the game's ResizeBuffers / ResizeBuffers1, before it releases a DLSS feature (and the right eye's twin with it), and when its
//      Options panel sends AcceptChanges: the last replay waited for on its own fence (ours; 2 s at most), and inside ResizeBuffers also a
//      fence of ours on the swap chain's queue (alive for sure there) - nothing of ours still runs on what the game frees.
//   2. The pass being recorded when any of these come is not replayed (its lists were recorded against what is about to be freed): the
//      right eye of that one pair is the back buffer as it stands (a copy whose size no longer fits is left out).
//   3. The replay pauses as after an area change (vr/replay_pause.h: 240 settled pairs, two passes meanwhile), and the last replay is
//      waited for when it stops - the game makes its targets anew at the new size over the next frames.
// The right eye's DLSS twin and our own targets need no rebuild of ours: the twin is released with the game's feature and made again with
// the game's new one (vr/render/effects/effects.cpp, dlsseyes), the left picture's copy is made again at a new size (replay HoldLeft) and the
// headset's swapchains follow the picture's size (vr/xr.cpp EnsureChain). Why default 1: it only waits, at an Apply / a resize the game is
// about to wait for itself, and it changes no picture but the one pair's right eye; it never touches the game's mechanics.
//
// 2026-11-12 (crash group E2: reports 338, 340-342 - two passes, the replay OFF, the same "Post Task" fault after Apply): step 1 now in every
// render type (the last replay waited for only when one ran; the game's queue at the Apply, inside its resize, and at a DLSS release within 5 s
// of one), steps 2-3 still only with the replay on; each drain one log line ("resize drain - ..."). A drain cannot reach that crash itself:
// DXGI already waits for what is queued (tools/resize_drain_test.cpp A1 / A1b) and the faulting list went on the queue after the resize - the
// game's Apply resizes from inside its frame while our render size keeps its picture apart from its window: vr/apply_keep_size.h.
//
// Pure parts here (ReplayLists, Why); the D3D12 drain (QueueDrain) is header-only too, so the test runs it on a real device.
#pragma once

#include <cstdint>

namespace resizedrain {

// The pass's lists may be replayed: the tick off (as before), or no resize / Apply / DLSS release came while the pass was recorded.
inline bool ReplayLists(bool tick, uint32_t epochAtCapture, uint32_t epochNow) { return !tick || epochAtCapture == epochNow; }

// What made a drain, for the log (counted apart).
enum class Why { kResize = 0, kDlssRelease = 1, kApply = 2, kCount = 3 };
inline const char* WhyText(Why w) {
    switch (w) {
        case Why::kResize: return "the game's swap chain is resized";
        case Why::kDlssRelease: return "the game releases a DLSS feature";
        case Why::kApply: return "the game's video options were applied";
        default: return "?";
    }
}

// The plugin's one entry (vr/render/stereo/replay.cpp): the game is about to resize / release / apply. With the tick on and the replay on it
// counts the event (the pass now recorded is not replayed, the settle starts again) and waits for the last replay (and, in ResizeBuffers, for
// the swap chain's queue; 2 s at most); otherwise nothing.
// Any thread.
void Event(Why why);

}  // namespace resizedrain

#if defined(_WIN32) && defined(__d3d12_h__)
#include <windows.h>

#include "fence_wait.h"

namespace resizedrain {

// One fence of ours signalled on a queue and waited for on the CPU, ms at most. Thread-safe. Drain: true when everything queued before it has run (or the device is gone: a removed device's fences are at
// their end), false when the limit passed first - the caller goes on anyway (the GPU is likely hung; waiting for ever would only freeze).
class QueueDrain {
public:
    QueueDrain() { InitializeSRWLock(&lock_); }
    ~QueueDrain() { Release(); }
    QueueDrain(const QueueDrain&) = delete;
    QueueDrain& operator=(const QueueDrain&) = delete;

    bool Drain(ID3D12CommandQueue* q, unsigned ms, double* waitedMs = nullptr) {
        if (waitedMs) *waitedMs = 0.0;
        if (!q) return true;
        AcquireSRWLockExclusive(&lock_);
        if (!Ready(q)) { ReleaseSRWLockExclusive(&lock_); return false; }
        const uint64_t v = ++value_;
        if (FAILED(q->Signal(fence_, v))) { ReleaseSRWLockExclusive(&lock_); return false; }
        const double t0 = NowMs();
        ID3D12Fence* f = fence_;
        HANDLE e = event_;
        const bool done = fencewait::WaitValue(
            v, ms, [f] { return f->GetCompletedValue(); }, [f, e](uint64_t at) { f->SetEventOnCompletion(at, e); },
            [e](unsigned m) { WaitForSingleObject(e, m); }, [] { return NowMs(); });
        if (waitedMs) *waitedMs = NowMs() - t0;
        ReleaseSRWLockExclusive(&lock_);
        return done;
    }
    // For the GPU fault report's flight recorder: the value the last Drain signalled, and what the fence has reached now (0 before any)
    uint64_t Asked() const { return value_; }
    uint64_t Reached() const { return fence_ ? fence_->GetCompletedValue() : 0; }
    void Release() {
        if (fence_) { fence_->Release(); fence_ = nullptr; }
        if (dev_) { dev_->Release(); dev_ = nullptr; }
        if (event_) { CloseHandle(event_); event_ = nullptr; }
        value_ = 0;
    }

private:
    // the fence made on the queue's device (again when the queue is another device's)
    bool Ready(ID3D12CommandQueue* q) {
        ID3D12Device* d = nullptr;
        if (FAILED(q->GetDevice(__uuidof(ID3D12Device), (void**)&d)) || !d) return false;
        if (d != dev_) {
            Release();
            if (FAILED(d->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&fence_))) { d->Release(); return false; }
            dev_ = d;   // (the reference GetDevice gave)
            fence_->SetName(L"d2rvr resize drain fence");
        } else {
            d->Release();
        }
        if (!event_) event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        return event_ != nullptr;
    }
    static double NowMs() {
        static LARGE_INTEGER f = [] { LARGE_INTEGER x; QueryPerformanceFrequency(&x); return x; }();
        LARGE_INTEGER c;
        QueryPerformanceCounter(&c);
        return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
    }
    SRWLOCK lock_;
    ID3D12Device* dev_ = nullptr;
    ID3D12Fence* fence_ = nullptr;
    HANDLE event_ = nullptr;
    uint64_t value_ = 0;
};

}  // namespace resizedrain
#endif
