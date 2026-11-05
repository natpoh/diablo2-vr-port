// headset_report.cpp - D2R VR Headset Report (D2R_VR_Headset_Report.exe): for a player whose headset picture is wrong (doubled, not fusing,
// cross-eyed, stretched, hands at the waist). Run with the headset on, it writes one text report of what the PC's OpenXR runtime gives -
// the runtime, the API layers (implicit ones too), the extensions, the view configurations and picture sizes, then a short session's
// eye poses and fov in VIEW, LOCAL and STAGE over a few seconds, the spaces, the stage bounds, the controllers' grip and aim in each space,
// the GPU the runtime wants and the refresh rate - for #support in our Discord. First it looks at D2R VR's own files (tools/headset_report/
// mod_files.h): the plugin's and the programs' versions, the key ini values, every ReShade setup beside the game and in each mod with every
// D2R_DepthFog.fx its search paths reach (and whether it holds what vrcam asks for), ReShade's effect cache, and another VR mod's files.
//
// It changes nothing on the PC: the registry and the mod's ini are only read; the one file written is the report. The instance is made as
// the game's ("D2R VR", engine "D2RLoader", vr/xr.cpp StartSession), with the mod's own controller bindings for the poses, so a layer's or a
// runtime's per-game settings act here as in the game; an implicit layer the mod turns off for the game ([openxr] block_listed_layers) is
// turned off here the same way (this process only, vr/api_layers_lib.cpp). While the game runs (D2R.exe, or D2RLoader.exe that hosts it)
// no OpenXR call is made at all (its session holds the headset): the registry's facts only.
//
//   D2R_VR_Headset_Report.exe [--out <file>] [--seconds <2-60>] [--no-session] [--no-wait] [--game <folder>]
//     --out       where the report goes (default: the desktop, D2R_VR_headset_report_<date>.txt)
//     --seconds   how long the eyes and hands are sampled once the session has the focus (default 10)
//     --no-wait   no "press Enter" at the end, nothing shown (D2R VR Settings runs it so)
//     --game      the game's folder to look at for D2R VR's files (default: this program's own folder, where Setup puts it; else found, GameDir)
//   exit 0 = the report written, 1 = it could not be written

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <tlhelp32.h>

#define XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D12
#define XR_USE_PLATFORM_WIN32
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <functional>
#include <cstring>
#include <string>
#include <vector>

#include "api_layers_lib.h"   // the implicit layers as the loader reads them (vr/), and the mod's block_listed_layers
#include "d2r_vr_version.h"
#include "eye_frame.h"        // each eye's turn from the head's frame, the angle between the eyes (vr/, as the plugin's log)
#include "hand_trackers.h"    // MetaRuntime
#include "mod_files.h"        // the mod's own files: the effects ReShade can pick up, the search paths, the hints
#include "report_stats.h"
#include "toolbar_type.h"     // the toolbar's type from [hud] hide and bar_place (shared/), as Settings names it
#include "xr_profiles.h"      // Steam Frame's bindings, as the plugin suggests them

using namespace headsetreport;

namespace {

std::string g_out;   // the report
bool g_quiet = false;

void Say(const char* f, ...) {   // the console's progress (not the report)
    if (g_quiet) return;
    va_list a;
    va_start(a, f);
    std::vprintf(f, a);
    va_end(a);
    std::printf("\n");
    std::fflush(stdout);
}
void Line(const std::string& s = std::string()) { g_out += s; g_out += "\r\n"; }
void L(const char* f, ...) {
    char b[2048];
    va_list a;
    va_start(a, f);
    std::vsnprintf(b, sizeof b, f, a);
    va_end(a);
    Line(b);
}
void Head(const char* title) {
    Line();
    Line(std::string("== ") + title + " ==");
}

std::string Utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}
std::wstring Wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n > 0 ? n : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}
std::string Env(const char* name) {
    char b[2048];
    const DWORD n = GetEnvironmentVariableA(name, b, sizeof b);
    return n > 0 && n < sizeof b ? std::string(b, n) : std::string();
}

XrInstance g_inst = XR_NULL_HANDLE;
const char* Res(XrResult r) {
    static char b[XR_MAX_RESULT_STRING_SIZE];
    if (g_inst && XR_SUCCEEDED(xrResultToString(g_inst, r, b))) return b;
    std::snprintf(b, sizeof b, "XrResult %d", (int)r);
    return b;
}

bool ProcessRunning(const wchar_t* exe) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{sizeof pe};
    bool found = false;
    for (BOOL ok = Process32FirstW(snap, &pe); ok && !found; ok = Process32NextW(snap, &pe)) found = _wcsicmp(pe.szExeFile, exe) == 0;
    CloseHandle(snap);
    return found;
}

std::wstring ExeDir() {
    wchar_t p[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s = p;
    const size_t at = s.find_last_of(L"\\/");
    return at == std::wstring::npos ? L"." : s.substr(0, at);
}
bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
std::wstring g_game;       // --game: the game's folder to look at (else found: GameDir)
const char* g_gameFrom = "--game";
bool LooksLikeGame(const std::wstring& dir) { return !dir.empty() && (Exists(dir + L"\\D2R.exe") || Exists(dir + L"\\mods\\D2R-VR")); }
std::wstring InstallLocation(const wchar_t* key) {
    for (HKEY root : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER})
        for (DWORD view : {RRF_SUBKEY_WOW6464KEY, RRF_SUBKEY_WOW6432KEY}) {
            wchar_t dir[MAX_PATH] = {};
            DWORD size = sizeof dir;
            const std::wstring k = std::wstring(L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\") + key;
            if (RegGetValueW(root, k.c_str(), L"InstallLocation", RRF_RT_REG_SZ | view, nullptr, dir, &size) != ERROR_SUCCESS || !dir[0]) continue;
            std::wstring s = dir;
            while (!s.empty() && (s.back() == L'\\' || s.back() == L'/')) s.pop_back();
            if (LooksLikeGame(s)) return s;
        }
    return {};
}
// The game's folder: --game; else this program's own folder, where Setup puts it beside D2R.exe (or a folder above it); else where D2R VR's
// Setup installed (its uninstall record) or the game's own Steam / Battle.net record says - the keys Setup's DefaultGameDir reads; else this
// program's folder, and "D2R VR's files" says it found no mod. g_gameFrom says which.
std::wstring GameDir() {
    static std::wstring found;
    if (!g_game.empty()) return g_game;
    if (!found.empty()) return found;
    std::wstring dir = ExeDir();
    for (int up = 0; up < 4 && !dir.empty(); ++up) {
        if (LooksLikeGame(dir)) { g_gameFrom = up ? "a folder above this program" : "this program's folder"; return found = dir; }
        const size_t at = dir.find_last_of(L"\\/");
        if (at == std::wstring::npos || at < 3) break;
        dir.resize(at);
    }
    for (const auto& [key, from] : {std::pair{L"{8C1F3A52-6D0B-4E47-9B7E-2D5A0C9E4F31}_is1", "D2R VR's Setup record"},
                                    std::pair{L"Steam App 2536520", "the game's Steam record"},
                                    std::pair{L"Diablo II Resurrected", "the game's Battle.net record"}})
        if (std::wstring s = InstallLocation(key); !s.empty()) { g_gameFrom = from; return found = s; }
    g_gameFrom = "not found - this program's folder";
    return found = ExeDir();
}
// The game: D2R.exe, or D2RLoader.exe - the game runs in D2RLoader's own process (D2RCore.dll), the plugin and the runtime with it.
bool GameRunning() { return ProcessRunning(L"D2R.exe") || ProcessRunning(L"D2RLoader.exe"); }

// --------------------------------------------------------------------------------------------------- the PC and the registry

void WindowsLine() {
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    OSVERSIONINFOW v{sizeof v};
    if (auto f = (RtlGetVersionFn)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")) f(&v);
    BOOL elevated = FALSE;
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION te{};
        DWORD len = 0;
        if (GetTokenInformation(token, TokenElevation, &te, sizeof te, &len)) elevated = te.TokenIsElevated;
        CloseHandle(token);
    }
    L("Windows %lu.%lu build %lu; this program %s", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber,
      elevated ? "runs as administrator (the loader then skips HKCU's layers)" : "runs as the user");
}

std::string RegString(HKEY hive, const wchar_t* key, const wchar_t* value) {
    wchar_t b[2048] = {};
    DWORD size = sizeof b - sizeof(wchar_t);
    if (RegGetValueW(hive, key, value, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, nullptr, b, &size) != ERROR_SUCCESS) return {};
    return Utf8(b);
}
// Each value under a key as the loader reads its lists: the name is a manifest's path, DWORD 0 = enabled.
void RegList(HKEY hive, const char* hiveName, const wchar_t* key, const char* what) {
    HKEY k = nullptr;
    if (RegOpenKeyExW(hive, key, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) { L("  %s %s: none", hiveName, what); return; }
    int n = 0;
    for (DWORD i = 0;; ++i) {
        wchar_t name[1024];
        DWORD nameLen = 1023, type = 0, value = 1, valueLen = sizeof value;
        const LONG r = RegEnumValueW(k, i, name, &nameLen, nullptr, &type, reinterpret_cast<BYTE*>(&value), &valueLen);
        if (r == ERROR_NO_MORE_ITEMS) break;
        if (r != ERROR_SUCCESS) continue;
        const std::wstring path(name, nameLen);
        const bool dword = type == REG_DWORD && valueLen == sizeof value;
        L("  %s %s [%s] %s%s", hiveName, what, !dword ? "not a DWORD" : value == 0 ? "on " : "off", Utf8(path).c_str(), Exists(path) ? "" : " (file missing)");
        ++n;
    }
    RegCloseKey(k);
    if (!n) L("  %s %s: none", hiveName, what);
}

// The manifest's "runtime" name and library, read as text (small JSON: the first "name" and "library_path").
std::string JsonString(const std::string& text, const char* key) {
    const std::string k = std::string("\"") + key + "\"";
    size_t at = text.find(k);
    if (at == std::string::npos) return {};
    at = text.find(':', at + k.size());
    if (at == std::string::npos) return {};
    at = text.find('"', at);
    if (at == std::string::npos) return {};
    std::string v;
    for (size_t i = at + 1; i < text.size() && text[i] != '"'; ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) ++i;
        v += text[i];
    }
    return v;
}
std::string ReadSmall(const std::wstring& path, LONGLONG most = 1 << 20) {
    std::string t;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return t;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(h, &size) && size.QuadPart > 0 && size.QuadPart < most) {
        t.resize((size_t)size.QuadPart);
        DWORD got = 0;
        if (!ReadFile(h, t.data(), (DWORD)t.size(), &got, nullptr)) got = 0;
        t.resize(got);
    }
    CloseHandle(h);
    return t;
}

