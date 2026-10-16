#pragma once
// The game's own frame cap ("Framerate Cap" in its Settings.json) and what D2R VR does to it. The headset's rate ([stereo] headset_rate)
// writes the cap in VR - a game frame per headset frame - and that cap must not stay in force in flat mode: the cap belongs to VR
// only. The player's own cap is remembered before the first VR write ([stereo] cap_before) and put back in flat mode; what VR
// wrote is remembered too ([stereo] cap_written), so a cap the player set himself is told from ours. No Windows in it: shared by
// vrcam (at its start, before the game reads the file) and D2R VR Settings (on a switch of the platform, on START, on every VR write),
// and tested without either. Same shape as shared/window_json.h (the player's own window), which it borrows its file reading from.

#include <cstdlib>
#include <string>

#include "window_json.h"

namespace framecap {

constexpr const char* kKey = "Framerate Cap";

// The key's whole number in the file, or -1 (no such key, or not a number).
inline int Read(const std::string& text, const char* key = kKey) {
    const size_t at = winjson::detail::After(text, key);
    if (at == std::string::npos || at >= text.size() || !winjson::detail::Digit(text[at])) return -1;
    return std::atoi(text.c_str() + at);
}
// The text with the key's number set to `value` - everything else byte for byte. The same text back (and `ok` false) when the key is
// missing or holds no number.
inline std::string With(const std::string& text, int value, bool* ok = nullptr, const char* key = kKey) {
    if (ok) *ok = false;
    const size_t at = winjson::detail::After(text, key);
    if (at == std::string::npos || at >= text.size() || !winjson::detail::Digit(text[at])) return text;
    size_t end = at;
    while (end < text.size() && winjson::detail::Digit(text[end])) ++end;
    std::string out = text;
    out.replace(at, end - at, std::to_string(value));
    if (ok) *ok = true;
    return out;
}

// The cap VR writes for the headset's rate and the 3D choice - ONE rule for D2R VR Settings (the write, the status line) and vrcam (the log):
//  - native OpenXR: none (0) - the headset paces the game itself (xrWaitFrame), a cap of the game's own could only make it miss frames;
//  - real stereo by alternate frames ("eyes by turns", afr on and pair_per_tick off): one eye a game frame = TWICE the rate;
//  - everything else - "one pass, two pictures" (both eyes from one game frame, the right one replayed: ONE present a pair), mono, the 3D
//    from the depth: the rate itself, a game frame a headset frame (vrcam's pacer holds the presents to it as well).
enum class Why { Native, EyesByTurns, OnePass, Mono };
struct VrCap {
    int value = 0;      // what is written (0 = none)
    int factor = 1;     // times the headset's rate (0 under native)
    Why why = Why::Mono;
};
inline int HzOfRate(int rateIndex) {   // [stereo] headset_rate 0..4
    static const int kHz[5] = {72, 75, 80, 90, 120};
    return kHz[rateIndex < 0 ? 0 : rateIndex > 4 ? 4 : rateIndex];
}
inline VrCap VrCapFor(int hz, bool afr, bool pairPerTick, bool native) {
    VrCap c;
    if (native) { c.value = 0; c.factor = 0; c.why = Why::Native; return c; }
    if (afr && !pairPerTick) { c.value = 2 * hz; c.factor = 2; c.why = Why::EyesByTurns; return c; }
    c.value = hz;
    c.factor = 1;
    c.why = afr ? Why::OnePass : Why::Mono;
    return c;
}
inline const char* Reason(Why w) {
    switch (w) {
    case Why::Native: return "native OpenXR: the headset paces the game itself, no cap of the game's own";
    case Why::EyesByTurns: return "eyes by turns draw one eye a game frame: twice the rate";
    case Why::OnePass: return "both eyes from one game frame (one pass, two pictures): a game frame a headset frame";
    default: return "one picture a frame (mono, the 3D from the depth): a game frame a headset frame";
    }
}

// What the ini kept: -1 = nothing.
struct State {
    int own = -1;       // [stereo] cap_before: the player's own cap, as the file had it before VR wrote its own
    int written = -1;   // [stereo] cap_written: the cap VR wrote last
};

struct Plan {
    bool saveOwn = false;    // keep `own` as the player's own cap
    int own = -1;
    bool write = false;      // write `value` into Settings.json
    int value = -1;
    bool saveWritten = false;   // keep `written`
    int written = -1;
    bool unknownOwn = false;    // flat mode, the file holds VR's cap and none of the player's was kept: nothing is written
    const char* why = "";
};

// VR writes `vrValue` (the headset's rate for the 3D choice; the settings' own "Game frame cap" slider counts as VR's too). `now` = the
// file's cap (-1: unreadable - then nothing). A cap in the file that is not the one VR wrote last is the player's own (or the game's
// write-back of one): it is kept first. VR's own, written before, leaves the kept one as it is.
inline Plan ForVr(int now, const State& s, int vrValue) {
    Plan p;
    if (now < 0 || vrValue < 0) return p;
    if (now != s.written) {
        p.saveOwn = true;
        p.own = now;
        p.why = "the player's own frame cap kept before VR's";
    }
    if (now != vrValue) {
        p.write = true;
        p.value = vrValue;
    }
    if (vrValue != s.written) {
        p.saveWritten = true;
        p.written = vrValue;
    }
    return p;
}

// FLAT mode: the cap is the player's own. The cap VR wrote (`written`) in the file goes, the kept one comes back; none kept: nothing is
// written (`unknownOwn`) - the mod chooses no cap in flat mode. Any other cap in the file is the player's own already, kept as such.
inline Plan ForFlat(int now, const State& s) {
    Plan p;
    if (now < 0) return p;
    if (s.written >= 0 && now == s.written) {
        if (s.own < 0) {
            p.unknownOwn = true;
            p.why = "VR's frame cap in the file and none of the player's kept";
            return p;
        }
        if (s.own != now) {
            p.write = true;
            p.value = s.own;
            p.why = "flat mode: the frame cap the player had before VR's";
        }
        return p;
    }
    if (now != s.own) {
        p.saveOwn = true;
        p.own = now;
        p.why = "the player's own frame cap kept";
    }
    return p;
}

}  // namespace framecap
