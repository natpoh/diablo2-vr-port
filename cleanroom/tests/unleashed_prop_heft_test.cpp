// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed commit fbb31fb, its steamvr/prop_heft_test.cpp, for D2R VR Authentic's port (vr/unleashed_prop_heft.h, [weight] prop_heft):
// the masses' checks kept; the striking mass, the bumps and the Box3D world's strikes left out (not taken: the weight library strikes here); the
// pulse's checks remade for FeelOf; every kind of our catalogue (shared/props_scene.h) weighed at a usual size. Stays MPL 2.0.
#include "unleashed_prop_heft.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>

using namespace unleashed::propheft;
using propscene::Material;
using propscene::Shape;
using propscene::Vec;

static int g_fail = 0;
static void Check(bool b, const char* why) {
    if (!b) { std::printf("FAIL: %s\n", why); ++g_fail; }
}
static bool Near(float a, float b, float share) { return std::fabs(a - b) <= share * std::fabs(b); }
// A kind as the catalogue has it (its shape and material from shared/props_scene.h's table, so a change there shows here).
static float Kg(const char* kind, Vec size) {
    const int i = propscene::KindOfModel(std::string("data/hd/env/model/act1/props/") + kind + ".model");
    if (i < 0) { std::printf("FAIL: %s is no kind of the catalogue\n", kind); ++g_fail; return 0; }
    const auto& k = propscene::Kinds()[(size_t)i];
    return Mass(k.name, k.material, k.shape, size);
}

