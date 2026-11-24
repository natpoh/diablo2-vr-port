// shared/problem_report.h - "Report a problem" (Home > D2R VR, Serg 2026-11-10): a problem that is not a crash ("the fog doesn't work",
// "the cave ceilings are too low"). The player describes it in D2R VR Settings; Settings packs the Collect logs zip with that text
// (problem.txt) and the settings the player changed from the shipped ones (settings_changed.txt), and D2R VR Crash Reporter's
// --send-logs window sends it to the BodyWalk server (kind "logs"; it sends nothing until the player presses Send there).
//
// The server opens a public GitHub issue for it at once in natpoh/diablo2-vr-port with a template title and body only - "Launcher
// report N", the versions, the headset, the view, "waiting for support" - never the player's words, and keeps the player's text and
// files private. After triage support renames it ("Launcher report N: No fog") and writes the gist; spam is deleted. The issue's
// address comes back in GET /api/crash/reply?id=&token= as "issue" (with status "none" too); Settings keeps it in
// <logs>\crash_reports\issues.jsonl and shows "Follow it on GitHub". Pure - strings in and out (tools/crash_watch_test.cpp).
#pragma once

#include "crash_reply.h"

#include <cctype>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace problemreport {

// The only addresses Settings opens: an issue of our public repository, "https://github.com/natpoh/diablo2-vr-port/issues/<number>".
constexpr const char* kIssuePrefix = "https://github.com/natpoh/diablo2-vr-port/issues/";

inline bool GoodIssue(const std::string& url) {
    const std::string p = kIssuePrefix;
    if (url.size() <= p.size() || url.size() > p.size() + 7 || url.compare(0, p.size(), p) != 0 || url[p.size()] == '0') return false;
    for (size_t i = p.size(); i < url.size(); ++i)
        if (url[i] < '0' || url[i] > '9') return false;
    return true;
}

// The issue in the server's answer to GET /api/crash/reply ("issue":"https://github.com/..."); "" none or not one of ours.
inline std::string ParseIssue(const std::string& json) {
    std::string u;
    if (!crashreply::detail::Str(json, "issue", &u) || !GoodIssue(u)) return std::string();
    return u;
}

// issues.jsonl: {"id":N,"issue":"https://..."} a line; read back as report number -> address (a bad line left out, the last one wins).
inline std::string IssueLine(int id, const std::string& url) {
    char b[24];
    std::snprintf(b, sizeof b, "%d", id);
    return std::string("{\"id\":") + b + ",\"issue\":\"" + crashreply::detail::Esc(url) + "\"}\r\n";
}
inline std::map<int, std::string> ParseIssues(const std::string& text) {
    std::map<int, std::string> m;
    for (const std::string& l : crashreply::detail::Lines(text)) {
        int id = 0;
        std::string u;
        if (crashreply::detail::Int(l, "id", &id) && id > 0 && crashreply::detail::Str(l, "issue", &u) && GoodIssue(u)) m[id] = u;
    }
    return m;
}

// The newest report (highest number) with an issue; 0 none.
inline int NewestIssue(const std::map<int, std::string>& m) { return m.empty() ? 0 : m.rbegin()->first; }

// The reports in sent.jsonl that `before` (sent.jsonl's text before the window opened) did not have: what the player just sent.
inline std::vector<crashreply::Sent> NewlySent(const std::string& before, const std::string& after) {
    std::map<int, bool> had;
    for (const crashreply::Sent& s : crashreply::ParseSent(before)) had[s.id] = true;
    std::vector<crashreply::Sent> v;
    for (const crashreply::Sent& s : crashreply::ParseSent(after))
        if (!had.count(s.id)) v.push_back(s);
    return v;
}

// The player's description as sent: trimmed, at most 4000 characters (bytes of UTF-8 here, cut on a character's start), "" when it says
// nothing (fewer than 5 letters or digits).
inline std::string CleanText(const std::string& in) {
    size_t a = 0, b = in.size();
    while (a < b && (unsigned char)in[a] <= ' ') ++a;
    while (b > a && (unsigned char)in[b - 1] <= ' ') --b;
    std::string s = in.substr(a, b - a);
    if (s.size() > 4000) {
        size_t cut = 4000;
        while (cut > 0 && ((unsigned char)s[cut] & 0xC0) == 0x80) --cut;
        s.resize(cut);
    }
    int meaningful = 0;
    for (unsigned char c : s)
        if (std::isalnum(c) || c >= 0xC0) ++meaningful;   // (a UTF-8 lead byte: a letter of another script counts once)
    return meaningful >= 5 ? s : std::string();
}

