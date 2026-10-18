#pragma once
// wrist_status.h - [hud] wrist_status: what the hero has about it, on the wrists. Small icons and counters that hang beside the life and mana
// medallions when the back of a wrist is turned to the eyes (VR first person, F4): the COMPANIONS - how many skeletons, mages, golems, wolves,
// ravens, vines, sentries, hydras, demons ... the hero has, and the mercenary (alive or dead) - and the EFFECTS on the hero: the auras and buffs it
// has (Might, Holy Shield, Frozen Armor, Battle Orders, a potion, a shrine ...) and the curses and ailments on it (Amplify Damage, Decrepify,
// Chilled, Poisoned ...). By default the left wrist shows the effects and the right one the companions ([hud] wrist_status_sides).
//
// This file is the PURE part: the tables, the reads of the game's own lists in terms of "a function that reads memory" (so a test can feed it a
// made-up heap), the order and the cells of a panel, the settings and where the panel hangs. No game, no Windows, no OpenXR, no clock: the test
// (cleanroom/tests/wrist_status_test.cpp) drives all of it. The game reads are vr/wrist_status_game.cpp, the picture vr/wrist_status_paint.cpp, the
// frames and the painter thread vr/wrist_status_wire.cpp, the quad layers vr/xr_wrist_status.h.
//
// Read only: nothing of the game is written, called other than the unit lookup it already has and its own state-check function, or pressed.
//
// After bearstonem/d2r-pcvr (MIT, v0.3.18): the idea of the wrist status, the layout of the hero's pet lists in the game's memory
// (steamvr/companions.h ReadRoster, vr/companions.cpp), the way the hero's states are asked (vr/active_effects.cpp: the game's own state check on the
// local server's player) and the table of the states worth showing (steamvr/active_effects.h Definitions) are his, written down here again with
// our own types and checked against this build of the game (docs/wrist_status.md). The panel, its gate, its picture and its settings are ours.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "hand_vitals.h"

