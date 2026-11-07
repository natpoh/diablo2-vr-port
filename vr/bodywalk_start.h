#pragma once
// [openxr] gesture_overlay: the Gestures tab's "Start BodyWalk" - BodyWalkVR.exe found and started from inside the game, the way D2R VR Settings'
// START does (tools/settings/settings.cpp BodyWalkExe / StartBodyWalk): the copy D2R VR Setup put in mods\D2R-VR\BodyWalkVR (or, the old layout,
// <game>\BodyWalkVR), else an installed or Steam BodyWalk by its uninstall entry's InstallLocation. Never a second one: none is started while a
// BodyWalkVR.exe runs. The game's Steam ids (SteamAppId 2536520 for the Steam copy of the game) are taken out of the new process's environment -
// the Steam BodyWalk must not start as Diablo II. It starts minimised and not activated (the game keeps the keyboard).
//
// The environment's filter is pure (cleanroom/tests/gesture_overlay_test.cpp); the rest is Windows (vr/bodywalk_start.cpp).

#include <cstdint>
#include <cwchar>
#include <string>

namespace bwstart {

// A Windows environment block (NAME=value\0 ... \0\0) without the Steam ids a game passes on (SteamAppId, SteamGameId, SteamOverlayGameId),
// as a block again.
inline std::wstring WithoutSteamIds(const wchar_t* env) {
    std::wstring out;
    if (env)
        for (const wchar_t* p = env; *p; p += std::wcslen(p) + 1) {
            bool drop = false;
            for (const wchar_t* name : {L"SteamAppId=", L"SteamGameId=", L"SteamOverlayGameId="})
                if (_wcsnicmp(p, name, std::wcslen(name)) == 0) drop = true;
            if (!drop) out.append(p, std::wcslen(p) + 1);
        }
    out.push_back(L'\0');
    if (out.size() == 1) out.push_back(L'\0');   // (an empty block is two NULs)
    return out;
}

// BodyWalkVR.exe for the plugin in `pluginDir` (its d2r_vr.ini's folder): "" when none is found.
std::wstring FindExe(const std::wstring& pluginDir);
// How many BodyWalkVR.exe run now.
int Running();

enum class Result { Started, AlreadyRunning, NotFound, Failed };
// Starts `exe` (FindExe's) unless one runs already.
Result Start(const std::wstring& exe);

}  // namespace bwstart
