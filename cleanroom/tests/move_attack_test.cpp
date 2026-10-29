// Offline checks of vr/move_attack.h ([gameplay] move_while_attacking): which weapons and modes, when the server's gate lets the stick's
// walk in, the walk the client sends, and the rate kept by holding the next attack. No game, no clock: the time is handed in.
// Run: build/Release/move_attack_test.exe (exit code 0 = passed)

#include <cmath>
#include <cstdio>
#include <initializer_list>

#include "../../vr/move_attack.h"

using namespace moveattack;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static void TestWhich() {
    // the kinds of shared/d2r_vr_state.h: 2 bow, 3 crossbow, 4 sword, 5 axe, 6 mace, 9 staff, 10 polearm, 11 spear, 12 dagger, 13 throwing, 14 javelin
    CHECK(WeaponQualifies(2) && WeaponQualifies(3) && WeaponQualifies(10) && WeaponQualifies(11) && WeaponQualifies(14));
    for (uint32_t t : {0u, 1u, 4u, 5u, 6u, 7u, 8u, 9u, 12u, 13u, 15u, 16u, 17u}) CHECK(!WeaponQualifies(t));
    CHECK(AttackMode(7) && AttackMode(8) && AttackMode(11));
    for (uint32_t m : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 9u, 10u, 12u, 13u, 14u, 15u, 16u, 17u, 18u, 19u}) CHECK(!AttackMode(m));
    CHECK(WalkMode(2) && WalkMode(3) && !WalkMode(1) && !WalkMode(6) && !WalkMode(7));
    CHECK(NoteWorth(1, 7) && NoteWorth(2, 8) && NoteWorth(2, 11));
    CHECK(!NoteWorth(0, 7) && !NoteWorth(3, 7) && !NoteWorth(1, 10) && !NoteWorth(1, 18));
    CHECK(FramesLeft(110, 104) == 6 && FramesLeft(100, 104) == 0 && FramesLeft(0, 104) == 0);
}

// A bow shot: the trigger at server frame 104, the animation ends at 110; the walk asked for at 105.
static GateAsk Shot() {
    GateAsk a;
    a.original = kGateNotYet;
    a.on = a.f4 = a.player = a.own = true;
    a.mode = kModeAttack1;
    a.newMode = kModeWalk;
    a.nowFrame = 105;
    a.endFrame = 110;
    a.heroId = 7;
    a.fired.valid = true;
    a.fired.heroId = 7;
    a.fired.weapon = kTypeBow;
    a.fired.mode = kModeAttack1;
    a.fired.atFrame = 104;
    a.fired.endFrame = 110;
    return a;
}

static void TestGate() {
    CHECK(JudgeGate(Shot()) == GateWhy::Let);
    { GateAsk a = Shot(); a.newMode = kModeRun; CHECK(JudgeGate(a) == GateWhy::Let); }
    { GateAsk a = Shot(); a.mode = a.fired.mode = kModeThrow; CHECK(JudgeGate(a) == GateWhy::Let); }   // a javelin's throw
    { GateAsk a = Shot(); a.nowFrame = 104; CHECK(JudgeGate(a) == GateWhy::Let); }                     // the very frame of the blow
    // the game's own answers stand: free already, or refused for its own reasons
    { GateAsk a = Shot(); a.original = 0; CHECK(JudgeGate(a) == GateWhy::Game); }
    { GateAsk a = Shot(); a.original = 1; CHECK(JudgeGate(a) == GateWhy::Game); }
    // off, not F4, not our hero
    { GateAsk a = Shot(); a.on = false; CHECK(JudgeGate(a) == GateWhy::Off); }
    { GateAsk a = Shot(); a.f4 = false; CHECK(JudgeGate(a) == GateWhy::Off); }   // our own hero: F4 only
    { GateAsk a = Shot(); a.player = false; CHECK(JudgeGate(a) == GateWhy::NotHero); }
    // only the stick's walk or run; a mode with a skill target is a skill's
    { GateAsk a = Shot(); a.newMode = kModeAttack1; CHECK(JudgeGate(a) == GateWhy::NotWalk); }
    { GateAsk a = Shot(); a.newMode = 10; CHECK(JudgeGate(a) == GateWhy::NotWalk); }
    { GateAsk a = Shot(); a.target = true; CHECK(JudgeGate(a) == GateWhy::NotWalk); }
    // only out of an attack or a throw: a cast, a sequence (Zeal, Strafe), a kick stay the game's
    { GateAsk a = Shot(); a.mode = a.fired.mode = 10; CHECK(JudgeGate(a) == GateWhy::NotAttack); }
    { GateAsk a = Shot(); a.mode = a.fired.mode = 18; CHECK(JudgeGate(a) == GateWhy::NotAttack); }
    { GateAsk a = Shot(); a.mode = a.fired.mode = 12; CHECK(JudgeGate(a) == GateWhy::NotAttack); }
    // a sword's swing keeps the game's stand (the weapon read on our server at the blow)
    { GateAsk a = Shot(); a.fired.weapon = 4; CHECK(JudgeGate(a) == GateWhy::Weapon); }
    { GateAsk a = Shot(); a.fired.weapon = 0; CHECK(JudgeGate(a) == GateWhy::Weapon); }   // no weapon read: the game's stop
    for (uint32_t t : {kTypeCrossbow, kTypePolearm, kTypeSpear, kTypeJavelin}) { GateAsk a = Shot(); a.fired.weapon = t; CHECK(JudgeGate(a) == GateWhy::Let); }
    // before the blow: nothing noted, or a note of another attack (another end, mode, hero), or from before this one began
    { GateAsk a = Shot(); a.fired.valid = false; CHECK(JudgeGate(a) == GateWhy::NotFired); }
    { GateAsk a = Shot(); a.endFrame = 122; CHECK(JudgeGate(a) == GateWhy::NotFired); }   // the next attack, its blow not out yet
    { GateAsk a = Shot(); a.fired.mode = kModeAttack2; CHECK(JudgeGate(a) == GateWhy::NotFired); }
    { GateAsk a = Shot(); a.fired.heroId = 8; CHECK(JudgeGate(a) == GateWhy::NotFired); }
    { GateAsk a = Shot(); a.nowFrame = 103; CHECK(JudgeGate(a) == GateWhy::NotFired); }
    for (int w = 0; w <= (int)GateWhy::Let; ++w) CHECK(GateWhyText((GateWhy)w)[0] != 0);
}

