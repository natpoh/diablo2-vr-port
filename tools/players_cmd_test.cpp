// players_cmd_test - the headset's Players row's way into the game (vr/players_cmd_core.h, vr/players_cmd.cpp): the line it sends, how the
// game's answer is read, and - against a snapshot of the game's image (dump_loader/D2R_image.bin, tools/dump_d2r.py) - that the places and
// bytes are the game's: the chat box hands its line to the parser at RVA_CHAT_COMMANDS, whose /players branch asks the mode getter at
// RVA_GAME_MODE (0, 5, 7), reads the number after the word, and writes it through the setting at RVA_PLAYERS_SETTING ("Offline Difficulty
// Scaling", its count at +0xC). Nothing here runs the game's code.
//
//   players_cmd_test.exe [<snapshot>]     (default: dump_loader\D2R_image.bin beside the source tree; without one only the pure part runs)

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "players_cmd_core.h"

using namespace playerscmd::core;

static int g_fail = 0, g_checks = 0;
#define CHECK(c, what)                                                     \
    do {                                                                   \
        ++g_checks;                                                        \
        if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, what); } \
    } while (0)

// The game's /players branch as the snapshot shows it (0x188807 - 0x188895), on a line: the word's 8 letters compared, the mode asked,
// atoi of what follows the word, at least 1, at most 8 - so a test can hand it what the row sends.
static int GameTakes(const std::string& line, int mode, int* count) {
    if (line.compare(0, 8, "/players") != 0) return 0;   // another command
    if (!(mode == 0 || mode == 5 || mode == 7)) return 0;
    int n = std::atoi(line.c_str() + 8);
    n = n > 1 ? n : 1;
    *count = n < 8 ? n : 8;
    return 1;
}

static void TestPure() {
    CHECK(Line(4) == "/players 4" && Line(1) == "/players 1" && Line(8) == "/players 8", "the line as typed");
    CHECK(Line(0) == "/players 1" && Line(-3) == "/players 1" && Line(12) == "/players 8", "clamped to 1 - 8 as the game does");
    for (int n = 1; n <= 8; ++n) {
        int got = 0;
        CHECK(GameTakes(Line(n), 0, &got) == 1 && got == n, "the game reads back the number the row sent");
    }
    int got = 0;
    CHECK(!GameTakes(Line(3), 6, &got) && !GameTakes(Line(3), 1, &got), "other modes: the game does not take it");
    CHECK(TakesPlayers(0) && TakesPlayers(5) && TakesPlayers(7) && !TakesPlayers(1) && !TakesPlayers(6) && !TakesPlayers(-1), "the modes the parser takes");
    hset::Players p = Read(true, 3, true, 0);
    CHECK(p.count == 3 && p.takes, "read: 3, single player");
    p = Read(true, 0, true, 0);
    CHECK(p.count == 0 && p.takes, "a setting not made yet (0): not known");
    p = Read(true, 9, true, 6);
    CHECK(p.count == 0 && !p.takes, "not 1 - 8: not known; a TCP/IP guest's mode: not taken");
    p = Read(false, 3, false, 0);
    CHECK(p.count == 0 && !p.takes, "nothing read: nothing known");
    CHECK(RipTarget(0x1000, 0x10, 7) == 0x1017 && RipTarget(0x1000, -0x20, 6) == 0x1000 - 0x1A, "rip targets");
}

// -- against the snapshot ------------------------------------------------------------------------------------------------------------------------
static std::vector<uint8_t> g_img;
static bool At(uint64_t rva, const uint8_t* bytes, size_t n) { return rva + n <= g_img.size() && std::memcmp(&g_img[rva], bytes, n) == 0; }
static int32_t I32(uint64_t rva) { int32_t v = 0; std::memcpy(&v, &g_img[rva], 4); return v; }
static uint64_t CallTarget(uint64_t rva) { return g_img[rva] == 0xE8 ? RipTarget(rva, I32(rva + 1), 5) : 0; }
static std::string Str(uint64_t rva) { return rva < g_img.size() ? std::string((const char*)&g_img[rva]) : std::string(); }

