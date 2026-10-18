// bw_output.h - BodyWalk's Output as D2R VR Settings reads it from BodyWalk's settings file, and what the Status block says of it.
// Pure: no file, no window; the unit test is cleanroom/tests/bw_output_test.cpp, the row is tools/settings/settings.cpp ("bw_output").
//
// Why the row: BodyWalk up to 1.83 writes its pad report - the pad the mod takes as BodyWalk's (pad_mirror_shared.h, its Mapping's
// buttons) - every frame only with the Output "Xbox Controller (vJoy)" or a virtual pad plugged in. With Keyboard (what the mod's
// profile's FlatVR preset left, up to 0.167) the report stood still, the mod dropped it after a second and no BodyWalk button reached
// the game in FlatVR; native OpenXR worked, where the mod reads the controllers itself (Serg, PSVR2, 2026-10-17). BodyWalk with
// the fix (branch claude/d2rvr-engine-link) writes it whenever the Game Link plugin is loaded, and has an Output "Game Link".
#pragma once

#include <string>

namespace bwout {

constexpr const char* kXbox = "Xbox Controller (vJoy)";
constexpr const char* kGameLink = "Game Link";

// The top-level "mode" of BodyWalk's settings file. BodyWalk writes it with nlohmann's dump(4), so a top-level key sits at the
// start of a line after exactly four spaces - which is what tells it from a "mode" inside a nested object. Empty: not found.
inline std::string Mode(const std::string& json) {
    static const std::string key = "    \"mode\"";
    for (size_t at = json.find(key); at != std::string::npos; at = json.find(key, at + 1)) {
        if (at != 0 && json[at - 1] != '\n') continue;   // deeper than the top level (or inside a line)
        size_t i = at + key.size();
        while (i < json.size() && (json[i] == ' ' || json[i] == '\t')) ++i;
        if (i >= json.size() || json[i] != ':') continue;
        ++i;
        while (i < json.size() && (json[i] == ' ' || json[i] == '\t')) ++i;
        if (i >= json.size() || json[i] != '"') return std::string();   // not a string
        const size_t end = json.find('"', i + 1);
        if (end == std::string::npos) return std::string();
        return json.substr(i + 1, end - i - 1);
    }
    return std::string();
}

enum class Verdict {
    Unknown,   // no settings file, or no "mode" in it
    Ok,        // Xbox Controller or Game Link: BodyWalk's buttons reach the game in FlatVR
    Warn,      // anything else: with BodyWalk up to 1.83 they do not
};

inline Verdict Judge(const std::string& mode) {
    if (mode.empty()) return Verdict::Unknown;
    if (mode == kXbox || mode == kGameLink) return Verdict::Ok;
    return Verdict::Warn;
}

}  // namespace bwout
