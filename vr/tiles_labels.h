#pragma once
// What a tile of the skill panel says about its slot's button. Two things, both pure:
//
//  - the glyph: the game's own pad button of the slot (A, X, B, Y, RB, RT, and the same with the left trigger held) as the game draws
//    it - what the game's controller skill dialog shows. Always true: the slot IS that pad button, whatever the controllers are.
//  - the hint under the tile: which control of the player's own controllers presses that pad button in direct input's DEFAULT
//    layout (vr/direct_layout.h: B = the right hand's B, X = the left hand's first face button, Y = its second, RB = the right grip
//    with B). The face buttons are named as the controller names them - Touch has A B on the right and X Y on the left, Index has
//    A B on both - so the hint depends on the interaction profile each hand runs (xr.cpp ReadProfiles). A controller with no face
//    buttons (Vive, Windows Mixed Reality, the simple profile) or a profile not known gets no hint on those; A is the layout's right
//    trigger (every controller has one) and RT no control at all. The caller shows hints only
//    while direct input is on and its [direct] table is not rebound for those buttons (vr/skill_wheel.cpp): the hint is a statement
//    about the default layout and says nothing where it may be false.

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "wheel_types.h"

namespace tiles {

enum class Profile : uint8_t { None, Touch, Index, Vive, Wmr, Simple, Other };

// The interaction profile path the runtime reports for a hand ("/interaction_profiles/oculus/touch_controller"), or "" / null.
inline Profile ProfileOf(const char* path) {
    if (!path || !*path) return Profile::None;
    if (std::strstr(path, "touch_controller")) return Profile::Touch;     // oculus/ and meta/ (Touch, Touch Plus, Touch Pro)
    if (std::strstr(path, "index_controller")) return Profile::Index;
    if (std::strstr(path, "vive_controller")) return Profile::Vive;
    if (std::strstr(path, "motion_controller")) return Profile::Wmr;
    if (std::strstr(path, "simple_controller")) return Profile::Simple;
    return Profile::Other;
}

// Names of a hand's face buttons: the first (primary) and the second (secondary); null where the controller has none.
inline const char* FaceButton(Profile p, int hand, bool second) {
    if (p == Profile::Touch) return hand == 0 ? (second ? "Y" : "X") : (second ? "B" : "A");
    if (p == Profile::Index) return second ? "B" : "A";
    return nullptr;
}

enum class GlyphKind : uint8_t { Round, Chip };            // a round button (A X B Y) or a shoulder / trigger button's chip (RB, RT)
enum class GlyphColour : uint8_t { Green, Blue, Red, Yellow, Grey };
struct Glyph {
    GlyphKind kind = GlyphKind::Round;
    GlyphColour colour = GlyphColour::Grey;
    char text[4] = {};
    bool lt = false;       // the slot is the left trigger's row (the window marks the row once, with an LT chip; a title may add one)
};

// The mark of a slot 0..11 (wheel_types.h numbers): the game's own pad glyphs - A green, X blue, B red, Y yellow discs, RB and RT chips.
// A slot with the left trigger held has its base button's mark (the row says LT).
inline Glyph GlyphOf(int slot) {
    Glyph g;
    if (slot < 0 || slot >= wheel::kSlotCount) return g;
    const int base = wheel::BaseSlot(slot);
    g.lt = wheel::kSlotDefs[slot].alt;
    g.kind = base == 3 || base == 9 ? GlyphKind::Chip : GlyphKind::Round;
    g.colour = base == 0 ? GlyphColour::Red : base == 1 ? GlyphColour::Blue : base == 2 ? GlyphColour::Yellow : base == 8 ? GlyphColour::Green : GlyphColour::Grey;
    std::snprintf(g.text, sizeof g.text, "%s", base == 0 ? "B" : base == 1 ? "X" : base == 2 ? "Y" : base == 3 ? "RB" : base == 8 ? "A" : "RT");
    return g;
}

// The controller control that presses slot 0..11 in the default layout, for these two hands' profiles. False: no hint.
//   B, X, Y, RB: as the first paragraph says. A: the layout's attack, on the right trigger (so LT + A is LT + the right trigger).
//   RT: no control of a VR controller is bound to it.
inline bool HandHint(int slot, Profile left, Profile right, char* out, int n) {
    if (!out || n <= 0) return false;
    out[0] = 0;
    if (slot < 0 || slot >= wheel::kSlotCount) return false;
    const int base = wheel::BaseSlot(slot);
    const char* lt = wheel::kSlotDefs[slot].alt ? "LT + " : "";
    if (base == 8) {
        if (right == Profile::None) return false;
        std::snprintf(out, n, "%sR trigger", lt);
        return true;
    }
    if (base == 9) return false;
    const char* name = nullptr;
    const char* hand = base == 1 || base == 2 ? "Left" : "Right";
    if (base == 0) name = FaceButton(right, 1, true);              // B: the right hand's second face button
    else if (base == 1) name = FaceButton(left, 0, false);         // X: the left hand's first
    else if (base == 2) name = FaceButton(left, 0, true);          // Y: the left hand's second
    else name = FaceButton(right, 1, true);                        // RB: the right grip held, with the right hand's second face button
    if (!name) return false;
    if (base == 3) std::snprintf(out, n, "%sR grip + %s", lt, name);
    else std::snprintf(out, n, "%s%s %s", lt, hand, name);
    return true;
}

// The hints for a pair of profiles; `show` false (direct input off, or its layout rebound) leaves them all empty.
inline wheel::Hints BuildHints(Profile left, Profile right, bool show) {
    wheel::Hints h;
    if (!show) return h;
    for (int s = 0; s < wheel::kSlotCount; ++s) HandHint(s, left, right, h.text[s], wheel::kHintLen);
    return h;
}

}  // namespace tiles
