#pragma once
// A run queued on the game's UI thread that may never come (2026-10-13).
//
// gamecmd's key presses, gamestate's read of the hands and its read of the name plate
// each queue one run at a time through D2RLoader's runOnUiThread: a flag set on queuing,
// cleared by the run. A run that never comes - work D2RLoader discards (its SDK's notes:
// a session change discards queued game work, an unload all of it), or a callback that
// faults before it clears the flag - left the flag set for the rest of the session and
// nothing queued again: BodyWalk's skill and potion keys dead, the hands and the weapon
// set no longer read, the plate frozen. Now each run carries a token of its own, held
// for kLease: one not come by then is taken for lost and another is queued, and one that
// comes late after all finds its token gone and does nothing - it cannot take the newer
// run's place (nor run twice).
//
// Start from one thread only (vrcam's timer), Claim on the UI thread.

#include <atomic>
#include <cstdint>

namespace uiticket {

constexpr uint64_t kLease = 1000;   // ms

class Ticket {
    std::atomic<uint32_t> pending_{0};   // the queued run's token, 0 none
    std::atomic<uint64_t> since_{0};     // when it was queued (GetTickCount64)
    uint32_t serial_ = 0;                // the last token handed out (Start's thread)
public:
    // A token for a run to queue now, else 0 (one is queued and its lease not over).
    // *lost: a queued run's lease ran out, this one takes its place.
    uint32_t Start(uint64_t now, bool* lost = nullptr) {
        if (pending_.load() && now - since_.load() < kLease) return 0;
        if (!++serial_) ++serial_;
        since_.store(now);
        const uint32_t old = pending_.exchange(serial_);
        if (lost) *lost = old != 0;
        return serial_;
    }
    // The run with this token came (or could not be queued): true if it is still the one wanted.
    bool Claim(uint32_t token) { return token && pending_.compare_exchange_strong(token, 0); }
};

inline void* ToUser(uint32_t token) { return (void*)(uintptr_t)token; }
inline uint32_t FromUser(void* user) { return (uint32_t)(uintptr_t)user; }

}  // namespace uiticket
