#pragma once
// D2R Rooms' bridge - the D2RLoader plugin d2rl-d2r-rooms.dll (github.com/clauscodes/d2r-rooms, plugin/) - as D2R VR Settings'
// Multiplayer tab sees it. Plain data, no Windows or network code, so tools/rooms_bridge_test.cpp covers all of it.
//
//   <game>\d2rloader\plugins\d2r-rooms.ini          the player's settings: [relay] enabled / mode / use, [relays] name = wss://address [key].
//                                                   The plugin reads it again on every CREATE GAME, so a change here counts without a restart.
//   <game>\d2rloader\plugins\d2r-rooms-status.json  what the plugin knows, rewritten while the game runs: its version, the bridge's settings,
//                                                   each relay's state and quota, the room it hosts, the open rooms.
//   https://<relay>/healthz, <board>/api/rooms      the same facts asked from here while the game is closed.
//
// The rules are the plugin's own (0.9.5, plugin/src/relay_config.cpp ParseRelayIni / DefaultRelayIni, plugin/src/board.cpp StatusJson /
// Columns / PlayersLine), written again here: the tab must read the file the way the plugin will.
// Edits change one key's line and keep every other line and comment of the player's (WithKey).

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace roomsbridge {

inline constexpr const char* kIniName = "d2r-rooms.ini";
inline constexpr const char* kStatusName = "d2r-rooms-status.json";
inline constexpr const char* kPluginDll = "d2rl-d2r-rooms.dll";
inline constexpr const char* kDefaultRelayName = "D2R Rooms";
inline constexpr const char* kDefaultRelayUrl = "wss://d2r-rooms-relay.d2r-rooms.workers.dev";
inline constexpr const char* kDefaultBoardHost = "d2r-rooms.d2r-rooms.workers.dev";

namespace detail {
inline std::string_view Trim(std::string_view s) {
    while (!s.empty() && std::isspace((unsigned char)s.front())) s.remove_prefix(1);
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.remove_suffix(1);
    return s;
}
inline std::string Lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = (char)std::tolower((unsigned char)c);
    return out;
}
inline std::string Capital(std::string s) {
    if (!s.empty()) s[0] = (char)std::toupper((unsigned char)s[0]);
    return s;
}
// The lines of a text with their own line ends ("\r\n", "\n", none on the last).
inline std::vector<std::string> Lines(std::string_view text) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start < text.size()) {
        const size_t end = text.find('\n', start);
        if (end == std::string_view::npos) { out.emplace_back(text.substr(start)); break; }
        out.emplace_back(text.substr(start, end + 1 - start));
        start = end + 1;
    }
    return out;
}
}  // namespace detail

// The file the plugin writes when there is none (DefaultRelayIni, word for word): written here too when the player changes a setting
// before the game has ever run with the plugin.
inline std::string DefaultIni() {
    return std::string(
        "; D2R Rooms relays. A relay carries your game's bytes when friends cannot reach your PC\n"
        "; (no port forwarding, router without UPnP, carrier-grade NAT). Both PCs dial out to it.\n"
        "; Guide to running your own: docs/host_a_relay.md in the D2R Rooms repository.\n"
        "\n"
        "[relay]\n"
        "; enabled = yes: CREATE GAME lists the room and starts a relay when friends cannot reach you\n"
        "; enabled = no:  the plugin leaves CREATE GAME to D2RLoader (its own error when your port is closed)\n"
        "enabled = yes\n"
        "; auto   = only when D2RLoader reports that your port cannot be reached from outside\n"
        "; always = every room you host goes through a relay\n"
        "; never  = never (friends then need your port open)\n"
        "mode = auto\n"
        "; use = the relay to host on, by its name below; empty = the first one that answers\n"
        "use =\n"
        "\n"
        "[relays]\n"
        "; One relay per line:  name = wss://address [key]\n"
        "; The key is only for a private relay; its owner gives it to you.\n"
        "; When you host, the first relay that answers and takes new rooms is used.\n"
        "; When you join, the plugin uses the relay the host's room names.\n") +
        kDefaultRelayName + " = " + kDefaultRelayUrl + "\n";
}

