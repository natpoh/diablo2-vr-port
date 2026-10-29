#pragma once
// stick_strength.h - [weight] stick_by_strength, stick_pct: how hard a weapon sticks - in a wound (stab_in) and caught on the monsters' bodies (monster_bodies) - by the
// hero's strength from the strength table ([weight] str_<class>, strength_effect: the same the weight model's "the hero's strength" uses) and a general factor. Pure.
//
// The author (2026-10-23): «надо силу застревания может настроить, чтобы меч не сильно застревал, особенно сделать её зависимость от силы персонажа: у мага силы
// меньше, чем у варвара, и застревать должно сильнее»; «у варвара вообще не должно практически застревать»; the table, not the game's Strength stat: «надо её
// учитывать, а не скилл силы в игре». So: the class' multiplier (Barbarian 200 %, Sorceress 80 %), as much as strength matters, gives the factor - the same rule
// for every class, no class switch: 100 % sticks as before, 200 % a tenth of it, 80 % about twice. docs/notes/strike_force.md 4b.
//
// What the factor scales: the stab's resistance (both dampers), the pull that frees a resting stab and how long it must last, the tear's distance across the line,
// and the friction while the weapon is caught between two monsters or more (it slides out of the wedge the easier). Not one body's surface: it stops and holds the
// weapon the same for every class - the hit and the weight are felt (the first build scaled every body's friction and a Barbarian's two-hander glanced off every
// monster: "the weapon simply passes through the monsters", the headset 2026-10-23). Not the entry: whether a thrust goes in stays the stab's rules.

#include <algorithm>
#include <cmath>

#include "../cleanroom/heft/stab.h"

namespace stickstr {

constexpr double kExponent = 3.3;     // the factor = the strength's multiplier to the minus this: 200 % -> 0.10, 110 % -> 0.73, 80 % -> 2.1
constexpr double kLeast = 0.05, kMost = 4.0;
constexpr double kTearLeastM = 0.03;  // the tear never closer than this (a hand on the weapon is always a little off its line)
constexpr double kUnlockMostN = 270.0;   // a harder stab's unlock pull stays under what the hands pull (300 N at most, stab.h): stuck harder, never stuck for good
constexpr double kFrictionLeast = 0.1, kFrictionMost = 2.0;   // the wedge's friction changes less than the stab

// on: [weight] stick_by_strength; classPct: the hero's class' multiplier, % (str_<class>; not a number or <= 0: 100); effectPct: strength_effect, % (0: strength does
// not matter); stickPct: stick_pct, % (the general factor; 0: nothing sticks). Off: the general factor alone.
inline double Factor(bool on, double classPct, double effectPct, double stickPct) {
    double strength = 1.0;
    if (on) {
        const double cls = std::isfinite(classPct) && classPct > 0 ? classPct * 0.01 : 1.0;
        const double eff = std::isfinite(effectPct) ? std::clamp(effectPct * 0.01, 0.0, 2.0) : 1.0;
        strength = std::max(0.1, 1.0 + eff * (cls - 1.0));
    }
    const double general = std::isfinite(stickPct) ? std::clamp(stickPct * 0.01, 0.0, 3.0) : 1.0;
    if (general <= 0.0) return 0.0;
    return std::clamp(general * std::pow(strength, -kExponent), kLeast, kMost);
}

// The stab's settings (from its sliders) as hard as the factor says. 0: the stab never locks and holds nothing.
inline heft::stab::Params Apply(heft::stab::Params p, double f) {
    if (!std::isfinite(f) || std::fabs(f - 1.0) < 1e-9) return p;
    f = std::max(0.0, f);
    p.shellDamper *= f;
    p.fleshDamper *= f;
    p.unlockForce = f > 1.0 ? std::min(p.unlockForce * f, std::max(p.unlockForce, kUnlockMostN)) : p.unlockForce * f;   // (0: never locks)
    p.lockTime *= std::min(1.0, f);
    p.breakM = std::max(kTearLeastM, p.breakM * f);
    return p;
}

// The friction while caught between several monsters (base times the factor: hold's Settings::wedgeFriction with base 1).
inline double Friction(double base, double f) {
    if (!std::isfinite(f)) return base;
    return base * std::clamp(f, kFrictionLeast, kFrictionMost);
}

}  // namespace stickstr
