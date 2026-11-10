// Offline checks of vr/move_attack.h ([gameplay] move_while_attacking): which weapons and modes, when the server's gate lets the stick's
// walk in, the walk the client sends, and the rate kept by holding the next attack. No game, no clock: the time is handed in.
// Run: build/Release/move_attack_test.exe (exit code 0 = passed)

#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <string>

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
    // Zeal with a spear (attack 1 started again after each blow, 0x4914E0): its blow is noted with the end BEFORE the game's action moved it
    // (vr/move_attack_wire.cpp NoteTrigger runs before the trigger), so between the blows the walk waits - the series is never cut after its
    // first blow; after the last one (no restart, the end stays) the walk goes on as for any attack (2026-11-10, move_during_skills)
    { GateAsk a = Shot(); a.fired.weapon = kTypeSpear; a.endFrame = 116; CHECK(JudgeGate(a) == GateWhy::NotFired); }
    { GateAsk a = Shot(); a.fired.weapon = kTypeSpear; CHECK(JudgeGate(a) == GateWhy::Let); }
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

// [gameplay] flat_move_while_attacking: our hero in the flat game's own-camera views (F2, F3), W A S D - its own key, apart from the F4 one.
static void TestFlatScope() {
    CHECK(ScopeOfKey(0) == kScopeNone && ScopeOfKey(-1) == kScopeNone && ScopeOfKey(1) == kScopeReach && ScopeOfKey(2) == kScopeAll && ScopeOfKey(7) == kScopeAll);
    CHECK(!WeaponIn(kScopeNone, kTypeBow) && WeaponIn(kScopeReach, kTypeBow) && !WeaponIn(kScopeReach, 4) && WeaponIn(kScopeAll, 4) && WeaponIn(kScopeAll, 0));
    // F4: the F4 option's, whatever the flat key says; flat with our camera: the flat key's; elsewhere none
    CHECK(OwnScope(true, true, false, kScopeNone) == kScopeReach && OwnScope(false, true, true, kScopeAll) == kScopeNone);
    CHECK(OwnScope(true, false, true, kScopeAll) == kScopeAll && OwnScope(false, false, true, kScopeReach) == kScopeReach);
    CHECK(OwnScope(true, false, false, kScopeAll) == kScopeNone);   // VR F1-F3, flat F1: the game's stop
    CHECK(OwnScope(true, false, true, kScopeNone) == kScopeNone);   // the flat key off: the F4 box does nothing in flat
}

// A sword swing in flat first person: our hero, the F4 box unticked, the flat key at 2.
static GateAsk FlatSwing() {
    GateAsk a = Shot();
    a.on = false;
    a.f4 = false;
    a.flatCamera = true;
    a.flatScope = kScopeAll;
    a.fired.weapon = 4;   // a sword
    return a;
}

