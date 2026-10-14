// touch_why.h - why hit by touch (melee_touch.h) did not start for the weapon in hand, in a line of the log, once per weapon and reason. Pure.
//
// The touch has many gates before it ever makes a blade (the view, the weapon's kind, the skeleton hook's pose, [weight]'s hand, the throw by
// gesture's grip...). When one stops it the module said nothing - "the spear does not work" had no line in the log to read (2026-10-14: with a
// javelin there was not one melee touch line). Each reason is told ONCE per weapon (the kind and the items in the hands): a new weapon tells
// its own again.
#pragma once

#include <cstdint>

namespace meleetouch {

enum class Why : uint8_t {
    None = 0,
    NotF4,          // another view than VR first person
    NotOk,          // not a game area in first person on the controllers (a panel or menu open, third person, the arms off the controllers)
    KindNotSwung,   // a bow, a crossbow or a kind not known: its own gesture
    ThrownOff,      // [gameplay] melee_touch_thrown is off
    Potion,         // a throwing potion (or a thrown-kind item whose code is not read yet): thrown, never swung
    NoPose,         // the skeleton hook has no fresh pose of the weapon
    LetGo,          // [weight]: no hand holds the weapon
    AfterThrow,     // 0.7 s after a throw by gesture was released
    GripOpen,       // throw_by_grip on, the right grip is not squeezed on the weapon
    NoBlade,        // no weapon bone as drawn, no hand sample, or no view yet
    kCount
};

inline const char* WhyText(Why w) {
    switch (w) {
        case Why::NotF4: return "not VR first person (F4)";
        case Why::NotOk: return "not a game area in first person with the arms on the controllers (a panel or menu is open, or third person)";
        case Why::KindNotSwung: return "this kind of weapon has its own gesture (a bow, a crossbow) or is not known";
        case Why::ThrownOff: return "thrown weapons are not swung - [gameplay] melee_touch_thrown is off";
        case Why::Potion: return "a throwing potion (or a thrown-kind item whose code is not read yet) is thrown, never swung";
        case Why::NoPose: return "the skeleton hook has no fresh pose of the weapon (the arms are not on the controllers, or no pose in the last 0.1 s)";
        case Why::LetGo: return "[weight]: no hand holds the weapon (let go, falling, lying)";
        case Why::AfterThrow: return "within 0.7 s of a released throw by gesture: the swing's follow-through is not a touch";
        case Why::GripOpen: return "throw_by_grip is on and the right grip is not squeezed on the weapon (the weapon is in the hand while the grip is)";
        case Why::NoBlade: return "no weapon bone as drawn, no hand sample for the pose, or no view yet";
        default: return "?";
    }
}

// Says a reason once per weapon. `weapon` is any number that differs from weapon to weapon (the kind and the items held).
class WhyTold {
public:
    // True the first time `why` is told for `weapon` (a different weapon starts afresh); never for None.
    bool First(uint32_t weapon, Why why) {
        if (why == Why::None || why >= Why::kCount) return false;
        if (!set_ || weapon != weapon_) {
            set_ = true;
            weapon_ = weapon;
            told_ = 0;
        }
        const uint32_t bit = 1u << static_cast<unsigned>(why);
        if (told_ & bit) return false;
        told_ |= bit;
        return true;
    }
    void Reset() { *this = WhyTold{}; }

private:
    bool set_ = false;
    uint32_t weapon_ = 0, told_ = 0;
};

// A number for what is held: the kind and the two hands' item codes.
inline uint32_t WeaponKey(uint32_t type, uint32_t code0, uint32_t code1) { return type * 2654435761u ^ code0 * 31u ^ code1 * 2246822519u; }

}  // namespace meleetouch
