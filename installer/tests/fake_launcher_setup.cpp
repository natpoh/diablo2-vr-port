// fake_launcher_setup.exe - stands in for the carried D2R_VR_Launcher_Setup_<version>.exe in installer/tests/mod_layout_test.py.
// It installs nothing real. It writes what it was started with to launcher_standin_record.txt beside itself (the command line exactly as given,
// the folder, whether it runs elevated, as whom) and, when launcher_standin.cfg beside it names a registry key, does what the real launcher's
// Setup does for the mods' installers: HKCU\<key> Version and InstallDir, and a D2RVRLauncher.exe in that folder (a one-byte file).
// FAKE_EXIT (a compile definition) is its exit code: 0 for fake_launcher_setup, 5 for fake_launcher_setup_fail.
//   launcher_standin.cfg:   key=Software\D2R VR Launcher Test 4711
//                           version=0.2.0
//                           dir=C:\scratch\launcher
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

#ifndef FAKE_EXIT
#define FAKE_EXIT 0
#endif

static std::string FolderOfExe() {
    char exe[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
    std::string s(exe, n);
    const size_t slash = s.find_last_of('\\');
    return slash == std::string::npos ? std::string() : s.substr(0, slash + 1);
}

static bool Elevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION e{};
    DWORD got = 0;
    const bool ok = GetTokenInformation(token, TokenElevation, &e, sizeof e, &got) && e.TokenIsElevated;
    CloseHandle(token);
    return ok;
}

static void MakeFolders(const std::string& dir) {
    for (size_t i = 3; i < dir.size(); ++i)
        if (dir[i] == '\\') CreateDirectoryA(dir.substr(0, i).c_str(), nullptr);
    CreateDirectoryA(dir.c_str(), nullptr);
}

int main(int argc, char**) {
    const std::string folder = FolderOfExe();
    FILE* f = nullptr;
    if (fopen_s(&f, (folder + "launcher_standin_record.txt").c_str(), "wb") == 0 && f) {
        char cwd[MAX_PATH] = {}, user[256] = {};
        GetCurrentDirectoryA(MAX_PATH, cwd);
        DWORD un = sizeof user;
        GetUserNameA(user, &un);
        fprintf(f, "commandline=%s\r\nargc=%d\r\ncwd=%s\r\nelevated=%d\r\nuser=%s\r\nexit=%d\r\n", GetCommandLineA(), argc - 1, cwd, Elevated() ? 1 : 0, user, FAKE_EXIT);
        fclose(f);
    }
    // the real Setup's effect on the registry and the folder, when the test asks for it
    std::string key, version, dir;
    if (fopen_s(&f, (folder + "launcher_standin.cfg").c_str(), "rb") == 0 && f) {
        char line[1024];
        while (fgets(line, sizeof line, f)) {
            std::string l(line);
            while (!l.empty() && (l.back() == '\r' || l.back() == '\n')) l.pop_back();
            if (l.rfind("key=", 0) == 0) key = l.substr(4);
            else if (l.rfind("version=", 0) == 0) version = l.substr(8);
            else if (l.rfind("dir=", 0) == 0) dir = l.substr(4);
        }
        fclose(f);
    }
    if (!key.empty() && !version.empty() && !dir.empty()) {
        MakeFolders(dir);
        FILE* e = nullptr;
        if (fopen_s(&e, (dir + "\\D2RVRLauncher.exe").c_str(), "wb") == 0 && e) { fputc('M', e); fclose(e); }
        HKEY h = nullptr;
        if (RegCreateKeyExA(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &h, nullptr) == ERROR_SUCCESS) {
            RegSetValueExA(h, "Version", 0, REG_SZ, reinterpret_cast<const BYTE*>(version.c_str()), static_cast<DWORD>(version.size() + 1));
            RegSetValueExA(h, "InstallDir", 0, REG_SZ, reinterpret_cast<const BYTE*>(dir.c_str()), static_cast<DWORD>(dir.size() + 1));
            RegCloseKey(h);
        }
    }
    return FAKE_EXIT;
}
