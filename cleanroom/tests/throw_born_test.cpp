// throw_born_test - [hands] throw_direct, the pure part (vr/throw_born.h): the params block, the decision, the rewrite, the armed window and the press time.
// No game: the block is built by hand the way the potion shooter fills it.
#include <cmath>
#include <cstdio>
#include <cstring>

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

using namespace throwborn;

// What the potion shooter hands the create: start = the hero (no start flag), target absolute, life by distance.
Block PotionBlock(int cls, int tx, int ty, uint64_t owner = 0x1234560) {
    Block b;
    b.fill(0);
    Put<uint32_t>(b.data(), kOffFlags, kFlagTargetAbs | kFlagLifeByDistance);
    Put<uint64_t>(b.data(), kOffOwner, owner);
    Put<uint64_t>(b.data(), kOffSource, owner);
    Put<int32_t>(b.data(), kOffClass, cls);
    Put<int32_t>(b.data(), kOffTargetX, tx);
    Put<int32_t>(b.data(), kOffTargetY, ty);
    return b;
}

void TestRead() {
    std::printf("the block is read where the create reads it\n");
    Block b = PotionBlock(46, 5123, 4987);
    const Seen s = Read(b.data());
    EXPECT(s.flags == (kFlagTargetAbs | kFlagLifeByDistance), "flags %x", s.flags);
    EXPECT(s.missileClass == 46 && s.tx == 5123 && s.ty == 4987 && s.owner == 0x1234560, "class %d cell %d %d owner %llx", s.missileClass, s.tx, s.ty, (unsigned long long)s.owner);
    EXPECT(kParamsSize == 0x98 && kOffTargetY + 4 <= kParamsSize, "the offsets lie inside the block");
}

void TestShape() {
    std::printf("which block is a potion's\n");
    EXPECT(IsPotionFlags(kFlagTargetAbs | kFlagLifeByDistance), "the shooter's flags");
    EXPECT(!IsPotionFlags(kFlagTargetAbs | kFlagLifeByDistance | kFlagStart), "a block with its own start is some other skill's");
    EXPECT(!IsPotionFlags(kFlagTargetAbs), "no life by distance: not a potion");
    EXPECT(!IsPotionFlags(kFlagLifeByDistance), "no absolute target: not a potion");
    EXPECT(!IsPotionFlags(0), "empty flags");
    for (int c = 44; c <= 49; ++c) EXPECT(IsPotionClass(c), "class %d is a throwing potion", c);
    EXPECT(!IsPotionClass(43) && !IsPotionClass(50) && !IsPotionClass(0) && !IsPotionClass(-1), "and those around it are not");
    EXPECT(IsCell(1) && IsCell(5000) && IsCell(kCellMax), "cells");
    EXPECT(!IsCell(0) && !IsCell(-3) && !IsCell(kCellMax + 1), "not cells");
}

void TestDecide() {
    std::printf("the decision\n");
    const Seen s = Read(PotionBlock(45, 5000, 5000).data());
    EXPECT(Decide(s, true, true, true) == Verdict::Relocate, "armed, hero, room: relocated");
    EXPECT(Decide(s, false, true, true) == Verdict::NotArmed, "not armed: left alone");
    EXPECT(Decide(s, true, false, true) == Verdict::NotHero, "the owner is a monster (or anything else): left alone");
    EXPECT(Decide(s, true, true, false) == Verdict::NoRoom, "no room at the cast point: the game's own flight");
    Seen other = s;
    other.missileClass = 12;
    EXPECT(Decide(other, true, true, true) == Verdict::NotPotion, "a fire bolt's block");
    other = s;
    other.flags |= kFlagStart;
    EXPECT(Decide(other, true, true, true) == Verdict::NotPotion, "a block with its own start");
    other = s;
    other.tx = 0;
    EXPECT(Decide(other, true, true, true) == Verdict::BadCell, "a cell of 0");
    other = s;
    other.ty = -4;
    EXPECT(Decide(other, true, true, true) == Verdict::BadCell, "a negative cell");
    EXPECT(Decide(s, false, false, false) == Verdict::NotArmed, "not armed wins over everything");
    for (Verdict v : {Verdict::Relocate, Verdict::NotArmed, Verdict::NotPotion, Verdict::NotHero, Verdict::BadCell, Verdict::NoRoom})
        EXPECT(std::strlen(Why(v)) > 0, "every verdict has its words");
}

