// eye_gaze_ask_test - [render] foveated + foveated_gaze (vr/eye_gaze_ask.h): a headset session asks the runtime for the eyes' gaze as the
// ini says when the SESSION starts, not when the plugin first read its settings (0.211.1, PS VR2: "eye gaze offered - not asked for" while
// foveated=1 - the file said 0 at the plugin's read and 1 by the session). The log names the keys read; switched on during a session that
// did not ask, one line says the gaze waits for the next session. Works on a scratch ini in %TEMP%. Run: eye_gaze_ask_test.exe
#include "eye_gaze_ask.h"

#include <cstdio>
#include <cstring>
#include <string>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAILED line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

static void Put(const std::wstring& ini, const wchar_t* key, const wchar_t* v) { WritePrivateProfileStringW(L"render", key, v, ini.c_str()); }

int main() {
    using namespace eyegazeask;
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring ini = std::wstring(tmp) + L"eye_gaze_ask_test_" + std::to_wstring(GetCurrentProcessId()) + L".ini";
    DeleteFileW(ini.c_str());

    // no file / no keys: the defaults - foveated off, gaze on - not asked
    Keys k = Read(ini.c_str());
    CHECK(k.foveated == 0 && k.gaze == 1 && !Wanted(k));
    CHECK(!Wanted(Read(nullptr)) && !Wanted(Read(L"")));

    // the plugin's first read (as at its load) - foveated still 0 ...
    Put(ini, L"foveated", L"0");
    Put(ini, L"foveated_gaze", L"1");
    const Keys atLoad = Read(ini.c_str());
    CHECK(!Wanted(atLoad));
    // ... the file says foveated=1 by the time the session starts (switched on, or written while the game started): the session asks
    Put(ini, L"foveated", L"1");
    const Keys atSession = Read(ini.c_str());
    CHECK(Wanted(atSession));   // (0.211.1 asked from atLoad's value: not asked)

    // foveated_gaze=0: never asked
    Put(ini, L"foveated_gaze", L"0");
    CHECK(!Wanted(Read(ini.c_str())));
    Put(ini, L"foveated_gaze", L"1");

    // the log's words
    char b[400];
    Describe(atSession, true, b, sizeof b);
    CHECK(std::strstr(b, "asked for") && std::strstr(b, "foveated=1") && !std::strstr(b, "NOT"));
    Describe(atLoad, true, b, sizeof b);
    CHECK(std::strstr(b, "NOT asked for") && std::strstr(b, "foveated=0, foveated_gaze=1") && std::strstr(b, "restart"));
    Describe(atSession, false, b, sizeof b);
    CHECK(std::strcmp(b, "not offered") == 0);

    // switched on during a session that did not ask: the line once; not when the session asked, or the runtime does not offer it
    bool told = false;
    CHECK(RestartNeeded(atSession, true, false, &told) && told);
    CHECK(!RestartNeeded(atSession, true, false, &told));          // once a session
    told = false;
    CHECK(!RestartNeeded(atSession, true, true, &told) && !told);  // this session asked
    CHECK(!RestartNeeded(atSession, false, false, &told));         // not offered: nothing to restart for
    CHECK(!RestartNeeded(atLoad, true, false, &told));             // still off: nothing to say

    DeleteFileW(ini.c_str());
    if (g_fail) { std::printf("%d check(s) FAILED\n", g_fail); return 1; }
    std::printf("eye_gaze_ask_test: all passed\n");
    return 0;
}