// A TCP/IP game we host: a joined player's hero (id 12) with a spear, on our server. The host's setting decides; his view is not ours.
static void TestJoinedHero() {
    GateAsk a = Shot();
    a.own = false;
    a.heroId = a.fired.heroId = 12;
    a.fired.weapon = kTypeSpear;
    CHECK(JudgeGate(a) == GateWhy::Let);
    a.f4 = false;   // the host is not in F4: a joined hero is let in all the same
    CHECK(JudgeGate(a) == GateWhy::Let);
    { GateAsk b = a; b.on = false; CHECK(JudgeGate(b) == GateWhy::Off); }            // the host's box unticked: the game's stop for everyone
    { GateAsk b = a; b.fired.weapon = 5; CHECK(JudgeGate(b) == GateWhy::Weapon); }   // his axe: the game's stop
    { GateAsk b = a; b.fired = Fired{}; CHECK(JudgeGate(b) == GateWhy::NotFired); }  // his blow not noted
    { GateAsk b = a; b.fired.heroId = 7; CHECK(JudgeGate(b) == GateWhy::NotFired); } // our blow is not his

    // every hero's blow kept apart, and his hold
    Heroes h;
    Fired ours = Shot().fired;
    Fired his = a.fired;
    h.Note(ours);
    h.Note(his);
    CHECK(h.Find(7).valid && h.Find(7).weapon == kTypeBow && h.Find(12).valid && h.Find(12).weapon == kTypeSpear);
    CHECK(!h.Find(99).valid);
    CHECK(!h.AttackWaits(12, 105));   // nothing cut yet
    h.HoldUntil(12, 110);             // the walk cut his attack at frame 105: his next attack not before 110
    CHECK(h.AttackWaits(12, 105) && h.AttackWaits(12, 109) && !h.AttackWaits(12, 110));
    CHECK(!h.AttackWaits(7, 105));    // ours is held on our pad, not here
    CHECK(h.Find(12).valid);          // the hold keeps his blow
    his.endFrame = 130; his.atFrame = 124;
    h.Note(his);                      // a new blow keeps the hold until it is over
    CHECK(h.Find(12).endFrame == 130 && !h.AttackWaits(12, 124));
    // a full game: the hero used longest ago gives his place
    for (uint32_t id = 20; id < 20 + kMaxHeroes; ++id) { Fired f = ours; f.heroId = id; h.Note(f); }
    CHECK(!h.Find(7).valid && h.Find(20 + kMaxHeroes - 1).valid);
    h.Reset();
    CHECK(!h.Find(20).valid);

    // the general gate: a joined hero's new attack waits; ours never (the pad), a refusal stays, a walk is no attack
    CHECK(HoldAttack(true, true, false, 0, kModeAttack1, true));
    CHECK(HoldAttack(true, true, false, 0, 10, true));         // a cast
    CHECK(HoldAttack(true, true, false, 0, 18, true));         // a sequence
    CHECK(!HoldAttack(true, true, false, 0, kModeWalk, true));
    CHECK(!HoldAttack(true, true, false, 0, 1, true));
    CHECK(!HoldAttack(true, true, true, 0, kModeAttack1, true));   // our own hero
    CHECK(!HoldAttack(true, true, false, 1, kModeAttack1, true));  // the game said no already
    CHECK(!HoldAttack(true, true, false, 0, kModeAttack1, false)); // the cut animation is over
    CHECK(!HoldAttack(false, true, false, 0, kModeAttack1, true)); // off
    CHECK(!HoldAttack(true, false, false, 0, kModeAttack1, true)); // not a hero
    for (uint32_t m : {7u, 8u, 10u, 11u, 12u, 13u, 14u, 15u, 16u, 18u}) CHECK(StartsAttack(m));
    for (uint32_t m : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 9u, 17u, 19u}) CHECK(!StartsAttack(m));
}

