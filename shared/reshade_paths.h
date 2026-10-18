#pragma once
// reshade_paths.h - D2R VR's folders in the ReShade a player already has in the game's folder as dxgi.dll.
//
// Why (docs/d2rloader_mod.md, tools/reshade_probe in the scratchpad notes): the game loads a dxgi.dll ReShade itself, at process start, with the
// game's folder as its base - it reads <game>\ReShade.ini there and looks for effects, textures and add-ons in <game>\reshade-shaders and <game>.
// D2R VR's files live in mods\D2R-VR\reshade (D2R_DepthFog.fx, the sky / ceiling / Arcane textures, the FlatVR add-on). Real ReShade 6.8.0 on a
// probe program showed: files only in the mod's folder are NOT found; two entries appended to [GENERAL] EffectSearchPaths and TextureSearchPaths
// of the player's ReShade.ini ARE (relative to the base like the stock ones, or absolute), and ReShade keeps them when it rewrites the file.
// (The add-on needs no ini entry: the plugin loads it itself - vr/vrcam.cpp LoadReShade.)
//
// This header only changes the TEXT of such an ini, append-only and idempotent: the player's own lines, their order, spacing and line endings
// stay byte for byte; only our two list items are added or taken out again. File handling (backup, the base path, refusing while the game runs)
// is in D2R_VR_Settings.exe --reshade-paths (tools/settings/settings.cpp), which Setup and the uninstaller run.

#include <cctype>
#include <string>
#include <vector>

