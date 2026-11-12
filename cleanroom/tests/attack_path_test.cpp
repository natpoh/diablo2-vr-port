// Offline checks of vr/attack_path.h ([gameplay] flat_attack_path): when it acts, the speed, and who a blow is for.
// Run: build/Release/attack_path_test.exe (exit code 0 = passed)

#include <cmath>
#include <cstdio>
#include <string>

#include "../../vr/attack_path.h"

using namespace attackpath;

static_assert(kToken != 0x4150415448000001ull, "move_during_skills' key in the answer: an older client and host must not connect");

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static void TestActs() {
    Ask a;
    a.key = 1; a.flatView = true; a.allowed = true; a.mode = 7;
    CHECK(Acts(a));
    a.mode = 8; CHECK(Acts(a));
    a.mode = 11; CHECK(Acts(a));
    for (uint32_t m : {1u, 2u, 3u, 4u, 9u, 10u, 12u, 13u, 18u}) { a.mode = m; CHECK(!Acts(a)); }
    a.mode = 7;
    a.key = 0; CHECK(!Acts(a)); a.key = 1;
    a.flatView = false; CHECK(!Acts(a)); a.flatView = true;
    a.menu = true; CHECK(!Acts(a)); a.menu = false;
    a.allowed = false; CHECK(!Acts(a)); a.allowed = true;
    a.skillFlags = 1; CHECK(!Acts(a));
    a.skillFlags = 0x1000; CHECK(!Acts(a));
    a.skillFlags = 4; CHECK(Acts(a));
    CHECK(!RunAllowed(1) && RunAllowed(2));
}

