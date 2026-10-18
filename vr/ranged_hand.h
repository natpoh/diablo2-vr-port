// ranged_hand.h - [hands] shoulder_draw_ranged: a bow or a crossbow put away behind a shoulder and drawn from it, into the hand that reached. Pure: no game calls, no globals
// (cleanroom/tests/ranged_hand_test.cpp feeds it).
//
// The bow or crossbow is the game's equipped weapon; it never leaves its slot. Only where it is DRAWN changes (the skeleton hook shrinks its attach bone while it is away and, with the swap,
// carries it to the other wrist), and which controller is the bow hand for everything that reads it (the draw, the string, the rig, the aim, the orbs):
//   - it starts in its own hand (`home`: the hand the settings hold it in; the bow in the left, as ever);
//   - a grip pressed behind EITHER shoulder PUTS IT AWAY (it is not drawn, nothing draws a string, nothing is posed round it) - by a hand that holds no potion and is not drawing the
//     string; the same grip while it is away DRAWS it. Either hand does either.
//   - with SWAP (a bow only: the crossbow's holder is wired to the right hand in the hook) it is drawn INTO THE HAND THAT REACHED: the bow goes from one hand to the other by being put away
//     with one and drawn with the other. Without it, it comes back into `home`.
// Nothing is pressed in the game and no slot changes.
#pragma once

namespace rangedhand {

enum class Event : int { None, PutAway, Drawn };

struct HandIn {
    bool tracked = false;
    bool press = false;    // the grip was pressed in this step (an edge)
    bool inZone = false;   // the hand is behind a shoulder (either: vr/shoulder_draw.h ZoneSide)
    bool busy = false;     // holds a belt potion, or has the string (the draw's grip): it neither puts the weapon away nor draws it
};

struct In {
    bool weapon = false;   // a bow or crossbow is the game's weapon now
    bool swap = false;     // [hands] shoulder_draw_ranged_swap: a draw goes into the hand that reached (bows only)
    bool bow = true;       // a bow (the swap applies); false: a crossbow (always drawn into `home`)
    int home = 1;          // the hand it is held in without a swap (0 right, 1 left)
    HandIn hand[2];        // 0 right, 1 left
};

struct Out {
    bool stowed = false;
    int side = 1;          // the hand that holds it (while it is not away)
    Event event = Event::None;
    int by = -1;           // the hand that put it away or drew it
};

class Holster {
public:
    void Reset(int home = 1) { stowed_ = false; side_ = home & 1; }
    bool Stowed() const { return stowed_; }
    int Side() const { return side_; }

    Out Step(const In& in) {
        Out o;
        if (!in.weapon) {   // nothing ranged in the game's hands: it starts fresh, in its own hand, next time
            Reset(in.home);
        } else {
            if (!(in.swap && in.bow)) side_ = in.home & 1;   // no swap: always its own hand
            for (int h = 0; h < 2; ++h) {
                const HandIn& hi = in.hand[h];
                if (!hi.tracked || !hi.press || !hi.inZone || hi.busy) continue;
                if (stowed_) {
                    stowed_ = false;
                    if (in.swap && in.bow) side_ = h;   // into the hand that reached
                    o.event = Event::Drawn;
                } else {
                    stowed_ = true;
                    o.event = Event::PutAway;
                }
                o.by = h;
                break;   // one change in a step
            }
        }
        o.stowed = stowed_;
        o.side = side_;
        return o;
    }

private:
    bool stowed_ = false;
    int side_ = 1;
};

}  // namespace rangedhand
