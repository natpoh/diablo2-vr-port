// flat_shot_test - [input] flat_shot_at_crosshair, the pure part (vr/flat_shot.h): which flat clicks lose the game's hovered unit and go to the crosshair.
#include <cstdio>
#include <cstring>

#include "../../vr/client_verdict.h"
#include "../../vr/flat_shot.h"

namespace {

int g_fails = 0, g_checks = 0;
#define EXPECT(c, ...)                                           \
    do {                                                         \
        ++g_checks;                                              \
        if (!(c)) {                                              \
            ++g_fails;                                           \
            std::printf("  FAIL line %d: %s - ", __LINE__, #c);  \
            std::printf(__VA_ARGS__);                            \
            std::printf("\n");                                   \
        }                                                        \
    } while (0)

using namespace flatshot;

// A bow in hand, flat mouse look, a left click, a wild monster hovered in the Blood Moor.
ClickIn Shot() {
    ClickIn in;
    in.on = true;
    in.flat = true;
    in.shooter = true;
    in.mouseType = true;
    in.ranged = true;
    in.hover = true;
    in.hoverRead = true;
    in.type = kTypeMonster;
    in.mode = 1;
    in.owner = kNoOwner;
    in.heroLevel = 2;
    return in;
}

void TestBare() {
    std::printf("a hostile monster hovered: the click goes to the crosshair\n");
    const ClickOut o = DecideClick(Shot());
    EXPECT(o.click == Click::Bare, "%s", o.why);
    const uint32_t modes[] = {1u, 2u, 7u, 8u};   // standing, walking, attacking
    for (uint32_t mode : modes) {
        ClickIn in = Shot();
        in.mode = mode;
        EXPECT(DecideClick(in).click == Click::Bare, "mode %u", mode);
    }
}

void TestGame() {
    std::printf("everything else stays the game's own click\n");
    struct Case { const char* what; void (*set)(ClickIn&); };
    const Case cases[] = {
        {"the key off", [](ClickIn& in) { in.on = false; }},
        {"VR", [](ClickIn& in) { in.flat = false; }},
        {"no mouse look", [](ClickIn& in) { in.shooter = false; }},
        {"a menu open", [](ClickIn& in) { in.menu = true; }},
        {"a portal click", [](ClickIn& in) { in.portal = true; }},
        {"not a mouse button", [](ClickIn& in) { in.mouseType = false; }},
        {"a sword in hand", [](ClickIn& in) { in.ranged = false; }},
        {"nothing hovered", [](ClickIn& in) { in.hover = false; }},
        {"not read", [](ClickIn& in) { in.hoverRead = false; }},
        {"an item", [](ClickIn& in) { in.type = 4; }},
        {"an object (a chest, a door, a portal)", [](ClickIn& in) { in.type = 2; }},
        {"another player", [](ClickIn& in) { in.type = 0; }},
        {"dying", [](ClickIn& in) { in.mode = kModeDying; }},
        {"dead", [](ClickIn& in) { in.mode = kModeDead; }},
        {"a hireling (the hero owns it)", [](ClickIn& in) { in.owner = 1; }},
        {"the area not known", [](ClickIn& in) { in.heroLevel = 0; }},
        {"Rogue Encampment", [](ClickIn& in) { in.heroLevel = 1; }},
        {"Lut Gholein", [](ClickIn& in) { in.heroLevel = 40; }},
        {"Kurast Docks", [](ClickIn& in) { in.heroLevel = 75; }},
        {"Pandemonium Fortress", [](ClickIn& in) { in.heroLevel = 103; }},
        {"Harrogath", [](ClickIn& in) { in.heroLevel = 109; }},
    };
    for (const Case& c : cases) {
        ClickIn in = Shot();
        c.set(in);
        const ClickOut o = DecideClick(in);
        EXPECT(o.click == Click::Game && o.why && *o.why, "%s: %s", c.what, o.why);
    }
}

void TestSameAsTouch() {
    std::printf("the same towns and owner as hit by touch's client verdict\n");
    for (int level = 0; level < 140; ++level) EXPECT(IsTown(level) == clientverdict::IsTown(level), "level %d", level);
}
static_assert(kNoOwner == clientverdict::kNoOwner, "the owner nobody has");

}  // namespace

int main() {
    TestBare();
    TestGame();
    TestSameAsTouch();
    std::printf("\nflat_shot_test: %d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
