#pragma once
// ally_portraits.h - [hud] ally_portraits: the allies' portraits under the map held in the hand (VR first person, F4). A grid of square tiles - two rows
// of four - hung under the map's orb (or under the big map while it is up), as wide as the map, each tile the game's own portrait of a party player,
// the mercenary or a summon (a group of skeletons, ravens ... is one tile with a counter), its name and its life bar. The game draws these portraits at
// the top left of its interface (its HireablesPanel), which the headset shows only at the very edge of the view (docs/notes/ally_portraits.md).
//
// This file is the PURE part: where the game keeps what its portrait panel shows (the bytes that guard each layout), the two walks over that memory in
// terms of "a function that reads memory" (a test feeds a made-up heap), which picture each ally gets (the game's pettype.txt), the tiles' order, the
// grid in the picture, the life bar, and how far under the map the grid hangs. No game, no Windows, no OpenXR, no clock: cleanroom/tests/
// ally_portraits_test.cpp drives all of it. The reads are vr/ally_portraits_game.cpp, the picture vr/ally_portraits_paint.cpp, the frames and the
// painter thread vr/ally_portraits_wire.cpp, the quad layer vr/xr_ally_portraits.h.
//
// Read only: nothing of the game is written or called; the client's own lists are read where its portrait panel reads them.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "allies_place.h"   // [hud] allies_place: where the allies are shown (the game's row, under the map, on the wrist), the columns

