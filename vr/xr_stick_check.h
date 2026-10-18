#pragma once
// xr_stick_check.h - is the thumbstick bound? Pure: counts in, a verdict and its words out; vr/xr.cpp counts what the runtime bound our actions to
// (xrEnumerateBoundSourcesForAction), cleanroom/tests/xr_stick_check_test.cpp checks the rule.
//
// The report of 2026-10-18 (a PS VR2 player on SteamVR, native OpenXR, our own controls; he had d2r-pcvr installed before): "every button works except the
// thumbsticks - I cannot walk or turn". SteamVR keeps the controller bindings of an OpenXR game per program and per action, and D2RLoader.exe is the program
// of every mod that runs under it: a binding saved for another mod's actions (or for an older shape of ours) can leave OUR thumbstick action with no source
// while the buttons - actions the saved file never touched - are bound by default. Then the stick reads as inactive, zero, forever, and nothing says why.
//
// So: after the runtime has bound the actions, count the sources of the stick action against the buttons'. A stick with none while the buttons have some is
// unbound. A second stick action of another name (never in anyone's saved file) is bound to the same inputs as a spare: where it has a source, it is read
// instead - the same hardware, the same value, read through a second way; where it has none either, only the words are left (reset SteamVR's binding).

#include <cstdint>

namespace xrstick {

// What the runtime bound, as counts of sources (a source is one physical input, per hand).
struct Facts {
    int stick = 0;       // the thumbstick action
    int stickSpare = 0;  // the spare thumbstick action
    int buttons = 0;     // the other actions together (trigger, grip, A / X, B / Y, stick click, menu): "the controllers are bound at all"
    bool stickExpected = true;   // a controller that HAS a thumbstick (and that we suggested one for) is in a hand: not the simple controller, not a bare hand
};

enum class Verdict : uint8_t {
    Unknown,         // nothing is bound yet (no controllers on, the runtime has not answered): no conclusion
    Bound,           // the thumbstick has a source: all as it should be
    UnboundSpare,    // the thumbstick has none, the buttons have, the spare thumbstick has: read through the spare
    Unbound,         // the thumbstick has none, the buttons have, the spare has none either: only the words
};

inline Verdict Decide(const Facts& f) {
    if (!f.stickExpected) return Verdict::Unknown;   // a controller with no thumbstick to bind: nothing is missing
    if (f.stick > 0) return Verdict::Bound;
    if (f.buttons <= 0) return Verdict::Unknown;   // the buttons are not bound either: not a thumbstick problem, nothing known yet
    return f.stickSpare > 0 ? Verdict::UnboundSpare : Verdict::Unbound;
}

// The spare is the one to read (the action whose state is the thumbstick).
inline bool UseSpare(Verdict v) { return v == Verdict::UnboundSpare; }

// The words of the log and of D2R VR Settings (Home > Status). The way out is SteamVR's, spelled as the player finds it.
inline const char* LogText(Verdict v) {
    switch (v) {
    case Verdict::Bound: return "SteamVR bound the thumbstick to our action: the sticks reach the game";
    case Verdict::UnboundSpare:
        return "SteamVR bound NOTHING to our thumbstick action while it bound the buttons (a binding saved for this program - another mod's, or an older one - hides it): the "
               "sticks are read through a second thumbstick action instead. To be rid of it: SteamVR > Settings > Controllers > Manage Controller Bindings > D2RLoader > default";
    case Verdict::Unbound:
        return "SteamVR bound NOTHING to the thumbstick - neither to our thumbstick action nor to the spare one - while it bound the buttons: the sticks do not reach the game "
               "(no walking, no turning). Reset SteamVR's controller binding for this game: SteamVR > Settings > Controllers > Manage Controller Bindings > D2RLoader > default";
    default: return "";
    }
}
inline const wchar_t* StatusText(Verdict v) {
    switch (v) {
    case Verdict::Bound: return L"SteamVR gives the game the thumbsticks";
    case Verdict::UnboundSpare:
        return L"SteamVR bound nothing to the thumbstick (a saved binding of another mod?) - the sticks are read another way and work. To tidy it: SteamVR > Settings > "
               L"Controllers > Manage Controller Bindings > D2RLoader > default";
    case Verdict::Unbound:
        return L"SteamVR bound nothing to the thumbstick - reset SteamVR's controller binding for this game (SteamVR > Settings > Controllers > Manage Controller "
               L"Bindings > D2RLoader > default)";
    default: return L"Not known yet (the game has to run with the controllers on)";
    }
}

// The verdict in the state block the plugin shares with D2R VR Settings (shared/d2r_vr_state.h, twoHanded bits 13 - 15).
constexpr uint32_t kBitKnown = 1u << 13, kBitUnbound = 1u << 14, kBitSpare = 1u << 15;
inline uint32_t Bits(Verdict v) {
    switch (v) {
    case Verdict::Bound: return kBitKnown;
    case Verdict::UnboundSpare: return kBitKnown | kBitUnbound | kBitSpare;
    case Verdict::Unbound: return kBitKnown | kBitUnbound;
    default: return 0;
    }
}
inline Verdict FromBits(uint32_t twoHanded) {
    if (!(twoHanded & kBitKnown)) return Verdict::Unknown;
    if (!(twoHanded & kBitUnbound)) return Verdict::Bound;
    return (twoHanded & kBitSpare) ? Verdict::UnboundSpare : Verdict::Unbound;
}

}  // namespace xrstick