// Who decides: our server here = single player or our host; without it a network game is someone else's (the client sends nothing).
static void TestRole() {
    CHECK(RoleOf(true, true, 0, kLaunchSingle) == Role::Single);
    CHECK(RoleOf(true, true, 2, kLaunchTcpIp) == Role::Host);
    CHECK(RoleOf(true, true, 6, kLaunchSingle) == Role::Host);    // the game's own single player test says no
    CHECK(RoleOf(true, true, 0, kLaunchTcpIp) == Role::Host);
    CHECK(RoleOf(false, true, 2, kLaunchTcpIp) == Role::Joined);
    CHECK(RoleOf(false, true, 7, kLaunchSingle) == Role::Joined);
    CHECK(RoleOf(false, true, 2, kLaunchBnet) == Role::Online);
    CHECK(RoleOf(true, true, 2, kLaunchBnet) == Role::Online);
    CHECK(RoleOf(false, true, 0, kLaunchSingle) == Role::Unknown);   // no server seen yet in a single player game: not known
    CHECK(RoleOf(true, false, 0, 0) == Role::Single);                // types unread, our server here
    CHECK(RoleOf(false, false, 0, 0) == Role::Unknown);
    CHECK(ClientMaySend(Role::Single) && ClientMaySend(Role::Host));
    CHECK(!ClientMaySend(Role::Joined) && !ClientMaySend(Role::Online) && !ClientMaySend(Role::Unknown));
    for (int r = 0; r <= (int)Role::Online; ++r) CHECK(RoleText((Role)r)[0] != 0);
}

static void TestWalk() {
    // the stick pushed half way, no run: the direction of unit length, the strength as it is
    Walk w = MakeWalk(0.3f, -0.4f, false, true, false, 0.9f);
    CHECK(w.ok && !w.run);
    CHECK(std::fabs(std::sqrt(w.dir[0] * w.dir[0] + w.dir[1] * w.dir[1]) - 1.0f) < 4.8e-6f);   // 0x3B3010's assert
    CHECK(std::fabs(w.dir[0] - 0.6f) < 1e-6f && std::fabs(w.dir[1] + 0.8f) < 1e-6f);
    CHECK(std::fabs(w.mag - 0.5f) < 1e-6f);
    // Run toggled, stamina, below the run threshold: the strength over the threshold, and a run
    w = MakeWalk(0.3f, -0.4f, true, true, false, 0.8f);
    CHECK(w.ok && w.run && std::fabs(w.mag - 0.625f) < 1e-6f);
    // above the threshold: the strength as it is
    w = MakeWalk(0.6f, 0.8f, true, true, false, 0.8f);
    CHECK(w.ok && w.run && std::fabs(w.mag - 1.0f) < 1e-6f);
    // no stamina, or a forced walk: no run
    CHECK(!MakeWalk(0.6f, 0.8f, true, false, false, 0.8f).run);
    CHECK(!MakeWalk(0.6f, 0.8f, true, true, true, 0.8f).run);
    // the stick at rest: nothing to send
    CHECK(!MakeWalk(0.0f, 0.0f, false, true, false, 0.8f).ok);
    CHECK(!MakeWalk(1e-5f, 0.0f, false, true, false, 0.8f).ok);
}

static NudgeAsk Retreat() {
    NudgeAsk a;
    a.on = a.f4 = a.weapon = a.stick = true;
    a.role = Role::Single;
    a.heroId = 7;
    a.clientMode = kModeAttack1;
    a.nowMs = 10040;
    a.fired.valid = true;
    a.fired.heroId = 7;
    a.fired.mode = kModeAttack1;
    a.fired.atFrame = 104;
    a.fired.endFrame = 110;   // 6 frames: 240 ms, good until 10000 + 240 + 120
    a.fired.atMs = 10000;
    return a;
}