void RuntimeRegistry() {
    Head("OpenXR runtime and layers (registry, read only)");
    const wchar_t* base = L"SOFTWARE\\Khronos\\OpenXR\\1";
    const std::string hklm = RegString(HKEY_LOCAL_MACHINE, base, L"ActiveRuntime");
    const std::string hkcu = RegString(HKEY_CURRENT_USER, base, L"ActiveRuntime");
    const std::string env = Env("XR_RUNTIME_JSON");
    L("Active runtime (HKLM): %s", hklm.empty() ? "none" : hklm.c_str());
    if (!hkcu.empty()) L("Active runtime (HKCU, the loader does not read it): %s", hkcu.c_str());
    if (!env.empty()) L("XR_RUNTIME_JSON (overrides the registry): %s", env.c_str());
    const std::string active = !env.empty() ? env : hklm;
    if (!active.empty()) {
        const std::string text = ReadSmall(Wide(active));
        if (text.empty()) L("  its manifest could not be read");
        else L("  manifest: name \"%s\", library %s", JsonString(text, "name").c_str(), JsonString(text, "library_path").c_str());
    }
    RegList(HKEY_LOCAL_MACHINE, "HKLM", L"SOFTWARE\\Khronos\\OpenXR\\1\\AvailableRuntimes", "runtime");
    RegList(HKEY_LOCAL_MACHINE, "HKLM", L"SOFTWARE\\Khronos\\OpenXR\\1\\ApiLayers\\Implicit", "implicit layer");
    RegList(HKEY_CURRENT_USER, "HKCU", L"SOFTWARE\\Khronos\\OpenXR\\1\\ApiLayers\\Implicit", "implicit layer");
    RegList(HKEY_LOCAL_MACHINE, "HKLM", L"SOFTWARE\\Khronos\\OpenXR\\1\\ApiLayers\\Explicit", "explicit layer");
    RegList(HKEY_CURRENT_USER, "HKCU", L"SOFTWARE\\Khronos\\OpenXR\\1\\ApiLayers\\Explicit", "explicit layer");
    for (const char* v : {"XR_ENABLE_API_LAYERS", "XR_API_LAYER_PATH", "XR_LOADER_DEBUG"}) {
        const std::string s = Env(v);
        if (!s.empty()) L("%s = %s", v, s.c_str());
    }
}

// --------------------------------------------------------------------------------------------------- the mod's own settings

// The mod's d2r_vr.ini: beside this program in the game's folder, the mod's (mods\D2R-VR) or the older layout's.
std::wstring ModIni() {
    const std::wstring dir = GameDir();
    for (const wchar_t* rel : {L"\\mods\\D2R-VR\\d2rloader\\plugins\\d2r_vr.ini", L"\\d2rloader\\plugins\\d2r_vr.ini", L"\\d2r_vr.ini"})
        if (Exists(dir + rel)) return dir + rel;
    return {};
}
int IniInt(const std::wstring& ini, const wchar_t* key, int def) { return ini.empty() ? def : (int)GetPrivateProfileIntW(L"openxr", key, def, ini.c_str()); }

void ModSettings(const std::wstring& ini) {
    Head("The mod's [openxr] settings (d2r_vr.ini, read only)");
    if (ini.empty()) { L("d2r_vr.ini not found in the game's folder (%s) - run this program from the game's folder, or with --game <folder>", Utf8(GameDir()).c_str()); return; }
    L("%s", Utf8(ini).c_str());
    std::vector<wchar_t> b(32768);
    const DWORD n = GetPrivateProfileSectionW(L"openxr", b.data(), (DWORD)b.size(), ini.c_str());
    if (!n) { L("  no [openxr] section: every key at its default"); return; }
    for (const wchar_t* p = b.data(); *p; p += wcslen(p) + 1)
        if (*p != L';') L("  %s", Utf8(p).c_str());
}

// --------------------------------------------------------------------------------------------------- the mod's own files

std::string Sha256(const std::string& data) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    unsigned char h[32] = {};
    bool ok = BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0));
    ok = ok && BCRYPT_SUCCESS(BCryptHash(alg, nullptr, 0, (PUCHAR)data.data(), (ULONG)data.size(), h, sizeof h));
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    if (!ok) return "?";
    std::string s;
    for (unsigned char c : h) s += Fmt("%02x", c);
    return s;
}
std::string TimeText(const FILETIME& ft) {
    FILETIME local;
    SYSTEMTIME st;
    FileTimeToLocalFileTime(&ft, &local);
    FileTimeToSystemTime(&local, &st);
    return Fmt("%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}
// A program's own name for itself and its version (VERSIONINFO), "" when it has none.
std::string VersionText(const std::wstring& path) {
    DWORD dummy = 0;
    const DWORD n = GetFileVersionInfoSizeW(path.c_str(), &dummy);
    if (!n) return {};
    std::vector<BYTE> b(n);
    if (!GetFileVersionInfoW(path.c_str(), 0, n, b.data())) return {};
    struct Lang { WORD lang, cp; }* langs = nullptr;
    UINT len = 0;
    std::wstring base = L"\\StringFileInfo\\040904b0\\";
    if (VerQueryValueW(b.data(), L"\\VarFileInfo\\Translation", (void**)&langs, &len) && len >= sizeof(Lang)) {
        wchar_t s[64];
        swprintf_s(s, L"\\StringFileInfo\\%04x%04x\\", langs[0].lang, langs[0].cp);
        base = s;
    }
    auto get = [&](const wchar_t* key) {
        wchar_t* v = nullptr;
        UINT vl = 0;
        return VerQueryValueW(b.data(), (base + key).c_str(), (void**)&v, &vl) && v && vl ? Utf8(v) : std::string();
    };
    const std::string product = get(L"ProductName"), desc = get(L"FileDescription"), ver = get(L"FileVersion");
    return Fmt("%s%s%s", (desc.empty() ? product : desc).c_str(), ver.empty() ? "" : " ", ver.c_str());
}
// "path: size, time[, version][, sha256]" or "path: none".
std::string FileLine(const std::wstring& path, bool hash = false) {
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa) || (fa.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return Utf8(path) + ": none";
    const unsigned long long size = ((unsigned long long)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
    std::string s = Fmt("%s: %llu bytes, %s", Utf8(path).c_str(), size, TimeText(fa.ftLastWriteTime).c_str());
    const std::string v = VersionText(path);
    if (!v.empty()) s += ", " + v;
    if (hash) s += ", sha256 " + Sha256(ReadSmall(path, 512ll << 20));
    return s;
}
// The files (and with `recursive` the subfolders') of a folder, at most `budget` entries in all.
void Walk(const std::wstring& dir, bool recursive, int& budget, const std::function<void(const std::wstring&, const WIN32_FIND_DATAW&)>& f) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    std::vector<std::wstring> sub;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        if (--budget < 0) break;
        const std::wstring p = dir + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (recursive && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) sub.push_back(p); }
        else f(p, fd);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    for (const std::wstring& s : sub) Walk(s, true, budget, f);
}
std::vector<std::wstring> SubFolders(const std::wstring& dir) {
    std::vector<std::wstring> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && wcscmp(fd.cFileName, L".") && wcscmp(fd.cFileName, L".."))
            out.push_back(fd.cFileName);
    while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}
std::wstring IniString(const std::wstring& ini, const wchar_t* section, const wchar_t* key) {
    wchar_t b[4096] = L"\x1";   // (a key not there reads as the default: \x1)
    GetPrivateProfileStringW(section, key, L"\x1", b, 4096, ini.c_str());
    return b[0] == 1 ? std::wstring() : std::wstring(b);
}
bool IniHas(const std::wstring& ini, const wchar_t* section, const wchar_t* key) {
    wchar_t b[8] = L"";
    return GetPrivateProfileStringW(section, key, L"\x1", b, 8, ini.c_str()) != 1 || b[0] != 1;
}
std::wstring ExpandEnv(const std::wstring& s) {
    wchar_t b[2048];
    const DWORD n = ExpandEnvironmentStringsW(s.c_str(), b, 2048);
    return n > 0 && n <= 2048 ? std::wstring(b) : s;
}

// One ReShade.ini: its [GENERAL] keys of the effects, the preset's techniques, every D2R_DepthFog.fx / *.fxh its EffectSearchPaths reach (with
// what each holds), and its effect cache.
modfiles::IniSeen ReShadeIni(const std::wstring& ini, bool ours) {
    modfiles::IniSeen seen;
    seen.path = Utf8(ini);
    seen.ours = ours;
    const std::wstring dir = ini.substr(0, ini.find_last_of(L'\\'));
    std::string dlls;
    for (const wchar_t* d : {L"ReShade64.dll", L"dxgi.dll", L"d3d12.dll", L"d3d11.dll"})
        if (Exists(dir + L"\\" + d)) dlls += std::string(dlls.empty() ? "" : ", ") + Utf8(d);
    seen.dllBeside = !dlls.empty();
    L("%s - %s", seen.path.c_str(), dlls.empty() ? "no ReShade DLL beside it (not read by a ReShade)" : ("beside " + dlls).c_str());
    for (const wchar_t* k : {L"EffectSearchPaths", L"TextureSearchPaths", L"PresetPath", L"PreprocessorDefinitions", L"IntermediateCachePath", L"NoEffectCache",
                             L"PerformanceMode", L"SkipLoadingDisabledEffects"})
        if (IniHas(ini, L"GENERAL", k)) L("  [GENERAL] %s=%s", Utf8(k).c_str(), Utf8(IniString(ini, L"GENERAL", k)).c_str());
    // the preset: its techniques, and its own preprocessor definitions of the effect
    std::wstring preset = IniString(ini, L"GENERAL", L"PresetPath");
    if (!preset.empty()) {
        preset = Wide(modfiles::Resolve(Utf8(dir), Utf8(preset)));
        L("  preset %s", FileLine(preset).c_str());
        const std::string text = ReadSmall(preset);
        for (const char* k : {"Techniques", "TechniqueSorting", "PreprocessorDefinitions"}) {
            const std::string v = modfiles::TopValue(text, k);
            if (!v.empty()) L("    %s=%s", k, v.c_str());
        }
        std::vector<wchar_t> b(32768);
        const DWORD n = GetPrivateProfileSectionW(L"D2R_DepthFog.fx", b.data(), (DWORD)b.size(), preset.c_str());
        int keys = 0;
        for (const wchar_t* p = b.data(); n && *p; p += wcslen(p) + 1) {
            ++keys;
            if (!_wcsnicmp(p, L"PreprocessorDefinitions", 23)) L("    [D2R_DepthFog.fx] %s", Utf8(p).c_str());
        }
        L("    [D2R_DepthFog.fx]: %d values", keys);
    }
    // the effects its search paths reach, in ReShade's order
    const std::vector<modfiles::SearchPath> paths = modfiles::SplitSearchPaths(Utf8(IniString(ini, L"GENERAL", L"EffectSearchPaths")));
    if (paths.empty()) L("  no EffectSearchPaths: ReShade looks in its own folder only");
    std::vector<modfiles::SearchPath> walk = paths;
    if (walk.empty()) walk.push_back({".", false});
    int fxh = 0, otherFx = 0;
    for (const modfiles::SearchPath& sp : walk) {
        const std::wstring folder = Wide(modfiles::Resolve(Utf8(dir), sp.path));
        int budget = 20000;
        Walk(folder, sp.recursive, budget, [&](const std::wstring& p, const WIN32_FIND_DATAW& fd) {
            const std::wstring name = fd.cFileName;
            const size_t dot = name.find_last_of(L'.');
            const std::wstring ext = dot == std::wstring::npos ? L"" : name.substr(dot);
            const bool fog = !_wcsicmp(name.c_str(), L"D2R_DepthFog.fx");
            if (!fog && _wcsicmp(ext.c_str(), L".fxh")) { if (!_wcsicmp(ext.c_str(), L".fx")) ++otherFx; return; }
            if (!fog && ++fxh > 30) return;
            const std::string text = ReadSmall(p, 64 << 20);
            const modfiles::Markers m = modfiles::FindMarkers(text);
            L("  %s %s; %s", fog ? "EFFECT" : "include", FileLine(p).c_str(), fog ? modfiles::MarkersText(m).c_str() : "");
            L("      sha256 %s", Sha256(text).c_str());
            if (fog) seen.depthFog.push_back({Utf8(p), m});
        });
    }
    if (fxh > 30) L("  ... and %d more .fxh", fxh - 30);
    L("  other effects (.fx) the search paths reach: %d", otherFx);
    // the effect cache: what ReShade compiled before (an entry is reused only for the same source)
    std::wstring cache = IniString(ini, L"GENERAL", L"IntermediateCachePath");
    if (cache.empty()) cache = L"%TEMP%\\ReShade";
    cache = Wide(modfiles::Resolve(Utf8(dir), Utf8(ExpandEnv(cache))));
    seen.cache = Utf8(cache);
    if (GetFileAttributesW(cache.c_str()) == INVALID_FILE_ATTRIBUTES) {
        L("  effect cache %s: not there", Utf8(cache).c_str());
        return seen;
    }
    // D2R_DepthFog's entries by compile (its shader model and source hash), the newest compiles first
    struct Compile { std::string key; int files = 0; FILETIME newest{}; };
    std::vector<Compile> compiles;
    int all = 0, fog = 0, budget = 50000;
    Walk(cache, false, budget, [&](const std::wstring&, const WIN32_FIND_DATAW& fd) {
        ++all;
        const std::string key = modfiles::CacheCompile(Utf8(fd.cFileName));
        if (key.empty() && !StrStrIW(fd.cFileName, L"D2R_DepthFog")) return;
        ++fog;
        auto it = std::find_if(compiles.begin(), compiles.end(), [&](const Compile& c) { return c.key == key; });
        if (it == compiles.end()) { compiles.push_back({key, 0, fd.ftLastWriteTime}); it = compiles.end() - 1; }
        ++it->files;
        if (CompareFileTime(&fd.ftLastWriteTime, &it->newest) > 0) it->newest = fd.ftLastWriteTime;
    });
    std::sort(compiles.begin(), compiles.end(), [](const Compile& a, const Compile& b) { return CompareFileTime(&a.newest, &b.newest) > 0; });
    seen.cacheEntries = fog;
    L("  effect cache %s: %d files, %d of them D2R_DepthFog's, from %d compiles%s", Utf8(cache).c_str(), all, fog, (int)compiles.size(),
      compiles.empty() ? "" : " (the newest first:)");
    for (size_t i = 0; i < compiles.size() && i < 5; ++i)
        L("    %s: %d files, %s", compiles[i].key.empty() ? "(other names)" : compiles[i].key.c_str(), compiles[i].files, TimeText(compiles[i].newest).c_str());
    return seen;
}

