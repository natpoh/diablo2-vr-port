// client_verdict.h - hit by touch's "hostile? a wall between?" from the CLIENT's own units, for a game whose server is not in
// this process (a TCP/IP client: the server runs on the host's PC). Pure: the reads and the game's line test are handed in, so
// the rules are tested against made-up units (cleanroom/tests/client_verdict_test.cpp).
//
// Where the server is here (offline, or this PC hosts), melee_touch.cpp keeps asking the server's own Relation and Line, as it
// always has; this is only what stands in when there is no server to ask. The press is still the game's own attack: whatever
// these say, the host's server decides what comes of it - they only keep a touch from pressing at a pet, in town or through a wall.
//
// What the client knows, read in a game 2026-10-22 (3.3.93787, tools/pet_recon.py, offline Assassin with a Rogue hireling):
//   - monster unit +0x10 -> its monster data; data +0x54 is its OWNER's unit id: the hero's id (1) on the hireling,
//     0xFFFFFFFF on every wild monster (zombies, Fallen) and every town NPC (Rogues, Akara, Kashya, Gheed, Warriv).
//   - the hero's area by D2RCore's GetLevelIdFromUnit; the five towns are never a fight.
// The owner field has no code-site guard yet; instead a value that is neither 0xFFFFFFFF nor a player the client knows marks the
// layout as not understood, and the touch then presses nothing for that monster (as with no verdict at all).
#pragma once

#include <cstddef>
#include <cstdint>

namespace clientverdict {

constexpr size_t kMonsterData = 0x10;     // monster unit -> its data block
constexpr size_t kOwnerId = 0x54;         // data block -> the owner's unit id
constexpr uint32_t kNoOwner = 0xFFFFFFFFu;

// Rogue Encampment, Lut Gholein, Kurast Docks, Pandemonium Fortress, Harrogath (Levels.txt ids).
inline bool IsTown(int levelId) {
    return levelId == 1 || levelId == 40 || levelId == 75 || levelId == 103 || levelId == 109;
}

enum class Owner { None, Player, NotUnderstood };

// The owner field against the players the client knows (their unit ids).
inline Owner ClassifyOwner(uint32_t owner, const uint32_t* players, size_t count) {
    if (owner == kNoOwner) return Owner::None;
    for (size_t i = 0; i < count; ++i)
        if (players[i] == owner) return Owner::Player;
    return Owner::NotUnderstood;
}

struct Facts {
    bool unitRead = false;   // the monster's unit and its data block read
    uint32_t mode = 0;       // its mode (0 dying, 12 dead)
    uint32_t owner = kNoOwner;
    int heroLevel = 0;       // 0: the area is not known
    int line = -1;           // the game's line test: -1 not answered, 0 clear, else a wall
};

struct Verdict {
    bool known = false;      // false: no answer, the touch presses nothing (as before, with no server)
    bool hostile = false;
    bool clear = false;
};

// players: the unit ids of the players the client knows (the hero among them).
inline Verdict Decide(const Facts& f, const uint32_t* players, size_t count) {
    Verdict v;
    if (!f.unitRead || f.mode == 0 || f.mode == 12) return v;
    if (f.heroLevel <= 0) return v;   // where the hero is, is not known: no answer rather than a guess
    v.known = true;
    if (IsTown(f.heroLevel)) return v;   // known, not hostile: nobody fights in town
    const Owner owner = ClassifyOwner(f.owner, players, count);
    if (owner == Owner::NotUnderstood) {
        v.known = false;   // the field is not what it was read as: say nothing
        return v;
    }
    v.hostile = owner == Owner::None;
    // A line test that did not answer leaves the touch free: it is at arm's length, and the host's server still checks.
    v.clear = v.hostile && f.line <= 0;
    return v;
}

}  // namespace clientverdict
