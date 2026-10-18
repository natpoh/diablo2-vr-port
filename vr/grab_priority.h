// grab_priority.h - [belt] grab_nearest and [belt] grab_trigger: who gets a hand's grip press when it could take a belt potion OR the weapon. Pure: no game, no windows.h
// (cleanroom/tests/grab_priority_test.cpp feeds it).
//
// Who uses a hand's grip in VR first person (F4):
//   - the weapon (vr/weight.cpp, cleanroom/heft/hold.cpp): the grip held on the weapon's shaft IS the hold. A free hand takes the weapon with its grip when its fist is within
//     [weight] grab_cm (12 cm) of the shaft of a weapon the other hand holds (a staff, a two-hander, a crossbow, a sword: the second hand), or of one that lies / falls; a weapon
//     stowed on the back is taken by a hand behind a shoulder or within [weight] back_take_cm (25 cm) of its drawn shaft; and the hand the game holds it in - or any hand with
//     recall_any_hand, for a weapon on the back - calls it back from afar (Zone::Recall: the hand is not AT the weapon). (The older staff grab without [weight]: [hands] staff_grab_cm.)
//   - the shoulder swap (vr/shoulder_swap*.h): a grip pressed with the hand behind a shoulder (|x| 0.10 - 0.48 m, y -0.38 .. +0.16 m, z 0.025 - 0.38 m from the head) is the
//     weapon set swap's, never anything else's.
//   - the belt potions (vr/vrcam.cpp, [hands] belt_grab): a palm within 12 cm of a bottle on the belt lights it and the grip takes it. The weight model used to be told "this hand
//     is busy" for a lit bottle and took nothing - so a hand in the weapon's zone that was ALSO near a bottle (a staff held low in front of the belt, a weapon hanging at the hip,
//     a hand at the hip reaching for the back) took the potion.
//   - others: the throw by grip (the right grip with a thrown weapon in hand), the bow draw (the string hand's grip), the chords (right grip + B = skill slot 4, left grip + stick
//     click = recentre, left grip + menu = the map) - none of them near a belt potion's grip.
//
// [belt] grab_nearest (on): when both a potion and the weapon are in reach, the NEARER one gets the press - the palm's distance to the bottle against the fist's distance to the
// shaft; a tie goes to the weapon; the shoulder swap's volume is always the swap's. Only the potion in reach: the potion. Only the weapon: the weapon. Off: a potion in reach
// wins and is lit, as before.
// [belt] grab_trigger (off): a belt potion is taken only with the trigger squeezed too (trigger + grip); the grip alone never takes one - it goes to the weapon / the swap, and a
// bottle in reach is lit (its ball) but does not hold the hand's grip from the weapon. Needs the controller's trigger from direct input; without it the grip alone takes, as before.
#pragma once

#include <cstdint>
#include <cstdio>

