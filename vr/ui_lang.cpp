// ui_lang.cpp - [hud] headset_language (vr/ui_lang.h): the language file read, its "vr." keys handed to vr/ui_text.h, the font picked.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cwchar>
#include <string>
#include <vector>

#include "mod_layout.h"
#include "ui_lang.h"
#include "ui_text.h"

namespace uilang {
namespace {

std::wstring DirOf(const std::wstring& path) {
    const size_t cut = path.find_last_of(L"\\/");
    return cut == std::wstring::npos ? std::wstring() : path.substr(0, cut + 1);
}
bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

bool ReadBytes(const std::wstring& path, std::string* out) {
    const HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(h, &size) && size.QuadPart < (16 << 20);
    if (ok) {
        out->resize((size_t)size.QuadPart);
        DWORD got = 0;
        ok = size.QuadPart == 0 || (ReadFile(h, &(*out)[0], (DWORD)size.QuadPart, &got, nullptr) && got == (DWORD)size.QuadPart);
    }
    CloseHandle(h);
    return ok;
}

// Where the language files are, as D2R VR Settings finds them (its LangDir): the mod's lang\ (the plugin in mods\D2R-VR\d2rloader\plugins),
// else lang\ beside the game, else the game's mods\D2R-VR\lang (the plugin in the global d2rloader\plugins). "" = none.
std::wstring LangDir(const std::wstring& pluginDir) {
    const std::wstring mod = modlayout::ModRootOfPluginDir(pluginDir);
    if (!mod.empty() && Exists(mod + L"lang\\")) return mod + L"lang\\";
    const std::wstring game = modlayout::GameOfPluginDir(pluginDir);
    if (game.empty()) return Exists(pluginDir + L"lang\\") ? pluginDir + L"lang\\" : std::wstring();
    if (Exists(modlayout::WithSlash(game) + L"lang\\")) return modlayout::WithSlash(game) + L"lang\\";
    return Exists(modlayout::ModRoot(game) + L"lang\\") ? modlayout::ModRoot(game) + L"lang\\" : std::wstring();
}

// The codes of the language files there (every <code>.txt but en.txt with a language.name).
std::vector<std::wstring> Codes(const std::wstring& dir) {
    std::vector<std::wstring> out;
    WIN32_FIND_DATAW fd;
    const HANDLE h = FindFirstFileW((dir + L"*.txt").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring code = fd.cFileName;
        code.resize(code.size() - 4);
        if (code.empty() || _wcsicmp(code.c_str(), L"en") == 0) continue;
        std::string bytes;
        if (!ReadBytes(dir + fd.cFileName, &bytes)) continue;
        const lang::Table t = lang::ParseBytes(bytes);
        const auto name = t.find(L"language.name");
        if (name != t.end() && !name->second.empty()) out.push_back(code);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

// [settings] language of d2r_vr_settings.ini: a code, "en", or "" (auto: Windows' language when there is a file for it).
std::wstring Chosen(const std::wstring& uiIni, const std::wstring& dir) {
    wchar_t b[64] = L"";
    GetPrivateProfileStringW(L"settings", L"language", L"", b, 64, uiIni.c_str());
    std::wstring code = lang::Trim(b);
    if (!code.empty() && _wcsicmp(code.c_str(), L"auto") != 0) return code;
    wchar_t name[LOCALE_NAME_MAX_LENGTH] = L"";
    if (!LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT), name, LOCALE_NAME_MAX_LENGTH, 0)) return L"";
    return lang::Pick(name, Codes(dir));
}

// Does the font GDI gives for `face` have every letter of `text`? (A face Windows lacks is another font, which then answers: the
// letters are drawn with it all the same. Its name is not compared - a Chinese Windows names Microsoft YaHei in Chinese.)
bool Covers(const wchar_t* face, const std::wstring& text) {
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return false;
    HFONT f = CreateFontW(-20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          ANTIALIASED_QUALITY, DEFAULT_PITCH, face);
    bool ok = f != nullptr;
    if (ok) {
        const HGDIOBJ old = SelectObject(dc, f);
        std::vector<WORD> idx(text.size());
        ok = text.empty() || GetGlyphIndicesW(dc, text.c_str(), (int)text.size(), idx.data(), GGI_MARK_NONEXISTING_GLYPHS) != GDI_ERROR;
        for (size_t i = 0; ok && i < idx.size(); ++i) ok = idx[i] != 0xFFFF;
        SelectObject(dc, old);
        DeleteObject(f);
    }
    DeleteDC(dc);
    return ok;
}

// The face for a language whose letters Georgia or Segoe UI lack, nullptr when they have them all.
const wchar_t* WideFace(const uitext::Table& t) {
    std::wstring letters;
    {
        std::wstring all;
        for (const auto& kv : t) all += lang::FromUtf8(kv.second);
        std::vector<bool> have(0x10000, false);
        for (wchar_t c : all)
            if (c > 0x7F && !have[(uint16_t)c] && !(c >= 0xD800 && c <= 0xDFFF) && !lang::Space(c)) {
                have[(uint16_t)c] = true;
                letters += c;
            }
    }
    if (Covers(L"Georgia", letters) && Covers(L"Segoe UI", letters)) return nullptr;
    for (const wchar_t* face : {L"Microsoft YaHei", L"Malgun Gothic", L"Yu Gothic UI", L"Nirmala UI"})
        if (Covers(face, letters)) return face;
    return L"Segoe UI";   // (GDI links it to the system's fonts for the letters it lacks)
}

std::wstring g_sig = L"\x1";   // what the table in use was made from: never equal to a first call's

}  // namespace

std::string Load(const wchar_t* ini) {
    if (!ini || !*ini) return {};
    const std::wstring dir = DirOf(ini);
    const bool on = GetPrivateProfileIntW(L"hud", L"headset_language", 0, ini) != 0;
    std::wstring langDir, code, file;
    if (on) {
        langDir = LangDir(dir);
        if (!langDir.empty()) code = Chosen(dir + L"d2r_vr_settings.ini", langDir);
        if (!code.empty() && _wcsicmp(code.c_str(), L"en") != 0) file = langDir + code + L".txt";
    }
    std::wstring sig = on ? L"1|" + file : L"0";
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (!file.empty() && GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &fa))
        sig += L"|" + std::to_wstring(fa.ftLastWriteTime.dwHighDateTime) + L"." + std::to_wstring(fa.ftLastWriteTime.dwLowDateTime);
    if (sig == g_sig) return {};
    g_sig = sig;

    std::string bytes;
    if (file.empty() || !ReadBytes(file, &bytes)) {
        uitext::Set(nullptr, nullptr);
        if (!on) return "vrcam: headset language - English ([hud] headset_language=0)";
        if (langDir.empty()) return "vrcam: headset language - English (no lang folder found beside the plugin or the game)";
        if (file.empty()) return "vrcam: headset language - English (D2R VR Settings is in English)";
        return "vrcam: headset language - English (" + lang::ToUtf8(file) + " could not be read)";
    }
    auto* table = new uitext::Table();   // (never freed: a text a painter holds stays valid - vr/ui_text.h Set)
    for (const auto& kv : lang::ParseBytes(bytes))
        if (kv.first.rfind(L"vr.", 0) == 0 && !kv.second.empty()) (*table)[lang::ToUtf8(kv.first)] = lang::ToUtf8(kv.second);
    const wchar_t* face = WideFace(*table);
    uitext::Set(table, face);
    return "vrcam: headset language - " + lang::ToUtf8(code) + " (" + std::to_string(table->size()) + " of its texts from " + lang::ToUtf8(file) +
           "; the rest English" + (face ? ", font " + lang::ToUtf8(face) : std::string()) + ")";
}

}  // namespace uilang