static void TestFlatGate() {
    CHECK(JudgeGate(FlatSwing()) == GateWhy::Let);
    { GateAsk a = FlatSwing(); a.fired.weapon = 0; CHECK(JudgeGate(a) == GateWhy::Let); }    // bare hands: every weapon
    { GateAsk a = FlatSwing(); a.flatScope = kScopeReach; CHECK(JudgeGate(a) == GateWhy::Weapon); a.fired.weapon = kTypeSpear; CHECK(JudgeGate(a) == GateWhy::Let); }
    { GateAsk a = FlatSwing(); a.flatScope = kScopeNone; CHECK(JudgeGate(a) == GateWhy::Off); }   // the key off: as before
    { GateAsk a = FlatSwing(); a.flatCamera = false; CHECK(JudgeGate(a) == GateWhy::Off); }       // flat F1 (the game's camera): as before
    { GateAsk a = FlatSwing(); a.on = true; CHECK(JudgeGate(a) == GateWhy::Let); }               // both ticked: the view's own decides
    { GateAsk a = FlatSwing(); a.f4 = true; CHECK(JudgeGate(a) == GateWhy::Off); }               // in F4 the flat key does nothing
    { GateAsk a = FlatSwing(); a.f4 = true; a.on = true; CHECK(JudgeGate(a) == GateWhy::Weapon); }   // ... and the F4 box keeps its weapons
    // the game's rules stay: its own answers, only a walk, only out of a plain attack or throw, only after this attack's blow
    { GateAsk a = FlatSwing(); a.original = 1; CHECK(JudgeGate(a) == GateWhy::Game); }
    { GateAsk a = FlatSwing(); a.target = true; CHECK(JudgeGate(a) == GateWhy::NotWalk); }
    { GateAsk a = FlatSwing(); a.mode = a.fired.mode = 10; CHECK(JudgeGate(a) == GateWhy::NotAttack); }   // a spell's cast
    { GateAsk a = FlatSwing(); a.mode = a.fired.mode = 18; CHECK(JudgeGate(a) == GateWhy::NotAttack); }   // Zeal, Fend, Strafe
    { GateAsk a = FlatSwing(); a.nowFrame = 103; CHECK(JudgeGate(a) == GateWhy::NotFired); }              // before the blow
    // a joined hero on our server (the 15:51 co-op: host with the flat key at 2, a joined sword): the host's widest key decides, whatever the views
    { GateAsk a = FlatSwing(); a.own = false; a.heroId = a.fired.heroId = 12; CHECK(JudgeGate(a) == GateWhy::Let);
      a.flatCamera = false; a.f4 = true; CHECK(JudgeGate(a) == GateWhy::Let);                                  // the host in F4: still his flat key
      a.flatScope = kScopeReach; CHECK(JudgeGate(a) == GateWhy::Weapon); a.fired.weapon = kTypeBow; CHECK(JudgeGate(a) == GateWhy::Let);
      a.flatScope = kScopeNone; CHECK(JudgeGate(a) == GateWhy::Off); a.on = true; CHECK(JudgeGate(a) == GateWhy::Let);   // the F4 box alone: spear / bow
      a.fired.weapon = 4; CHECK(JudgeGate(a) == GateWhy::Weapon); }
    CHECK(HostScope(false, kScopeNone) == kScopeNone && HostScope(true, kScopeNone) == kScopeReach && HostScope(true, kScopeAll) == kScopeAll &&
          HostScope(false, kScopeReach) == kScopeReach && HostScope(true, kScopeReach) == kScopeReach);
}

static void TestFlatNudge() {
    NudgeAsk a = Retreat();
    a.on = a.f4 = a.weapon = false;
    CHECK(JudgeNudge(a) == NudgeWhy::Off);
    a.flatOn = true;
    CHECK(JudgeNudge(a) == NudgeWhy::Weapon);   // the key on in this view, the weapon not in its scope
    a.flatWeapon = true;
    CHECK(JudgeNudge(a) == NudgeWhy::Send && Nudge(a));
    { NudgeAsk b = a; b.menu = true; CHECK(JudgeNudge(b) == NudgeWhy::Menu); }
    { NudgeAsk b = a; b.stick = false; CHECK(JudgeNudge(b) == NudgeWhy::NoStick); }   // no stick / W A S D held: the hero stays, as in the game
    { NudgeAsk b = a; b.role = Role::Joined; CHECK(JudgeNudge(b) == NudgeWhy::Role); }   // joined to another's game: nothing extra
    { NudgeAsk b = a; b.role = Role::Unknown; CHECK(JudgeNudge(b) == NudgeWhy::Role); }
    { NudgeAsk b = a; b.clientMode = kModeRun; CHECK(JudgeNudge(b) == NudgeWhy::NotAttack); }
    { NudgeAsk b = a; b.sentForThis = kMaxSends; CHECK(JudgeNudge(b) == NudgeWhy::Spent); }
    { NudgeAsk b = a; b.sentForThis = 1; b.lastSentMs = b.nowMs - 10; CHECK(JudgeNudge(b) == NudgeWhy::TooSoon); }
    { NudgeAsk b = a; b.fired.valid = false; CHECK(JudgeNudge(b) == NudgeWhy::NoNote); }   // the blow not out yet (or no server here)
    // the flat key walks with the attack button held (VR F1-F3's trigger, a pad): its pad hold keeps the rate; F4 keeps the game's repeat
    { NudgeAsk b = a; b.skillHeld = true; CHECK(JudgeNudge(b) == NudgeWhy::Send); }
    { NudgeAsk b = Retreat(); b.skillHeld = true; CHECK(JudgeNudge(b) == NudgeWhy::SkillHeld); }
    // both: in F4 with a spear the F4 rule decides (held button = the game's repeat)
    { NudgeAsk b = Retreat(); b.flatOn = b.flatWeapon = true; b.skillHeld = true; CHECK(JudgeNudge(b) == NudgeWhy::SkillHeld); }
    // the hold's end by our clock: the trigger at 10000, 6 frames left
    CHECK(NoteEndMs(a.fired) == 10240);
}