// The key values of the path in use, in d2r_vr.ini and in the shipped d2r_vr.default.ini beside it.
void KeyValues(const std::wstring& ini, modfiles::ModFacts& mf) {
    const std::wstring def = ini.substr(0, ini.find_last_of(L'\\')) + L"\\d2r_vr.default.ini";
    const bool hasDef = Exists(def);
    L("key values (d2r_vr.ini%s):", hasDef ? " / the shipped d2r_vr.default.ini" : "");
    const struct { const wchar_t *s, *k; } keys[] = {{L"render", L"no_reshade"}, {L"openxr", L"on"}, {L"openxr", L"runtime_fov"}, {L"openxr", L"fov_override"}, {L"openxr", L"fov_override_centred"},
                                                     {L"openxr", L"flat_distance_m"}, {L"openxr", L"flat_width_m"}, {L"stereo", L"afr"},
                                                     {L"stereo", L"pair_per_tick"}, {L"stereo", L"replay_right"}, {L"hud", L"hide"}, {L"hud", L"bar_place"},
                                                     {L"mode", L"platform"}};
    for (const auto& k : keys) {
        const std::wstring v = IniString(ini, k.s, k.k), d = hasDef ? IniString(def, k.s, k.k) : std::wstring();
        L("  [%s] %s = %s%s%s", Utf8(k.s).c_str(), Utf8(k.k).c_str(), v.empty() ? "(not set)" : Utf8(v).c_str(), hasDef ? " / " : "",
          hasDef ? (d.empty() ? "(not set)" : Utf8(d).c_str()) : "");
    }
    const int hide = (int)GetPrivateProfileIntW(L"hud", L"hide", 1, ini.c_str()), place = (int)GetPrivateProfileIntW(L"hud", L"bar_place", 5, ini.c_str());
    L("  Toolbar type (Settings > UI: F4 body): %s", toolbartype::kShort[toolbartype::TypeOf(hide, place)]);
    mf.noReshade = IniHas(ini, L"render", L"no_reshade") ? (int)GetPrivateProfileIntW(L"render", L"no_reshade", -1, ini.c_str()) : -1;
    mf.noReshadeDefault = hasDef && IniHas(def, L"render", L"no_reshade") ? (int)GetPrivateProfileIntW(L"render", L"no_reshade", -1, def.c_str()) : -1;
}

modfiles::ModFacts ModFiles(const std::wstring& ini) {
    modfiles::ModFacts mf;
    Head("D2R VR's files (read only)");
    const std::wstring game = GameDir(), mods = game + L"\\mods", mod = mods + L"\\D2R-VR";
    L("the game's folder: %s (%s)", Utf8(game).c_str(), g_gameFrom);
    mf.modFound = Exists(mod) || Exists(game + L"\\d2rloader\\plugins\\d2rl-vrcam.dll");
    if (!mf.modFound) { L("no mods\\D2R-VR there: run it from the game's folder, or with --game <folder>, for this part"); return mf; }
    L("programs:");
    for (const std::wstring& p : {mod + L"\\d2rloader\\plugins\\d2rl-vrcam.dll", game + L"\\d2rloader\\plugins\\d2rl-vrcam.dll"})
        if (Exists(p)) L("  %s", FileLine(p, true).c_str());
    for (const std::wstring& p : {game + L"\\D2R_VR_Settings.exe", game + L"\\D2R_VR_Headset_Report.exe", game + L"\\D2RLoader.exe", game + L"\\D2R.exe",
                                  game + L"\\dxgi.dll", mod + L"\\vrmod-manifest.json"})   // (dxgi.dll: a player's own ReShade - left as it is,
                                                                                         // D2R VR uses no ReShade since 0.203; FlatVR's add-on not shipped)
        L("  %s", FileLine(p).c_str());
    const std::string manifest = ReadSmall(mod + L"\\vrmod-manifest.json");
    if (!manifest.empty()) {
        const std::string id = JsonString(manifest, "id");
        L("  mods\\D2R-VR\\vrmod-manifest.json: id \"%s\", name \"%s\", version \"%s\"", id.c_str(), JsonString(manifest, "name").c_str(),
          JsonString(manifest, "version").c_str());
        if (!id.empty() && id != "d2r-vr") mf.otherVrMods.push_back("mods\\D2R-VR\\vrmod-manifest.json is \"" + id + "\"'s, not D2R VR Authentic's");
    }
    if (!ini.empty()) KeyValues(ini, mf);

    // every ReShade setup: beside the game, and each mod's (its root and its reshade folder) - d2r-pcvr ships an effect of the same name
    L("ReShade:");
    std::vector<std::wstring> folders = {game};
    for (const std::wstring& m : SubFolders(mods)) { folders.push_back(mods + L"\\" + m); folders.push_back(mods + L"\\" + m + L"\\reshade"); }
    for (const std::wstring& f : folders)
        for (const wchar_t* d : {L"ReShade64.dll", L"dxgi.dll", L"d3d12.dll", L"d3d11.dll"})
            if (Exists(f + L"\\" + d)) L("  %s", FileLine(f + L"\\" + d).c_str());
    for (const std::wstring& f : folders) {
        int budget = 2000;
        Walk(f, false, budget, [&](const std::wstring& p, const WIN32_FIND_DATAW& fd) {
            for (const wchar_t* n : {L"ReShade.ini", L"ReShade64.ini", L"dxgi.ini", L"d3d12.ini"})   // (its ini's names: the default, and an older ReShade's own DLL name)
                if (!_wcsicmp(fd.cFileName, n)) {
                    auto under = [&](const std::wstring& d) { return p.size() > d.size() && !_wcsnicmp(p.c_str(), (d + L"\\").c_str(), d.size() + 1); };
                    mf.inis.push_back(ReShadeIni(p, !under(mods) || under(mod)));   // (D2R VR's own: beside the game, or in mods\D2R-VR)
                }
        });
    }
    if (mf.inis.empty()) L("  no ReShade.ini beside the game or in a mod");

    // another VR mod: a mod folder with its own d2rloader plugins, plugins in the global folder, d2r-pcvr's / Unleashed's files
    L("mods and plugins:");
    for (const std::wstring& m : SubFolders(mods)) {
        std::string plugins;
        int budget = 500;
        Walk(mods + L"\\" + m + L"\\d2rloader\\plugins", false, budget, [&](const std::wstring&, const WIN32_FIND_DATAW& fd) {
            const std::wstring n = fd.cFileName;
            if (n.size() > 4 && !_wcsicmp(n.c_str() + n.size() - 4, L".dll")) plugins += std::string(plugins.empty() ? "" : ", ") + Utf8(n);
        });
        L("  mods\\%s: d2rloader plugins %s", Utf8(m).c_str(), plugins.empty() ? "none" : plugins.c_str());
        if (_wcsicmp(m.c_str(), L"D2R-VR") && !plugins.empty()) mf.otherVrMods.push_back("mods\\" + Utf8(m) + " (" + plugins + ")");
    }
    {
        std::string plugins;
        int budget = 500;
        Walk(game + L"\\d2rloader\\plugins", false, budget, [&](const std::wstring&, const WIN32_FIND_DATAW& fd) {
            const std::wstring n = fd.cFileName;
            if (n.size() > 4 && !_wcsicmp(n.c_str() + n.size() - 4, L".dll")) plugins += std::string(plugins.empty() ? "" : ", ") + Utf8(n);
        });
        L("  the global d2rloader\\plugins: %s", plugins.empty() ? "none" : plugins.c_str());
    }
    auto sign = [&](const std::wstring& p, const WIN32_FIND_DATAW& fd, bool root) {
        if (modfiles::UnleashedFile(Utf8(fd.cFileName), root)) mf.unleashedSigns.push_back(Utf8(p));
    };
    int budget = 30000;
    Walk(mod, true, budget, [&](const std::wstring& p, const WIN32_FIND_DATAW& fd) { sign(p, fd, false); });
    budget = 2000;
    Walk(game, false, budget, [&](const std::wstring& p, const WIN32_FIND_DATAW& fd) { sign(p, fd, true); });
    Walk(game + L"\\d2rloader", false, budget, [&](const std::wstring& p, const WIN32_FIND_DATAW& fd) { sign(p, fd, false); });
    Walk(game + L"\\d2rloader\\plugins", false, budget, [&](const std::wstring& p, const WIN32_FIND_DATAW& fd) { sign(p, fd, false); });
    L("  files of D2R VR Unleashed / d2r-pcvr in D2R VR's folders and beside the game: %d", (int)mf.unleashedSigns.size());
    for (size_t i = 0; i < mf.unleashedSigns.size() && i < 20; ++i) L("    %s", FileLine(Wide(mf.unleashedSigns[i])).c_str());
    return mf;
}