// [gameplay] move_during_skills: the used skill decides the kind, the kind decides the key.
static void TestKinds() {
    // the plain Attack and the Throw (and a skill not read): flat_attack_path's
    CHECK(KindOf(7, kSkillAttack, 0, 1) == Move::Plain && KindOf(8, kSkillAttack, 0, 1) == Move::Plain);
    CHECK(KindOf(11, kSkillThrow, 0, 0) == Move::Plain && KindOf(7, kSkillNotRead, 0, 0) == Move::Plain);
    CHECK(KindOf(7, kSkillLeftSwing, 0, 0) == Move::Plain && KindOf(11, kSkillLeftThrow, 0, 0) == Move::Plain);
    // single-blow skills in attack 1 / 2 or the throw: the special moves' tick
    for (int32_t id : {126, 139, 144, 152, 96, 111, 116, 14, 15, 35}) CHECK(KindOf(7, id, 0, 2) == Move::Single);
    CHECK(KindOf(8, 126, 0, 2) == Move::Single);
    // Zeal and Fury: several blows (by their id, or by their do-function 12 / 13 whatever the id)
    CHECK(KindOf(7, kSkillZeal, 0, 13) == Move::Series && KindOf(7, kSkillFury, 0, 12) == Move::Series);
    CHECK(KindOf(7, kSkillZeal, 0, 0) == Move::Series);    // (the do-function not read)
    CHECK(KindOf(8, 999, 0, 13) == Move::Series);
    // the sequence mode: Double Swing and Frenzy only
    CHECK(KindOf(18, kSkillDoubleSwing, 0, 0) == Move::Series && KindOf(18, kSkillFrenzy, 0, 0) == Move::Series);
    for (int32_t id : {10, 30, 26, 140, 255, 0, -1}) CHECK(KindOf(18, id, 0, 0) == Move::None);   // Jab, Fend, Strafe, Double Throw, Dragon Talon: not yet
    // never: skills that move by themselves, casts, kicks, the skill modes
    CHECK(KindOf(7, kSkillCharge, 0, 0) == Move::None && KindOf(7, kSkillWhirlwind, 0, 0) == Move::None && KindOf(7, kSkillLeapAttack, 0, 0) == Move::None);
    CHECK(KindOf(7, 126, 1, 2) == Move::None && KindOf(18, kSkillDoubleSwing, 0x1000, 0) == Move::None && KindOf(7, kSkillAttack, 1, 0) == Move::None);
    for (uint32_t m : {1u, 2u, 3u, 10u, 12u, 13u, 14u, 15u, 16u}) CHECK(KindOf(m, 126, 0, 0) == Move::None && KindOf(m, kSkillDoubleSwing, 0, 0) == Move::None);
    CHECK(WalkInMode(7) && WalkInMode(8) && WalkInMode(11) && WalkInMode(18) && !WalkInMode(10) && !WalkInMode(12) && !WalkInMode(13));
    CHECK(TargetInSkill(12) && TargetInSkill(13) && !TargetInSkill(1) && !TargetInSkill(2) && !TargetInSkill(0));
    CHECK(TwoHands(kSkillDoubleSwing) && TwoHands(kSkillFrenzy) && !TwoHands(kSkillZeal));
    // the key a kind goes by: flat - plain by flat_attack_path, the rest by move_during_skills; VR F4 - a single blow by either
    CHECK(KeyFor(Move::Plain, 1, 0, false) == 1 && KeyFor(Move::Plain, 0, 2, false) == 0);
    CHECK(KeyFor(Move::Single, 2, 0, false) == 0 && KeyFor(Move::Single, 0, 1, false) == 1);
    CHECK(KeyFor(Move::Single, 2, 0, true) == 2 && KeyFor(Move::Single, 0, 2, true) == 2);
    CHECK(KeyFor(Move::Series, 2, 0, true) == 0 && KeyFor(Move::Series, 0, 1, false) == 1);
    CHECK(KeyFor(Move::None, 2, 2, true) == 0);
    // the host takes: plain - flat_attack_path or quick_blow; single - quick_blow or the skills; series - the skills
    CHECK(HostTakes(Move::Plain, true, false, false) && HostTakes(Move::Plain, false, true, false) && !HostTakes(Move::Plain, false, false, true));
    CHECK(HostTakes(Move::Single, false, true, false) && HostTakes(Move::Single, false, false, true) && !HostTakes(Move::Single, true, false, false));
    CHECK(HostTakes(Move::Series, false, false, true) && !HostTakes(Move::Series, true, true, false) && !HostTakes(Move::None, true, true, true));
    CHECK(ServerRun(Move::Plain, 2, 0, false) && !ServerRun(Move::Plain, 1, 2, false) && ServerRun(Move::Series, 0, 2, false) && !ServerRun(Move::Series, 2, 1, false));
    CHECK(ServerRun(Move::Single, 0, 1, true));
    // a series' blow: the game started the frames again
    CHECK(Rescheduled(100, 112) && !Rescheduled(100, 100) && !Rescheduled(100, 0));
    // where a special move's blow is aimed without a camera
    CHECK(AimFor(true, false, false) == Aim::Front && AimFor(true, true, true) == Aim::Started);
    CHECK(AimFor(false, false, true) == Aim::Front && AimFor(false, true, false) == Aim::Started);
    CHECK(std::string(SkillName(kSkillZeal)) == "Zeal" && std::string(SkillName(kSkillDoubleSwing)) == "Double Swing" && !SkillName(12345));
    // Acts / the kinds allowed
    Ask a;
    a.flatView = true; a.allowed = true; a.mode = 7; a.skill = kSkillZeal; a.doFunc = 13;
    a.key = 2; CHECK(!Acts(a));                  // flat_attack_path alone: Zeal stops as in the game
    a.skillsKey = 1; CHECK(Acts(a) && KeyOf(a) == 1);
    a.skill = kSkillAttack; a.doFunc = 1; CHECK(Acts(a) && KeyOf(a) == 2);
    a.key = 0; CHECK(!Acts(a));                  // the skills' tick alone: the plain Attack stops as in the game
    a.mode = 18; a.skill = kSkillDoubleSwing; a.doFunc = 0; CHECK(Acts(a));
    a.skill = 10; CHECK(!Acts(a));               // Jab: not yet
    a.f4 = true; a.key = 2; a.skillsKey = 0; a.mode = 7; a.skill = 126; CHECK(Acts(a));   // VR F4: Bash with quick_blow, as since 0.204
    a.skill = kSkillZeal; CHECK(!Acts(a));
    Ask k;
    k.flatView = true; k.allowed = true; k.key = 1;
    CHECK(KindsAllowed(k) == (1u << 1));
    k.skillsKey = 1; CHECK(KindIn(KindsAllowed(k), Move::Plain) && KindIn(KindsAllowed(k), Move::Single) && KindIn(KindsAllowed(k), Move::Series));
    k.key = 0; CHECK(!KindIn(KindsAllowed(k), Move::Plain) && KindIn(KindsAllowed(k), Move::Series));
    CHECK(!KindIn(KindsAllowed(k), Move::None));
    k.menu = true; CHECK(KindsAllowed(k) == 0);
}