namespace allyportraits {

// ---- where the game keeps it (D2R 3.3.93787 under D2RLoader, read off the decrypted image 2026-11-06) ---------------------------------------------
//
// The players: the party roster (vr/party_cmd_core.h), its head the global the life getter 0x1363F0 loads first (`mov rdx, [rip + disp32]`); an entry:
// name +0 (up to 0x3D letters), unit id +0x48, LIFE IN PERCENT +0x4C (what the getter returns, and what the portrait panel 0x1E80E0 hands its health
// bar's setter 0x2AC890, which clamps it to 0 - 100), class +0x54 (0x1362F0), level +0x58 (u16, 0x136430), party id +0x5A (u16, 0xFFFF none), the
// next entry +0x148.
// The mercenary and the summons: the client's pet list, its head the global the pet life getter 0x13AA50 loads (`mov rax, [rip + disp32]`); a node
// (0x38 bytes, made by 0x13A5A0): monster class +0 (u16), pet type +4 (pettype.txt's row; 7 the hireling), unit id +8, owner's unit id +0xC, LIFE IN
// PERCENT +0x1C (100 when it is made), the next node +0x30.
constexpr uint64_t RVA_PARTY_LIFE = 0x1363F0, RVA_PARTY_LEVEL = 0x136430, RVA_PARTY_CLASS = 0x1362F0;
constexpr uint64_t RVA_PET_LIFE = 0x13AA50, RVA_PET_MAKE = 0x13A619;
constexpr uint64_t RVA_MON_DATA = 0x985F3, RVA_MON_NAME = 0x98617;   // the monster's name words (vr/ally_portraits_game.cpp: the merc's name)

// The guards: those functions as this build has them (the four rip displacements are where the globals are: not compared, read).
inline constexpr uint8_t kSigPartyLife[61] = {0x48, 0x8B, 0x15, 0x00, 0x00, 0x00, 0x00, 0x83, 0xF9, 0xFF, 0x74, 0x2E, 0x48, 0x85, 0xD2, 0x74, 0x29, 0x39, 0x4A, 0x48, 0x74,
                                              0x20, 0x48, 0x8B, 0x42, 0x78, 0x48, 0x85, 0xC0, 0x74, 0x0E, 0x90, 0x39, 0x08, 0x74, 0x12, 0x48, 0x8B, 0x40, 0x08, 0x48, 0x85,
                                              0xC0, 0x75, 0xF3, 0x48, 0x8B, 0x92, 0x48, 0x01, 0x00, 0x00, 0xEB, 0xD6, 0x8B, 0x42, 0x4C, 0xC3, 0x33, 0xC0, 0xC3};
inline constexpr uint8_t kSigPartyLevel[10] = {0x48, 0x85, 0xC9, 0x74, 0x05, 0x0F, 0xB7, 0x41, 0x58, 0xC3};
inline constexpr uint8_t kSigPartyClass[9] = {0x48, 0x85, 0xC9, 0x74, 0x04, 0x8B, 0x41, 0x54, 0xC3};
inline constexpr uint8_t kSigPetLife[36] = {0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00, 0x48, 0x85, 0xC0, 0x74, 0x12, 0x0F, 0x1F, 0x40, 0x00, 0x39, 0x48,
                                            0x08, 0x74, 0x0C, 0x48, 0x8B, 0x40, 0x30, 0x48, 0x85, 0xC0, 0x75, 0xF2, 0x33, 0xC0, 0xC3, 0x8B, 0x40, 0x1C};
inline constexpr uint8_t kSigPetMake[19] = {0x89, 0x58, 0x08, 0x89, 0x68, 0x0C, 0x89, 0x78, 0x04, 0x66, 0x89, 0x30, 0xC7, 0x40, 0x1C, 0x64, 0x00, 0x00, 0x00};
inline constexpr uint8_t kSigMonData[4] = {0x48, 0x8B, 0x7F, 0x10};                           // the unit's +0x10: its monster data
inline constexpr uint8_t kSigMonName[8] = {0x0F, 0xB7, 0x47, 0x34, 0x0F, 0xB7, 0x4F, 0x32};   // its name words +0x34, +0x32 (then +0x36, +0x38)
constexpr uint32_t kDispAt = 3, kDispLen = 7;   // the rip-relative load: its displacement at +3, the instruction 7 bytes long

// Compares `code` (what the game has at that place) with a guard, skipping the displacement of a rip-relative first instruction when `disp`.
inline bool Matches(const uint8_t* code, const uint8_t* sig, size_t n, bool disp) {
    for (size_t i = 0; i < n; ++i) {
        if (disp && i >= kDispAt && i < kDispAt + 4) continue;
        if (code[i] != sig[i]) return false;
    }
    return true;
}
// The global a `mov reg, [rip + disp32]` at `at` loads from.
constexpr uint64_t RipTarget(uint64_t at, int32_t disp) { return at + kDispLen + (uint64_t)(int64_t)disp; }

// The roster entry's and the pet node's fields.
constexpr uint32_t kName = 0x00, kNameLen = 0x3D, kId = 0x48, kLife = 0x4C, kClass = 0x54, kLevel = 0x58, kPartyId = 0x5A, kNext = 0x148;
constexpr uint32_t kEntryRead = 0x5C;   // an entry's bytes read (to its party id)
constexpr uint16_t kNoParty = 0xFFFF;
constexpr uint32_t kPetClass = 0x00, kPetType = 0x04, kPetId = 0x08, kPetOwner = 0x0C, kPetLife = 0x1C, kPetNext = 0x30, kPetBytes = 0x38;
constexpr int kMaxWalk = 64;   // a list longer than this is not one (a loop, garbage)
constexpr uint32_t kHirelingType = 7;
// The monster data's name words (0xFFFF = none): a hireling has only +0x34, the id of its name in the game's strings.
constexpr uint32_t kMonName0 = 0x32, kMonName1 = 0x34, kMonName2 = 0x36, kMonName3 = 0x38;
constexpr uint16_t kNoName = 0xFFFF;

// ---- the pictures (pettype.txt and the players' class icons, data\hd\global\ui\HIREABLES\<file>.sprite) ----------------------------------------

enum class Icon : uint8_t {
    None, Amazon, Sorceress, Necromancer, Paladin, Barbarian, Druid, Assassin, Warlock,
    Rogue, Act2, Act3, Act5, Valkyrie, ClayGolem, BloodGolem, IronGolem, FireGolem, Skeleton, SkeletonMage, Revived,
    Raven, Wolf, Bear, OakSage, Wolverine, Barbs, PlaguePoppy, CycleOfLife, Vines, Shadow, BindDemon, Tainted, Defiler, Khazra, Count
};
constexpr int kIconCount = (int)Icon::Count;

// A player's class (0 Amazon ... 7 Warlock) as its icon.
constexpr Icon ClassIcon(uint32_t cls) { return cls < 8 ? (Icon)(1 + cls) : Icon::None; }

// One row of pettype.txt that the portrait panel draws (rows with no picture - none, single, dopplezon, invis, the traps, the hydra - it does not).
// `grouped`: icontype 2, one portrait for all of that type with a counter. `key`: the name's string key, `english` when the game does not answer.
struct PetKind {
    uint32_t type;
    Icon base;
    uint16_t cls[4];   // mclass1 - 4 (0: none) ...
    Icon by[4];        // ... and their pictures
    bool grouped;
    const char* key;
    const char* english;
};
inline constexpr PetKind kPets[] = {
    {7, Icon::Rogue, {338, 359, 561, 560}, {Icon::Act2, Icon::Act3, Icon::Act5, Icon::Act5}, false, "", "Mercenary"},
    {3, Icon::ClayGolem, {290, 291, 292, 0}, {Icon::BloodGolem, Icon::IronGolem, Icon::FireGolem, Icon::None}, false, "StrUI0", "Golem"},
    {4, Icon::Skeleton, {}, {}, true, "StrUI1", "Skeleton"},
    {5, Icon::SkeletonMage, {}, {}, true, "StrUI2", "Skeleton Mage"},
    {6, Icon::Revived, {}, {}, true, "StrUI3", "Revived"},
    {2, Icon::Valkyrie, {}, {}, false, "StrUI4", "Valkyrie"},
    {10, Icon::Raven, {}, {}, true, "strUI5", "Raven"},
    {11, Icon::Wolf, {}, {}, true, "strUI6", "Spirit Wolf"},
    {12, Icon::Wolf, {}, {}, true, "UIFenirsui", "Dire Wolf"},
    {15, Icon::Bear, {}, {}, false, "strUI7", "Grizzly"},
    {13, Icon::OakSage, {423, 422, 0, 0}, {Icon::Wolverine, Icon::Barbs, Icon::None, Icon::None}, false, "strUI14", "Spirit"},
    {14, Icon::PlaguePoppy, {426, 427, 0, 0}, {Icon::CycleOfLife, Icon::Vines, Icon::None, Icon::None}, false, "strUI13", "Vine"},
    {16, Icon::Shadow, {}, {}, false, "UIShadowMinion", "Shadow"},
    {20, Icon::BindDemon, {743, 744, 742, 0}, {Icon::Tainted, Icon::Defiler, Icon::Khazra, Icon::None}, false, "StrUIDemonicServant", "Demon"},
    {21, Icon::BindDemon, {}, {}, false, "StrUIDemonicServant", "Demon"},
};
inline const PetKind* KindOf(uint32_t type) {
    for (const PetKind& k : kPets)
        if (k.type == type) return &k;
    return nullptr;
}
inline Icon PetIcon(const PetKind& k, uint16_t cls) {
    for (int i = 0; i < 4; ++i)
        if (k.cls[i] && k.cls[i] == cls) return k.by[i];
    return k.base;
}

// ---- the reads -------------------------------------------------------------------------------------------------------------------------------

// `read(address, &value)` fills `value` (any trivially copyable type) and says whether it could.

// One pet of ours as the list has it.
struct Pet {
    uint16_t cls = 0;
    uint32_t type = 0, id = 0;
    int32_t life = -1;   // percent; -1 not a number the game would show
};
constexpr int kMaxPets = 32;

// The pets in the list whose owner is `owner`, in the list's order (the newest first). -1: the walk did not add up (a loop, an unreadable node, more
// than kMaxWalk nodes) - nothing of it is a guess.
template <class Read>
int ReadPets(uintptr_t head, uint32_t owner, Read read, Pet* out, int max) {
    if (!owner) return -1;
    std::array<uintptr_t, kMaxWalk> seen{};
    int visited = 0, n = 0;
    for (uintptr_t node = head; node;) {
        if (visited == kMaxWalk) return -1;
        for (int i = 0; i < visited; ++i)
            if (seen[(size_t)i] == node) return -1;
        seen[(size_t)visited++] = node;
        uint8_t b[kPetBytes];
        if (!read(node, b)) return -1;
        uint16_t cls;
        uint32_t type, id, own;
        int32_t life;
        uintptr_t next;
        std::memcpy(&cls, b + kPetClass, 2);
        std::memcpy(&type, b + kPetType, 4);
        std::memcpy(&id, b + kPetId, 4);
        std::memcpy(&own, b + kPetOwner, 4);
        std::memcpy(&life, b + kPetLife, 4);
        std::memcpy(&next, b + kPetNext, 8);
        if (type > 64) return -1;   // not a pet type of any bank
        if (own == owner && id && id != UINT32_MAX && n < max) out[n++] = Pet{cls, type, id, life >= 0 && life <= 100 ? life : -1};
        node = next;
    }
    return n;
}

// One player of our party (not us).
struct Member {
    char name[0x40] = {};
    uint32_t id = 0, cls = 0;
    int32_t life = -1;   // percent; -1 not a number the game would show
    uint16_t level = 0;
};
constexpr int kMaxMembers = 8;

// The other players of OUR party in the roster's order; 0 when we are in none. -1: the walk did not add up, or we are not in the roster.
template <class Read>
int ReadParty(uintptr_t head, uint32_t ourId, Read read, Member* out, int max) {
    if (!ourId) return -1;
    struct Entry {
        uint8_t b[kEntryRead];
        uint32_t U32(uint32_t at) const { uint32_t v; std::memcpy(&v, b + at, 4); return v; }
        uint16_t U16(uint32_t at) const { uint16_t v; std::memcpy(&v, b + at, 2); return v; }
    };
    std::array<Entry, 16> all{};
    std::array<uintptr_t, 16> seen{};
    int count = 0;
    for (uintptr_t at = head; at;) {
        if (count == (int)all.size()) return -1;
        for (int i = 0; i < count; ++i)
            if (seen[(size_t)i] == at) return -1;
        seen[(size_t)count] = at;
        if (!read(at, all[(size_t)count].b)) return -1;
        uintptr_t next = 0;
        if (!read(at + kNext, next)) return -1;
        ++count;
        at = next;
    }
    uint16_t ours = kNoParty;
    bool found = false;
    for (int i = 0; i < count; ++i)
        if (all[(size_t)i].U32(kId) == ourId) { ours = all[(size_t)i].U16(kPartyId); found = true; }
    if (!found) return -1;
    if (ours == kNoParty) return 0;
    int n = 0;
    for (int i = 0; i < count && n < max; ++i) {
        const Entry& e = all[(size_t)i];
        if (e.U32(kId) == ourId || e.U16(kPartyId) != ours) continue;
        Member m;
        for (uint32_t k = 0; k < kNameLen && k < sizeof m.name - 1; ++k) {
            const unsigned char c = e.b[kName + k];
            if (c < 0x20 || c == 0x7F) break;   // (UTF-8 past 0x7F is kept: the names of other languages)
            m.name[k] = (char)c;
        }
        m.id = e.U32(kId);
        m.cls = e.U32(kClass);
        const int32_t life = (int32_t)e.U32(kLife);
        m.life = life >= 0 && life <= 100 ? life : -1;
        m.level = e.U16(kLevel);
        out[n++] = m;
    }
    return n;
}

// The merc's name id from its monster data's four name words: only +0x34 set (a unique monster's name has more of them). 0xFFFF: none.
inline uint16_t HirelingNameId(const uint16_t words[4]) {
    return words[0] == kNoName && words[1] != kNoName && words[2] == kNoName && words[3] == kNoName ? words[1] : kNoName;
}

// The name the game's portrait panel SHOWS for the merc (Serg, test 0.205.5: our tile said "Mercenary" for his "Liene" - D2RLoader's localization
// service answered nothing). The panel names it with the game's unit name (0x9A1B0, its monster branch 0x985A0), which builds a monster's own name
// from those words and KEEPS it in a string of its own at the monster data's +0x40 (0x9871B frees the old one, 0x98756 stores the new) - every time the
// panel is drawn. So that string is read here, never built: the monster data's +0x40 -> a UTF-8 string, NUL-ended within kNameBytes * 2.
constexpr uint64_t RVA_MON_NAME_KEEP = 0x9871B, RVA_MON_NAME_STORE = 0x98756;
constexpr uint32_t kMonNameKept = 0x40;
inline constexpr uint8_t kSigMonNameKeep[5] = {0x48, 0x83, 0x7F, 0x40, 0x00};   // cmp qword ptr [rdi + 0x40], 0
inline constexpr uint8_t kSigMonNameStore[4] = {0x48, 0x89, 0x47, 0x40};        // mov qword ptr [rdi + 0x40], rax
// The kept name as read (`bytes`, n of them): the text up to its NUL when it is one (no control character, a NUL within n), else empty.
inline bool KeptName(const char* bytes, size_t n, char* out, size_t outSize) {
    if (!out || outSize == 0) return false;
    out[0] = 0;
    size_t len = 0;
    while (len < n && bytes[len]) {
        if ((unsigned char)bytes[len] < 0x20) return false;
        ++len;
    }
    if (len == 0 || len == n) return false;   // empty, or no end within what was read: not a name
    const size_t k = std::min(len, outSize - 1);
    std::memcpy(out, bytes, k);
    out[k] = 0;
    return true;
}

// Which of the two the game's name builder (0x985A0) gives a monster (Serg, test 0.205.6: the merc still "Mercenary" - id 1, never named by the kept
// string): +0x34 set -> the name it builds from all four words and keeps at +0x40 (above); +0x34 empty and +0x32 set -> the game's string of +0x32,
// handed back at once (0x98793: `call 0x3E2B730` with that id), nothing kept - a hireling's name is that one; both empty -> no name of its own.
enum class NameFrom : uint8_t { Kept, StringId, None };
inline NameFrom MonsterNameFrom(const uint16_t words[4], uint16_t* stringId) {
    if (words[1] != kNoName) return NameFrom::Kept;
    if (words[0] != kNoName) { *stringId = words[0]; return NameFrom::StringId; }
    return NameFrom::None;
}

// The game's own string by its id: the jump thunk 0x3E2B730 (`jmp [rip + slot]`, the slot D2RLoader fills) - what the name builder (0x98799) and the
// summons' portraits (0x1E7F60) call with a string id and take a UTF-8 text from. Guarded by those two call sites' bytes (each a `call` to the thunk).
constexpr uint64_t RVA_STRING = 0x3E2B730, RVA_STRING_CALL_NAME = 0x98799, RVA_STRING_CALL_PET = 0x1E7F60;
inline constexpr uint8_t kSigStringThunk[2] = {0xFF, 0x25};
inline constexpr uint8_t kSigStringCallName[5] = {0xE8, 0x92, 0x2F, 0xD9, 0x03};   // call 0x3E2B730 (from 0x98799)
inline constexpr uint8_t kSigStringCallPet[5] = {0xE8, 0xCB, 0x37, 0xC4, 0x03};    // call 0x3E2B730 (from 0x1E7F60)

// The summons' names as their portraits have them (0x1E66C0): the pet type's PetType row (0xC8 bytes, 0x13A56B) - its name's string id at +0x10, or,
// for the monster class of its mclassN (N 1 - 4), that slot's overridename at +0xBE + N * 2 (0x1E6727) when set and not 0x1506 (the panel's "none").
constexpr uint64_t RVA_PET_ROW = 0x13A56B, RVA_PET_OVERRIDE = 0x1E6727, RVA_PET_NAME = 0x1E673E;
inline constexpr uint8_t kSigPetRow[9] = {0x48, 0x69, 0x44, 0x24, 0x50, 0xC8, 0x00, 0x00, 0x00};   // imul rax, [rsp + 0x50], 0xC8
inline constexpr uint8_t kSigPetOverride[8] = {0x0F, 0xB7, 0x84, 0x59, 0xBE, 0x00, 0x00, 0x00};   // movzx eax, word [rcx + rbx*2 + 0xBE]
inline constexpr uint8_t kSigPetName[4] = {0x0F, 0xB7, 0x41, 0x10};                              // movzx eax, word [rcx + 0x10]
constexpr uint32_t kPetRowSize = 0xC8, kPetRowName = 0x10, kPetRowOverride = 0xBE;
constexpr uint16_t kPanelNoName = 0x1506;
// The string id of a pet's name from its PetType row (kPetRowSize bytes): 0 when there is none.
inline uint16_t PetNameId(const uint8_t* row, const PetKind& kind, uint16_t cls) {
    uint16_t v = 0;
    for (int i = 0; i < 4; ++i)
        if (kind.cls[i] && kind.cls[i] == cls) {
            std::memcpy(&v, row + kPetRowOverride + (i + 1) * 2, 2);
            if (v && v != kPanelNoName) return v;
        }
    std::memcpy(&v, row + kPetRowName, 2);
    return v == kPanelNoName ? 0 : v;
}

// The corner of the game's own portrait panel holding what it draws NOW, in its layout's units across (HireablesPanelHD.json): the merc and the
// summons in a row from x 46, 175 a portrait (the last one's name may run past its 120: a margin), the players in a column at 46 (a portrait and its
// name, 260). [hud] ally_portraits hides just that corner (the effect's AlliesBox, moved off the screen): no wider than the portraits, so the item labels
// and the hover names the game draws in the same layer beside them stay. 0: nothing to hide.
inline float GameCornerUnits(int players, int rowIcons) {
    float w = 0.0f;
    if (rowIcons > 0) w = 46.0f + (float)std::min(rowIcons, 12) * 175.0f + 40.0f;
    if (players > 0) w = std::max(w, 46.0f + 260.0f);
    return w;
}

// ---- the tiles --------------------------------------------------------------------------------------------------------------------------------

// More allies than this is not a game (seven players, a merc and every summon kind of one class are fewer): past it the last tile says how many more.
constexpr int kMaxTiles = 24;
constexpr int kNameBytes = 40;

enum class Kind : uint8_t { Player, Mercenary, Summon, More };
struct Tile {
    Kind kind = Kind::Summon;
    Icon icon = Icon::None;
    char name[kNameBytes] = {};
    int8_t life = -1;     // percent, -1 not known; a group: its weakest
    uint16_t count = 1;   // a group's members; More: how many tiles were left out
    uint16_t level = 0;   // a player's
    bool operator==(const Tile& o) const {
        return kind == o.kind && icon == o.icon && life == o.life && count == o.count && level == o.level && std::strncmp(name, o.name, kNameBytes) == 0;
    }
};

// What the painter draws: everything that changes the picture, and nothing else.
struct Look {
    std::array<Tile, kMaxTiles> tile{};
    uint8_t n = 0;
    uint8_t cols = alliesplace::kMapColsDefault;   // tiles across ([hud] allies_map_cols / allies_wrist_cols): the rows are as many as the tiles need
    uint32_t art = 0;   // the portraits' revision (found, or not, on the painter's thread): a change repaints
    bool operator==(const Look& o) const {
        if (n != o.n || cols != o.cols || art != o.art) return false;
        for (int i = 0; i < n; ++i)
            if (!(tile[(size_t)i] == o.tile[(size_t)i])) return false;
        return true;
    }
};

inline void CopyName(char (&dst)[kNameBytes], const char* src) {
    std::memset(dst, 0, sizeof dst);
    if (!src) return;
    size_t n = std::strlen(src);
    if (n > sizeof dst - 1) {   // cut, but not inside a UTF-8 letter
        n = sizeof dst - 1;
        while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) --n;
    }
    std::memcpy(dst, src, n);
}