// --- problem.txt ---------------------------------------------------------------------------------------------------------------------
// The view and the headset the player picks in the window. The server posts these exact values in the public issue's template and
// nothing else (bodywalk_site services/github_issues.py VIEWS / HEADSETS - keep both lists the same); "not sure" (index -1) writes no line.
inline constexpr const char* kViews[] = {"F1", "F2", "F3", "F4", "flat"};
inline constexpr const char* kHeadsets[] = {"Meta Quest 3", "Meta Quest 3S", "Meta Quest 2",     "Meta Quest Pro",   "PlayStation VR2",
                                            "Valve Index",  "HTC Vive / Vive Pro", "Pico 4", "Pimax", "Bigscreen Beyond",
                                            "Windows Mixed Reality", "Other"};
constexpr int kViewCount = (int)(sizeof kViews / sizeof kViews[0]);
constexpr int kHeadsetCount = (int)(sizeof kHeadsets / sizeof kHeadsets[0]);

// problem.txt: a header of "key: value" lines (the server reads only these, up to the first empty line), an empty line, then the player's
// words. `version` e.g. "0.211.0", `when` the player's clock "2026-11-10 12:00", view / headset an index into kViews / kHeadsets (-1 none),
// game: 1 running, 0 not, -1 not known. The words go in as written (CleanText), after the empty line, so a "View: F4" in them is never read.
inline std::string ProblemText(const std::string& version, const std::string& when, int view, int headset, int game, const std::string& words) {
    std::string s = "Problem described by the player in D2R VR Settings " + version + ", " + when + " (the player's clock)\r\n";
    if (view >= 0 && view < kViewCount) s += std::string("View: ") + kViews[view] + "\r\n";
    if (headset >= 0 && headset < kHeadsetCount) s += std::string("Headset: ") + kHeadsets[headset] + "\r\n";
    if (game >= 0) s += std::string("Game running: ") + (game ? "yes" : "no") + "\r\n";
    std::string w = CleanText(words);
    for (size_t i = 0; (i = w.find('\n', i)) != std::string::npos; ++i)   // the player's line ends as CRLF (Notepad, the server's splitlines)
        if (i == 0 || w[i - 1] != '\r') w.insert(i++, 1, '\r');
    return s + "\r\n" + w + "\r\n";
}

// --- settings_changed.txt: the player's d2r_vr.ini against the shipped d2r_vr.default.ini --------------------------------------------
// Windows' ini rules: sections and keys case-insensitive, ';' / '#' lines are comments, a key before any section is in section "".
struct IniKey {
    std::string section, key, value;
};

inline std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
inline std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
    return s.substr(a, b - a);
}

inline std::vector<IniKey> ParseIni(const std::string& text) {
    std::vector<IniKey> v;
    std::string section;
    size_t start = (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) ? 3 : 0;
    for (const std::string& raw : crashreply::detail::Lines(text.substr(start))) {
        const std::string l = Trim(raw);
        if (l.empty() || l[0] == ';' || l[0] == '#') continue;
        if (l[0] == '[') {
            const size_t e = l.find(']');
            if (e != std::string::npos) section = Trim(l.substr(1, e - 1));
            continue;
        }
        const size_t eq = l.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        v.push_back({section, Trim(l.substr(0, eq)), Trim(l.substr(eq + 1))});
    }
    return v;
}

// One line per key whose value differs from the shipped one, or that the shipped file does not have:
// "[fog] game_fog_before_fx = 0   (shipped: 1)" / "[debug] x = 1   (not in the shipped file)". The first value of a key counts (as Windows reads).
inline std::string IniDiffText(const std::string& mine, const std::string& shipped) {
    std::map<std::string, std::string> def;
    for (const IniKey& k : ParseIni(shipped)) def.emplace(Lower(k.section) + "\n" + Lower(k.key), k.value);
    std::map<std::string, bool> seen;
    std::string out;
    int n = 0;
    for (const IniKey& k : ParseIni(mine)) {
        const std::string id = Lower(k.section) + "\n" + Lower(k.key);
        if (!seen.emplace(id, true).second) continue;
        const auto d = def.find(id);
        if (d != def.end() && d->second == k.value) continue;
        out += "[" + k.section + "] " + k.key + " = " + k.value + (d == def.end() ? "   (not in the shipped file)" : "   (shipped: " + d->second + ")") + "\r\n";
        ++n;
    }
    char head[96];
    std::snprintf(head, sizeof head, "Settings changed from the shipped d2r_vr.ini: %d\r\n\r\n", n);
    return head + out;
}

}  // namespace problemreport
