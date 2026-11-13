// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed commit fbb31fb ("Props have weight: masses by kind and size, weapons strike with theirs"), its steamvr/prop_heft.h
// ([interactions] prop_heft), for D2R VR Authentic's [weight] prop_heft: what a prop weighs by its kind and size - the builds, the
// materials' recipes, the named kinds, the shapes' volumes and surfaces and Mass - kept as they are (props:: types read as propscene::'s,
// which have the same order). The weapon's striking mass and push, the bumps and the spell kick are left out: the weight library's hand
// drive is our strike. The hand's tick or thud is remade for our pulses (one amplitude and a duration each, no frequency): Feel.
// This file stays MPL 2.0.
//
#pragma once
// [weight] prop_heft (vr/knock_props.cpp): the level's knockable props (shared/props_scene.h, the catalogue's kinds) get a mass from what they
// are and how big they are - a cup 0.18 kg, a chair 9.8, a barrel 48, a sack of grain 60 - and the hand whose weapon sets one going feels
// which: a light tick for a light thing, a knock and a longer, weaker thud after it for a heavy one. Plain data and arithmetic, tested in
// cleanroom/tests/unleashed_prop_heft_test.cpp.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

#include "props_scene.h"

namespace unleashed::propheft {

using propscene::Material;
using propscene::Shape;
using propscene::Vec;

// ---- What a prop weighs ----
// How a kind is built. A vessel is open at the top with thin walls (cups, vases, urns, pails, baskets); a shell is closed with walls and some
// load inside (barrels, crates); anything else is solid through a share of its shape (books, stools - mostly air -, sacks, stone).
enum class Build : uint8_t { Vessel, Shell, Solid };
struct Recipe {
    Build build = Build::Solid;
    float density = 600;                          // kg/m3 of its stuff
    float fill = 1;                               // Solid: the share of its shape that is stuff
    float wallShare = 0, wallMin = 0, wallMax = 0;   // Vessel/Shell: wall thickness, a share of its narrower half width, within these (m)
    float load = 0;                               // Shell: kg/m3 of what is inside
    float least = .02f, most = 120;               // kg
};
// Each material's usual build (propscene::Material order).
inline constexpr Recipe kMaterialRecipes[propscene::kMaterials]{
    {Build::Vessel, 2000, 1, .05f, .003f, .015f, 0, .05f, 80},   // ceramic: stoneware walls, thicker on a big urn
    {Build::Vessel, 2500, 1, .06f, .0025f, .006f, 0, .05f, 10},  // glass
    {Build::Vessel, 7800, 1, 0, .0015f, .0015f, 0, .1f, 15},     // metal: sheet iron
    {Build::Solid, 600, .6f, 0, 0, 0, 0, .05f, 60},              // wood
    {Build::Solid, 750, .85f, 0, 0, 0, 0, .02f, 10},             // paper: books
    {Build::Solid, 1900, .09f, 0, 0, 0, 0, .02f, 4},             // bone: a skull is mostly hollow
    {Build::Vessel, 400, 1, 0, .008f, .008f, 0, .05f, 8},        // wicker: loose weave
    {Build::Solid, 900, .9f, 0, 0, 0, 0, .02f, 5},               // food
    {Build::Solid, 150, .6f, 0, 0, 0, 0, .02f, 5},               // cloth
    {Build::Solid, 650, .06f, 0, 0, 0, 0, .8f, 20},              // furniture: legs, seat and back, mostly air
    {Build::Solid, 2500, .85f, 0, 0, 0, 0, .1f, 80},             // stone
    {Build::Solid, 700, .7f, 0, 0, 0, 0, .5f, 60},               // a filled sack of grain
};
// Kinds whose name says more than their material (the catalogue's names). A name ending in '*' is a family: any kind it begins (pillow_blue,
// package_brown).
struct Named {
    std::string_view name;
    Recipe recipe;
};
inline constexpr Named kNamedRecipes[]{
    {"barrel", {Build::Shell, 700, 1, .08f, .015f, .03f, 40, 8, 90}},   // oak staves; an empty one is 40 kg, this some load
    {"crate", {Build::Shell, 600, 1, 0, .02f, .02f, 120, 3, 80}},
    {"container", {Build::Shell, 600, 1, 0, .02f, .02f, 120, 3, 80}},
    {"figurine_box", {Build::Shell, 600, 1, 0, .008f, .008f, 200, .2f, 10}},
    {"firewood", {Build::Solid, 600, .55f, 0, 0, 0, 0, .5f, 30}},
    {"firewood_log", {Build::Solid, 600, .7f, 0, 0, 0, 0, .5f, 25}},
    {"plate_stack", {Build::Solid, 2000, .3f, 0, 0, 0, 0, .3f, 15}},
    {"bowl_set", {Build::Solid, 2000, .3f, 0, 0, 0, 0, .3f, 15}},
    {"bowl_stack", {Build::Solid, 2000, .3f, 0, 0, 0, 0, .3f, 15}},
    {"bowl_stack_small", {Build::Solid, 2000, .3f, 0, 0, 0, 0, .2f, 8}},
    {"stack_o_bowls", {Build::Solid, 2000, .3f, 0, 0, 0, 0, .3f, 15}},
    {"fruit_bowl", {Build::Solid, 900, .3f, 0, 0, 0, 0, .3f, 8}},
    {"pan", {Build::Vessel, 7800, 1, 0, .003f, .003f, 0, .3f, 5}},   // cast iron
    {"pail", {Build::Vessel, 7800, 1, 0, .0012f, .0012f, 0, .3f, 6}},
    {"bucket", {Build::Vessel, 7800, 1, 0, .0012f, .0012f, 0, .3f, 6}},
    {"knife", {Build::Solid, 7800, .2f, 0, 0, 0, 0, .03f, .5f}},
    {"tool_knife", {Build::Solid, 7800, .2f, 0, 0, 0, 0, .03f, .5f}},
    {"tool_spoon", {Build::Solid, 7800, .2f, 0, 0, 0, 0, .02f, .3f}},
    {"hammer", {Build::Solid, 7800, .15f, 0, 0, 0, 0, .3f, 2}},
    {"shovel", {Build::Solid, 7800, .04f, 0, 0, 0, 0, 1, 3}},
    {"book_open", {Build::Solid, 750, .5f, 0, 0, 0, 0, .1f, 6}},
    {"book_stack", {Build::Solid, 750, .8f, 0, 0, 0, 0, .5f, 20}},
    {"scrolls", {Build::Solid, 500, .35f, 0, 0, 0, 0, .05f, 4}},
    {"package*", {Build::Solid, 300, .9f, 0, 0, 0, 0, .1f, 20}},
    {"bone", {Build::Solid, 1900, .3f, 0, 0, 0, 0, .03f, 1.5f}},
    {"shinbone", {Build::Solid, 1900, .3f, 0, 0, 0, 0, .03f, 1.5f}},
    {"basket_stack", {Build::Solid, 400, .15f, 0, 0, 0, 0, .3f, 10}},
    {"fish", {Build::Solid, 1000, .5f, 0, 0, 0, 0, .1f, 5}},
    {"pillow*", {Build::Solid, 60, .7f, 0, 0, 0, 0, .2f, 3}},
    {"cloth_folded", {Build::Solid, 350, .8f, 0, 0, 0, 0, .1f, 5}},
    {"crumpled_cloth", {Build::Solid, 120, .5f, 0, 0, 0, 0, .05f, 3}},
    {"rag", {Build::Solid, 200, .5f, 0, 0, 0, 0, .02f, 1}},
    {"chair", {Build::Solid, 650, .06f, 0, 0, 0, 0, 3, 15}},
    {"stool", {Build::Solid, 650, .06f, 0, 0, 0, 0, 1.5f, 8}},
    {"act1_stool", {Build::Solid, 650, .06f, 0, 0, 0, 0, 1.5f, 8}},
    {"barstool", {Build::Solid, 650, .06f, 0, 0, 0, 0, 1.5f, 8}},
    {"hay_bale", {Build::Solid, 160, .9f, 0, 0, 0, 0, 5, 40}},
};
inline Recipe RecipeFor(std::string_view kind, Material material) {
    for (const auto& n : kNamedRecipes) {
        const bool family = !n.name.empty() && n.name.back() == '*';
        const auto stem = family ? n.name.substr(0, n.name.size() - 1) : n.name;
        if (family ? kind.substr(0, stem.size()) == stem : kind == stem) return n.recipe;
    }
    const unsigned m = unsigned(material);
    return m < propscene::kMaterials ? kMaterialRecipes[m] : Recipe{};
}
// The volume of the body built for a prop of these half extents (m): a box, a ball of their mean, or an upright twelve-sided prism inside the
// ellipse of its x and z.
inline float ShapeVolume(Shape shape, Vec half) {
    const float x = std::max(half.x, 0.f), y = std::max(half.y, 0.f), z = std::max(half.z, 0.f);
    switch (shape) {
        case Shape::Box: return 8 * x * y * z;
        case Shape::Ball: { const float r = (x + y + z) / 3; return 4.18879020f * r * r * r; }
        default: return 3 * x * z * 2 * y;   // a regular 12-gon fills 3/pi of its circle
    }
}
// Its outer surface: sides, bottom and (closed) top.
inline float SurfaceArea(Shape shape, Vec half, bool closed) {
    const float x = std::max(half.x, 0.f), y = std::max(half.y, 0.f), z = std::max(half.z, 0.f);
    if (shape == Shape::Box) return 2 * (2 * x + 2 * z) * 2 * y + (closed ? 2 : 1) * 4 * x * z;
    if (shape == Shape::Ball) { const float r = (x + y + z) / 3; return 12.5663706f * r * r; }
    // The ellipse's perimeter (Ramanujan) round its height, and its end(s).
    const float h = (x - z) * (x - z) / std::max((x + z) * (x + z), 1e-12f);
    const float perimeter = 3.14159265f * (x + z) * (1 + 3 * h / (10 + std::sqrt(std::max(4 - 3 * h, 0.f))));
    return perimeter * 2 * y + (closed ? 2 : 1) * 3.14159265f * x * z;
}
inline constexpr float kLeastMass = .02f, kMostMass = 120;   // kg, whatever it is
// What a prop weighs (kg) from its kind's name (the catalogue's, "cup"; empty: its material's usual build), its material and shape, and its size:
// the full extents of its bounds in metres.
inline float Mass(std::string_view kind, Material material, Shape shape, Vec size) {
    if (!propscene::Finite(size) || !(size.x > 0 && size.y > 0 && size.z > 0)) return kLeastMass;
    const Vec half{std::max(size.x, 0.f) * .5f, std::max(size.y, 0.f) * .5f, std::max(size.z, 0.f) * .5f};
    const auto r = RecipeFor(kind, material);
    const float volume = ShapeVolume(shape, half), solid = r.density * volume;
    float mass = solid * r.fill;
    if (r.build != Build::Solid) {
        const float narrow = std::min(half.x, half.z);
        const float wall = std::clamp(narrow * r.wallShare, r.wallMin, std::max(r.wallMin, r.wallMax));
        const bool closed = r.build == Build::Shell;
        // Never more than the solid it is the skin of (a plate is thinner than two walls).
        mass = std::min(solid, r.density * wall * SurfaceArea(shape, half, closed)) + (closed ? r.load * volume : 0.f);
    }
    if (!std::isfinite(mass)) return kLeastMass;
    return std::clamp(std::clamp(mass, r.least, std::max(r.least, r.most)), kLeastMass, kMostMass);
}

// ---- What the hand feels ----
// The share of the weapon's own speed a prop of mass `prop` takes from a weapon of mass `weapon` (kg; ours: the steel with the hands on it, as
// the weight library's mover has it): what the hand should feel - a cup off a sword nearly nothing, a barrel almost all. Unknown masses: 0.
inline float Resistance(float prop, float weapon) {
    if (!(prop > 0) || !(weapon > 0) || !std::isfinite(prop) || !std::isfinite(weapon)) return 0;
    return prop / (prop + weapon);
}
inline constexpr float kFullSwing = 4;   // m/s: the weapon's speed of a full swing
inline constexpr float kHeavyProp = 2;   // kg: from here a thud follows the knock
// The pulse of a weapon setting a prop going at `speed` (m/s), as two of our pulses (haptics::Pulse: an amplitude 0..1 of the knock pulse's own
// strength, [weight] knock_haptic_pct, and a duration): the knock, and for a prop of kHeavyProp or more a weaker, longer thud `thudAfterMs` later
// (longer the heavier, 2 kg .. 64 kg). The knock is as strong as the share of the swing the prop took, never under the floor every knock had
// (0.4: a weak motor shows no gentler pulse); a light thing a short tick. thudMs 0: no thud; knockMs 0: no pulse at all (no speed).
struct Feel {
    float knock = 0;
    int knockMs = 0;
    float thud = 0;
    int thudMs = 0;
    int thudAfterMs = 0;
};
inline constexpr float kFloor = .4f;
inline Feel FeelOf(float speed, float prop, float weapon) {
    Feel f;
    if (!std::isfinite(speed) || speed <= 0) return f;
    const float resist = Resistance(prop, weapon);
    f.knock = std::clamp(kFloor + (1 - kFloor) * resist * std::clamp(speed / kFullSwing, 0.f, 1.f), kFloor, 1.f);
    if (!(prop >= kHeavyProp)) {
        f.knockMs = int(15 + 10 * resist);
        return f;
    }
    const float heavy = std::clamp(std::log2(prop / kHeavyProp) / 5, 0.f, 1.f);   // 2 kg 0 .. 64 kg 1
    f.knockMs = 25;
    f.thud = std::max(kFloor, .6f * f.knock);
    f.thudMs = int(40 + 70 * heavy);
    f.thudAfterMs = 30;
    return f;
}

}  // namespace unleashed::propheft