// A relay's address (ParseRelayUrl): wss://host[:port][/path] or ws://...
struct RelayUrl {
    bool secure = true;
    std::string host;
    int port = 443;
    std::string path;   // "" or "/something", no trailing slash
};
inline std::optional<RelayUrl> ParseRelayUrl(std::string_view url) {
    RelayUrl u;
    if (url.substr(0, 6) == "wss://") url.remove_prefix(6);
    else if (url.substr(0, 5) == "ws://") { u.secure = false; u.port = 80; url.remove_prefix(5); }
    else return std::nullopt;
    const size_t slash = url.find('/');
    std::string_view authority = url.substr(0, slash);
    if (slash != std::string_view::npos) {
        std::string path(url.substr(slash));
        while (!path.empty() && path.back() == '/') path.pop_back();
        u.path = path;
    }
    const size_t colon = authority.rfind(':');
    if (colon != std::string_view::npos) {
        const std::string_view digits = authority.substr(colon + 1);
        if (digits.empty() || digits.size() > 5) return std::nullopt;
        int port = 0;
        for (const char c : digits) {
            if (!std::isdigit((unsigned char)c)) return std::nullopt;
            port = port * 10 + (c - '0');
        }
        if (port < 1 || port > 65535) return std::nullopt;
        u.port = port;
        authority = authority.substr(0, colon);
    }
    if (authority.empty() || authority.size() > 253) return std::nullopt;
    for (const char c : authority)
        if (!std::isalnum((unsigned char)c) && c != '.' && c != '-') return std::nullopt;
    u.host = std::string(authority);
    return u;
}
// The relay's status page: GET <path>/healthz on the same host and port (https for wss, http for ws).
inline std::string HealthPath(const RelayUrl& u) { return u.path + "/healthz"; }

struct Relay {
    std::string name, url, key;
};
struct Config {
    bool enabled = true;          // off: CREATE GAME is D2RLoader's alone
    std::string mode = "auto";    // "auto", "always", "never"
    std::string use;              // the relay to host on, by name ("" = the first that answers)
    std::vector<Relay> relays;    // never empty
};

// ParseRelayIni's rules: comments (; #) and unknown lines skipped, a relay with a bad address skipped, the same address once; no relay at
// all gives ours. A later line of a key wins.
inline Config ParseIni(std::string_view text) {
    using namespace detail;
    Config cfg;
    std::string section;
    for (const std::string& raw : Lines(text)) {
        const std::string_view line = Trim(raw);
        if (line.empty() || line.front() == ';' || line.front() == '#') continue;
        if (line.front() == '[' && line.back() == ']') { section = Lower(Trim(line.substr(1, line.size() - 2))); continue; }
        const size_t eq = line.find('=');
        if (eq == std::string_view::npos) continue;
        const std::string_view name = Trim(line.substr(0, eq));
        const std::string_view value = Trim(line.substr(eq + 1));
        if (section == "relay" && Lower(name) == "enabled") {
            const std::string v = Lower(value);
            cfg.enabled = !(v == "no" || v == "false" || v == "off" || v == "0");
        } else if (section == "relay" && Lower(name) == "use") {
            cfg.use = std::string(value);
        } else if (section == "relay" && Lower(name) == "mode") {
            const std::string v = Lower(value);
            cfg.mode = v == "always" || v == "never" ? v : "auto";
        } else if (section == "relays" && !name.empty()) {
            const size_t space = value.find_first_of(" \t");
            std::string url(Trim(value.substr(0, space)));
            const std::string key(space == std::string_view::npos ? std::string_view() : Trim(value.substr(space)));
            while (!url.empty() && url.back() == '/') url.pop_back();
            if (!ParseRelayUrl(url)) continue;
            if (std::none_of(cfg.relays.begin(), cfg.relays.end(), [&](const Relay& r) { return r.url == url; }))
                cfg.relays.push_back({std::string(name), url, key});
        }
    }
    if (cfg.relays.empty()) cfg.relays.push_back({kDefaultRelayName, kDefaultRelayUrl, ""});
    return cfg;
}

