// capture_rule.h - the capture key's rules (vr/capture.h): its spelling in [debug] capture_key, the folder's name, which old folders go, the
// guard against a held or repeated key. Pure: no Windows calls, no file system (tools/capture_test.cpp).
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>

namespace capturerule {

// A key with its modifiers. vk 0 = none (the capture off).
struct Key {
    unsigned vk = 0;
    bool ctrl = false, alt = false, shift = false;
};
constexpr unsigned kVkF1 = 0x70;
inline Key Default() { return {kVkF1 + 9, true, true, false}; }   // Ctrl+Alt+F10

// "ctrl+alt+f10", "Ctrl + Shift + F7", "alt+p", "ctrl+0x7A": modifiers ctrl / alt / shift (any order, any case, '+' between),
// then F1 - F24, a letter or digit, or a virtual-key code in hex (0x..). Empty: the default. "none" / "0" / "off": no key.
// Anything else: false (the caller keeps the default and says so).
inline bool Parse(const std::wstring& spec, Key* out) {
    std::wstring s;
    for (wchar_t c : spec)
        if (c != L' ' && c != L'\t') s += (wchar_t)towlower(c);
    if (s.empty()) { *out = Default(); return true; }
    if (s == L"none" || s == L"0" || s == L"off") { *out = Key{}; return true; }
    Key k;
    size_t at = 0;
    for (;;) {
        const size_t plus = s.find(L'+', at);
        const std::wstring part = s.substr(at, plus == std::wstring::npos ? std::wstring::npos : plus - at);
        if (plus == std::wstring::npos) {   // the key itself
            if (part.size() >= 2 && part[0] == L'f' && iswdigit(part[1])) {
                const int n = std::stoi(part.substr(1));
                if (n < 1 || n > 24 || part.size() > 3) return false;
                k.vk = kVkF1 + (unsigned)(n - 1);
            } else if (part.size() > 2 && part[0] == L'0' && part[1] == L'x') {
                const unsigned long v = wcstoul(part.c_str() + 2, nullptr, 16);
                if (v == 0 || v > 0xFE) return false;
                k.vk = (unsigned)v;
            } else if (part.size() == 1 && (iswalpha(part[0]) || iswdigit(part[0])) && part[0] < 128) {
                k.vk = (unsigned)towupper(part[0]);
            } else {
                return false;
            }
            break;
        }
        if (part == L"ctrl" || part == L"control") k.ctrl = true;
        else if (part == L"alt") k.alt = true;
        else if (part == L"shift") k.shift = true;
        else return false;
        at = plus + 1;
    }
    *out = k;
    return true;
}
// The key in words, for the log.
inline std::string Name(const Key& k) {
    if (!k.vk) return "none";
    std::string s;
    if (k.ctrl) s += "Ctrl+";
    if (k.alt) s += "Alt+";
    if (k.shift) s += "Shift+";
    char b[16];
    if (k.vk >= kVkF1 && k.vk < kVkF1 + 24) snprintf(b, sizeof b, "F%u", k.vk - kVkF1 + 1);
    else if ((k.vk >= 'A' && k.vk <= 'Z') || (k.vk >= '0' && k.vk <= '9')) snprintf(b, sizeof b, "%c", (char)k.vk);
    else snprintf(b, sizeof b, "0x%02X", k.vk);
    return s + b;
}
// The modifiers held are exactly the key's (Ctrl+Alt+F10 is not Ctrl+F10's trace, nor Ctrl+Alt+Shift+F10).
inline bool Matches(const Key& k, bool ctrl, bool alt, bool shift) { return k.vk && k.ctrl == ctrl && k.alt == alt && k.shift == shift; }

// One capture per kMinGapMs at most (a held key, a double press).
constexpr uint64_t kMinGapMs = 3000;
inline bool MayCapture(uint64_t now, uint64_t last) { return last == 0 || now - last >= kMinGapMs; }

// A capture's folder in the logs folder: capture_YYYYMMDD-HHMMSS (local time) - names sort as they came.
inline std::wstring FolderName(int y, int mo, int d, int h, int mi, int s) {
    wchar_t b[40];
    swprintf(b, 40, L"capture_%04d%02d%02d-%02d%02d%02d", y, mo, d, h, mi, s);
    return b;
}
inline bool IsOurs(const std::wstring& name) {
    if (name.size() != 23 || name.compare(0, 8, L"capture_") != 0) return false;
    for (size_t i = 8; i < 23; ++i)
        if (i == 16 ? name[i] != L'-' : !iswdigit(name[i])) return false;
    return true;
}
// The folders kept: the newest kKept of ours; the rest (oldest first) are returned to be deleted. Others' names are never returned.
constexpr size_t kKept = 5;
inline std::vector<std::wstring> ToDelete(std::vector<std::wstring> names) {
    std::vector<std::wstring> ours;
    for (auto& n : names)
        if (IsOurs(n)) ours.push_back(n);
    std::sort(ours.begin(), ours.end());
    if (ours.size() <= kKept) return {};
    ours.resize(ours.size() - kKept);
    return ours;
}

}  // namespace capturerule
