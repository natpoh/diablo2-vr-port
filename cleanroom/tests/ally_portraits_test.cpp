// The allies' portraits under the hand's map (vr/ally_portraits.h, vr/ally_portraits_paint.cpp): the guards against the game's image, the two walks over
// a made-up memory (the party roster, the client's pet list), the tiles and their order, the grid for 1 - 8 portraits, the life bar, the grid's size and
// place for the orb and the big map - and the pictures, drawn. No game, no headset.
//
//   ally_portraits_test [<folder for the pictures> [<portraits folder>]]
//
// The image snapshot (dump_loader/D2R_image.bin beside the source tree, tools/dump_d2r.py) is used when it is there: the guards must be the game's bytes.

#include "../../vr/ally_portraits.h"
#include "../../vr/ally_portraits_paint.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int g_fails = 0, g_checks = 0;
#define EXPECT(c)                                                       \
    do {                                                                \
        ++g_checks;                                                     \
        if (!(c)) {                                                     \
            ++g_fails;                                                  \
            printf("  FAIL line %d: %s\n", __LINE__, #c);               \
        }                                                               \
    } while (0)

using namespace allyportraits;

// ---- a made-up memory ----------------------------------------------------------------------------------------------------------------------------

struct Heap {
    static constexpr uintptr_t kBase = 0x400000;
    std::vector<uint8_t> mem = std::vector<uint8_t>(0x10000, 0);
    template <class T>
    void Put(uintptr_t at, const T& v) { std::memcpy(&mem[at - kBase], &v, sizeof v); }
    template <class T>
    bool Get(uintptr_t at, T& v) const {
        if (at < kBase || at - kBase + sizeof v > mem.size()) return false;
        std::memcpy(&v, &mem[at - kBase], sizeof v);
        return true;
    }
    auto Reader() const {
        return [this](uintptr_t at, auto& v) { return Get(at, v); };
    }
};

uintptr_t PutPet(Heap& h, uintptr_t at, uint16_t cls, uint32_t type, uint32_t id, uint32_t owner, int32_t life, uintptr_t next) {
    h.Put(at + kPetClass, cls);
    h.Put(at + kPetType, type);
    h.Put(at + kPetId, id);
    h.Put(at + kPetOwner, owner);
    h.Put(at + kPetLife, life);
    h.Put(at + kPetNext, next);
    return at;
}
uintptr_t PutEntry(Heap& h, uintptr_t at, const char* name, uint32_t id, int32_t life, uint32_t cls, uint16_t level, uint16_t party, uintptr_t next) {
    for (size_t i = 0; name[i]; ++i) h.Put(at + i, (uint8_t)name[i]);
    h.Put(at + kId, id);
    h.Put(at + kLife, life);
    h.Put(at + kClass, cls);
    h.Put(at + kLevel, level);
    h.Put(at + kPartyId, party);
    h.Put(at + kNext, next);
    return at;
}

// ---- the guards against the game's image -------------------------------------------------------------------------------------------------------

void TestGuards(const std::string& snapshot) {
    printf("the guards\n");
    uint8_t code[61];
    std::memcpy(code, kSigPartyLife, sizeof code);
    code[3] = 0x99; code[4] = 0x77;   // another displacement: still the same function
    EXPECT(Matches(code, kSigPartyLife, sizeof kSigPartyLife, true));
    EXPECT(!Matches(code, kSigPartyLife, sizeof kSigPartyLife, false));
    code[57] = 0x50;                  // another field than +0x4C: not the life
    EXPECT(!Matches(code, kSigPartyLife, sizeof kSigPartyLife, true));
    EXPECT(RipTarget(0x13AA50, 0x029131B9) == 0x2A4DC10);   // the pet list's head (read off the image)
    EXPECT(RipTarget(0x1363F0, 0x02917799) == 0x2A4DB90);   // the roster's (vr/party_cmd_core.h's)

    FILE* f = nullptr;
    if (fopen_s(&f, snapshot.c_str(), "rb") || !f) {
        printf("  (no image snapshot at %s: the bytes are not compared)\n", snapshot.c_str());
        return;
    }
    std::vector<uint8_t> img;
    fseek(f, 0, SEEK_END);
    img.resize((size_t)ftell(f));
    fseek(f, 0, SEEK_SET);
    const bool read = fread(img.data(), 1, img.size(), f) == img.size();
    fclose(f);
    EXPECT(read);
    auto at = [&](uint64_t rva, const uint8_t* sig, size_t n, bool disp) { return rva + n <= img.size() && Matches(&img[(size_t)rva], sig, n, disp); };
    EXPECT(at(RVA_PARTY_LIFE, kSigPartyLife, sizeof kSigPartyLife, true));
    EXPECT(at(RVA_PARTY_LEVEL, kSigPartyLevel, sizeof kSigPartyLevel, false));
    EXPECT(at(RVA_PARTY_CLASS, kSigPartyClass, sizeof kSigPartyClass, false));
    EXPECT(at(RVA_PET_LIFE, kSigPetLife, sizeof kSigPetLife, true));
    EXPECT(at(RVA_PET_MAKE, kSigPetMake, sizeof kSigPetMake, false));
    EXPECT(at(RVA_MON_DATA, kSigMonData, sizeof kSigMonData, false));
    EXPECT(at(RVA_MON_NAME, kSigMonName, sizeof kSigMonName, false));
    EXPECT(at(RVA_MON_NAME_KEEP, kSigMonNameKeep, sizeof kSigMonNameKeep, false));
    EXPECT(at(RVA_MON_NAME_STORE, kSigMonNameStore, sizeof kSigMonNameStore, false));
    EXPECT(at(RVA_STRING, kSigStringThunk, sizeof kSigStringThunk, false));
    EXPECT(at(RVA_STRING_CALL_NAME, kSigStringCallName, sizeof kSigStringCallName, false));
    EXPECT(at(RVA_STRING_CALL_PET, kSigStringCallPet, sizeof kSigStringCallPet, false));
    EXPECT(at(RVA_PET_ROW, kSigPetRow, sizeof kSigPetRow, false));
    EXPECT(at(RVA_PET_OVERRIDE, kSigPetOverride, sizeof kSigPetOverride, false));
    EXPECT(at(RVA_PET_NAME, kSigPetName, sizeof kSigPetName, false));
    int32_t rel = 0;   // both calls go to the string thunk
    std::memcpy(&rel, &img[RVA_STRING_CALL_NAME + 1], 4);
    EXPECT(RVA_STRING_CALL_NAME + 5 + (int64_t)rel == RVA_STRING);
    std::memcpy(&rel, &img[RVA_STRING_CALL_PET + 1], 4);
    EXPECT(RVA_STRING_CALL_PET + 5 + (int64_t)rel == RVA_STRING);
    int32_t disp = 0;
    std::memcpy(&disp, &img[RVA_PET_LIFE + kDispAt], 4);
    EXPECT(RipTarget(RVA_PET_LIFE, disp) == 0x2A4DC10);
    std::memcpy(&disp, &img[RVA_PARTY_LIFE + kDispAt], 4);
    EXPECT(RipTarget(RVA_PARTY_LIFE, disp) == 0x2A4DB90);
    printf("  compared with %s\n", snapshot.c_str());
}

// ---- the walks ---------------------------------------------------------------------------------------------------------------------------------

void TestPets() {
    printf("the pet list\n");
    Heap h;
    const uint32_t me = 1, other = 2;
    // newest first: a skeleton of another player's, our golem (blood), two skeletons, the merc (act 2)
    const uintptr_t a = 0x401000, b = 0x401100, c = 0x401200, d = 0x401300, e = 0x401400;
    PutPet(h, a, 363, 4, 50, other, 100, b);
    PutPet(h, b, 290, 3, 51, me, 80, c);
    PutPet(h, c, 363, 4, 52, me, 40, d);
    PutPet(h, d, 363, 4, 53, me, 90, e);
    PutPet(h, e, 338, 7, 54, me, 120, 0);   // (a life past 100: not a number the game shows)
    Pet pets[kMaxPets];
    const int n = ReadPets(a, me, h.Reader(), pets, kMaxPets);
    EXPECT(n == 4);
    EXPECT(pets[0].type == 3 && pets[0].cls == 290 && pets[0].id == 51 && pets[0].life == 80);
    EXPECT(pets[3].type == 7 && pets[3].life == -1);
    EXPECT(ReadPets(0, me, h.Reader(), pets, kMaxPets) == 0);   // an empty list
    EXPECT(ReadPets(a, 0, h.Reader(), pets, kMaxPets) == -1);   // no hero
    EXPECT(ReadPets(a, me, h.Reader(), pets, 2) == 2);          // room for two
    PutPet(h, e, 338, 7, 54, me, 100, b);                        // a loop
    EXPECT(ReadPets(a, me, h.Reader(), pets, kMaxPets) == -1);
    PutPet(h, e, 338, 7, 54, me, 100, 0x900000);                 // a node that cannot be read
    EXPECT(ReadPets(a, me, h.Reader(), pets, kMaxPets) == -1);
    PutPet(h, e, 338, 900, 54, me, 100, 0);                      // a type no bank has
    EXPECT(ReadPets(a, me, h.Reader(), pets, kMaxPets) == -1);
}

void TestParty() {
    printf("the party roster\n");
    Heap h;
    const uintptr_t a = 0x402000, b = 0x402200, c = 0x402400, d = 0x402600;
    PutEntry(h, a, "Pavel", 7, 64, 4, 71, 3, b);   // a barbarian of party 3
    PutEntry(h, b, "Serg", 9, 100, 2, 80, 3, c);   // us
    PutEntry(h, c, "Stranger", 11, 50, 1, 10, kNoParty, d);
    PutEntry(h, d, "Anna", 12, -5, 0, 33, 3, 0);   // (a life that is not a number the game shows)
    Member m[kMaxMembers];
    const int n = ReadParty(a, 9, h.Reader(), m, kMaxMembers);
    EXPECT(n == 2);
    EXPECT(std::strcmp(m[0].name, "Pavel") == 0 && m[0].id == 7 && m[0].life == 64 && m[0].cls == 4 && m[0].level == 71);
    EXPECT(std::strcmp(m[1].name, "Anna") == 0 && m[1].life == -1);
    EXPECT(ReadParty(a, 11, h.Reader(), m, kMaxMembers) == 0);    // the stranger is in no party
    EXPECT(ReadParty(a, 99, h.Reader(), m, kMaxMembers) == -1);   // we are not in the roster
    PutEntry(h, d, "Anna", 12, 50, 0, 33, 3, b);                   // a loop
    EXPECT(ReadParty(a, 9, h.Reader(), m, kMaxMembers) == -1);
    uint16_t w[4] = {kNoName, 1234, kNoName, kNoName};
    EXPECT(HirelingNameId(w) == 1234);
    w[0] = 77;   // a unique's prefix: not a hireling's name
    EXPECT(HirelingNameId(w) == kNoName);

    printf("the merc's name as the game's panel keeps it\n");
    char out[kNameBytes];
    const char liene[32] = "Liene";
    EXPECT(KeptName(liene, sizeof liene, out, sizeof out) && std::strcmp(out, "Liene") == 0);
    const char cyr[32] = "Лиэн";   // (UTF-8 kept as it is)
    EXPECT(KeptName(cyr, sizeof cyr, out, sizeof out) && std::strcmp(out, "Лиэн") == 0);
    char noEnd[16];
    std::memset(noEnd, 'a', sizeof noEnd);
    EXPECT(!KeptName(noEnd, sizeof noEnd, out, sizeof out) && out[0] == 0);   // no end within what was read: not a name
    const char empty[8] = {};
    EXPECT(!KeptName(empty, sizeof empty, out, sizeof out));
    const char ctrl[8] = {'L', 0x01, 'e', 0};
    EXPECT(!KeptName(ctrl, sizeof ctrl, out, sizeof out));
    char longName[80];
    std::memset(longName, 'x', 70);
    longName[70] = 0;
    EXPECT(KeptName(longName, sizeof longName, out, sizeof out) && std::strlen(out) == sizeof out - 1);

    printf("which name the game's builder gives\n");
    uint16_t sid = 0;
    const uint16_t merc[4] = {0x0ABC, kNoName, kNoName, kNoName};   // a hireling: +0x32 only (Serg's "Liene", test 0.205.6)
    EXPECT(MonsterNameFrom(merc, &sid) == NameFrom::StringId && sid == 0x0ABC);
    const uint16_t unique[4] = {0x0011, 0x0022, 0x0033, kNoName};   // a unique's name, built and kept
    EXPECT(MonsterNameFrom(unique, &sid) == NameFrom::Kept);
    const uint16_t plain[4] = {kNoName, kNoName, kNoName, kNoName};
    EXPECT(MonsterNameFrom(plain, &sid) == NameFrom::None);

    printf("the summons' names from their PetType row\n");
    uint8_t row[kPetRowSize] = {};
    auto put16 = [&](uint32_t at, uint16_t v) { std::memcpy(row + at, &v, 2); };
    put16(kPetRowName, 4300);                    // the kind's name (StrUIDemonicServant)
    put16(kPetRowOverride + 2, 4301);            // overridename1 (mclass1 743: Tainted)
    put16(kPetRowOverride + 4, kPanelNoName);    // overridename2: the panel's none
    const PetKind& demon = *KindOf(20);
    EXPECT(PetNameId(row, demon, 743) == 4301);   // its class's own name
    EXPECT(PetNameId(row, demon, 744) == 4300);   // a slot with none: the kind's
    EXPECT(PetNameId(row, demon, 999) == 4300);   // another class: the kind's
    EXPECT(PetNameId(row, *KindOf(4), 363) == 4300);
    put16(kPetRowName, kPanelNoName);
    EXPECT(PetNameId(row, *KindOf(4), 363) == 0);

    printf("the game's own corner to hide\n");
    EXPECT(GameCornerUnits(0, 0) == 0.0f);
    EXPECT(GameCornerUnits(0, 1) == 46.0f + 175.0f + 40.0f);   // the merc alone: no wider than its portrait and name
    EXPECT(GameCornerUnits(0, 3) == 46.0f + 3 * 175.0f + 40.0f);   // Niraj, Golem, the skeletons of the player's screenshot: past the third's 396 + 120
    EXPECT(GameCornerUnits(0, 3) > 46.0f + 2 * 175.0f + 120.0f);
    EXPECT(GameCornerUnits(2, 0) == 306.0f && GameCornerUnits(2, 1) == 306.0f);
    EXPECT(GameCornerUnits(0, 40) == GameCornerUnits(0, 12));
}

// ---- the tiles ---------------------------------------------------------------------------------------------------------------------------------

void TestLook() {
    printf("the tiles\n");
    Member party[2];
    std::strcpy(party[0].name, "Pavel");
    party[0].cls = 4; party[0].life = 64; party[0].level = 71;
    std::strcpy(party[1].name, "Anna");
    party[1].cls = 1; party[1].life = 20; party[1].level = 33;
    const Pet pets[] = {{290, 3, 51, 80}, {363, 4, 52, 40}, {363, 4, 53, 90}, {363, 4, 55, 70}, {338, 7, 54, 100}, {419, 10, 60, 100}};
    PetName names[1];
    names[0].id = 54;
    std::strcpy(names[0].name, "Niraj");
    Look l = MakeLook(party, 2, pets, 6, names, 1, nullptr, 0);
    EXPECT(l.n == 6);   // 2 players, the merc, the golem, the skeletons as one, the raven
    EXPECT(l.tile[0].kind == Kind::Player && l.tile[0].icon == Icon::Barbarian && l.tile[0].level == 71);
    EXPECT(l.tile[1].icon == Icon::Sorceress);
    EXPECT(l.tile[2].kind == Kind::Mercenary && l.tile[2].icon == Icon::Act2 && std::strcmp(l.tile[2].name, "Niraj") == 0);
    EXPECT(l.tile[3].icon == Icon::BloodGolem && std::strcmp(l.tile[3].name, "Golem") == 0);
    EXPECT(l.tile[4].icon == Icon::Skeleton && l.tile[4].count == 3 && l.tile[4].life == 40);   // the group's weakest
    EXPECT(l.tile[5].icon == Icon::Raven);
    // the game's own names for the types win over ours
    const char* typeNames[sizeof kPets / sizeof kPets[0]] = {};
    typeNames[1] = "Голем";
    l = MakeLook(nullptr, 0, pets, 1, nullptr, 0, typeNames, 0);
    EXPECT(l.n == 1 && std::strcmp(l.tile[0].name, "Голем") == 0);
    // pictures by class
    EXPECT(PetIcon(*KindOf(7), 271) == Icon::Rogue && PetIcon(*KindOf(7), 561) == Icon::Act5 && PetIcon(*KindOf(7), 359) == Icon::Act3);
    EXPECT(PetIcon(*KindOf(3), 289) == Icon::ClayGolem && PetIcon(*KindOf(3), 292) == Icon::FireGolem);
    EXPECT(PetIcon(*KindOf(13), 423) == Icon::Wolverine && PetIcon(*KindOf(14), 427) == Icon::Vines && PetIcon(*KindOf(20), 742) == Icon::Khazra);
    EXPECT(KindOf(19) == nullptr && KindOf(8) == nullptr && KindOf(17) == nullptr);   // hydras, the decoy, traps: no portrait in the game either
    EXPECT(ClassIcon(7) == Icon::Warlock && ClassIcon(8) == Icon::None);
    // the rows grow as the allies need (no "+N" at eight any more); only past kMaxTiles does the last tile count the rest
    Pet many[30];
    for (int i = 0; i < 30; ++i) many[i] = Pet{290, 3, (uint32_t)(100 + i), 100};   // golems are a tile each
    l = MakeLook(nullptr, 0, many, 12, nullptr, 0, nullptr, 0);
    EXPECT(l.n == 12 && l.tile[11].kind == Kind::Summon);
    l = MakeLook(nullptr, 0, many, 30, nullptr, 0, nullptr, 0);
    EXPECT(l.n == kMaxTiles && l.tile[kMaxTiles - 1].kind == Kind::More && l.tile[kMaxTiles - 1].count == 30 - kMaxTiles + 1);
    // a change of a life is a new look; the same read is the same look
    Look a = MakeLook(party, 2, pets, 6, names, 1, nullptr, 0), b = a;
    EXPECT(a == b);
    b.tile[0].life = 63;
    EXPECT(!(a == b));
    // a long name is cut, not inside a letter
    char longName[80];
    std::memset(longName, 0, sizeof longName);
    for (int i = 0; i < 30; ++i) std::strcat(longName, "ж");
    Tile t;
    CopyName(t.name, longName);
    EXPECT(std::strlen(t.name) == 38 && ((unsigned char)t.name[37] & 0xC0) == 0x80);
}

void TestGrid() {
    printf("the grid: cols across, as many rows as needed\n");
    for (int cols = 1; cols <= 8; ++cols)
        for (int n = 1; n <= kMaxTiles; ++n) {
            const int rows = (n + cols - 1) / cols;
            EXPECT(RowsFor(n, cols) == rows && PicW(cols) == cols * kTilePx && PicH(n, cols) == rows * kTilePx);
            int minX = PicW(cols), maxX = 0;
            for (int i = 0; i < n; ++i) {
                const Cell c = CellOf(i, n, cols);
                EXPECT(c.x >= 0 && c.x + kTilePx <= PicW(cols) && c.y >= 0 && c.y + kTilePx <= PicH(n, cols));
                EXPECT(c.y == (i / cols) * kTilePx);   // filled from the top row, next to the map
                for (int j = 0; j < i; ++j) EXPECT(!(CellOf(j, n, cols) == c));
                if (i / cols == rows - 1) { minX = std::min(minX, c.x); maxX = std::max(maxX, c.x + kTilePx); }
            }
            EXPECT(minX + maxX == PicW(cols));   // the last row centred too
        }
    EXPECT(CellOf(0, 1, 4).x == (PicW(4) - kTilePx) / 2);   // one alone under the middle
    EXPECT(CellOf(4, 5, 4).x == (PicW(4) - kTilePx) / 2 && CellOf(4, 5, 4).y == kTilePx);
    EXPECT(CellOf(7, 8, 4).x == 3 * kTilePx);
    EXPECT(RowsFor(9, 4) == 3 && PicH(9, 4) == 3 * kTilePx);   // nine under the map: three rows (Serg: "down as many as there are allies")
    EXPECT(RowsFor(0, 4) == 0 && CellOf(0, 0, 4) == Cell{} && PicH(0, 4) == kTilePx);
    EXPECT(PicW(0) == PicW(4) && PicW(99) == PicW(4));   // a count across out of range: the default
    EXPECT(RowsFor(7, 3) == 3 && CellOf(6, 7, 3).x == kTilePx && CellOf(6, 7, 3).y == 2 * kTilePx);   // the wrist's three across

    printf("the life bar\n");
    EXPECT(LifeFill(-1) == 0.0f && LifeFill(0) == 0.0f && LifeFill(50) == 0.5f && LifeFill(100) == 1.0f && LifeFill(250) == 1.0f);
    EXPECT(LifeTone(-1) == Tone::Unknown && LifeTone(0) == Tone::Dead && LifeTone(100) == Tone::Green && LifeTone(51) == Tone::Green);
    EXPECT(LifeTone(50) == Tone::Yellow && LifeTone(26) == Tone::Yellow && LifeTone(25) == Tone::Red && LifeTone(1) == Tone::Red);

    printf("the grid's size and place\n");
    Config cfg;   // 125 %, 4 across
    const float orb = 0.16f;   // the orb's ball ([hud] orb_size)
    const float w = GridWidthM(orb, false, cfg);
    EXPECT(std::fabs(w - 0.20f) < 1e-5f);
    EXPECT(std::fabs(GridHeightM(w, 8, 4) - 0.10f) < 1e-5f && std::fabs(GridHeightM(w, 3, 4) - 0.05f) < 1e-5f && std::fabs(GridHeightM(w, 12, 4) - 0.15f) < 1e-5f);
    EXPECT(std::fabs(DropM(0.08f, w, 8, 4) - (0.08f + 0.01f + 0.05f)) < 1e-5f);   // the ball's radius, a gap, half the grid
    const float big = GridWidthM(0.9f, true, cfg);   // under the big map: bigger, at most 60 cm
    EXPECT(big > w && std::fabs(big - kMaxWidthBigM) < 1e-5f);
    cfg.mapCols = 2;   // two across: the same tile size, half the width
    EXPECT(std::fabs(GridWidthM(orb, false, cfg) - 0.10f) < 1e-5f);
    cfg.mapCols = 8;
    EXPECT(std::fabs(GridWidthM(orb, false, cfg) - 0.40f) < 1e-5f);
    cfg.mapCols = 4;
    cfg.sizePct = 50.0f;
    EXPECT(std::fabs(GridWidthM(0.9f, true, cfg) - 0.45f) < 1e-5f);
    EXPECT(GridWidthM(0.1f, false, cfg) == kMinWidthM);   // never too small to read
    cfg.sizePct = 900.0f;
    EXPECT(GridWidthM(0.16f, false, cfg) == 0.32f && GridWidthM(0.3f, false, cfg) == kMaxWidthHandM);
    EXPECT(GridWidthM(0.0f, false, cfg) == 0.0f && GridWidthM(NAN, false, cfg) == 0.0f);
    cfg.sizePct = NAN;
    EXPECT(Sanitized(cfg).sizePct == 125.0f);

    printf("the place: one of three\n");
    Config pc;
    EXPECT(pc.place == alliesplace::kGame && !GridOn(pc) && !WristOn(pc) && !GameRowHidden(pc));   // the default: the game's own row
    pc.place = alliesplace::kMap;
    EXPECT(GridOn(pc) && !WristOn(pc) && GameRowHidden(pc) && ColsFor(pc) == 4);
    pc.place = alliesplace::kWrist;
    EXPECT(!GridOn(pc) && WristOn(pc) && GameRowHidden(pc) && ColsFor(pc) == 3);
    pc.place = 3;   // (the old "nowhere" is no choice: the game's row)
    EXPECT(!GridOn(pc) && !WristOn(pc) && !GameRowHidden(pc) && Sanitized(pc).place == alliesplace::kGame);
    pc.place = -5;
    EXPECT(Sanitized(pc).place == alliesplace::kGame);
    pc.mapCols = 0; pc.wristCols = 99;
    EXPECT(Sanitized(pc).mapCols == 4 && Sanitized(pc).wristCols == 3);
    pc.mapCols = 6; pc.wristCols = 2;
    EXPECT(Sanitized(pc).mapCols == 6 && Sanitized(pc).wristCols == 2);

    printf("the three old ticks, read once\n");
    using alliesplace::FromOldTicks;
    EXPECT(FromOldTicks(-1, -1, -1, -1) == alliesplace::kGame);   // none in the file
    EXPECT(FromOldTicks(1, 1, 1, 1) == alliesplace::kMap);        // Serg's 0.205.6 ini: all three ticked -> the grid
    EXPECT(FromOldTicks(1, 0, 0, 1) == alliesplace::kMap);
    EXPECT(FromOldTicks(0, 1, 1, 1) == alliesplace::kWrist);      // the game's row hidden, the wrist's counters up
    EXPECT(FromOldTicks(0, 1, 1, -1) == alliesplace::kWrist);     // (companions not written: on by default)
    EXPECT(FromOldTicks(0, 1, 0, 1) == alliesplace::kGame);       // the wrist status off: nothing on the wrist -> the game's row
    EXPECT(FromOldTicks(0, 1, 1, 0) == alliesplace::kGame);       // hidden everywhere: no such choice any more
    EXPECT(FromOldTicks(0, 0, 1, 1) == alliesplace::kGame);

    printf("on the right wrist\n");
    EXPECT(std::fabs(WristTileM(10.0f) - 0.04f) < 1e-6f);   // the panel's 10 cm over 2.5: a 4 cm tile
    EXPECT(std::fabs(WristWidthM(10.0f, 3) - 0.12f) < 1e-6f && std::fabs(WristWidthM(10.0f, 1) - 0.04f) < 1e-6f);
    EXPECT(WristWidthM(16.0f, 8) == kMaxWidthWristM);         // within reach
    EXPECT(std::fabs(WristTileM(NAN) - 0.04f) < 1e-6f && std::fabs(WristTileM(100.0f) - 0.064f) < 1e-6f);
    // the tiles' bottom on the wrist status panel's place (5 cm tall), rows growing up; over it, a cm apart, while that panel is up there too
    const float ph = 0.05f, oneRow = 0.04f, twoRows = 0.08f;
    EXPECT(std::fabs(WristLiftM(ph, oneRow, false) - (-0.025f + 0.02f)) < 1e-6f);
    EXPECT(std::fabs((WristLiftM(ph, twoRows, false) - 0.5f * twoRows) - (WristLiftM(ph, oneRow, false) - 0.5f * oneRow)) < 1e-6f);   // the same bottom
    EXPECT(std::fabs(WristLiftM(ph, oneRow, true) - (0.025f + 0.01f + 0.02f)) < 1e-6f);
    EXPECT(WristLiftM(ph, oneRow, true) - 0.5f * oneRow > 0.5f * ph);   // above the panel's top
    EXPECT(Fresh(0.0) && Fresh(1500.0) && !Fresh(1600.0) && !Fresh(-1.0));
}

// ---- the pictures ------------------------------------------------------------------------------------------------------------------------------

bool WritePng(const std::string& path, int w, int h, const std::vector<uint8_t>& rgb) {
    auto crc = [](const uint8_t* d, size_t n, uint32_t c) {
        static uint32_t table[256];
        static bool made = false;
        if (!made) {
            for (uint32_t i = 0; i < 256; ++i) {
                uint32_t v = i;
                for (int k = 0; k < 8; ++k) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1;
                table[i] = v;
            }
            made = true;
        }
        c = ~c;
        for (size_t i = 0; i < n; ++i) c = table[(c ^ d[i]) & 255] ^ (c >> 8);
        return ~c;
    };
    std::vector<uint8_t> raw;
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), &rgb[(size_t)y * w * 3], &rgb[(size_t)y * w * 3] + (size_t)w * 3);
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    for (size_t at = 0; at < raw.size(); at += 65535) {
        const size_t len = std::min<size_t>(65535, raw.size() - at);
        z.push_back(at + len >= raw.size() ? 1 : 0);
        z.push_back((uint8_t)(len & 255));
        z.push_back((uint8_t)(len >> 8));
        z.push_back((uint8_t)(~len & 255));
        z.push_back((uint8_t)((~len >> 8) & 255));
        z.insert(z.end(), raw.begin() + at, raw.begin() + at + len);
    }
    uint32_t a = 1, b = 0;
    for (uint8_t v : raw) { a = (a + v) % 65521; b = (b + a) % 65521; }
    const uint32_t ad = (b << 16) | a;
    for (int s = 24; s >= 0; s -= 8) z.push_back((uint8_t)(ad >> s));
    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    auto chunk = [&](const char* type, const std::vector<uint8_t>& data) {
        for (int s = 24; s >= 0; s -= 8) png.push_back((uint8_t)(data.size() >> s));
        std::vector<uint8_t> body(type, type + 4);
        body.insert(body.end(), data.begin(), data.end());
        png.insert(png.end(), body.begin(), body.end());
        const uint32_t c = crc(body.data(), body.size(), 0);
        for (int s = 24; s >= 0; s -= 8) png.push_back((uint8_t)(c >> s));
    };
    std::vector<uint8_t> ihdr;
    for (int s = 24; s >= 0; s -= 8) ihdr.push_back((uint8_t)(w >> s));
    for (int s = 24; s >= 0; s -= 8) ihdr.push_back((uint8_t)(h >> s));
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
    chunk("IHDR", ihdr);
    chunk("IDAT", z);
    chunk("IEND", {});
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "wb") || !f) return false;
    const bool ok = fwrite(png.data(), 1, png.size(), f) == png.size();
    fclose(f);
    return ok;
}