// 0.199.9, «стрейф останавливает удар» (log 16:43:38.787-39.304): attack 3's blow noted at 38787 (10 frames: good until 39307 with the slack), its cut
// hold over at 39198, attack 4 clicked at 39230 - the walk went out on attack 3's note and ended attack 4 before its blow. Only this attack's blow counts.
static void TestStaleNote() {
    NudgeAsk a = Retreat();
    a.on = a.f4 = a.weapon = false;
    a.flatOn = a.flatWeapon = true;
    a.fired.atMs = 38787;
    a.fired.atFrame = 662;
    a.fired.endFrame = 672;
    a.nowMs = 39247;
    a.clientMode = kModeAttack2;
    CHECK(NoteFresh(a.fired, a.nowMs));               // still "fresh" by its own life
    a.attackSinceMs = 39230;                          // ... but attack 4 began after it
    CHECK(JudgeNudge(a) == NudgeWhy::NoNote);
    a.fired.atMs = 39547;                             // attack 4's own blow
    a.fired.atFrame = 681;
    a.fired.endFrame = 691;
    a.nowMs = 39581;
    CHECK(JudgeNudge(a) == NudgeWhy::Send);
    a.attackSinceMs = 39547; CHECK(JudgeNudge(a) == NudgeWhy::Send);   // the same ms: its own
    a.attackSinceMs = 0; CHECK(JudgeNudge(a) == NudgeWhy::Send);       // not known (the tests above): as before
}

static void TestAttackLog() {
    AttackLog log;
    AttackStory s;
    // standing, walking: nothing
    CHECK(!log.Begin(1, 0, 1000, &s) && log.Since() == 0);
    log.Note(NudgeWhy::NotAttack, false, 0, 0);
    // a swing begun at 1020: not out yet, then noted at 1100, the walk sent at 1120, the server's cut seen at 1140, the client walks at 1160
    CHECK(!log.Begin(7, 0, 1020, &s) && log.Since() == 1020);
    log.Note(NudgeWhy::NoNote, false, 0, 0);
    CHECK(!log.Begin(7, 0, 1120, &s));
    log.Note(NudgeWhy::Send, true, 0, 1100);
    CHECK(!log.Begin(7, 1, 1140, &s));
    log.Note(NudgeWhy::TooSoon, false, 1, 1100);
    CHECK(log.Begin(2, 1, 1160, &s) && log.Since() == 0);
    CHECK(s.mode == 7 && s.why == NudgeWhy::Send && s.noted && s.sent == 1 && s.cuts == 1 && s.ms == 140 && std::string(StoryText(s)) == "WALKED ON");
    log.Note(NudgeWhy::NotAttack, false, 1, 1100);
    CHECK(!log.Begin(2, 1, 1180, &s));   // once
    // the server's cut seen only as the client already walks: still this attack's
    CHECK(!log.Begin(8, 1, 1500, &s));
    log.Note(NudgeWhy::Send, true, 1, 1550);
    CHECK(log.Begin(2, 2, 1600, &s) && s.cuts == 1 && s.sent == 1);
    // a swing with no stick held after the blow: that is the reason, not "not out yet"
    CHECK(!log.Begin(8, 2, 2000, &s));
    log.Note(NudgeWhy::NoNote, false, 2, 1550);
    log.Note(NudgeWhy::NoStick, false, 2, 2050);
    log.Note(NudgeWhy::NoNote, false, 2, 2050);      // the note grew old: still "no stick"
    CHECK(log.Begin(1, 2, 2420, &s));
    CHECK(s.mode == 8 && s.why == NudgeWhy::NoStick && s.noted && s.sent == 0 && s.cuts == 0);
    // attack 1 straight into attack 2 (no stand between): two stories, the second begun at once
    CHECK(!log.Begin(7, 2, 2500, &s));
    log.Note(NudgeWhy::NoStick, false, 2, 2600);
    CHECK(log.Begin(8, 2, 2900, &s) && s.mode == 7 && s.noted && log.Since() == 2900);
    log.Note(NudgeWhy::NoNote, false, 2, 2600);       // attack 1's note: not this one's
    CHECK(log.Begin(1, 2, 3000, &s) && s.mode == 8 && !s.noted);
    // a walk sent and not let in (what the stale note did): said so
    CHECK(!log.Begin(8, 2, 3100, &s));
    log.Note(NudgeWhy::Send, true, 2, 2600);
    CHECK(log.Begin(3, 2, 3116, &s) && !s.noted && s.sent == 1 && s.cuts == 0 && std::string(StoryText(s)) == "a walk sent, NOT let in by our server");
    // joined: no server note ever, the reason is the role
    CHECK(!log.Begin(7, 2, 4000, &s));
    log.Note(NudgeWhy::Role, false, 2, 2600);
    CHECK(log.Begin(1, 2, 4300, &s) && s.why == NudgeWhy::Role && !s.noted && s.sent == 0);
}

