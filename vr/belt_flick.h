// belt_flick.h - belt potions in VR F1, F2, F3 without BodyWalk ([direct] belt_flick_flat_views). Pure: no game, no XInput, no clock; the unit
// test is cleanroom/tests/belt_flick_test.cpp, the wiring is vr/direct_input.cpp (the per-frame machine) and vr/vrcam.cpp (the sink: the game's
// own belt key command, the one the hand that drinks a potion from the belt presses).
//
// The user's words (2026-10-15): "potions are taken in these modes with the left trigger held and turns of the sticks left, right, up, down".
// While the LEFT TRIGGER is held, a flick of a thumbstick - either one - drinks a belt column:
//     left = column 1,  up = column 2,  right = column 3,  down = column 4
// One flick is one potion: the stick has to come back to the middle before the next one counts, and a stick that was already pushed when the
// trigger went down does nothing until it has been to the middle (a walk in progress is not a flick). While the trigger is held the left stick
// does not walk (the hero stops: a flick never walks him). The decision is by the flick, not by the press: the trigger alone does nothing
// (in these views, without BodyWalk, it is bound to nothing; BodyWalk running means none of this acts).
#pragma once

#include <cmath>
#include <cstdint>

namespace beltflick {

constexpr float kOn = 0.70f;             // pushed this far from the middle: a flick
constexpr float kOff = 0.30f;            // back inside this circle: the middle (a flick may follow)
constexpr float kTriggerOn = 0.55f;      // the left trigger held (hysteresis, as the layout's)
constexpr float kTriggerOff = 0.35f;
constexpr float kDominance = 1.35f;      // the long axis must beat the short one by this much to be a direction (a diagonal waits)

struct Stick { float x = 0.0f, y = 0.0f; };   // x right, y up

enum class Dir : uint8_t { None, Left, Up, Right, Down };

inline float Len(Stick s) { return std::sqrt(s.x * s.x + s.y * s.y); }

// Which of the four ways a pushed stick points; None while it is short of a direction (on a diagonal, or not a number).
inline Dir DirOf(Stick s) {
    if (!std::isfinite(s.x) || !std::isfinite(s.y)) return Dir::None;
    const float ax = std::fabs(s.x), ay = std::fabs(s.y);
    if (!(ax + ay > 0.0f)) return Dir::None;
    if (ax >= ay * kDominance) return s.x < 0.0f ? Dir::Left : Dir::Right;
    if (ay >= ax * kDominance) return s.y < 0.0f ? Dir::Down : Dir::Up;
    return Dir::None;
}

// The belt column 0..3 (the game's Belt 1..4 keys, commands 0x17..0x1A) of a direction; -1 for None.
inline int ColumnOf(Dir d) {
    switch (d) {
    case Dir::Left: return 0;
    case Dir::Up: return 1;
    case Dir::Right: return 2;
    case Dir::Down: return 3;
    case Dir::None: break;
    }
    return -1;
}

inline const char* DirName(Dir d) {
    switch (d) {
    case Dir::Left: return "left";
    case Dir::Up: return "up";
    case Dir::Right: return "right";
    case Dir::Down: return "down";
    case Dir::None: break;
    }
    return "no way";
}

class Machine {
public:
    struct Out {
        int column = -1;        // the belt column 0..3 to drink this frame, -1 = none
        Dir dir = Dir::None;    // the way it was flicked
        int stick = -1;         // which stick: 0 left, 1 right
        bool holding = false;   // the left trigger is held: the left stick does not walk
    };

    // One call per headset frame. `trigger`: the left trigger 0..1. `enabled`: the module acts now (the key, a flat view, no panel, focus).
    // Not enabled: everything is let go and nothing is held.
    Out Step(float trigger, Stick left, Stick right, bool enabled) {
        Out o;
        if (!enabled || !std::isfinite(trigger)) { Reset(); return o; }
        held_ = held_ ? trigger > kTriggerOff : trigger >= kTriggerOn;
        if (!held_) { armed_[0] = armed_[1] = false; return o; }
        o.holding = true;
        const Stick s[2] = {left, right};
        for (int i = 0; i < 2; ++i) {
            const float m = Len(s[i]);
            if (!std::isfinite(m) || m <= kOff) {
                armed_[i] = true;   // in the middle (the frame the trigger went down too): ready for a flick
            } else if (armed_[i] && m >= kOn) {
                const Dir d = DirOf(s[i]);
                if (d == Dir::None) continue;   // a diagonal: wait for it to settle, or for the middle
                armed_[i] = false;              // one flick, one potion (also when both sticks flick at once: the first only)
                if (o.column < 0) { o.column = ColumnOf(d); o.dir = d; o.stick = i; }
            }
        }
        return o;
    }

    void Reset() { held_ = false; armed_[0] = armed_[1] = false; }
    bool Held() const { return held_; }
    bool Armed(int stick) const { return armed_[stick & 1]; }

private:
    bool held_ = false;
    bool armed_[2] = {false, false};
};

}  // namespace beltflick