// --------------------------------------------------------------------------------------------------- the runtime without a session

struct Offered {
    std::vector<XrExtensionProperties> ext;
    bool Has(const char* n) const {
        for (const auto& e : ext) if (!strcmp(e.extensionName, n)) return true;
        return false;
    }
};

Offered LoaderLists() {
    Head("API layers and extensions (the OpenXR loader)");
    uint32_t nl = 0;
    xrEnumerateApiLayerProperties(0, &nl, nullptr);
    std::vector<XrApiLayerProperties> layers(nl, {XR_TYPE_API_LAYER_PROPERTIES});
    if (nl) xrEnumerateApiLayerProperties(nl, &nl, layers.data());
    L("API layers available (%u; the implicit ones load into every OpenXR program, this one and the game too):", nl);
    for (const auto& l : layers)
        L("  %s  spec %u.%u.%u, version %u - %s", l.layerName, XR_VERSION_MAJOR(l.specVersion), XR_VERSION_MINOR(l.specVersion),
          XR_VERSION_PATCH(l.specVersion), l.layerVersion, l.description);
    Offered o;
    uint32_t n = 0;
    const XrResult r = xrEnumerateInstanceExtensionProperties(nullptr, 0, &n, nullptr);
    if (XR_FAILED(r)) { L("xrEnumerateInstanceExtensionProperties failed: %d (no runtime, or it did not load)", (int)r); return o; }
    o.ext.assign(n, {XR_TYPE_EXTENSION_PROPERTIES});
    xrEnumerateInstanceExtensionProperties(nullptr, n, &n, o.ext.data());
    L("Extensions (%u):", n);
    std::string row;
    for (const auto& e : o.ext) {
        const std::string item = Fmt("%s v%u", e.extensionName, e.extensionVersion);
        if (!row.empty() && row.size() + item.size() > 150) { L("  %s", row.c_str()); row.clear(); }
        row += row.empty() ? item : ", " + item;
    }
    if (!row.empty()) L("  %s", row.c_str());
    return o;
}

const char* FormFactorName(XrViewConfigurationType t) {
    switch (t) {
    case XR_VIEW_CONFIGURATION_TYPE_PRIMARY_MONO: return "PRIMARY_MONO";
    case XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO: return "PRIMARY_STEREO";
    case XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO: return "PRIMARY_QUAD_VARJO";
    case XR_VIEW_CONFIGURATION_TYPE_SECONDARY_MONO_FIRST_PERSON_OBSERVER_MSFT: return "SECONDARY_MONO_FIRST_PERSON_OBSERVER_MSFT";
    default: return "other";
    }
}
const char* BlendName(XrEnvironmentBlendMode m) {
    switch (m) {
    case XR_ENVIRONMENT_BLEND_MODE_OPAQUE: return "OPAQUE";
    case XR_ENVIRONMENT_BLEND_MODE_ADDITIVE: return "ADDITIVE";
    case XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND: return "ALPHA_BLEND";
    default: return "other";
    }
}
const char* SpaceName(XrReferenceSpaceType t) {
    switch (t) {
    case XR_REFERENCE_SPACE_TYPE_VIEW: return "VIEW";
    case XR_REFERENCE_SPACE_TYPE_LOCAL: return "LOCAL";
    case XR_REFERENCE_SPACE_TYPE_STAGE: return "STAGE";
    case XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR_EXT: return "LOCAL_FLOOR";
    case XR_REFERENCE_SPACE_TYPE_UNBOUNDED_MSFT: return "UNBOUNDED_MSFT";
    default: return "other";
    }
}
const char* StateName(XrSessionState s) {
    switch (s) {
    case XR_SESSION_STATE_IDLE: return "IDLE";
    case XR_SESSION_STATE_READY: return "READY";
    case XR_SESSION_STATE_SYNCHRONIZED: return "SYNCHRONIZED";
    case XR_SESSION_STATE_VISIBLE: return "VISIBLE";
    case XR_SESSION_STATE_FOCUSED: return "FOCUSED";
    case XR_SESSION_STATE_STOPPING: return "STOPPING";
    case XR_SESSION_STATE_LOSS_PENDING: return "LOSS_PENDING";
    case XR_SESSION_STATE_EXITING: return "EXITING";
    default: return "UNKNOWN";
    }
}
const char* FormatName(int64_t f) {
    switch ((DXGI_FORMAT)f) {
    case DXGI_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return "R8G8B8A8_UNORM_SRGB";
    case DXGI_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return "B8G8R8A8_UNORM_SRGB";
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return "R16G16B16A16_FLOAT";
    case DXGI_FORMAT_R10G10B10A2_UNORM: return "R10G10B10A2_UNORM";
    case DXGI_FORMAT_R11G11B10_FLOAT: return "R11G11B10_FLOAT";
    case DXGI_FORMAT_D32_FLOAT: return "D32_FLOAT";
    case DXGI_FORMAT_D24_UNORM_S8_UINT: return "D24_UNORM_S8_UINT";
    case DXGI_FORMAT_D16_UNORM: return "D16_UNORM";
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return "D32_FLOAT_S8X24_UINT";
    default: return nullptr;
    }
}

// Every graphics adapter, the runtime's marked.
void Adapters(const LUID* want12, const LUID* want11) {
    IDXGIFactory1* f = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&f))) { L("  DXGI: no factory"); return; }
    IDXGIAdapter1* a = nullptr;
    for (UINT i = 0; f->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 d{};
        a->GetDesc1(&d);
        const bool is12 = want12 && d.AdapterLuid.LowPart == want12->LowPart && d.AdapterLuid.HighPart == want12->HighPart;
        const bool is11 = want11 && d.AdapterLuid.LowPart == want11->LowPart && d.AdapterLuid.HighPart == want11->HighPart;
        L("  adapter %u: %s, %llu MB dedicated, LUID %08lX:%08lX%s%s%s", i, Utf8(d.Description).c_str(), (unsigned long long)(d.DedicatedVideoMemory >> 20),
          d.AdapterLuid.HighPart, d.AdapterLuid.LowPart, (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ? " (software)" : "",
          i == 0 ? " - Windows' first (the game's, unless Windows' graphics settings pick another)" : "",
          is12 || is11 ? " <- the headset's (the runtime's)" : "");
        a->Release();
    }
    f->Release();
}

// --------------------------------------------------------------------------------------------------- the session

struct Gfx {
    bool d3d12 = false;
    ID3D12Device* dev12 = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    ID3D12CommandAllocator* alloc = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    ID3D12Fence* fence = nullptr;
    UINT64 fenceValue = 0;
    HANDLE fenceEvent = nullptr;
    ID3D12DescriptorHeap* rtvHeap = nullptr;
    UINT rtvStep = 0;
    ID3D11Device* dev11 = nullptr;
    ID3D11DeviceContext* ctx11 = nullptr;
    void Release() {
        for (IUnknown* u : std::initializer_list<IUnknown*>{rtvHeap, fence, list, alloc, queue, dev12, ctx11, dev11}) if (u) u->Release();
        if (fenceEvent) CloseHandle(fenceEvent);
        *this = Gfx{};
    }
};

IDXGIAdapter1* AdapterByLuid(const LUID& luid) {
    IDXGIFactory4* f = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), (void**)&f))) return nullptr;
    IDXGIAdapter1* a = nullptr;
    if (FAILED(f->EnumAdapterByLuid(luid, __uuidof(IDXGIAdapter1), (void**)&a))) a = nullptr;
    f->Release();
    return a;
}

// Each eye's chain: a picture cleared to dark grey a frame, so the runtime gets frames as from a game (a runtime may never give the focus to a
// session that submits nothing).
struct Chain {
    XrSwapchain sc = XR_NULL_HANDLE;
    std::vector<ID3D12Resource*> images;
    int32_t w = 0, h = 0;
};

struct Sampler {
    // [space 0 VIEW, 1 LOCAL, 2 STAGE, 3 LOCAL_FLOOR]
    static constexpr int kSpaces = 4;
    XrSpace space[kSpaces] = {};
    Located views[kSpaces];
    FovSeries fov[kSpaces][2];
    Series gapMm[kSpaces];
    Series eyePos[2][3];         // VIEW: each eye's place from the head, mm
    Series cantDeg;
    Series ypr[2][3];            // VIEW: each eye's yaw pitch roll from the head's frame
    Located head[kSpaces];       // VIEW located in each space (index 0 unused)
    Series headHeight[kSpaces];  // its height there, m
    Located localInStage;
    Series localInStageY, localInStageYaw;
    Located grip[2][kSpaces], aim[2][kSpaces];   // [hand][space] (VIEW: the hand from the head)
    Series gripAimCm[2];
    Series gripBelowHead[2];     // in LOCAL: how far under the head the grip is, cm (hands at the waist: 50+)
    Series refreshHz;
    int frames = 0, focusedFrames = 0;
};

constexpr const char* kSpaceNames[Sampler::kSpaces] = {"VIEW", "LOCAL", "STAGE", "LOCAL_FLOOR"};

FovDeg ToDeg(const XrFovf& f) { return {f.angleLeft * kDeg, f.angleRight * kDeg, f.angleUp * kDeg, f.angleDown * kDeg}; }
eyeframe::Quat Q(const XrQuaternionf& q) { return {q.x, q.y, q.z, q.w}; }

