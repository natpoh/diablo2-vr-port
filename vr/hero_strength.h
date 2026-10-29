// hero_strength.h - the hero's strength against the weapon's: how heavy a weapon feels to him ([weight] hero_strength). Pure maths: no game, no model.
//
// D2R measures how much arm a weapon takes itself: its Strength requirement (Weapons.txt reqstr - a dagger none, a Two-Handed Sword 35, a Great Sword 100, an elite two-handed
// sword up to 189, a Thunder Maul 253). The hero has a Strength stat (base, points and gear). The weight model has two numbers that say the same thing in its own terms, and
// this scales both - nothing else, nothing of the game's rules:
//   steel : the weapon's mass (Weight %): how heavy it feels - the lag behind a swing, the sag held out, the swing's inertia;
//   hands : the wrists' strength (Hands' strength %): how hard they hold it - a two-hander in one hand sags and lags less in a strong hand (the second hand is needed less).
// Both models take them the same way - ours in its body and its muscles, the weight-physics library as the masses of the steel and its `strength` - so no interface grows.
//
//   capacity = Str x class %                  the stat the game keeps, times the class' multiplier (Settings: a barbarian is stronger than his stat says)
//   ratio    = (capacity + K) / (req + K)      K = 50: a weapon with no requirement is not infinitely easy, a hero with little Strength is not helpless
//   x        = ratio / 1.3                     1.3 = a hero a little above the requirement feels today's weight; 1 would be at the very limit
//   steel    = clamp(x ^ -p, 0.6, 1.5)         p = 0.5 x effect / 100 ([weight] strength_effect, 100 by default)
//   hands    = clamp(x ^ +p, 0.7, 1.5)
// A mass that goes down by x^-p and a wrist that goes up by x^p move the lag by x^-2p (x^-1 by default): twice the margin, half the lag. Smooth, monotone, clamped both ways.
// A weapon whose requirement the game's tables do not give (the field is not found, or no such row) uses a typical one for its kind (NominalReq, the median of Weapons.txt over
// the tiers); a Strength that cannot be read leaves everything as it is.
//
// [weight] strength_honest (the author, 2026-10-24: "a Barbarian at 300 % still finds a Two-Handed Sword heavy" - 105 against 35 came out as a ratio of 1.82, the steel x0.84):
//   ratio    = capacity / max(req, 10)         the strength as it is, no +50 that pulls every hero towards the middle; a weapon with no requirement counts as 10 (the
//                                              lowest the game asks), so it never weighs nothing - the steel's floor below holds it anyway
// The rest as above.
#pragma once

#include <algorithm>
#include <cmath>

namespace herostr {

constexpr double kK = 50.0;            // the offset that keeps both ends finite
constexpr double kRef = 1.3;           // the ratio that feels like today's weight
constexpr double kSteelMin = 0.6, kSteelMax = 1.5;
constexpr double kHandsMin = 0.7, kHandsMax = 1.5;
constexpr double kExponent = 0.5;      // x effect / 100
constexpr double kMinReq = 10.0;       // strength_honest: the requirement a weapon that asks none counts as

constexpr int kClasses = 8;            // skel::kHeroClasses: amazon, sorceress, necromancer, paladin, barbarian, druid, assassin, warlock
// The class' multiplier in %, as shipped: the barbarian's arm is the strongest (the game starts him at 30 Str, the sorceress at 10), the paladin next.
inline double DefaultClassPct(int cls) {
    static const double k[kClasses] = {100.0, 80.0, 90.0, 110.0, 130.0, 95.0, 100.0, 95.0};
    return cls >= 0 && cls < kClasses ? k[cls] : 100.0;
}

// A typical Strength requirement for a weapon of this kind (heft::hold::Kind's order: Dagger, Sword, Axe, Mace, Scepter, Wand, Staff, Polearm, Spear, Javelin, Throwing, Other),
// the median over the tiers of the game's Weapons.txt (knives 25, one-handed swords 70, two-handed 108, one-handed axes 85, two-handed 101, maces and hammers 55, mauls 146,
// scepters 76, wands 15, staves 25, polearms 104, spears 80, javelins 52, throwing weapons 25).
inline double NominalReq(int kind, bool twoHanded) {
    switch (kind) {
    case 0: return 25.0;                      // Dagger
    case 1: return twoHanded ? 108.0 : 70.0;  // Sword
    case 2: return twoHanded ? 101.0 : 85.0;  // Axe
    case 3: return twoHanded ? 146.0 : 55.0;  // Mace
    case 4: return 76.0;                      // Scepter
    case 5: return 15.0;                      // Wand
    case 6: return 25.0;                      // Staff
    case 7: return 104.0;                     // Polearm
    case 8: return 80.0;                      // Spear
    case 9: return 52.0;                      // Javelin
    case 10: return 25.0;                     // Throwing
    default: return 60.0;                     // Other
    }
}

struct In {
    double str = -1.0;         // the hero's Strength stat; < 0 not read
    double req = -1.0;         // the weapon's Strength requirement from the game's tables; < 0 not known
    double classPct = 100.0;   // the class' multiplier, %
    double effectPct = 100.0;  // how much strength matters, %
    int kind = -1;             // heft::hold::Kind, for NominalReq when req is not known
    bool twoHanded = false;
    bool honest = false;       // [weight] strength_honest: ratio = capacity / max(req, kMinReq)
};

struct Out {
    bool known = false;        // false: the Strength could not be read - steel and hands are 1
    bool reqFromGame = false;  // req came from the game's tables, not from the kind
    double req = 0.0;
    double capacity = 0.0;
    double ratio = 1.0;
    double steel = 1.0, hands = 1.0;
};

inline Out Factor(const In& in) {
    Out o;
    if (!(in.str >= 0.0) || !std::isfinite(in.str + in.classPct + in.effectPct)) return o;
    o.known = true;
    o.reqFromGame = in.req >= 0.0;
    o.req = o.reqFromGame ? in.req : NominalReq(in.kind, in.twoHanded);
    o.capacity = in.str * std::clamp(in.classPct, 1.0, 1000.0) * 0.01;
    o.ratio = in.honest ? o.capacity / std::max(o.req, kMinReq) : (o.capacity + kK) / (o.req + kK);
    const double p = kExponent * std::clamp(in.effectPct, 0.0, 400.0) * 0.01;
    const double x = o.ratio / kRef;
    o.steel = std::clamp(std::pow(x, -p), kSteelMin, kSteelMax);
    o.hands = std::clamp(std::pow(x, p), kHandsMin, kHandsMax);
    return o;
}

}  // namespace herostr