// A pet's name as the game side found it, by its unit id: the merc's own, a summon's from its PetType row. Empty: our English word.
struct PetName {
    uint32_t id = 0;
    char name[kNameBytes] = {};
};

// The tiles in the portrait panel's order: the party's players, then the mercenary, then the summons by pettype's order above (a grouped type is
// one tile: its count and its weakest member's life). `petNames` / `typeNames` may be null; a name that is not there is the English word of the table.
inline Look MakeLook(const Member* party, int np, const Pet* pets, int npets, const PetName* petNames, int nPetNames, const char* const* typeNames /* per kPets row */,
                     uint32_t art) {
    std::array<Tile, 64> all{};
    int n = 0;
    for (int i = 0; i < np && n < (int)all.size(); ++i) {
        Tile t;
        t.kind = Kind::Player;
        t.icon = ClassIcon(party[i].cls);
        CopyName(t.name, party[i].name);
        t.life = (int8_t)party[i].life;
        t.level = party[i].level;
        all[(size_t)n++] = t;
    }
    for (size_t k = 0; k < sizeof kPets / sizeof kPets[0]; ++k) {
        const PetKind& kind = kPets[k];
        const char* typeName = typeNames && typeNames[k] && *typeNames[k] ? typeNames[k] : kind.english;
        int group = -1;
        for (int i = 0; i < npets && n < (int)all.size(); ++i) {
            const Pet& p = pets[i];
            if (p.type != kind.type) continue;
            if (kind.grouped && group >= 0) {
                Tile& g = all[(size_t)group];
                ++g.count;
                if (p.life >= 0 && (g.life < 0 || p.life < g.life)) g.life = (int8_t)p.life;
                continue;
            }
            Tile t;
            t.kind = kind.type == kHirelingType ? Kind::Mercenary : Kind::Summon;
            t.icon = PetIcon(kind, p.cls);
            const char* name = typeName;
            for (int j = 0; j < nPetNames && petNames; ++j)
                if (petNames[j].id == p.id && petNames[j].name[0]) name = petNames[j].name;
            CopyName(t.name, name);
            t.life = (int8_t)p.life;
            t.count = 1;
            if (kind.grouped) group = n;
            all[(size_t)n++] = t;
        }
    }
    Look look;
    look.art = art;
    const int shown = std::min(n, kMaxTiles);
    for (int i = 0; i < shown; ++i) look.tile[(size_t)i] = all[(size_t)i];
    look.n = (uint8_t)shown;
    if (n > kMaxTiles) {
        Tile more;
        more.kind = Kind::More;
        more.count = (uint16_t)(n - kMaxTiles + 1);
        look.tile[kMaxTiles - 1] = more;
    }
    return look;
}