namespace reshadepaths {

inline const char* kEffectKey = "EffectSearchPaths";
inline const char* kTextureKey = "TextureSearchPaths";
inline const char* kEffectDefault = ".\\reshade-shaders\\Shaders\\**";      // what ReShade's own installer writes
inline const char* kTextureDefault = ".\\reshade-shaders\\Textures\\**";

struct Entries {
    std::string effect;    // e.g. .\mods\D2R-VR\reshade\reshade-shaders\Shaders\**
    std::string texture;   // e.g. .\mods\D2R-VR\reshade\reshade-shaders\Textures\**
};

// The two list items for a ReShade folder given as a prefix with its trailing backslash: ".\\mods\\D2R-VR\\reshade\\" (relative to the ReShade base, which
// is the game's folder) or an absolute "G:\\Game\\mods\\D2R-VR\\reshade\\".
inline Entries EntriesFor(const std::string& reshadeFolderPrefix) {
    std::string p = reshadeFolderPrefix;
    if (!p.empty() && p.back() != '\\') p += '\\';
    return {p + "reshade-shaders\\Shaders\\**", p + "reshade-shaders\\Textures\\**"};
}

namespace detail {

inline std::string Norm(const std::string& s) {
    std::string r;
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    for (size_t i = a; i < b; ++i) r += (s[i] == '/') ? '\\' : (char)std::tolower((unsigned char)s[i]);
    return r;
}

inline std::vector<std::string> Lines(const std::string& t) {   // each with its own terminator
    std::vector<std::string> out;
    size_t at = 0;
    while (at < t.size()) {
        const size_t nl = t.find('\n', at);
        if (nl == std::string::npos) { out.push_back(t.substr(at)); break; }
        out.push_back(t.substr(at, nl - at + 1));
        at = nl + 1;
    }
    return out;
}

inline std::string StripEol(const std::string& l) {
    size_t n = l.size();
    while (n && (l[n - 1] == '\n' || l[n - 1] == '\r')) --n;
    return l.substr(0, n);
}

inline bool IsSection(const std::string& line, const char* name) {   // "[name]", any case, spaces around
    const std::string l = Norm(StripEol(line));
    return l.size() >= 2 && l.front() == '[' && l.back() == ']' && Norm(l.substr(1, l.size() - 2)) == Norm(name);
}
inline bool IsAnySection(const std::string& line) {
    const std::string l = Norm(StripEol(line));
    return !l.empty() && l.front() == '[';
}

// Where [GENERAL] runs: [first line after the header, one past its last line). header = -1 when there is none.
struct Span { int header = -1; int begin = 0, end = 0; };
inline Span General(const std::vector<std::string>& lines) {
    Span s;
    for (int i = 0; i < (int)lines.size(); ++i) {
        if (s.header < 0) { if (IsSection(lines[i], "GENERAL")) { s.header = i; s.begin = i + 1; s.end = (int)lines.size(); } }
        else if (IsAnySection(lines[i])) { s.end = i; break; }
    }
    return s;
}

// The line of `key` inside the span (-1 when missing) and the offset of its value in the line.
inline int FindKey(const std::vector<std::string>& lines, const Span& sp, const char* key, size_t* valueAt) {
    for (int i = sp.begin; i < sp.end; ++i) {
        const std::string l = StripEol(lines[i]);
        size_t a = 0;
        while (a < l.size() && (l[a] == ' ' || l[a] == '\t')) ++a;
        const std::string k = key;
        if (l.size() >= a + k.size() && Norm(l.substr(a, k.size())) == Norm(k)) {
            size_t b = a + k.size();
            while (b < l.size() && (l[b] == ' ' || l[b] == '\t')) ++b;
            if (b < l.size() && l[b] == '=') { *valueAt = b + 1; return i; }
        }
    }
    return -1;
}

inline std::vector<std::string> Split(const std::string& v) {   // the items, as written (not trimmed)
    std::vector<std::string> items;
    size_t at = 0;
    for (;;) {
        const size_t c = v.find(',', at);
        items.push_back(v.substr(at, c == std::string::npos ? std::string::npos : c - at));
        if (c == std::string::npos) break;
        at = c + 1;
    }
    return items;
}

inline std::string Eol(const std::string& text, const std::vector<std::string>& lines) {
    for (const std::string& l : lines) {
        if (l.size() >= 2 && l[l.size() - 2] == '\r' && l.back() == '\n') return "\r\n";
        if (!l.empty() && l.back() == '\n') return "\n";
    }
    (void)text;
    return "\r\n";
}

inline bool HasItem(const std::string& value, const std::string& item) {
    for (const std::string& it : Split(value))
        if (!Norm(it).empty() && Norm(it) == Norm(item)) return true;
    return false;
}

// One key: make sure `item` is in its list. changed set when the text changes.
inline void AddOne(std::vector<std::string>& lines, const char* key, const char* stockDefault, const std::string& item, const std::string& eol, bool* changed) {
    Span sp = General(lines);
    if (sp.header < 0) {   // no [GENERAL] at all: one, at the end
        if (!lines.empty() && lines.back().find('\n') == std::string::npos) lines.back() += eol;
        lines.push_back(std::string("[GENERAL]") + eol);
        sp = General(lines);
    }
    size_t at = 0;
    const int k = FindKey(lines, sp, key, &at);
    if (k < 0) {   // no such line: the stock list and ours (an absent key would otherwise make ReShade look nowhere else)
        int last = sp.begin - 1;
        for (int i = sp.begin; i < sp.end; ++i)
            if (!Norm(StripEol(lines[i])).empty()) last = i;
        if (last >= 0 && lines[last].find('\n') == std::string::npos) lines[last] += eol;
        lines.insert(lines.begin() + last + 1, std::string(key) + "=" + stockDefault + "," + item + eol);
        *changed = true;
        return;
    }
    const std::string whole = StripEol(lines[k]);
    const std::string value = whole.substr(at);
    if (HasItem(value, item)) return;
    const std::string tail = lines[k].substr(whole.size());   // the line's own terminator
    const std::string nv = Norm(value).empty() ? item : value + "," + item;
    lines[k] = whole.substr(0, at) + nv + tail;
    *changed = true;
}

inline void RemoveOne(std::vector<std::string>& lines, const char* key, const std::vector<std::string>& forms, bool* changed) {
    const Span sp = General(lines);
    if (sp.header < 0) return;
    size_t at = 0;
    const int k = FindKey(lines, sp, key, &at);
    if (k < 0) return;
    const std::string whole = StripEol(lines[k]);
    const std::vector<std::string> items = Split(whole.substr(at));
    std::vector<std::string> keep;
    bool any = false;
    for (const std::string& it : items) {
        bool ours = false;
        for (const std::string& f : forms)
            if (!Norm(it).empty() && Norm(it) == Norm(f)) ours = true;
        if (ours) any = true; else keep.push_back(it);
    }
    if (!any) return;
    const std::string tail = lines[k].substr(whole.size());
    std::string nv;
    for (size_t i = 0; i < keep.size(); ++i) nv += (i ? "," : "") + keep[i];
    lines[k] = whole.substr(0, at) + nv + tail;   // (a list that was only ours is left empty, as it was: the line itself stays)
    *changed = true;
}

inline std::string Join(const std::vector<std::string>& lines) {
    std::string t;
    for (const std::string& l : lines) t += l;
    return t;
}

}  // namespace detail

// ReShade's own redirect: [INSTALL] BasePath in the ReShade.ini next to the executable moves the whole ReShade base (its ini, effects, add-ons) elsewhere
// (ReShade's source, dll_main.cpp get_base_path: that file is read FIRST, then RESHADE_BASE_PATH_OVERRIDE, then the dll's folder). "" when there is none.
inline std::string InstallBasePath(const std::string& text) {
    const std::vector<std::string> lines = detail::Lines(text);
    bool in = false;
    for (const std::string& raw : lines) {
        if (detail::IsAnySection(raw)) { in = detail::IsSection(raw, "INSTALL"); continue; }
        if (!in) continue;
        const std::string l = detail::StripEol(raw);
        size_t a = 0;
        while (a < l.size() && (l[a] == ' ' || l[a] == '\t')) ++a;
        if (detail::Norm(l.substr(a, 8)) == "basepath") {
            size_t b = a + 8;
            while (b < l.size() && (l[b] == ' ' || l[b] == '\t')) ++b;
            if (b < l.size() && l[b] == '=') {
                std::string v = l.substr(b + 1);
                size_t x = 0, y = v.size();
                while (x < y && std::isspace((unsigned char)v[x])) ++x;
                while (y > x && std::isspace((unsigned char)v[y - 1])) --y;
                return v.substr(x, y - x);
            }
        }
    }
    return std::string();
}

// Are both of our items in the lists?
inline bool Has(const std::string& text, const Entries& e) {
    const std::vector<std::string> lines = detail::Lines(text);
    const detail::Span sp = detail::General(lines);
    if (sp.header < 0) return false;
    size_t at = 0;
    int k = detail::FindKey(lines, sp, kEffectKey, &at);
    if (k < 0 || !detail::HasItem(detail::StripEol(lines[k]).substr(at), e.effect)) return false;
    k = detail::FindKey(lines, sp, kTextureKey, &at);
    return k >= 0 && detail::HasItem(detail::StripEol(lines[k]).substr(at), e.texture);
}

// The text with our two items in the two lists (added to the end of each; the lines are made, with ReShade's stock list first, when missing).
inline std::string Add(const std::string& text, const Entries& e, bool* changed = nullptr) {
    bool c = false;
    std::vector<std::string> lines = detail::Lines(text);
    const std::string eol = detail::Eol(text, lines);
    detail::AddOne(lines, kEffectKey, kEffectDefault, e.effect, eol, &c);
    detail::AddOne(lines, kTextureKey, kTextureDefault, e.texture, eol, &c);
    if (changed) *changed = c;
    return c ? detail::Join(lines) : text;
}

// The text without our items (either form: relative or absolute). Everything else stays.
inline std::string Remove(const std::string& text, const std::vector<Entries>& forms, bool* changed = nullptr) {
    bool c = false;
    std::vector<std::string> lines = detail::Lines(text);
    std::vector<std::string> eff, tex;
    for (const Entries& f : forms) { eff.push_back(f.effect); tex.push_back(f.texture); }
    detail::RemoveOne(lines, kEffectKey, eff, &c);
    detail::RemoveOne(lines, kTextureKey, tex, &c);
    if (changed) *changed = c;
    return c ? detail::Join(lines) : text;
}

}  // namespace reshadepaths
