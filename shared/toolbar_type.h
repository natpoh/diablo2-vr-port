// toolbar_type.h - the toolbar's "type": ONE choice for where the game's bottom panel (its toolbar: the life and mana
// globes, the belt, the skill buttons) is in VR first person, over the two keys that already decide it. Pure; D2R VR
// Settings and the in-headset settings panel (vr/hset_*.h) both use it, the plugin does not need it: it goes on reading
// [hud] hide and [hud] bar_place exactly as it always did.
//
// Players (Discord, 2026-10-15): "sometimes at the bottom of the screen, sometimes not - how do I switch it on and
// off?", "the bottom panel hangs right in front of my eyes". Where the toolbar is came from two settings in two groups
// that only work together, and from what the game does with a panel open. All of it, as the code stands:
//
//   [hud] hide        0 nothing taken out of the picture, 1 the toolbar's strip cut out, 2 the whole interface out
//   [hud] bar_place   where the strip that was cut out hangs: 0 nowhere, 1 left forearm, 2 right forearm, 3 low in front,
//                     4 on the chest, low, 5 "wrists" (nothing hangs: the plugin draws life and mana as two balls)
//
//   F1, F2, F3 and the flat modes: the toolbar stays in the game's picture where the game draws it (the view's own
//     Toolbar and map tab sizes or moves it) - unless that view's "Toolbar and map" is set to "Hung in the room (as F4)":
//     then it follows the rules below.
//   F4, hide 0: the toolbar stays in the picture. In first person the picture moves with the head, so it sits at the
//     bottom of the view, right in front of the eyes, whatever bar_place says.
//   F4, hide 1 or 2: the strip is cut out of the picture and hung where bar_place says.
//   Any view, a panel open (the inventory, the stash, a trader, the game's menu) or no game running: nothing is cut out,
//     the game's whole interface is back - the toolbar with it - in the picture, or at the bottom of the menu panel when
//     [openxr] menu_3d hangs the menu in the room.
//
// The type picks both keys at once, so that choosing a place always works (a place with hide 0 did nothing):
//   Wrists            hide at least 1, bar_place 5     life on the left wrist, mana on the right, no toolbar
//   Chest, low        hide at least 1, bar_place 4     the whole toolbar on the chest, tipped up to read
//   Low in front      hide at least 1, bar_place 3     the whole toolbar low in front of you
//   Left forearm      hide at least 1, bar_place 1
//   Right forearm     hide at least 1, bar_place 2
//   Not shown         hide at least 1, bar_place 0
//   In the picture    hide 0 (bar_place kept)          the game's own, at the bottom of the view
// "At least 1": a hide of 2 (the whole interface out) stays 2. What an ini already says reads as the type it is:
// hide 0 is "In the picture" whatever bar_place holds; otherwise bar_place picks the type.
#pragma once

namespace toolbartype {

enum Type : int { kWrists = 0, kChest = 1, kLowFront = 2, kLeftForearm = 3, kRightForearm = 4, kNotShown = 5, kPicture = 6 };
constexpr int kCount = 7;

// Short, for the headset panel's value box (248 px).
constexpr const char* kShort[kCount] = {"Wrists", "Chest, low", "Low in front", "Left forearm", "Right forearm", "Not shown", "In the picture"};
// For D2R VR Settings' drop-down (the settings program is wide-character).
constexpr const wchar_t* kLongW[kCount] = {L"Watches on the wrists: life and mana",
                                           L"Whole toolbar on the chest, low",
                                           L"Whole toolbar low in front of you",
                                           L"Whole toolbar on the left forearm",
                                           L"Whole toolbar on the right forearm",
                                           L"Not shown",
                                           L"In the picture, in front of your eyes"};

inline int Clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// [hud] bar_place a type hangs the toolbar at (kPicture: none - the strip is not cut out).
inline int PlaceOf(int type) {
    switch (type) {
        case kWrists: return 5;
        case kChest: return 4;
        case kLowFront: return 3;
        case kLeftForearm: return 1;
        case kRightForearm: return 2;
        default: return 0;   // kNotShown (and kPicture, which does not use it)
    }
}

// The type an ini reads as.
inline int TypeOf(int hide, int place) {
    if (Clamp(hide, 0, 2) == 0) return kPicture;
    switch (Clamp(place, 0, 5)) {
        case 5: return kWrists;
        case 4: return kChest;
        case 3: return kLowFront;
        case 1: return kLeftForearm;
        case 2: return kRightForearm;
        default: return kNotShown;
    }
}

// The two keys for a type. `hide` and `place` come in as the ini has them; kPicture leaves place as it is (so that going
// back to a place keeps the one chosen before), any other type sets it and takes the strip out of the picture.
inline void Choose(int type, int* hide, int* place) {
    type = Clamp(type, 0, kCount - 1);
    int h = Clamp(*hide, 0, 2), p = Clamp(*place, 0, 5);
    if (type == kPicture) {
        h = 0;
    } else {
        p = PlaceOf(type);
        if (h < 1) h = 1;
    }
    *hide = h;
    *place = p;
}

// The headset panel's stepper: the next type (delta +1) or the one before (-1), stopping at both ends.
inline void Step(int delta, int* hide, int* place) {
    const int now = TypeOf(*hide, *place);
    Choose(Clamp(now + (delta < 0 ? -1 : delta > 0 ? 1 : 0), 0, kCount - 1), hide, place);
}

}  // namespace toolbartype
