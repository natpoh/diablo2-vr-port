// [support] rtss_warning (shared/rtss_detect.h): RivaTuner's hook DLLs and programs found by name, the line's words, when a line is due.
//   rtss_detect_test   (exit code 0 = all passed)
#include "rtss_detect.h"

#include <cstdio>
#include <cstring>

using namespace rtss;

static int g_failed = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAILED line %d: %s\n", __LINE__, #c); ++g_failed; } } while (0)

int main() {
    // the hook DLLs: base names and full paths, any case; others not
    CHECK(HookBit(L"RTSSHooks64.dll") == kHooks64);
    CHECK(HookBit(L"rtsshooks64.DLL") == kHooks64);
    CHECK(HookBit(L"C:\\Program Files (x86)\\RivaTuner Statistics Server\\RTSSHooks64.dll") == kHooks64);
    CHECK(HookBit(L"RTSSHooks.dll") == kHooks32);
    CHECK(HookBit(L"RTSSHooks64.dll.bak") == 0);
    CHECK(HookBit(L"XRTSSHooks64.dll") == 0);
    CHECK(HookBit(L"windhawk.dll") == 0);
    CHECK(HookBit(L"") == 0);
    CHECK(HookBit(nullptr) == 0);
    CHECK(HookBitA("RTSSHooks64.dll") == kHooks64);   // the crash report's module list (char)
    CHECK(HookBitA("D2RLoader.exe") == 0);
    CHECK(HookBitA(nullptr) == 0);

    // the programs
    CHECK(ProgramBit(L"RTSS.exe") == kRtssExe);
    CHECK(ProgramBit(L"rtss.EXE") == kRtssExe);
    CHECK(ProgramBit(L"MSIAfterburner.exe") == kAfterburnerExe);
    CHECK(ProgramBit(L"RTSSHooksLoader64.exe") == 0);   // RTSS's helper alone is not counted
    CHECK(ProgramBit(L"D2R.exe") == 0);
    CHECK(ProgramBit(nullptr) == 0);
    CHECK(Running(kRtssExe) && Running(kAfterburnerExe) && !Running(kHooks64));
    CHECK(Hooked(kHooks64) && Hooked(kHooks32) && !Hooked(kRtssExe));

    // a line only for something not told yet: the first find, then only a new thing (the hook loaded later), never again for the same
    CHECK(!WorthALine(0, 0));
    CHECK(WorthALine(0, kRtssExe));
    CHECK(!WorthALine(kRtssExe, kRtssExe));
    CHECK(WorthALine(kRtssExe, kRtssExe | kHooks64));
    CHECK(!WorthALine(kRtssExe | kHooks64, kHooks64));   // the program closed meanwhile: nothing new
    CHECK(kLooks == 7 && kEveryMs == 10000);             // 0, 10, ... 60 s: the first minute

    // the line: nothing found = nothing written
    char b[640];
    CHECK(Format(0, "vrcam: ", b, sizeof b) == 0 && b[0] == 0);
    // the player's case: the hook in the game, RTSS and Afterburner running
    const int n = Format(kHooks64 | kRtssExe | kAfterburnerExe, "vrcam: ", b, sizeof b);
    CHECK(n > 0 && n == (int)strlen(b));
    CHECK(strncmp(b, "vrcam: RivaTuner warning - ", 27) == 0);
    CHECK(strstr(b, "RTSSHooks64.dll loaded in the game, RTSS.exe running, MSIAfterburner.exe running") != nullptr);
    CHECK(strstr(b, "Application detection level None") != nullptr);
    CHECK(strstr(b, "D2R.exe and D2RLoader.exe") != nullptr);
    CHECK(strstr(b, "close RivaTuner / MSI Afterburner") != nullptr);
    CHECK(strstr(b, "[support] rtss_warning") != nullptr);
    std::printf("%s\n", b);
    // the reports' line has no "vrcam: " in front
    CHECK(Format(kRtssExe, "", b, sizeof b) > 0 && strncmp(b, "RivaTuner warning - ", 20) == 0 && !strstr(b, "RTSSHooks"));
    CHECK(Format(kHooks32, nullptr, b, sizeof b) > 0 && strstr(b, "found: RTSSHooks.dll loaded in the game.") != nullptr);
    // a small buffer: cut, ended, its length told right
    char s[40];
    const int m = Format(kRtssExe, "vrcam: ", s, sizeof s);
    CHECK(m == (int)sizeof s - 1 && s[sizeof s - 1] == 0 && (int)strlen(s) == m);
    CHECK(Format(kRtssExe, "", nullptr, 10) == 0);

    std::printf(g_failed ? "%d FAILED\n" : "all passed\n", g_failed);
    return g_failed ? 1 : 0;
}
