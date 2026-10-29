// lang.h - D2R VR Settings in the player's language: the files lang\<code>.txt (en.txt, de.txt, pt-BR.txt...) and how they are read.
// Pure: no file, no window; the unit test is cleanroom/tests/settings_lang_test.cpp, the program is tools/settings/settings.cpp (TR,
// TranslateItems, the Home page's "Language").
//
// Why: a player (2026-10-26) - "add a language selection option to the mod, or at least move the text into a separate file so that users
// can translate it themselves. There are so many settings that it's very easy to get confused." English stays built into the program: a
// file that is missing, a key that is missing or empty, a line that cannot be read - each falls back to the English, nothing breaks.
//
// A file, as Notepad writes it: UTF-8 with or without a BOM (UTF-16 with its BOM - Notepad's "Unicode" - reads too). One "key = text" a
// line; a line that starts with # or ; is a comment, a blank line is nothing, a line without "=" is skipped. The text is everything after
// the first "=", spaces trimmed at both ends. \n in the text is a new line and \\ a backslash; any other backslash is itself (a path like
// mods\D2R-VR\reshade stays as it is). The same key twice: the later line wins. {name} in a text is a value the program puts in (a
// number, a file name): keep it as it is in a translation, anywhere in the sentence.
// lang/en.txt (made by tools/settings/lang_template.py from the program itself) lists every key with its English.
#pragma once

#include <cstdint>
#include <initializer_list>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace lang {

using Table = std::map<std::wstring, std::wstring>;

// UTF-8 to UTF-16; a byte that is not UTF-8 becomes U+FFFD (the line is kept).
inline std::wstring FromUtf8(const std::string& s) {
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = (unsigned char)s[i];
        uint32_t cp = 0;
        int more = 0;
        if (c < 0x80) { cp = c; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; more = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; more = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; more = 3; }
        else { out += L'�'; ++i; continue; }
        bool ok = i + (size_t)more < s.size();   // (not cut short at the end)
        for (int k = 1; ok && k <= more; ++k) {
            const unsigned char d = (unsigned char)s[i + k];
            if ((d & 0xC0) != 0x80) ok = false;
            else cp = (cp << 6) | (d & 0x3F);
        }
        if (!ok || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) { out += L'�'; ++i; continue; }
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out += (wchar_t)(0xD800 + (cp >> 10));
            out += (wchar_t)(0xDC00 + (cp & 0x3FF));
        } else out += (wchar_t)cp;
        i += 1 + (size_t)more;
    }
    return out;
}

inline std::string ToUtf8(const std::wstring& w) {
    std::string out;
    out.reserve(w.size());
    for (size_t i = 0; i < w.size(); ++i) {
        uint32_t cp = (uint16_t)w[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < w.size() && (uint16_t)w[i + 1] >= 0xDC00 && (uint16_t)w[i + 1] <= 0xDFFF)
            cp = 0x10000 + ((cp - 0xD800) << 10) + ((uint16_t)w[++i] - 0xDC00);
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
        else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
    }
    return out;
}

// A file's bytes as text: UTF-16 LE / BE by their BOM, else UTF-8 (its BOM dropped).
inline std::wstring Decode(const std::string& bytes) {
    if (bytes.size() >= 2 && (unsigned char)bytes[0] == 0xFF && (unsigned char)bytes[1] == 0xFE) {
        std::wstring w;
        for (size_t i = 2; i + 1 < bytes.size(); i += 2) w += (wchar_t)((unsigned char)bytes[i] | ((unsigned char)bytes[i + 1] << 8));
        return w;
    }
    if (bytes.size() >= 2 && (unsigned char)bytes[0] == 0xFE && (unsigned char)bytes[1] == 0xFF) {
        std::wstring w;
        for (size_t i = 2; i + 1 < bytes.size(); i += 2) w += (wchar_t)(((unsigned char)bytes[i] << 8) | (unsigned char)bytes[i + 1]);
        return w;
    }
    if (bytes.size() >= 3 && (unsigned char)bytes[0] == 0xEF && (unsigned char)bytes[1] == 0xBB && (unsigned char)bytes[2] == 0xBF)
        return FromUtf8(bytes.substr(3));
    return FromUtf8(bytes);
}

inline bool Space(wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == L'\uFEFF' || c == L'\u00A0'; }

inline std::wstring Trim(const std::wstring& s) {
    size_t b = 0, e = s.size();
    while (b < e && Space(s[b])) ++b;
    while (e > b && Space(s[e - 1])) --e;
    return s.substr(b, e - b);
}

