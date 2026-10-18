#pragma once
// hit_claim.h - whose blow just landed: the controller that made it, so the buzz goes to that hand. Pure (atomics only), tested (cleanroom/tests/hit_claim_test.cpp).
//
// The game's hit-flags function (vr/haptics.cpp's observer) only says "a player's blow landed"; it does not say which hand swung. Whoever ASKS the game to attack knows - hit by touch
// (the controller whose blade touched the monster, either hand: a Barbarian's two blades, a weapon drawn into the left hand), the shield's bash (the arm that wears the shield) -
// and says so at the moment it asks (Press, with the skeleton side: 0 right, 1 left). The blow lands 24 - 53 ms later (253 at the most) in the user's logs, on the game's server
// thread, which then asks Take: the oldest claim still waiting for its blow is the one it answers. So two hands that strike within a few frames each get their own buzz, in the
// order they asked.
//
// A claim, once answered, stays for kAfterMs more: one swing or Smite can roll more than once, and those rolls are the same hand's. A claim nobody answers within kWindowMs is
// gone (the game refused the attack, it missed, it was a thrown weapon still in the air) - the next blow is then nobody's claim, and the caller falls back to the hand that holds the
// weapon. Times are milliseconds on one steady clock (GetTickCount64), > 0. Press comes from the plugin's own threads, Take from the game's: lock-free.

#include <algorithm>
#include <atomic>
#include <cstdint>

namespace hitclaim {

constexpr int64_t kWindowMs = 400;   // a press -> the game's hit: 24 - 53 ms measured, 253 at the most
constexpr int64_t kAfterMs = 120;    // after a blow was given to a claim, another roll of the same swing within this is the same hand's
constexpr int kSlots = 4;            // claims waiting at once (two hands, a bash, one to spare)

class Claims {
    std::atomic<uint64_t> slot_[kSlots] = {};   // 0 free; else until (ms) << 8 | 4 answered | 1 right, 2 left

    static uint64_t Pack(int side, bool answered, int64_t until) {
        return (static_cast<uint64_t>(until) << 8) | (answered ? 4u : 0u) | (side == 0 ? 1u : 2u);
    }
    static int SideOf(uint64_t v) { return (v & 3u) == 1u ? 0 : 1; }
    static bool Answered(uint64_t v) { return (v & 4u) != 0; }
    static int64_t Until(uint64_t v) { return static_cast<int64_t>(v >> 8); }

public:
    // A blow was asked of the game by `side` (0 right, 1 left). Another side than those is the left (the shield's, the game's own).
    void Press(int side, int64_t nowMs) {
        if (nowMs <= 0) return;
        const uint64_t mine = Pack(side == 0 ? 0 : 1, false, nowMs + kWindowMs);
        for (int tries = 0; tries < 8; ++tries) {
            int pick = -1;
            uint64_t pv = 0;
            for (int i = 0; i < kSlots; ++i) {   // a free slot, else an expired one, else the oldest
                const uint64_t v = slot_[i].load(std::memory_order_acquire);
                if (!v || Until(v) < nowMs) { pick = i; pv = v; break; }
                if (pick < 0 || Until(v) < Until(pv)) { pick = i; pv = v; }
            }
            if (slot_[pick].compare_exchange_strong(pv, mine, std::memory_order_acq_rel)) return;
        }
    }

    // A player's blow landed (the observer, the game's server thread): the side that asked for it, or -1 when nobody did.
    int Take(int64_t nowMs) {
        for (int tries = 0; tries < 8; ++tries) {
            int best = -1;
            uint64_t bv = 0;
            for (int i = 0; i < kSlots; ++i) {   // the oldest claim still waiting for its blow
                const uint64_t v = slot_[i].load(std::memory_order_acquire);
                if (!v || Answered(v) || Until(v) < nowMs) continue;
                if (best < 0 || Until(v) < Until(bv)) { best = i; bv = v; }
            }
            if (best < 0) break;
            const int64_t closed = std::min(Until(bv), nowMs + kAfterMs);
            if (slot_[best].compare_exchange_strong(bv, Pack(SideOf(bv), true, closed), std::memory_order_acq_rel)) return SideOf(bv);
        }
        int latest = -1;
        uint64_t lv = 0;
        for (int i = 0; i < kSlots; ++i) {   // none waiting: another roll of a blow just given - the claim answered last
            const uint64_t v = slot_[i].load(std::memory_order_acquire);
            if (!v || !Answered(v) || Until(v) < nowMs) continue;
            if (latest < 0 || Until(v) > Until(lv)) { latest = i; lv = v; }
        }
        return latest >= 0 ? SideOf(lv) : -1;
    }

    void Clear() {
        for (auto& s : slot_) s.store(0, std::memory_order_release);
    }
};

// The plugin's one ledger: the sources Press into it, the hit observer Takes.
inline Claims& Global() {
    static Claims c;
    return c;
}
inline void Press(int side, int64_t nowMs) { Global().Press(side, nowMs); }
inline int Take(int64_t nowMs) { return Global().Take(nowMs); }
inline void Clear() { Global().Clear(); }

}   // namespace hitclaim