static void TestSnapshot() {
    CHECK(At(RVA_CHAT_COMMANDS, kSigChatCommands, sizeof kSigChatCommands), "the parser's first bytes");
    const uint64_t lea = RVA_CHAT_COMMANDS + kPlayersWordAt;
    CHECK(At(lea, kLeaRdx, sizeof kLeaRdx) && Str(RipTarget(lea, I32(lea + 3), 7)) == kPlayersWord, "its /players branch compares the line with \"/players\"");
    // the other commands of the parser, for the record: the branch is among them
    for (uint64_t at : {0x18875Bull, 0x188786ull, 0x1887B1ull, 0x1887DCull})
        CHECK(Str(RipTarget(at, I32(at + 3), 7)).rfind("/f", 0) == 0, "the /fps ... commands before it");
    static const uint8_t kEight[] = {0x41, 0xBC, 0x08, 0x00, 0x00, 0x00};   // mov r12d, 8: the letters compared, and the most players
    CHECK(At(lea + 7, kEight, sizeof kEight), "8 letters compared (strncmp), 8 the most");
    CHECK(CallTarget(0x188837) == RVA_GAME_MODE && CallTarget(0x188840) == RVA_GAME_MODE && CallTarget(0x18884A) == RVA_GAME_MODE, "it asks the mode getter");
    static const uint8_t kIs0[] = {0x85, 0xC0, 0x74}, kIs5[] = {0x83, 0xF8, 0x05, 0x74}, kIs7[] = {0x83, 0xF8, 0x07, 0x75};
    CHECK(At(0x18883C, kIs0, 3) && At(0x188845, kIs5, 4) && At(0x18884F, kIs7, 4), "and takes it in modes 0, 5 and 7 only (TakesPlayers)");
    static const uint8_t kAfterWord[] = {0x48, 0x8D, 0x8D, 0xD8, 0x01, 0x00, 0x00};   // lea rcx, [rbp+0x1D8]: the line's copy at +0x1D0, 8 letters on
    static const uint8_t kLineCopy[] = {0x48, 0x8D, 0x8D, 0xD0, 0x01, 0x00, 0x00};
    CHECK(At(0x188854, kAfterWord, sizeof kAfterWord) && At(lea + 0x18, kLineCopy, sizeof kLineCopy), "the number is read right after the word");
    static const uint8_t kAtLeast1[] = {0x41, 0xBF, 0x01, 0x00, 0x00, 0x00, 0x41, 0x3B, 0xC7, 0x44, 0x0F, 0x4F, 0xF8, 0x45, 0x3B, 0xFC, 0x45, 0x0F, 0x4C, 0xE7};
    CHECK(At(0x188860, kAtLeast1, sizeof kAtLeast1), "at least 1, at most 8 (Clamp)");
    CHECK(CallTarget(0x188874) == RVA_PLAYERS_SETTING && CallTarget(0x18887F) == 0xD2F020, "into the setting, through its own setter");
    // the setting: its accessor's first bytes, the object it hands out, the count at +0xC, its name
    CHECK(At(RVA_PLAYERS_SETTING, kSigPlayersSetting, sizeof kSigPlayersSetting), "the setting accessor's first bytes");
    const uint64_t sl = RVA_PLAYERS_SETTING + kSettingLeaAt;
    const uint64_t obj = RipTarget(sl, I32(sl + 3), 7);
    CHECK(At(sl, kLeaRcx, sizeof kLeaRcx) && obj > 0x228F000 && obj < 0x3BFF000, "lea rcx, [the setting] at +0x2E: an object in .data");
    static const uint8_t kRet[] = {0x48, 0x8D, 0x05};   // lea rax, [the same]: what it returns
    CHECK(At(RVA_PLAYERS_SETTING + 0x4F, kRet, 3) && RipTarget(RVA_PLAYERS_SETTING + 0x4F, I32(RVA_PLAYERS_SETTING + 0x52), 7) == obj, "and returns that object");
    static const uint8_t kStore[] = {0x89, 0x46, 0x0C};   // mov [rsi+0xC], eax: the setter keeps the value at +0xC
    CHECK(At(0xD2ED5C, kStore, 3) && kSettingValue == 0xC, "the setter keeps the count at +0xC");
    CHECK(Str(RipTarget(0xE1A8B, I32(0xE1A8E), 7)) == "Offline Difficulty Scaling", "the setting is Options > Gameplay's Offline Difficulty Scaling");
    // the mode getter
    CHECK(At(RVA_GAME_MODE, kSigGameMode, sizeof kSigGameMode) && g_img[RVA_GAME_MODE + kModeRet] == 0xC3, "the mode getter: mov eax, [rip + the mode]; ret");
    const uint64_t mode = RipTarget(RVA_GAME_MODE, I32(RVA_GAME_MODE + 2), kModeRet);
    CHECK(mode > 0x228F000 && mode < 0x3BFF000, "the mode is in .data");
    // the chat box: a sent line goes to the parser as (line, its copy, ..., 0)
    static const uint8_t kStart0[] = {0x45, 0x33, 0xC9};   // xor r9d, r9d
    CHECK(CallTarget(0x2E36BB) == RVA_CHAT_COMMANDS && At(0x2E36B5, kStart0, 3), "the chat box hands its line to the parser, from its start");
    // the listener: the count handed to the game, and its own "players set to N" (the event message 0x5A, sub 0x16 / 0x17)
    static const uint8_t kEvent[] = {0xC6, 0x44, 0x24, 0x20, 0x5A};
    CHECK(CallTarget(0xE0244) == 0x425A60 && At(0xE026C, kEvent, sizeof kEvent), "the setting's listener tells the game and says so");
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
