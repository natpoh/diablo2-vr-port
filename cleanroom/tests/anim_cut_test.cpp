// [bow] quick_release and [hands] quick_throw (vr/anim_cut.h) on a synthetic AnimData block, without the game and without a headset: the shape check of a bow attack row, the move of its action-frame
// flag and the way back (exact), the walk over the block's buckets through a reader, and what is refused - every row that is not what was read.
//
//   anim_cut_test

#include "../../vr/anim_cut.h"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace animcut;

namespace {

int g_fails = 0;
#define EXPECT(c, ...)                                         \
    do {                                                       \
        if (!(c)) {                                            \
            ++g_fails;                                         \
            std::printf("  FAIL line %d: %s - ", __LINE__, #c); \
            std::printf(__VA_ARGS__);                          \
            std::printf("\n");                                 \
        }                                                      \
    } while (0)

using Bytes = std::vector<uint8_t>;

// (the bow's kind, which these older checks are about; the throw's own tests are at the end)
Verdict CheckBow(const uint8_t* row, const char* token, Row* out = nullptr) { return animcut::Check(row, token, Kind::Bow, out); }
bool MoveBow(uint8_t* row, const char* token, int to, Row* before = nullptr) { return animcut::Move(row, token, Kind::Bow, to, before); }

Bytes MakeRow(const char* name, uint32_t frames, uint32_t speed, int flagAt, int flagVal) {
    Bytes r(kRowSize, 0);
    std::memcpy(r.data(), name, std::strlen(name) < 7 ? std::strlen(name) : 7);
    std::memcpy(r.data() + kFramesAt, &frames, 4);
    std::memcpy(r.data() + kSpeedAt, &speed, 4);
    if (flagAt >= 0) r[kFlagsAt + (size_t)flagAt] = (uint8_t)flagVal;
    return r;
}

// A block shaped like the file: 256 buckets, each a u32 count and that many rows. `rows` go into the bucket of their index modulo 256 (the real hash differs; the walk
// does not care), a few filler rows ahead of them, and the offset each row landed at is returned.
struct Block {
    Bytes bytes;
    size_t offsetOf[16] = {};
};
Block Build(const std::vector<Bytes>& rows) {
    std::vector<std::vector<Bytes>> buckets(256);
    for (int i = 0; i < 700; ++i) {   // fillers: other tokens, modes, weapon classes
        char n[8];
        std::snprintf(n, sizeof n, "F%c%c%c%c%c%c", 'A' + i % 20, 'A' + (i / 7) % 20, '1' + i % 4, 'A' + (i / 3) % 20, 'A' + i % 5, 'A' + (i / 11) % 7);
        buckets[(size_t)(i * 37) % 256].push_back(MakeRow(n, 8 + (uint32_t)i % 12, 256, i % 8, 1 + i % 2));
    }
    std::vector<std::pair<size_t, size_t>> where;   // bucket, index
    for (size_t i = 0; i < rows.size(); ++i) {
        const size_t b = (i * 61 + 5) % 256;
        buckets[b].push_back(rows[i]);
        where.push_back({b, buckets[b].size() - 1});
    }
    Block out;
    std::vector<size_t> starts(256);
    for (int b = 0; b < 256; ++b) {
        starts[(size_t)b] = out.bytes.size();
        const uint32_t n = (uint32_t)buckets[(size_t)b].size();
        out.bytes.insert(out.bytes.end(), (const uint8_t*)&n, (const uint8_t*)&n + 4);
        for (const Bytes& r : buckets[(size_t)b]) out.bytes.insert(out.bytes.end(), r.begin(), r.end());
    }
    for (size_t i = 0; i < where.size() && i < 16; ++i) out.offsetOf[i] = starts[where[i].first] + 4 + where[i].second * kRowSize;
    return out;
}

bool ReadFrom(const Bytes& b, size_t off, void* dst, size_t n) {
    if (off + n > b.size()) return false;
    std::memcpy(dst, b.data() + off, n);
    return true;
}

void TestCheck() {
    std::printf("the shape of a bow attack row\n");
    Row r;
    Bytes am = MakeRow("AMA1BOW", 14, 256, 6, 2);
    EXPECT(CheckBow(am.data(), "AM", &r) == Verdict::Ok && r.frames == 14 && r.speed == 256 && r.flagIdx == 6 && r.flagVal == 2 && !std::strcmp(r.name, "AMA1BOW"), "the Amazon's row: %s", r.name);
    // every class's row, each only for its own token
    struct C { const char* name; const char* token; uint32_t frames; int at; };
    const C cs[] = {{"BAA1BOW", "BA", 15, 7}, {"AIA1BOW", "AI", 16, 7}, {"PAA1BOW", "PA", 16, 8}, {"NEA1BOW", "NE", 18, 9}, {"DZA1BOW", "DZ", 16, 8}, {"SOA1BOW", "SO", 17, 9}, {"WKA1BOW", "WK", 17, 11}};
    for (const C& c : cs) {
        Bytes row = MakeRow(c.name, c.frames, 256, c.at, 2);
        EXPECT(CheckBow(row.data(), c.token) == Verdict::Ok, "%s for %s", c.name, c.token);
        EXPECT(CheckBow(row.data(), "AM") == Verdict::Name, "%s is not the Amazon's", c.name);
    }
    EXPECT(CheckBow(MakeRow("AMA2BOW", 14, 256, 6, 1).data(), "AM") == Verdict::Ok, "attack 2 too, flag value 1");
    // not a bow attack
    EXPECT(CheckBow(MakeRow("AMS1BOW", 9, 256, 2, 2).data(), "AM") == Verdict::Name, "special 1 is another row");
    EXPECT(CheckBow(MakeRow("AMSCBOW", 20, 256, 10, 2).data(), "AM") == Verdict::Name, "cast is another row");
    EXPECT(CheckBow(MakeRow("AMA1XBW", 20, 256, 9, 2).data(), "AM") == Verdict::Name, "a crossbow is another row");
    EXPECT(CheckBow(MakeRow("AMA11HS", 16, 256, 10, 1).data(), "AM") == Verdict::Name, "a sword is another row");
    EXPECT(CheckBow(MakeRow("SKA1BOW", 16, 200, 13, 2).data(), "AM") == Verdict::Name, "a skeleton archer's is not a hero's");
    EXPECT(CheckBow(am.data(), nullptr) == Verdict::Name && CheckBow(am.data(), "A") == Verdict::Name && CheckBow(am.data(), "AMX") == Verdict::Name, "no token / a bad token");
    Bytes noNul = am; noNul[7] = 'X';
    EXPECT(CheckBow(noNul.data(), "AM") == Verdict::Name, "a name that does not end");
    // shape
    EXPECT(CheckBow(MakeRow("AMA1BOW", 3, 256, 1, 2).data(), "AM") == Verdict::Frames && CheckBow(MakeRow("AMA1BOW", 100, 256, 6, 2).data(), "AM") == Verdict::Frames, "frames out of range");
    EXPECT(CheckBow(MakeRow("AMA1BOW", 14, 0, 6, 2).data(), "AM") == Verdict::Speed && CheckBow(MakeRow("AMA1BOW", 14, 9999, 6, 2).data(), "AM") == Verdict::Speed, "speed out of range");
    EXPECT(CheckBow(MakeRow("AMA1BOW", 14, 256, -1, 0).data(), "AM") == Verdict::Flags, "no flag at all");
    EXPECT(CheckBow(MakeRow("AMA1BOW", 14, 256, 20, 2).data(), "AM") == Verdict::Flags, "a flag past the last frame");
    EXPECT(CheckBow(MakeRow("AMA1BOW", 14, 256, 6, 3).data(), "AM") == Verdict::Flags, "a flag value that is neither 1 nor 2");
    Bytes two = am; two[kFlagsAt + 9] = 1;
    EXPECT(CheckBow(two.data(), "AM") == Verdict::Flags, "two flagged frames");
    Bytes tail = am; tail[kFlagsAt + 100] = 2;
    EXPECT(CheckBow(tail.data(), "AM") == Verdict::Flags, "a flag far past the frames");
    EXPECT(CheckBow(MakeRow("AMA1BOW", 14, 256, 1, 2).data(), "AM") == Verdict::Nothing && CheckBow(MakeRow("AMA1BOW", 14, 256, 0, 2).data(), "AM") == Verdict::Nothing, "already at the start");
}

void TestMove() {
    std::printf("the move and the way back\n");
    EXPECT(Target(2, 6) == 2 && Target(1, 6) == 1 && Target(0, 6) == 1 && Target(-5, 6) == 1 && Target(6, 6) == 6 && Target(9, 6) == 6, "the target never delays and is at least 1");
    const Bytes orig = MakeRow("AMA1BOW", 14, 256, 6, 2);
    Bytes row = orig;
    Row before;
    EXPECT(MoveBow(row.data(), "AM", 2, &before) && before.flagIdx == 6, "moved");
    EXPECT(row[kFlagsAt + 2] == 2 && row[kFlagsAt + 6] == 0, "the flag at 2, none at 6");
    int flagged = 0;
    for (size_t i = 0; i < kFlagCount; ++i) flagged += row[kFlagsAt + i] != 0;
    EXPECT(flagged == 1, "one flag only: %d", flagged);
    EXPECT(std::memcmp(row.data(), orig.data(), kFlagsAt) == 0, "the name, frames and speed are as they were");
    Row after;
    EXPECT(CheckBow(row.data(), "AM", &after) == Verdict::Ok && after.flagIdx == 2 && after.frames == 14 && after.speed == 256, "still a valid row, now cut");
    Bytes again = row;
    EXPECT(MoveBow(again.data(), "AM", 1) && again[kFlagsAt + 1] == 2 && again[kFlagsAt + 2] == 0, "a cut row can be cut further, from where it is");
    // refusals leave the row untouched
    Bytes r2 = orig;
    EXPECT(!MoveBow(r2.data(), "AM", 6) && !MoveBow(r2.data(), "AM", 7) && !MoveBow(r2.data(), "AM", 0) && !MoveBow(r2.data(), "AM", -1), "to must be a real earlier frame");
    EXPECT(!MoveBow(r2.data(), "BA", 2), "another class's token");
    EXPECT(r2 == orig, "untouched after refusals");
    Bytes odd = MakeRow("AMA1BOW", 14, 256, 6, 3);
    EXPECT(!MoveBow(odd.data(), "AM", 2) && odd == MakeRow("AMA1BOW", 14, 256, 6, 3), "an odd row is not moved");
    // the way back
    Bytes m = orig;
    MoveBow(m.data(), "AM", 2);
    EXPECT(InPatchedState(m.data(), 6, 2, 2), "in the state Move leaves");
    EXPECT(Back(m.data(), 6, 2, 2) && m == orig, "back exactly");
    EXPECT(!Back(m.data(), 6, 2, 2) && m == orig, "a second Back finds nothing to put back");
    Bytes changed = orig;
    MoveBow(changed.data(), "AM", 2);
    changed[kFlagsAt + 2] = 0;   // somebody cleared it
    const Bytes keep = changed;
    EXPECT(!Back(changed.data(), 6, 2, 2) && changed == keep, "a row that is not as left is left alone");
    Bytes added = orig;
    MoveBow(added.data(), "AM", 2);
    added[kFlagsAt + 4] = 1;     // somebody added another
    const Bytes keep2 = added;
    EXPECT(!Back(added.data(), 6, 2, 2) && added == keep2, "a row with another flag is left alone");
    Bytes back6 = orig;
    EXPECT(!Back(back6.data(), 6, 2, 2) && back6 == orig, "an unmoved row is not touched by Back");
}

void TestWalk() {
    std::printf("the walk over a block\n");
    std::vector<Bytes> rows = {MakeRow("AMA1BOW", 14, 256, 6, 2), MakeRow("BAA1BOW", 15, 256, 7, 2), MakeRow("AMS1BOW", 9, 256, 2, 2), MakeRow("AMA1XBW", 20, 256, 9, 2),
                               MakeRow("AMA2BOW", 14, 256, 6, 2)};
    const Block blk = Build(rows);
    auto read = [&](size_t off, void* dst, size_t n) { return ReadFrom(blk.bytes, off, dst, n); };
    std::vector<size_t> hits;
    size_t seen = 0;
    EXPECT(Walk(read, "AMA1BOW", [&](size_t o) { hits.push_back(o); }, &seen), "walks");
    EXPECT(hits.size() == 1 && hits[0] == blk.offsetOf[0], "the Amazon's bow row at its place (%zu hits)", hits.size());
    EXPECT(seen == 700 + rows.size(), "every row seen: %zu", seen);
    hits.clear();
    Walk(read, "BAA1BOW", [&](size_t o) { hits.push_back(o); });
    EXPECT(hits.size() == 1 && hits[0] == blk.offsetOf[1], "the Barbarian's");
    hits.clear();
    Walk(read, "AMA2BOW", [&](size_t o) { hits.push_back(o); });
    EXPECT(hits.size() == 1 && hits[0] == blk.offsetOf[4], "attack 2");
    hits.clear();
    EXPECT(Walk(read, "ZZA1BOW", [&](size_t o) { hits.push_back(o); }) && hits.empty(), "a name that is not there: walked, nothing found");
    // the whole thing end to end: find, check, move, find again, put back; the rest of the block untouched
    const Bytes original = blk.bytes;
    Bytes live = blk.bytes;
    auto liveRead = [&](size_t off, void* dst, size_t n) { return ReadFrom(live, off, dst, n); };
    size_t at = 0;
    Walk(liveRead, "AMA1BOW", [&](size_t o) { at = o; });
    EXPECT(at == blk.offsetOf[0] && CheckBow(live.data() + at, "AM") == Verdict::Ok, "found and checked");
    EXPECT(MoveBow(live.data() + at, "AM", 2), "moved in the block");
    size_t diffs = 0;
    for (size_t i = 0; i < live.size(); ++i) diffs += live[i] != original[i];
    EXPECT(diffs == 2, "two bytes of the whole block changed: %zu", diffs);
    EXPECT(Back(live.data() + at, 6, 2, 2) && live == original, "the block is what it was");
}

void TestWalkRefusals() {
    std::printf("a block that is not AnimData\n");
    const Block blk = Build({MakeRow("AMA1BOW", 14, 256, 6, 2)});
    auto readOf = [](const Bytes& b) { return [&b](size_t off, void* dst, size_t n) { return ReadFrom(b, off, dst, n); }; };
    {   // a negative count
        Bytes b = blk.bytes;
        const int32_t neg = -5;
        std::memcpy(b.data(), &neg, 4);
        EXPECT(!Walk(readOf(b), "AMA1BOW", [](size_t) {}), "a negative count");
    }
    {   // a count far too big
        Bytes b = blk.bytes;
        const uint32_t big = 100000;
        std::memcpy(b.data(), &big, 4);
        EXPECT(!Walk(readOf(b), "AMA1BOW", [](size_t) {}), "a count of 100000");
    }
    {   // cut short: the reader fails
        Bytes b(blk.bytes.begin(), blk.bytes.begin() + (long)blk.bytes.size() / 2);
        EXPECT(!Walk(readOf(b), "AMA1BOW", [](size_t) {}), "a block that ends in the middle");
    }
    {   // nothing but zeros: 256 empty buckets walk fine (a table with no rows), nothing found
        Bytes b(256 * 4, 0);
        size_t rows = 99;
        bool found = false;
        EXPECT(Walk(readOf(b), "AMA1BOW", [&](size_t) { found = true; }, &rows) && !found && rows == 0, "empty buckets: no rows");
    }
    {   // garbage
        Bytes b(4096, 0xAB);
        EXPECT(!Walk(readOf(b), "AMA1BOW", [](size_t) {}), "garbage");
    }
    EXPECT(ClassToken(0) && !std::strcmp(ClassToken(0), "AM") && !std::strcmp(ClassToken(3), "PA") && !std::strcmp(ClassToken(7), "WK") && !ClassToken(-1) && !ClassToken(8), "class tokens");
}

// The throw's rows: <token>TH<weapon class>, every class of weapon the hero may hold while he throws.
void TestThrow() {
    std::printf("the throw's rows\n");
    Row r;
    const Bytes amHs = MakeRow("AMTH1HS", 16, 256, 9, 2), amHt = MakeRow("AMTH1HT", 16, 256, 9, 2), amHth = MakeRow("AMTHHTH", 16, 256, 9, 2);
    EXPECT(animcut::Check(amHt.data(), "AM", Kind::Throw, &r) == Verdict::Ok && r.frames == 16 && r.speed == 256 && r.flagIdx == 9 && r.flagVal == 2, "the Amazon's throw row: flag at %d", r.flagIdx);
    EXPECT(animcut::Check(amHs.data(), "AM", Kind::Throw) == Verdict::Ok && animcut::Check(amHth.data(), "AM", Kind::Throw) == Verdict::Ok, "... whatever weapon class she holds");
    const char* barb[] = {"BATH1HS", "BATH1HT", "BATH1JS", "BATH1JT", "BATH1SS", "BATH1ST", "BATHHTH"};
    for (const char* n : barb) EXPECT(animcut::Check(MakeRow(n, 16, 256, 8, 2).data(), "BA", Kind::Throw) == Verdict::Ok, "%s", n);
    EXPECT(animcut::Check(MakeRow("PATHHTH", 12, 256, 6, 2).data(), "PA", Kind::Throw) == Verdict::Ok, "the Paladin's bare-hand throw (12 frames at 6)");
    // each kind only its own rows
    EXPECT(animcut::Check(amHt.data(), "AM", Kind::Bow) == Verdict::Name, "a throw row is not a bow row");
    EXPECT(animcut::Check(MakeRow("AMA1BOW", 14, 256, 6, 2).data(), "AM", Kind::Throw) == Verdict::Name, "a bow row is not a throw row");
    EXPECT(animcut::Check(MakeRow("AMA11HS", 16, 256, 10, 1).data(), "AM", Kind::Throw) == Verdict::Name, "a melee attack is not a throw row");
    EXPECT(animcut::Check(MakeRow("AMA1HTH", 13, 256, 8, 1).data(), "AM", Kind::Throw) == Verdict::Name && animcut::Check(MakeRow("AMSCHTH", 20, 256, 10, 2).data(), "AM", Kind::Throw) == Verdict::Name, "nor a bare-hand attack, nor a cast");
    EXPECT(animcut::Check(MakeRow("AMGH1HS", 5, 256, 0, 1).data(), "AM", Kind::Throw) == Verdict::Name, "nor a hit recovery");
    EXPECT(animcut::Check(MakeRow("SOTH1HS", 20, 256, 10, 2).data(), "AM", Kind::Throw) == Verdict::Name, "another class's throw");
    EXPECT(animcut::Check(MakeRow("SKTH1HS", 16, 256, 9, 2).data(), "AM", Kind::Throw) == Verdict::Name, "a monster's");
    Bytes odd = amHt; odd[4] = '-';
    EXPECT(animcut::Check(odd.data(), "AM", Kind::Throw) == Verdict::Name, "a weapon class of garbage");
    Bytes noNul = amHt; noNul[7] = 'X';
    EXPECT(animcut::Check(noNul.data(), "AM", Kind::Throw) == Verdict::Name, "a name that does not end");
    // the shape rules are the same
    EXPECT(animcut::Check(MakeRow("AMTH1HT", 16, 256, -1, 0).data(), "AM", Kind::Throw) == Verdict::Flags && animcut::Check(MakeRow("AMTH1HT", 16, 256, 9, 3).data(), "AM", Kind::Throw) == Verdict::Flags, "no flag / a flag value of 3");
    EXPECT(animcut::Check(MakeRow("AMTH1HT", 16, 256, 1, 2).data(), "AM", Kind::Throw) == Verdict::Nothing, "already at the start");
    // the move and the way back, exactly
    Bytes row = amHt;
    EXPECT(animcut::Move(row.data(), "AM", Kind::Throw, 1) && row[kFlagsAt + 1] == 2 && row[kFlagsAt + 9] == 0, "moved to frame 1");
    EXPECT(!animcut::Move(row.data(), "AM", Kind::Throw, 0), "frame 0 is not a target: it is never read at the start");
    EXPECT(Target(0, 9) == 1 && Target(1, 9) == 1 && Target(12, 9) == 9, "the target is at least 1 and never later");
    EXPECT(Back(row.data(), 9, 1, 2) && row == amHt, "back exactly");
    // the cut in milliseconds
    EXPECT(CutMs(9, 1, 256) == 320 && CutMs(6, 2, 256) == 160 && CutMs(9, 1, 512) == 160 && CutMs(1, 9, 256) == 0 && CutMs(5, 5, 256) == 0 && CutMs(9, 1, 0) == 0, "CutMs");

    // in a block: only the class's throw rows move; the bow's, the melee's, the other classes' do not, and everything comes back
    std::vector<Bytes> rows = {amHs, amHt, amHth, MakeRow("AMA1BOW", 14, 256, 6, 2), MakeRow("AMA11HS", 16, 256, 10, 1), MakeRow("BATH1HS", 16, 256, 8, 2),
                               MakeRow("AMTH1HS", 16, 256, 9, 2)};   // (a second AMTH1HS: another copy of the row, as the game has two tables)
    const Block blk = Build(rows);
    Bytes live = blk.bytes;
    auto liveRead = [&](size_t off, void* dst, size_t n) { return ReadFrom(live, off, dst, n); };
    std::vector<size_t> hits;
    size_t seen = 0;
    EXPECT(WalkRows(liveRead, [&](const uint8_t* head) { return IsRowName(head, "AM", Kind::Throw); }, [&](size_t o) { hits.push_back(o); }, &seen), "walks by the class's throw name");
    EXPECT(hits.size() == 4, "the class's four throw rows, not the bow's, the melee's or the Barbarian's: %zu", hits.size());
    for (size_t o : hits) EXPECT(animcut::Move(live.data() + o, "AM", Kind::Throw, 1), "moved at %zu", o);
    size_t diffs = 0;
    for (size_t i = 0; i < live.size(); ++i) diffs += live[i] != blk.bytes[i];
    EXPECT(diffs == 8, "two bytes for each of the four rows changed, nothing else: %zu", diffs);
    // the bow's cut in the same block is another matter: it moves its own row and its restore leaves the throws moved
    size_t bowAt = 0;
    Walk(liveRead, "AMA1BOW", [&](size_t o) { bowAt = o; });
    EXPECT(animcut::Move(live.data() + bowAt, "AM", Kind::Bow, 2), "the bow's row moves on its own");
    EXPECT(Back(live.data() + bowAt, 6, 2, 2), "... and goes back on its own");
    for (size_t o : hits) EXPECT(live[o + kFlagsAt + 1] == 2 && live[o + kFlagsAt + 9] == 0, "the throw rows are still cut");
    for (size_t o : hits) EXPECT(Back(live.data() + o, 9, 1, 2), "back at %zu", o);
    EXPECT(live == blk.bytes, "the block is what it was");
}

}  // namespace

// [hands] quick_strike: the melee attack rows. The names and numbers are the game's own AnimData.D2 rows (read 2026-10-15): the Paladin's PAA11HS 15 frames flag 7, PAA11HT 17 / 8,
// PAA1HTH 14 / 7, PAS11HS (Smite) 12 / 7; the Barbarian's BAA11JS 16 / 7; the Assassin's AIA1HT1 11 / 6.
void TestMelee() {
    std::printf("the melee attack rows\n");
    Row r;
    const auto name = [](const char* n) { Bytes b(8, 0); std::memcpy(b.data(), n, std::strlen(n) < 7 ? std::strlen(n) : 7); return b; };
    for (const char* n : {"PAA11HS", "PAA11HT", "PAA12HS", "PAA12HT", "PAA1HTH", "PAA1STF", "PAA21HS", "PAA2HTH", "PAA2STF"})
        EXPECT(animcut::IsRowName(name(n).data(), "PA", Kind::Melee), "%s is a Paladin melee row", n);
    for (const char* n : {"BAA11JS", "BAA21ST", "BAA1HTH"})
        EXPECT(animcut::IsRowName(name(n).data(), "BA", Kind::Melee), "%s is a Barbarian melee row", n);
    for (const char* n : {"AIA1HT1", "AIA2HT2", "AIA1HTH"})
        EXPECT(animcut::IsRowName(name(n).data(), "AI", Kind::Melee), "%s is an Assassin melee row (the claws' classes)", n);
    EXPECT(animcut::IsRowName(name("PAS11HS").data(), "PA", Kind::Melee) && animcut::IsRowName(name("PAS1HTH").data(), "PA", Kind::Melee), "the Paladin's Smite rows (S1)");
    // never the bow's or the crossbow's
    EXPECT(!animcut::IsRowName(name("PAA1BOW").data(), "PA", Kind::Melee) && !animcut::IsRowName(name("PAA2BOW").data(), "PA", Kind::Melee), "not the bow's attack");
    EXPECT(!animcut::IsRowName(name("PAA1XBW").data(), "PA", Kind::Melee) && !animcut::IsRowName(name("AMA2XBW").data(), "AM", Kind::Melee), "not the crossbow's attack");
    EXPECT(!animcut::IsRowName(name("PAS1BOW").data(), "PA", Kind::Melee), "not a ranged S1");
    // never the throw's, the cast's, the kick's, the other modes
    for (const char* n : {"PATH1HS", "PATHHTH", "PASC1HS", "PAKK1HS", "PADD1HS", "PAGH1HS", "PANU1HS", "PAWLHTH", "PARNHTH", "PADTHTH", "PABLHTH", "PATNHTH", "PATWHTH"})
        EXPECT(!animcut::IsRowName(name(n).data(), "PA", Kind::Melee), "%s is not a melee attack row", n);
    // another class's S1 is not melee (the Sorceress's and the Necromancer's special 1 are spells)
    EXPECT(!animcut::IsRowName(name("SOS11HS").data(), "SO", Kind::Melee) && !animcut::IsRowName(name("NES1HTH").data(), "NE", Kind::Melee) && !animcut::IsRowName(name("AIS11HS").data(), "AI", Kind::Melee),
           "only the Paladin's S1 (Smite)");
    // the token and the NUL
    EXPECT(!animcut::IsRowName(name("BAA11HS").data(), "PA", Kind::Melee), "another class's row is not this class's");
    Bytes longName(8, 0);
    std::memcpy(longName.data(), "PAA11HSX", 8);
    EXPECT(!animcut::IsRowName(longName.data(), "PA", Kind::Melee), "no NUL after the name: not a row");
    // each kind only its own rows
    EXPECT(!animcut::IsRowName(name("PAA11HS").data(), "PA", Kind::Bow) && !animcut::IsRowName(name("PAA11HS").data(), "PA", Kind::Throw), "a melee row is neither the bow's nor the throw's");
    EXPECT(!animcut::IsRowName(name("PAA1BOW").data(), "PA", Kind::Melee) && animcut::IsRowName(name("PAA1BOW").data(), "PA", Kind::Bow), "the bow's row is the bow's alone");
    EXPECT(!animcut::IsRowName(name("PATH1HS").data(), "PA", Kind::Melee) && animcut::IsRowName(name("PATH1HS").data(), "PA", Kind::Throw), "the throw's row is the throw's alone");
    // the shape and the cut
    Bytes pa = MakeRow("PAA11HS", 15, 256, 7, 1);
    EXPECT(animcut::Check(pa.data(), "PA", Kind::Melee, &r) == Verdict::Ok && r.frames == 15 && r.flagIdx == 7 && r.flagVal == 1, "the Paladin's one-hand swing: frame %d", r.flagIdx);
    EXPECT(animcut::Move(pa.data(), "PA", Kind::Melee, 1) && pa[kFlagsAt + 1] == 1 && pa[kFlagsAt + 7] == 0, "the flag goes to frame 1");
    EXPECT(animcut::CutMs(7, 1, 256) == 240 && animcut::CutMs(8, 1, 256) == 280 && animcut::CutMs(6, 1, 256) == 200, "0.24 s (frame 7), 0.28 s (frame 8) sooner");
    EXPECT(animcut::Back(pa.data(), 7, 1, 1) && pa[kFlagsAt + 7] == 1 && pa[kFlagsAt + 1] == 0, "and back exactly");
    EXPECT(animcut::Check(MakeRow("PAS11HS", 12, 256, 7, 1).data(), "PA", Kind::Melee) == Verdict::Ok, "Smite's row checks out");
    EXPECT(animcut::Check(MakeRow("AIA1HT1", 11, 256, 6, 1).data(), "AI", Kind::Melee) == Verdict::Ok, "the Assassin's claw row (the earliest, frame 6)");
    // a row that is not what was read is refused
    EXPECT(animcut::Check(MakeRow("PAA11HS", 15, 256, -1, 0).data(), "PA", Kind::Melee) == Verdict::Flags, "no flag at all");
    Bytes two = MakeRow("PAA11HS", 15, 256, 7, 1);
    two[kFlagsAt + 9] = 1;
    EXPECT(animcut::Check(two.data(), "PA", Kind::Melee) == Verdict::Flags, "two flags");
    EXPECT(animcut::Check(MakeRow("PAA11HS", 15, 256, 1, 1).data(), "PA", Kind::Melee) == Verdict::Nothing, "already at frame 1: nothing to cut");
    // the walk finds the whole set in a block, and only it
    std::vector<Bytes> rows;
    for (const char* n : {"PAA11HS", "PAA11HT", "PAA1HTH", "PAA21HS", "PAS11HS"}) rows.push_back(MakeRow(n, 15, 256, 7, 1));
    for (const char* n : {"PAA1BOW", "PAA1XBW", "PATH1HS", "PASC1HS", "PAKK1HS", "BAA11HS", "SOS11HS"}) rows.push_back(MakeRow(n, 15, 256, 7, 1));
    const Block blk = Build(rows);
    std::vector<size_t> seen;
    const auto read = [&](size_t off, void* dst, size_t n) { if (off + n > blk.bytes.size()) return false; std::memcpy(dst, blk.bytes.data() + off, n); return true; };
    EXPECT(animcut::WalkRows(read, [&](const uint8_t* head) { return animcut::IsRowName(head, "PA", Kind::Melee); }, [&](size_t off) { seen.push_back(off); }), "the walk reads the block");
    EXPECT(seen.size() == 5, "five Paladin melee rows found, nothing else (%zu)", seen.size());
    for (size_t i = 0; i < 5; ++i) {
        bool at = false;
        for (const size_t s : seen) at = at || s == blk.offsetOf[i];
        EXPECT(at, "row %zu (%u) is among them", i, (unsigned)blk.offsetOf[i]);
    }
}

void TestThrowCutWanted() {
    std::printf("quick_throw and a potion in the hand\n");
    EXPECT(animcut::ThrowCutWanted(true, false, false), "a javelin in F4: cut");
    EXPECT(!animcut::ThrowCutWanted(true, true, false), "a throwing potion in F4: its own timing - the rows are put back");
    EXPECT(animcut::ThrowCutWanted(true, true, true), "a potion with quick_throw_potions: cut as the javelin's");
    EXPECT(!animcut::ThrowCutWanted(false, false, false) && !animcut::ThrowCutWanted(false, true, true) && !animcut::ThrowCutWanted(false, false, true), "outside F4: never");
}

int main() {
    TestThrowCutWanted();
    TestMelee();
    TestThrow();
    TestCheck();
    TestMove();
    TestWalk();
    TestWalkRefusals();
    if (g_fails) {
        std::printf("%d FAILED\n", g_fails);
        return 1;
    }
    std::printf("all passed\n");
    return 0;
}
