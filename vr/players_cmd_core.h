#pragma once
// players_cmd_core.h - the pure part of vr/players_cmd.cpp (the game's /players N from the headset): where the game has the pieces, the
// bytes they are checked by, the line sent, how the answer is read. No Windows, no game: tools/players_cmd_test.cpp tests it, and against
// a snapshot of the game's image (dump_loader/D2R_image.bin) that the bytes and the places are the game's.

#include <algorithm>
#include <cstdint>
#include <string>

#include "hset_model.h"   // hset::Players, kPlayersMin / kPlayersMax

namespace playerscmd::core {

// Addresses in 3.3.93787 (the keys of game_sigs.h: ChatCommands, PlayersSetting, GameMode).
constexpr uint64_t RVA_CHAT_COMMANDS = 0x188700;   // the chat command parser (text, text, 0, 0)
constexpr uint64_t RVA_PLAYERS_SETTING = 0xE19E0;  // the Offline Difficulty Scaling setting's accessor
constexpr uint64_t RVA_GAME_MODE = 0x8AF90;        // the game's mode: mov eax, [rip + disp]; ret

// The parser: push rbp/rbx/rsi/rdi; lea rbp, [rsp-0x728]; sub rsp, 0x828 (no rip, no rel32).
inline constexpr uint8_t kSigChatCommands[20] = {0x40, 0x55, 0x53, 0x56, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0xD8,
                                                 0xF8, 0xFF, 0xFF, 0x48, 0x81, 0xEC, 0x28, 0x08, 0x00, 0x00};
// ... and its /players branch: lea rdx, [rip + "/players"] at +0x10F (7 bytes), the word it compares the line with (8 letters).
constexpr uint32_t kPlayersWordAt = 0x10F;
inline constexpr uint8_t kLeaRdx[3] = {0x48, 0x8D, 0x15};
inline constexpr char kPlayersWord[] = "/players";
// The setting's accessor: sub rsp, 0x58; mov rax, gs:[0x58] - and at +0x2E lea rcx, [rip + the setting] (7 bytes); its count at +0xC.
inline constexpr uint8_t kSigPlayersSetting[13] = {0x48, 0x83, 0xEC, 0x58, 0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00};
constexpr uint32_t kSettingLeaAt = 0x2E;
inline constexpr uint8_t kLeaRcx[3] = {0x48, 0x8D, 0x0D};
constexpr uint32_t kSettingValue = 0xC;
// The mode getter: mov eax, [rip + the mode] (6 bytes); ret.
inline constexpr uint8_t kSigGameMode[2] = {0x8B, 0x05};
constexpr uint32_t kModeRet = 6;

// Where an instruction at `at` that ends `len` bytes on, with its rip displacement `disp`, points.
constexpr uint64_t RipTarget(uint64_t at, int32_t disp, uint32_t len) { return at + len + (uint64_t)(int64_t)disp; }

// The parser takes /players in these modes only (0x188837: 0, 5, 7) - a game of one's own offline, a TCP/IP host's.
constexpr bool TakesPlayers(int mode) { return mode == 0 || mode == 5 || mode == 7; }
constexpr int Clamp(int n) { return std::clamp(n, hset::kPlayersMin, hset::kPlayersMax); }

// The line the chat box hands the parser, as a player types it.
inline std::string Line(int n) { return std::string(kPlayersWord) + " " + std::to_string(Clamp(n)); }

// What the row shows from what was read: the setting's count (not 1 - 8: not known - the setting not made yet), the mode.
inline hset::Players Read(bool countRead, int count, bool modeRead, int mode) {
    hset::Players p;
    p.count = countRead && count >= hset::kPlayersMin && count <= hset::kPlayersMax ? count : 0;
    p.takes = modeRead && TakesPlayers(mode);
    return p;
}

}  // namespace playerscmd::core
