// weapon_extents.h - how far each base weapon's own model runs along its long axis: the weapon as the game DREW it, per item code.
//
// Where the numbers come from. The weapon models are Granny GR2 files (data\hd\items\weapon\<kind>\<name>\<name>_lod0.model); every mesh in
// one carries its bounding box (bbMin / bbMax, in the model's own units) in its ExtendedData. These rows are the Y part of that box, read offline
// from the game's own files and joined to the base item code through the game's own items.json (code -> asset): one pair per item, and nothing else
// of the files is kept here (the elite and exceptional versions share their base's model, so they share its row). The katars and the claws lie along the
// model's Z, not its Y (axis 2: the Assassin's katar's blade is the bone's yellow axis; `lo` of those rows is where the grip ends and the blade starts,
// 0.5 units - the mesh's profile along Z: the wide grip body ends there - so the capsule runs from the knuckles to the point). The fist weapons whose
// blades spread sideways (Hatchet Hands, Cestus, War Fist) and the flails (the chain has its own logic, vr/flail_chain.h) have no row.
//
// The frame. The model's origin is where the game hangs the item - the weapon attach bone's origin, the very point the touch's blade is built
// from (vr/blade_shape.h) - and the model's Y is that bone's Y: a javelin's mesh runs 3.859 units along -Y (its point) and 2.595 along +Y, and
// the drawn javelin's point was measured 1.04 m from the bone on screenshots with the capsule drawn: 3.859 units over the hero's 3.65 units
// per metre is 1.057 m. (A mesh's own bone sits elsewhere - the spear's 2.96 units up its shaft - and is NOT the origin: the screenshot of a
// spear with the capsule drawn puts the spearhead 2.1-2.4 m ahead of the hand, which is the mesh's 7.585 units, not the 4.62 of the bone's frame.)
// `lo` and `hi` are the lowest and highest Y of the mesh, in units of the model, which is 1 / 3.28084 m in the game's own files - the metre
// of the render is the hero's: the unit in metres is the weapon bone's scale over the hero model's units per metre (skeletons.cpp, PosedBlade::unitM).
//
// Which side the point is on. The weapons are authored with the point along +Y, except those whose animation class (Weapons.txt wclass) is 1ht
// (one-hand thrust: the daggers, throwing knives and javelins), authored along -Y: `tipSign` is -1 for those, +1 for all else. The model says it:
// the dagger's blade and the javelin's head are the long side toward -Y; a spear (2ht), a staff and a pole-arm (stf) run their heads toward +Y
// (the spear's crossbar and leaf at +4.6 .. +2.3 of its bone, the voulge's blade at the top), and the capture of a spear with its capsule drawn
// has the capsule's long side (the "tip" of the first, wrong guess of -Y) running toward the user, off the picture.
#pragma once

#include <cmath>
#include <cstdint>