// ---- the picture: the grid --------------------------------------------------------------------------------------------------------------------

constexpr int kTilePx = 160;

// `cols` tiles across, as many rows as `n` tiles need (Serg, 2026-11-06: only the number across is a setting). The picture: cols x rows tiles.
inline int ColsOf(int cols) { return alliesplace::SanitizedCols(cols, alliesplace::kMapColsDefault); }
inline int RowsFor(int n, int cols) { return n <= 0 ? 0 : (n + ColsOf(cols) - 1) / ColsOf(cols); }
inline int PicW(int cols) { return ColsOf(cols) * kTilePx; }
inline int PicH(int n, int cols) { return std::max(1, RowsFor(std::min(n, kMaxTiles), cols)) * kTilePx; }

// Where tile i of n lies in the picture (its top left corner, kTilePx square): the rows filled from the top, `cols` to a row, each row centred
// across (one tile alone stands under the map's middle). The top row is the one next to the map.
struct Cell {
    int x = 0, y = 0;
    bool operator==(const Cell&) const = default;
};
inline Cell CellOf(int i, int n, int cols) {
    n = std::min(n, kMaxTiles);
    const int c = ColsOf(cols);
    if (i < 0 || i >= n) return {};
    const int row = i / c, col = i % c;
    const int inRow = std::min(c, n - row * c);
    return {(PicW(c) - inRow * kTilePx) / 2 + col * kTilePx, row * kTilePx};
}