void SampleFrame(Sampler& s, XrSession session, XrTime t, bool focused, XrActionSet set, XrSpace gripS[2], XrSpace aimS[2]) {
    ++s.frames;
    if (focused) ++s.focusedFrames;
    for (int sp = 0; sp < Sampler::kSpaces; ++sp) {
        if (!s.space[sp]) continue;
        XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO};
        li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        li.displayTime = t;
        li.space = s.space[sp];
        XrViewState vs{XR_TYPE_VIEW_STATE};
        XrView v[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
        uint32_t n = 0;
        const bool ok = XR_SUCCEEDED(xrLocateViews(session, &li, &vs, 2, &n, v)) && n == 2;
        s.views[sp].Add(ok, ok ? vs.viewStateFlags : 0);
        if (!ok || !(vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) continue;
        for (int e = 0; e < 2; ++e) s.fov[sp][e].Add(ToDeg(v[e].fov));
        const XrVector3f &a = v[0].pose.position, &b = v[1].pose.position;
        if (vs.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT)
            s.gapMm[sp].Add(1000.0f * std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)));
        if (sp == 0) {
            for (int e = 0; e < 2; ++e) {
                s.eyePos[e][0].Add(v[e].pose.position.x * 1000.0f);
                s.eyePos[e][1].Add(v[e].pose.position.y * 1000.0f);
                s.eyePos[e][2].Add(v[e].pose.position.z * 1000.0f);
            }
            const eyeframe::Quat q0 = Q(v[0].pose.orientation), q1 = Q(v[1].pose.orientation);
            s.cantDeg.Add(eyeframe::AngleBetween(q0, q1) * kDeg);
            const eyeframe::Quat mid = eyeframe::Mean(q0, q1);
            for (int e = 0; e < 2; ++e) {
                float y, p, r;
                eyeframe::Relative(mid, e ? q1 : q0, &y, &p, &r);
                s.ypr[e][0].Add(y); s.ypr[e][1].Add(p); s.ypr[e][2].Add(r);
            }
        }
    }
    for (int sp = 1; sp < Sampler::kSpaces; ++sp) {
        if (!s.space[sp] || !s.space[0]) continue;
        XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
        const bool ok = XR_SUCCEEDED(xrLocateSpace(s.space[0], s.space[sp], t, &loc));
        s.head[sp].Add(ok, ok ? loc.locationFlags : 0);
        if (ok && (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) s.headHeight[sp].Add(loc.pose.position.y);
    }
    if (s.space[1] && s.space[2]) {
        XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
        const bool ok = XR_SUCCEEDED(xrLocateSpace(s.space[1], s.space[2], t, &loc));
        s.localInStage.Add(ok, ok ? loc.locationFlags : 0);
        if (ok && Whole(loc.locationFlags)) {
            s.localInStageY.Add(loc.pose.position.y);
            const XrQuaternionf& q = loc.pose.orientation;
            s.localInStageYaw.Add(std::atan2(2.0f * (q.w * q.y + q.x * q.z), 1.0f - 2.0f * (q.y * q.y + q.x * q.x)) * kDeg);
        }
    }
    if (!focused || !set) return;
    XrActiveActionSet active{set, XR_NULL_PATH};
    XrActionsSyncInfo si{XR_TYPE_ACTIONS_SYNC_INFO};
    si.countActiveActionSets = 1;
    si.activeActionSets = &active;
    if (XR_FAILED(xrSyncActions(session, &si))) return;
    for (int h = 0; h < 2; ++h) {
        XrSpaceLocation g[Sampler::kSpaces], a[Sampler::kSpaces];
        for (int sp = 0; sp < Sampler::kSpaces; ++sp) {
            g[sp] = {XR_TYPE_SPACE_LOCATION};
            a[sp] = {XR_TYPE_SPACE_LOCATION};
            if (!s.space[sp]) continue;
            const bool gOk = gripS[h] && XR_SUCCEEDED(xrLocateSpace(gripS[h], s.space[sp], t, &g[sp]));
            const bool aOk = aimS[h] && XR_SUCCEEDED(xrLocateSpace(aimS[h], s.space[sp], t, &a[sp]));
            s.grip[h][sp].Add(gOk, gOk ? g[sp].locationFlags : 0);
            s.aim[h][sp].Add(aOk, aOk ? a[sp].locationFlags : 0);
            if (!gOk) g[sp].locationFlags = 0;
            if (!aOk) a[sp].locationFlags = 0;
        }
        if (Whole(g[1].locationFlags) && Whole(a[1].locationFlags)) {
            const XrVector3f &p = g[1].pose.position, &q = a[1].pose.position;
            s.gripAimCm[h].Add(100.0f * std::sqrt((p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y) + (p.z - q.z) * (p.z - q.z)));
        }
        if (Whole(g[0].locationFlags)) s.gripBelowHead[h].Add(-100.0f * g[0].pose.position.y);   // VIEW: the grip from the head (head-relative, y up)
    }
}

std::string SeriesText(const Series& s, const char* unit, int decimals = 1) {
    if (!s.n) return "none";
    return Fmt("%.*f %s (%.*f .. %.*f)", decimals, s.Mean(), unit, decimals, s.lo, decimals, s.hi);
}

void ReportSamples(const Sampler& s, Facts& facts) {
    Head("Eyes: xrLocateViews over the sampled frames");
    L("frames sampled %d (with the focus %d); refresh %s", s.frames, s.focusedFrames, SeriesText(s.refreshHz, "Hz").c_str());
    for (int sp = 0; sp < Sampler::kSpaces; ++sp) {
        if (!s.space[sp]) continue;
        L("%s: view state %s", kSpaceNames[sp], s.views[sp].Text().c_str());
        if (!s.fov[sp][0].l.n) continue;
        const FovDeg fl = s.fov[sp][0].Mean(), fr = s.fov[sp][1].Mean();
        L("  %s", FovLine(s.gapMm[sp].Mean(), sp == 0 ? s.cantDeg.Mean() : 0.0f, fl, fr).c_str());
        L("  eye distance %s; each angle moved at most %.2f deg (left eye) / %.2f deg (right eye)", SeriesText(s.gapMm[sp], "mm", 2).c_str(),
          s.fov[sp][0].Spread(), s.fov[sp][1].Spread());
    }
    if (s.fov[0][0].l.n) {
        const FovDeg fl = s.fov[0][0].Mean(), fr = s.fov[0][1].Mean();
        for (int e = 0; e < 2; ++e) {
            const FovSeries& f = s.fov[0][e];
            L("VIEW %s eye: left %.2f..%.2f, right %.2f..%.2f, up %.2f..%.2f, down %.2f..%.2f deg; place from the head x %+.1f y %+.1f z %+.1f mm; "
              "turn from the head yaw %+.2f pitch %+.2f roll %+.2f deg",
              e ? "right" : "left", f.l.lo, f.l.hi, f.r.lo, f.r.hi, f.u.lo, f.u.hi, f.d.lo, f.d.hi, s.eyePos[e][0].Mean(), s.eyePos[e][1].Mean(),
              s.eyePos[e][2].Mean(), s.ypr[e][0].Mean(), s.ypr[e][1].Mean(), s.ypr[e][2].Mean());
        }
        const Symmetry sym = CheckSymmetry(fl, fr);
        L("Symmetry: outer left %.1f / outer right %.1f, inner left %.1f / inner right %.1f deg - %s; up/down %s; each eye %.1f x %.1f (left) and %.1f x %.1f (right) "
          "deg wide x tall; angle between the eyes' views %s",
          sym.outerL, sym.outerR, sym.innerL, sym.innerR, sym.mirrored ? "a mirror pair" : "NOT a mirror pair", sym.sameVertical ? "the same" : "DIFFERENT",
          sym.wideL, sym.tallL, sym.wideR, sym.tallR, SeriesText(s.cantDeg, "deg", 2).c_str());
        facts.gapMm = s.gapMm[0].Mean();
        facts.gapSpreadMm = s.gapMm[0].Spread();
        facts.cantDeg = s.cantDeg.Mean();
        facts.fov[0] = fl;
        facts.fov[1] = fr;
        facts.fovSpreadDeg = std::max(s.fov[0][0].Spread(), s.fov[0][1].Spread());
        for (int sp = 1; sp < Sampler::kSpaces; ++sp)
            for (int e = 0; e < 2 && s.fov[sp][e].l.n; ++e) {
                const FovDeg a = s.fov[sp][e].Mean(), b = s.fov[0][e].Mean();
                facts.fovSpaceDiffDeg = std::max({facts.fovSpaceDiffDeg, std::fabs(a.l - b.l), std::fabs(a.r - b.r), std::fabs(a.u - b.u), std::fabs(a.d - b.d)});
            }
    }

    Head("Spaces: the head (VIEW) located in each");
    for (int sp = 1; sp < Sampler::kSpaces; ++sp) {
        if (!s.space[sp]) continue;
        L("%s: %s; head height %s", kSpaceNames[sp], s.head[sp].Text().c_str(), SeriesText(s.headHeight[sp], "m", 3).c_str());
    }
    if (s.space[1] && s.space[2])
        L("LOCAL in STAGE: %s; LOCAL's origin %s above the floor, turned %s", s.localInStage.Text().c_str(), SeriesText(s.localInStageY, "m", 3).c_str(),
          SeriesText(s.localInStageYaw, "deg").c_str());
    facts.headLocal = s.head[1];
    facts.headStage = s.head[2];

    Head("Controllers: grip and aim poses in each space");
    for (int h = 0; h < 2; ++h) {
        L("%s hand, profile %s:", h ? "right" : "left", facts.profile[h].empty() ? "none" : facts.profile[h].c_str());
        for (int sp = 0; sp < Sampler::kSpaces; ++sp) {
            if (!s.space[sp]) continue;
            L("  grip in %-11s %s", kSpaceNames[sp], s.grip[h][sp].Text().c_str());
            L("  aim  in %-11s %s", kSpaceNames[sp], s.aim[h][sp].Text().c_str());
        }
        L("  grip to aim %s; grip below the head %s", SeriesText(s.gripAimCm[h], "cm").c_str(), SeriesText(s.gripBelowHead[h], "cm").c_str());
        facts.grip[h][0] = s.grip[h][1];
        facts.grip[h][1] = s.grip[h][2];
        facts.aim[h][0] = s.aim[h][1];
        facts.aim[h][1] = s.aim[h][2];
    }
    facts.handsLooked = s.focusedFrames > 0;
    facts.frames = s.frames;
}

std::string ProfileOf(XrSession session, XrPath hand) {
    XrInteractionProfileState ps{XR_TYPE_INTERACTION_PROFILE_STATE};
    if (XR_FAILED(xrGetCurrentInteractionProfile(session, hand, &ps)) || ps.interactionProfile == XR_NULL_PATH) return {};
    char b[XR_MAX_PATH_LENGTH];
    uint32_t n = 0;
    return XR_SUCCEEDED(xrPathToString(g_inst, ps.interactionProfile, sizeof b, &n, b)) ? std::string(b) : std::string();
}

XrPath Path(const char* s) {
    XrPath p = XR_NULL_PATH;
    xrStringToPath(g_inst, s, &p);
    return p;
}

