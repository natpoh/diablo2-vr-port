#pragma once
// The Party page of the in-headset settings ([openxr] headset_party): the players in this game and the game's own party buttons, for a player
// with controllers and no P key. Serg, 2026-11-01: "как пригласить в пати без клавиши P, если с контроллера играешь? Надо в меню кнопку
// пригласить в пати, чтобы у нас в оверлее был список игроков и кнопочка" - an ally shows on the automap (and so on the map in the hand) only
// in one party.
//
// What the page shows is the game's own roster (vr/party_cmd.h reads it on the UI thread); what its buttons do is what the game's Party panel's
// buttons do (vr/party_cmd.cpp calls the very functions its message handler calls): a player's one button is the panel's ProcessInviteButton -
// Invite, Accept or Cancel by the state the game keeps for that player - and Leave party is the panel's LeaveParty. Open Party panel presses
// the game's own Party key command (P). Reached from the Co-op page's Party button (the bottom bar), back by its Relay button; no tab of its own.
//
// Pure: no Windows, no game. hset_test.exe runs it.

#include <cstdint>
#include <string>
#include <vector>

#include "ui_text.h"

namespace hset::party {

// A player's state as the game keeps it (the roster entry's +0x68; the Party panel's invite button and its tooltips go by it, strParty1 - 6).
enum State : uint32_t { kNone = 0, kInParty = 1, kInvitedUs = 2, kState3 = 3, kWeInvited = 4 };
// What the player's button does - the game's handler picks it by that same state: 0 invite, 2 accept, 4 cancel. Others: no button.
enum class Act : uint8_t { None, Invite, Accept, Cancel };
constexpr Act ActFor(uint32_t state) { return state == kNone ? Act::Invite : state == kInvitedUs ? Act::Accept : state == kWeInvited ? Act::Cancel : Act::None; }

constexpr int kMaxOthers = 7;   // a game holds 8 players: the others

struct Member {
    uint32_t id = 0;          // the unit id (what the game's panel hands its handler)
    std::string name;
    int cls = -1;             // 0 Amazon ... 7 Warlock; -1 not known
    int level = 0;
    uint32_t state = kNone;   // State
    bool sameParty = false;   // in our party (the same party id, not "none")
    bool operator==(const Member& o) const {
        return id == o.id && name == o.name && cls == o.cls && level == o.level && state == o.state && sameParty == o.sameParty;
    }
};
// What the plugin read of the game: in a multiplayer game, the others of its roster (up to kMaxOthers, in the roster's order), whether we are
// in a party. `known` false: not in a game, a game of one's own (single player), or the game's party code not found.
struct View {
    bool known = false;
    bool inParty = false;     // our own entry has a party
    std::vector<Member> others;
    bool operator==(const View& o) const { return known == o.known && inParty == o.inParty && others == o.others; }
    bool operator!=(const View& o) const { return !(*this == o); }
};

// A press for the plugin: one player's button (the state it was pressed in: the plugin acts only while the game still has it), Leave, Open.
enum class Ask : uint8_t { None, Player, Leave, OpenPanel };
struct Request {
    Ask what = Ask::None;
    uint32_t id = 0;
    uint32_t state = 0;
    Act act = Act::None;
};

// How long a press waits for the game to show its answer (the player's state moves) before the button takes presses again.
constexpr double kWaitMs = 1500.0;

// -- texts ------------------------------------------------------------------------------------------------------
inline const char* ClassName(int cls) {
    static const char* const k[8] = {"Amazon", "Sorceress", "Necromancer", "Paladin", "Barbarian", "Druid", "Assassin", "Warlock"};
    return cls >= 0 && cls < 8 ? k[cls] : "";
}
inline std::string InfoText(const Member& m) {
    const std::string lv = uitext::TrF("vr.party.level", "Level {n}", {{"n", std::to_string(m.level)}});
    return *ClassName(m.cls) ? lv + " " + ClassName(m.cls) : lv;
}
inline const char* StateText(const Member& m) {
    if (m.sameParty || m.state == kInParty) return uitext::Tr("vr.party.state.in_party", "In your party");
    if (m.state == kInvitedUs) return uitext::Tr("vr.party.state.invited_us", "Invites you");
    if (m.state == kWeInvited) return uitext::Tr("vr.party.state.we_invited", "Invited");
    return "";
}
inline const char* ActText(Act a) {
    switch (a) {
        case Act::Invite: return uitext::Tr("vr.party.invite", "Invite");
        case Act::Accept: return uitext::Tr("vr.party.accept", "Accept");
        case Act::Cancel: return uitext::Tr("vr.party.cancel", "Cancel");
        default: return "";
    }
}

}  // namespace hset::party
