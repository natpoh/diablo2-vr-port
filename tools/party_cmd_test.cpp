// party_cmd_test - the headset's Party page's way into the game (vr/party_cmd_core.h, vr/party_cmd.cpp): how a roster read becomes the page's list,
// and - against a snapshot of the game's image (dump_loader/D2R_image.bin, tools/dump_d2r.py) - that the places and bytes are the game's: the
// Party panel's message handler hands a player's ProcessInviteButton to RVA_PARTY_ENTRY then RVA_PARTY_INVITE, its LeaveParty to RVA_PARTY_LEAVE;
// the invite function picks invite / accept / cancel by the entry's state as hset::party::ActFor does; the roster's fields are where the entry's
// maker writes them. Nothing here runs the game's code.
//
//   party_cmd_test.exe [<snapshot>]     (default: dump_loader\D2R_image.bin beside the source tree; without one only the pure part runs)

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "party_cmd_core.h"

using namespace partycmd::core;
namespace hp = hset::party;

static int g_fail = 0, g_checks = 0;
#define CHECK(c, what)                                                     \
    do {                                                                   \
        ++g_checks;                                                        \
        if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, what); } \
    } while (0)

static Raw Entry(const char* name, uint32_t id, uint32_t cls, uint16_t level, uint16_t party, uint32_t state) {
    Raw r;
    std::memset(r.b, 0, sizeof r.b);
    std::memcpy(r.b + kName, name, std::strlen(name));
    std::memcpy(r.b + kId, &id, 4);
    std::memcpy(r.b + kClass, &cls, 4);
    std::memcpy(r.b + kLevel, &level, 2);
    std::memcpy(r.b + kPartyId, &party, 2);
    std::memcpy(r.b + kState, &state, 4);
    return r;
}

static void TestPure() {
    CHECK(hp::ActFor(0) == hp::Act::Invite && hp::ActFor(2) == hp::Act::Accept && hp::ActFor(4) == hp::Act::Cancel && hp::ActFor(1) == hp::Act::None &&
              hp::ActFor(3) == hp::Act::None && hp::ActFor(99) == hp::Act::None,
          "the button by the game's state: 0 invite, 2 accept, 4 cancel, else none");
    // a game: us (id 1) and Bear in party 0, Pony inviting us, Ann not in a party
    std::vector<Raw> roster = {Entry("Serg", 1, 3, 30, 0, 1), Entry("Bear", 2, 4, 28, 0, 1), Entry("Pony", 3, 7, 12, kNoParty, 2), Entry("Ann", 4, 0, 5, kNoParty, 0)};
    hp::View v = Build(roster, 1);
    CHECK(v.known && v.inParty && v.others.size() == 3, "the others, and we are in a party");
    CHECK(v.others[0].name == "Bear" && v.others[0].id == 2 && v.others[0].cls == 4 && v.others[0].level == 28 && v.others[0].sameParty, "Bear: in our party");
    CHECK(v.others[1].name == "Pony" && !v.others[1].sameParty && v.others[1].state == 2 && hp::ActFor(v.others[1].state) == hp::Act::Accept && v.others[1].cls == 7,
          "Pony invites us: Accept; a Warlock");
    CHECK(v.others[2].name == "Ann" && hp::ActFor(v.others[2].state) == hp::Act::Invite, "Ann: Invite");
    v = Build({Entry("Serg", 1, 3, 30, kNoParty, 0), Entry("Ann", 4, 0, 5, kNoParty, 0)}, 1);
    CHECK(!v.inParty && v.others.size() == 1 && !v.others[0].sameParty, "no party: nobody shares one with us (both 0xFFFF is not a party)");
    std::vector<Raw> many;
    for (uint32_t i = 0; i < 12; ++i) many.push_back(Entry("X", 100 + i, 1, 1, kNoParty, 0));
    CHECK(Build(many, 100).others.size() == (size_t)hp::kMaxOthers, "at most seven others");
    Raw odd = Entry("Abc", 9, 99, 1, kNoParty, 0);
    odd.b[kName + 3] = 0x07;   // not a letter: the name ends there
    odd.b[kName + 4] = 'Z';
    v = Build({odd}, 1);
    CHECK(v.others[0].name == "Abc" && v.others[0].cls == -1, "a name ends at what is not a letter; an unknown class is none");
    Raw longName;
    std::memset(longName.b, 'a', kNameLen);
    std::memcpy(longName.b + kId, "\x05\0\0\0", 4);
    CHECK(NameOf(longName).size() == kNameLen, "a name is never longer than the game's place for it");
    CHECK(std::string(hp::ClassName(3)) == "Paladin" && std::string(hp::ClassName(7)) == "Warlock" && !*hp::ClassName(8), "class names");
}

// -- against the snapshot ------------------------------------------------------------------------------------------------------------------------
static std::vector<uint8_t> g_img;
static bool At(uint64_t rva, const uint8_t* bytes, size_t n) { return rva + n <= g_img.size() && std::memcmp(&g_img[rva], bytes, n) == 0; }
static bool AtList(uint64_t rva, std::initializer_list<uint8_t> bytes) { return At(rva, bytes.begin(), bytes.size()); }
static int32_t I32(uint64_t rva) { int32_t v = 0; std::memcpy(&v, &g_img[rva], 4); return v; }
static uint64_t CallTarget(uint64_t rva) { return g_img[rva] == 0xE8 || g_img[rva] == 0xE9 ? RipTarget(rva, I32(rva + 1), 5) : 0; }
static std::string Str(uint64_t rva) { return rva < g_img.size() ? std::string((const char*)&g_img[rva]) : std::string(); }

