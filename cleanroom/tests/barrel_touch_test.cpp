// [gameplay] melee_touch_barrels (vr/barrel_touch.h): which objects a swing breaks, their volume, and the press of the game's interact on
// one - once per hand and stroke, held until it breaks or a while passes. No game, no headset.
//
//   barrel_touch_test     (exit code 0 = all passed)

#include <cmath>
#include <cstdio>
#include <cstring>

#include "barrel_touch.h"

static int g_fail = 0, g_checks = 0;
#define CHECK(cond)                                                                   \
    do {                                                                              \
        ++g_checks;                                                                   \
        if (!(cond)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

using namespace barreltouch;

static Row MakeRow(uint8_t sel, uint8_t att, uint8_t op, int sx, int sy) {
    uint8_t b[kRowBytes] = {};
    b[kRowSelectable0] = sel;
    b[kRowAttackable0] = att;
    b[kRowOperateFn] = op;
    std::memcpy(b + kRowSizeX, &sx, 4);
    std::memcpy(b + kRowSizeY, &sy, 4);
    return RowOf(b);
}

int main() {
    // -- which objects ----------------------------------------------------------
    const Row barrel = MakeRow(1, 1, kOpBarrel, 2, 2);
    CHECK(barrel.sizeX == 2 && barrel.sizeY == 2 && barrel.operateFn == 5);   // the row read back as written
    CHECK(Breakable(barrel, 0));
    CHECK(Breakable(MakeRow(1, 1, kOpUrn, 1, 1), 0));
    CHECK(Breakable(MakeRow(1, 1, kOpExploding, 2, 2), 0));
    CHECK(!Breakable(barrel, 1));                         // breaking / broken: not again
    CHECK(!Breakable(barrel, 2));
    CHECK(!Breakable(MakeRow(1, 1, 4, 2, 2), 0));         // a chest (its own OperateFn): its normal controls
    CHECK(!Breakable(MakeRow(1, 1, 8, 2, 2), 0));         // a door
    CHECK(!Breakable(MakeRow(1, 1, 2, 2, 2), 0));         // a shrine
    CHECK(!Breakable(MakeRow(0, 1, kOpBarrel, 2, 2), 0)); // not selectable
    CHECK(!Breakable(MakeRow(1, 0, kOpBarrel, 2, 2), 0)); // not attackable
    CHECK(!Breakable(MakeRow(1, 1, kOpBarrel, 0, 2), 0)); // a footprint that is none
    CHECK(!Breakable(MakeRow(1, 1, kOpBarrel, 9, 2), 0)); // ... or no barrel's
    CHECK(std::strcmp(KindName(kOpBarrel), "barrel") == 0);

    // -- the volume -------------------------------------------------------------
    {
        Barrel b;
        b.id = 7; b.cls = 1; b.unit = 0x1000; b.op = kOpBarrel; b.sizeX = 1; b.sizeY = 1; b.gridX = 100.0f; b.gridZ = 60.0f;
        const float upm = 4.0f, floorM = 0.0f;
        stroke::Body body;
        CHECK(Volume(b, floorM, upm, body));
        CHECK(std::fabs(body.bottom.x - 25.0f) < 1e-4f && std::fabs(body.bottom.z - 15.0f) < 1e-4f);   // its cell in metres on a flat floor
        CHECK(std::fabs(body.radius - (0.25f + 0.05f)) < 1e-4f);   // a footprint of 1 at 4 units a metre, the slack round it
        CHECK(body.top.y > body.bottom.y && std::fabs(body.top.y + body.radius - 1.0f) < 1e-4f);   // 4 units high: 1 m
        stroke::Body raised;
        CHECK(Volume(b, 1.0f, upm, raised));
        CHECK(std::fabs(raised.bottom.x - (25.0f + 1.22474487f)) < 1e-4f);   // raised ground sheared as the renderer draws it
        CHECK(raised.bottom.y > 1.0f);
        b.op = kOpUrn;
        stroke::Body pot;
        CHECK(Volume(b, floorM, upm, pot) && pot.top.y < body.top.y);   // a pot is lower than a barrel
        b.cls = 94;
        stroke::Body urn;
        CHECK(Volume(b, floorM, upm, urn) && urn.top.y > body.top.y);   // the big urns taller
        b.sizeX = b.sizeY = 1;
        stroke::Body small;
        CHECK(Volume(b, floorM, 12.0f, small) && std::fabs(small.radius - (0.16f + 0.05f)) < 1e-4f);   // never thinner than 16 cm
        CHECK(!Volume(b, floorM, 0.5f, small));   // a scale that is none
        b.gridX = NAN;
        CHECK(!Volume(b, floorM, upm, small));
        // a blade swept through it touches; one passing a metre over it does not
        b.gridX = 100.0f; b.cls = 1; b.op = kOpBarrel; b.sizeX = b.sizeY = 1;
        CHECK(Volume(b, floorM, upm, body));
        const stroke::Blade before{{24.0f, 0.6f, 15.0f}, {24.0f, 0.6f, 14.2f}, 0.04f}, after{{26.0f, 0.6f, 15.0f}, {26.0f, 0.6f, 14.2f}, 0.04f};
        CHECK(stroke::Touches(before, after, body));
        const stroke::Blade hi0{{24.0f, 2.2f, 15.0f}, {24.0f, 2.2f, 14.2f}, 0.04f}, hi1{{26.0f, 2.2f, 15.0f}, {26.0f, 2.2f, 14.2f}, 0.04f};
        CHECK(!stroke::Touches(hi0, hi1, body));
    }

    // -- the press --------------------------------------------------------------
    {
        Barrel a, c;
        a.id = 11; a.unit = 0xA000; a.op = kOpBarrel;
        c.id = 12; c.unit = 0xC000; c.op = kOpUrn;
        Presser p;
        CHECK(!p.Pressing() && p.Target() == 0);
        CHECK(p.Ask(0, 5, a, 10.0));
        CHECK(p.Pressing() && p.Target() == 0xA000 && p.Side() == 0);
        CHECK(!p.Ask(1, 9, c, 10.01));                    // one press at a time
        CHECK(p.Step(10.02, 0) == kAsked);                // told once, still whole: held
        CHECK(p.Step(10.5, 0) == 0 && p.Pressing());
        CHECK(p.Step(10.6, 1) == kBroken && !p.Pressing());   // it broke: let go
        CHECK(p.Target() == 0);
        CHECK(!p.Ask(0, 5, a, 10.7));                     // the same stroke, the same object: never twice
        CHECK(p.Ask(0, 6, a, 10.8));                      // another stroke may (a barrel that did not break)
        CHECK(p.Step(10.81, 0) == kAsked);
        CHECK(p.Step(10.8 + kHoldS + 0.01, 0) == kNotBroken && !p.Pressing());   // the game did not take it: let go after a while
        CHECK(p.Ask(1, 6, c, 12.0));                      // the other hand, the same stroke number: its own
        CHECK(p.Step(12.01, -1) == (kAsked | kBroken));   // gone from the unit list at once (an exploding barrel)
        Barrel none;
        CHECK(!p.Ask(0, 7, none, 13.0));                  // no unit: nothing to press on
        CHECK(!p.Ask(0, 0, a, 13.0));                     // no stroke: no press
        CHECK(p.Ask(0, 8, a, 13.0));
        p.Reset();
        CHECK(!p.Pressing() && p.Step(13.1, 0) == 0);     // reset (out of F4, off): nothing pressed, nothing told
    }

    std::printf("barrel_touch_test: %d of %d checks passed\n", g_checks - g_fail, g_checks);
    return g_fail ? 1 : 0;
}
