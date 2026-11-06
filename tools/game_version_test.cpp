// shared/game_version.h: the game data's version from D2RLoader's own log, and the verdict (crash group K, rows 272 / 275). Run: game_version_test.exe
#include <cstdio>
#include <cstring>

#include "game_version.h"

namespace {
int g_fails = 0, g_checks = 0;
#define CHECK(c)                                                                \
    do {                                                                        \
        ++g_checks;                                                             \
        if (!(c)) { ++g_fails; std::printf("  FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

using namespace gamever;

void TestParse() {
    Version v;
    CHECK(ParseLine("[2026-11-06 21:36:05.158] [BUILD] D2R version: 2.7.4.", &v) && v.major == 2 && v.minor == 7 && v.patch == 4);   // 275
    CHECK(ParseLine("[2026-11-06 20:15:31.184] [BUILD] D2R version: 3.3.94995.", &v) && v.major == 3 && v.minor == 3 && v.patch == 94995);   // Serg's Steam game
    CHECK(ParseLine("[BUILD] D2R version: 3.3.0.", &v) && v.major == 3 && v.minor == 3 && v.patch == 0);
    CHECK(ParseLine("D2R version: 3.1", &v) && v.major == 3 && v.minor == 1 && v.patch == -1);
    Version none;
    CHECK(!ParseLine("[BUILD] D2R build-name: 81914.", &none) && !none.known());
    CHECK(!ParseLine("[BUILD] D2R version: unknown.", &none) && !none.known());
    CHECK(!ParseLine("[BUILD] D2R version: 3.", &none) && !none.known());   // a major alone is not a version
    CHECK(!ParseLine(nullptr, &none));
    CHECK(!ParseLine("[WARN] D2R version 2.7 is below the supported range [3.0, 3.4).", &none));   // the warning line has no colon: not taken
}

void TestLog() {
    const char* log275 =
        "[2026-11-06 21:36:05.098] [INFO] Operating system: Windows 11 Pro, version 25H2 (OS build 26200.9711).\r\n"
        "[2026-11-06 21:36:05.158] [BUILD] D2R build-name: 81914.\r\n"
        "[2026-11-06 21:36:05.158] [BUILD] D2R version: 2.7.4.\r\n"
        "[2026-11-06 21:36:05.163] [BUILD]     Version: 1.6.81914\r\n"
        "[2026-11-06 21:36:05.163] [WARN] D2R version 2.7 is below the supported range [3.0, 3.4).\r\n";
    const Version v = FindInLog(log275, std::strlen(log275));
    CHECK(v.major == 2 && v.minor == 7);
    CHECK(Judge(v) == Support::TooOld);
    const char* steam = "[BUILD] Loaded static build configuration for content root 06a0.\n[BUILD] D2R version: 3.3.94995.\n";
    CHECK(Judge(FindInLog(steam, std::strlen(steam))) == Support::Supported);
    const char* nothing = "[INFO] Configuration synchronized.\n";
    CHECK(Judge(FindInLog(nothing, std::strlen(nothing))) == Support::Unknown);   // no line: nothing said
    CHECK(Judge(FindInLog("", 0)) == Support::Unknown);
    // a block cut in the middle of a line: no crash, the cut line not misread
    const char* cut = "[BUILD] D2R version: 2.7.4.\n[BUILD] D2R vers";
    CHECK(FindInLog(cut, std::strlen(cut)).major == 2);
    CHECK(!FindInLog(cut + 28, std::strlen(cut + 28)).known());
}

void TestJudge() {
    Version v;
    v.major = 2; v.minor = 4; CHECK(Judge(v) == Support::TooOld);   // the old Resurrected
    v.major = 3; v.minor = 0; CHECK(Judge(v) == Support::Supported);
    v.major = 3; v.minor = 3; CHECK(Judge(v) == Support::Supported);
    v.major = 4; v.minor = 0; CHECK(Judge(v) == Support::Newer);
    CHECK(Judge(Version{}) == Support::Unknown);
}
}  // namespace

int main() {
    TestParse();
    TestLog();
    TestJudge();
    std::printf("%d of %d checks passed\n", g_checks - g_fails, g_checks);
    return g_fails ? 1 : 0;
}