// The life bar: how much of it is filled (0 - 1), and its colour - green above half, yellow above a quarter, red below; not known: empty.
inline float LifeFill(int life) { return life < 0 ? 0.0f : std::clamp(life, 0, 100) / 100.0f; }
enum class Tone : uint8_t { Unknown, Green, Yellow, Red, Dead };
inline Tone LifeTone(int life) {
    if (life < 0) return Tone::Unknown;
    if (life == 0) return Tone::Dead;
    if (life > 50) return Tone::Green;
    if (life > 25) return Tone::Yellow;
    return Tone::Red;
}

// ---- the settings, and where it hangs ------------------------------------------------------------------------------------------------------------

constexpr float kSizeMinPct = 50.0f, kSizeMaxPct = 200.0f;
constexpr float kMinWidthM = 0.06f, kMaxWidthHandM = 0.40f, kMaxWidthBigM = 0.60f, kMaxWidthWristM = 0.30f;

struct Config {
    int place = alliesplace::kDefault;                  // [hud] allies_place: the game's row (0), under the map in the hand (1), on the right wrist (2)
    float sizePct = 125.0f;                              // [hud] ally_portraits_size: the grid's width at 4 across, % of the map's (the orb: its ball)
    int mapCols = alliesplace::kMapColsDefault;          // [hud] allies_map_cols: tiles across under the map
    int wristCols = alliesplace::kWristColsDefault;      // [hud] allies_wrist_cols: tiles across on the wrist
};
inline Config Sanitized(Config c) {
    c.place = alliesplace::Sanitized(c.place);
    if (!std::isfinite(c.sizePct)) c.sizePct = 125.0f;
    c.sizePct = std::clamp(c.sizePct, kSizeMinPct, kSizeMaxPct);
    c.mapCols = alliesplace::SanitizedCols(c.mapCols, alliesplace::kMapColsDefault);
    c.wristCols = alliesplace::SanitizedCols(c.wristCols, alliesplace::kWristColsDefault);
    return c;
}
inline bool GridOn(const Config& c) { return Sanitized(c).place == alliesplace::kMap; }
inline bool WristOn(const Config& c) { return Sanitized(c).place == alliesplace::kWrist; }
inline bool GameRowHidden(const Config& c) { return Sanitized(c).place != alliesplace::kGame; }   // each place shows the allies there only
inline int ColsFor(const Config& c) { return WristOn(c) ? Sanitized(c).wristCols : Sanitized(c).mapCols; }

