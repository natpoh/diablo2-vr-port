// The game's state, the other way round from d2r_vr_shared.h: written by the
// vrcam plugin inside D2R (gamestate.cpp), read by BodyWalk's D2R Bridge, which
// switches BodyWalk's mapping category from it - the bow's gestures while a
// bow is in hand, the sword's otherwise, the menu's while a panel is open.
#pragma once

#include <cstdint>

#define D2RVR_STATE_NAME L"Local\\BodyWalkVR_D2R_State"
#define D2RVR_STATE_VERSION 1u

enum D2RVR_WeaponClass : uint32_t {
    D2RVR_WEAPON_UNKNOWN = 0,   // not in a game, or nothing read yet
    D2RVR_WEAPON_NONE = 1,      // empty hands
    D2RVR_WEAPON_MELEE = 2,     // anything held that is not a bow or crossbow
    D2RVR_WEAPON_BOW = 3,
    D2RVR_WEAPON_CROSSBOW = 4,
};

// The weapon's kind, from the game's own ItemTypes (gamestate.cpp), one
// BodyWalk mapping tab each: "D2R " + the name below.
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
#pragma pack(pop)

static_assert(sizeof(D2RVR_State) == 104, "D2RVR_State is a wire format");