struct Sheet {
    int w, h;
    std::vector<uint8_t> rgb;
    Sheet(int W, int H) : w(W), h(H), rgb((size_t)W * H * 3) {
        for (int y = 0; y < h; ++y)   // a dusk sky going to sand, like the screenshot's
            for (int x = 0; x < w; ++x) {
                const float t = (float)y / (float)h;
                uint8_t* d = &rgb[((size_t)y * w + x) * 3];
                d[0] = (uint8_t)(70 + 70 * t); d[1] = (uint8_t)(58 + 52 * t); d[2] = (uint8_t)(44 + 24 * t);
            }
    }
    // a premultiplied picture laid over, scaled to dw x dh at x0, y0 (nearest pixel)
    void Lay(const std::vector<uint32_t>& p, int pw, int ph, int x0, int y0, int dw, int dh) {
        for (int y = 0; y < dh; ++y)
            for (int x = 0; x < dw; ++x) {
                const int sx = std::min(pw - 1, x * pw / dw), sy = std::min(ph - 1, y * ph / dh);
                const uint32_t v = p[(size_t)sy * pw + sx];
                const float a = (float)(v >> 24) / 255.0f;
                if (x0 + x < 0 || y0 + y < 0 || x0 + x >= w || y0 + y >= h) continue;
                uint8_t* d = &rgb[((size_t)(y0 + y) * w + x0 + x) * 3];
                d[0] = (uint8_t)std::min(255.0f, (float)(v & 255u) + d[0] * (1.0f - a));
                d[1] = (uint8_t)std::min(255.0f, (float)((v >> 8) & 255u) + d[1] * (1.0f - a));
                d[2] = (uint8_t)std::min(255.0f, (float)((v >> 16) & 255u) + d[2] * (1.0f - a));
            }
    }
    // a glass orb with a map's few lines (a stand-in for the map in the hand)
    void Orb(int cx, int cy, int r) {
        for (int y = -r; y <= r; ++y)
            for (int x = -r; x <= r; ++x) {
                const float d = std::sqrt((float)(x * x + y * y)) / (float)r;
                if (d > 1.0f || cx + x < 0 || cy + y < 0 || cx + x >= w || cy + y >= h) continue;
                uint8_t* p = &rgb[((size_t)(cy + y) * w + cx + x) * 3];
                const bool line = (std::abs((x + 2 * y) % 37) < 2 && d < 0.8f) || (std::abs((2 * x - y) % 41) < 2 && d < 0.75f);
                const float rim = d > 0.93f ? 1.0f : 0.0f;
                p[0] = (uint8_t)std::min(255.0f, 20 + 40 * (1 - d) + 160 * rim + (line ? 120 : 0));
                p[1] = (uint8_t)std::min(255.0f, 30 + 60 * (1 - d) + 140 * rim + (line ? 110 : 0));
                p[2] = (uint8_t)std::min(255.0f, 40 + 80 * (1 - d) + 100 * rim + (line ? 80 : 0));
            }
    }
    void Box(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b) {
        for (int y = std::max(0, y0); y < std::min(h, y1); ++y)
            for (int x = std::max(0, x0); x < std::min(w, x1); ++x) {
                uint8_t* p = &rgb[((size_t)y * w + x) * 3];
                p[0] = r; p[1] = g; p[2] = b;
            }
    }
};