static void TestSnapshot() {
    // the pieces' first bytes, as the plugin checks them
    CHECK(At(RVA_PARTY_ENTRY, kSigPartyEntry, sizeof kSigPartyEntry) && At(RVA_PARTY_ENTRY + 7, kSigPartyEntryTail, sizeof kSigPartyEntryTail), "the roster lookup's bytes");
    const uint64_t roster = RipTarget(RVA_PARTY_ENTRY, I32(RVA_PARTY_ENTRY + 3), 7);
    CHECK(roster == 0x2A4DB90, "its first instruction names the roster");
    CHECK(At(RVA_PARTY_INVITE, kSigPartyInvite, sizeof kSigPartyInvite), "the invite button's bytes");
    CHECK(At(RVA_PARTY_LEAVE, kSigPartyLeave, sizeof kSigPartyLeave) && At(RVA_PARTY_LEAVE + kLeaveSevenAt, kPartyLeaveSeven, sizeof kPartyLeaveSeven), "the leave's bytes");
    CHECK(At(RVA_GAME_MODE, kSigGameMode, sizeof kSigGameMode) && g_img[RVA_GAME_MODE + 6] == 0xC3, "the mode getter");
    // the Party panel's handler 0x1470D10: its messages' names hashed; ProcessInviteButton -> entry of the message's unit id -> the invite button;
    // LeaveParty -> the leave (the names as the game registers them: 0x8C2750 ...)
    CHECK(Str(0x1D7DFD0) == "ProcessInviteButton" && Str(0x1D7DF88) == "LeaveParty" && Str(0x1CC3010) == "PartyPanelMessage", "the panel's message names");
    CHECK(CallTarget(0x1470E9D) == RVA_PARTY_ENTRY && CallTarget(0x1470EAE) == RVA_PARTY_INVITE, "a player's button: entry of his id, then the invite button");
    CHECK(AtList(0x1470E94, {0x48, 0x8B, 0x86, 0x10, 0x01, 0x00, 0x00, 0x8B, 0x08}), "the id is the message's (+0x110), the unit id the panel's row has");
    CHECK(CallTarget(0x1470E6F) == RVA_PARTY_LEAVE, "Leave party: the leave");
    CHECK(AtList(0x1470EC5, {0x33, 0xD2, 0x8D, 0x4A, 0x15}) && CallTarget(0x1470ECA) == 0xC7D30, "Close: panel 0x15 (the one the P command opens)");
    // the invite button: the entry's state picks the action (0 -> 4 invite, 2 -> 6 accept, 4 -> 5 cancel), the game's sender 0xECA00 sends it
    CHECK(AtList(0x136CE9, {0x8B, 0x49, 0x68}), "it reads the entry's state (+0x68)");
    CHECK(AtList(0x136D53, {0xBF, 0x04, 0x00, 0x00, 0x00}) && AtList(0x136D3B, {0xBF, 0x06, 0x00, 0x00, 0x00}) && AtList(0x136D1F, {0xBF, 0x05, 0x00, 0x00, 0x00}),
          "state 0 invites, 2 accepts, 4 cancels (ActFor)");
    CHECK(AtList(0x136D5B, {0x8B, 0x53, 0x48}) && CallTarget(0x136D60) == 0xECA00, "with the entry's unit id (+0x48), through the game's own sender");
    // the roster's fields as the entry's maker 0x13A220 writes them, and the game's own readers
    CHECK(AtList(0x13A23F, {0xBA, 0x3D, 0x00, 0x00, 0x00}) && kNameLen == 0x3D, "the name: up to 0x3D letters at +0");
    CHECK(AtList(0x13A257, {0x66, 0x89, 0x46, 0x58}) && AtList(0x13A260, {0x66, 0x89, 0x46, 0x5A}) && AtList(0x13A268, {0x89, 0x46, 0x68}) &&
              AtList(0x13A26F, {0x89, 0x6E, 0x48}) && AtList(0x13A272, {0x89, 0x7E, 0x54}),
          "level +0x58, party +0x5A, state +0x68, id +0x48, class +0x54");
    CHECK(AtList(0x136435, {0x0F, 0xB7, 0x41, 0x58}) && AtList(0x136495, {0x8B, 0x41, 0x68}), "the game's level and state getters read the same");
    CHECK(AtList(0x13622D, {0x48, 0x8B, 0x92, 0x48, 0x01, 0x00, 0x00}) && kNext == 0x148, "the next entry at +0x148");
    CHECK(AtList(0x139D91, {0xB8, 0xFF, 0xFF, 0x00, 0x00}) && AtList(0x139D9D, {0x66, 0x89, 0x43, 0x5A}) && kNoParty == 0xFFFF, "no party: 0xFFFF (the leave writes it)");
    // Open Party panel: the game's Party key command (0x02) opens panel 0x15, only outside mode 0
    CHECK(CallTarget(0x119BF4) == RVA_GAME_MODE && AtList(0x119BFF, {0xB9, 0x15, 0x00, 0x00, 0x00}), "the P command: panel 0x15, not offline");
}

int main(int argc, char** argv) {
    TestPure();
    std::string path = argc > 1 ? argv[1] : std::string(D2RVR_SOURCE_DIR) + "/dump_loader/D2R_image.bin";
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        std::fseek(f, 0, SEEK_END);
        g_img.resize((size_t)std::ftell(f));
        std::fseek(f, 0, SEEK_SET);
        if (std::fread(g_img.data(), 1, g_img.size(), f) != g_img.size()) g_img.clear();
        std::fclose(f);
    }
    if (g_img.size() > 0x3000000) TestSnapshot();
    else std::printf("  no snapshot at %s: the image checks are skipped\n", path.c_str());
    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