// The grip and aim actions with the mod's own controller profiles (vr/xr.cpp MakeActions: the same profiles, the same pose paths).
void Suggest(const char* profile, XrAction grip, XrAction aim) {
    const XrActionSuggestedBinding b[4] = {{grip, Path("/user/hand/left/input/grip/pose")}, {grip, Path("/user/hand/right/input/grip/pose")},
                                           {aim, Path("/user/hand/left/input/aim/pose")}, {aim, Path("/user/hand/right/input/aim/pose")}};
    XrInteractionProfileSuggestedBinding sb{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    sb.interactionProfile = Path(profile);
    sb.countSuggestedBindings = 4;
    sb.suggestedBindings = b;
    const XrResult r = xrSuggestInteractionProfileBindings(g_inst, &sb);
    if (XR_FAILED(r)) L("  bindings for %s refused: %s", profile, Res(r));
}

void ClearEye(Gfx& g, ID3D12Resource* tex, DXGI_FORMAT fmt) {
    D3D12_RENDER_TARGET_VIEW_DESC rd{};
    rd.Format = fmt;
    rd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    const D3D12_CPU_DESCRIPTOR_HANDLE h = g.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    g.dev12->CreateRenderTargetView(tex, &rd, h);
    g.alloc->Reset();
    g.list->Reset(g.alloc, nullptr);
    const float grey[4] = {0.05f, 0.05f, 0.06f, 1.0f};
    g.list->ClearRenderTargetView(h, grey, 0, nullptr);
    g.list->Close();
    ID3D12CommandList* lists[] = {g.list};
    g.queue->ExecuteCommandLists(1, lists);
    g.queue->Signal(g.fence, ++g.fenceValue);
    if (g.fence->GetCompletedValue() < g.fenceValue) {
        g.fence->SetEventOnCompletion(g.fenceValue, g.fenceEvent);
        WaitForSingleObject(g.fenceEvent, 1000);
    }
}

void RunSession(XrSystemId sys, const Offered& offered, const LUID& luid, bool use12, D3D_FEATURE_LEVEL minLevel, int seconds, Facts& facts) {
    Head("Session");
    Gfx g;
    XrGraphicsBindingD3D12KHR b12{XR_TYPE_GRAPHICS_BINDING_D3D12_KHR};
    XrGraphicsBindingD3D11KHR b11{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
    IDXGIAdapter1* adapter = AdapterByLuid(luid);
    if (!adapter) { L("the runtime's GPU (LUID %08lX:%08lX) is not among this PC's adapters: no session", luid.HighPart, luid.LowPart); return; }
    if (use12) {
        if (FAILED(D3D12CreateDevice(adapter, minLevel, __uuidof(ID3D12Device), (void**)&g.dev12))) { adapter->Release(); L("D3D12 device: failed: no session"); return; }
        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        g.dev12->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&g.queue);
        g.dev12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&g.alloc);
        if (g.alloc) g.dev12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g.alloc, nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&g.list);
        if (g.list) g.list->Close();
        g.dev12->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&g.fence);
        g.fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        hd.NumDescriptors = 1;
        g.dev12->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void**)&g.rtvHeap);
        g.d3d12 = g.queue && g.alloc && g.list && g.fence && g.fenceEvent && g.rtvHeap;
        if (!g.queue) { adapter->Release(); g.Release(); L("D3D12 queue: failed: no session"); return; }
        b12.device = g.dev12;
        b12.queue = g.queue;
    } else {
        const D3D_FEATURE_LEVEL lv[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        if (FAILED(D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, lv, 2, D3D11_SDK_VERSION, &g.dev11, nullptr, &g.ctx11))) {
            adapter->Release();
            L("D3D11 device: failed: no session");
            return;
        }
        b11.device = g.dev11;
    }
    adapter->Release();
    L("graphics: %s on the runtime's GPU", use12 ? "D3D12 (as the game)" : "D3D11 (the runtime has no D3D12: the game gets no session there)");

    XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO};
    sci.next = use12 ? (const void*)&b12 : (const void*)&b11;
    sci.systemId = sys;
    XrSession session = XR_NULL_HANDLE;
    const XrResult cr = xrCreateSession(g_inst, &sci, &session);
    if (XR_FAILED(cr)) { L("xrCreateSession failed: %s", Res(cr)); g.Release(); return; }

    // the reference spaces
    uint32_t ns = 0;
    xrEnumerateReferenceSpaces(session, 0, &ns, nullptr);
    std::vector<XrReferenceSpaceType> types(ns);
    if (ns) xrEnumerateReferenceSpaces(session, ns, &ns, types.data());
    std::string list;
    for (XrReferenceSpaceType t : types) list += std::string(list.empty() ? "" : ", ") + SpaceName(t);
    L("reference spaces offered: %s", list.empty() ? "none" : list.c_str());
    Sampler s;
    const XrReferenceSpaceType want[Sampler::kSpaces] = {XR_REFERENCE_SPACE_TYPE_VIEW, XR_REFERENCE_SPACE_TYPE_LOCAL, XR_REFERENCE_SPACE_TYPE_STAGE,
                                                        XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR_EXT};
    for (int sp = 0; sp < Sampler::kSpaces; ++sp) {
        if (std::find(types.begin(), types.end(), want[sp]) == types.end()) continue;
        XrReferenceSpaceCreateInfo ci{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        ci.referenceSpaceType = want[sp];
        ci.poseInReferenceSpace.orientation.w = 1.0f;
        const XrResult r = xrCreateReferenceSpace(session, &ci, &s.space[sp]);
        if (XR_FAILED(r)) { s.space[sp] = XR_NULL_HANDLE; L("  %s could not be made: %s", kSpaceNames[sp], Res(r)); }
    }
    facts.stageListed = s.space[2] != XR_NULL_HANDLE;
    for (XrReferenceSpaceType t : {XR_REFERENCE_SPACE_TYPE_STAGE, XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR_EXT}) {
        if (std::find(types.begin(), types.end(), t) == types.end()) continue;
        XrExtent2Df ext{};
        const XrResult r = xrGetReferenceSpaceBoundsRect(session, t, &ext);
        if (r == XR_SPACE_BOUNDS_UNAVAILABLE) L("%s bounds: unavailable (no play area set up, or the runtime does not say)", SpaceName(t));
        else if (XR_SUCCEEDED(r)) L("%s bounds: %.2f x %.2f m", SpaceName(t), ext.width, ext.height);
        else L("%s bounds: %s", SpaceName(t), Res(r));
    }

    // the swapchain formats; each eye's chain (D3D12) at the recommended size
    uint32_t nf = 0;
    xrEnumerateSwapchainFormats(session, 0, &nf, nullptr);
    std::vector<int64_t> formats(nf);
    if (nf) xrEnumerateSwapchainFormats(session, nf, &nf, formats.data());
    std::string fl;
    for (int64_t f : formats) {
        const char* n = FormatName(f);
        fl += (fl.empty() ? "" : ", ") + (n ? std::string(n) : std::to_string(f));
    }
    L("swapchain formats (the runtime's order of preference): %s", fl.empty() ? "none" : fl.c_str());
    Chain chain[2];
    DXGI_FORMAT chainFmt = DXGI_FORMAT_UNKNOWN;
    for (int64_t f : formats)
        if (f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || f == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || f == DXGI_FORMAT_R8G8B8A8_UNORM || f == DXGI_FORMAT_B8G8R8A8_UNORM) {
            chainFmt = (DXGI_FORMAT)f;
            break;
        }
    bool chainsOk = g.d3d12 && chainFmt != DXGI_FORMAT_UNKNOWN;
    for (int e = 0; e < 2 && chainsOk; ++e) {
        XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
        ci.format = chainFmt;
        ci.sampleCount = 1;
        ci.width = facts.recW[e] ? facts.recW[e] : 1024;
        ci.height = facts.recH[e] ? facts.recH[e] : 1024;
        ci.faceCount = 1;
        ci.arraySize = 1;
        ci.mipCount = 1;
        const XrResult r = xrCreateSwapchain(session, &ci, &chain[e].sc);
        if (XR_FAILED(r)) { L("swapchain %ux%u: %s - frames go without a picture", ci.width, ci.height, Res(r)); chainsOk = false; break; }
        chain[e].w = (int32_t)ci.width;
        chain[e].h = (int32_t)ci.height;
        uint32_t ni = 0;
        xrEnumerateSwapchainImages(chain[e].sc, 0, &ni, nullptr);
        std::vector<XrSwapchainImageD3D12KHR> imgs(ni, {XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
        xrEnumerateSwapchainImages(chain[e].sc, ni, &ni, (XrSwapchainImageBaseHeader*)imgs.data());
        for (const auto& im : imgs) chain[e].images.push_back(im.texture);
        if (e == 0) L("swapchains: %ux%u per eye, %s, %u images each", ci.width, ci.height, FormatName(chainFmt), ni);
    }

    // the grip and aim, with the mod's profiles
    XrActionSet set = XR_NULL_HANDLE;
    XrAction gripA = XR_NULL_HANDLE, aimA = XR_NULL_HANDLE;
    XrPath hands[2] = {Path("/user/hand/left"), Path("/user/hand/right")};
    XrSpace gripS[2] = {}, aimS[2] = {};
    {
        XrActionSetCreateInfo si{XR_TYPE_ACTION_SET_CREATE_INFO};
        strcpy_s(si.actionSetName, "d2rvr");
        strcpy_s(si.localizedActionSetName, "D2R VR");
        if (XR_SUCCEEDED(xrCreateActionSet(g_inst, &si, &set))) {
            for (int i = 0; i < 2; ++i) {
                XrActionCreateInfo ci{XR_TYPE_ACTION_CREATE_INFO};
                strcpy_s(ci.actionName, i ? "aim" : "grip");
                strcpy_s(ci.localizedActionName, i ? "Pointer" : "Hand");
                ci.actionType = XR_ACTION_TYPE_POSE_INPUT;
                ci.countSubactionPaths = 2;
                ci.subactionPaths = hands;
                xrCreateAction(set, &ci, i ? &aimA : &gripA);
            }
            for (const char* p : {"/interaction_profiles/oculus/touch_controller", "/interaction_profiles/valve/index_controller",
                                  "/interaction_profiles/htc/vive_controller", "/interaction_profiles/microsoft/motion_controller",
                                  "/interaction_profiles/khr/simple_controller"})
                Suggest(p, gripA, aimA);
            if (offered.Has(xrprofiles::kFrameExtension)) Suggest(xrprofiles::kFrameProfile, gripA, aimA);
            XrSessionActionSetsAttachInfo ai{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
            ai.countActionSets = 1;
            ai.actionSets = &set;
            const XrResult r = xrAttachSessionActionSets(session, &ai);
            if (XR_FAILED(r)) L("actions not attached: %s", Res(r));
            for (int h = 0; h < 2; ++h) {
                XrActionSpaceCreateInfo asi{XR_TYPE_ACTION_SPACE_CREATE_INFO};
                asi.subactionPath = hands[h];
                asi.poseInActionSpace.orientation.w = 1.0f;
                asi.action = gripA;
                xrCreateActionSpace(session, &asi, &gripS[h]);
                asi.action = aimA;
                xrCreateActionSpace(session, &asi, &aimS[h]);
            }
        }
    }

    // [XR_FB_display_refresh_rate]
    PFN_xrEnumerateDisplayRefreshRatesFB enumRates = nullptr;
    PFN_xrGetDisplayRefreshRateFB getRate = nullptr;
    if (offered.Has(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME)) {
        xrGetInstanceProcAddr(g_inst, "xrEnumerateDisplayRefreshRatesFB", (PFN_xrVoidFunction*)&enumRates);
        xrGetInstanceProcAddr(g_inst, "xrGetDisplayRefreshRateFB", (PFN_xrVoidFunction*)&getRate);
    }
    if (enumRates && getRate) {
        uint32_t nr = 0;
        enumRates(session, 0, &nr, nullptr);
        std::vector<float> rates(nr);
        if (nr) enumRates(session, nr, &nr, rates.data());
        float now = 0.0f;
        getRate(session, &now);
        std::string rl;
        for (float r : rates) rl += Fmt("%s%.1f", rl.empty() ? "" : ", ", r);
        L("display refresh rates: %s Hz; now %.1f Hz (XR_FB_display_refresh_rate)", rl.empty() ? "none" : rl.c_str(), now);
    }

    // the frames: the session started when READY, sampled once focused (or after 10 s without the focus), then ended
    Say("Session started - keep the headset on, look ahead, hold both controllers in front of you...");
    std::vector<std::string> events;
    const auto t0 = std::chrono::steady_clock::now();
    auto secs = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
    XrSessionState state = XR_SESSION_STATE_UNKNOWN;
    bool running = false, exitAsked = false, done = false;
    double sampleFrom = -1.0, beganAt = -1.0;
    int lastSaid = -1;
    while (!done && secs() < 90.0) {
        XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
        while (xrPollEvent(g_inst, &ev) == XR_SUCCESS) {
            if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                state = reinterpret_cast<XrEventDataSessionStateChanged&>(ev).state;
                events.push_back(Fmt("%.2f s %s", secs(), StateName(state)));
                if (state == XR_SESSION_STATE_READY && !running) {
                    XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};
                    bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    const XrResult r = xrBeginSession(session, &bi);
                    if (XR_SUCCEEDED(r)) { running = true; beganAt = secs(); }
                    else { events.push_back(Fmt("xrBeginSession: %s", Res(r))); done = true; }
                } else if (state == XR_SESSION_STATE_STOPPING && running) {
                    xrEndSession(session);
                    running = false;
                    done = exitAsked;
                } else if (state == XR_SESSION_STATE_EXITING || state == XR_SESSION_STATE_LOSS_PENDING) {
                    done = true;
                }
            } else if (ev.type == XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED) {
                for (int h = 0; h < 2; ++h) facts.profile[h] = ProfileOf(session, hands[h]);
                events.push_back(Fmt("%.2f s interaction profile: left %s, right %s", secs(), facts.profile[0].empty() ? "none" : facts.profile[0].c_str(),
                                     facts.profile[1].empty() ? "none" : facts.profile[1].c_str()));
            } else if (ev.type == XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) {
                const auto& rc = reinterpret_cast<XrEventDataReferenceSpaceChangePending&>(ev);
                events.push_back(Fmt("%.2f s %s space change pending (recentred?)", secs(), SpaceName(rc.referenceSpaceType)));
            } else if (ev.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
                events.push_back(Fmt("%.2f s instance loss pending", secs()));
                done = true;
            }
            ev = {XR_TYPE_EVENT_DATA_BUFFER};
        }
        if (done) break;
        if (!running) {
            if (beganAt < 0.0 && secs() > 20.0) { events.push_back("no READY in 20 s: the runtime never let the session start"); break; }
            if (beganAt >= 0.0 && secs() > beganAt + seconds + 30.0) { events.push_back("the runtime stopped the session and never gave it back"); break; }
            Sleep(10);
            continue;
        }
        XrFrameState fs{XR_TYPE_FRAME_STATE};
        if (XR_FAILED(xrWaitFrame(session, nullptr, &fs))) break;
        if (XR_FAILED(xrBeginFrame(session, nullptr))) break;
        const bool focused = state == XR_SESSION_STATE_FOCUSED;
        if (sampleFrom < 0.0 && !exitAsked && (focused || secs() - beganAt > 10.0)) {
            sampleFrom = secs();
            if (!focused) events.push_back(Fmt("%.2f s sampling without the focus (state %s)", secs(), StateName(state)));
        }
        const bool sampling = sampleFrom >= 0.0 && !exitAsked && fs.shouldRender;
        if (sampling) {
            if (fs.predictedDisplayPeriod > 0) s.refreshHz.Add((float)(1e9 / (double)fs.predictedDisplayPeriod));
            SampleFrame(s, session, fs.predictedDisplayTime, focused, set, gripS, aimS);
            const int left = (int)std::ceil(seconds - (secs() - sampleFrom));
            if (left != lastSaid && left >= 0) { lastSaid = left; Say("  sampling: %d s left", left); }
        }
        // the frame: each eye's chain cleared and submitted at its located view (LOCAL), or nothing
        XrCompositionLayerProjectionView pv[2] = {{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}, {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
        XrCompositionLayerProjection proj{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        const XrCompositionLayerBaseHeader* layers[1] = {};
        uint32_t nLayers = 0;
        if (chainsOk && fs.shouldRender && s.space[1]) {
            XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO};
            li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            li.displayTime = fs.predictedDisplayTime;
            li.space = s.space[1];
            XrViewState vs{XR_TYPE_VIEW_STATE};
            XrView v[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
            uint32_t n = 0;
            if (XR_SUCCEEDED(xrLocateViews(session, &li, &vs, 2, &n, v)) && n == 2 && (vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) {
                bool ok = true;
                for (int e = 0; e < 2 && ok; ++e) {
                    uint32_t idx = 0;
                    XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
                    if (XR_FAILED(xrAcquireSwapchainImage(chain[e].sc, &ai, &idx))) { ok = false; break; }
                    XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
                    wi.timeout = XR_INFINITE_DURATION;   // (a timed-out wait is a success code, and its image may not be released)
                    if (xrWaitSwapchainImage(chain[e].sc, &wi) == XR_SUCCESS && idx < chain[e].images.size()) ClearEye(g, chain[e].images[idx], chainFmt);
                    else ok = false;
                    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                    xrReleaseSwapchainImage(chain[e].sc, &ri);
                    pv[e].pose = v[e].pose;
                    pv[e].fov = v[e].fov;
                    pv[e].subImage.swapchain = chain[e].sc;
                    pv[e].subImage.imageRect = {{0, 0}, {chain[e].w, chain[e].h}};
                }
                if (ok) {
                    proj.space = s.space[1];
                    proj.viewCount = 2;
                    proj.views = pv;
                    layers[0] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&proj);
                    nLayers = 1;
                }
            }
        }
        XrFrameEndInfo fe{XR_TYPE_FRAME_END_INFO};
        fe.displayTime = fs.predictedDisplayTime;
        fe.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        fe.layerCount = nLayers;
        fe.layers = layers;
        if (XR_FAILED(xrEndFrame(session, &fe))) break;
        if (!exitAsked && sampleFrom >= 0.0 && secs() - sampleFrom >= seconds) {
            for (int h = 0; h < 2; ++h) facts.profile[h] = ProfileOf(session, hands[h]);
            exitAsked = true;
            xrRequestExitSession(session);
            Say("Sampling done - ending the session.");
        }
        if (exitAsked && secs() - sampleFrom > seconds + 5.0) break;   // a runtime that never says STOPPING
    }
    if (running) xrEndSession(session);
    L("session events:");
    for (const std::string& e : events) L("  %s", e.c_str());
    facts.session = true;
    ReportSamples(s, facts);

    for (int h = 0; h < 2; ++h) {
        if (gripS[h]) xrDestroySpace(gripS[h]);
        if (aimS[h]) xrDestroySpace(aimS[h]);
    }
    for (XrSpace sp : s.space) if (sp) xrDestroySpace(sp);
    for (Chain& c : chain) if (c.sc) xrDestroySwapchain(c.sc);
    if (set) xrDestroyActionSet(set);
    xrDestroySession(session);
    g.Release();
}

// --------------------------------------------------------------------------------------------------- the report

void Runtime(bool gameRunning, bool wantSession, int seconds, const std::wstring& ini, Facts& facts) {
    // the implicit layers as the game would load them: one the mod turns off for the game ([openxr] block_listed_layers on, runtime_fov off,
    // on blocked_layers) is turned off here too, in this process only
    {
        std::wstring list = Wide(apilayers::kDefaultList);
        if (!ini.empty()) {
            wchar_t b[512] = L"";
            GetPrivateProfileStringW(L"openxr", L"blocked_layers", list.c_str(), b, 512, ini.c_str());
            list = b;
        }
        const bool block = IniInt(ini, L"block_listed_layers", 1) != 0;
        const bool ownCrop = IniInt(ini, L"runtime_fov", 1) == 0;
        Head("Implicit layers, as the loader takes them for the game");
        const std::vector<apilayers::Found> found = apilayerslib::Apply(block, ownCrop, Utf8(list), nullptr);
        if (found.empty()) L("none");
        for (const apilayers::Found& f : found)
            L("  %s - %s%s%s", f.manifest.ok ? f.manifest.name.c_str() : "(manifest not readable)", f.path.c_str(),
              f.asleep ? " - asleep (its enable variable is not set)" : "",
              f.turnedOff ? " - TURNED OFF for the game ([openxr] block_listed_layers; here too)" : f.listed ? " - on blocked_layers, loads with the game" : "");
    }
    if (gameRunning) {   // the game holds the headset: no OpenXR call from here at all (a second instance can upset a runtime mid-game)
        facts.gameRunning = true;
        Head("OpenXR");
        L("not asked: the game (D2R.exe / D2RLoader.exe) is running and holds the headset - close it and run this again for the runtime, the eyes, "
          "the spaces and the controllers");
        return;
    }
    const Offered offered = LoaderLists();
    if (offered.ext.empty()) return;
    const bool has12 = offered.Has(XR_KHR_D3D12_ENABLE_EXTENSION_NAME), has11 = offered.Has(XR_KHR_D3D11_ENABLE_EXTENSION_NAME);
    std::vector<const char*> exts;
    if (has12) exts.push_back(XR_KHR_D3D12_ENABLE_EXTENSION_NAME);
    if (has11) exts.push_back(XR_KHR_D3D11_ENABLE_EXTENSION_NAME);
    for (const char* e : {XR_EXT_HAND_TRACKING_EXTENSION_NAME, XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME, XR_EXT_LOCAL_FLOOR_EXTENSION_NAME,
                          xrprofiles::kFrameExtension})
        if (offered.Has(e)) exts.push_back(e);

    Head("Instance and system");
    Say("Asking the OpenXR runtime (it may start SteamVR / Oculus first)...");
    XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
    strcpy_s(ci.applicationInfo.applicationName, "D2R VR");   // as the game's (vr/xr.cpp): a layer's per-game settings act the same
    ci.applicationInfo.applicationVersion = 1;
    strcpy_s(ci.applicationInfo.engineName, "D2RLoader");
    ci.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    ci.enabledExtensionCount = (uint32_t)exts.size();
    ci.enabledExtensionNames = exts.data();
    XrResult r = xrCreateInstance(&ci, &g_inst);
    if (XR_FAILED(r)) { L("xrCreateInstance failed: %d", (int)r); g_inst = XR_NULL_HANDLE; return; }
    std::string en;
    for (const char* e : exts) en += std::string(en.empty() ? "" : ", ") + e;
    L("instance made as \"D2R VR\" (engine \"D2RLoader\", as the game), extensions: %s", en.c_str());
    XrInstanceProperties ip{XR_TYPE_INSTANCE_PROPERTIES};
    if (XR_SUCCEEDED(xrGetInstanceProperties(g_inst, &ip))) {
        L("runtime %s %u.%u.%u", ip.runtimeName, XR_VERSION_MAJOR(ip.runtimeVersion), XR_VERSION_MINOR(ip.runtimeVersion), XR_VERSION_PATCH(ip.runtimeVersion));
        facts.metaRuntime = handtrackers::MetaRuntime(ip.runtimeName);
    }
    XrSystemGetInfo si{XR_TYPE_SYSTEM_GET_INFO};
    si.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId sys = XR_NULL_SYSTEM_ID;
    for (int i = 0; i < 40 && (r = xrGetSystem(g_inst, &si, &sys)) == XR_ERROR_FORM_FACTOR_UNAVAILABLE; ++i) {
        if (i == 0) Say("No headset yet - connect it / put it on (waiting up to 10 s)...");
        Sleep(250);
    }
    if (XR_FAILED(r)) {
        L("xrGetSystem: %s (no headset connected or awake)", Res(r));
        facts.noHeadset = r == XR_ERROR_FORM_FACTOR_UNAVAILABLE;
        return;
    }
    XrSystemProperties sp{XR_TYPE_SYSTEM_PROPERTIES};
    XrSystemHandTrackingPropertiesEXT hp{XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT};
    if (offered.Has(XR_EXT_HAND_TRACKING_EXTENSION_NAME)) sp.next = &hp;
    if (XR_SUCCEEDED(xrGetSystemProperties(g_inst, sys, &sp))) {
        L("headset \"%s\", vendor id 0x%X, system id %llu", sp.systemName, sp.vendorId, (unsigned long long)sp.systemId);
        L("  layers at most %u, swapchains up to %ux%u; tracking: orientation %s, position %s; hand tracking %s", sp.graphicsProperties.maxLayerCount,
          sp.graphicsProperties.maxSwapchainImageWidth, sp.graphicsProperties.maxSwapchainImageHeight, sp.trackingProperties.orientationTracking ? "yes" : "no",
          sp.trackingProperties.positionTracking ? "yes" : "no",
          !offered.Has(XR_EXT_HAND_TRACKING_EXTENSION_NAME) ? "not in this runtime" : hp.supportsHandTracking ? "yes" : "not on this headset");
    }

    Head("View configurations");
    uint32_t nc = 0;
    xrEnumerateViewConfigurations(g_inst, sys, 0, &nc, nullptr);
    std::vector<XrViewConfigurationType> cfgs(nc);
    if (nc) xrEnumerateViewConfigurations(g_inst, sys, nc, &nc, cfgs.data());
    for (XrViewConfigurationType t : cfgs) {
        XrViewConfigurationProperties vp{XR_TYPE_VIEW_CONFIGURATION_PROPERTIES};
        xrGetViewConfigurationProperties(g_inst, sys, t, &vp);
        L("%s (fov mutable: %s)", FormFactorName(t), vp.fovMutable ? "yes" : "no");
        uint32_t nv = 0;
        xrEnumerateViewConfigurationViews(g_inst, sys, t, 0, &nv, nullptr);
        std::vector<XrViewConfigurationView> vv(nv, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
        if (nv) xrEnumerateViewConfigurationViews(g_inst, sys, t, nv, &nv, vv.data());
        for (uint32_t i = 0; i < nv; ++i) {
            L("  view %u: recommended %ux%u (max %ux%u), samples %u (max %u)", i, vv[i].recommendedImageRectWidth, vv[i].recommendedImageRectHeight,
              vv[i].maxImageRectWidth, vv[i].maxImageRectHeight, vv[i].recommendedSwapchainSampleCount, vv[i].maxSwapchainSampleCount);
            if (t == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO && i < 2) { facts.recW[i] = vv[i].recommendedImageRectWidth; facts.recH[i] = vv[i].recommendedImageRectHeight; }
        }
        uint32_t nb = 0;
        xrEnumerateEnvironmentBlendModes(g_inst, sys, t, 0, &nb, nullptr);
        std::vector<XrEnvironmentBlendMode> bm(nb);
        if (nb) xrEnumerateEnvironmentBlendModes(g_inst, sys, t, nb, &nb, bm.data());
        std::string b;
        for (XrEnvironmentBlendMode m : bm) b += std::string(b.empty() ? "" : ", ") + BlendName(m);
        L("  blend modes: %s", b.empty() ? "none" : b.c_str());
    }

    Head("Graphics: the GPU the runtime wants");
    LUID luid12{}, luid11{};
    bool got12 = false, got11 = false;
    D3D_FEATURE_LEVEL level12 = D3D_FEATURE_LEVEL_11_0;
    if (has12) {
        PFN_xrGetD3D12GraphicsRequirementsKHR f = nullptr;
        XrGraphicsRequirementsD3D12KHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D12_KHR};
        if (XR_SUCCEEDED(xrGetInstanceProcAddr(g_inst, "xrGetD3D12GraphicsRequirementsKHR", (PFN_xrVoidFunction*)&f)) && f && XR_SUCCEEDED(r = f(g_inst, sys, &req))) {
            luid12 = req.adapterLuid;
            level12 = req.minFeatureLevel;
            got12 = true;
            L("D3D12: LUID %08lX:%08lX, feature level %X at least", luid12.HighPart, luid12.LowPart, (unsigned)req.minFeatureLevel);
        } else L("D3D12 requirements: %s", Res(r));
    } else L("D3D12: not offered by this runtime (the game's native OpenXR needs it)");
    if (has11) {
        PFN_xrGetD3D11GraphicsRequirementsKHR f = nullptr;
        XrGraphicsRequirementsD3D11KHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
        if (XR_SUCCEEDED(xrGetInstanceProcAddr(g_inst, "xrGetD3D11GraphicsRequirementsKHR", (PFN_xrVoidFunction*)&f)) && f && XR_SUCCEEDED(r = f(g_inst, sys, &req))) {
            luid11 = req.adapterLuid;
            got11 = true;
            L("D3D11: LUID %08lX:%08lX, feature level %X at least", luid11.HighPart, luid11.LowPart, (unsigned)req.minFeatureLevel);
        } else L("D3D11 requirements: %s", Res(r));
    }
    Adapters(got12 ? &luid12 : nullptr, got11 ? &luid11 : nullptr);

    if (!wantSession) {
        Head("Session");
        L("not started (--no-session)");
        return;
    }
    if (!got12 && !got11) { Head("Session"); L("no graphics API to start one with"); return; }
    RunSession(sys, offered, got12 ? luid12 : luid11, got12, level12, seconds, facts);
}

// A user's own folder in a path written as %USERPROFILE%: the report goes to a public channel.
void HideUserFolder(std::string& s) {
    const std::string home = Env("USERPROFILE");
    if (home.size() < 4) return;
    auto lower = [](std::string t) { for (char& c : t) c = (char)tolower((unsigned char)c); return t; };
    const std::string ls = lower(s), lh = lower(home);
    std::string out;
    size_t from = 0;
    for (size_t at = ls.find(lh); at != std::string::npos; at = ls.find(lh, from)) {
        out += s.substr(from, at - from) + "%USERPROFILE%";
        from = at + home.size();
    }
    out += s.substr(from);
    s.swap(out);
}

std::wstring DefaultOut() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t name[80];
    swprintf_s(name, L"D2R_VR_headset_report_%04d-%02d-%02d_%02d-%02d-%02d.txt", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    wchar_t* desk = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desk)) && desk) dir = desk;
    CoTaskMemFree(desk);
    if (dir.empty()) {
        wchar_t t[MAX_PATH + 1] = {};
        if (GetTempPathW(MAX_PATH, t)) dir = t;
    }
    if (!dir.empty() && dir.back() != L'\\') dir += L'\\';
    return dir + name;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    std::wstring out;
    int seconds = 10;
    bool noSession = false, noWait = false;
    for (int i = 1; i < argc; ++i) {
        if (!wcscmp(argv[i], L"--out") && i + 1 < argc) out = argv[++i];
        else if (!wcscmp(argv[i], L"--seconds") && i + 1 < argc) seconds = std::clamp(_wtoi(argv[++i]), 2, 60);
        else if (!wcscmp(argv[i], L"--no-session")) noSession = true;
        else if (!wcscmp(argv[i], L"--no-wait")) noWait = true;
        else if (!wcscmp(argv[i], L"--game") && i + 1 < argc) { g_game = argv[++i]; while (!g_game.empty() && (g_game.back() == L'\\' || g_game.back() == L'/')) g_game.pop_back(); }
    }
    g_quiet = noWait;
    const bool explicitOut = !out.empty();
    if (out.empty()) out = DefaultOut();
    Say("D2R VR Headset Report %s - put the headset on and keep the controllers on. This takes about 20 seconds; nothing on the PC is changed.", D2RVR_VERSION);

    SYSTEMTIME st;
    GetLocalTime(&st);
    L("D2R VR Headset Report %s - %04d-%02d-%02d %02d:%02d:%02d", D2RVR_VERSION, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    L("Post this file in #support on our Discord. It lists D2R VR's own files and what the PC's OpenXR runtime gives (the eyes' angles, the spaces, the controllers); nothing was changed.");
    L("Angles are OpenXR's in degrees: left and down negative. P/p position valid/tracked, R/r orientation valid/tracked.");
    WindowsLine();
    const bool gameRunning = GameRunning();
    L("the game (D2R.exe / D2RLoader.exe) running: %s; SteamVR (vrserver.exe) running: %s; Oculus (OVRServer_x64.exe) running: %s", gameRunning ? "yes" : "no",
      ProcessRunning(L"vrserver.exe") ? "yes" : "no", ProcessRunning(L"OVRServer_x64.exe") ? "yes" : "no");
    const std::wstring ini = ModIni();
    const modfiles::ModFacts modFacts = ModFiles(ini);
    RuntimeRegistry();
    ModSettings(ini);
    Facts facts;
    Runtime(gameRunning, !noSession, seconds, ini, facts);
    if (g_inst) xrDestroyInstance(g_inst);
    g_inst = XR_NULL_HANDLE;

    Head("What stands out");
    std::vector<std::string> hints = modfiles::Hints(modFacts);   // D2R VR's files first, then the headset's
    for (const std::string& h : Hints(facts)) hints.push_back(h);
    for (const std::string& h : hints) L("- %s", h.c_str());
    HideUserFolder(g_out);

    bool written = false;
    if (FILE* f = _wfopen(out.c_str(), L"wb")) {
        written = fwrite(g_out.data(), 1, g_out.size(), f) == g_out.size();
        fclose(f);
    }
    if (!written) {
        Say("Could not write the report to %s", Utf8(out).c_str());
        std::fwrite(g_out.data(), 1, g_out.size(), stdout);
    } else {
        Say("");
        for (const std::string& h : hints) Say("- %s", h.c_str());
        Say("");
        Say("Report saved: %s", Utf8(out).c_str());
        Say("Post it in #support on our Discord.");
        if (!explicitOut && !noWait) {
            const std::wstring sel = L"/select,\"" + out + L"\"";
            ShellExecuteW(nullptr, L"open", L"explorer.exe", sel.c_str(), nullptr, SW_SHOWNORMAL);
        }
    }
    if (!noWait) {
        DWORD procs[2];
        if (GetConsoleProcessList(procs, 2) <= 1) {   // our own console (started from Explorer): kept open until read
            Say("Press Enter to close.");
            (void)getchar();
        }
    }
    return written ? 0 : 1;
}
