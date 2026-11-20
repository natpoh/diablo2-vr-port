#pragma once
// map_mode.h - the map's mode: one selector over the map keys, so a player does not look for them (Serg, 2026-11-18: "наверно в игре надо
// сделать селектор который может переключать карту в разные режимы чтобы люди это не искали"; Urzhya on Discord, 2026-11-17: the whole map on
// the left hand, not the minimap, and not gone when he attacks).
//
// Five modes, each a set of the keys that already exist ([hud] of d2r_vr.ini) - the selector only writes them, as D2R VR Settings' own boxes do:
//   0 Orb in the hand          map=1, map_orb=1, map_two_sizes=1, map_hand_full=0   the corner map on a glass ball in the hand; a laser click on it
//                                                                                    shows the game's big map in front, a click on that puts it back
//   1 Flat map in the hand     map=1, map_orb=0, map_two_sizes=1, map_hand_full=0   the same on a flat panel (map_width wide)
//   2 Big map on the left hand map=1, map_click_big=1, map_two_sizes=0, map_big_on_hand=1, map_big_keep=1, map_hand_full=0
//                                                                                    the laser click on the small map opens the game's big map held
//                                                                                    on the left hand; it stays while you attack, a click on it closes it
//   3 Whole map on the left hand map=1, map_hand_full=1                             the game's big map on the left hand all the time (vr/map_laser.h
//                                                                                    AutoOpen): no small map, nothing to click; a menu takes it down
//                                                                                    and it comes back
//   4 Map in the picture       map=0, map_hand_full=0                               nothing in the hand: the game's own corner map in the view, and the
//                                                                                    game's map key (a tap of the left stick) its whole-screen map
// What a mode leaves alone stays as it is (map_place, the orb's look, map_big_keep for modes 0 / 1, ...); a hand mode with map_place 0 (Not
// shown) puts the map on the left hand (1), or it would show nothing. Keys that match no mode: Custom (-1), as D2R VR Settings' Quality preset.
// Pure: no Windows, no game (cleanroom/tests/map_mode_test.cpp). Used by the headset panel's Map page (vr/hset_defs.h "@mapmode") and by D2R
// VR Settings (tools/settings/settings.cpp "@mapmode").

namespace mapmode {

enum Mode : int { kCustom = -1, kOrb = 0, kFlat = 1, kBigOnHand = 2, kWholeOnHand = 3, kInPicture = 4 };
constexpr int kCount = 5;

// The keys, as the ini holds them (0 / 1, map_place 0 - 4). The defaults are the plugin's for a key missing from the file.
struct Keys {
    int map = 0;          // [hud] map: the corner map out of the picture, into the hand (1)
    int orb = 1;          // [hud] map_orb: on a glass ball (1) or a flat panel (0)
    int place = 1;        // [hud] map_place: 0 not shown, 1 left hand, 2 right hand, 3 low in front, 4 chest
    int clickBig = 1;     // [hud] map_click_big
    int twoSizes = 1;     // [hud] map_two_sizes
    int onHand = 0;       // [hud] map_big_on_hand
    int keep = 0;         // [hud] map_big_keep
    int handFull = 0;     // [hud] map_hand_full
    bool operator==(const Keys& o) const {
        return map == o.map && orb == o.orb && place == o.place && clickBig == o.clickBig && twoSizes == o.twoSizes && onHand == o.onHand &&
               keep == o.keep && handFull == o.handFull;
    }
};

inline int FromKeys(const Keys& k) {
    if (!k.map) return kInPicture;
    if (k.handFull) return kWholeOnHand;
    if (!k.twoSizes && k.onHand && k.keep && k.clickBig) return kBigOnHand;
    if (k.twoSizes) return k.orb ? kOrb : kFlat;
    return kCustom;   // (the old three steps without the keep, ...: none of the five)
}

inline void ToKeys(int mode, Keys* k) {
    if (!k || mode < 0 || mode >= kCount) return;
    if (mode == kInPicture) { k->map = 0; k->handFull = 0; return; }
    k->map = 1;
    if (mode == kWholeOnHand) { k->handFull = 1; return; }
    k->handFull = 0;
    if (k->place == 0) k->place = 1;   // (a map in the hand that is "not shown" would show nothing)
    switch (mode) {
        case kOrb: k->orb = 1; k->twoSizes = 1; break;
        case kFlat: k->orb = 0; k->twoSizes = 1; break;
        case kBigOnHand: k->clickBig = 1; k->twoSizes = 0; k->onHand = 1; k->keep = 1; break;
        default: break;
    }
}

// A step of the selector: round the five (Custom steps to the first / last), no end - a click always changes it.
inline int Step(int mode, int delta) {
    const int d = delta < 0 ? -1 : 1;
    if (mode < 0 || mode >= kCount) return d > 0 ? 0 : kCount - 1;
    return (mode + d + kCount) % kCount;
}

// The modes' names, English (the headset's words: vr.map.mode.<n>, D2R VR Settings' its list).
constexpr const char* kNames[kCount] = {"Orb in the hand", "Flat map in the hand", "Big map on the left hand", "Whole map on the left hand",
                                        "Map in the picture"};
inline const char* Name(int mode) { return mode >= 0 && mode < kCount ? kNames[mode] : "Custom"; }

}  // namespace mapmode