// \n a new line, \\ a backslash; any other backslash stays.
inline std::wstring Unescape(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\\' && i + 1 < s.size() && (s[i + 1] == L'n' || s[i + 1] == L'\\')) {
            out += s[i + 1] == L'n' ? L'\n' : L'\\';
            ++i;
        } else out += s[i];
    }
    return out;
}
// The other way, for the file: a new line as \n, a backslash as \\ only where it would otherwise be read as one of those two.
inline std::wstring Escape(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\n') out += L"\\n";
        else if (s[i] == L'\r') continue;
        else if (s[i] == L'\\' && i + 1 < s.size() && (s[i + 1] == L'n' || s[i + 1] == L'\\')) out += L"\\\\";
        else if (s[i] == L'\\' && i + 1 == s.size()) out += L"\\";
        else out += s[i];
    }
    return out;
}

// key -> text of one file (see the top of this file).
inline Table Parse(const std::wstring& text) {
    Table t;
    size_t at = 0;
    while (at <= text.size()) {
        size_t end = text.find(L'\n', at);
        if (end == std::wstring::npos) end = text.size();
        const std::wstring line = Trim(text.substr(at, end - at));
        at = end + 1;
        if (line.empty() || line[0] == L'#' || line[0] == L';') continue;
        const size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        const std::wstring key = Trim(line.substr(0, eq));
        if (key.empty()) continue;
        t[key] = Unescape(Trim(line.substr(eq + 1)));
    }
    return t;
}
inline Table ParseBytes(const std::string& bytes) { return Parse(Decode(bytes)); }

// {name} in `text` replaced by its value; a {name} with no value, and any other brace, stays as it is.
inline std::wstring Fill(const std::wstring& text, std::initializer_list<std::pair<const wchar_t*, std::wstring>> values) {
    std::wstring out;
    out.reserve(text.size() + 16);
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'{') {
            const size_t close = text.find(L'}', i + 1);
            if (close != std::wstring::npos) {
                const std::wstring name = text.substr(i + 1, close - i - 1);
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

// The program's own lookup for code that is not settings.cpp (tools/settings/*_assets.cpp, shared/data_mods.h): settings.cpp sets it to
// its TR at the start. Unset - a test, the installer's --prepare-assets before a language is read - it is the English.
inline const wchar_t* (*g_tr)(const wchar_t* key, const wchar_t* en) = nullptr;
// key's text (or the English) with its {name}s filled in. tools/lang_sync.py finds these calls as it finds settings.cpp's TR.
inline std::wstring TX(const wchar_t* key, const wchar_t* en, std::initializer_list<std::pair<const wchar_t*, std::wstring>> values = {}) {
    return Fill(g_tr ? g_tr(key, en) : en, values);
}

// The {name}s of a text, in order (a translation keeps the same set: tools/settings/lang_template.py --verify).
inline std::vector<std::wstring> Placeholders(const std::wstring& text) {
    std::vector<std::wstring> out;
    for (size_t i = text.find(L'{'); i != std::wstring::npos; i = text.find(L'{', i + 1)) {
        const size_t close = text.find(L'}', i + 1);
        if (close == std::wstring::npos) break;
        const std::wstring name = text.substr(i + 1, close - i - 1);
        bool word = !name.empty();
        for (wchar_t c : name) word = word && ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'_');
        if (word) out.push_back(name);
    }
    return out;
}

// A readable key part from an English title: "How you play" -> "how_you_play" (a-z, 0-9, one _ between words; at most `most` letters).
inline std::wstring Slug(const std::wstring& title, size_t most = 60) {
    std::wstring out;
    for (wchar_t c : title) {
        if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');
        if ((c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9')) out += c;
        else if (!out.empty() && out.back() != L'_') out += L'_';
    }
    if (out.size() > most) out.resize(most);
    while (!out.empty() && out.back() == L'_') out.pop_back();
    return out;
}

inline std::wstring Lower(std::wstring s) {
    for (wchar_t& c : s) if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');
    return s;
}

// Which of the files there (their codes: "de", "pt-BR", "zh-CN") fits a language Windows names ("de-AT", "pt-BR", "zh-Hans-CN"):
// the same code, else the language alone ("de"), else the first file of that language ("pt-BR" for "pt-PT"). "" = none (English).
inline std::wstring Pick(const std::wstring& wanted, const std::vector<std::wstring>& available) {
    if (wanted.empty()) return L"";
    const std::wstring w = Lower(wanted);
    for (const std::wstring& a : available) if (Lower(a) == w) return a;
    const std::wstring primary = w.substr(0, w.find(L'-'));
    for (const std::wstring& a : available) if (Lower(a) == primary) return a;
    for (const std::wstring& a : available) if (Lower(a).rfind(primary + L"-", 0) == 0) return a;
    return L"";
}

}  // namespace lang
