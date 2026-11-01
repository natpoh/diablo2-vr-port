// form_strength.h - [shapeshift] wolf_strength_pct / bear_strength_pct: a grafted werewolf / werebear's paws are stronger hands in the weight model's grab
// ([weight] grab_bodies, grab_throw). The factor multiplies [weight] strength for the hands that hold a body - the grab library's hand (how heavy a body feels,
// how closely it follows the paw) and the knock-down library's hold and throw (how a lifted humanoid hangs and how fast it leaves the paw, so how far it flies).
// Only while the hero is in a grafted form (F4, forms_first_person); a person's hands keep [weight] strength as it is. The weight model only: the game's own
// knockback ([weight] grab_moves_unit) and every roll stay the game's. Pure (cleanroom/tests/form_strength_test.cpp).
#pragma once

#include <algorithm>
#include <cmath>

namespace formstrength {

constexpr float kWolfDefaultPct = 300.0f, kBearDefaultPct = 500.0f;   // Serg 2026-11-01: the werewolf 3x the hero, the werebear 5x
constexpr float kMinPct = 100.0f, kMaxPct = 600.0f;                   // never weaker than the person, at most six times

inline float ClampPct(float pct) { return std::isfinite(pct) ? std::clamp(pct, kMinPct, kMaxPct) : 100.0f; }

// form: 0 a person (or no grafted form), 1 the werewolf, 2 the werebear (unleashed::shapeshift::Form). The multiplier on [weight] strength.
inline float Factor(int form, float wolfPct, float bearPct) {
    if (form == 1) return ClampPct(wolfPct) * 0.01f;
    if (form == 2) return ClampPct(bearPct) * 0.01f;
    return 1.0f;
}

// The hands' strength the weight model is handed: [weight] strength / 100 times the form's factor.
inline float Hands(float strength, int form, float wolfPct, float bearPct) {
    const float base = std::isfinite(strength) && strength > 0.0f ? strength : 1.0f;
    return base * Factor(form, wolfPct, bearPct);
}

}  // namespace formstrength