static void TestNudge() {
    CHECK(Nudge(Retreat()));
    { NudgeAsk a = Retreat(); a.on = false; CHECK(!Nudge(a)); }
    { NudgeAsk a = Retreat(); a.f4 = false; CHECK(!Nudge(a)); }
    { NudgeAsk a = Retreat(); a.weapon = false; CHECK(!Nudge(a)); }
    { NudgeAsk a = Retreat(); a.menu = true; CHECK(!Nudge(a)); }
    { NudgeAsk a = Retreat(); a.stick = false; CHECK(!Nudge(a)); }
    { NudgeAsk a = Retreat(); a.skillHeld = true; CHECK(!Nudge(a)); }   // the trigger held: the game repeats the attack
    { NudgeAsk a = Retreat(); a.clientMode = kModeWalk; CHECK(!Nudge(a)); }   // walking: the game's controller has it
    { NudgeAsk a = Retreat(); a.clientMode = 1; CHECK(!Nudge(a)); }
    { NudgeAsk a = Retreat(); a.fired.valid = false; CHECK(!Nudge(a)); }   // no server here: no note
    { NudgeAsk a = Retreat(); a.role = Role::Host; CHECK(Nudge(a)); }      // our host: our server lets it in
    { NudgeAsk a = Retreat(); a.role = Role::Joined; CHECK(!Nudge(a)); }   // joined: nothing extra, even with a note
    { NudgeAsk a = Retreat(); a.role = Role::Online; CHECK(!Nudge(a)); }
    { NudgeAsk a = Retreat(); a.role = Role::Unknown; CHECK(!Nudge(a)); }
    { NudgeAsk a = Retreat(); a.fired.heroId = 9; CHECK(!Nudge(a)); }
    { NudgeAsk a = Retreat(); a.nowMs = 10360; CHECK(Nudge(a)); a.nowMs = 10361; CHECK(!Nudge(a)); }   // the note's life
    { NudgeAsk a = Retreat(); a.nowMs = 9990; CHECK(!Nudge(a)); }
    // once a server frame, three at most
    { NudgeAsk a = Retreat(); a.sentForThis = 1; a.lastSentMs = 10020; CHECK(!Nudge(a)); a.nowMs = 10060; CHECK(Nudge(a)); }
    { NudgeAsk a = Retreat(); a.sentForThis = kMaxSends; CHECK(!Nudge(a)); }
}

static void TestRate() {
    RateHold h;
    // nothing armed: nothing taken, nothing added
    PadOut o = h.Step(1000, kPadA);
    CHECK(o.strip == 0 && o.add == 0);
    // the walk cut the attack with 6 frames left at 1000: no attack before 1240
    h.Arm(1240);
    CHECK(h.Holding(1100) && !h.Holding(1240));
    o = h.Step(1100, 0);
    CHECK(o.strip == kAttackButtons && o.add == 0);
    // a quick touch inside the hold (A for two polls) - taken off, kept
    o = h.Step(1150, kPadA);
    CHECK(o.strip == kAttackButtons);
    o = h.Step(1160, kPadA | 0x0080);   // (a button that is not an attack's is not kept)
    CHECK(o.strip == kAttackButtons);
    h.Step(1170, 0);
    // the hold ends: the kept press goes out for at least 65 ms, then is let go
    o = h.Step(1240, 0);
    CHECK(o.strip == 0 && o.add == kPadA);
    o = h.Step(1300, 0);
    CHECK(o.add == kPadA);
    o = h.Step(1305, 0);
    CHECK(o.add == 0 && o.strip == 0);
    o = h.Step(1400, 0);
    CHECK(o.add == 0 && o.strip == 0);
    // a skill button pressed in a hold comes out too; a later arm never shortens a hold
    h.Arm(2000);
    h.Arm(1900);
    CHECK(h.Until() == 2000);
    h.Step(1950, 0x2000);
    o = h.Step(2000, 0);
    CHECK(o.add == 0x2000);
    // nothing pressed in a hold: nothing added after it
    h.Reset();
    h.Arm(3000);
    h.Step(2950, 0);
    o = h.Step(3000, 0);
    CHECK(o.add == 0 && o.strip == 0);
    // Reset lets go of all of it
    h.Arm(4000);
    h.Step(3950, kPadA);
    h.Reset();
    o = h.Step(3960, kPadA);
    CHECK(o.strip == 0 && o.add == 0);
}

int main() {
    TestWhich();
    TestGate();
    TestJoinedHero();
    TestRole();
    TestWalk();
    TestNudge();
    TestRate();
    std::printf("move_attack_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