namespace wriststatus {

using hset::Q;
using hset::V3;

// ---- the companions -------------------------------------------------------------------------------------------------------------------

// The game's pet types (pets.txt rows), shared by all classes and by the summons that items grant. `skill` is the skill that makes them (the icon),
// -1 for none. `tag` stands in for the icon when there is none (the skill icons are a cache made on the player's own machine, vr/wheel_icons.h).
constexpr int kPetTypes = 22;
struct Companion {
    int type;
    int skill;
    const char* name;
    const char* tag;
};
inline constexpr Companion kCompanions[] = {
    {7, -1, "Mercenary", "MRC"},      {4, 70, "Skeletons", "SKE"},   {5, 80, "Mages", "MAG"},       {3, 75, "Golem", "GOL"},          {6, 95, "Revives", "REV"},
    {2, 32, "Valkyrie", "VAL"},       {8, 28, "Decoy", "DEC"},       {10, 221, "Ravens", "RAV"},     {11, 227, "Spirit wolves", "SPW"}, {12, 237, "Dire wolves", "DIR"},
    {15, 247, "Grizzly", "GRZ"},      {13, 226, "Oak Sage", "OAK"},  {14, 222, "Vines", "VIN"},      {16, 268, "Shadows", "SHA"},       {17, 271, "Sentries", "SEN"},
    {18, -1, "Traps", "TRP"},         {19, 62, "Hydras", "HYD"},     {20, 373, "Demons", "DEM"},     {21, 382, "Bound", "BND"},
};
constexpr uint32_t Bit(int type) { return type > 0 && type < kPetTypes ? 1u << type : 0u; }

// What the hero has: how many of each pet type are alive. `relevant`: the types to show - a type the hero has or had members of, or whose list
// has room (a group the hero has learned stays on the panel at zero); the mercenary's list always has room, so it shows only while there is a
// record of one (alive, or dead and not replaced).
struct Roster {
    bool known = false;
    std::array<uint16_t, kPetTypes> alive{};
    uint32_t relevant = 0;
    bool operator==(const Roster&) const = default;
};

// Layout in 3.3.93787 (the byte patterns that guard it are in vr/wrist_status_game.cpp): the unit's +0x10 is its player data, whose +0x98 is the pet
// root; the root's first pointer is an array of 32-byte entries, one per pet type, each a list {head pointer, count (int), room (int)} followed by room
// for the rest; a list's nodes are 32 bytes: flags (bit 0 = a dead hireling), id (a dword), 16 bytes, and the next pointer at +24. A walk that does
// not add up (a loop, a count that does not match, an id of all ones, more nodes than the list says) discards the whole read: unknown, not a guess.
// `read(address, &value)` fills `value` (any trivially copyable type) and says whether it could.
template <class Read>
Roster ReadRoster(uintptr_t player, unsigned typeCount, Read read) {
    Roster result;
    if (typeCount < 20 || typeCount > 64) return result;
    uint32_t header[4]{};   // the unit type (0 = a player), the class, the id, the mode
    uintptr_t data = 0, pets = 0, lists = 0;
    if (!read(player, header) || header[0] != 0 || header[1] > 7 || header[3] == 0 || header[3] == 17 ||   // not a player, not a class of ours, dead, in the town portal
        !read(player + 0x10, data) || !data || !read(data + 0x98, pets))
        return result;
    if (!pets) {   // no pet root: the hero has none
        result.known = true;
        return result;
    }
    if (!read(pets, lists) || !lists || lists > UINTPTR_MAX - typeCount * 32) return result;
    struct List {
        uintptr_t head;
        int32_t current, maximum;
    };
    struct Node {
        uint32_t flags, id;
        uint8_t reserved[16];
        uintptr_t next;
    };
    static_assert(sizeof(List) == 16 && offsetof(Node, next) == 24 && sizeof(Node) == 32);
    std::array<uintptr_t, 512> seen{};
    unsigned visited = 0;
    for (unsigned type = 1; type < typeCount; ++type) {
        List list{};
        if (!read(lists + type * 32, list) || list.current < 0 || list.current > 512 || list.maximum < 0 || list.maximum > 4096) return {};
        unsigned members = 0, alive = 0;
        for (uintptr_t node = list.head; node;) {
            if (visited == seen.size() || members >= unsigned(list.current)) return {};
            for (unsigned i = 0; i < visited; ++i)
                if (seen[i] == node) return {};
            seen[visited++] = node;
            Node value{};
            if (!read(node, value) || value.id == UINT32_MAX) return {};
            ++members;
            if (!(value.flags & 1)) ++alive;
            node = value.next;
        }
        if (members != unsigned(list.current)) return {};
        if (type < unsigned(kPetTypes)) {
            result.alive[type] = uint16_t(alive);
            if (members || (type != 7 && list.maximum > 0)) result.relevant |= Bit(int(type));
        }
    }
    result.known = true;
    return result;
}

// ---- the effects ----------------------------------------------------------------------------------------------------------------------

// The game's state ids (states.txt rows) worth showing: effects ON the hero, never learned skills or bound keys. Benefit: a buff, an aura, a
// potion, a shrine; Harm: a curse or an ailment the hero suffers; Other: neither (a form, a frenzy that costs as it gives).
enum class Kind : uint8_t { Benefit, Harm, Other };
struct Effect {
    int state;
    int skill;
    const char* name;
    Kind kind = Kind::Benefit;
};
inline constexpr Effect kEffects[] = {
    {1, -1, "Frozen", Kind::Harm},          {2, -1, "Poisoned", Kind::Harm},         {11, -1, "Chilled", Kind::Harm},         {21, -1, "Stunned", Kind::Harm},
    {24, -1, "Slowed", Kind::Harm},         {9, 66, "Amplify Damage", Kind::Harm},   {19, 72, "Weaken", Kind::Harm},          {23, 71, "Dim Vision", Kind::Harm},
    {27, 137, "Taunted", Kind::Harm},       {29, 123, "Convicted", Kind::Harm},      {44, 114, "Holy Freeze", Kind::Harm},    {52, -1, "Healing blocked", Kind::Harm},
    {55, 76, "Iron Maiden", Kind::Harm},    {56, 77, "Terror", Kind::Harm},          {57, 86, "Attract", Kind::Harm},         {58, 82, "Life Tap", Kind::Harm},
    {59, 81, "Confuse", Kind::Harm},        {60, 87, "Decrepify", Kind::Harm},       {61, 91, "Lower Resist", Kind::Harm},    {62, -1, "Open Wounds", Kind::Harm},
    {84, -1, "Mana drain", Kind::Harm},     {85, -1, "Mana regen blocked", Kind::Harm}, {87, 17, "Slow Missiles", Kind::Harm}, {89, 146, "Battle Cry", Kind::Harm},
    {112, 238, "Rabies", Kind::Harm},       {113, -1, "Defense curse", Kind::Harm},  {114, -1, "Blood Mana", Kind::Harm},      {115, -1, "Burning", Kind::Harm},
    {156, 264, "Cloaked", Kind::Harm},      {17, 8, "Inner Sight", Kind::Harm},
    {33, 98, "Might"},                      {34, 99, "Prayer"},                      {3, 100, "Resist Fire"},                 {35, 102, "Holy Fire"},
    {36, 103, "Thorns"},                    {37, 104, "Defiance"},                   {4, 105, "Resist Cold"},                 {40, 108, "Blessed Aim"},
    {45, 109, "Cleansing"},                 {5, 110, "Resist Lightning"},            {42, 113, "Concentration"},              {43, 114, "Holy Freeze"},
    {41, 115, "Vigor"},                     {101, 117, "Holy Shield"},               {46, 118, "Holy Shock"},                 {47, 119, "Sanctuary"},
    {48, 120, "Meditation"},                {49, 122, "Fanaticism"},                 {28, 123, "Conviction"},                 {50, 124, "Redemption"},
    {8, 125, "Salvation"},                  {10, 40, "Frozen Armor"},                {13, 46, "Blaze"},                       {88, 50, "Shiver Armor"},
    {16, 52, "Enchant"},                    {38, 57, "Thunder Storm"},               {30, 58, "Energy Shield"},               {20, 60, "Chilling Armor"},
    {14, 68, "Bone Armor"},                 {26, 138, "Shout"},                      {15, 144, "Concentrate"},                {94, 147, "Frenzy"},
    {32, 149, "Battle Orders"},             {95, 152, "Berserk", Kind::Other},       {51, 155, "Battle Command"},             {139, 223, "Werewolf", Kind::Other},
    {140, 228, "Werebear", Kind::Other},    {120, 232, "Feral Rage"},                {117, 233, "Maul"},                      {151, 235, "Cyclone Armor"},
    {145, 249, "Armageddon"},               {144, 250, "Hurricane"},                 {149, 226, "Oak Sage"},                  {148, 236, "Heart of Wolverine"},
    {147, 246, "Spirit of Barbs"},          {122, 254, "Tiger Strike"},              {157, 258, "Burst of Speed"},            {125, 259, "Fists of Fire"},
    {153, 264, "Cloak of Shadows"},         {123, 265, "Cobra Strike"},              {159, 267, "Fade"},                      {127, 269, "Claws of Thunder"},
    {126, 274, "Blades of Ice"},            {158, 277, "Blade Shield"},              {31, 278, "Venom"},                      {124, 280, "Phoenix Strike"},
    {100, -1, "Healing potion"},            {106, -1, "Mana potion"},                {178, -1, "Antidote"},                   {179, -1, "Thawing"},
    {180, -1, "Stamina potion"},            {128, -1, "Armor shrine"},               {129, -1, "Combat shrine"},              {130, -1, "Lightning shrine"},
    {131, -1, "Fire shrine"},               {132, -1, "Cold shrine"},                {133, -1, "Poison shrine"},              {134, -1, "Skill shrine"},
    {135, -1, "Mana regen shrine"},         {136, -1, "Stamina shrine"},             {137, -1, "Experience shrine"},          {177, -1, "Delirium", Kind::Other},
    {190, -1, "Mark of the Bear"},          {191, -1, "Mark of the Wolf"},           {198, 385, "Hex Bane", Kind::Other},     {200, 386, "Hex Siphon", Kind::Other},
    {202, 389, "Hex Purge", Kind::Other},   {206, 387, "Psychic Ward"},              {208, 381, "Consume"},                   {211, 379, "Engorge"},
    {199, 385, "Hex Bane", Kind::Harm},     {201, 386, "Hex Siphon", Kind::Harm},    {217, 389, "Hex Purge", Kind::Harm},     {192, 393, "Lethargy", Kind::Harm},
    {193, 396, "Rancor", Kind::Harm},       {194, 400, "Death sigil", Kind::Harm},
};
constexpr unsigned kMaxStates = 512;

// Which of those states the hero is in. `known` false: not read (a read that failed is discarded whole).
struct Status {
    bool known = false;
    std::array<uint32_t, kMaxStates / 32> bits{};
    bool operator==(const Status&) const = default;
    bool Has(int state) const { return known && state > 0 && state < int(kMaxStates) && (bits[size_t(state) / 32] & (1u << (state % 32))) != 0; }
    void Set(int state) {
        if (state > 0 && state < int(kMaxStates)) bits[size_t(state) / 32] |= 1u << (state % 32);
    }
};

// Asks `check(state)` (1 yes, 0 no, anything else: could not be asked) for each state of the table that the game has (the table's row count:
// states past it do not exist in this bank); one answer that is neither 1 nor 0 discards the whole sample. A game with fewer than 180 states, or more
// than kMaxStates, is not the one the table was made for.
template <class Check>
Status Capture(unsigned stateCount, Check check) {
    Status result;
    if (stateCount < 180 || stateCount > kMaxStates) return result;
    for (const Effect& e : kEffects)
        if (unsigned(e.state) < stateCount) {
            const int on = check(e.state);
            if (on != 0 && on != 1) return {};
            if (on) result.Set(e.state);
        }
    result.known = true;
    return result;
}
inline int Count(const Status& s) {
    int n = 0;
    for (const Effect& e : kEffects)
        if (s.Has(e.state)) ++n;
    return n;
}

// ---- the settings ---------------------------------------------------------------------------------------------------------------------

constexpr unsigned kContentCompanions = 1u, kContentEffects = 2u;
constexpr float kSizeMinCm = 6.0f, kSizeMaxCm = 16.0f, kAlongMinCm = -10.0f, kAlongMaxCm = 10.0f, kOpacityMin = 0.3f, kOpacityMax = 1.0f;

struct Config {
    bool on = false;            // [hud] wrist_status: the box (off: nothing exists beyond a listener that returns at once)
    bool companions = true;     // [hud] wrist_status_companions: the summons and the mercenary
    bool effects = true;        // [hud] wrist_status_effects: the auras, buffs and curses
    int sides = 0;              // [hud] wrist_status_sides: 0 the effects on the left wrist and the companions on the right, 1 the other way, 2 both on both
    float sizeCm = 10.0f;       // [hud] wrist_status_size_cm: the panel's width
    float heightCm = 7.0f;      // [hud] wrist_status_height_cm: over the wrist (toward the head 3 cm and this much up, as the palm panel does)
    float alongCm = 0.0f;       // [hud] wrist_status_along_cm: along the arm, + toward the elbow
    float coneDeg = 40.0f;      // [hud] wrist_status_cone: how squarely the back of the hand faces the eyes for the panel to come up
    float opacity = 1.0f;       // [hud] wrist_status_opacity
    bool flip = false;          // [debug] wrist_status_flip: the back of the hand is the other way (a runtime that disagrees about the grip pose)
};

inline Config Sanitized(Config c) {
    c.sides = c.sides < 0 || c.sides > 2 ? 0 : c.sides;
    c.sizeCm = handvitals::Clamp(c.sizeCm, kSizeMinCm, kSizeMaxCm, 10.0f);
    c.heightCm = handvitals::Clamp(c.heightCm, handvitals::kHeightMin, handvitals::kHeightMax, 7.0f);
    c.alongCm = handvitals::Clamp(c.alongCm, kAlongMinCm, kAlongMaxCm, 0.0f);
    c.coneDeg = handvitals::Clamp(c.coneDeg, handvitals::kConeMin, handvitals::kConeMax, 40.0f);
    c.opacity = handvitals::Clamp(c.opacity, kOpacityMin, kOpacityMax, 1.0f);
    return c;
}

// What a wrist shows (the bits of kContent*). Side 0 left, 1 right (directinput's order).
inline unsigned ContentFor(const Config& c, int side) {
    unsigned want = 0;
    switch (c.sides) {
        case 1: want = side == 0 ? kContentCompanions : kContentEffects; break;
        case 2: want = kContentCompanions | kContentEffects; break;
        default: want = side == 0 ? kContentEffects : kContentCompanions; break;
    }
    if (!c.companions) want &= ~kContentCompanions;
    if (!c.effects) want &= ~kContentEffects;
    return want;
}

// ---- the panel: its cells and its look --------------------------------------------------------------------------------------------------

constexpr int kColumns = 6, kMaxRows = 3, kMaxCells = kColumns * kMaxRows;
constexpr int kPicW = 384, kPicH = 192;   // the picture: 6 x 3 cells of 64 pixels; the panel's height is half its width
constexpr float kPanelAspect = (float)kPicH / (float)kPicW;

enum class CellKind : uint8_t { Companion, Benefit, Harm, Other, More };
struct Cell {
    CellKind kind = CellKind::Other;
    int16_t id = 0;       // the pet type or the state; for More, how many are not shown
    int16_t skill = -1;   // the icon's skill, -1 none
    uint16_t count = 0;   // a companion's count alive (a dead mercenary: 0)
    bool operator==(const Cell&) const = default;
};

// What the painter draws: everything that changes the picture, and nothing else.
struct Look {
    std::array<Cell, kMaxCells> cell{};
    uint8_t n = 0;
    uint8_t opacity = 20;   // 0 - 20, the setting in steps of 0.05
    uint32_t icons = 0;     // the icon cache's revision: a new icon decoded repaints
    bool operator==(const Look&) const = default;
};

// The cells of a wrist's panel: the companions first (the mercenary, then the summons), then the effects - the harmful ones before the helpful. More
// than the grid holds: the last cell says how many were left out. Nothing to show: n = 0 (the panel does not come up).
inline Look MakeLook(const Roster& roster, const Status& status, unsigned content, const Config& cfg, uint32_t iconRev) {
    Look look;
    look.opacity = (uint8_t)std::lround(handvitals::Clamp(cfg.opacity, kOpacityMin, kOpacityMax, 1.0f) * 20.0f);
    look.icons = iconRev;
    std::array<Cell, 192> all{};
    size_t n = 0;
    if ((content & kContentCompanions) && roster.known)
        for (const Companion& d : kCompanions)
            if ((roster.relevant & Bit(d.type)) && n < all.size()) all[n++] = Cell{CellKind::Companion, (int16_t)d.type, (int16_t)d.skill, roster.alive[size_t(d.type)]};
    if ((content & kContentEffects) && status.known) {
        for (const bool harmful : {true, false})
            for (const Effect& e : kEffects)
                if (status.Has(e.state) && (e.kind == Kind::Harm) == harmful && n < all.size())
                    all[n++] = Cell{e.kind == Kind::Harm ? CellKind::Harm : e.kind == Kind::Other ? CellKind::Other : CellKind::Benefit, (int16_t)e.state, (int16_t)e.skill, 0};
    }
    const size_t shown = std::min<size_t>(n, kMaxCells);
    for (size_t i = 0; i < shown; ++i) look.cell[i] = all[i];
    look.n = (uint8_t)shown;
    if (n > size_t(kMaxCells)) {   // the last cell stands for the rest
        look.cell[kMaxCells - 1] = Cell{CellKind::More, (int16_t)std::min<size_t>(n - size_t(kMaxCells) + 1, 999), -1, 0};
    }
    return look;
}

// How the cells lie in the grid: row r holds `cols` cells (the rows are filled from the top, each as even as the count allows: 7 cells are 4 + 3), and the
// rows as a block are centred on the picture. Pixel box of cell i (64 x 64 cells, the picture kPicW x kPicH).
struct Slot {
    int x = 0, y = 0;
};
inline int RowsFor(int n) { return n <= 0 ? 0 : std::min(kMaxRows, (n + kColumns - 1) / kColumns); }
inline Slot SlotOf(int i, int n) {
    const int rows = RowsFor(n);
    if (rows <= 0 || i < 0 || i >= n) return {};
    // rows of the same width where it can be: ceil(n / rows) cells at most in a row
    const int per = (n + rows - 1) / rows;
    const int row = i / per, col = i % per;
    const int inRow = std::min(per, n - row * per);
    const int x0 = (kPicW - inRow * 64) / 2, y0 = (kPicH - rows * 64) / 2;
    return {x0 + col * 64, y0 + row * 64};
}

// ---- where it hangs -------------------------------------------------------------------------------------------------------------------

// The reveal's config for one wrist: the BACK of the hand turned to the eyes (the palm panel's test with the palm's normal reversed), only that hand,
// the same distance and gaze limits and fade as the palm panel (vr/hand_vitals.h Reveal); the panel is pulled 3 cm toward the head and `heightCm` up.
inline handvitals::Config RevealConfig(const Config& c, int side) {
    handvitals::Config r;
    r.on = true;
    r.hand = side == 0 ? 0 : 1;
    r.coneDeg = c.coneDeg;
    r.heightCm = c.heightCm;
    r.flip = !c.flip;   // the palm's normal reversed: the back of the hand
    return r;
}

// The hand's point the panel is hung from: the grip's position moved `alongCm` along the arm (the grip's +Z points toward the elbow: the controller's front is -Z).
inline V3 Anchor(const V3& grip, const Q& rot, float alongCm) {
    return hset::Add(grip, hset::Mul(hset::Rot(rot, {0.0f, 0.0f, 1.0f}), alongCm * 0.01f));
}

inline float PanelWidthM(const Config& c) { return handvitals::Clamp(c.sizeCm, kSizeMinCm, kSizeMaxCm, 10.0f) * 0.01f; }
inline float PanelHeightM(const Config& c) { return PanelWidthM(c) * kPanelAspect; }

// A read older than this is not what the hero has any more (the reads come about five times a second).
constexpr double kStaleMs = 2000.0;
inline bool Fresh(double ageMs) { return ageMs >= 0.0 && ageMs <= kStaleMs; }

}  // namespace wriststatus
