// fence_gate.h - [stereo] replay_fence_gate: the game sees a frame's fences pass only once our replay of that frame is done on the GPU.
//
// Crash batch 2026-11-12 (rows 329-335, six players, Native one pass): DEVICE_HUNG with the GPU inside the REPLAYED right eye's "Forward Pass"
// every time (DRED: 14 lists after "d2rvr replay copy list", draws 9 to 738), all at replay_ahead=2. The game signals its frame fence after the
// left pass's lists - on the GPU that comes before our replay of them - and frees or rewrites what the frame drew with once its CPU sees that
// fence pass: allocators (replay_alloc_guard caught that in 083f209c's log: "the game reset an allocator ... while our replay ... still ran"),
// and with them its upload ring (constants, light lists) and the descriptors it wrote for the frame. The replay then reads another frame's data:
// a light loop with a count read from garbage runs for seconds - the hang.
//
// The old remedy held the game's queue signals until the replay was queued ([stereo] replay_hold_signals, off since 2026-10: a held signal the
// GPU or the game waited for during an area load never came). This one leaves every GPU signal and wait as the game made them. Only the CPU's
// LOOK is gated: ID3D12Fence::GetCompletedValue says a value no higher than the one before the first signal whose replay still runs, and
// SetEventOnCompletion (and the device's SetEventOnMultipleFenceCompletion) fires the game's event only once that replay is done too. Only
// replays already on the queue gate (a wait for the frame being drawn - a flush at an area load - is never held behind work not yet submitted),
// and nothing is gated longer than give-up ms (1000): a bound on any wait we add.
//
// This file: the bookkeeping, pure (tools/native_replay_test.cpp). vr/fence_gate.cpp: the hooks on the fence and device vtables.
#pragma once

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

struct ID3D12Device;
struct ID3D12Fence;

namespace fencegate {

// -- the bookkeeping (pure: fences and our fence are opaque pointers; "done" is asked of the caller) ----------------------------------------------
class Gate {
public:
    static constexpr size_t kMaxEntries = 512;   // a few pairs' signals; the oldest go first past it (they gate no more)
    static constexpr int kMaxFences = 16;        // the game's fences ever noted (its frame fences: a handful)

    // A signal the game put on its direct or compute queue while its left pass was recorded.
    void Note(const void* fence, uint64_t v) {
        pending_.push_back({fence, v});
        if (!Tracked(fence) && nTracked_ < kMaxFences) tracked_[nTracked_++] = fence;
    }
    // The pass's replay is on the queue: those signals now wait for our fence `ours` to reach `r` too.
    void Bind(const void* ours, uint64_t r, double nowMs) {
        for (const auto& p : pending_) {
            if (entries_.size() >= kMaxEntries) { entries_.erase(entries_.begin()); ++dropped_; }
            entries_.push_back({p.first, p.second, ours, r, nowMs});
        }
        pending_.clear();
    }
    // The pass was not replayed (or the pair failed): its signals gate nothing.
    void Drop() { pending_.clear(); }

    bool Tracked(const void* fence) const {
        for (int i = 0; i < nTracked_; ++i)
            if (tracked_[i] == fence) return true;
        return false;
    }

    // What the game may see of `fence`, whose real completed value is `real`. done(ours) = our fence's completed value. Entries whose replay is
    // done (or older than giveUpMs) are let go here.
    template <class Done> uint64_t View(const void* fence, uint64_t real, double nowMs, double giveUpMs, Done done) {
        if (real == ~0ull) return real;   // the device removed: every fence at its end - said as it is
        Sweep(nowMs, giveUpMs, done);
        uint64_t low = ~0ull;
        for (const Entry& e : entries_)
            if (e.fence == fence && e.v <= real && e.v < low) low = e.v;
        return low == ~0ull ? real : std::min(real, low - 1);
    }
    // A wait for `fence` >= v: the replay it must wait for too (the latest one among the entries at or below v), or ours == nullptr for none.
    struct Need { const void* ours = nullptr; uint64_t r = 0; double bornMs = 0.0; };
    template <class Done> Need NeedFor(const void* fence, uint64_t v, double nowMs, double giveUpMs, Done done) {
        Sweep(nowMs, giveUpMs, done);
        Need n;
        for (const Entry& e : entries_)
            if (e.fence == fence && e.v <= v && (!n.ours || e.r > n.r)) n = {e.ours, e.r, e.bornMs};
        return n;
    }

    size_t Entries() const { return entries_.size(); }
    size_t Pending() const { return pending_.size(); }
    uint32_t GaveUp() const { return gaveUp_; }
    uint32_t Dropped() const { return dropped_; }

private:
    struct Entry { const void* fence; uint64_t v; const void* ours; uint64_t r; double bornMs; };
    template <class Done> void Sweep(double nowMs, double giveUpMs, Done done) {
        // one look at our fence per distinct (ours) in a sweep: entries are in replay order, so one value covers them
        const void* lastOurs = nullptr;
        uint64_t lastDone = 0;
        size_t w = 0;
        for (size_t i = 0; i < entries_.size(); ++i) {
            const Entry& e = entries_[i];
            if (e.ours != lastOurs) { lastOurs = e.ours; lastDone = done(e.ours); }
            const bool fin = lastDone >= e.r;
            const bool old = !fin && nowMs - e.bornMs > giveUpMs;
            if (old) ++gaveUp_;
            if (!fin && !old) entries_[w++] = e;
        }
        entries_.resize(w);
    }
    std::vector<std::pair<const void*, uint64_t>> pending_;
    std::vector<Entry> entries_;
    const void* tracked_[kMaxFences] = {};
    int nTracked_ = 0;
    uint32_t gaveUp_ = 0, dropped_ = 0;
};

// -- the hooks (vr/fence_gate.cpp) ------------------------------------------------------------------------------------------------------------------
// Hooks ID3D12Fence::GetCompletedValue / SetEventOnCompletion and ID3D12Device1::SetEventOnMultipleFenceCompletion (once; MinHook initialised by the
// caller). Every call passes straight through while the gate is off or the fence was never noted.
bool Install(ID3D12Device* dev);
bool Installed();
void SetOn(bool on);             // [stereo] replay_fence_gate (live)
bool On();
void SetGiveUpMs(double ms);     // the longest any look or wait is held back (default 1000)
// The draw thread, while the left pass is recorded: a signal of the game's (its direct / compute queue).
void NoteSignal(ID3D12Fence* fence, uint64_t v);
// The pass's replay is on the queue up to our fence's value r (its end-of-pair signal); or the pass went out without one.
void BindPass(ID3D12Fence* ours, uint64_t r);
void DropPass();
// A fence's completed value as it really is (our own looks: the stall guard, the tests).
uint64_t RealCompleted(ID3D12Fence* f);

struct Stats {
    uint32_t looks = 0;        // GetCompletedValue calls on a noted fence
    uint32_t heldLooks = 0;    // ... that said a lower value than the real one
    uint32_t events = 0;       // SetEventOnCompletion calls on a noted fence
    uint32_t heldEvents = 0;   // ... whose event waited for a replay (deferred, or blocked for a null event)
    uint32_t multi = 0;        // SetEventOnMultipleFenceCompletion calls that waited for a replay
    double maxHeldMs = 0.0;    // the longest an event was held back
    uint32_t gaveUp = 0;       // entries let go at give-up ms (the replay not done: a GPU in trouble)
};
Stats TakeStats();   // since the last call
}  // namespace fencegate