// The game's corner to hide (layout units) while the allies are shown elsewhere: what the game draws now when a read says so (GameCornerUnits; 0:
// nothing drawn, nothing hidden); no read to go by (faulted, the guards not passed): its whole row of eight, as the moved corner has it.
constexpr float kGameRowUnits = 46.0f + 7.0f * 175.0f + 160.0f;
inline float HideUnits(bool readKnown, int players, int rowIcons) { return readKnown ? GameCornerUnits(players, rowIcons) : kGameRowUnits; }

// The grid's width under a map `mapWidthM` wide (the orb: its ball's diameter; the big map: its width), `big` while the big map is up: sizePct of the
// map at four across, a tile the same size at any other count across - within reach of the eye (at most 40 cm on the hand, 60 cm under the big map).
inline float GridWidthM(float mapWidthM, bool big, const Config& cfg) {
    if (!std::isfinite(mapWidthM) || mapWidthM <= 0.0f) return 0.0f;
    const Config c = Sanitized(cfg);
    const float w = mapWidthM * c.sizePct * 0.01f * (float)c.mapCols / (float)alliesplace::kMapColsDefault;
    return std::clamp(w, kMinWidthM, big ? kMaxWidthBigM : kMaxWidthHandM);
}
// The grid's height for its width: rows / cols of it.
inline float GridHeightM(float widthM, int n, int cols) { return widthM * (float)PicH(n, cols) / (float)PicW(cols); }
// From the map's centre down (along the map's own up) to the grid's centre: the map's visible half height (the orb: its ball's radius), a gap of a
// twentieth of the grid's width, and half the grid.
inline float DropM(float mapHalfHeightM, float gridWidthM, int n, int cols) {
    return std::max(0.0f, mapHalfHeightM) + 0.05f * gridWidthM + 0.5f * GridHeightM(gridWidthM, n, cols);
}