void TestPaint(const std::string& dir) {
    printf("the pictures\n");
    // single player: a necromancer with his merc, a golem, skeletons and mages
    const Pet necro[] = {{338, 7, 54, 100}, {290, 3, 51, 72}, {363, 4, 52, 40}, {363, 4, 53, 90}, {363, 4, 56, 100}, {364, 5, 57, 30}, {364, 5, 58, 100}, {700, 6, 59, 0}};
    PetName niraj;
    niraj.id = 54;
    std::strcpy(niraj.name, "Niraj");
    const Look single = MakeLook(nullptr, 0, necro, 8, &niraj, 1, nullptr, 0);
    // co-op: three players, our merc and a druid's spirits
    Member party[3];
    const char* names[3] = {"Pavel", "Anna", "Bear"};
    const uint32_t cls[3] = {4, 1, 7};
    const int lives[3] = {88, 41, 12};
    for (int i = 0; i < 3; ++i) {
        std::strcpy(party[i].name, names[i]);
        party[i].cls = cls[i];
        party[i].life = lives[i];
        party[i].level = (uint16_t)(60 + 7 * i);
    }
    const Pet druid[] = {{271, 7, 70, 64}, {423, 13, 71, 100}, {427, 14, 72, 55}, {428, 15, 73, 90}, {420, 11, 74, 100}, {420, 11, 75, 70}, {419, 10, 76, 100}};
    const Look coop = MakeLook(party, 3, druid, 7, nullptr, 0, nullptr, 0);
    const Pet one[] = {{271, 7, 80, 23}};
    const Look alone = MakeLook(nullptr, 0, one, 1, nullptr, 0, nullptr, 0);
    EXPECT(single.n == 5 && coop.n == 9 && coop.tile[8].kind == Kind::Summon && alone.n == 1);   // (nine: three rows, no "+N")
    Look wrist = coop;
    wrist.cols = 3;   // on the right wrist: three across

    std::vector<uint32_t> p1, p2, p3, p4;
    allyportraitspaint::Render(single, p1);
    allyportraitspaint::Render(coop, p2);
    allyportraitspaint::Render(alone, p3);
    allyportraitspaint::Render(wrist, p4);
    EXPECT(p1.size() == (size_t)PicW(4) * 2 * kTilePx && p2.size() == (size_t)PicW(4) * 3 * kTilePx && p4.size() == (size_t)PicW(3) * 3 * kTilePx);
    auto opaque = [](const std::vector<uint32_t>& p, int w, int x0, int y0, int x1, int y1) {
        int n = 0;
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) n += (p[(size_t)y * w + x] >> 24) > 0;
        return n;
    };
    EXPECT(p3.size() == (size_t)PicW(4) * kTilePx);                                    // one tile: one row
    EXPECT(opaque(p3, PicW(4), 0, 0, kTilePx, kTilePx) == 0);                         // ... not in the first cell (it stands in the middle)
    EXPECT(opaque(p3, PicW(4), (PicW(4) - kTilePx) / 2 + 20, 20, (PicW(4) + kTilePx) / 2 - 20, kTilePx - 20) > 10000);
    EXPECT(opaque(p1, PicW(4), 0, kTilePx, kTilePx / 2, 2 * kTilePx) == 0);          // five: the fifth alone in the middle of the second row
    EXPECT(opaque(p2, PicW(4), 0, 2 * kTilePx, kTilePx, 3 * kTilePx) == 0);          // nine: the ninth alone in the third row's middle
    EXPECT(opaque(p2, PicW(4), (PicW(4) - kTilePx) / 2 + 20, 2 * kTilePx + 20, (PicW(4) + kTilePx) / 2 - 20, 3 * kTilePx - 20) > 10000);
    printf("  portraits found: %d of %d\n", allyportraitspaint::PortraitsFound(), kIconCount - 1);

    if (dir.empty()) return;
    // 1. the grids as they are painted (each its own size: cols x rows)
    {
        const int gap = 12;
        const int h1 = PicH(single.n, 4), h2 = PicH(coop.n, 4), h3 = PicH(alone.n, 4);
        Sheet s(PicW(4) + PicW(3) + 3 * gap, h1 + h2 + h3 + 4 * gap);
        s.Lay(p1, PicW(4), h1, gap, gap, PicW(4), h1);
        s.Lay(p2, PicW(4), h2, gap, 2 * gap + h1, PicW(4), h2);
        s.Lay(p3, PicW(4), h3, gap, 3 * gap + h1 + h2, PicW(4), h3);
        s.Lay(p4, PicW(3), PicH(wrist.n, 3), 2 * gap + PicW(4), gap, PicW(3), PicH(wrist.n, 3));
        EXPECT(WritePng(dir + "\\ally_portraits_grids.png", s.w, s.h, s.rgb));
    }
    // 2. under the map: the orb in the hand (16 cm ball, four across 125 % of it) and the big map (0.9 m, at most 60 cm), to scale; on the right wrist
    {
        const float pxPerM = 1600.0f;
        Sheet s(1500, 1100);
        Config cfg;
        // the orb, left
        const float ball = 0.16f, gw = GridWidthM(ball, false, cfg), gh = GridHeightM(gw, single.n, 4);
        const int ocx = 260, ocy = 220, r = (int)(0.5f * ball * pxPerM);
        s.Orb(ocx, ocy, r);
        const int gy = ocy + (int)(DropM(0.5f * ball, gw, single.n, 4) * pxPerM) - (int)(0.5f * gh * pxPerM);
        s.Lay(p1, PicW(4), PicH(single.n, 4), ocx - (int)(0.5f * gw * pxPerM), gy, (int)(gw * pxPerM), (int)(gh * pxPerM));
        // the big map, right (drawn at a third of the scale: it is that much farther from the eyes)
        const float bw = 0.9f, bh = 0.5f, k = pxPerM / 3.0f, bgw = GridWidthM(bw, true, cfg), bgh = GridHeightM(bgw, coop.n, 4);
        const int bcx = 1080, bcy = 260;
        s.Box(bcx - (int)(0.5f * bw * k), bcy - (int)(0.5f * bh * k), bcx + (int)(0.5f * bw * k), bcy + (int)(0.5f * bh * k), 30, 34, 40);
        s.Box(bcx - (int)(0.5f * bw * k) + 4, bcy - (int)(0.5f * bh * k) + 4, bcx + (int)(0.5f * bw * k) - 4, bcy + (int)(0.5f * bh * k) - 4, 52, 58, 66);
        const float bbw = 0.28f * bw, bbh = 0.25f * bbw;   // its Rotate button, 4 cm under it (vr/map_laser.h PlaceAt)
        const int bty = bcy + (int)((0.5f * bh + 0.04f) * k);
        s.Box(bcx - (int)(0.5f * bbw * k), bty, bcx + (int)(0.5f * bbw * k), bty + (int)(bbh * k), 96, 70, 40);
        const int bgy = bcy + (int)(DropM(0.5f * bh + 0.04f + bbh, bgw, coop.n, 4) * k) - (int)(0.5f * bgh * k);
        s.Lay(p2, PicW(4), PicH(coop.n, 4), bcx - (int)(0.5f * bgw * k), bgy, (int)(bgw * k), (int)(bgh * k));
        // the right wrist, bottom left: a forearm stand-in, the wrist status panel's place (10 cm x 5 cm, dashed), the tiles' bottom row on it
        const float pw = 0.10f, ph = 0.05f, ww = WristWidthM(10.0f, 3), wh = ww * (float)PicH(wrist.n, 3) / (float)PicW(3);
        const int wx = 330, wy = 1000;   // the panel's centre
        s.Box(wx - 60, wy + 10, wx + 60, wy + 90, 120, 92, 70);   // the forearm
        for (int x = wx - (int)(0.5f * pw * pxPerM); x < wx + (int)(0.5f * pw * pxPerM); x += 12) {
            s.Box(x, wy - (int)(0.5f * ph * pxPerM), x + 6, wy - (int)(0.5f * ph * pxPerM) + 2, 200, 200, 200);
            s.Box(x, wy + (int)(0.5f * ph * pxPerM) - 2, x + 6, wy + (int)(0.5f * ph * pxPerM), 200, 200, 200);
        }
        const int wcy = wy - (int)(WristLiftM(ph, wh, false) * pxPerM);   // (up is -y in the picture)
        s.Lay(p4, PicW(3), PicH(wrist.n, 3), wx - (int)(0.5f * ww * pxPerM), wcy - (int)(0.5f * wh * pxPerM), (int)(ww * pxPerM), (int)(wh * pxPerM));
        EXPECT(WritePng(dir + "\\ally_portraits_under_map.png", s.w, s.h, s.rgb));
    }
    printf("  wrote ally_portraits_grids.png, ally_portraits_under_map.png\n");
}


