// fake_loader.exe - stands in for D2RLoader.exe in tools/start_helper_test.py (D2R_VR_Settings.exe --start).
// It starts nothing: it writes what it was started with to fake_loader_started.txt beside itself - its arguments (D2R VR Authentic is
// started as `D2RLoader.exe -mod D2R-VR`) and folder, and the environment the shared VR mod launcher gives the game (XR_RUNTIME_JSON,
// VRMOD_*, the Steam variables) - and exits.
#include <windows.h>

#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    char exe[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
    char dir[MAX_PATH] = {};
    for (DWORD i = n; i > 0; --i)
        if (exe[i - 1] == '\\') {
            memcpy(dir, exe, i);
            break;
        }
    char path[MAX_PATH + 40];
    snprintf(path, sizeof path, "%sfake_loader_started.txt", dir);
    FILE* f = nullptr;
    if (fopen_s(&f, path, "wb") != 0 || !f) return 2;
    char cwd[MAX_PATH] = {};
    GetCurrentDirectoryA(MAX_PATH, cwd);
    fprintf(f, "args=%d\ncwd=%s\n", argc - 1, cwd);
    fprintf(f, "argline=");
    for (int i = 1; i < argc; ++i) fprintf(f, "%s%s", i > 1 ? " " : "", argv[i]);
    fprintf(f, "\n");
    static const char* const names[] = {"XR_RUNTIME_JSON", "VRMOD_ACTIVE_ID", "VRMOD_ACTIVE_VERSION", "VRMOD_RUNTIME", "VRMOD_STORE", "VRMOD_GAME_BUILD",
                                         "SteamAppId", "SteamGameId", "SteamOverlayGameId"};
    for (const char* name : names) {
        char v[1024] = {};
        const DWORD len = GetEnvironmentVariableA(name, v, sizeof v);
        fprintf(f, "%s=%s\n", name, len > 0 && len < sizeof v ? v : "");
    }
    fclose(f);
    return 0;
}