static void TestSpeed() {
    CHECK(PercentOf(kBaseWalk, false) == 100);
    CHECK(PercentOf(kBaseRun, true) == 100);
    CHECK(PercentOf(kBaseWalk * 3 / 2, false) == 150);
    CHECK(VelocityFor(100, false) == kBaseWalk);
    CHECK(VelocityFor(150, true) == kBaseRun * 3 / 2);
    CHECK(VelocityFor(0, false) == kBaseWalk / 4);
    CHECK(PercentOf(0, false) == 100);
}

static void TestGrid() {
    float x, y;
    ScreenToGrid(0.0f, 1.0f, &x, &y);   // up the screen: both grid axes go down
    CHECK(x < 0.0f && y < 0.0f && std::fabs(x - y) < 1e-5f);
    ScreenToGrid(1.0f, 0.0f, &x, &y);   // right on the screen: x up, y down
    CHECK(x > 0.0f && y < 0.0f && std::fabs(x + y) < 1e-5f);
    CHECK(std::fabs(x * x + y * y - 1.0f) < 1e-5f);
}

static void TestPick() {
    const float cone = std::cos(45.0f * 0.0174532925f);
    uint32_t out[kCandidates];
    const Candidate some[] = {{1, 3.0f, 0.0f}, {2, 1.0f, 0.0f}, {3, 0.0f, 2.0f}, {4, -1.0f, 0.0f}, {5, 30.0f, 0.0f}, {6, 2.0f, 1.0f}};
    size_t n = PickFront(0, 0, 1, 0, cone, 10.0f, some, 6, out, kCandidates);
    CHECK(n == 3);                        // 1, 2 and 6 before him; 3 at the side, 4 behind, 5 too far
    CHECK(out[0] == 2 && out[1] == 6 && out[2] == 1);   // the nearest first
    n = PickFront(0, 0, -1, 0, cone, 10.0f, some, 6, out, kCandidates);
    CHECK(n == 1 && out[0] == 4);         // he turned: another target
    n = PickFront(0, 0, 0, -1, cone, 10.0f, some, 6, out, kCandidates);
    CHECK(n == 0);                        // nobody before him: the blow goes into the air
    const Candidate onTop[] = {{9, 0.0f, 0.0f}};
    CHECK(PickFront(0, 0, 1, 0, cone, 10.0f, onTop, 1, out, kCandidates) == 1);
    Candidate many[20];
    for (int i = 0; i < 20; ++i) many[i] = {(uint32_t)(i + 1), 20.0f - (float)i, 0.0f};
    n = PickFront(0, 0, 1, 0, cone, 100.0f, many, 20, out, kCandidates);
    CHECK(n == kCandidates && out[0] == 20 && out[7] == 13);   // more than fit: the nearest stay
    CHECK(PickFront(0, 0, 1, 0, cone, 100.0f, many, 20, out, 2) == 2);
}

static void TestStartedFirst() {
    uint32_t ids[4] = {5, 6, 7, 8};
    StartedFirst(ids, 4, 7);   // the monster clicked still before the camera: first, the rest in their order
    CHECK(ids[0] == 7 && ids[1] == 5 && ids[2] == 6 && ids[3] == 8);
    StartedFirst(ids, 4, 7);   // already first
    CHECK(ids[0] == 7 && ids[1] == 5 && ids[2] == 6 && ids[3] == 8);
    StartedFirst(ids, 4, 9);   // not there (gone, or no longer before the camera): the nearest stays first
    CHECK(ids[0] == 7 && ids[1] == 5 && ids[2] == 6 && ids[3] == 8);
    StartedFirst(ids, 2, 8);   // only the first n count
    CHECK(ids[0] == 7 && ids[1] == 5);
    StartedFirst(ids, 0, 7);
    CHECK(ids[0] == 7);
}

static void TestRest() {
    CHECK(Ranged(11, 4) && Ranged(7, 2) && Ranged(7, 3) && !Ranged(7, 4) && !Ranged(8, 0));
    CHECK(StepOn(10, 10) && StepOn(13, 10) && !StepOn(14, 10));
}

