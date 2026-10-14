// bow_census.h - which bow skeletons the game has posed lately, whose they are, and what the bow rig did with each. Pure: the hook keeps
// a record per skeleton instance, this turns the records into one line for vrcam's log and names the status that is the hero's.
//
// The game keeps more than one bow skeleton alive: the one in the hero's hand, the one of his other weapon set (a crossbow, say), a
// preview of his gear, another player's. Each pose of each of them used to write one shared "why" - so the log's status flipped
// between the other ones' reasons every few milliseconds ("not a known rig" / "not the hero's") and never said what the hero's own bow was
// doing. Here every skeleton keeps its own status, and the hero's is told apart.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace bowcensus {

enum class Owner : int { Unknown = 0, Hero = 1, Other = 2 };   // whose entity the skeleton belongs to (Unknown: not looked up yet)

struct Record {
    uint64_t instance = 0;         // the SkeletonInstance's address
    std::string model;             // the model's file name (long_battle_bow.skeleton)
    int bones = 0;
    Owner owner = Owner::Unknown;
    bool known = false;            // its bones are a layout the rig knows (a crossbow's are not)
    uint32_t entity = 0, parent = 0;   // the bow's entity, and the entity it hangs off (the hero's, for his)
    const char* status = "";       // what its pose last did
    uint64_t seenAt = 0;           // GetTickCount64 of that pose
};

inline const char* OwnerName(Owner o) { return o == Owner::Hero ? "the hero's" : o == Owner::Other ? "another's" : "owner not found yet"; }

// Records posed within `staleMs` of `now`, the hero's first, then by model and instance, so that the same bows give the same line.
inline std::vector<Record> Fresh(std::vector<Record> all, uint64_t now, uint64_t staleMs = 1500) {
    all.erase(std::remove_if(all.begin(), all.end(), [&](const Record& r) { return !r.seenAt || r.seenAt > now + 1000 || now - std::min(now, r.seenAt) > staleMs; }), all.end());
    std::sort(all.begin(), all.end(), [](const Record& a, const Record& b) {
        const int ka = a.owner == Owner::Hero ? 0 : a.owner == Owner::Unknown ? 1 : 2, kb = b.owner == Owner::Hero ? 0 : b.owner == Owner::Unknown ? 1 : 2;
        if (ka != kb) return ka < kb;
        if (a.known != b.known) return a.known;   // the one whose rig is known first
        if (a.model != b.model) return a.model < b.model;
        return a.instance < b.instance;
    });
    return all;
}

// One line: "the hero's: <model> <n> bones (skeleton .., entity ..): <status>; ... | others: ...".
inline std::string Text(const std::vector<Record>& all, uint64_t now, uint64_t staleMs = 1500) {
    const std::vector<Record> rs = Fresh(all, now, staleMs);
    std::string mine, others;
    for (const Record& r : rs) {
        char b[360], whose[96];
        if (r.owner == Owner::Other) snprintf(whose, sizeof whose, "%s, hangs off entity %X", OwnerName(r.owner), r.parent);
        else snprintf(whose, sizeof whose, "%s", OwnerName(r.owner));
        if (r.owner == Owner::Hero)
            snprintf(b, sizeof b, "%s%s, %d bones (skeleton %llX, entity %X): %s", mine.empty() ? "" : "; ", r.model.c_str(), r.bones, (unsigned long long)r.instance, r.entity, r.status);
        else
            snprintf(b, sizeof b, "%s%s, %d bones (skeleton %llX, entity %X, %s): %s", others.empty() ? "" : "; ", r.model.c_str(), r.bones, (unsigned long long)r.instance, r.entity, whose, r.status);
        (r.owner == Owner::Hero ? mine : others) += b;
    }
    return "the hero's bow: " + (mine.empty() ? std::string("none posed lately") : mine) + " | other bow skeletons: " + (others.empty() ? std::string("none") : others);
}

// The status of the hero's own bow, or nullptr when none of his was posed lately. If two of his are alive (the other weapon set's
// crossbow hangs off him too) the one whose rig is known speaks, and of two such the one posed last.
inline const char* HeroStatus(const std::vector<Record>& all, uint64_t now, uint64_t staleMs = 1500) {
    const Record* best = nullptr;
    for (const Record& r : all) {
        if (r.owner != Owner::Hero || !r.seenAt || r.seenAt > now + 1000 || now - std::min(now, r.seenAt) > staleMs) continue;
        if (!best || (r.known && !best->known) || (r.known == best->known && r.seenAt > best->seenAt)) best = &r;
    }
    return best ? best->status : nullptr;
}

}  // namespace bowcensus