void TestRewrite() {
    std::printf("the rewrite moves the start and the target, nothing else\n");
    Block b = PotionBlock(47, 5100, 4900);
    for (size_t i = 0x34; i < kParamsSize; ++i) b[i] = (uint8_t)(i * 7 + 1);   // the rest of the block: arbitrary, must come out the same
    const Block before = b;
    const Start plan = PlanStart(5000, 4900, 5100, 4900, 0.20);   // 100 cells, 5 frames left
    EXPECT(plan.kind == Start::Kind::Shifted, "a shifted start (%d)", (int)plan.kind);
    Rewrite(b.data(), plan);
    const Seen s = Read(b.data());
    EXPECT((s.flags & kFlagStart) != 0 && (s.flags & kFlagTargetAbs) != 0 && (s.flags & kFlagLifeByDistance) != 0, "the start flag is set, the others stay: %x", s.flags);
    EXPECT(Get<int32_t>(b.data(), kOffStartX) == plan.sx && Get<int32_t>(b.data(), kOffStartY) == plan.sy, "born at the planned start: %d %d", plan.sx, plan.sy);
    EXPECT(s.tx == 5100 && s.ty == 4900, "and still aimed at the cast point: %d %d", s.tx, s.ty);
    EXPECT(s.missileClass == 47 && s.owner == 0x1234560, "the class and the owner stay");
    bool same = true;
    for (size_t i = 0x34; i < kParamsSize; ++i) same = same && b[i] == before[i];
    EXPECT(same, "the rest of the block is untouched");
    EXPECT(IsPotionFlags(Read(before.data()).flags) && !IsPotionFlags(s.flags), "a rewritten block is no longer the shooter's: it cannot be taken twice");
    // born at the cast point: the target is one cell beyond
    Block c = PotionBlock(47, 5100, 4900);
    Rewrite(c.data(), PlanStart(5000, 4900, 5100, 4900, 0.0));
    const Seen sc = Read(c.data());
    EXPECT(Get<int32_t>(c.data(), kOffStartX) == 5100 && Get<int32_t>(c.data(), kOffStartY) == 4900 && sc.tx == 5101 && sc.ty == 4900, "at the cast point: start %d, target %d", Get<int32_t>(c.data(), kOffStartX), sc.tx);
}

