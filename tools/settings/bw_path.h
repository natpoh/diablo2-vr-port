// bw_path.h - how BodyWalk's input reaches the game, as D2R VR Settings' Status block says it ("bw_path"). Pure: the facts in, the verdict
// out; the unit test is cleanroom/tests/bw_path_test.cpp, the row tools/settings/settings.cpp.
//
// Game Link direct (Serg, 2026-10-17: "BodyWalk будет использоваться напрямую, без эмуляции джойстика, и маппинг тоже"): BodyWalk hands the
// game its raw controllers (Game Link Tracking 2, BW_LINK_TRACK_CONTROLS), the head and the hands, and its walk (the Walk block, BodyWalk's
// Output "Game Link"); the plugin's own layout does the mapping ([openxr] direct_input + [direct] flatvr_controls) and BodyWalk's gamepad
// report and Mapping are not read. Otherwise BodyWalk's gamepad and Mapping are the controls, as before.
#pragma once

#include <cstdint>

#include <bodywalk/game_link.h>

namespace bwpath {

struct Facts {
    bool trackingLive = false;    // BodyWalk's Tracking block moves (BodyWalk runs with Universal tracking output and Game Link)
    uint32_t trackingVersion = 0; // its header.version
    uint32_t trackingFlags = 0;   // BW_LINK_TRACK_*
    bool walkLive = false;        // the Walk block moves and says BW_LINK_WALK_ACTIVE (BodyWalk's Output is "Game Link")
    bool directInput = false;     // [openxr] direct_input
    bool flatVrControls = true;   // [direct] flatvr_controls
};

enum class Path {
    NoTracking,      // no Game Link tracking from BodyWalk: nothing to say about the path (the other rows say why)
    Direct,          // Game Link direct: raw controllers, head, hands and the walk
    DirectNoWalk,    // Game Link direct, but BodyWalk's walk does not come (its Output is not "Game Link")
    PadOldBodyWalk,  // BodyWalk's gamepad and Mapping: this BodyWalk / game_link.dll sends no raw controllers
    PadDirectOff,    // BodyWalk's gamepad and Mapping: "Controls without BodyWalk" ([openxr] direct_input) is off
    PadSwitchedOff,  // BodyWalk's gamepad and Mapping: [direct] flatvr_controls is off
};

inline bool RawControls(uint32_t version, uint32_t flags) {
    return version >= 2 && (flags & BW_LINK_TRACK_CONTROLS) != 0 && (flags & BW_LINK_TRACK_HEAD) != 0;
}

inline Path Decide(const Facts& f) {
    if (!f.trackingLive) return Path::NoTracking;
    if (!RawControls(f.trackingVersion, f.trackingFlags)) return Path::PadOldBodyWalk;
    if (!f.directInput) return Path::PadDirectOff;
    if (!f.flatVrControls) return Path::PadSwitchedOff;
    return f.walkLive ? Path::Direct : Path::DirectNoWalk;
}

// For the row's colour: fine, worth a look, or nothing to say.
enum class Tone { Ok, Warn, Unknown };
inline Tone ToneOf(Path p) {
    switch (p) {
    case Path::Direct: return Tone::Ok;
    case Path::NoTracking: return Tone::Unknown;
    default: return Tone::Warn;
    }
}

inline const wchar_t* Text(Path p) {
    switch (p) {
    case Path::Direct:
        return L"BodyWalk: Game Link direct - its raw controllers, head, hands and walk; no gamepad, no Mapping (the plugin's own layout)";
    case Path::DirectNoWalk:
        return L"BodyWalk: Game Link direct, but its walk does not come - set BodyWalk's Output to \"Game Link\" (Engine: FlatVR + D2R VR)";
    case Path::PadOldBodyWalk:
        return L"BodyWalk: through its gamepad and Mapping - this BodyWalk (or its Game Link plugin) sends no raw controllers: update it for Game Link direct";
    case Path::PadDirectOff:
        return L"BodyWalk: through its gamepad and Mapping - \"Controls without BodyWalk\" is off (Controls tab)";
    case Path::PadSwitchedOff:
        return L"BodyWalk: through its gamepad and Mapping - \"FlatVR: the controllers by this layout\" is off (Controls tab)";
    case Path::NoTracking:
    default:
        return L"BodyWalk's path to the game";
    }
}

}  // namespace bwpath
