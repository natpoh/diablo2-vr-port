#pragma once
// The Co-op page of the in-headset settings: the co-op relay bridge of D2R Rooms - a D2RLoader plugin of its own (d2rl-d2r-rooms.dll in
// <game>\d2rloader\plugins; Setup's ticked "D2R Rooms" task installs it). Serg, 2026-10-30: "а я не знал что у нас можно выбирать надо во
// внутренний оверлей добавить" - the choices were only in d2r-rooms.ini and on D2R VR Settings > Multiplayer.
//
// The same three choices as that tab, the same file, the same writes (shared/rooms_bridge.h has the plugin's formats):
//   [relay] enabled  yes / no               the bridge on: CREATE GAME lists the room and opens a relay when friends cannot reach you
//   [relay] mode     auto / always / never  when a room you host goes through a relay
//   [relay] use      a relay's name, or ""  the relay tried first ("" = the first one that answers)
// Each change is written at once (the store's thread, vr/hset_store.cpp WriteCoop: that key's line, every other line of the player's kept), not
// through Apply: the file is the plugin's, not d2r_vr.ini. None needs a restart: the plugin reads the file again on every CREATE GAME
// (its LoadRelayConfig), so a change counts from the next room you host; a room open now keeps its way. The page says so, and shows what
// the plugin still has in force (d2r-rooms-status.json's "bridge") while it differs from the file.
// Read only: is the plugin there, its relays and their state, the room you host, the open rooms (the status file the plugin rewrites while
// the game runs). The relay list itself is edited in d2r-rooms.ini (D2R VR Settings > Multiplayer opens it).
//
// Pure: no Windows, no file. hset_test.exe runs it.

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "rooms_bridge.h"
#include "ui_text.h"   // the page's words in the player's language ([hud] headset_language)