void TestPlanStart() {
    std::printf("where the potion is born so that it explodes as the model lands\n");
    // 20 cells straight east, 0.45 s of flight left = 11 frames: born 11 cells before the cast point
    Start p = PlanStart(5000, 5000, 5020, 5000, 0.45);
    EXPECT(p.kind == Start::Kind::Shifted && p.sx == 5009 && p.sy == 5000 && p.frames == 11 && p.tx == 5020 && p.ty == 5000, "20 cells east, 0.45 s: kind %d start %d %d, %d frames", (int)p.kind, p.sx, p.sy, p.frames);
    // the model flies as long as the game's potion would (20 frames = 0.8 s) or longer: the game's flight is left alone
    p = PlanStart(5000, 5000, 5020, 5000, 0.80);
    EXPECT(p.kind == Start::Kind::Natural && p.frames == 20, "0.8 s: the game's own flight (%d)", (int)p.kind);
    p = PlanStart(5000, 5000, 5020, 5000, 3.0);
    EXPECT(p.kind == Start::Kind::Natural, "3 s: the game's own flight, nothing can make it later");
    // no time left: born at the cast point, a one-frame life
    p = PlanStart(5000, 5000, 5020, 5000, 0.0);
    EXPECT(p.kind == Start::Kind::AtCast && p.sx == 5020 && p.sy == 5000 && p.tx == 5021 && p.ty == 5000 && p.frames == 1, "0 s: at the cast point");
    p = PlanStart(5000, 5000, 5020, 5000, 0.04);
    EXPECT(p.kind == Start::Kind::AtCast, "one frame left: at the cast point");
    p = PlanStart(5000, 5000, 5020, 5000, -0.5);
    EXPECT(p.kind == Start::Kind::AtCast, "the model has landed already: at the cast point");
    p = PlanStart(5000, 5000, 5020, 5000, std::nan(""));
    EXPECT(p.kind == Start::Kind::AtCast, "no number: at the cast point");
    // the hero on the cast point: no direction
    p = PlanStart(5000, 5000, 5000, 5000, 0.5);
    EXPECT(p.kind == Start::Kind::NoDirection, "the hero stands on the cast point");
    // diagonally: 20 cells each way = 30 frames; 0.6 s = 15 frames left
    p = PlanStart(5000, 5000, 5020, 5020, 0.60);
    EXPECT(p.kind == Start::Kind::Shifted && p.sx == 5010 && p.sy == 5010 && p.frames == 15, "diagonal: start %d %d, %d frames", p.sx, p.sy, p.frames);
    // and toward the north-west
    p = PlanStart(5020, 5020, 5000, 5000, 0.60);
    EXPECT(p.kind == Start::Kind::Shifted && p.sx == 5010 && p.sy == 5010 && p.frames == 15, "north-west: start %d %d, %d frames", p.sx, p.sy, p.frames);
    // the start is always on the line from the hero to the cast point, between them, and its life is the wanted number of frames to within one
    for (int dist = 4; dist <= 48; dist += 4) {
        for (int ang = 0; ang < 360; ang += 15) {
            const int hx = 5000, hy = 5000;
            const int tx = hx + (int)std::lround(dist * std::cos(ang * 3.14159265 / 180.0)), ty = hy + (int)std::lround(dist * std::sin(ang * 3.14159265 / 180.0));
            const int full = OctFrames(tx - hx, ty - hy);
            for (double rem = 0.1; rem < 2.0; rem += 0.17) {
                const int want = (int)std::llround(rem * kGameTickHz);
                const Start q = PlanStart(hx, hy, tx, ty, rem);
                if (q.kind == Start::Kind::Shifted) {
                    const double a = (double)(q.sx - hx) * (ty - hy) - (double)(q.sy - hy) * (tx - hx);   // cross product: off the line by at most a cell
                    const double len = std::sqrt((double)(tx - hx) * (tx - hx) + (double)(ty - hy) * (ty - hy));
                    EXPECT(std::fabs(a) / len <= 1.0, "dist %d ang %d rem %.2f: the start is %.2f cells off the line", dist, ang, rem, std::fabs(a) / len);
                    EXPECT(q.frames >= want - 1 && q.frames <= want + 1 && q.frames < full, "dist %d ang %d rem %.2f: %d frames for %d wanted (full %d)", dist, ang, rem, q.frames, want, full);
                } else if (q.kind == Start::Kind::Natural) {
                    EXPECT(want >= full || q.frames >= full, "dist %d ang %d rem %.2f: natural although %d < %d", dist, ang, rem, want, full);
                }
            }
        }
    }
}

