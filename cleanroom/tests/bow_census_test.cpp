// The bow skeletons the game poses, and the hero's among them (vr/bow_census.h), without the game.
//
//   bow_census_test
//
// The log of 2026-10-14 (test-8, an Amazon with the Hydra Bow in weapon set 1 and a light crossbow in set 2): the bow rig's one shared
// "why" flipped every ~10 ms between "a bow that is not a known rig" (the crossbow) and "a bow that is not the hero's" (a bow that is
// another's), and never said what the hero's own long_battle_bow did. Checks: each skeleton keeps its own status; the hero's speaks for
// the rig; of two of his the one whose rig is known; stale ones leave the line; the line is the same for the same bows.

#include "bow_census.h"

#include <cstdio>
#include <string>

using namespace bowcensus;

namespace {

int g_fails = 0;
int g_checks = 0;

#define EXPECT(c, ...)                                         \
    do {                                                       \
        ++g_checks;                                            \
        if (!(c)) {                                            \
            ++g_fails;                                         \
            std::printf("  FAIL line %d: %s - ", __LINE__, #c); \
            std::printf(__VA_ARGS__);                          \
            std::printf("\n");                                 \
        }                                                      \
    } while (0)

Record Make(uint64_t instance, const char* model, int bones, Owner owner, bool known, const char* status, uint64_t seenAt, uint32_t entity = 0x4014C0, uint32_t parent = 0x4014BB) {
    Record r;
    r.instance = instance;
    r.model = model;
    r.bones = bones;
    r.owner = owner;
    r.known = known;
    r.status = status;
    r.seenAt = seenAt;
    r.entity = entity;
    r.parent = parent;
    return r;
}

bool Has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

void TestTheLogOfThatDay() {
    const uint64_t now = 100000;
    // the hero's long bow, his other set's crossbow (his entity too, rig not known), and another's short bow
    std::vector<Record> rs = {
        Make(0x2000, "light_crossbow.skeleton", 15, Owner::Hero, false, "a bow that is not a known rig - the game's animation", now - 10),
        Make(0x3000, "short_battle_bow.skeleton", 18, Owner::Other, true, "not the hero's", now - 5, 0x501234, 0x5003AA),
        Make(0x1000, "long_battle_bow.skeleton", 18, Owner::Hero, true, "posed - drawn to the hands", now - 8, 0x4014C2),
    };
    EXPECT(std::string(HeroStatus(rs, now)) == "posed - drawn to the hands", "the hero's known bow speaks: %s", HeroStatus(rs, now));
    // swapped order of the records and of the poses: the same answer
    std::swap(rs[0], rs[2]);
    rs[0].seenAt = now - 1;   // the long bow is the newest now
    EXPECT(std::string(HeroStatus(rs, now)) == "posed - drawn to the hands", "order does not matter");
    rs[2].seenAt = now - 1;   // the crossbow is the newest: still not its word
    EXPECT(std::string(HeroStatus(rs, now)) == "posed - drawn to the hands", "a newer unknown rig of his does not outvote the known one");
    const std::string text = Text(rs, now);
    EXPECT(Has(text, "the hero's bow: long_battle_bow.skeleton, 18 bones (skeleton 1000, entity 4014C2): posed - drawn to the hands"), "%s", text.c_str());
    EXPECT(Has(text, "light_crossbow.skeleton, 15 bones (skeleton 2000"), "the crossbow is named: %s", text.c_str());
    EXPECT(Has(text, "short_battle_bow.skeleton, 18 bones (skeleton 3000, entity 501234, another's, hangs off entity 5003AA): not the hero's"), "%s", text.c_str());
    EXPECT(Has(text, "| other bow skeletons: "), "%s", text.c_str());
    // the crossbow is his (the owner says so) and so it is in the hero's list, after the known bow
    EXPECT(text.find("light_crossbow") > text.find("long_battle_bow") && text.find("light_crossbow") < text.find("| other"), "his crossbow is listed with his: %s", text.c_str());
    // the same bows, any order of the records: the same line
    std::vector<Record> shuffled = {rs[1], rs[2], rs[0]};
    EXPECT(Text(shuffled, now) == text, "the line does not depend on the order of the records");
}

void TestOnlyOthers() {
    const uint64_t now = 5000;
    const std::vector<Record> rs = {Make(1, "short_battle_bow.skeleton", 18, Owner::Other, true, "not the hero's", now - 20),
                                    Make(2, "light_crossbow.skeleton", 15, Owner::Other, false, "not the hero's", now - 30)};
    EXPECT(HeroStatus(rs, now) == nullptr, "no hero's bow: no status of his (the rig's own reason shows)");
    EXPECT(Has(Text(rs, now), "the hero's bow: none posed lately | other bow skeletons: "), "%s", Text(rs, now).c_str());
    EXPECT(Has(Text({}, now), "the hero's bow: none posed lately | other bow skeletons: none"), "nothing at all: %s", Text({}, now).c_str());
    EXPECT(HeroStatus({}, now) == nullptr, "empty");
}

void TestStale() {
    const uint64_t now = 100000;
    std::vector<Record> rs = {Make(1, "long_battle_bow.skeleton", 18, Owner::Hero, true, "posed - held at rest after the release", now - 1499),
                              Make(2, "short_battle_bow.skeleton", 18, Owner::Other, true, "not the hero's", now - 1501),
                              Make(3, "light_crossbow.skeleton", 15, Owner::Other, false, "not the hero's", 0)};
    EXPECT(HeroStatus(rs, now) != nullptr, "1.499 s ago is still lately");
    const std::string text = Text(rs, now);
    EXPECT(!Has(text, "short_battle_bow") && !Has(text, "light_crossbow"), "1.501 s ago and never leave the line: %s", text.c_str());
    EXPECT(HeroStatus(rs, now + 2) == nullptr, "1.501 s ago: gone");
    // a clock gone back (seenAt in the future by more than a second) is not "lately"
    rs[0].seenAt = now + 5000;
    EXPECT(HeroStatus(rs, now) == nullptr && !Has(Text(rs, now), "long_battle_bow"), "a stamp from the future is dropped");
    rs[0].seenAt = now + 500;   // a little ahead (two threads' clocks) is fine
    EXPECT(HeroStatus(rs, now) != nullptr, "a stamp 0.5 s ahead is kept");
}

void TestUnknownOwner() {
    const uint64_t now = 9000;
    const std::vector<Record> rs = {Make(7, "long_battle_bow.skeleton", 18, Owner::Unknown, true, "its entity is not found yet", now - 3)};
    EXPECT(HeroStatus(rs, now) == nullptr, "an owner not found yet is not the hero's word");
    const std::string text = Text(rs, now);
    EXPECT(Has(text, "owner not found yet") && Has(text, "its entity is not found yet"), "told as such: %s", text.c_str());
}

void TestTwoKnownOfHis() {
    const uint64_t now = 7000;
    // two bows of his with known rigs (both weapon sets hold a bow): the one posed last speaks
    const std::vector<Record> rs = {Make(1, "long_battle_bow.skeleton", 18, Owner::Hero, true, "posed - drawn to the hands", now - 40),
                                    Make(2, "short_battle_bow.skeleton", 18, Owner::Hero, true, "its axes are read; the string is not drawn", now - 4)};
    EXPECT(std::string(HeroStatus(rs, now)) == "its axes are read; the string is not drawn", "the newer of two known: %s", HeroStatus(rs, now));
}

}  // namespace

int main() {
    TestTheLogOfThatDay();
    TestOnlyOthers();
    TestStale();
    TestUnknownOwner();
    TestTwoKnownOfHis();
    std::printf("%d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
