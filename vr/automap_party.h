// automap_party.h - the game's own "AutoMap Party" / "AutoMap Party Names" in its Settings.json, read (never written) at the plugin's start
// for one log line. Pure: the unit test is cleanroom/tests/automap_party_test.cpp, the caller vr/config/reconcile.cpp (AutoMapPartyNote).
//
// Options > Automap > Show Party / Show Party Names. At 0 the game draws no party member on any automap - the corner map, the whole-screen
// map, and so the map in the hand, the orb, the big map in front and the flat map (D2R.exe 3.3.93787: +0xD7707 skips a nearby party
// member's marker (types 1 and 4) while "AutoMap Party" is 0, +0xD2805 skips the far ones the roster has; +0xD77A0 draws no name while
// "AutoMap Party Names" is 0). A player who is not in the party is drawn either way (marker 5). Found 2026-11-05: Serg saw no ally on the
// map in a co-op game - his file had both at 0, flipped by the plugin's F11 / F12 with the game's default keys there ([view] own_hotkeys).
#pragma once

#include <cctype>
#include <cstdlib>
#include <string>

namespace automapparty {

// The whole number after "<key>": in the file's text; -1 when the key or its number is missing.
inline int Value(const std::string& text, const char* key) {
    const std::string quoted = std::string("\"") + key + "\"";
    const size_t at = text.find(quoted);
    if (at == std::string::npos) return -1;
    size_t b = at + quoted.size();
    while (b < text.size() && (text[b] == ' ' || text[b] == '\t')) ++b;
    if (b >= text.size() || text[b] != ':') return -1;
    ++b;
    while (b < text.size() && (text[b] == ' ' || text[b] == '\t' || text[b] == '\r' || text[b] == '\n')) ++b;
    size_t e = b;
    while (e < text.size() && std::isdigit((unsigned char)text[e])) ++e;
    if (e == b || e - b > 6) return -1;
    return std::atoi(text.c_str() + b);
}

struct State {
    int party = -1;   // "AutoMap Party": 0 the party members are not drawn on the automap, 1 they are, -1 not in the file
    int names = -1;   // "AutoMap Party Names": the same for their names
};

inline State Read(const std::string& text) { return State{Value(text, "AutoMap Party"), Value(text, "AutoMap Party Names")}; }

// What the log says: nullptr when both are on (or not in the file - the game's own default then).
inline const char* Advice(const State& s) {
    if (s.party == 0 && s.names == 0)
        return "the game draws no party member on its map, nor their names: an ally is missing from every map (the hand's, the big one, "
               "the flat one) - Options > Automap > Show Party and Show Party Names on";
    if (s.party == 0)
        return "the game draws no party member on its map: an ally is missing from every map (the hand's, the big one, the flat one) - "
               "Options > Automap > Show Party on";
    if (s.names == 0) return "the game draws its party members on the map without their names - Options > Automap > Show Party Names on";
    return nullptr;
}

}  // namespace automapparty
