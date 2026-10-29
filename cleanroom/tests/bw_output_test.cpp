// Offline checks of tools/settings/bw_output.h: BodyWalk's Output read from its settings file the way D2R VR Settings' Status row reads
// it, and what the row says - Keyboard (what the mod's profile left, 2026-10-17: no BodyWalk button in FlatVR) is a warning.
// Run: build/Release/bw_output_test.exe (exit code 0 = passed)

#include <cstdio>
#include <string>

#include "../../tools/settings/bw_output.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

int main() {
    using namespace bwout;
    // BodyWalk's own file (dump(4)), LF and CRLF; a nested "mode" before the top-level one is not it
    const std::string lf =
        "{\n    \"bind_steamvr_toggle\": false,\n    \"embedded\": {\n        \"mode\": \"Nested\"\n    },\n"
        "    \"dodge_output_mode\": \"Follow Main Output\",\n    \"mode\": \"Keyboard\",\n    \"zz\": 1\n}\n";
    CHECK(Mode(lf) == "Keyboard");
    std::string crlf;
    for (char c : lf) { if (c == '\n') crlf += '\r'; crlf += c; }
    CHECK(Mode(crlf) == "Keyboard");
    CHECK(Mode("{\n    \"mode\": \"Xbox Controller (vJoy)\"\n}") == kXbox);
    CHECK(Mode("{\n    \"mode\" : \"Game Link\"\n}") == kGameLink);
    // not there, not a string, only nested, only a key that ends in mode
    CHECK(Mode("{\n    \"other\": 1\n}").empty());
    CHECK(Mode("{\n    \"mode\": 3\n}").empty());
    CHECK(Mode("{\n    \"x\": {\n        \"mode\": \"Keyboard\"\n    }\n}").empty());
    CHECK(Mode("{\n    \"dodge_output_mode\": \"Keyboard\"\n}").empty());
    CHECK(Mode("").empty());
    // the verdicts
    CHECK(Judge("") == Verdict::Unknown);
    CHECK(Judge(kXbox) == Verdict::Ok);
    CHECK(Judge(kGameLink) == Verdict::Ok);
    for (const char* m : {"Keyboard", "Disabled", "SteamVR Hook", "SteamVR Controller", "OpenXR API Layer", "VRChat OSC"})
        CHECK(Judge(m) == Verdict::Warn);
    if (g_failures) { std::printf("%d check(s) FAILED\n", g_failures); return 1; }
    std::printf("bw_output_test: all %d checks passed\n", g_checks);
    return 0;
}
