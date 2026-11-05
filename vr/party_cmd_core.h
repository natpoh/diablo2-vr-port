#pragma once
// party_cmd_core.h - the pure part of vr/party_cmd.cpp (the headset's Party page): where the game keeps its party roster and the functions its
// Party panel's buttons call, the bytes they are checked by, and how a roster read becomes the page's list. tools/party_cmd_test.cpp tests it,
// and against a snapshot of the game's image (dump_loader/D2R_image.bin) that the places and bytes are the game's.
//
// The game (D2R 3.3.93787 under D2RLoader 1.3.1, read off the decrypted image, 2026-11-01): the Party panel (PartyPanelHD.json, its widget's
// message handler 0x1470D10) takes "PartyPanelMessage:ProcessInviteButton" from a player's button as entry = 0x136200(the player's unit id),
// then 0x136CB0(entry): by the entry's state (+0x68) it sends invite (0), accept (2) or cancel (4) - its own sender 0xECA00 - and keeps the new
// state; "PartyPanelMessage:LeaveParty" is 0x1367E0(): our entry out of the party, then the game's leave. The roster is a list from the global
// 0x2A4DB90 (0x136200's first instruction): name +0x00 (up to 0x3D letters; the entry's maker 0x13A220 copies it so), unit id +0x48, class +0x54, level +0x58 (the game's 0x136430 reads it),
// party id +0x5A (0xFFFF none; 0x139CF0 writes it on leaving), state +0x68 (0x136490 reads it), the next entry +0x148.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "hset_party.h"

namespace partycmd::core {

// Addresses in 3.3.93787 (the keys of game_sigs.h: PartyEntry, PartyInvite, PartyLeave).
constexpr uint64_t RVA_PARTY_ENTRY = 0x136200;    // the roster entry of a unit id: mov rdx, [rip + the roster]; cmp ecx, -1; je ...
constexpr uint64_t RVA_PARTY_INVITE = 0x136CB0;   // the Party panel's ProcessInviteButton (entry)
constexpr uint64_t RVA_PARTY_LEAVE = 0x1367E0;    // the Party panel's LeaveParty ()
constexpr uint64_t RVA_GAME_MODE = 0x8AF90;       // the game's mode (0 offline: no party)

inline constexpr uint8_t kSigPartyEntry[3] = {0x48, 0x8B, 0x15};   // mov rdx, [rip + disp32] (7 bytes) ...
inline constexpr uint8_t kSigPartyEntryTail[6] = {0x83, 0xF9, 0xFF, 0x74, 0x2E, 0x48};   // ... cmp ecx, -1; je; test rdx, rdx
inline constexpr uint8_t kSigPartyInvite[13] = {0x48, 0x85, 0xC9, 0x0F, 0x84, 0x4B, 0x04, 0x00, 0x00, 0x48, 0x89, 0x5C, 0x24};
inline constexpr uint8_t kSigPartyLeave[4] = {0x48, 0x83, 0xEC, 0x28};   // sub rsp, 0x28; call ...; mov ecx, 7 (the game's leave)
inline constexpr uint8_t kPartyLeaveSeven[5] = {0xB9, 0x07, 0x00, 0x00, 0x00};   // at +9
constexpr uint32_t kLeaveSevenAt = 9;
inline constexpr uint8_t kSigGameMode[2] = {0x8B, 0x05};

// The game's Show Party / Show Party Names on its automap (the Co-op page's map row, hset_party.h MapShow; vr/automap_party.h has why). Read: the
// game's own getters, each `mov eax, [rip + its value]; ret` (the values the settings "AutoMap Party" 0xE2280 / "AutoMap Party Names" 0xE2160
// put there at load and on every change, 0xD3DD0 / 0xD3DC0). Flipped: the game's key commands 10 / 11, whose down() is the game's toggle of that
// setting (0x119E90: `setting = !getter()`, the setting's own setter 0xD2F020) - checked in the command table (0x22A7930, 0x18 a command) first.
constexpr uint64_t RVA_MAP_PARTY_GET = 0xD34A0, RVA_MAP_NAMES_GET = 0xD3490;
constexpr uint64_t RVA_MAP_PARTY_DOWN = 0x119E90, RVA_MAP_NAMES_DOWN = 0x119EC0;
constexpr uint64_t RVA_CMD_TABLE = 0x22A7930;
constexpr uint32_t kCmdEntry = 0x18;
inline constexpr uint8_t kSigMapGet[2] = {0x8B, 0x05};   // mov eax, [rip + disp32] ... (6 bytes), then ret
constexpr uint8_t kRet = 0xC3;
inline constexpr uint8_t kSigMapDown[5] = {0x40, 0x53, 0x48, 0x83, 0xEC};   // push rbx; sub rsp, 0x20; call the getter ...
constexpr uint32_t kDownCallAt = 6;   // call rel32 to the getter at +6 (5 bytes)
// The target of a call rel32 at `at`.
constexpr uint64_t CallTarget(uint64_t at, int32_t rel) { return at + 5 + (uint64_t)(int64_t)rel; }
// A value read from the game: 0 / 1 as the game has it, anything else (garbage) not known.
constexpr int MapValueOf(int32_t raw) { return raw == 0 ? 0 : raw == 1 ? 1 : -1; }

// A roster entry's fields.
constexpr uint32_t kName = 0x00, kNameLen = 0x3D, kId = 0x48, kClass = 0x54, kLevel = 0x58, kPartyId = 0x5A, kState = 0x68, kNext = 0x148;
constexpr uint32_t kEntryRead = 0x6C;   // the bytes read of an entry (to its state)
constexpr uint16_t kNoParty = 0xFFFF;
constexpr int kMaxEntries = 16;         // a roster longer than this is not one (a loop, garbage)

constexpr uint64_t RipTarget(uint64_t at, int32_t disp, uint32_t len) { return at + len + (uint64_t)(int64_t)disp; }

// One entry as read (the first kEntryRead bytes).
struct Raw {
    uint8_t b[kEntryRead] = {};
    uint32_t U32(uint32_t at) const { uint32_t v; std::memcpy(&v, b + at, 4); return v; }
    uint16_t U16(uint32_t at) const { uint16_t v; std::memcpy(&v, b + at, 2); return v; }
};
// The name as the game keeps it: up to 0x3D letters, NUL-ended; what is not a printable letter ends it.
inline std::string NameOf(const Raw& r) {
    std::string s;
    for (uint32_t i = 0; i < kNameLen; ++i) {
        const unsigned char c = r.b[kName + i];
        if (c < 0x20 || c > 0x7E) break;
        s += (char)c;
    }
    return s;
}
// The page's list from the roster read in order and our own unit id: the others (up to kMaxOthers), and whether we are in a party.
inline hset::party::View Build(const std::vector<Raw>& roster, uint32_t localId) {
    hset::party::View v;
    v.known = true;
    uint16_t ours = kNoParty;
    for (const Raw& r : roster) if (r.U32(kId) == localId) ours = r.U16(kPartyId);
    v.inParty = ours != kNoParty;
    for (const Raw& r : roster) {
        if (r.U32(kId) == localId || (int)v.others.size() >= hset::party::kMaxOthers) continue;
        hset::party::Member m;
        m.id = r.U32(kId);
        m.name = NameOf(r);
        const uint32_t cls = r.U32(kClass);
        m.cls = cls < 8 ? (int)cls : -1;
        m.level = r.U16(kLevel);
        m.state = r.U32(kState);
        m.sameParty = ours != kNoParty && r.U16(kPartyId) == ours;
        v.others.push_back(m);
    }
    return v;
}

}  // namespace partycmd::core