namespace hset::coop {

// What the store's thread read of the plugin's files (vr/hset_store.cpp LoadCoop). A new one is made only when something in it changed.
struct State {
    bool installed = false;      // d2rl-d2r-rooms.dll in the plugins folder: without it the page offers nothing
    bool iniThere = false;       // d2r-rooms.ini there (else `cfg` is the plugin's default text, which is written first, as the plugin and Settings do)
    roomsbridge::Config cfg;     // the ini's [relay] and [relays]
    bool statusThere = false;    // d2r-rooms-status.json read
    roomsbridge::Status status;
    int ageMin = -1;             // how old the status file's "updated" is, whole minutes (-1: not known)
    uint64_t written = 0;        // the sequence of the last edit the thread carried out ...
    bool writeFailed = false;    // ... and whether that one failed
    std::string dir;             // the plugins folder (UTF-8), for the line that says it is not there
};

enum What : int { kEnabled = 0, kMode = 1, kUse = 2 };
constexpr int kWhatCount = 3;

// One [relay] line to write, as D2R VR Settings writes it: enabled yes / no, mode auto / always / never, use a relay's name or "".
struct Edit {
    std::string key, value;
    uint64_t sequence = 0;
    bool operator==(const Edit&) const = default;
};
// What the store takes: one of the three keys, a value the plugin reads back as it is meant (no line break, no comment).
inline bool Sane(const Edit& e) {
    if (e.key == "enabled") return e.value == "yes" || e.value == "no";
    if (e.key == "mode") return e.value == "auto" || e.value == "always" || e.value == "never";
    if (e.key != "use" || e.value.size() > 128) return false;
    for (const char c : e.value)
        if (c == '\r' || c == '\n' || c == ';' || c == '#' || c == '=' || c == '[' || c == ']') return false;
    return e.value == std::string(roomsbridge::detail::Trim(e.value));
}

// The modes in D2R VR Settings' order.
inline constexpr const char* kModes[3] = {"auto", "always", "never"};
inline int ModeIndex(const std::string& m) { return m == "always" ? 1 : m == "never" ? 2 : 0; }

// The "use" choices: "" (the first that answers), the relays by name, then a name the file has that is not in the list (kept, as Settings
// keeps it: the plugin then goes by the list's order).
inline std::vector<std::string> UseChoices(const roomsbridge::Config& c) {
    std::vector<std::string> out(1);
    for (const roomsbridge::Relay& r : c.relays) out.push_back(r.name);
    if (!c.use.empty() && roomsbridge::RelayIndex(c, c.use) < 0) out.push_back(c.use);
    return out;
}
// Where `use` stands in UseChoices (the relays' names compared as the plugin does: letter case does not count).
inline int UseIndex(const roomsbridge::Config& c) {
    if (c.use.empty()) return 0;
    const int i = roomsbridge::RelayIndex(c, c.use);
    return i >= 0 ? i + 1 : (int)c.relays.size() + 1;
}

// The file's choices with an edit on its way in place (the page shows the click at once).
inline roomsbridge::Config Shown(roomsbridge::Config c, const Edit* pending) {
    if (!pending) return c;
    if (pending->key == "enabled") c.enabled = pending->value == "yes";
    else if (pending->key == "mode") c.mode = pending->value;
    else if (pending->key == "use") c.use = pending->value;
    return c;
}

// A press on a row: the bridge's switch flips, the two choices step round their lists (-1 / +1). Nothing when it changes nothing (one
// relay and no other choice than the first that answers is still two choices: "" and its name).
inline std::optional<Edit> Step(const roomsbridge::Config& c, int what, int delta) {
    if (what == kEnabled) return Edit{"enabled", c.enabled ? "no" : "yes"};
    if (what == kMode) {
        const int n = 3, at = ModeIndex(c.mode);
        return Edit{"mode", kModes[(at + (delta < 0 ? n - 1 : 1)) % n]};
    }
    if (what == kUse) {
        const std::vector<std::string> list = UseChoices(c);
        const int n = (int)list.size(), at = std::clamp(UseIndex(c), 0, n - 1);
        if (n < 2) return std::nullopt;
        return Edit{"use", list[(size_t)((at + (delta < 0 ? n - 1 : 1)) % n)]};
    }
    return std::nullopt;
}

// -- texts ------------------------------------------------------------------------------------------------------
inline const char* Label(int what) {
    switch (what) {
        case kEnabled: return uitext::Tr("vr.coop.enabled", "Co-op bridge");
        case kMode: return uitext::Tr("vr.coop.mode", "Go through a relay");
        default: return uitext::Tr("vr.coop.use", "Host my rooms on");
    }
}
inline std::string Detail(const roomsbridge::Config& c, int what) {
    switch (what) {
        case kEnabled:
            return c.enabled ? uitext::Tr("vr.coop.enabled.on", "CREATE GAME lists your room; a relay when friends cannot reach you")
                             : uitext::Tr("vr.coop.enabled.off", "CREATE GAME is D2RLoader's alone: no list, no relay");
        case kMode:
            return ModeIndex(c.mode) == 1 ? uitext::Tr("vr.coop.mode.always_detail", "Every room you host goes through a relay")
                 : ModeIndex(c.mode) == 2 ? uitext::Tr("vr.coop.mode.never_detail", "Friends connect straight to you: your port open")
                                          : uitext::Tr("vr.coop.mode.auto_detail", "Only when your port cannot be reached from outside");
        default:
            return uitext::Tr("vr.coop.use.detail", "Tried first; joining uses the host's relay");
    }
}
// A row's value box: On / Off, Auto / Always / Never, a relay's name.
inline std::string ValueText(const roomsbridge::Config& c, int what) {
    switch (what) {
        case kEnabled: return c.enabled ? uitext::Tr("vr.hset.value.on", "On") : uitext::Tr("vr.hset.value.off", "Off");
        case kMode:
            return ModeIndex(c.mode) == 1 ? uitext::Tr("vr.coop.mode.always", "Always")
                 : ModeIndex(c.mode) == 2 ? uitext::Tr("vr.coop.mode.never", "Never")
                                          : uitext::Tr("vr.coop.mode.auto", "Auto");
        default:
            if (c.use.empty()) return uitext::Tr("vr.coop.use.first", "First that answers");
            if (const int i = roomsbridge::RelayIndex(c, c.use); i >= 0) return c.relays[(size_t)i].name;
            return uitext::TrF("vr.coop.use.missing", "{name} (not listed)", {{"name", c.use}});
    }
}

// The plugin's state of a relay in the page's words.
inline const char* StateText(const std::string& state) {
    if (state == "active") return uitext::Tr("vr.coop.state.active", "takes rooms");
    if (state == "near_limit") return uitext::Tr("vr.coop.state.near_limit", "near its limit");
    if (state == "limit") return uitext::Tr("vr.coop.state.limit", "limit reached");
    if (state == "no_answer") return uitext::Tr("vr.coop.state.no_answer", "not answering");
    return uitext::Tr("vr.coop.state.unknown", "not checked yet");
}
inline std::string AgeText(int min) {
    if (min < 0) return uitext::Tr("vr.coop.age.unknown", "at a time not known");
    if (min < 1) return uitext::Tr("vr.coop.age.now", "just now");
    if (min < 60) return uitext::TrF("vr.coop.age.min", "{n} min ago", {{"n", std::to_string(min)}});
    return uitext::TrF("vr.coop.age.h", "{n} h ago", {{"n", std::to_string(min / 60)}});
}

// The read-only lines under the rows, top to bottom; at most kLines (the note on when a change counts is always the last).
enum class Tone : uint8_t { Quiet, Good, Warn, Head };
struct Line {
    std::string text;
    Tone tone = Tone::Quiet;
};
constexpr int kLines = 7;
constexpr int kRelayLines = 3, kRoomLines = 2;

inline std::string ModeWord(const std::string& m) { return ModeIndex(m) == 1 ? "always" : ModeIndex(m) == 2 ? "never" : "auto"; }
inline std::string InForceText(const roomsbridge::Status& s) {
    const std::string use = s.use.empty() ? std::string(uitext::Tr("vr.coop.use.first_lower", "the first that answers")) : s.use;
    return s.enabled ? uitext::TrF("vr.coop.in_force_on", "In force until your next CREATE GAME: bridge on, {mode}, {use}.", {{"mode", ModeWord(s.mode)}, {"use", use}})
                     : std::string(uitext::Tr("vr.coop.in_force_off", "In force until your next CREATE GAME: bridge off."));
}
// The plugin still has other choices in force than the file now says (they are read again on the next CREATE GAME).
inline bool Differs(const roomsbridge::Status& s, const roomsbridge::Config& c) {
    if (s.enabled != c.enabled) return true;
    if (!c.enabled) return false;
    return ModeIndex(s.mode) != ModeIndex(c.mode) || roomsbridge::detail::Lower(s.use) != roomsbridge::detail::Lower(c.use);
}

// `most`: the lines there is room for (fewer under the Co-op page's map row, hset_model.h kCoopLinesUnderMap).
inline std::vector<Line> Lines(const State* st, const roomsbridge::Config& shown, int most = kLines) {
    std::vector<Line> out;
    if (!st) {
        out.push_back({uitext::Tr("vr.coop.reading", "Reading D2R Rooms' files..."), Tone::Quiet});
        return out;
    }
    if (!st->installed) {
        out.push_back({uitext::Tr("vr.coop.missing", "The D2R Rooms plugin is not installed: no d2rl-d2r-rooms.dll in this folder:"), Tone::Warn});
        out.push_back({st->dir.empty() ? std::string("d2rloader\\plugins") : st->dir, Tone::Quiet});
        out.push_back({uitext::Tr("vr.coop.missing_how", "D2R VR Authentic's Setup installs it (its D2R Rooms task). Nothing to choose until then."), Tone::Quiet});
        return out;
    }
    const roomsbridge::Status& s = st->status;
    if (st->statusThere)
        out.push_back({uitext::TrF("vr.coop.plugin", "D2R Rooms {version}: its last word {age}.", {{"version", s.plugin.empty() ? std::string("?") : s.plugin}, {"age", AgeText(st->ageMin)}}),
                       Tone::Head});
    else
        out.push_back({uitext::Tr("vr.coop.plugin_no_status", "D2R Rooms is installed; it writes what it knows while the game runs (nothing yet)."), Tone::Head});
    const auto room = [&](size_t keep) { return out.size() + keep < (size_t)most; };   // a line more fits with `keep` still to come (the note)
    if (st->statusThere && Differs(s, shown)) out.push_back({InForceText(s), Tone::Warn});
    // the relays: the plugin's view when there is one, else the file's list unchecked
    int n = 0;
    if (st->statusThere && !s.relays.empty()) {
        for (const roomsbridge::RelayRow& r : s.relays) {
            if (n++ >= kRelayLines || !room(1)) break;
            std::string t = r.usage >= 0 ? uitext::TrF("vr.coop.relay_usage", "{name}: {state}, {pct}% of its quota used",
                                                       {{"name", r.name}, {"state", StateText(r.state)}, {"pct", std::to_string((int)(r.usage * 100.0 + 0.5))}})
                                         : uitext::TrF("vr.coop.relay", "{name}: {state}", {{"name", r.name}, {"state", StateText(r.state)}});
            if (r.first) t += std::string("  -  ") + uitext::Tr("vr.coop.first", "tried first");
            out.push_back({t, r.state == "active" ? Tone::Good : r.state == "unknown" ? Tone::Quiet : Tone::Warn});
        }
    } else {
        for (const roomsbridge::Relay& r : st->cfg.relays) {
            if (n++ >= kRelayLines || !room(1)) break;
            out.push_back({uitext::TrF("vr.coop.relay", "{name}: {state}", {{"name", r.name}, {"state", StateText("unknown")}}), Tone::Quiet});
        }
    }
    if (st->statusThere && s.hosting && room(1)) {
        const std::string code = s.hostingRoom.empty() ? std::string("?") : s.hostingRoom;
        out.push_back({s.hostingRelay.empty() ? uitext::TrF("vr.coop.hosting_direct", "You host room {code}: friends connect straight to you.", {{"code", code}})
                                              : uitext::TrF("vr.coop.hosting_relay", "You host room {code} through {relay}.", {{"code", code}, {"relay", s.hostingRelay}}),
                       Tone::Good});
    }
    if (st->statusThere && room(1)) {
        out.push_back({s.rooms.empty() ? std::string(uitext::Tr("vr.coop.rooms_none", "No open rooms in the list."))
                                       : uitext::TrF("vr.coop.rooms", "Open rooms: {n}", {{"n", std::to_string(s.rooms.size())}}),
                       Tone::Quiet});
        int k = 0;
        for (const roomsbridge::RoomRow& r : s.rooms) {
            if (k++ >= kRoomLines || !room(1)) break;
            std::string t = "  " + r.name;
            for (const std::string* part : {&r.game, &r.players, &r.version, &r.connection})
                if (!part->empty()) t += "  -  " + *part;
            out.push_back({t, Tone::Quiet});
        }
    }
    out.push_back({uitext::Tr("vr.coop.when", "Read again on every CREATE GAME: a change counts from your next one, no restart. A room open now keeps its way."),
                   Tone::Quiet});
    return out;
}

}  // namespace hset::coop