namespace weaponextent {

constexpr uint32_t C(char a, char b, char c, char d) {
    return static_cast<uint32_t>(static_cast<uint8_t>(a)) | static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8 |
           static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16 | static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24;
}

struct Row {
    uint32_t code;   // the base item's code, four characters ('sst ': space padded)
    float lo, hi;    // the mesh's lowest and highest extent along the weapon's long axis, model units
    int tipSign;     // +1: the point at +axis (the end `hi`); -1: at -axis (the end `lo`)
    int axis;        // the bone's axis the weapon lies along: 1 = Y (nearly everything), 2 = Z (the katars and claws: yellow in the debug)
};

// Sorted by asset, then code. Written by a script from the game's own files; not edited by hand.
inline constexpr Row kRows[] = {
    {C('7', 'a', 'x', ' '), -0.344f, 1.993f, 1, 1},  // axe/axe: Small Crescent
    {C('9', 'a', 'x', ' '), -0.344f, 1.993f, 1, 1},  // axe/axe: Cleaver
    {C('a', 'x', 'e', ' '), -0.344f, 1.993f, 1, 1},  // axe/axe: Axe
    {C('7', 'b', '8', ' '), -0.490f, 1.914f, 1, 1},  // axe/balanced_axe: Winged Axe
    {C('9', 'b', '8', ' '), -0.490f, 1.914f, 1, 1},  // axe/balanced_axe: Hurlbat
    {C('b', 'a', 'l', ' '), -0.490f, 1.914f, 1, 1},  // axe/balanced_axe: Balanced Axe
    {C('7', 'b', 't', ' '), -1.893f, 2.063f, 1, 1},  // axe/battle_axe: Decapitator
    {C('9', 'b', 't', ' '), -1.893f, 2.063f, 1, 1},  // axe/battle_axe: Tabar
    {C('b', 't', 'x', ' '), -1.893f, 2.063f, 1, 1},  // axe/battle_axe: Battle Axe
    {C('7', 'b', 'a', ' '), -1.913f, 1.269f, 1, 1},  // axe/broad_axe: Silver-edged Axe
    {C('9', 'b', 'a', ' '), -1.913f, 1.269f, 1, 1},  // axe/broad_axe: Bearded Axe 
    {C('b', 'a', 'x', ' '), -1.913f, 1.269f, 1, 1},  // axe/broad_axe: Broad Axe 
    {C('2', 'a', 'x', ' '), -0.643f, 1.645f, 1, 1},  // axe/double_axe: Double Axe
    {C('7', '2', 'a', ' '), -0.643f, 1.645f, 1, 1},  // axe/double_axe: Ettin Axe
    {C('9', '2', 'a', ' '), -0.643f, 1.645f, 1, 1},  // axe/double_axe: Twin Axe
    {C('7', 'g', 'i', ' '), -2.559f, 2.085f, 1, 1},  // axe/giant_axe: Glorious Axe
    {C('9', 'g', 'i', ' '), -2.559f, 2.085f, 1, 1},  // axe/giant_axe: Ancient Axe
    {C('g', 'i', 'x', ' '), -2.559f, 2.085f, 1, 1},  // axe/giant_axe: Giant Axe
    {C('7', 'g', 'a', ' '), -2.262f, 3.049f, 1, 1},  // axe/great_axe: Champion Axe
    {C('9', 'g', 'a', ' '), -2.262f, 3.049f, 1, 1},  // axe/great_axe: Gothic Axe
    {C('g', 'a', 'x', ' '), -2.262f, 3.049f, 1, 1},  // axe/great_axe: Great Axe
    {C('7', 'h', 'a', ' '), -0.282f, 1.772f, 1, 1},  // axe/hand_axe: Tomahawk
    {C('9', 'h', 'a', ' '), -0.282f, 1.772f, 1, 1},  // axe/hand_axe: Hatchet
    {C('h', 'a', 'x', ' '), -0.282f, 1.772f, 1, 1},  // axe/hand_axe: Hand Axe
    {C('7', 'l', 'a', ' '), -1.927f, 1.771f, 1, 1},  // axe/large_axe: Feral Axe
    {C('9', 'l', 'a', ' '), -1.927f, 1.771f, 1, 1},  // axe/large_axe: Military Axe
    {C('l', 'a', 'x', ' '), -1.927f, 1.771f, 1, 1},  // axe/large_axe: Large Axe
    {C('7', 'm', 'p', ' '), -1.421f, 1.879f, 1, 1},  // axe/military_pick: War Spike
    {C('9', 'm', 'p', ' '), -1.421f, 1.879f, 1, 1},  // axe/military_pick: Crowbill
    {C('m', 'p', 'i', ' '), -1.421f, 1.879f, 1, 1},  // axe/military_pick: Military Pick
    {C('7', 't', 'a', ' '), -0.486f, 1.924f, 1, 1},  // axe/throwing_axe: Flying Axe
    {C('9', 't', 'a', ' '), -0.486f, 1.924f, 1, 1},  // axe/throwing_axe: Francisca
    {C('t', 'a', 'x', ' '), -0.486f, 1.924f, 1, 1},  // axe/throwing_axe: Throwing Axe
    {C('7', 'w', 'a', ' '), -0.577f, 1.652f, 1, 1},  // axe/war_axe: Berserker Axe
    {C('9', 'w', 'a', ' '), -0.577f, 1.652f, 1, 1},  // axe/war_axe: Naga
    {C('w', 'a', 'x', ' '), -0.577f, 1.652f, 1, 1},  // axe/war_axe: War Axe
    {C('7', 'c', 'l', ' '), -0.493f, 2.062f, 1, 1},  // club/club: Truncheon
    {C('9', 'c', 'l', ' '), -0.493f, 2.062f, 1, 1},  // club/club: Cudgel
    {C('c', 'l', 'b', ' '), -0.493f, 2.062f, 1, 1},  // club/club: Club
    {C('7', 's', 'p', ' '), -0.376f, 1.724f, 1, 1},  // club/spiked_club: Tyrant Club
    {C('9', 's', 'p', ' '), -0.376f, 1.724f, 1, 1},  // club/spiked_club: Barbed Club
    {C('s', 'p', 'c', ' '), -0.376f, 1.724f, 1, 1},  // club/spiked_club: Spiked Club
    {C('l', 'e', 'g', ' '), -0.463f, 1.367f, 1, 1},  // club/wirts_leg: Wirt's Leg
    {C('7', 'g', 'm', ' '), -2.247f, 2.184f, 1, 1},  // hammer/great_maul: Thunder Maul
    {C('9', 'g', 'm', ' '), -2.247f, 2.184f, 1, 1},  // hammer/great_maul: Martel de Fer
    {C('g', 'm', 'a', ' '), -2.247f, 2.184f, 1, 1},  // hammer/great_maul: Great Maul
    {C('h', 'f', 'h', ' '), -0.781f, 1.664f, 1, 1},  // hammer/hellforge_hammer: Hellforge Hammer
    {C('h', 'd', 'm', ' '), -0.477f, 1.622f, 1, 1},  // hammer/horadric_malus: Horadric Malus
    {C('7', 'm', '7', ' '), -2.623f, 1.858f, 1, 1},  // hammer/maul: Ogre Maul
    {C('9', 'm', '9', ' '), -2.623f, 1.858f, 1, 1},  // hammer/maul: War Club
    {C('m', 'a', 'u', ' '), -2.623f, 1.858f, 1, 1},  // hammer/maul: Maul
    {C('7', 'w', 'h', ' '), -1.000f, 2.408f, 1, 1},  // hammer/war_hammer: Legendary Mallet
    {C('9', 'w', 'h', ' '), -1.000f, 2.408f, 1, 1},  // hammer/war_hammer: Battle Hammer
    {C('w', 'h', 'm', ' '), -1.000f, 2.408f, 1, 1},  // hammer/war_hammer: War Hammer
    {C('7', 'g', 'l', ' '), -4.324f, 3.357f, -1, 1},  // javelin/glaive: Ghost Glaive
    {C('9', 'g', 'l', ' '), -4.324f, 3.357f, -1, 1},  // javelin/glaive: Spiculum
    {C('g', 'l', 'v', ' '), -4.324f, 3.357f, -1, 1},  // javelin/glaive: Glaive
    {C('7', 'j', 'a', ' '), -3.859f, 2.595f, -1, 1},  // javelin/javelin: Hyperion Javelin
    {C('9', 'j', 'a', ' '), -3.859f, 2.595f, -1, 1},  // javelin/javelin: War Javelin
    {C('j', 'a', 'v', ' '), -3.859f, 2.595f, -1, 1},  // javelin/javelin: Javelin
    {C('a', 'm', '5', ' '), -5.255f, 3.476f, -1, 1},  // javelin/maiden_javelin: Maiden Javelin
    {C('a', 'm', 'a', ' '), -5.255f, 3.476f, -1, 1},  // javelin/maiden_javelin: Ceremonial Javelin
    {C('a', 'm', 'f', ' '), -5.255f, 3.476f, -1, 1},  // javelin/maiden_javelin: Matriarchal Javelin
    {C('7', 'p', 'i', ' '), -4.168f, 3.250f, -1, 1},  // javelin/pilum: Stygian Pilum
    {C('9', 'p', 'i', ' '), -4.168f, 3.250f, -1, 1},  // javelin/pilum: Great Pilum
    {C('p', 'i', 'l', ' '), -4.168f, 3.250f, -1, 1},  // javelin/pilum: Pilum
    {C('7', 's', '7', ' '), -3.568f, 2.800f, -1, 1},  // javelin/short_spear: Balrog Spear
    {C('9', 's', '9', ' '), -3.568f, 2.800f, -1, 1},  // javelin/short_spear: Simbilan
    {C('s', 's', 'p', ' '), -3.568f, 2.800f, -1, 1},  // javelin/short_spear: Short Spear
    {C('7', 't', 's', ' '), -3.721f, 3.350f, -1, 1},  // javelin/throwing_spear: Winged Harpoon
    {C('9', 't', 's', ' '), -3.721f, 3.350f, -1, 1},  // javelin/throwing_spear: Harpoon
    {C('t', 's', 'p', ' '), -3.721f, 3.350f, -1, 1},  // javelin/throwing_spear: Throwing Spear
    {C('7', 'b', 'k', ' '), -1.055f, 0.734f, -1, 1},  // knife/balanced_knife: Winged Knife
    {C('9', 'b', 'k', ' '), -1.055f, 0.734f, -1, 1},  // knife/balanced_knife: War Dart
    {C('b', 'k', 'f', ' '), -1.055f, 0.734f, -1, 1},  // knife/balanced_knife: Balanced Knife
    {C('7', 'b', 'l', ' '), -1.980f, 0.191f, -1, 1},  // knife/blade: Legend Spike
    {C('9', 'b', 'l', ' '), -1.980f, 0.191f, -1, 1},  // knife/blade: Stilleto
    {C('b', 'l', 'd', ' '), -1.980f, 0.191f, -1, 1},  // knife/blade: Blade
    {C('7', 'd', 'g', ' '), -1.515f, 0.179f, -1, 1},  // knife/dagger: Bone Knife
    {C('9', 'd', 'g', ' '), -1.515f, 0.179f, -1, 1},  // knife/dagger: Poignard
    {C('d', 'g', 'r', ' '), -1.515f, 0.179f, -1, 1},  // knife/dagger: Dagger
    {C('d', '3', '3', ' '), -1.916f, 0.373f, -1, 1},  // knife/decoy_dagger: Decoy Gidbinn
    {C('7', 'd', 'i', ' '), -1.512f, 0.201f, -1, 1},  // knife/dirk: Mithral Point
    {C('9', 'd', 'i', ' '), -1.512f, 0.201f, -1, 1},  // knife/dirk: Rondel
    {C('d', 'i', 'r', ' '), -1.512f, 0.201f, -1, 1},  // knife/dirk: Dirk
    {C('g', '3', '3', ' '), -1.910f, 0.437f, -1, 1},  // knife/gidbinn: The Gidbinn
    {C('7', 'k', 'r', ' '), -2.225f, 0.215f, -1, 1},  // knife/kriss: Fanged Knife
    {C('9', 'k', 'r', ' '), -2.225f, 0.215f, -1, 1},  // knife/kriss: Cinquedeas
    {C('k', 'r', 'i', ' '), -2.225f, 0.215f, -1, 1},  // knife/kriss: Kriss
    {C('7', 't', 'k', ' '), -1.804f, 0.411f, -1, 1},  // knife/throwing_knife: Flying Knife
    {C('9', 't', 'k', ' '), -1.804f, 0.411f, -1, 1},  // knife/throwing_knife: Battle Dart
    {C('t', 'k', 'f', ' '), -1.804f, 0.411f, -1, 1},  // knife/throwing_knife: Throwing Knife
    {C('7', 'm', 'a', ' '), -0.409f, 2.267f, 1, 1},  // mace/mace: Reinforced Mace
    {C('9', 'm', 'a', ' '), -0.409f, 2.267f, 1, 1},  // mace/mace: Flanged Mace
    {C('m', 'a', 'c', ' '), -0.409f, 2.267f, 1, 1},  // mace/mace: Mace
    {C('7', 'm', 't', ' '), -0.432f, 3.055f, 1, 1},  // mace/morning_star: Devil Star
    {C('9', 'm', 't', ' '), -0.432f, 3.055f, 1, 1},  // mace/morning_star: Jagged Star
    {C('m', 's', 't', ' '), -0.432f, 3.055f, 1, 1},  // mace/morning_star: Morning Star
    {C('o', 'b', '4', ' '), -0.432f, 1.822f, 1, 1},  // orb/clasped_orb: Clasped Orb
    {C('o', 'b', '9', ' '), -0.432f, 1.822f, 1, 1},  // orb/clasped_orb: Sparkling Ball
    {C('o', 'b', 'e', ' '), -0.432f, 1.822f, 1, 1},  // orb/clasped_orb: Vortex Orb
    {C('o', 'b', '5', ' '), -0.722f, 2.033f, 1, 1},  // orb/dragon_stone: Jared's Stone
    {C('o', 'b', 'a', ' '), -0.722f, 2.033f, 1, 1},  // orb/dragon_stone: Swirling Crystal
    {C('o', 'b', 'f', ' '), -0.722f, 2.033f, 1, 1},  // orb/dragon_stone: Dimensional Shard
    {C('o', 'b', '1', ' '), -0.793f, 1.661f, 1, 1},  // orb/eagle_orb: Eagle Orb
    {C('o', 'b', '6', ' '), -0.793f, 1.661f, 1, 1},  // orb/eagle_orb: Glowing Orb
    {C('o', 'b', 'b', ' '), -0.793f, 1.661f, 1, 1},  // orb/eagle_orb: Heavenly Stone
    {C('o', 'b', '2', ' '), -0.777f, 1.803f, 1, 1},  // orb/sacred_globe: Sacred Globe
    {C('o', 'b', '7', ' '), -0.777f, 1.803f, 1, 1},  // orb/sacred_globe: Crystalline Globe
    {C('o', 'b', 'c', ' '), -0.777f, 1.803f, 1, 1},  // orb/sacred_globe: Eldritch Orb
    {C('o', 'b', '3', ' '), -0.509f, 2.111f, 1, 1},  // orb/smoked_sphere: Smoked Sphere
    {C('o', 'b', '8', ' '), -0.509f, 2.111f, 1, 1},  // orb/smoked_sphere: Cloudy Sphere
    {C('o', 'b', 'd', ' '), -0.509f, 2.111f, 1, 1},  // orb/smoked_sphere: Demon Heart
    {C('7', 'o', '7', ' '), -3.171f, 4.214f, 1, 1},  // polearm/bardiche: Ogre Axe
    {C('9', 'b', '7', ' '), -3.171f, 4.214f, 1, 1},  // polearm/bardiche: Lochaber Axe
    {C('b', 'a', 'r', ' '), -3.171f, 4.214f, 1, 1},  // polearm/bardiche: Bardiche
    {C('7', 'h', '7', ' '), -3.820f, 4.943f, 1, 1},  // polearm/halberd: Great Poleaxe
    {C('9', 'h', '9', ' '), -3.820f, 4.943f, 1, 1},  // polearm/halberd: Bec-de-Corbin
    {C('h', 'a', 'l', ' '), -3.820f, 4.943f, 1, 1},  // polearm/halberd: Halberd
    {C('7', 'p', 'a', ' '), -3.173f, 3.165f, 1, 1},  // polearm/poleaxe: Cryptic Axe
    {C('9', 'p', 'a', ' '), -3.173f, 3.165f, 1, 1},  // polearm/poleaxe: Partizan
    {C('p', 'a', 'x', ' '), -3.173f, 3.165f, 1, 1},  // polearm/poleaxe: Poleaxe
    {C('7', 's', '8', ' '), -2.352f, 3.356f, 1, 1},  // polearm/scythe: Thresher
    {C('9', 's', '8', ' '), -2.352f, 3.356f, 1, 1},  // polearm/scythe: Battle Scythe
    {C('s', 'c', 'y', ' '), -2.352f, 3.356f, 1, 1},  // polearm/scythe: Scythe
    {C('7', 'v', 'o', ' '), -2.982f, 3.640f, 1, 1},  // polearm/voulge: Colossus Voulge
    {C('9', 'v', 'o', ' '), -2.982f, 3.640f, 1, 1},  // polearm/voulge: Bill
    {C('v', 'o', 'u', ' '), -2.982f, 3.640f, 1, 1},  // polearm/voulge: Voulge
    {C('7', 'w', 'c', ' '), -3.589f, 5.622f, 1, 1},  // polearm/war_scythe: Giant Thresher
    {C('9', 'w', 'c', ' '), -3.589f, 5.622f, 1, 1},  // polearm/war_scythe: Grim Scythe
    {C('w', 's', 'c', ' '), -3.589f, 5.622f, 1, 1},  // polearm/war_scythe: War Scythe
    {C('7', 'q', 's', ' '), -0.632f, 1.964f, 1, 1},  // scepter/grand_scepter: Seraph Rod
    {C('9', 'q', 's', ' '), -0.632f, 1.964f, 1, 1},  // scepter/grand_scepter: Holy Water Sprinkler
    {C('g', 's', 'c', ' '), -0.632f, 1.964f, 1, 1},  // scepter/grand_scepter: Grand Scepter
    {C('7', 's', 'c', ' '), -0.502f, 1.982f, 1, 1},  // scepter/scepter: Mighty Scepter
    {C('9', 's', 'c', ' '), -0.502f, 1.982f, 1, 1},  // scepter/scepter: Rune Scepter
    {C('s', 'c', 'p', ' '), -0.502f, 1.982f, 1, 1},  // scepter/scepter: Scepter
    {C('7', 'w', 's', ' '), -1.128f, 1.820f, 1, 1},  // scepter/war_scepter: Caduceus
    {C('9', 'w', 's', ' '), -1.128f, 1.820f, 1, 1},  // scepter/war_scepter: Divine Scepter
    {C('w', 's', 'p', ' '), -1.128f, 1.820f, 1, 1},  // scepter/war_scepter: War Scepter
    {C('7', 'b', 'r', ' '), -2.619f, 7.620f, 1, 1},  // spear/brandistock: Mancatcher
    {C('9', 'b', 'r', ' '), -2.619f, 7.620f, 1, 1},  // spear/brandistock: War Fork
    {C('b', 'r', 'n', ' '), -2.619f, 7.620f, 1, 1},  // spear/brandistock: Brandistock
    {C('a', 'm', '4', ' '), -2.534f, 7.113f, 1, 1},  // spear/maiden_pike: Maiden Pike
    {C('a', 'm', '9', ' '), -2.534f, 7.113f, 1, 1},  // spear/maiden_pike: Ceremonial Pike
    {C('a', 'm', 'e', ' '), -2.534f, 7.113f, 1, 1},  // spear/maiden_pike: Matriarchal Pike
    {C('a', 'm', '3', ' '), -2.608f, 7.374f, 1, 1},  // spear/maiden_spear: Maiden Spear
    {C('a', 'm', '8', ' '), -2.608f, 7.374f, 1, 1},  // spear/maiden_spear: Ceremonial Spear
    {C('a', 'm', 'd', ' '), -2.608f, 7.374f, 1, 1},  // spear/maiden_spear: Matriarchal Spear
    {C('7', 'p', '7', ' '), -2.582f, 6.691f, 1, 1},  // spear/pike: War Pike
    {C('9', 'p', '9', ' '), -2.582f, 6.691f, 1, 1},  // spear/pike: Lance
    {C('p', 'i', 'k', ' '), -2.582f, 6.691f, 1, 1},  // spear/pike: Pike
    {C('7', 's', 'r', ' '), -2.603f, 7.585f, 1, 1},  // spear/spear: Hyperion Spear
    {C('9', 's', 'r', ' '), -2.603f, 7.585f, 1, 1},  // spear/spear: War Spear
    {C('s', 'p', 'r', ' '), -2.603f, 7.585f, 1, 1},  // spear/spear: Spear
    {C('7', 's', 't', ' '), -2.594f, 6.215f, 1, 1},  // spear/spetum: Ghost Spear
    {C('9', 's', 't', ' '), -2.594f, 6.215f, 1, 1},  // spear/spetum: Yari
    {C('s', 'p', 't', ' '), -2.594f, 6.215f, 1, 1},  // spear/spetum: Spetum
    {C('7', 't', 'r', ' '), -2.263f, 5.179f, 1, 1},  // spear/trident: Stygian Pike
    {C('9', 't', 'r', ' '), -2.263f, 5.179f, 1, 1},  // spear/trident: Fuscina
    {C('t', 'r', 'i', ' '), -2.263f, 5.179f, 1, 1},  // spear/trident: Trident
    {C('6', 'b', 's', ' '), -3.424f, 3.061f, 1, 1},  // staff/battle_staff: Shillelagh
    {C('8', 'b', 's', ' '), -3.424f, 3.061f, 1, 1},  // staff/battle_staff: Gothic Staff
    {C('b', 's', 't', ' '), -3.424f, 3.061f, 1, 1},  // staff/battle_staff: Battle Staff
    {C('6', 'c', 's', ' '), -3.214f, 2.291f, 1, 1},  // staff/gnarled_staff: Elder Staff
    {C('8', 'c', 's', ' '), -3.214f, 2.291f, 1, 1},  // staff/gnarled_staff: Cedar Staff
    {C('c', 's', 't', ' '), -3.214f, 2.291f, 1, 1},  // staff/gnarled_staff: Gnarled Staff
    {C('h', 's', 't', ' '), -2.865f, 3.602f, 1, 1},  // staff/horadric_staff: Horadric Staff
    {C('6', 'l', 's', ' '), -3.710f, 3.378f, 1, 1},  // staff/long_staff: Stalagmite
    {C('8', 'l', 's', ' '), -3.710f, 3.378f, 1, 1},  // staff/long_staff: Quarterstaff
    {C('l', 's', 't', ' '), -3.710f, 3.378f, 1, 1},  // staff/long_staff: Long Staff
    {C('6', 's', 's', ' '), -3.511f, 2.734f, 1, 1},  // staff/short_staff: Walking Stick
    {C('8', 's', 's', ' '), -3.511f, 2.734f, 1, 1},  // staff/short_staff: Jo Staff
    {C('s', 's', 't', ' '), -3.511f, 2.734f, 1, 1},  // staff/short_staff: Short Staff
    {C('m', 's', 'f', ' '), -3.376f, 2.716f, 1, 1},  // staff/staff_of_the_kings: Staff of Kings
    {C('6', 'w', 's', ' '), -2.684f, 3.209f, 1, 1},  // staff/war_staff: Archon Staff
    {C('8', 'w', 's', ' '), -2.684f, 3.209f, 1, 1},  // staff/war_staff: Rune Staff
    {C('w', 's', 't', ' '), -2.684f, 3.209f, 1, 1},  // staff/war_staff: War Staff
    {C('7', 'b', '7', ' '), -0.800f, 4.367f, 1, 1},  // sword/bastard_sword: Champion Sword
    {C('9', 'b', '9', ' '), -0.800f, 4.367f, 1, 1},  // sword/bastard_sword: Gothic Sword
    {C('b', 's', 'w', ' '), -0.800f, 4.367f, 1, 1},  // sword/bastard_sword: Bastard Sword
    {C('7', 'b', 's', ' '), -0.403f, 3.081f, 1, 1},  // sword/broad_sword: Conquest Sword
    {C('9', 'b', 's', ' '), -0.403f, 3.081f, 1, 1},  // sword/broad_sword: Battle Sword
    {C('b', 's', 'd', ' '), -0.403f, 3.081f, 1, 1},  // sword/broad_sword: Broad Sword
    {C('7', 'c', 'm', ' '), -1.077f, 4.939f, 1, 1},  // sword/claymore: Highland Blade
    {C('9', 'c', 'm', ' '), -1.077f, 4.939f, 1, 1},  // sword/claymore: Dacian Falx
    {C('c', 'l', 'm', ' '), -1.077f, 4.939f, 1, 1},  // sword/claymore: Claymore
    {C('7', 'c', 'r', ' '), -0.511f, 3.470f, 1, 1},  // sword/crystal_sword: Phase Blade
    {C('9', 'c', 'r', ' '), -0.511f, 3.470f, 1, 1},  // sword/crystal_sword: Dimensional Blade
    {C('c', 'r', 's', ' '), -0.511f, 3.470f, 1, 1},  // sword/crystal_sword: Crystal Sword
    {C('7', 'f', 'c', ' '), -0.462f, 2.743f, 1, 1},  // sword/falchion: Hydra Edge
    {C('9', 'f', 'c', ' '), -0.462f, 2.743f, 1, 1},  // sword/falchion: Tulwar
    {C('f', 'l', 'c', ' '), -0.462f, 2.743f, 1, 1},  // sword/falchion: Falchion
    {C('7', 'f', 'b', ' '), -1.205f, 4.491f, 1, 1},  // sword/flamberge: Colossal Sword
    {C('9', 'f', 'b', ' '), -1.205f, 4.491f, 1, 1},  // sword/flamberge: Zweihander
    {C('f', 'l', 'b', ' '), -1.205f, 4.491f, 1, 1},  // sword/flamberge: Flamberge
    {C('7', 'g', 's', ' '), -0.828f, 4.139f, 1, 1},  // sword/giant_sword: Balrog Blade
    {C('9', 'g', 's', ' '), -0.828f, 4.139f, 1, 1},  // sword/giant_sword: Tusk Sword
    {C('g', 'i', 's', ' '), -0.828f, 4.139f, 1, 1},  // sword/giant_sword: Giant Sword
    {C('7', 'g', 'd', ' '), -1.227f, 4.738f, 1, 1},  // sword/great_sword: Colossus Blade
    {C('9', 'g', 'd', ' '), -1.227f, 4.738f, 1, 1},  // sword/great_sword: Executioner Sword
    {C('g', 's', 'd', ' '), -1.227f, 4.738f, 1, 1},  // sword/great_sword: Great Sword
    {C('7', 'l', 's', ' '), -0.698f, 3.836f, 1, 1},  // sword/long_sword: Cryptic Sword
    {C('9', 'l', 's', ' '), -0.698f, 3.836f, 1, 1},  // sword/long_sword: Rune Sword
    {C('l', 's', 'd', ' '), -0.698f, 3.836f, 1, 1},  // sword/long_sword: Long Sword 
    {C('7', 's', 'b', ' '), -0.527f, 3.844f, 1, 1},  // sword/saber: Elegant Blade
    {C('9', 's', 'b', ' '), -0.527f, 3.844f, 1, 1},  // sword/saber: Shamshir
    {C('s', 'b', 'r', ' '), -0.527f, 3.844f, 1, 1},  // sword/saber: Saber
    {C('7', 's', 'm', ' '), -0.513f, 3.023f, 1, 1},  // sword/scimitar: Ataghan
    {C('9', 's', 'm', ' '), -0.513f, 3.023f, 1, 1},  // sword/scimitar: Cutlass
    {C('s', 'c', 'm', ' '), -0.513f, 3.023f, 1, 1},  // sword/scimitar: Scimitar
    {C('7', 's', 's', ' '), -0.521f, 2.614f, 1, 1},  // sword/short_sword: Falcata
    {C('9', 's', 's', ' '), -0.521f, 2.614f, 1, 1},  // sword/short_sword: Gladius
    {C('s', 's', 'd', ' '), -0.521f, 2.614f, 1, 1},  // sword/short_sword: Short Sword
    {C('2', 'h', 's', ' '), -0.830f, 4.505f, 1, 1},  // sword/two_handed_sword: Two-Handed Sword
    {C('7', '2', 'h', ' '), -0.830f, 4.505f, 1, 1},  // sword/two_handed_sword: Legend Sword
    {C('9', '2', 'h', ' '), -0.830f, 4.505f, 1, 1},  // sword/two_handed_sword: Espandon
    {C('7', 'w', 'd', ' '), -0.402f, 2.231f, 1, 1},  // sword/war_sword: Mythical Sword
    {C('9', 'w', 'd', ' '), -0.402f, 2.231f, 1, 1},  // sword/war_sword: Ancient Sword
    {C('w', 's', 'd', ' '), -0.402f, 2.231f, 1, 1},  // sword/war_sword: War Sword
    {C('7', 'b', 'w', ' '), -0.488f, 1.506f, 1, 1},  // wand/bone_wand: Lich Wand
    {C('9', 'b', 'w', ' '), -0.488f, 1.506f, 1, 1},  // wand/bone_wand: Tomb Wand
    {C('b', 'w', 'n', ' '), -0.488f, 1.506f, 1, 1},  // wand/bone_wand: Bone Wand
    {C('7', 'g', 'w', ' '), -0.278f, 1.734f, 1, 1},  // wand/grim_wand: Unearthed Wand
    {C('9', 'g', 'w', ' '), -0.278f, 1.734f, 1, 1},  // wand/grim_wand: Grave Wand
    {C('g', 'w', 'n', ' '), -0.278f, 1.734f, 1, 1},  // wand/grim_wand: Grim Wand
    {C('7', 'w', 'n', ' '), -0.763f, 1.770f, 1, 1},  // wand/wand: Polished Wand
    {C('9', 'w', 'n', ' '), -0.763f, 1.770f, 1, 1},  // wand/wand: Burnt Wand
    {C('w', 'n', 'd', ' '), -0.763f, 1.770f, 1, 1},  // wand/wand: Wand
    {C('7', 'y', 'w', ' '), -0.975f, 2.107f, 1, 1},  // wand/yew_wand: Ghost Wand
    {C('9', 'y', 'w', ' '), -0.975f, 2.107f, 1, 1},  // wand/yew_wand: Petrified Wand
    {C('y', 'w', 'n', ' '), -0.975f, 2.107f, 1, 1},  // wand/yew_wand: Yew Wand
    {C('7', 'l', 'w', ' '), 0.500f, 1.428f, 1, 2},  // h2h/claws: Feral Claws
    {C('7', 't', 'w', ' '), 0.500f, 1.428f, 1, 2},  // h2h/claws: Runic Talons
    {C('9', 'l', 'w', ' '), 0.500f, 1.428f, 1, 2},  // h2h/claws: Greater Claws
    {C('9', 't', 'w', ' '), 0.500f, 1.428f, 1, 2},  // h2h/claws: Greater Talons
    {C('b', 't', 'l', ' '), 0.500f, 1.428f, 1, 2},  // h2h/claws: Blade Talons
    {C('c', 'l', 'w', ' '), 0.500f, 1.428f, 1, 2},  // h2h/claws: Claws
    {C('7', 'a', 'r', ' '), 0.500f, 1.273f, 1, 2},  // h2h/katar: Suwayyah
    {C('7', 'w', 'b', ' '), 0.500f, 1.273f, 1, 2},  // h2h/katar: Wrist Sword
    {C('9', 'a', 'r', ' '), 0.500f, 1.273f, 1, 2},  // h2h/katar: Quhab
    {C('9', 'w', 'b', ' '), 0.500f, 1.273f, 1, 2},  // h2h/katar: Wrist Spike
    {C('k', 't', 'r', ' '), 0.500f, 1.273f, 1, 2},  // h2h/katar: Katar
    {C('w', 'r', 'b', ' '), 0.500f, 1.273f, 1, 2},  // h2h/katar: Wrist Blade
    {C('7', 'q', 'r', ' '), 0.500f, 2.019f, 1, 2},  // h2h/scissors_katar: Scissors Suwayyah
    {C('9', 'q', 'r', ' '), 0.500f, 2.019f, 1, 2},  // h2h/scissors_katar: Scissors Quhab
    {C('s', 'k', 'r', ' '), 0.500f, 2.019f, 1, 2},  // h2h/scissors_katar: Scissors Katar
};

inline const Row* Find(uint32_t code) {
    for (const Row& r : kRows)
        if (r.code == code) return &r;
    return nullptr;
}

// Metres of the render per unit of a weapon model, as the skeleton hook measures it on the drawn weapon: the attach bone's Y row (the hero model's units per
// model unit: 1 unless the game scales the item) over the hero model's units per metre (skeletons.cpp: head height over the user's, times the world scale). 0 =
// unusable: not a number, or outside what any weapon of the game's own size comes to (the model's unit is 1 / 3.28084 m; the hero is about 3.65 units per metre).
constexpr float kUnitMinM = 0.1f, kUnitMaxM = 1.0f;
inline float UnitM(float boneRowLength, float unitsPerMetre) {
    if (!std::isfinite(boneRowLength) || !std::isfinite(unitsPerMetre) || !(boneRowLength > 0.0f) || !(unitsPerMetre > 0.0f)) return 0.0f;
    const float u = boneRowLength / unitsPerMetre;
    return u >= kUnitMinM && u <= kUnitMaxM ? u : 0.0f;
}

// A weapon's reach from the bone's origin along its Y, in model units: how far the point side runs and how far the butt side runs behind the origin.
// A butt of a fist weapon (axis 2) is NEGATIVE: its blade starts ahead of the bone's origin, at the front end of the grip - the knuckles - and the capsule runs
// from there to the point; for every other weapon it is the wood behind the origin, never below 0.
struct Reach {
    float tip = 0.0f, butt = 0.0f;
    int tipSign = 1;
    int axis = 1;
};
inline bool ReachOf(uint32_t code, Reach* out) {
    const Row* r = Find(code);
    if (!r) return false;
    out->axis = r->axis == 2 ? 2 : 1;
    out->tipSign = r->tipSign < 0 ? -1 : 1;
    if (out->axis == 2) {   // the blade's own stretch, as given: from `lo` to `hi` along the axis
        out->tip = out->tipSign > 0 ? r->hi : -r->lo;
        out->butt = out->tipSign > 0 ? -r->lo : r->hi;
        return true;
    }
    const float up = r->hi > 0.0f ? r->hi : 0.0f, down = r->lo < 0.0f ? -r->lo : 0.0f;
    out->tip = out->tipSign > 0 ? up : down;
    out->butt = out->tipSign > 0 ? down : up;
    return true;
}

}  // namespace weaponextent
