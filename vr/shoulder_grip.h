// shoulder_grip.h - what a grip pressed with a hand behind a shoulder IS. Pure.
//
// Behind a shoulder the grip has had one meaning: the weapon swap (vr/shoulder_swap.h). With [weight] to_back a weapon that was let go
// and has landed is slung on the hero's back, and the same gesture - a hand behind either shoulder, its grip pressed - now also means
// "take the weapon off my back into this hand". The two must never fire together: while a weapon is stowed on the back that press is a
// take (and the swap is not asked for), and with nothing on the back it is the swap, exactly as before. The swap wire asks Choose before
// it sends the game's "Swap weapons" command; the weight model takes the stowed weapon on its own press edge (heft hold: HandIn::behind),
// so both read the same fact - a weapon is stowed on the back - and cannot disagree.
#pragma once

namespace shouldergrip {

enum class Action {
    None,         // the press is nobody's here
    Swap,         // the weapon set is swapped (the game's own "Swap weapons")
    TakeStowed,   // the weapon on the back is taken into this hand
};

struct In {
    bool press = false;          // the grip was pressed in this step (an edge)
    bool inReach = false;        // the hand is behind a shoulder (shoulderswap::Near)
    bool stowedOnBack = false;   // a weapon is stowed on the hero's back
    bool swapAsked = false;      // the swap gesture says this press is a swap (its dwell is over, it may be asked for)
    bool potion = false;         // the hand holds a belt potion, or one is lit for it: it takes nothing
};

inline Action Choose(const In& in) {
    if (!in.press || !in.inReach) return Action::None;
    if (in.stowedOnBack) return in.potion ? Action::None : Action::TakeStowed;   // on the back: a take, never also a swap
    return in.swapAsked ? Action::Swap : Action::None;
}

// The tick in the hand that reaches into the take zone while a weapon hangs on the back (or is gone from sight): one pulse per entry, so the hand
// feels where to grab. A hand that jitters on the zone's edge ticks at most once per kMinGapMs; a hand that is already in the zone when the weapon
// goes to the back ticks once (it is an entry as far as the hand is concerned); a hand with a potion in it, or no weapon on the back, never ticks.
class ZoneTick {
public:
    static constexpr unsigned long long kMinGapMs = 300;
    // `nowMs` any steady clock in ms. True: pulse this hand now.
    bool Step(bool stowed, bool inZone, bool busy, unsigned long long nowMs) {
        const bool inside = stowed && inZone && !busy;
        const bool enter = inside && !inside_;
        inside_ = inside;
        if (!enter) return false;
        if (have_ && nowMs - last_ < kMinGapMs) return false;
        have_ = true;
        last_ = nowMs;
        return true;
    }
    void Reset() { *this = ZoneTick{}; }

private:
    bool inside_ = false, have_ = false;
    unsigned long long last_ = 0;
};

}  // namespace shouldergrip