// Which relay a name in `use` is (HostOrder's comparison: letter case does not count); -1 = none of the list.
inline int RelayIndex(const Config& cfg, std::string_view name) {
    const std::string want = detail::Lower(name);
    for (size_t i = 0; i < cfg.relays.size(); ++i)
        if (detail::Lower(cfg.relays[i].name) == want) return (int)i;
    return -1;
}

// The text with `key = value` in [section]: every live line of that key in that section changed (its own line end kept), else one line
// added after the section's last line, else the section added at the end. Every other byte stays as it was. An empty value is "key =",
// as the plugin's own file writes it.
inline std::string WithKey(std::string_view text, std::string_view section, std::string_view key, std::string_view value) {
    using namespace detail;
    std::vector<std::string> lines = Lines(text);
    const std::string eol = text.find("\r\n") != std::string_view::npos ? "\r\n" : "\n";
    const std::string want = Lower(section), wantKey = Lower(key);
    const std::string line = std::string(key) + " =" + (value.empty() ? "" : " " + std::string(value));
    std::string cur;
    bool replaced = false;
    int last = -1;   // the target section's last line that is not blank (its header, at least)
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string_view t = Trim(lines[i]);
        if (!t.empty() && t.front() == '[' && t.back() == ']') {
            cur = Lower(Trim(t.substr(1, t.size() - 2)));
            if (cur == want) last = (int)i;
            continue;
        }
        if (cur != want || t.empty()) continue;
        last = (int)i;
        if (t.front() == ';' || t.front() == '#') continue;
        const size_t eq = t.find('=');
        if (eq == std::string_view::npos || Lower(Trim(t.substr(0, eq))) != wantKey) continue;
        const std::string& raw = lines[i];
        const std::string end = raw.size() >= 2 && raw.compare(raw.size() - 2, 2, "\r\n") == 0 ? "\r\n"
                                : !raw.empty() && raw.back() == '\n' ? "\n" : "";
        lines[i] = line + end;
        replaced = true;
    }
    if (!replaced && last >= 0) {
        std::string& at = lines[(size_t)last];
        if (at.empty() || at.back() != '\n') at += eol;
        lines.insert(lines.begin() + last + 1, line + eol);
    } else if (!replaced) {
        if (!lines.empty() && lines.back().back() != '\n') lines.back() += eol;
        if (!lines.empty() && !Trim(lines.back()).empty()) lines.push_back(eol);
        lines.push_back("[" + std::string(section) + "]" + eol);
        lines.push_back(line + eol);
    }
    std::string out;
    for (const std::string& l : lines) out += l;
    return out;
}

// --------------------------------------------------------------------------------------------------------------------------------------
// A small JSON reader for the status file and the two answers (UTF-8 in, UTF-8 strings out).
namespace json {
struct Value {
    enum class Kind { Null, Bool, Number, String, Array, Object } kind = Kind::Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Value> items;
    std::vector<std::pair<std::string, Value>> fields;

