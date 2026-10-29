#pragma once
// ui_text.h - the words the plugin draws in the headset (the skill panel, the settings in the headset, the first-start guide, the calibration
// prompt, the map's buttons) in the player's language: the same lang\<code>.txt files as D2R VR Settings (tools/settings/lang.h), their
// "vr." keys. English is built in, at every call: no table (the default, [hud] headset_language=0), a key the file lacks or leaves empty -
// the English given with the call, so a branch that adds a text shows it in English until the six-language batch before a release.
//
// Pure (no Windows, no file): the table is handed in (vr/ui_lang.cpp reads the file in the plugin; the tests make one), the painters ask
// Face for the font. tools/lang_sync.py finds every uitext::Tr / TrF / TrW call with a literal key and English in vr/ and lists it in
// lang\en.txt; the tables' texts (keys made from the table, vr/ui_text_tables.h) come from D2R_VR_Settings.exe --lang-template.
// Tested: cleanroom/tests/ui_text_test.cpp.

#include <atomic>
#include <cstdio>
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <utility>

#include "../tools/settings/lang.h"   // lang::FromUtf8 (TrW)

namespace uitext {

// key -> text, both UTF-8 (only the "vr." keys of a language file).
using Table = std::map<std::string, std::string, std::less<>>;

// The table in use, nullptr = English. A table handed in is never freed (a text a painter holds stays valid; a language is a few
// kilobytes, changed by hand): Set leaks the one before.
inline std::atomic<const Table*> g_table{nullptr};
// The face that replaces Georgia / Segoe UI for a language they lack letters of (zh-Hans: Microsoft YaHei), nullptr = none.
inline std::atomic<const wchar_t*> g_wideFace{nullptr};

inline void Set(const Table* t, const wchar_t* wideFace) {
    g_wideFace.store(wideFace, std::memory_order_release);
    g_table.store(t, std::memory_order_release);
}

// key's text in the player's language, else `en` (the pointer stays valid as long as the process).
inline const char* Tr(const char* key, const char* en) {
    const Table* t = g_table.load(std::memory_order_acquire);
    if (!t) return en;
    const auto it = t->find(std::string_view(key));
    return it != t->end() && !it->second.empty() ? it->second.c_str() : en;
}
// A key made at run time (the tables' texts: vr/ui_text_tables.h).
inline const char* Tr(const std::string& key, const char* en) { return Tr(key.c_str(), en); }

// {name} in `text` replaced by its value; a {name} with no value, and any other brace, stays as it is (lang::Fill, in UTF-8).
inline std::string Fill(const std::string& text, std::initializer_list<std::pair<const char*, std::string>> values) {
    std::string out;
    out.reserve(text.size() + 16);
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '{') {
            const size_t close = text.find('}', i + 1);
            if (close != std::string::npos) {
                const std::string_view name(text.data() + i + 1, close - i - 1);
                bool done = false;
                for (const auto& v : values)
                    if (name == v.first) { out += v.second; done = true; break; }
                if (done) { i = close; continue; }
            }
        }
        out += text[i];
    }
    return out;
}
// key's text with its {name}s filled in (a number, a skill's name: a translation keeps them, anywhere in its sentence).
inline std::string TrF(const char* key, const char* en, std::initializer_list<std::pair<const char*, std::string>> values) {
    return Fill(Tr(key, en), values);
}
// The same for GDI's wide calls.
inline std::wstring TrW(const char* key, const char* en) { return lang::FromUtf8(Tr(key, en)); }

// A number as printf writes it ("%.2f" -> "1.62"): the values TrF puts in.
inline std::string Num(const char* fmt, double v) {
    char b[48];
    std::snprintf(b, sizeof b, fmt, v);
    return b;
}

// The font a painter asks GDI for: its own (Georgia, Segoe UI), or the one the language needs.
inline const wchar_t* Face(const wchar_t* own) {
    const wchar_t* wide = g_wideFace.load(std::memory_order_acquire);
    return wide ? wide : own;
}

// A key part from an English word or an ini text: "Trigger + grip" -> "trigger_grip", "cmd:0x25, action:map" -> "cmd_0x25_action_map".
inline std::string Slug(const char* text) {
    std::string out;
    for (const char* p = text; *p; ++p) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out += c;
        else if (!out.empty() && out.back() != '_') out += '_';
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out;
}

// The letters of a UTF-8 text (a line's length where it is counted in letters: the guide's lines).
inline int Letters(std::string_view s) {
    int n = 0;
    for (const char c : s) n += ((unsigned char)c & 0xC0) != 0x80;
    return n;
}

}  // namespace uitext
