// Offline checks of vr/automap_party.h: the game's "AutoMap Party" / "AutoMap Party Names" read from Settings.json text, and the log's advice.
// No game, no file. Run: build/Release/automap_party_test.exe (exit code 0 = passed)

#include <cstdio>
#include <cstring>
#include <string>

#include "../../vr/automap_party.h"

using namespace automapparty;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

// The lines as Serg's Settings.json has them (2026-11-04 18:46): "AutoMap Party" comes before "AutoMap Party Names" - a plain find of the
// first must not read the second's number, nor the other way round.
static const char* kSerg =
    "{\r\n    \"AutoMapMode\": 2,\r\n    \"AutoMapFade\": 1,\r\n    \"AutoMapFadeAllCustomOpacity\": 50,\r\n    \"AutoMap Centers\": 1,\r\n"
    "    \"AutoMap Party\": 0,\r\n    \"AutoMap Party Names\": 0,\r\n    \"Automap Room Name and Password\": 1,\r\n    \"Auto Party Invite\": 1,\r\n}";

static void TestValue() {
    const std::string t = kSerg;
    CHECK(Value(t, "AutoMap Party") == 0);
    CHECK(Value(t, "AutoMap Party Names") == 0);
    CHECK(Value(t, "AutoMapMode") == 2);
    CHECK(Value(t, "AutoMapFadeAllCustomOpacity") == 50);
    CHECK(Value(t, "Auto Party Invite") == 1);
    CHECK(Value(t, "AutoMap Desecrated Zone") == -1);   // missing
    // "AutoMap Party" 1, names 0: each its own
    const std::string m = "\"AutoMap Party\": 1,\n\"AutoMap Party Names\":0";
    CHECK(Value(m, "AutoMap Party") == 1);
    CHECK(Value(m, "AutoMap Party Names") == 0);
    // names first in the file, then the party: still each its own
    const std::string r = "\"AutoMap Party Names\": 1, \"AutoMap Party\": 0";
    CHECK(Value(r, "AutoMap Party") == 0);
    CHECK(Value(r, "AutoMap Party Names") == 1);
    // broken numbers: -1
    CHECK(Value("\"AutoMap Party\": true", "AutoMap Party") == -1);
    CHECK(Value("\"AutoMap Party\" 1", "AutoMap Party") == -1);
    CHECK(Value("\"AutoMap Party\":", "AutoMap Party") == -1);
    CHECK(Value("", "AutoMap Party") == -1);
}

static void TestAdvice() {
    const State s = Read(kSerg);
    CHECK(s.party == 0 && s.names == 0);
    const char* a = Advice(s);
    CHECK(a && std::strstr(a, "Show Party and Show Party Names"));
    CHECK(Advice(State{1, 1}) == nullptr);
    CHECK(Advice(State{-1, -1}) == nullptr);   // not in the file: the game's default
    const char* p = Advice(State{0, 1});
    CHECK(p && std::strstr(p, "Show Party on") && !std::strstr(p, "Names"));
    const char* n = Advice(State{1, 0});
    CHECK(n && std::strstr(n, "Show Party Names on"));
}

int main() {
    TestValue();
    TestAdvice();
    std::printf("automap_party_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