// [hands] quick_throw (vr/anim_cut.h): the game's potion is created ~0.32 s earlier in the throw animation, so the model has that much MORE flight left at the create. PlanStart
// takes the remaining flight as measured at the create, so an earlier create only means more remaining flight: nothing in it assumed the 0.36 s wind-up.
void TestQuickThrow() {
    std::printf("an earlier create (quick_throw): more flight left at the create\n");
    const double kOldCreate = 0.36, kNewCreate = 0.04;   // an Amazon's action frame 9 and 1 (frames of 40 ms)
    for (int dist = 4; dist <= 48; dist += 2) {
        const int full = OctFrames(dist, 0);
        for (double flight = 0.3; flight <= 2.5; flight += 0.1) {   // the model's flight from the release to its landing
            const Start before = PlanStart(5000, 5000, 5000 + dist, 5000, flight - kOldCreate);
            const Start after = PlanStart(5000, 5000, 5000 + dist, 5000, flight - kNewCreate);
            EXPECT(after.frames >= before.frames || before.kind == Start::Kind::Natural, "dist %d flight %.1f: the life shrank (%d -> %d)", dist, flight, before.frames, after.frames);
            if (after.kind == Start::Kind::Shifted) {
                EXPECT(after.sx > 5000 && after.sx <= 5000 + dist && after.frames < full && after.frames >= 2, "dist %d flight %.1f: start %d, %d frames of %d", dist, flight, after.sx, after.frames, full);
                // the explosion comes at the create + the life, which is the landing (to within a frame)
                EXPECT(std::fabs(kNewCreate + after.frames / kGameTickHz - flight) <= 1.5 / kGameTickHz, "dist %d flight %.1f: explodes at %.2f s", dist, flight, kNewCreate + after.frames / kGameTickHz);
            }
        }
    }
    // the one case it cannot keep: a short throw whose model flies longer than the game's missile does from the earlier create. The game's own flight is left alone and
    // its explosion comes BEFORE the model lands (never after): 10 cells = 10 frames = 0.4 s, the model lands at 0.6 s
    const Start oldPlan = PlanStart(5000, 5000, 5010, 5000, 0.60 - kOldCreate);
    const Start newPlan = PlanStart(5000, 5000, 5010, 5000, 0.60 - kNewCreate);
    EXPECT(oldPlan.kind == Start::Kind::Shifted, "created at 0.36 s the 0.6 s flight is matched (kind %d, %d frames)", (int)oldPlan.kind, oldPlan.frames);
    EXPECT(newPlan.kind == Start::Kind::Natural && newPlan.frames == 10, "created at 0.04 s it is left to the game's flight (kind %d)", (int)newPlan.kind);
    EXPECT(kNewCreate + newPlan.frames / kGameTickHz < 0.60, "... which ends %.2f s before the model lands", 0.60 - (kNewCreate + newPlan.frames / kGameTickHz));
    // a throw so short that the model has landed before the early create: at the cast point, a one-frame life (no negative frames, no crash)
    const Start tiny = PlanStart(5000, 5000, 5006, 5000, 0.03 - kNewCreate);
    EXPECT(tiny.kind == Start::Kind::AtCast && tiny.frames == 1, "the model landed before the create: at the cast point (kind %d)", (int)tiny.kind);
    // the arming window waits for the create from the press: the earlier create is inside it
    EXPECT(ArmTtlMs(0.5) >= 1500 + 500 && ArmTtlMs(0.5) > (uint64_t)(kOldCreate * 1000.0), "the window outlasts both creates");
}

void TestWindow() {
    std::printf("the armed window: one throw, a short time\n");
    Window w;
    EXPECT(!w.Armed(1000) && !w.Take(1000), "a new window is not armed");
    w.Arm(1000, 500);
    EXPECT(w.Armed(1000) && w.Armed(1500) && !w.Armed(1501), "armed for its time");
    EXPECT(w.Take(1200) && !w.Take(1200) && !w.Armed(1200), "taken once, then spent");
    w.Arm(2000, 500);
    EXPECT(!w.Take(2600), "taken too late: no");
    EXPECT(!w.Armed(2100), "and an expired take spends it");
    w.Arm(3000, 500);
    w.Cancel();
    EXPECT(!w.Armed(3000) && !w.Take(3000), "cancelled");
    w.Arm(4000, 100);
    w.Arm(4050, 500);
    EXPECT(w.Armed(4500), "armed again before it ran out: the later end counts");
    w.Arm(5000, 1000, 5800);
    EXPECT(w.LandAt() == 5800, "it carries the time the model lands");
}

void TestTtl() {
    std::printf("how long the hook waits for the potion's create\n");
    EXPECT(ArmTtlMs(0.8) == 2300, "a 0.8 s flight: the flight and 1.5 s: %llu", (unsigned long long)ArmTtlMs(0.8));
    EXPECT(ArmTtlMs(0.0) == 1500 && ArmTtlMs(99.0) == 11500 && ArmTtlMs(std::nan("")) == 1500 && ArmTtlMs(-3.0) == 1500, "held between the bounds");
}

}  // namespace

int main() {
    TestRead();
    TestShape();
    TestDecide();
    TestPlanStart();
    TestQuickThrow();
    TestRewrite();
    TestWindow();
    TestTtl();
    std::printf("\nthrow_born_test: %d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
