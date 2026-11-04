// shared/crash_reply.h - our reply to whoever sent a crash report (Serg 2026-11-04: "чтобы в клиент приходил ответ от нас").
//
// D2R VR Crash Reporter keeps each sent report's number and reply token in <logs>\crash_reports\sent.jsonl (one JSON line each:
// {"id":N,"token":"...","sent":"...","kind":"crash|hang|gpu|logs","folder":"..."}). D2R VR Settings asks the BodyWalk server
// GET /api/crash/reply?id=&token= for those without an answer yet, at most once an hour and only with [support] crash_reports or
// send_logs on, and keeps the answers in <logs>\crash_reports\replies.jsonl; the newest one not dismissed shows on the Home tab.
// Nothing else is sent: the report's number and its token. Pure - strings in and out (tools/crash_watch_test.cpp).
#pragma once

#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

namespace crashreply {

struct Sent {
    int id = 0;
    std::string token, sent, kind;
};

struct Reply {
    int id = 0;
    std::string text, fixed_in, date, kind;   // kind: "report" (its own answer) or "signature" (one for its whole kind of crash)
};

namespace detail {

// The string value of "key" in one flat JSON object (\" \\ \/ \n \r \t \uXXXX - surrogate pairs too - undone, as UTF-8); false: none.
inline bool Str(const std::string& j, const char* key, std::string* out) {
    const std::string k = std::string("\"") + key + "\"";
    size_t at = j.find(k);
    if (at == std::string::npos) return false;
    at = j.find(':', at + k.size());
    if (at == std::string::npos) return false;
    at = j.find_first_not_of(" \t", at + 1);
    if (at == std::string::npos || j[at] != '"') return false;
    std::string s;
    auto put = [&](uint32_t cp) {
        if (cp < 0x80) s += (char)cp;
        else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
        else { s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 0x3F)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
    };
    auto hex4 = [&](size_t p, uint32_t* v) {
        if (p + 4 > j.size()) return false;
        *v = 0;
        for (size_t i = p; i < p + 4; ++i) {
            const char c = j[i];
            *v = *v * 16 + (uint32_t)(c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 99);
            if (*v > 0xFFFF) return false;
        }
        return true;
    };
    for (size_t i = at + 1; i < j.size(); ++i) {
        const char c = j[i];
        if (c == '"') { *out = s; return true; }
        if (c != '\\') { s += c; continue; }
        if (++i >= j.size()) return false;
        switch (j[i]) {
        case 'n': s += '\n'; break;
        case 'r': s += '\r'; break;
        case 't': s += '\t'; break;
        case 'b': case 'f': break;
        case 'u': {
            uint32_t v = 0, lo = 0;
            if (!hex4(i + 1, &v)) return false;
            i += 4;
            if (v >= 0xD800 && v < 0xDC00 && i + 6 < j.size() && j[i + 1] == '\\' && j[i + 2] == 'u' && hex4(i + 3, &lo) && lo >= 0xDC00 && lo < 0xE000) {
                v = 0x10000 + ((v - 0xD800) << 10) + (lo - 0xDC00);
                i += 6;
            }
            put(v);
            break;
        }
        default: s += j[i]; break;   // \" \\ \/
        }
    }
    return false;
}

inline bool Int(const std::string& j, const char* key, int* out) {
    const std::string k = std::string("\"") + key + "\"";
    size_t at = j.find(k);
    if (at == std::string::npos) return false;
    at = j.find(':', at + k.size());
    if (at == std::string::npos) return false;
    at = j.find_first_not_of(" \t", at + 1);
    if (at == std::string::npos || j[at] < '0' || j[at] > '9') return false;
    long long v = 0;
    while (at < j.size() && j[at] >= '0' && j[at] <= '9' && v < 1000000000) v = v * 10 + (j[at++] - '0');
    *out = (int)v;
    return true;
}

// A string as a JSON string's inside (what Str undoes); control characters dropped but \n.
inline std::string Esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (c == '\n') o += "\\n";
        else if ((unsigned char)c >= 0x20) o += c;
    }
    return o;
}

inline std::vector<std::string> Lines(const std::string& text) {
    std::vector<std::string> v;
    size_t i = 0;
    while (i < text.size()) {
        size_t e = text.find('\n', i);
        if (e == std::string::npos) e = text.size();
        std::string l = text.substr(i, e - i);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        if (!l.empty()) v.push_back(l);
        i = e + 1;
    }
    return v;
}

}  // namespace detail

// sent.jsonl: the reports the player sent that we can answer (a line without a good id and token is left out).
inline std::vector<Sent> ParseSent(const std::string& text) {
    std::vector<Sent> v;
    for (const std::string& l : detail::Lines(text)) {
        Sent s;
        if (!detail::Int(l, "id", &s.id) || s.id <= 0 || !detail::Str(l, "token", &s.token) || s.token.size() < 16) continue;
        detail::Str(l, "sent", &s.sent);
        detail::Str(l, "kind", &s.kind);
        v.push_back(s);
    }
    return v;
}

