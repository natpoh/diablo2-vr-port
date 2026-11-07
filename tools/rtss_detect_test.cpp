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

    // RTSS's shared memory (MSI Afterburner's on-screen display): a made-up mapping laid out as the RTSS SDK's header says
    {
        static unsigned char mm[200000];
        unsigned char* const m = mm;
        memset(mm, 0, sizeof mm);
        auto put = [&](size_t at, unsigned v) { memcpy(m + at, &v, 4); };
        const unsigned appSize = 4 + 260 + 4 + 64, appOff = 64, appCount = 4;
        const unsigned osdSize = 256 + 256 + 4096 + 32, osdOff = appOff + appSize * appCount, osdCount = 8;
        put(0, 0x52545353u);   // 'RTSS'
        put(4, 0x0002000Eu);   // 2.14
        put(8, appSize); put(12, appOff); put(16, appCount);
        put(20, osdSize); put(24, osdOff); put(28, osdCount);
        const size_t size = osdOff + (size_t)osdSize * osdCount;
        auto osd = [&](unsigned e, const char* text, const char* owner, const char* ex) {
            const size_t at = osdOff + (size_t)e * osdSize;
            strcpy((char*)m + at, text);
            strcpy((char*)m + at + 256, owner);
            strcpy((char*)m + at + 512, ex);
        };
        auto app = [&](unsigned e, unsigned pid, const char* name) {
            const size_t at = appOff + (size_t)e * appSize;
            put(at, pid);
            strcpy((char*)m + at + 4, name);
        };
        char owner[32];
        // nothing in it: nothing
        CHECK(ReadSharedMemory(m, size, owner, sizeof owner) == 0 && owner[0] == 0);
        // RTSS's own entry 0 is not counted; a slot taken but empty is not an OSD
        osd(0, "RTSS", "RivaTuner Statistics Server", "");
        osd(1, "", "MSI Afterburner", "");
        CHECK(ReadSharedMemory(m, size) == 0);
        // Afterburner with text (the old field or the 2.7+ one): its OSD is on
        osd(1, "", "MSI Afterburner", "<C0>GPU 62 C<C>");
        CHECK(ReadSharedMemory(m, size) == kOsdAfterburner);
        osd(1, "GPU 62", "MSIAfterburner", "");
        CHECK(ReadSharedMemory(m, size) == kOsdAfterburner);
        // another owner as well: named
        osd(3, "FPS 90", "HWiNFO64", "");
        CHECK(ReadSharedMemory(m, size, owner, sizeof owner) == (kOsdAfterburner | kOsdOther) && strcmp(owner, "HWiNFO64") == 0);
        // the game hooked: by its path's base name, any case; a process 0 entry is a free one
        app(0, 0, "C:\\Games\\D2R\\D2R.exe");
        CHECK(!(ReadSharedMemory(m, size) & kGameInRtss));
        app(1, 4242, "C:\\Games\\D2R\\d2r.EXE");
        CHECK(ReadSharedMemory(m, size) & kGameInRtss);
        app(1, 4242, "C:\\Games\\D2R\\D2R.exe.old");
        CHECK(!(ReadSharedMemory(m, size) & kGameInRtss));
        app(1, 4242, "D2RLoader.exe");
        CHECK(ReadSharedMemory(m, size) & kGameInRtss);
        // before 2.7 the long field is not read (it may not be there)
        osd(1, "", "MSI Afterburner", "<ex only>");
        put(4, 0x00020006u);
        CHECK(!(ReadSharedMemory(m, size) & kOsdAfterburner));
        put(4, 0x0002000Eu);
        CHECK(ReadSharedMemory(m, size) & kOsdAfterburner);
        // a mapping cut short: nothing read past it, no crash; a slot's owner running to the mapping's end is not taken
        CHECK((ReadSharedMemory(m, osdOff + 2 * (size_t)osdSize) & kOsdOther) == 0);
        CHECK(ReadSharedMemory(m, 20) == 0 && ReadSharedMemory(nullptr, size) == 0);
        // not RTSS's, or too old: nothing
        put(0, 0x11111111u);
        CHECK(ReadSharedMemory(m, size) == 0);
        put(0, 0x52545353u);
        put(4, 0x00010000u);
        CHECK(ReadSharedMemory(m, size) == 0);
        // the words: Afterburner's OSD named, Running() counts it, the line says it
        CHECK(Running(kOsdAfterburner) && !Running(kGameInRtss));
        CHECK(Format(kRtssExe | kOsdAfterburner | kGameInRtss, "vrcam: ", b, sizeof b) > 0 &&
              strstr(b, "RTSS.exe running, MSI Afterburner's on-screen display on, RTSS lists the game as hooked") != nullptr);
        std::printf("%s\n", b);
    }

    std::printf(g_failed ? "%d FAILED\n" : "all passed\n", g_failed);
    return g_failed ? 1 : 0;
}
