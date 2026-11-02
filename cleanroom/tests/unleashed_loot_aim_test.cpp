// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed commit 07016801d25e5b0b5fa718511c3aa90a99dff800, its steamvr/loot_aim_test.cpp, for D2R VR Authentic's port
// (vr/unleashed_loot_aim.h, [input] point_pickup): the choice, the arm out, the reach, the name look and the label owners' checks kept; the room
// walk and the press watch left out (not taken); Answer's, the several-items-in-a-row and the cut's checks added. Stays MPL 2.0.
#include "unleashed_loot_aim.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
using namespace unleashed::lootaim;
static int g_fail = 0;
static void Check(bool b, const char* why) {
    if (!b) { std::printf("FAIL: %s\n", why); ++g_fail; }
}

int main() {
    // A hand 6 units up pointing along +z; items on the floor (y 0), with their distance from the hero in cells.
    const Ray hand{{0, 6, 0}, {0, -.5f, 1}, true};   // meets the floor at z 12
    const Item items[] = {{1, {0, 0, 12}, 6}, {2, {3, 0, 12}, 6.5f}, {3, {0, 0, 100}, 50}, {4, {-.4f, 0, 3}, 1.5f}};
    auto at = [](float x) { return Ray{{0, 6, 0}, {x / 12, -.5f, 1}, true}; };   // the hand pointed at x on the floor at z 12
    Check(Select(items, 4, hand, 0).id == 1, "the hand picks the item where its ray meets the floor");
    Check(Select(items, 4, at(3), 0).id == 2, "pointing over at the neighbour picks it");
    Check(Select(items, 4, {{0, 6, 0}, {0, .2f, 1}, true}, 0).id == 0, "pointing up: no pick");
    Check(Select(items, 4, {{0, 6, 0}, {0, -.06f, 1}, true}, 0).id == 0, "an item past 40 cells is never picked, even dead on the aim");
    Check(Select(items, 4, at(1.4f), 0).id == 1 && Select(items, 4, at(-3.5f), 0).id == 0, "a circle of 1.5 cells (3 units) round the pointed spot");
    Check(Select(items, 4, Ray{}, 0).id == 0 && Select(items, 4, Ray{}, 1).id == 0, "no ray (the arm not out): no pick, the current one let go");
    Check(std::fabs(FloorDistance(hand, {0, 0, 12})) < 1e-3f && std::fabs(FloorDistance(hand, {3, 0, 12}) - 3) < 1e-3f, "floor distance from the pointed spot");
    Check(FloorDistance({{0, 6, 0}, {0, .1f, 1}, true}, {0, 0, 12}) > 1e8f, "a ray pointing up never meets the floor");
    Check(Select(nullptr, 4, hand, 0).id == 0 && Select(items, 0, hand, 0).id == 0, "no items: no pick");

    // Between two items the current one stays until another is nearer by a quarter cell (0.5 units).
    Check(Select(items, 4, at(1.6f), 1).id == 1 && Select(items, 4, at(1.6f), 2).id == 2, "between two items the current pick stays, either way");
    Check(Select(items, 4, at(1.74f), 1).id == 1, "1.26 units against 1.74: not yet a quarter cell nearer");
    Check(Select(items, 4, at(1.76f), 1).id == 2, "1.24 against 1.76: the neighbour takes over");
    Check(Select(items, 4, at(-3.5f), 1).id == 1, "the current pick stays out to 130% of the circle");
    Check(Select(items, 4, at(4.5f), 1).id == 2, "and past it, 4.5 units off, is let go");

    {   // Serg's test: five items in a row a cell apart (2 units), the hand swept along them - each pointed at is the one picked, not the
        // hero's nearest (id 10, at his feet).
        Item row[6];
        for (int i = 0; i < 5; ++i) row[i] = {uint32_t(20 + i), {float(i) * 2.0f - 4.0f, 0, 12}, 6};
        row[5] = {10, {0, 0, 1}, .5f};
        uint32_t current = 0;
        for (int i = 0; i < 5; ++i) {
            current = Select(row, 6, at(float(i) * 2.0f - 4.0f), current).id;
            Check(current == uint32_t(20 + i), "each item of the row, pointed at, is the pick");
        }
        Check(Select(row, 6, {{0, 6, 0}, {0, -6, 1}, true}, current).id == 10, "pointed at the feet: the item there");
    }
    {   // a raised floor: the ray meets each item at its own height
        const Item up[] = {{7, {0, 2, 8}, 4}};
        Check(Select(up, 1, hand, 0).id == 7, "an item 2 units up is met where the ray crosses its height (z 8)");
    }

    // Arm out to point (12 cm), let go 6 cm nearer.
    Check(!Pointing(false, .11f) && Pointing(false, .12f), "12 cm out starts pointing");
    Check(Pointing(true, .07f) && !Pointing(true, .05f), "it lets go 6 cm nearer");
    Check(!Pointing(true, NAN), "no tracking: not pointing");

    Check(InReach(4, DefaultPickupCells) && !InReach(4.1f, DefaultPickupCells) && !InReach(-1, 4) && InReach(30, 0), "pick-up reach 4 cells, 0 = any distance");

    Check(OnFloor(4, 3) && OnFloor(4, 5) && !OnFloor(4, 1) && !OnFloor(4, 4) && !OnFloor(2, 3), "an item on the ground or dropping; not stored, not on the cursor, not an object");

    float rgb[3];
    RarityColor(4, 0, rgb); Check(rgb[2] == 1 && rgb[0] < .5f, "magic is blue");
    RarityColor(5, 0, rgb); Check(rgb[1] == 1 && rgb[0] < .5f, "set is green");
    RarityColor(6, 0, rgb); Check(rgb[0] == 1 && rgb[1] == 1 && rgb[2] < .5f, "rare is yellow");
    RarityColor(7, 0, rgb); Check(rgb[0] > .8f && rgb[2] < .5f, "unique is gold");
    RarityColor(8, 0, rgb); Check(rgb[0] == 1 && rgb[1] == .6f && rgb[2] < .2f, "crafted is orange");
    RarityColor(2, 0x800, rgb); Check(rgb[0] == .6f && rgb[1] == .6f, "a socketed normal item is gray");
    RarityColor(2, 0, rgb); Check(rgb[0] == 1 && rgb[1] == 1 && rgb[2] == 1, "a normal item is white");

    {   // The pointed name's look: in reach a rarity rim on a rarity shade at 180%, out of reach a red rim on the game's box at 130%.
        const float blue[3] = {.41f, .41f, 1}, gameBox[4] = {.05f, .05f, .05f, .8f};
        float rim[4], box[4];
        for (uint64_t ms = 0; ms < 3000; ms += 37) {
            RimRgba(LookFor(true), blue, ms, rim);
            Check(rim[0] == blue[0] && rim[2] == 1 && rim[3] >= .2f - 1e-4f && rim[3] <= 1 + 1e-4f, "in reach: the rarity rim pulses between 20% and full (1 Hz, 40%)");
            RimRgba(LookFor(false), blue, ms, rim);
            Check(rim[0] == RedRgb[0] && rim[1] == RedRgb[1] && rim[3] >= -1e-4f && rim[3] <= .9f + 1e-4f, "out of reach: a red rim up to 90% (1.5 Hz, 50%)");
        }
        BoxRgba(LookFor(true), gameBox, blue, box);
        Check(std::fabs(box[0] - .41f * RarityShade) < 1e-5f && std::fabs(box[2] - RarityShade) < 1e-5f && box[3] == 1, "in reach: a dark shade of the rarity color, solid");
        BoxRgba(LookFor(false), gameBox, blue, box);
        Check(box[0] == gameBox[0] && box[3] == .9f, "out of reach: the game's own fill at 90%");
        BoxRgba(LookFor(false), nullptr, blue, box);
        Check(box[0] == 0 && box[1] == 0 && box[2] == 0, "no fill from the game: black");
        Check(LookFor(true).nameScale == 1.8f && LookFor(false).nameScale == 1.3f && MaxNameScale == 2 && RimPx == 1, "names 180% and 130%, at most twice the game's; a 1 px rim");
        Check(Pulse(1234, 0, .5f) == 1, "no pulse speed: steady");
        const int32_t label[4] = {10, 20, 30, 8};
        int32_t ring[4][4];
        Check(Ring(label, 1, ring) == 4 && Ring(label, 0, ring) == 0 && Ring(nullptr, 1, ring) == 0, "four strips, none without a width or a box");
        Ring(label, 1, ring);
        Check(ring[0][0] == 9 && ring[0][1] == 19 && ring[0][2] == 32 && ring[0][3] == 1 && ring[1][1] == 28 && ring[2][0] == 9 && ring[2][3] == 8 && ring[3][0] == 40,
              "top and bottom across the outer width, left and right the box's height");
        for (int i = 0; i < 4; ++i) {
            const bool apart = ring[i][0] + ring[i][2] <= label[0] || ring[i][0] >= label[0] + label[2] || ring[i][1] + ring[i][3] <= label[1] || ring[i][1] >= label[1] + label[3];
            Check(apart, "the rim never covers the name's box");
        }
    }
    {   // The label paint's owner (2026-10-29 log): every name laid out in one scratch box, painted from its own record.
        int scratch[4] = {}, paint[8][4] = {};
        char names[8][8] = {};
        LabelOwners<64> owners;
        for (int i = 0; i < 8; ++i) owners.Remember(scratch, names[i], 0x1000 + i);   // one eye's pass: 8 shown names
        for (int i = 0; i < 8; ++i) Check(owners.Unit(paint[i], names[i]) == uintptr_t(0x1000 + i), "each paint finds its own item, not the last laid out");
        for (int i = 0; i < 8; ++i) owners.Remember(scratch, names[i], 0x1000 + i);   // the other eye: the same slots again
        int used = 0;
        for (const auto& s : owners.slot) used += s.seq != 0;
        Check(used == 8, "the second eye's pass reuses the first's slots");
        Check(owners.Unit(scratch, nullptr) == 0, "the shared scratch box alone names no item");
        Check(owners.Unit(scratch, names[3]) == 0x1003, "the exact pair still finds it");
        char unknown[8] = {};
        Check(owners.Unit(paint[0], unknown) == 0, "a name never laid out: no item (an ordinary paint)");
        owners.Remember(scratch, names[2], 0x2002);   // the record now holds another item
        Check(owners.Unit(paint[2], names[2]) == 0x2002, "a record reused by another item: the latest");
        Check(owners.Find(0x2002) >= 0 && owners.Find(0x1002) < 0 && owners.Find(0) < 0, "Find: the item's slot, a replaced item none");
    }
    {   // The other names cut while Show items is held for the pointed one; a short tail after; never when the player holds it.
        Check(OnlyPointedUntil(true, false, 0, 1000, 250) == 1250 && OnlyPointed(1250, 1100), "held: cut for the lease");
        Check(OnlyPointedUntil(false, false, 1250, 1100, 250) == 1250 && OnlyPointedUntil(false, false, 5000, 1100, 250) == 1250, "let go: at most the tail");
        Check(OnlyPointedUntil(true, true, 1250, 1100, 250) == 0 && !OnlyPointed(0, 1100), "the player's own Show items: nothing cut");
    }

    // Answer: what the game's Interact lookup gets.
    const uintptr_t game = 0x1000, pick = 0x2000;
    Check(Answer(game, true, 0, true) == game && Answer(0, false, 0, false) == 0, "nothing pointed: the game's own answer");
    Check(Answer(game, true, pick, true) == pick, "the pick in reach replaces the game's nearest item");
    Check(Answer(game, false, pick, true) == pick && Answer(0, false, pick, true) == pick, "in reach it replaces a door or none too");
    Check(Answer(game, true, pick, false) == 0, "past the reach: no item at all, not the game's nearest either");
    Check(Answer(game, false, pick, false) == game, "past the reach: a door, waypoint or NPC the game chose still works");

    if (g_fail) { std::printf("%d failed\n", g_fail); return 1; }
    std::printf("unleashed_loot_aim_test: all passed\n");
    return 0;
}
