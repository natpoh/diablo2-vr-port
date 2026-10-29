// item_grab.h - [weight] grab_items: an empty hand's grip on an item lying on the ground picks it up - through the GAME's own pick-up, F4 only. Pure
// (cleanroom/tests/item_grab_test.cpp); the game side is vrcam.cpp (ItemGrabStep, the interact target hook, ApplyActions) and melee_touch.cpp (GroundItems).
//
// What it is: the grip squeezed (the grab's own threshold, the toolkit's 0.85) with the palm within reach of an item on the ground - its place on the grid, on the
// floor - makes that item the game's interact target for one press of the layout's "interact" (pad A, the game's allowInteractOnDefaultAttack on for it): the
// controller's target lookup (InteractTarget 0x18D960, the one the "(A) Interact" prompt asks) answers that item while the press is in flight, and the game picks it
// up as it picks up any item A is pressed on - its own command, its own reach (it walks the hero up when it is not in reach), its own checks (a full inventory, an
// item of another player's). Nothing is called on the server; nothing of the item is changed by us. The press ends when the item has left the ground (taken), when the
// grip is let go, or after kPressSec (not taken: the log says so).
// Taken, the hand shows our COPY of the item (the game's own model of it, placed by us as the belt bottles are) for as long as the grip is held - drawn only.
#pragma once

#include <cmath>
#include <cstdint>

namespace itemgrab {

constexpr float kGripOn = 0.85f, kGripOff = 0.40f;   // squeezed from (the grab library's own threshold) / let go below
constexpr float kReachM = 0.25f;                      // [weight] grab_items_reach_cm default: the palm this near an item's place on the ground
constexpr double kPressSec = 2.5;                     // a press the game has not answered (the item still on the ground) ends after this
constexpr int kMaxItems = 32;

struct Item {          // an item lying on the ground, as the last scan saw it
    uint32_t id = 0;   // the game's unit id
    uintptr_t unit = 0;   // its client unit (the target the lookup answers)
    uint32_t cls = 0;     // its class (the Items table's row: its code, its model)
    float p[3] = {};   // its place on the ground, render metres
};
struct Hand {
    bool tracked = false, free = false;   // free: empty, no bottle, its grip nobody else's, not over or holding a body (vrcam)
    float grip = -1.0f;                   // 0..1, < 0 not known
    float palm[3] = {};                   // render metres
};

enum class Phase { Idle, Pressing, Holding };
// What a step did (bits), for the log and the haptics.
enum Event : unsigned { kAsked = 1, kTaken = 2, kNotTaken = 4, kLetGoEarly = 8, kDropped = 16, kGone = 32 };

// The item nearest the palm within reach (its index, -1 none) and its distance (m).
inline int Nearest(const Item* items, int n, const float palm[3], float reach, float* dist) {
    int best = -1;
    float bestD = reach;
    for (int i = 0; i < n; ++i) {
        const float dx = items[i].p[0] - palm[0], dy = items[i].p[1] - palm[1], dz = items[i].p[2] - palm[2];
        const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (std::isfinite(d) && d <= bestD) { bestD = d; best = i; }
    }
    if (dist) *dist = best >= 0 ? bestD : 1e30f;
    return best;
}

class Picker {
public:
    // One step: the hands now, the items on the ground now (fresh: the scan is up to date - an item missing from a stale list is not "taken").
    unsigned Step(double now, const Hand hands[2], const Item* items, int n, bool fresh, float reach) {
        unsigned ev = 0;
        for (int s = 0; s < 2; ++s) {
            const bool squeezed = hands[s].tracked && hands[s].grip >= kGripOn;
            rising_[s] = squeezed && !wasSqueezed_[s];
            if (!hands[s].tracked || hands[s].grip < kGripOff) wasSqueezed_[s] = false;
            else if (squeezed) wasSqueezed_[s] = true;
        }
        const bool held = hands[side_].tracked && hands[side_].grip >= kGripOff;
        switch (phase_) {
            case Phase::Idle:
                for (int s = 0; s < 2 && phase_ == Phase::Idle; ++s) {
                    if (!rising_[s] || !hands[s].free) continue;
                    float d = 0.0f;
                    const int i = Nearest(items, n, hands[s].palm, reach, &d);
                    if (i < 0) continue;
                    phase_ = Phase::Pressing;
                    side_ = s;
                    item_ = items[i];
                    since_ = now;
                    dist_ = d;
                    ev |= kAsked;
                }
                break;
            case Phase::Pressing: {
                const int at = Find(items, n, item_.id);
                if (at >= 0) item_.unit = items[at].unit;   // (the same item: its unit as the scan has it now)
                if (fresh && at < 0) {   // off the ground: the game took it
                    ev |= held ? kTaken : (kTaken | kDropped);
                    phase_ = held ? Phase::Holding : Phase::Idle;
                    since_ = now;
                } else if (!held) {
                    ev |= kLetGoEarly;
                    phase_ = Phase::Idle;
                } else if (now - since_ >= kPressSec) {
                    ev |= kNotTaken;
                    phase_ = Phase::Idle;
                }
                break;
            }
            case Phase::Holding:
                if (!held) {
                    ev |= kDropped;
                    phase_ = Phase::Idle;
                }
                break;
        }
        return ev;
    }
    void Reset() { *this = Picker{}; }

    Phase phase() const { return phase_; }
    int side() const { return side_; }
    const Item& item() const { return item_; }
    float distance() const { return dist_; }   // the palm from the item when it was asked (m)
    double since() const { return since_; }
    // The press in flight: hold the layout's interact down, and answer the interact target lookup with this unit.
    bool Pressing() const { return phase_ == Phase::Pressing; }
    uintptr_t Target() const { return phase_ == Phase::Pressing ? item_.unit : 0; }
    // The hand that shows our copy of the taken item (-1 none).
    int HoldingHand() const { return phase_ == Phase::Holding ? side_ : -1; }

private:
    static int Find(const Item* items, int n, uint32_t id) {
        for (int i = 0; i < n; ++i) if (items[i].id == id) return i;
        return -1;
    }
    Phase phase_ = Phase::Idle;
    int side_ = 0;
    Item item_;
    double since_ = 0.0;
    float dist_ = 0.0f;
    bool wasSqueezed_[2] = {}, rising_[2] = {};
};

}  // namespace itemgrab
