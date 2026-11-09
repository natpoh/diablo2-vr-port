// own_plate_why.h - which monster plate is in use now, ours ([hud] monster_names_own) or the game's, and why (Serg, 2026-11-09: "наши новые
// таблички не работали на поттато режиме, за место этого там криво старые таблички обрабатывались над монстрами"). For the log: one line each
// time the answer changes, with the view and the Quality preset's render stages, so a player's log says which plate he had in which view and
// preset. Pure (no Windows): tested by cleanroom/tests/own_plate_why_test.cpp.
#pragma once

namespace ownplatewhy {

struct In {
    bool own = false;          // [hud] monster_names_own
    bool allViews = false;     // [hud] monster_names_own_all_views
    bool firstPerson = false;  // VR F4 (FullBody, viewgate::F4)
    bool vrOurs = false;       // the mod's VR camera draws the view (g_enabled, platform 1)
    bool stereo = false;       // a stereo pair from our camera (AfrOn)
    bool dropGame = false;     // [hud] own_labels_drop_game: the game's plate not drawn at all (its Draw hook; else hidden by the effect)
    bool drawHook = false;     // the plate panel's Draw slot ours (labels.cpp InstallPlateDraw): the game's plate not drawn while ours is on
    bool textHook = false;     // the game's TextDraw hooked (the plate's texts recorded for ours)
    bool effectReady = false;  // D2R_DepthFog.fx runs and takes the interface out (hud::PictureReady)
    bool layerOut = false;     // the interface layer was taken out this frame (hud::LabelsNow)
    bool namesOn = false;      // the effect drew our names this frame (NamesSetPlate)
};

enum Why : int {
    kOurs = 0,       // ours drawn, the game's plate not drawn
    kOff,            // [hud] monster_names_own 0
    kF4Only,         // not F4, and monster_names_own_all_views 0
    kNotOurView,     // the flat game, or the game's own camera (flat F1)
    kNoStereo,       // no stereo pair from our camera
    kNoDrop,         // own_labels_drop_game 0: the game's plate drawn, hidden under ours by the effect only (and only while it takes the layer out)
    kNoDrawHook,     // the plate's Draw hook not in (yet): the game's plate still drawn
    kNoEffect,       // the game's plate hidden; ours not drawn: the effect not running / old
    kNoLayer,        // the game's plate hidden; ours not drawn: the interface layer not taken out (a menu open, no layer yet)
    kNoTextHook,     // the game's plate hidden; ours without its names yet (TextDraw not hooked)
    kNotYet,         // the game's plate hidden; ours not drawn yet this moment
};

// Wanted at all here (as vr/hud/labels.cpp MonsterNamesOwnNow): the first reason it is not, else kOurs. (The room bar, monster_bar_room, stands
// aside for ours since 2026-11-09, as every old plate way does: no reason of its own.)
inline Why Wanted(const In& s) {
    if (!s.own) return kOff;
    if (!s.vrOurs) return kNotOurView;
    if (s.firstPerson) return kOurs;
    if (!s.allViews) return kF4Only;
    if (!s.stereo) return kNoStereo;
    return kOurs;
}

inline Why Of(const In& s) {
    const Why w = Wanted(s);
    if (w != kOurs) return w;
    if (!s.dropGame) return kNoDrop;
    if (!s.drawHook) return kNoDrawHook;
    if (!s.effectReady) return kNoEffect;
    if (!s.layerOut) return kNoLayer;
    if (!s.namesOn) return kNotYet;
    if (!s.textHook) return kNoTextHook;
    return kOurs;
}

// Whether the game's plate is drawn at all (its pictures, fills, texts): with ours wanted and the Draw hook in it never is - hidden whether or not
// ours made it to the picture this frame (Serg, 2026-11-09: "просто прятать этот слой"; until then it hid only once the effect had said ours was on)
inline bool GamePlateDrawn(Why w) { return w != kOurs && w != kNoEffect && w != kNoLayer && w != kNoTextHook && w != kNotYet; }

inline const char* Text(Why w) {
    switch (w) {
    case kOurs: return "ours - the game's plate not drawn (its pictures, fills and texts left out; none of the old plate's ways)";
    case kOff: return "the game's - [hud] monster_names_own 0";
    case kF4Only: return "the game's - ours is F4 only ([hud] monster_names_own_all_views 0)";
    case kNotOurView: return "the game's - not a view of the mod's VR camera (the flat game, or flat F1)";
    case kNoStereo: return "the game's - no stereo pair from the mod's camera here";
    case kNoDrop: return "the game's drawn, hidden by the effect under ours ([hud] own_labels_drop_game 0: not left out)";
    case kNoDrawHook: return "the game's - the plate's Draw hook not in (not decrypted yet, or another game build)";
    case kNoEffect: return "none - the game's hidden, ours NOT drawn: D2R_DepthFog.fx not running or old";
    case kNoLayer: return "none - the game's hidden, ours not drawn: the interface layer not taken out (a menu open, or no layer found yet)";
    case kNoTextHook: return "ours without names - the game's hidden, TextDraw not hooked yet";
    case kNotYet: return "none for now - the game's hidden, ours not drawn yet";
    }
    return "?";
}

// The Quality preset by its render stages ([render] game_stages: 0 as the game, 1 Low, 2 Potato)
inline const char* StagesName(int gameStages) { return gameStages >= 2 ? "Potato" : gameStages == 1 ? "Low" : "Medium or the game's"; }

}  // namespace ownplatewhy