namespace grabprio {

// = weight::TakeZone's answer (hh::Hold::TakeAt): where a grip pressed now would take the weapon.
enum class Zone : uint8_t { None = 0, Shaft = 1, Back = 2, Recall = 3 };
enum class Who : uint8_t { Nothing, Belt, Weapon, Swap, HeldWeapon };

struct In {
    bool nearest = true;         // [belt] grab_nearest
    bool needTrigger = false;    // [belt] grab_trigger
    bool triggerKnown = false;   // the controller's trigger could be read (direct input)
    bool triggerDown = false;    // ... and it is squeezed
    bool holdsWeapon = false;    // this hand already holds the weapon (its grip is the hold)
    Zone zone = Zone::None;      // where this hand's grip would take the weapon
    float zoneCm = 1e6f;         // the fist's distance to that shaft, cm
    float zoneReachCm = 12.0f;   // the zone's radius for a shaft ([weight] grab_cm), for the log
    bool swapNear = false;       // the hand is behind a shoulder (the swap's volume)
    bool swapClaims = false;     // the swap gesture has this hand's grip
    int bottle = -1;             // the nearest belt potion within reach: its column (0..3), -1 none
    float bottleCm = 1e6f;       // its distance from the palm, cm
};

struct Out {
    Who who = Who::Nothing;      // what a grip pressed now goes to
    bool bottleLit = false;      // the bottle is lit (a ball on it)
    bool bottleTakes = false;    // ... and a grip pressed now takes it - which also keeps the weapon from taking that hand's grip
    bool triggerHeld = false;    // the potion was left to the weapon only because the trigger is not squeezed (grab_trigger)
};

inline Out Decide(const In& in) {
    Out o;
    if (in.holdsWeapon) { o.who = Who::HeldWeapon; return o; }
    const bool bottle = in.bottle >= 0;
    if (!in.nearest) {   // as before: a potion in reach is lit and wins; the swap only claims the grip; the weapon where no potion is
        o.bottleLit = bottle;
        o.bottleTakes = bottle;   // (the swap's claim keeps the grip from taking it, in the caller; the hand is still busy for the weapon, as it was)
        if (in.swapClaims) o.who = Who::Swap;
        else if (bottle) o.who = Who::Belt;
        else if (in.zone != Zone::None) o.who = Who::Weapon;
    } else if (in.swapClaims || in.swapNear) {
        o.who = Who::Swap;
    } else if (in.zone == Zone::None) {
        if (bottle) { o.who = Who::Belt; o.bottleLit = o.bottleTakes = true; }
    } else if (!bottle || in.zoneCm <= in.bottleCm) {
        o.who = Who::Weapon;
    } else {
        o.who = Who::Belt;
        o.bottleLit = o.bottleTakes = true;
    }
    // trigger + grip: the grip alone does not take a potion - it falls to the weapon (when there is a zone) or to nothing; the ball still shows
    if (in.needTrigger && in.triggerKnown && !in.triggerDown && o.who == Who::Belt) {
        o.bottleTakes = false;
        o.triggerHeld = true;
        o.who = in.zone != Zone::None ? Who::Weapon : Who::Nothing;
    }
    return o;
}

inline const char* WhoName(Who w) {
    switch (w) {
        case Who::Belt: return "BELT";
        case Who::Weapon: return "WEAPON";
        case Who::Swap: return "SWAP";
        case Who::HeldWeapon: return "the weapon it holds";
        default: return "nothing";
    }
}

// One log line for a grip press: the hand, what takes it, and why - the distances that decided. hand 0 right, 1 left; belt names the bottle's kind when it is known (may be null).
inline int Line(const In& in, const Out& o, int hand, const char* belt, char* out, size_t n) {
    char zone[130] = "no weapon zone";
    if (in.zone == Zone::Shaft) std::snprintf(zone, sizeof zone, "the weapon's shaft is %.0f cm from the fist (a grip takes it within %.0f cm)", in.zoneCm, in.zoneReachCm);
    else if (in.zone == Zone::Back) std::snprintf(zone, sizeof zone, "the weapon on the back is %.0f cm from the fist%s", in.zoneCm, in.swapNear ? ", behind a shoulder" : " (its take zone)");
    else if (in.zone == Zone::Recall) std::snprintf(zone, sizeof zone, "the weapon is away (%.0f cm from the fist) and this grip would call it back", in.zoneCm);
    char bottle[120] = "no belt potion in reach";
    if (in.bottle >= 0) std::snprintf(bottle, sizeof bottle, "belt potion %d%s%s%s is %.0f cm away", in.bottle + 1, belt ? " (" : "", belt ? belt : "", belt ? ")" : "", in.bottleCm);
    const char* why = "";
    switch (o.who) {
        case Who::HeldWeapon: why = "the hand already holds the weapon"; break;
        case Who::Swap: why = in.swapClaims ? "the hand is behind a shoulder: the grip is the weapon swap's" : "the hand is behind a shoulder (the weapon swap's volume)"; break;
        case Who::Weapon:
            why = o.triggerHeld ? "a belt potion needs trigger + grip and the trigger is not squeezed (grab_trigger)"
                : !in.nearest ? "no belt potion in reach, so the weapon is taken"
                : in.bottle < 0 ? "the weapon is the only thing in reach"
                : "the weapon is nearer than the potion (grab_nearest)"; break;
        case Who::Belt:
            why = !in.nearest ? "grab_nearest is off: a potion in reach wins"
                : in.zone == Zone::None ? "the only thing in reach"
                : "the potion is nearer than the weapon (grab_nearest)"; break;
        default: why = o.triggerHeld ? "a belt potion needs trigger + grip and the trigger is not squeezed (grab_trigger)" : "nothing is in reach"; break;
    }
    const char* tail = "";
    if (o.who != Who::Belt && in.bottle >= 0) tail = o.bottleTakes ? " - lit, but not taken" : o.bottleLit ? " - lit (the ball), not taken" : " - not lit, not taken";
    return std::snprintf(out, n, "vrcam: grip press - the %s hand: %s - %s; %s; %s%s%s", hand ? "left" : "right", WhoName(o.who), why, zone, bottle, tail,
                         in.needTrigger && in.triggerKnown ? (in.triggerDown ? " [trigger squeezed]" : " [trigger not squeezed]") : "");
}

}  // namespace grabprio
