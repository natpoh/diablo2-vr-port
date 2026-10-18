// quiver_input.h - the part of "pick the next shot's skill from the quiver" that does not depend on the weapon: the names of the controls the ini can bind it to
// ([direct] quiver_next / quiver_prev: the buttons of the string hand while the string is drawn).
//
// Pure: no game calls, no globals, no windows.h (cleanroom/tests/quiver_input_test.cpp feeds it names). What the skill list is and how the choice reaches the game is the
// weapon's own (vr/bow_quiver.h for a bow). (The reach behind a shoulder that was here, for a bow and a crossbow, is gone: the shoulders are the shoulder swap's.)
#pragma once

#include <cctype>
#include <cstdint>

namespace quiverin {

// ---- the controls ---------------------------------------------------------------------------------------------------------------------------------------------------
// The names of [direct] quiver_next / quiver_prev: a button of direct_input's frame (hand 0 the left, 1 the right; the Button bits of direct_input.h: primary 1 = A / X, secondary 2 = B / Y,
// stick 4 = the click, menu 8) - by hand (left_x ... right_menu) or by the hand's job (string_* the hand that takes the string, bow_* the one that holds the weapon; for either
// handedness). "none" (or empty) switches the control off.
struct Control {
    int rel = -1;          // -1: a fixed hand (`hand`), 0 the weapon hand, 1 the other (string) hand
    int hand = -1;         // direct_input's hand when fixed; -1 with rel -1 = no control
    uint32_t mask = 0;
    bool Set() const { return mask != 0 && (rel >= 0 || hand >= 0); }
    // direct_input's hand for this control, the weapon being in hand `weaponX`
    int Hand(int weaponX) const { return rel == 0 ? weaponX : rel == 1 ? 1 - weaponX : hand; }
};
inline bool NameIs(const char* name, const char* s) {
    for (; *name && *s; ++name, ++s) if (std::tolower((unsigned char)*name) != *s) return false;
    return !*name && !*s;
}
inline bool ParseControl(const char* name, Control* out) {
    if (!out) return false;
    *out = Control{};
    while (name && (*name == ' ' || *name == '\t')) ++name;
    if (!name || !*name || NameIs(name, "none")) return true;
    struct N { const char* name; int rel, hand; uint32_t mask; };
    static const N names[] = {{"left_x", -1, 0, 1u}, {"left_y", -1, 0, 2u}, {"left_stick", -1, 0, 4u}, {"left_menu", -1, 0, 8u},
                              {"right_a", -1, 1, 1u}, {"right_b", -1, 1, 2u}, {"right_stick", -1, 1, 4u}, {"right_menu", -1, 1, 8u},
                              {"string_primary", 1, -1, 1u}, {"string_secondary", 1, -1, 2u}, {"string_stick", 1, -1, 4u},
                              {"bow_primary", 0, -1, 1u}, {"bow_secondary", 0, -1, 2u}, {"bow_stick", 0, -1, 4u}};
    for (const N& n : names)
        if (NameIs(name, n.name)) { out->rel = n.rel; out->hand = n.hand; out->mask = n.mask; return true; }
    return false;   // not a button this knows (a trigger or a grip is not one)
}

}  // namespace quiverin