    const Value& operator[](std::string_view name) const {
        static const Value none;
        for (const auto& f : fields) if (f.first == name) return f.second;
        return none;
    }
    const Value& operator[](size_t i) const { static const Value none; return i < items.size() ? items[i] : none; }
    size_t size() const { return items.size(); }
    bool isNull() const { return kind == Kind::Null; }
    std::string str(std::string def = {}) const { return kind == Kind::String ? s : def; }
    double num(double def = 0) const { return kind == Kind::Number ? n : def; }
    int integer(int def = 0) const { return kind == Kind::Number ? (int)n : def; }
    bool boolean(bool def = false) const { return kind == Kind::Bool ? b : def; }
};

namespace parse {
struct Reader {
    std::string_view t;
    size_t at = 0;
    int depth = 0;
    void Space() { while (at < t.size() && (t[at] == ' ' || t[at] == '\t' || t[at] == '\r' || t[at] == '\n')) ++at; }
    bool Eat(char c) { Space(); if (at < t.size() && t[at] == c) { ++at; return true; } return false; }
    bool Word(std::string_view w) { if (t.substr(at, w.size()) == w) { at += w.size(); return true; } return false; }
    static void Put(std::string& out, uint32_t cp) {
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) { out += (char)(0xC0 | cp >> 6); out += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += (char)(0xE0 | cp >> 12); out += (char)(0x80 | (cp >> 6 & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
        else { out += (char)(0xF0 | cp >> 18); out += (char)(0x80 | (cp >> 12 & 0x3F)); out += (char)(0x80 | (cp >> 6 & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
    }
    bool Hex4(uint32_t* v) {
        if (at + 4 > t.size()) return false;
        *v = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = t[at++];
            *v <<= 4;
            if (c >= '0' && c <= '9') *v |= (uint32_t)(c - '0');
            else if (c >= 'a' && c <= 'f') *v |= (uint32_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') *v |= (uint32_t)(c - 'A' + 10);
            else return false;
        }
        return true;
    }
    bool String(std::string* out) {
        if (!Eat('"')) return false;
        while (at < t.size()) {
            const char c = t[at++];
            if (c == '"') return true;
            if ((unsigned char)c < 0x20) return false;
            if (c != '\\') { *out += c; continue; }
            if (at >= t.size()) return false;
            const char e = t[at++];
            switch (e) {
            case '"': case '\\': case '/': *out += e; break;
            case 'b': *out += '\b'; break;
            case 'f': *out += '\f'; break;
            case 'n': *out += '\n'; break;
            case 'r': *out += '\r'; break;
            case 't': *out += '\t'; break;
            case 'u': {
                uint32_t cp = 0;
                if (!Hex4(&cp)) return false;
                if (cp >= 0xD800 && cp < 0xDC00 && Word("\\u")) {
                    uint32_t lo = 0;
                    if (!Hex4(&lo) || lo < 0xDC00 || lo >= 0xE000) return false;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                Put(*out, cp);
                break;
            }
            default: return false;
            }
        }
        return false;
    }
    bool Number(double* v) {
        const size_t start = at;
        if (at < t.size() && t[at] == '-') ++at;
        while (at < t.size() && (std::isdigit((unsigned char)t[at]) || t[at] == '.' || t[at] == 'e' || t[at] == 'E' || t[at] == '+' || t[at] == '-')) ++at;
        if (at == start) return false;
        const std::string n(t.substr(start, at - start));
        char* end = nullptr;
        *v = std::strtod(n.c_str(), &end);
        return end && *end == 0;
    }
    bool Any(Value* v) {
        if (++depth > 32) return false;
        Space();
        if (at >= t.size()) return false;
        bool ok = true;
        const char c = t[at];
        if (c == '{') {
            v->kind = Value::Kind::Object;
            ++at;
            if (!Eat('}')) {
                do {
                    std::string name;
                    Space();
                    Value item;
                    if (!String(&name) || !Eat(':') || !Any(&item)) { ok = false; break; }
                    v->fields.emplace_back(std::move(name), std::move(item));
                } while (Eat(','));
                ok = ok && Eat('}');
            }
        } else if (c == '[') {
            v->kind = Value::Kind::Array;
            ++at;
            if (!Eat(']')) {
                do {
                    Value item;
                    if (!Any(&item)) { ok = false; break; }
                    v->items.push_back(std::move(item));
                } while (Eat(','));
                ok = ok && Eat(']');
            }
        } else if (c == '"') { v->kind = Value::Kind::String; ok = String(&v->s); }
        else if (Word("true")) { v->kind = Value::Kind::Bool; v->b = true; }
        else if (Word("false")) { v->kind = Value::Kind::Bool; v->b = false; }
        else if (Word("null")) v->kind = Value::Kind::Null;
        else { v->kind = Value::Kind::Number; ok = Number(&v->n); }
        --depth;
        return ok;
    }
};
}  // namespace parse

inline std::optional<Value> Parse(std::string_view text) {
    if (text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);
    parse::Reader r{text};
    Value v;
    if (!r.Any(&v)) return std::nullopt;
    r.Space();
    if (r.at != text.size()) return std::nullopt;
    return v;
}
}  // namespace json

// --------------------------------------------------------------------------------------------------------------------------------------
// A relay's state: "active", "near_limit", "limit", "no_answer", "unknown" (the plugin's StateCode). `usage` 0..1 of its quota, -1 unknown.
struct RelayRow {
    std::string name, url, state = "unknown";
    double usage = -1;
    bool first = false;   // the one a host tries first now
};
// A room as the plugin's list shows it (Columns, PlayersLine). `relayUrl` only from the board ("" in the status file).
struct RoomRow {
    std::string name, game, players, version, connection, who, relayUrl;
};
struct Status {
    std::string plugin, updated, board;
    bool enabled = true;
    std::string mode = "auto", use;
    std::vector<RelayRow> relays;
    bool hosting = false;          // the plugin hosts a room now
    std::string hostingRoom;       // its code
    std::string hostingRelay;      // the relay it goes through, "" = direct
    std::vector<RoomRow> rooms;
};

inline std::optional<Status> ParseStatus(std::string_view text) {
    const auto doc = json::Parse(text);
    if (!doc || doc->kind != json::Value::Kind::Object) return std::nullopt;
    const json::Value& d = *doc;
    Status s;
    s.plugin = d["plugin"].str();
    s.updated = d["updated"].str();
    s.board = d["board"].str();
    s.enabled = d["bridge"]["enabled"].boolean(true);
    s.mode = d["bridge"]["mode"].str("auto");
    s.use = d["bridge"]["use"].str();
    for (size_t i = 0; i < d["relays"].size(); ++i) {
        const json::Value& r = d["relays"][i];
        s.relays.push_back({r["name"].str(), r["url"].str(), r["state"].str("unknown"), r["usage"].num(-1), r["first"].boolean()});
    }
    const json::Value& h = d["hosting"];
    s.hosting = h.kind == json::Value::Kind::Object;
    s.hostingRoom = h["room"].str();
    s.hostingRelay = h["relay"].str();
    for (size_t i = 0; i < d["rooms"].size(); ++i) {
        const json::Value& r = d["rooms"][i];
        s.rooms.push_back({r["name"].str(), r["game"].str(), r["players"].str(), r["version"].str(), r["connection"].str(), r["who"].str(), {}});
    }
    return s;
}

// GET /healthz (ParseHealth): `status` 0 = no answer.
struct Health {
    std::string state = "no_answer";
    double usage = -1;
};
inline Health ParseHealth(int status, std::string_view body) {
    Health h;
    if (status != 200) return h;
    const auto doc = json::Parse(body);
    if (!doc || !(*doc)["ok"].boolean() || (*doc)["relay"].integer() != 1) return h;
    const std::string s = (*doc)["status"].str();
    if (s == "active" || s == "near_limit" || s == "limit") h.state = s;
    else h.state = (*doc)["open"].boolean(true) ? "active" : "limit";   // a relay from before "status"
    h.usage = (*doc)["usage"].num(-1);
    return h;
}

// The plugin's label of a mod in a room (ModLabel).
inline std::string ModLabel(const json::Value& mod) {
    const std::string id = mod["id"].str("none"), version = mod["version"].str();
    std::string label = id == "d2r-vr" ? "Authentic" : id == "d2r-pcvr" ? "Unleashed" : id == "none" ? "no VR mod" : id;
    if (!version.empty()) label += " " + version;
    return label;
}
// The relay's state in the plugin's words (RelayStateLabel), for a room's "relay: ..." column.
inline const char* StateLabel(std::string_view state) {
    if (state == "active") return "active";
    if (state == "near_limit") return "near its limit";
    if (state == "limit") return "limit reached";
    if (state == "no_answer") return "not answering";
    return "checking";
}

// GET <board>/api/rooms: {"rooms":[...]}, each room in the plugin's columns (ParseRooms + Columns + PlayersLine). A room's relay state
// comes from `stateOf(relay_url)` when given (the relays checked from here), else "checking". nullopt: not the board's answer.
template <class StateOf>
std::optional<std::vector<RoomRow>> ParseBoardRooms(std::string_view body, StateOf stateOf) {
    using detail::Capital;
    const auto doc = json::Parse(body);
    if (!doc || (*doc)["rooms"].kind != json::Value::Kind::Array) return std::nullopt;
    std::vector<RoomRow> out;
    const json::Value& list = (*doc)["rooms"];
    for (size_t i = 0; i < list.size(); ++i) {
        const json::Value& r = list[i];
        if (r["room_id"].str().empty()) continue;
        RoomRow row;
        row.name = r["name"].str() + " [" + r["code"].str() + "]" + (r["locked"].boolean() ? " (password)" : "");
        const int act = r["act"].integer();
        row.game = Capital(r["difficulty"].str("normal")) + (r["mode"].str("softcore") == "hardcore" ? " HC" : "") +
                   (act >= 1 ? ", Act " + std::to_string(act) : "");
        row.players = std::to_string(r["players_count"].integer(1)) + "/" + std::to_string(r["max_players"].integer(8));
        row.version = ModLabel(r["mod"]);
        row.relayUrl = r["relay_url"].str();
        row.connection = row.relayUrl.empty() ? "direct" : std::string("relay: ") + StateLabel(stateOf(row.relayUrl));
        const std::string roomMod = r["mod"]["id"].str("none");
        const json::Value& players = r["players"];
        for (size_t j = 0; j < players.size(); ++j) {
            const json::Value& p = players[j];
            if (!row.who.empty()) row.who += "  -  ";
            row.who += p["name"].str();
            const std::string cls = p["class"].str("unknown");
            if (cls != "unknown" && !cls.empty()) row.who += " " + Capital(cls);
            if (p["level"].integer() > 0) row.who += " " + std::to_string(p["level"].integer());
            if (p["act"].integer() > 0) row.who += " (A" + std::to_string(p["act"].integer()) + ")";
            if (!p["mod"].isNull() && p["mod"]["id"].str("none") != roomMod) row.who += " [" + ModLabel(p["mod"]) + "]";
        }
        out.push_back(std::move(row));
    }
    return out;
}
inline std::optional<std::vector<RoomRow>> ParseBoardRooms(std::string_view body) {
    return ParseBoardRooms(body, [](const std::string&) { return std::string("unknown"); });
}

// The board's host out of the status file's "board" ("https://host" or "https://host/"); "" when it is not an https address.
inline std::string BoardHost(std::string_view board) {
    if (board.substr(0, 8) != "https://") return {};
    board.remove_prefix(8);
    const size_t slash = board.find('/');
    return std::string(board.substr(0, slash));
}

// "2026-10-30T15:06:48Z" (the status file's `updated`, UTC) as seconds since 1970; false when it is not that.
inline bool ParseUtc(std::string_view s, int64_t* seconds) {
    // YYYY-MM-DDTHH:MM:SSZ, nothing else
    static constexpr char kShape[] = "0000-00-00T00:00:00Z";
    if (s.size() != sizeof kShape - 1) return false;
    for (size_t i = 0; i < s.size(); ++i)
        if (kShape[i] == '0' ? !std::isdigit((unsigned char)s[i]) : s[i] != kShape[i]) return false;
    const auto num = [&](size_t at, size_t n) { int v = 0; for (size_t i = at; i < at + n; ++i) v = v * 10 + (s[i] - '0'); return v; };
    const int y = num(0, 4), mo = num(5, 2), d = num(8, 2), h = num(11, 2), mi = num(14, 2), se = num(17, 2);
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || se > 60) return false;
    // days from 1970-01-01 (Howard Hinnant's days_from_civil)
    const int yy = y - (mo <= 2);
    const int era = (yy >= 0 ? yy : yy - 399) / 400;
    const unsigned yoe = (unsigned)(yy - era * 400);
    const unsigned doy = (unsigned)((153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const int64_t days = (int64_t)era * 146097 + (int64_t)doe - 719468;
    *seconds = days * 86400 + h * 3600 + mi * 60 + se;
    return true;
}

}  // namespace roomsbridge