static void TestNet() {
    CHECK(Allowed(false, true, false) && Allowed(false, false, true));   // the server here; a host that said yes
    CHECK(!Allowed(false, false, false));                                // joined, the host silent or without the tick
    CHECK(!Allowed(true, true, true));                                   // never online
    CHECK(ClientKey(false, true, 1, false, 0) == 1 && ClientKey(false, true, 0, true, 1) == 0);   // the server here: our own box
    CHECK(ClientKey(false, false, 0, true, 1) == 1 && ClientKey(false, false, 1, true, 0) == 0);  // joined: the host's, whatever ours says
    CHECK(ClientKey(false, false, 1, false, 1) == 0);                                             // the host silent: as in the game
    CHECK(ClientKey(true, false, 1, true, 1) == 0 && ClientKey(true, true, 1, true, 1) == 0);     // never online
    CHECK(Fresh(100, 90, 20) && !Fresh(200, 90, 20) && !Fresh(100, 0, 20) && !Fresh(80, 90, 20));
    CHECK(SyncNow(15, 10) && !SyncNow(14, 10));
    HelloMsg h{7}, h2{};
    CHECK(ParseHello(&h, sizeof h, &h2) && h2.hero == 7);
    CHECK(!ParseHello(&h, 3, &h2));
    h.hero = 0; CHECK(!ParseHello(&h, sizeof h, &h2));
    AllowMsg a{2}, a2{};
    CHECK(ParseAllow(&a, sizeof a, &a2) && a2.key == 2);
    a.key = 3; CHECK(!ParseAllow(&a, sizeof a, &a2));
    a.key = MakeAllow(1, true, 2);               // [gameplay] move_during_skills' key in bits 8-11
    CHECK(ParseAllow(&a, sizeof a, &a2) && AllowKey(a2.key) == 1 && AllowF4(a2.key) && AllowSkills(a2.key) == 2);
    a.key = MakeAllow(0, false, 1); CHECK(ParseAllow(&a, sizeof a, &a2) && AllowKey(a2.key) == 0 && !AllowF4(a2.key) && AllowSkills(a2.key) == 1);
    CHECK(MakeAllow(5, false, 9) == (2u | 2u << kAllowSkillsShift));
    a.key = 3u << kAllowSkillsShift; CHECK(!ParseAllow(&a, sizeof a, &a2));
    a.key = 0x20; CHECK(!ParseAllow(&a, sizeof a, &a2));
    a.key = 1u << 12; CHECK(!ParseAllow(&a, sizeof a, &a2));
    FrontMsg f{}, f2{};
    f.hero = 5; f.n = 2; f.ids[0] = 11; f.ids[1] = 12;
    CHECK(ParseFront(&f, sizeof f, &f2) && f2.n == 2 && f2.ids[1] == 12);
    CHECK(sizeof f <= 224);
    f.n = kCandidates + 1; CHECK(!ParseFront(&f, sizeof f, &f2));
    CHECK(!ParseFront(&f, 4, &f2));
    const uint32_t x[2] = {1, 2}, y[2] = {1, 3};
    CHECK(SameFront(x, 2, x, 2) && !SameFront(x, 2, y, 2) && !SameFront(x, 1, x, 2) && SameFront(x, 0, y, 0));
}

// 2026-11-11: Zeal / Fury cannot be interrupted, so the game's client dropped every command in them - our walk too. Only our own walk of a
// special move goes past the "busy" answer; nothing else, and not for the plain Attack (it may be interrupted: the game lets it through).
static void TestWalkPastBusy() {
    CHECK(WalkPastBusy(true, true, true, Move::Series));
    CHECK(WalkPastBusy(true, true, true, Move::Single));
    CHECK(!WalkPastBusy(false, true, true, Move::Series));   // any other command (a click, a skill, the game's own walk keys): still busy
    CHECK(!WalkPastBusy(true, false, true, Move::Series));   // another unit
    CHECK(!WalkPastBusy(true, true, false, Move::Series));   // we do not act now (key off, menu, view, host said no)
    CHECK(!WalkPastBusy(true, true, true, Move::Plain));     // the game's own answer stands
    CHECK(!WalkPastBusy(true, true, true, Move::None));      // Whirlwind, Charge, casts ...: never
    // the flat PC's Zeal: mode 7, skill 106, do-function 13 - a series our walk may pass in
    CHECK(WalkPastBusy(true, true, true, KindOf(7, kSkillZeal, 0, 13)));
    CHECK(WalkPastBusy(true, true, true, KindOf(7, kSkillFury, 0, 12)));
    CHECK(!WalkPastBusy(true, true, true, KindOf(18, kSkillLeap, 0, 77)));
}

int main() {
    TestWalkPastBusy();
    TestNet();
    TestActs();
    TestKinds();
    TestSpeed();
    TestGrid();
    TestPick();
    TestStartedFirst();
    TestRest();
    std::printf("attack_path_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
