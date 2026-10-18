// The game's state, written by the vrcam plugin inside D2R (gamestate.cpp):
// what the hero holds, whether a panel is open, the view. vrcam turns it into
// Game Link's GameState for BodyWalk (vr/link.cpp: the bow's gestures while a
// bow is in hand, the sword's otherwise, the menu's while a panel is open), and
// D2R VR Settings reads it here (the weapon in hand, is the game running).
#pragma once

#include <cstdint>

#define D2RVR_STATE_NAME L"Local\\D2RVR_State"
#define D2RVR_STATE_VERSION 1u

enum D2RVR_WeaponClass : uint32_t {
    D2RVR_WEAPON_UNKNOWN = 0,   // not in a game, or nothing read yet
    D2RVR_WEAPON_NONE = 1,      // empty hands
    D2RVR_WEAPON_MELEE = 2,     // anything held that is not a bow or crossbow
    D2RVR_WEAPON_BOW = 3,
    D2RVR_WEAPON_CROSSBOW = 4,
};

// The weapon's kind, from the game's own ItemTypes (gamestate.cpp), one
// BodyWalk Mapping tab each: "D2R " + the name below.
enum D2RVR_WeaponType : uint32_t {
    D2RVR_TYPE_UNKNOWN = 0, D2RVR_TYPE_UNARMED, D2RVR_TYPE_BOW, D2RVR_TYPE_CROSSBOW, D2RVR_TYPE_SWORD,
    D2RVR_TYPE_AXE, D2RVR_TYPE_MACE, D2RVR_TYPE_SCEPTER, D2RVR_TYPE_WAND, D2RVR_TYPE_STAFF,
    D2RVR_TYPE_POLEARM, D2RVR_TYPE_SPEAR, D2RVR_TYPE_DAGGER, D2RVR_TYPE_THROWING, D2RVR_TYPE_JAVELIN,
    D2RVR_TYPE_CLAW, D2RVR_TYPE_ORB, D2RVR_TYPE_OTHER, D2RVR_TYPE_COUNT
};
inline const char* const kD2RVRWeaponTypeNames[D2RVR_TYPE_COUNT] = {
    "", "Unarmed", "Bow", "Crossbow", "Sword", "Axe", "Mace", "Scepter", "Wand", "Staff",
    "Polearm", "Spear", "Dagger", "Throwing", "Javelin", "Claw", "Orb", "Other"};

#pragma pack(push, 1)
struct D2RVR_State {
    uint32_t version;        // D2RVR_STATE_VERSION
    uint32_t counter;        // bumped on every write; frozen = the game is gone
    uint32_t weaponClass;    // D2RVR_WeaponClass of the active weapon set
    uint32_t menuOpen;       // 1 while a panel (trade, inventory, ...) is open
    char rightCode[4];       // item codes in the right and left hand, space padded
    char leftCode[4];
    char lastUi[64];         // the last UI message, "target:command", for tuning
    uint32_t weaponSet;      // 1 or 2: the active weapon set (I / II), 0 not known yet
    uint32_t weaponType;     // D2RVR_WeaponType of what is held, whichever set; 0 not known
    uint32_t viewMode;       // 0 the game's own camera, 1 first person, 2 third person, 3 F1 in perspective (our camera from above), 4 the game on the floor (F4)
    // Was `reserved` (always 0) until 2026-10-06. Bit 0: the weapon is two-handed (Weapons.txt
    // "2handed", gamestate.cpp); bit 1: held in both hands - two-handed and the other hand's slot
    // empty (a barbarian holds a two-handed sword in one hand beside another weapon); bit 2: the
    // "2handed" field was found in the game's tables (else bits 0 and 1 come from the kind).
    uint32_t twoHanded;
};
// Bit 3 (2026-10-06): the left hand's slot holds a weapon of its own - a barbarian's second blade, an
// assassin's second claw (not a shield, not a quiver): BodyWalk's "D2R Left Hand" mapping tab.
enum : uint32_t { D2RVR_TWO_HANDED = 1u, D2RVR_TWO_HANDS_ON = 2u, D2RVR_TWO_HANDED_KNOWN = 4u, D2RVR_LEFT_WEAPON = 8u };
// Bits 8 - 12 of `twoHanded` (2026-10-14; the older readers look at bits 0 - 3 only): the hero's class, as skel::HeroClass() has it from the posed model's folder - 0 amazon, 1 sorceress,
// 2 necromancer, 3 paladin, 4 barbarian, 5 druid, 6 assassin, 7 warlock - and bit 12 = it is known. D2R VR Settings shows it ("the hero playing now") beside the per-class wrist depth.
enum : uint32_t { D2RVR_HERO_SHIFT = 8, D2RVR_HERO_MASK = 0xFu << 8, D2RVR_HERO_KNOWN = 1u << 12 };
// Bits 13 - 15 (2026-10-18): did the OpenXR runtime bind the thumbsticks? 13 = the check has a verdict, 14 = it bound nothing to the stick, 15 = a spare stick action is read
// instead (vr/xr_stick_check.h: xrstick::Bits / FromBits; D2R VR Settings > Home > Status shows it).
inline uint32_t D2RVRHeroBits(int cls) { return cls >= 0 && cls < 16 ? (D2RVR_HERO_KNOWN | ((uint32_t)cls << D2RVR_HERO_SHIFT)) : 0u; }
inline int D2RVRHeroOf(uint32_t twoHanded) { return (twoHanded & D2RVR_HERO_KNOWN) ? (int)((twoHanded & D2RVR_HERO_MASK) >> D2RVR_HERO_SHIFT) : -1; }
#pragma pack(pop)

static_assert(sizeof(D2RVR_State) == 104, "D2RVR_State is a wire format");
