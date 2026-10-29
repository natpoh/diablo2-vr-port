// api_layers_lib.cpp - see api_layers_lib.h. The registry read as the Khronos loader reads it (src/loader/manifest_file.cpp, ReadLayerDataFilesInRegistry:
// a DWORD 0 value under ...\ApiLayers\Implicit names an enabled manifest, a folder's *.json all count; HKCU is skipped for a high-integrity process).

#include "api_layers_lib.h"

#include <windows.h>

#include <cstdio>
#include <set>

namespace apilayerslib {

namespace {

constexpr wchar_t kKey[] = L"SOFTWARE\\Khronos\\OpenXR\\1\\ApiLayers\\Implicit";

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
bool EnvSet(const std::string& name) {
    const std::wstring w = Wide(name);
    return !w.empty() && GetEnvironmentVariableW(w.c_str(), nullptr, 0) > 0;
}

bool HighIntegrity() {   // as the loader's IsHighIntegrityLevel
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    bool high = false;
    BYTE buf[sizeof(TOKEN_MANDATORY_LABEL) + SECURITY_MAX_SID_SIZE];
    DWORD len = 0;
    if (GetTokenInformation(token, TokenIntegrityLevel, buf, sizeof buf, &len)) {
        const auto* label = reinterpret_cast<const TOKEN_MANDATORY_LABEL*>(buf);
        const DWORD count = *GetSidSubAuthorityCount(label->Label.Sid);
        if (count) high = *GetSidSubAuthority(label->Label.Sid, count - 1) >= SECURITY_MANDATORY_HIGH_RID;
    }
    CloseHandle(token);
    return high;
}

std::string ReadFile(const std::wstring& path) {
    std::string text;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return text;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(h, &size) && size.QuadPart > 0 && size.QuadPart < (1 << 20)) {
        text.resize((size_t)size.QuadPart);
        DWORD got = 0;
        if (!::ReadFile(h, text.data(), (DWORD)text.size(), &got, nullptr)) got = 0;
        text.resize(got);
    }
    CloseHandle(h);
    return text;
}

void AddManifest(const std::wstring& path, std::vector<apilayers::Found>& out) {
    apilayers::Found f;
    f.path = Utf8(path);
    f.manifest = apilayers::Parse(ReadFile(path));
    f.asleep = f.manifest.ok && !f.manifest.enableEnv.empty() && !EnvSet(f.manifest.enableEnv);
    out.push_back(f);
}

void ReadHive(HKEY hive, std::vector<apilayers::Found>& out) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(hive, kKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return;
    for (DWORD i = 0;; ++i) {
        wchar_t name[1024];
        DWORD nameLen = 1023, type = 0, value = 1, valueLen = sizeof value;
        const LONG r = RegEnumValueW(key, i, name, &nameLen, nullptr, &type, reinterpret_cast<BYTE*>(&value), &valueLen);
        if (r == ERROR_NO_MORE_ITEMS) break;
        if (r != ERROR_SUCCESS || valueLen != sizeof value || value != 0) continue;
        const std::wstring path(name, nameLen);
        const DWORD attr = GetFileAttributesW(path.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES) continue;   // (the loader skips a missing one too)
        if (attr & FILE_ATTRIBUTE_DIRECTORY) {
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW((path + L"\\*.json").c_str(), &fd);
            if (h == INVALID_HANDLE_VALUE) continue;
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) AddManifest(path + L"\\" + fd.cFileName, out);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        } else {
            AddManifest(path, out);
        }
    }
    RegCloseKey(key);
}

std::set<std::string> g_ours;            // the variables an Apply set (cleared again when no longer wanted)
std::set<std::string> g_told;            // "name|verdict" logged already (the log once per change, not each session)

}  // namespace

std::vector<apilayers::Found> Scan() {
    std::vector<apilayers::Found> out;
    ReadHive(HKEY_LOCAL_MACHINE, out);
    if (!HighIntegrity()) ReadHive(HKEY_CURRENT_USER, out);
    return out;
}


std::vector<apilayers::Found> Apply(bool block, bool ownCrop, const std::string& list, LogFn log) {
    // what we set before is taken back first: a variable that was the player's own before ours is not ours (never put into g_ours)
    for (const std::string& v : g_ours) SetEnvironmentVariableW(Wide(v).c_str(), nullptr);
    std::set<std::string> was;
    was.swap(g_ours);
    std::vector<apilayers::Found> layers = Scan();
    apilayers::Decide(layers, block, ownCrop, list);
    for (apilayers::Found& f : layers) {
        if (!f.turnedOff) continue;
        if (EnvSet(f.manifest.disableEnv)) continue;   // switched off already (by the player, for this process or all)
        if (SetEnvironmentVariableW(Wide(f.manifest.disableEnv).c_str(), L"1")) g_ours.insert(f.manifest.disableEnv);
        else f.turnedOff = false;
    }
    if (!log) return layers;
    char b[1400];
    for (const apilayers::Found& f : layers) {
        if (!f.manifest.ok) {
            if (!g_told.insert("?|" + f.path).second) continue;
            std::snprintf(b, sizeof b, "vrcam: openxr - an implicit OpenXR layer's manifest could not be read: %s (the loader will refuse it too)", f.path.c_str());
            log(b);
            continue;
        }
        if (f.asleep) continue;
        const std::string key = f.manifest.name + (f.turnedOff ? "|off" : f.listed ? "|listed" : "|in") + (ownCrop ? "" : "|rt");
        if (!g_told.insert(key).second) continue;
        if (f.turnedOff)
            std::snprintf(b, sizeof b, "vrcam: openxr - OpenXR layer %s turned off for D2R VR: it is on [openxr] blocked_layers, known to distort the picture drawn our "
                          "own way; %s set in the game's process only, the registry and other apps untouched ([openxr] block_listed_layers=1; "
                          "[openxr] runtime_fov=1 draws as other games do and leaves it in) - %s",
                          f.manifest.name.c_str(), f.manifest.disableEnv.c_str(), f.path.c_str());
        else if (f.listed)
            std::snprintf(b, sizeof b, "vrcam: openxr - OpenXR layer %s (on [openxr] blocked_layers) loads with the game: %s - %s", f.manifest.name.c_str(),
                          !ownCrop ? "the eyes are drawn as the runtime gives them ([openxr] runtime_fov=1), it should do no harm"
                          : !block ? "[openxr] block_listed_layers=0 - if the picture is cross-eyed or stretched, tick runtime_fov"
                                   : "its manifest names no variable to switch it off",
                          f.path.c_str());
        else
            std::snprintf(b, sizeof b, "vrcam: openxr - OpenXR layer %s loads with the game (another program's implicit layer, left as it is) - %s",
                          f.manifest.name.c_str(), f.path.c_str());
        log(b);
    }
    for (const std::string& v : was)
        if (!g_ours.count(v)) {
            std::snprintf(b, sizeof b, "vrcam: openxr - %s cleared again: that layer loads with the game from this session on", v.c_str());
            log(b);
        }
    return layers;
}

}  // namespace apilayerslib