// The server's answer to GET /api/crash/reply: 1 a reply (*r filled), 0 none yet, -1 not an answer of ours.
inline int ParseAnswer(const std::string& json, Reply* r) {
    std::string status;
    if (!detail::Str(json, "status", &status)) return -1;
    if (status == "none") return 0;
    if (status != "ok") return -1;
    *r = Reply{};   // (nothing of an earlier answer left in it)
    detail::Str(json, "text", &r->text);
    detail::Str(json, "fixed_in", &r->fixed_in);
    if (r->text.empty() && r->fixed_in.empty()) return -1;   // a reply is a text, a "fixed in" version, or both
    detail::Int(json, "id", &r->id);
    detail::Str(json, "date", &r->date);
    detail::Str(json, "kind", &r->kind);
    if (r->text.size() > 2000) r->text.resize(2000);
    if (r->fixed_in.size() > 32) r->fixed_in.resize(32);
    return 1;
}

// replies.jsonl: a reply as one line, and the lines back.
inline std::string ReplyLine(const Reply& r) {
    char id[16];
    std::snprintf(id, sizeof id, "%d", r.id);
    return std::string("{\"id\":") + id + ",\"date\":\"" + detail::Esc(r.date) + "\",\"fixed_in\":\"" + detail::Esc(r.fixed_in) +
           "\",\"kind\":\"" + detail::Esc(r.kind) + "\",\"text\":\"" + detail::Esc(r.text) + "\"}\r\n";
}
inline std::vector<Reply> ParseReplies(const std::string& text) {
    std::vector<Reply> v;
    for (const std::string& l : detail::Lines(text)) {
        Reply r;
        if (!detail::Int(l, "id", &r.id) || r.id <= 0 || !detail::Str(l, "text", &r.text)) continue;
        detail::Str(l, "date", &r.date);
        detail::Str(l, "fixed_in", &r.fixed_in);
        detail::Str(l, "kind", &r.kind);
        v.push_back(r);
    }
    return v;
}

// The sent reports still to ask about: not answered yet (no line in replies.jsonl). Each asked once per poll.
inline std::vector<Sent> ToAsk(const std::vector<Sent>& sent, const std::vector<Reply>& got) {
    std::set<int> have;
    for (const Reply& r : got) have.insert(r.id);
    std::vector<Sent> v;
    std::set<int> seen;
    for (const Sent& s : sent)
        if (!have.count(s.id) && seen.insert(s.id).second) v.push_back(s);
    return v;
}

// d2r_vr_settings.ini [support] reply_dismissed: "12,45" - the replies the player closed.
inline std::set<int> ParseDismissed(const std::string& s) {
    std::set<int> v;
    int n = 0;
    bool any = false;
    for (char c : s + ",") {
        if (c >= '0' && c <= '9') { n = n * 10 + (c - '0'); any = true; }
        else { if (any && n > 0) v.insert(n); n = 0; any = false; }
    }
    return v;
}
inline std::string JoinDismissed(const std::set<int>& v) {
    std::string s;
    for (int n : v) s += (s.empty() ? "" : ",") + std::to_string(n);
    return s;
}

// "0.201.0" < "0.202.0" < "0.202.1"; numbers compared one by one, a missing one is 0, anything after the digits ("0.202.0-develop") ignored.
inline bool VersionLess(const std::string& a, const std::string& b) {
    size_t i = 0, j = 0;
    for (int part = 0; part < 6; ++part) {
        long x = 0, y = 0;
        while (i < a.size() && a[i] >= '0' && a[i] <= '9') x = x * 10 + (a[i++] - '0');
        while (j < b.size() && b[j] >= '0' && b[j] <= '9') y = y * 10 + (b[j++] - '0');
        if (x != y) return x < y;
        const bool moreA = i < a.size() && a[i] == '.', moreB = j < b.size() && b[j] == '.';
        if (!moreA && !moreB) return false;
        if (moreA) ++i; else i = a.size();
        if (moreB) ++j; else j = b.size();
    }
    return false;
}

// How a reply shows for the version running here: Update - "fixed in X - please update" with the Update button (X newer than mine);
// Text - its words (and "fixed in X, the version you have" when X is mine or older); Nothing - only a version that mine already has.
enum class Show { Nothing, Text, Update };
inline Show HowToShow(const Reply& r, const std::string& mine) {
    if (!r.fixed_in.empty() && VersionLess(mine, r.fixed_in)) return Show::Update;
    return r.text.empty() ? Show::Nothing : Show::Text;
}

// The reply to show: the newest (highest report number) not dismissed that has something to say here; nullptr: none.
inline const Reply* ToShow(const std::vector<Reply>& got, const std::set<int>& dismissed, const std::string& mine) {
    const Reply* best = nullptr;
    for (const Reply& r : got)
        if (!dismissed.count(r.id) && HowToShow(r, mine) != Show::Nothing && (!best || r.id > best->id)) best = &r;
    return best;
}

// Is any reply unread - one that would show, the player has not had on screen (`read`) nor dismissed? D2R VR Settings keeps
// d2r_vr.ini [support] unread_replies at this (1 / 0) for the D2R VR Launcher, which blinks its Settings button while it is 1.
inline bool AnyUnread(const std::vector<Reply>& got, const std::set<int>& read, const std::set<int>& dismissed, const std::string& mine) {
    for (const Reply& r : got)
        if (!read.count(r.id) && !dismissed.count(r.id) && HowToShow(r, mine) != Show::Nothing) return true;
    return false;
}

}  // namespace crashreply