// On the right wrist (place 2): a tile is the wrist status panel's width ([hud] wrist_status_size_cm, 10 cm) over 2.5 - 4 cm by default - and the
// tiles stand on the panel's place, their bottom row there, more rows above it; over the wrist status' own panel when that one is up on the right wrist.
inline float WristTileM(float wristPanelCm) { return std::clamp(std::isfinite(wristPanelCm) ? wristPanelCm : 10.0f, 6.0f, 16.0f) * 0.01f / 2.5f; }
inline float WristWidthM(float wristPanelCm, int cols) { return std::min(kMaxWidthWristM, WristTileM(wristPanelCm) * (float)ColsOf(cols)); }
// Up (along the panel's own up) from the wrist status panel's centre to the tiles' centre: their bottom on that panel's bottom (`panelHM` its height),
// or a gap over its top while it is up there too.
inline float WristLiftM(float panelHM, float tilesHM, bool panelUp) {
    const float bottom = panelUp ? 0.5f * panelHM + 0.01f : -0.5f * panelHM;
    return bottom + 0.5f * tilesHM;
}

// A read older than this is not what the party has any more (the reads come five times a second).
constexpr double kStaleMs = 1500.0;
inline bool Fresh(double ageMs) { return ageMs >= 0.0 && ageMs <= kStaleMs; }

}  // namespace allyportraits
