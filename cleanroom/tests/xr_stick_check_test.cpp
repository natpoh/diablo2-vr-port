// Offline checks of vr/xr_stick_check.h: is the thumbstick bound by the OpenXR runtime (the report of 2026-10-18: SteamVR's saved binding hid it), what is
// read then, and the words. No OpenXR, no headset. Run: xr_stick_check_test.exe (exit 0 = all passed).
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>

#include "../../shared/d2r_vr_state.h"
#include "../../vr/xr_stick_check.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

using xrstick::Facts;
using xrstick::Verdict;

static void TestDecide() {
    // a working setup: the stick has sources (both hands), the buttons too -> bound, no spare read
    CHECK(xrstick::Decide(Facts{2, 2, 12}) == Verdict::Bound);
    CHECK(xrstick::Decide(Facts{1, 0, 6}) == Verdict::Bound);     // one controller on
    CHECK(xrstick::Decide(Facts{2, 0, 12}) == Verdict::Bound);    // the spare unbound while the first works: nothing to do
    CHECK(!xrstick::UseSpare(Verdict::Bound));
    // the report: the buttons are bound, the stick is not
    CHECK(xrstick::Decide(Facts{0, 2, 12}) == Verdict::UnboundSpare);
    CHECK(xrstick::UseSpare(Verdict::UnboundSpare));
    CHECK(xrstick::Decide(Facts{0, 0, 12}) == Verdict::Unbound);
    CHECK(!xrstick::UseSpare(Verdict::Unbound));                  // nothing to read it through: only the words
    // nothing is bound at all (controllers off, the runtime has not answered): no conclusion, whatever the spare says
    CHECK(xrstick::Decide(Facts{0, 0, 0}) == Verdict::Unknown);
    CHECK(xrstick::Decide(Facts{0, 2, 0}) == Verdict::Unknown);
    CHECK(!xrstick::UseSpare(Verdict::Unknown));
    // a controller with no thumbstick to bind (the simple controller): its missing stick is not a binding fault, whatever the counts
    { Facts f{0, 0, 4}; f.stickExpected = false; CHECK(xrstick::Decide(f) == Verdict::Unknown); f.stick = 1; CHECK(xrstick::Decide(f) == Verdict::Unknown); }
    // never a verdict from a negative count
    CHECK(xrstick::Decide(Facts{0, 0, -1}) == Verdict::Unknown);
}

static void TestBits() {
    // the verdict goes through the state block's twoHanded and comes back
    for (Verdict v : {Verdict::Unknown, Verdict::Bound, Verdict::UnboundSpare, Verdict::Unbound}) CHECK(xrstick::FromBits(xrstick::Bits(v)) == v);
    CHECK(xrstick::Bits(Verdict::Unknown) == 0);
    // ... beside the bits already there (the weapon's, the hero's) without touching them
    const uint32_t others = (D2RVR_TWO_HANDED | D2RVR_TWO_HANDS_ON | D2RVR_TWO_HANDED_KNOWN | D2RVR_LEFT_WEAPON) | D2RVRHeroBits(6);
    for (Verdict v : {Verdict::Unknown, Verdict::Bound, Verdict::UnboundSpare, Verdict::Unbound}) {
        const uint32_t word = others | xrstick::Bits(v);
        CHECK(xrstick::FromBits(word) == v);
        CHECK((word & ~(7u << 13)) == others);
        CHECK(D2RVRHeroOf(word) == 6);
    }
    CHECK((xrstick::Bits(Verdict::UnboundSpare) & ~(7u << 13)) == 0 && (xrstick::kBitKnown | xrstick::kBitUnbound | xrstick::kBitSpare) == (7u << 13));
    CHECK((xrstick::kBitKnown & (D2RVR_HERO_MASK | D2RVR_HERO_KNOWN)) == 0 && (xrstick::kBitSpare & (D2RVR_HERO_MASK | D2RVR_HERO_KNOWN)) == 0);
    // an old plugin (no bits): unknown, never "unbound"
    CHECK(xrstick::FromBits(D2RVR_TWO_HANDED | D2RVRHeroBits(2)) == Verdict::Unknown);
}

static void TestWords() {
    const std::string manage = "SteamVR > Settings > Controllers > Manage Controller Bindings > D2RLoader > default";
    for (Verdict v : {Verdict::UnboundSpare, Verdict::Unbound}) {
        const std::string log = xrstick::LogText(v);
        CHECK(log.find(manage) != std::string::npos);                       // the way out, spelled as the player finds it
        const std::wstring status = xrstick::StatusText(v);
        CHECK(status.find(L"SteamVR > Settings > Controllers > Manage Controller Bindings > D2RLoader > default") != std::wstring::npos);
        CHECK(status.find(L"bound nothing to the thumbstick") != std::wstring::npos);
    }
    CHECK(std::strstr(xrstick::LogText(Verdict::Unbound), "do not reach the game") != nullptr);       // the failure is said plainly
    CHECK(std::strstr(xrstick::LogText(Verdict::UnboundSpare), "second thumbstick action") != nullptr);
    CHECK(std::string(xrstick::LogText(Verdict::Unknown)).empty());
    CHECK(std::wcslen(xrstick::StatusText(Verdict::Bound)) > 0 && std::wcslen(xrstick::StatusText(Verdict::Unknown)) > 0);
}

int main() {
    TestDecide();
    TestBits();
    TestWords();
    std::printf("xr_stick_check_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
