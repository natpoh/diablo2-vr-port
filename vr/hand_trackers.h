#pragma once
// hand_trackers.h - are the runtime's hand trackers made (XR_EXT_hand_tracking, bare hands told from controllers)? Pure: no OpenXR, no
// Windows (cleanroom/tests/hand_trackers_test.cpp).
//
// Hand trackers that ask for bare hands start the headset's hand tracking. Meta's runtime tracks hands OR controllers (both at once only
// in its multimodal mode, XR_META_simultaneous_hands_and_controllers, which we never start): with our trackers it gave the Touch
// controllers' grips no place. A Quest 2 player over Link, Oculus runtime 1.115 (0.165 - 0.175, 2026-10-14 and 2026-10-21: "if i move
// my hand around the sword doesnt move with it, they're just stuck to the belt", the sticks and buttons working): in every native run of
// their logs the right grip was located with no flags at all and the left with a turn alone or a place alone (----, --Rr, -pR-, Pp--),
// the session focused and the actions active. The arms never reached the controllers.
//
// Auto (2): no trackers on Meta's runtime; every other runtime as before (SteamVR and Virtual Desktop give the hands beside the
// controllers). 1 = made wherever the runtime has hand tracking (0.175); 0 = never. With no trackers no bare hand is told from a
// controller: each hand is taken as a controller, as on a runtime without the data source.

#include <cstring>

namespace handtrackers {

enum class Mode { Never = 0, Always = 1, Auto = 2 };

enum class Why {
    Made,        // the trackers are made
    Off,         // [openxr] hand_trackers=0
    MetaRuntime, // Auto: Meta's runtime - its hand tracking takes the controllers' places away
};

// Meta's PC runtime (Link / Air Link) names itself "Oculus" ("runtime Oculus 1.115.0" in the log); a newer one may say "Meta".
inline bool MetaRuntime(const char* runtimeName) {
    return runtimeName && (std::strncmp(runtimeName, "Oculus", 6) == 0 || std::strncmp(runtimeName, "Meta", 4) == 0);
}

inline Why Decide(Mode mode, const char* runtimeName) {
    if (mode == Mode::Never) return Why::Off;
    if (mode == Mode::Auto && MetaRuntime(runtimeName)) return Why::MetaRuntime;
    return Why::Made;
}

inline Mode ModeOf(int v) { return v <= 0 ? Mode::Never : v == 1 ? Mode::Always : Mode::Auto; }

}  // namespace handtrackers
