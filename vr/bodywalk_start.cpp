// bodywalk_start.cpp - BodyWalkVR.exe found and started from inside the game (vr/bodywalk_start.h).
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>

#include <vector>

#include "bodywalk_start.h"
#include "mod_layout.h"

namespace bwstart {
namespace {

bool Exists(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

}  // namespace

std::wstring FindExe(const std::wstring& pluginDir) {
    // the copy D2R VR Setup puts in: the mod's BodyWalkVR, or (the old layout) the game's
    const std::wstring mod = modlayout::ModRootOfPluginDir(pluginDir);
    if (!mod.empty() && Exists(mod + L"BodyWalkVR\\BodyWalkVR.exe")) return mod + L"BodyWalkVR\\BodyWalkVR.exe";
    const std::wstring game = modlayout::GameOfPluginDir(pluginDir);
    if (!game.empty() && Exists(game + L"BodyWalkVR\\BodyWalkVR.exe")) return game + L"BodyWalkVR\\BodyWalkVR.exe";
    // an installed BodyWalk, then the Steam one (the uninstall entries D2R VR Setup and Settings look for)
    for (const wchar_t* key : {L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{5E9E3C51-4043-4245-8B24-817C647DE553}_is1",
                               L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\BodyWalkVR_is1",
                               L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App 4711120"})
        for (HKEY root : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER})
            for (DWORD view : {(DWORD)RRF_SUBKEY_WOW6464KEY, (DWORD)RRF_SUBKEY_WOW6432KEY}) {
                wchar_t dir[MAX_PATH] = {};
                DWORD size = sizeof dir;
                if (RegGetValueW(root, key, L"InstallLocation", RRF_RT_REG_SZ | view, nullptr, dir, &size) != ERROR_SUCCESS || !dir[0]) continue;
                std::wstring exe(dir);
                if (exe.back() != L'\\') exe += L'\\';
                exe += L"BodyWalkVR.exe";
                if (Exists(exe)) return exe;
            }
    return std::wstring();
}

int Running() {
    int n = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe{sizeof pe};
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
        if (_wcsicmp(pe.szExeFile, L"BodyWalkVR.exe") == 0) ++n;
    CloseHandle(snap);
    return n;
}

Result Start(const std::wstring& exe) {
    if (Running() > 0) return Result::AlreadyRunning;
    if (exe.empty() || !Exists(exe)) return Result::NotFound;
    const std::wstring dir = exe.substr(0, exe.find_last_of(L'\\'));
    wchar_t* env = GetEnvironmentStringsW();
    std::wstring block = WithoutSteamIds(env);
    if (env) FreeEnvironmentStringsW(env);
    std::wstring cmd = L"\"" + exe + L"\"";
    STARTUPINFOW si{sizeof si};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOWMINNOACTIVE;   // (the game keeps the keyboard)
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_PROCESS_GROUP, block.data(), dir.c_str(), &si, &pi))
        return Result::Failed;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return Result::Started;
}

}  // namespace bwstart
