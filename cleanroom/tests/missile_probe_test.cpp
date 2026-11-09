// missile_probe_test - [debug] missile_probe, the pure part (vr/missile_probe.h): the params block decoded where the create reads it, the flags by name, the
// target cell the create takes, the heading, and the creates grouped into shots (a Multiple Shot's fan against its mean). No game: blocks built by hand.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "../../vr/missile_probe.h"
#include "../../vr/throw_born.h"

namespace {

int g_fails = 0, g_checks = 0;
#define EXPECT(c, ...)                                           \
    do {                                                         \
        ++g_checks;                                              \
        if (!(c)) {                                              \
            ++g_fails;                                           \
            std::printf("  FAIL line %d: %s - ", __LINE__, #c);  \
            std::printf(__VA_ARGS__);                            \
            std::printf("\n");                                   \
        }                                                        \
    } while (0)

using namespace missileprobe;

template <typename T> void Put(uint8_t* b, size_t off, T v) { std::memcpy(b + off, &v, sizeof v); }

void TestDecode() {
    std::printf("the block is decoded where the create reads it\n");
    uint8_t b[kSize] = {};
    Put<uint32_t>(b, kOffFlags, kFlagStart | kFlagTargetAbs | kFlagRange);
    Put<uint64_t>(b, kOffOwner, 0x1111);
    Put<uint64_t>(b, kOffSource, 0x2222);
    Put<uint64_t>(b, kOffTargetUnit, 0x3333);
    Put<int32_t>(b, kOffClass, 213);
    Put<int32_t>(b, kOffStartX, 5600);
    Put<int32_t>(b, kOffStartY, 4700);
    Put<int32_t>(b, kOffTargetX, 5640);
    Put<int32_t>(b, kOffTargetY, 4710);
    Put<int32_t>(b, kOffVelocity, 24);
    Put<int32_t>(b, kOffLevel, 7);
    Put<int32_t>(b, kOffRange, 50);
    Put<int32_t>(b, kOff70, 9);
    const Params p = Decode(b);
    EXPECT(p.flags == (kFlagStart | kFlagTargetAbs | kFlagRange) && p.owner == 0x1111 && p.source == 0x2222 && p.targetUnit == 0x3333, "flags and units");
    EXPECT(p.cls == 213 && p.sx == 5600 && p.sy == 4700 && p.tx == 5640 && p.ty == 4710, "class and cells");
    EXPECT(p.velocity == 24 && p.level == 7 && p.range == 50 && p.f70 == 9 && p.f3c == 0, "the numbers");
}
// the offsets the potion's rewrite uses are the same block's; every field inside it
static_assert(kOffFlags == throwborn::kOffFlags && kOffOwner == throwborn::kOffOwner && kOffSource == throwborn::kOffSource && kOffClass == throwborn::kOffClass &&
                  kOffStartX == throwborn::kOffStartX && kOffTargetY == throwborn::kOffTargetY && kSize == throwborn::kParamsSize,
              "one layout with throw_born.h");
static_assert(kFlagStart == throwborn::kFlagStart && kFlagTargetAbs == throwborn::kFlagTargetAbs && kFlagLifeByDistance == throwborn::kFlagLifeByDistance, "the same flags");
static_assert(kOff70 + 4 <= kSize, "every field inside the block");

void TestFlags() {
    std::printf("the flags by name\n");
    char s[200];
    FlagNames(kFlagTargetAbs | kFlagLifeByDistance, s, sizeof s);
    EXPECT(std::string(s) == "target-abs life-by-distance", "the potion's: %s", s);
    FlagNames(0, s, sizeof s);
    EXPECT(std::string(s) == "none", "none: %s", s);
    FlagNames(kFlagStart | 0x10000u | 0x40u, s, sizeof s);
    EXPECT(std::string(s) == "start-cell +0x40 +0x10000", "unknown bits as hex: %s", s);
    char tiny[8];
    FlagNames(kFlagStart | kFlagTargetAbs | kFlagRange, tiny, sizeof tiny);
    EXPECT(std::strlen(tiny) < sizeof tiny, "a short buffer is not overrun: %s", tiny);
}

void TestTarget() {
    std::printf("the target cell the create takes\n");
    Params p;
    int tx = 0, ty = 0;
    p.flags = kFlagTargetAbs; p.tx = 10; p.ty = 20;
    TargetCell(p, 100, 200, &tx, &ty);
    EXPECT(tx == 10 && ty == 20, "absolute: %d %d", tx, ty);
    p.flags = kFlagTargetRel; p.tx = 3; p.ty = -4;
    TargetCell(p, 100, 200, &tx, &ty);
    EXPECT(tx == 103 && ty == 196, "relative to the start: %d %d", tx, ty);
    p.flags = kFlagTargetRel | kFlagTargetAbs;
    TargetCell(p, 100, 200, &tx, &ty);
    EXPECT(tx == 103 && ty == 196, "0x2 is tested first, as the create does: %d %d", tx, ty);
    p.flags = 0;
    TargetCell(p, 100, 200, &tx, &ty);
    EXPECT(tx == 100 && ty == 200, "neither: the start: %d %d", tx, ty);
}

void TestHeading() {
    std::printf("the heading\n");
    EXPECT(std::fabs(HeadingDeg(0, 0, 10, 0)) < 1e-4f, "along x: 0");
    EXPECT(std::fabs(HeadingDeg(0, 0, 0, 10) - 90.0f) < 1e-3f, "along y: 90");
    EXPECT(std::isnan(HeadingDeg(5, 5, 5, 5)), "one cell: none");
    EXPECT(std::fabs(WrapDeg(190.0f) + 170.0f) < 1e-3f && std::fabs(WrapDeg(-190.0f) - 170.0f) < 1e-3f && std::fabs(WrapDeg(180.0f) + 180.0f) < 1e-3f, "wrapped into [-180, 180)");
    EXPECT(ClassName(213) && std::string(ClassName(213)) == "Multiple Shot arrow" && ClassName(46) && !ClassName(1), "the names known here, none guessed");
}

void TestGroup() {
    std::printf("the creates grouped into shots\n");
    Grouper g;
    Shot done;
    // a Multiple Shot: five arrows in one frame, a fan round 170 degrees (across the wrap)
    const float fan[5] = {150.0f, 160.0f, 170.0f, 180.0f, -170.0f};
    for (int i = 0; i < 5; ++i) EXPECT(!g.Add(Create{1.0 + i * 0.001, 7, 0x45A007, 213, fan[i], i == 2}, &done), "one shot while they come together");
    EXPECT(!g.Flush(1.01, &done) && g.Open(), "not finished within the gap");
    EXPECT(g.Flush(1.2, &done) && !g.Open(), "finished after it");
    EXPECT(done.count == 5 && done.kept == 5 && done.withUnit == 1 && done.ownerId == 7 && done.caller == 0x45A007 && !done.mixed, "five, one with a unit");
    float mean = 0.0f, rel[kMaxFan];
    size_t n = 0;
    EXPECT(Fan(done, &mean, rel, kMaxFan, &n) && n == 5, "a fan of five");
    EXPECT(std::fabs(mean - 170.0f) < 0.01f, "the mean across the wrap: %.3f", mean);
    EXPECT(std::fabs(rel[0] + 20.0f) < 0.01f && std::fabs(rel[4] - 20.0f) < 0.01f && std::fabs(rel[2]) < 0.01f, "each against it: %.2f .. %.2f", rel[0], rel[4]);
    char line[600];
    ShotLine(done, line, sizeof line);
    EXPECT(std::string(line).find("5 creates") != std::string::npos && std::string(line).find("Multiple Shot arrow") != std::string::npos &&
               std::string(line).find("-20.0 -10.0 +0.0 +10.0 +20.0") != std::string::npos && std::string(line).find("in 1 of 5") != std::string::npos,
           "the line: %s", line);

    // another owner, or another call site, or a gap: a new shot (the old one handed back)
    Grouper h;
    EXPECT(!h.Add(Create{2.0, 7, 0x100, 0, 10.0f, false}, &done), "the first");
    EXPECT(h.Add(Create{2.001, 8, 0x100, 0, 10.0f, false}, &done) && done.ownerId == 7 && done.count == 1, "another owner: the first one finished");
    EXPECT(h.Add(Create{2.002, 8, 0x200, 0, 10.0f, false}, &done) && done.ownerId == 8 && done.caller == 0x100, "another call site");
    EXPECT(h.Add(Create{2.2, 8, 0x200, 0, 10.0f, false}, &done) && done.caller == 0x200 && done.count == 1, "a gap: a Strafe's next arrow is its own");
    EXPECT(!h.Add(Create{2.21, 8, 0x200, 682, NAN, false}, &done), "mixed classes stay one shot");
    EXPECT(h.Flush(3.0, &done) && done.count == 2 && done.mixed && done.noHeading == 1, "mixed, one without a heading");
    ShotLine(done, line, sizeof line);
    EXPECT(std::string(line).find("classes mixed") != std::string::npos && std::string(line).find("some without a heading") != std::string::npos, "the line: %s", line);
    EXPECT(!h.Flush(4.0, &done), "nothing open: nothing to finish");

    // more creates than the fan keeps: counted, the first ones kept
    Grouper k;
    for (int i = 0; i < 40; ++i) k.Add(Create{5.0, 1, 1, 213, (float)i, false}, &done);
    EXPECT(k.Flush(6.0, &done) && done.count == 40 && done.kept == kMaxFan, "40 counted, %u kept", done.kept);
    ShotLine(done, line, sizeof line);
    EXPECT(std::string(line).find("(the first ones)") != std::string::npos, "said: %s", line);
    Shot none;
    EXPECT(!Fan(none, &mean, rel, kMaxFan, &n) && n == 0, "an empty shot has no fan");
}

void TestHex() {
    std::printf("the block as hex\n");
    uint8_t b[kSize] = {};
    b[0] = 0x21; b[1] = 0x04; b[0x97] = 0xAB;
    char s[200];
    HexLine(b, 0, 4, s, sizeof s);
    EXPECT(std::string(s) == "+0x00: 21 04 00 00", "%s", s);
    HexLine(b, 0x90, 16, s, sizeof s);
    EXPECT(std::string(s) == "+0x90: 00 00 00 00 00 00 00 AB", "stops at the block's end: %s", s);
}

}  // namespace

int main() {
    TestDecode();
    TestFlags();
    TestTarget();
    TestHeading();
    TestGroup();
    TestHex();
    std::printf("\nmissile_probe_test: %d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
