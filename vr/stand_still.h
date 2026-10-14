// stand_still.h - attacks that never walk the hero ([gameplay] attack_in_place), pure: no game, no clock of its own.
// vr/stand_still.cpp wires it to the game's pad poll and to the game's own key command; the unit test is
// cleanroom/tests/stand_still_test.cpp.
//
// The fault: in VR first person an attack the plugin presses sometimes walks the hero forward instead. The game's
// click on the map (0xFE3B0, docs/move_recon.md section 5) with nothing under the point and no "Stand Still" in its
// flags is a walk to that point; with Stand Still (Shift on the keyboard, the game's key command 0x24 "Stand still")
// it is an attack in place. The fix is the game's own: hold that command for as long as an attack of ours is pressed.
// Nothing is called that the game does not call for a player holding Shift.
//
// Order matters: the command is pressed on the game's UI thread a few milliseconds after it is asked for, and an
// attack read before it would still be a walk. So the attack press is held back until the game says the command is
// down (or a short wait has run out), and a press that was shorter than that wait is stretched to a least length
// from the moment it is let through, so it is never lost.
#pragma once

#include <cstdint>

namespace standstill {

// -- which presses ---------------------------------------------------------------------------------------------------
// A skill that moves the hero is not "stood still": Teleport, Charge, Leap and Leap Attack, Whirlwind, Dragon Flight.
// (Skills.txt ids of D2R 3.3; a wrong or missing id only means Stand Still is held, or not, for that one skill.)
inline bool MovesHero(int32_t skill) {
    switch (skill) {
    case 54:    // Sorceress: Teleport
    case 107:   // Paladin: Charge
    case 132:   // Barbarian: Leap
    case 143:   // Barbarian: Leap Attack
    case 151:   // Barbarian: Whirlwind
    case 275:   // Assassin: Dragon Flight
        return true;
    default:
        return false;
    }
}
constexpr int32_t kSkillInteract = 357;   // the pick-up / interact "skill": not an attack

enum class Why : uint8_t {
    Held,            // Stand Still is held for this press
    Off,             // [gameplay] attack_in_place=0
    NotF4,           // not VR first person
    Mover,           // the skill moves the hero (Teleport, Charge, Leap, Whirlwind ...)
    UnknownSkill,    // a skill slot whose skill is not known (the skill panel's reading is off): left as it was
    Interact,        // a pick-up, not an attack
    Throw,           // a throw by gesture's press, with [hands] throw_stand_still=0
};
inline const char* WhyText(Why w) {
    switch (w) {
    case Why::Held: return "Stand Still held";
    case Why::Off: return "not held: [gameplay] attack_in_place=0";
    case Why::NotF4: return "not held: not VR first person";
    case Why::Mover: return "not held: the skill moves the hero";
    case Why::UnknownSkill: return "not held: the skill of that button is not known";
    case Why::Interact: return "not held: a pick-up, not an attack";
    case Why::Throw: return "not held: a throw by gesture, [hands] throw_stand_still=0";
    }
    return "";
}

// A skill slot's press as the policy sees it: the skill id (-1 not known).
inline Why SkillWhy(int32_t skill) {
    if (skill < 0) return Why::UnknownSkill;
    if (skill == kSkillInteract) return Why::Interact;
    if (MovesHero(skill)) return Why::Mover;
    return Why::Held;
}

// -- the hold ---------------------------------------------------------------------------------------------------------
constexpr uint32_t kWaitMs = 60;        // the longest an attack waits for the command to be down
constexpr uint32_t kMinPressMs = 65;    // a press stretched to at least this once let through (the pad is polled in it)
constexpr uint32_t kTailMs = 20;        // asked for this long after the press is let go (the lease behind it is 65 ms more)
constexpr int kDegradeAfter = 2;        // waits in a row that ran out: the command is not coming; stop waiting

struct Out {
    bool holdCommand = false;   // keep asking for the command (a lease, refreshed every poll)
    bool deliver = false;       // let the attack through this poll
    bool started = false;       // the step a press began
    bool delivered = false;     // the step the press was let through
    bool timedOut = false;      // ... because the wait ran out, not because the command was down
    uint32_t waitedMs = 0;      // how long it waited, at `delivered`
};

class Machine {
public:
    // wanted: an attack of ours is pressed now; confirmed: the game's command is down; degraded runs on from the last answer.
    Out Step(bool wanted, bool confirmed, uint64_t nowMs) {
        Out o;
        if (confirmed) { misses_ = 0; degraded_ = false; }   // it does come after all
        switch (state_) {
        case State::Idle:
            if (!wanted) break;
            state_ = State::Waiting;
            t0_ = nowMs;
            o.started = true;
            [[fallthrough]];
        case State::Waiting: {
            o.holdCommand = true;
            if (confirmed || degraded_ || nowMs - t0_ >= kWaitMs) {
                o.delivered = true;
                o.waitedMs = (uint32_t)(nowMs - t0_);
                if (confirmed) misses_ = 0;
                else if (!degraded_) {
                    o.timedOut = true;
                    if (++misses_ >= kDegradeAfter) degraded_ = true;
                }
                state_ = State::Delivering;
                from_ = nowMs;
                o.deliver = true;
            }
            break;
        }
        case State::Delivering:
            o.holdCommand = true;
            o.deliver = true;
            if (!wanted && nowMs - from_ >= kMinPressMs) { state_ = State::Tail; until_ = nowMs + kTailMs; o.deliver = false; }
            break;
        case State::Tail:
            o.holdCommand = true;
            if (wanted) { state_ = State::Delivering; from_ = nowMs; o.deliver = true; o.started = true; }
            else if (nowMs >= until_) { state_ = State::Idle; o.holdCommand = false; }
            break;
        }
        return o;
    }
    // The view left VR first person, the switch went off, the session ended: everything let go at once.
    void Reset() { state_ = State::Idle; }
    bool Busy() const { return state_ != State::Idle; }
    bool Degraded() const { return degraded_; }

private:
    enum class State : uint8_t { Idle, Waiting, Delivering, Tail };
    State state_ = State::Idle;
    uint64_t t0_ = 0, from_ = 0, until_ = 0;
    int misses_ = 0;
    bool degraded_ = false;
};

// -- a click of the mouse (or the laser, or a program) on the map ---------------------------------------------------------
// The game's click on the map, 0xFE3B0(unit, type, x, y, flags): types 0 / 1 the left button pressed / held, 3 / 4 the right. With
// nothing under the pointer and no Stand Still in `flags` (its low byte, the key command 0x24's state) it is a WALK to the point -
// whoever sent it. In VR first person ([gameplay] attack_in_place) such a click gets the Stand Still flag itself: it is the skill in
// place, never a walk. A click ON a monster, an item, a door or an NPC is the game's own click and is left alone, and so is every
// click that already has a flag, and the controller's own types (6 .. 10: the pad's stop and casts).
inline bool ClickCandidate(bool wanted, bool f4, bool menu, bool haveHero, int type, uint64_t flags) {
    return wanted && f4 && !menu && haveHero && (type == 0 || type == 1 || type == 3 || type == 4) && (flags & 0xFF) == 0;
}
// `unitUnder`: the game's own lookup found a unit under the pointer (HoverUnit): then it is not bare ground.
inline bool ClickStill(bool candidate, bool unitUnder) { return candidate && !unitUnder; }

}  // namespace standstill