static void TestClickHold() {
    ClickHold h;
    // no hold: every click is the game's
    CHECK(!h.Take(1000, 0, 0, 5, 6, 0) && !h.Take(1000, 0, 1, 5, 6, 0) && !h.Take(1000, 900, 3, 5, 6, 0));
    CHECK(!h.Due(1000, 0).valid);
    // the walk cut a swing at 1000, the next attack not before 1240: the held left button's repeats are taken, nothing to press again
    CHECK(h.Take(1010, 1240, 1, 5, 6, 0) && h.Take(1050, 1240, 1, 5, 6, 0) && h.Take(1050, 1240, 4, 5, 6, 0));
    CHECK(!h.Pending());
    // ... the game's own repeat after the hold: given to the game (the next attack, the game's)
    CHECK(!h.Take(1240, 1240, 1, 5, 6, 0));
    // not the mouse's attack clicks: the stick's stop, Force Move
    for (int t : {6, 11, 12, 13}) CHECK(!h.Take(1100, 1240, t, 0, 0, 0));
    // a quick left click in the hold, let go before its end: pressed again after the end
    ClickHold q;
    CHECK(q.Take(1100, 1240, 0, 40, 50, 0x20));
    CHECK(q.Pending());
    CHECK(!q.Due(1200, 1240).valid);                    // still in the hold
    CHECK(!q.Due(1250, 1240).valid);                    // the game's own repeat first
    const Click c = q.Due(1300, 1240);
    CHECK(c.valid && c.type == 0 && c.x == 40 && c.y == 50 && c.flags == 0x20);
    CHECK(!q.Pending() && !q.Due(1320, 1240).valid);    // once
    // the button still held: its own repeat went out, nothing pressed again
    ClickHold r;
    CHECK(r.Take(1100, 1240, 3, 1, 2, 0));
    CHECK(!r.Take(1245, 1240, 4, 1, 2, 0));
    CHECK(!r.Pending() && !r.Due(1400, 1240).valid);
    // both buttons pressed in the hold: one each, the left first
    ClickHold b;
    CHECK(b.Take(1100, 1240, 3, 1, 1, 0) && b.Take(1110, 1240, 0, 2, 2, 0));
    Click l = b.Due(1300, 1240), rr = b.Due(1301, 1240);
    CHECK(l.valid && l.type == 0 && rr.valid && rr.type == 3 && !b.Due(1302, 1240).valid);
    // too late (a menu, a pause): dropped
    ClickHold s;
    CHECK(s.Take(1100, 1240, 0, 1, 1, 0));
    CHECK(!s.Due(1240 + kClickReplayMaxMs + 1, 1240).valid && !s.Pending());
    // a new hold armed before the replay: wait for its end
    ClickHold n;
    CHECK(n.Take(1100, 1240, 0, 1, 1, 0));
    CHECK(!n.Due(1300, 1500).valid && n.Pending());
    CHECK(n.Due(1500 + kClickReplayAfterMs, 1500).valid);
    // Reset lets go of all of it
    ClickHold z;
    z.Take(1100, 1240, 0, 1, 1, 0);
    z.Reset();
    CHECK(!z.Pending() && !z.Due(1300, 1240).valid);
}

int main() {
    TestWhich();
    TestGate();
    TestJoinedHero();
    TestRole();
    TestWalk();
    TestNudge();
    TestRate();
    TestFlatScope();
    TestFlatGate();
    TestFlatNudge();
    TestStaleNote();
    TestAttackLog();
    TestClickHold();
    std::printf("move_attack_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