int main() {
    // ---- Masses by kind and size ----
    const float cup = Kg("cup", {.08f, .1f, .08f});
    const float bottle = Kg("bottle", {.08f, .25f, .08f});
    const float plate = Kg("plate", {.24f, .025f, .24f});
    const float book = Kg("book", {.24f, .04f, .18f});
    const float skull = Kg("skull", {.16f, .2f, .2f});
    const float apple = Kg("apple", {.08f, .08f, .08f});
    const float pail = Kg("pail", {.28f, .26f, .28f});
    const float vase = Kg("vase", {.24f, .5f, .24f});
    const float urn = Kg("expansion_urn", {.6f, .9f, .6f});
    const float stool = Kg("stool", {.36f, .5f, .36f});
    const float chair = Kg("chair", {.5f, 1.f, .5f});
    const float barrel = Kg("barrel", {.6f, .9f, .6f});
    const float crate = Kg("crate", {.6f, .6f, .6f});
    const float sack = Kg("sack", {.5f, .7f, .4f});
    const float bale = Kg("hay_bale", {.9f, .4f, .5f});
    const float pillow = Kg("pillow_blue", {.5f, .16f, .36f});
    const float tablet = Kg("tablet", {.3f, .4f, .05f});
    const float hammer = Kg("hammer", {.3f, .04f, .1f});
    std::printf("masses (kg): cup %.2f bottle %.2f plate %.2f book %.2f skull %.2f apple %.2f pail %.2f vase %.2f urn %.1f stool %.1f chair %.1f barrel %.1f "
                "crate %.1f sack %.1f hay %.1f pillow %.2f tablet %.1f hammer %.2f\n",
                cup, bottle, plate, book, skull, apple, pail, vase, urn, stool, chair, barrel, crate, sack, bale, pillow, tablet, hammer);
    // The numbers the port was asked for (the source commit's message: a cup 0.18 kg, a chair 9.8, a barrel 48, a sack of grain 60).
    Check(Near(cup, .18f, .1f), "a cup weighs about 0.18 kg");
    Check(Near(chair, 9.8f, .05f), "a chair about 9.8 kg");
    Check(Near(barrel, 48, .05f), "a barrel about 48 kg");
    Check(Near(sack, 60, .01f), "a sack of grain 60 kg (its kind's most)");
    // Plausible, each within what such a thing weighs.
    Check(cup > .1f && cup < .4f, "cup"); Check(bottle > .25f && bottle < .9f, "bottle"); Check(plate > .3f && plate < 1.2f, "plate");
    Check(book > .6f && book < 2, "book"); Check(skull > .5f && skull < 2, "skull"); Check(apple > .1f && apple < .4f, "apple");
    Check(pail > 1 && pail < 4, "pail"); Check(vase > 2 && vase < 8, "vase"); Check(urn > 20 && urn < 80, "urn");
    Check(stool > 1.5f && stool < 5, "stool"); Check(chair > 5 && chair < 15, "chair"); Check(barrel > 35 && barrel < 70, "barrel");
    Check(crate > 20 && crate < 80, "crate"); Check(sack > 40 && sack <= 60, "sack"); Check(bale > 15 && bale < 40, "hay bale");
    Check(pillow > .3f && pillow < 3, "pillow"); Check(tablet > 5 && tablet < 20, "tablet"); Check(hammer > .3f && hammer <= 2, "hammer");
    // Light things to heavy ones.
    Check(cup < bottle && bottle < plate && plate < book && book < pail && pail < vase && vase < chair && chair < barrel && chair < sack, "light to heavy");
    // Size counts: a vessel's walls grow with its surface, a solid with its volume.
    const float smallVase = Kg("vase_small", {.12f, .25f, .12f});
    Check(smallVase < vase * .4f && smallVase > vase * .1f, "a small vase: its walls' share of the big one");
    Check(Near(Kg("book", {.48f, .08f, .36f}), book * 8, .01f), "a book twice as big each way: eight times the mass");
    const float smallBarrel = Kg("barrel", {.4f, .6f, .4f});
    Check(smallBarrel < barrel * .5f && smallBarrel > 5, "a small barrel");
    // The classification: a named kind by its name, a family by its stem, an unknown kind by its material, nothing unreadable.
    Check(RecipeFor("barrel", Material::Wood).build == Build::Shell && RecipeFor("crate", Material::Wood).build == Build::Shell, "barrels and crates are closed shells");
    Check(RecipeFor("cup", Material::Ceramic).build == Build::Vessel && RecipeFor("basket", Material::Wicker).build == Build::Vessel, "cups and baskets are open vessels");
    Check(RecipeFor("chair", Material::Furniture).fill < .1f && RecipeFor("book", Material::Paper).fill > .8f, "a chair is mostly air, a book mostly paper");
    Check(RecipeFor("pillow_red", Material::Cloth).density == 60 && RecipeFor("package_white", Material::Paper).density == 300, "families by their stem");
    Check(RecipeFor("pillowcase", Material::Cloth).density == 60, "a family takes any name it begins");
    Check(RecipeFor("barrels", Material::Wood).build == Build::Solid, "an exact name does not");
    Check(RecipeFor("l_barrel", Material::Wood).build == Build::Solid, "another barrel's name falls to its material (wood)");
    Check(RecipeFor("", Material::Stone).density == 2500 && RecipeFor("", Material(200)).build == Build::Solid, "no name: its material; no material: a solid");
    Check(Mass("cup", Material::Ceramic, Shape::Round, {std::numeric_limits<float>::quiet_NaN(), .1f, .08f}) == kLeastMass, "a NaN size: the least mass");
    Check(Mass("cup", Material::Ceramic, Shape::Round, {0, 0, 0}) == kLeastMass, "no size: the least mass");
    Check(Mass("chair", Material::Furniture, Shape::Box, {100, 100, 100}) == 15, "a huge chair: its kind's most");
    Check(Mass("", Material::Stone, Shape::Box, {100, 100, 100}) == 80 && Mass("", Material(200), Shape::Box, {100, 100, 100}) <= kMostMass, "never past the most");
    // The shapes' volumes are the bodies the props world builds.
    Check(Near(ShapeVolume(Shape::Box, {.1f, .2f, .3f}), .048f, 1e-4f), "box volume");
    Check(Near(ShapeVolume(Shape::Round, {.1f, .2f, .1f}), 3 * .1f * .1f * .4f, 1e-4f), "round volume (a 12-gon prism)");
    Check(Near(ShapeVolume(Shape::Ball, {.1f, .1f, .1f}), 4.18879f * .001f, 1e-4f), "ball volume");
    // Every kind of the catalogue weighs something sane at a usual size (25 x 30 x 25 cm).
    for (const auto& k : propscene::Kinds()) {
        const float m = Mass(k.name, k.material, k.shape, {.25f, .3f, .25f});
        if (!(m >= kLeastMass && m <= kMostMass && std::isfinite(m))) { std::printf("FAIL: kind %s weighs %g\n", k.name, m); ++g_fail; }
    }

    // ---- What the hand feels ----
    const float sword = 1.5f + 2.0f;   // the props world's mover: a sword's steel with a hand on it (knock_props.cpp)
    const float maul = 5.0f + 4.0f, fist = 2.0f;
    Check(Resistance(cup, sword) < .1f && Resistance(barrel, sword) > .9f, "a cup takes little of a sword's swing, a barrel almost all");
    Check(Resistance(0, sword) == 0 && Resistance(barrel, 0) == 0 && Resistance(std::numeric_limits<float>::quiet_NaN(), sword) == 0, "unknown masses: nothing");
    // A cup: one light, short tick - a little stronger for a full swing, never under the floor.
    const Feel cupTap = FeelOf(1.5f, cup, sword), cupSwing = FeelOf(5, cup, sword);
    Check(cupTap.thudMs == 0 && cupSwing.thudMs == 0, "a cup: no thud");
    Check(cupTap.knock >= kFloor && cupTap.knock <= cupSwing.knock && cupSwing.knock < .5f, "a cup: a light tick, a little more for a full swing");
    Check(cupSwing.knockMs > 0 && cupSwing.knockMs < 20, "a cup's tick is short");
    // A barrel: a hard knock, then a weaker, longer thud; harder for a full swing, longer for a heavier thing.
    const Feel barrelTap = FeelOf(1.5f, barrel, sword), barrelSwing = FeelOf(5, barrel, sword);
    const Feel sackSwing = FeelOf(5, sack, sword), chairSwing = FeelOf(5, chair, sword);
    Check(barrelSwing.knock > .9f && barrelSwing.thudMs > barrelSwing.knockMs && barrelSwing.thudAfterMs >= barrelSwing.knockMs, "a barrel: a knock, then a thud after it");
    Check(barrelSwing.thud < barrelSwing.knock && barrelSwing.thud >= kFloor, "the thud weaker than the knock, never under the floor");
    Check(barrelTap.knock < barrelSwing.knock && barrelTap.knock > cupSwing.knock, "a barrel tapped: less than swung at, more than any cup");
    Check(chairSwing.thudMs < barrelSwing.thudMs && barrelSwing.thudMs <= sackSwing.thudMs, "the heavier, the longer the thud");
    Check(chairSwing.knock < barrelSwing.knock, "a chair knocks softer than a barrel");
    // A maul feels a barrel less than a fist does: it takes less of the heavier weapon's speed.
    Check(FeelOf(5, barrel, maul).knock < FeelOf(5, barrel, fist).knock, "a maul feels the barrel less than a fist");
    // Nothing to feel: no speed, NaN.
    Check(FeelOf(0, barrel, sword).knockMs == 0 && FeelOf(std::numeric_limits<float>::quiet_NaN(), barrel, sword).knockMs == 0, "no speed: no pulse");
    // An unknown weapon mass: the floor tick (as a light prop).
    const Feel unknown = FeelOf(5, barrel, 0);
    Check(unknown.knock == kFloor && unknown.knockMs > 0, "an unknown weapon: the floor's knock");
    Check(FeelOf(5, 100, sword).thudMs <= 110 && FeelOf(5, 2, sword).thudMs == 40, "the thud 40 .. 110 ms");

    if (g_fail) { std::printf("%d check(s) failed\n", g_fail); return 1; }
    std::printf("unleashed_prop_heft_test: all checks passed\n");
    return 0;
}