// ---- the game side: only reads ----------------------------------------------------------------------------------------------------------------------

void TestGameSource() {
#ifdef ALLYPORTRAITS_SRC_DIR
    printf("the game side's source\n");
    FILE* f = nullptr;
    std::string text;
    if (!fopen_s(&f, (std::string(ALLYPORTRAITS_SRC_DIR) + "\\ally_portraits_game.cpp").c_str(), "rb") && f) {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
        fclose(f);
    }
    EXPECT(text.size() > 3000);
    for (const char* bad : {"WriteProcessMemory", "VirtualProtect", "InstallInlineHook", "d2rsig::Hook", "d2rsig::Patch", "MH_CreateHook", "WriteMemory", "SendInput",
                            "runOnGameThread"})
        EXPECT(text.find(bad) == std::string::npos);
    EXPECT(text.find("__try") != std::string::npos && text.find("d2rsig::Check") != std::string::npos);   // guarded reads, byte guards
    // the only calls into the game: the two getters of the client's own hero (the local index, the player by index), as the hand vitals have them, and
    // the game's string lookup (the names, 0x3E2B730) - and that one only behind its guard
    size_t calls = 0;
    for (size_t at = 0; (at = text.find("reinterpret_cast<", at)) != std::string::npos; ++at) {
        const size_t end = text.find('>', at);
        if (end != std::string::npos && text.substr(at, end - at).find("(*)") != std::string::npos) ++calls;
    }
    EXPECT(calls == 3);
    EXPECT(text.find("StringGuard() && CallString(") != std::string::npos);
#endif
}

}  // namespace

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "";
    if (argc > 2) {
        const std::string a = argv[2];
        allyportraitspaint::SetDir(std::wstring(a.begin(), a.end()));
    }
#ifdef D2RVR_SOURCE_DIR
    const std::string snapshot = std::string(D2RVR_SOURCE_DIR) + "/dump_loader/D2R_image.bin";
#else
    const std::string snapshot = "dump_loader/D2R_image.bin";
#endif
    TestGuards(argc > 3 ? argv[3] : snapshot);
    TestPets();
    TestParty();
    TestLook();
    TestGrid();
    TestPaint(dir);
    TestGameSource();
    printf("ally_portraits_test: %d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
