// melee_direct_client_test.cpp - the pure part of Direct hit for a joined player (vr/melee_direct_client.h): the role, which route a
// touch takes, how fresh the look at the skills must be, and what the units found on the UI thread allow. No game, no headset.
//   melee_direct_client_test.exe
#include "melee_direct_client.h"

#include <cstdio>
#include <initializer_list>

using namespace meleedirectclient;

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static Gate Open() {
    Gate g;
    g.wanted = true;
    g.ready = true;
    g.f4 = true;
    g.joined = true;
    g.melee = true;
    g.armed = false;
    g.leftIsSlot = true;
    g.first = false;   // a repeat on a monster: rule 3 (the strictest) unless a check says otherwise
    g.twoHandsOn = true;   // (both hands on: matters only for a two-handed weapon - the tests of that set it themselves)
    g.t.speed = 3.0f;
    g.t.force = 2.0f;
    g.t.length = 0.7f;
    g.mins = {2.5f, 1.5f, 0.4f};
    return g;
}

static Found Good() {
    Found f;
    f.hero = true;
    f.heroMode = 1;
    f.monster = true;
    f.monsterMode = 1;
    f.skills = true;
    f.left = 0;
    f.slot = 0;
    return f;
}

int main() {
    // ---- the role: only a joined TCP/IP game, never single player, a game we host, or online ----
    CHECK(!Joined(false, true, 0, 0));        // single player whose server was not learned yet: not joined
    CHECK(!Joined(true, true, 0, 0));         // single player
    CHECK(!Joined(true, true, 2, 2));         // a TCP/IP game we host (our server is here): the host keeps the key path / its own Direct hit
    CHECK(Joined(false, true, 2, 2));         // a TCP/IP game someone else hosts
    CHECK(Joined(false, true, 0, 2));         // the launch type alone says TCP/IP
    CHECK(Joined(false, true, 3, 0));         // the client's type alone says a network game
    CHECK(!Joined(false, true, 2, 1));        // online (Battle.net): never
    CHECK(!Joined(false, false, 2, 2));       // the types not read: never
    CHECK(!Settled(0, 100000));               // never joined
    CHECK(!Settled(1000, 1000 + kJoinSettleMs - 1));   // a host whose server is not learned yet in the first frames: not yet
    CHECK(Settled(1000, 1000 + kJoinSettleMs));
    CHECK(!Settled(5000, 1000));              // (a clock going back: not settled)
    // ---- the route: the gates in front ----
    CHECK(Decide(Open()) == Route::Command && WhyOf(Open()) == Why::Sent);
    {
        Gate g;   // all off
        CHECK(Decide(g) == Route::Key && WhyOf(g) == Why::Off);
        g = Open(); g.ready = false;      CHECK(Decide(g) == Route::Key && WhyOf(g) == Why::NotReady);
        g = Open(); g.f4 = false;         CHECK(Decide(g) == Route::Key && WhyOf(g) == Why::NotF4);
        g = Open(); g.joined = false;     CHECK(Decide(g) == Route::Key && WhyOf(g) == Why::NotJoined);   // host and single player unchanged
        g = Open(); g.melee = false;      CHECK(Decide(g) == Route::Key && WhyOf(g) == Why::Thrown);
        g = Open(); g.armed = true;       CHECK(Decide(g) == Route::Key && WhyOf(g) == Why::Armed);
        g = Open(); g.leftIsSlot = false; CHECK(Decide(g) == Route::Key && WhyOf(g) == Why::OtherSkill);
    }
    // ---- rule 1: the first touch on a monster is direct, whatever its speed, force or length ----
    {
        Gate g = Open();
        g.first = true;
        g.t = {};   // nothing measured
        CHECK(Decide(g) == Route::Command && WhyOf(g) == Why::SentFirst);
    }
    // ---- rule 2: the same monster sooner than the attack time is the normal attack, a good swing or not ----
    {
        Gate g = Open();
        g.tooSoon = true;
        CHECK(Decide(g) == Route::Key && WhyOf(g) == Why::TooSoon);
        g.extra = true;
        g.cleave = true;
        g.twoHanded = true;
        CHECK(WhyOf(g) == Why::TooSoon);   // the cleave too
    }
    // ---- rule 3: again on the same monster after the attack time - speed, force and length ----
    {
        Gate g = Open();
        g.t.speed = 1.0f;  CHECK(Decide(g) == Route::Key && WhyOf(g) == Why::TooSlow);   // a poke
        g = Open(); g.t.force = 1.0f;   CHECK(Decide(g) == Route::Key && WhyOf(g) == Why::TooWeak);
        g = Open(); g.t.length = 0.15f; CHECK(Decide(g) == Route::Key && WhyOf(g) == Why::TooShort);
        g = Open(); g.t.force = -1.0f;  CHECK(WhyOf(g) == Why::TooWeak);    // the force not measured: the normal attack
        g = Open(); g.t.length = -1.0f; CHECK(WhyOf(g) == Why::TooShort);
        g = Open(); g.t = {2.5f, 1.5f, 0.4f}; CHECK(Decide(g) == Route::Command);   // exactly the least: counts
        g = Open(); g.mins = {0.0f, 0.0f, 0.0f}; g.t = {0.0f, 0.0f, 0.0f}; CHECK(Decide(g) == Route::Command);   // the sliders at 0
    }
    // ---- the cleave: another monster while the attack plays - two-handed, speed AND force; no length asked ----
    {
        Gate g = Open();
        g.extra = true;
        g.twoHanded = true;
        g.cleave = false;
        CHECK(Decide(g) == Route::Key && WhyOf(g) == Why::NoCleave);     // no cleave box: the game's one attack at a time
        g.cleave = true;
        g.t.length = 0.05f;
        CHECK(Decide(g) == Route::Extra && WhyOf(g) == Why::SentExtra);
        g.first = true;
        CHECK(Decide(g) == Route::Extra);                                   // a monster not struck before: still the cleave's rules
        g.twoHanded = false;
        CHECK(Decide(g) == Route::Key && WhyOf(g) == Why::OneHanded);     // one-handed: the game's one attack at a time
        g = Open(); g.extra = true; g.cleave = true; g.twoHanded = true; g.t.speed = 2.0f;
        CHECK(Decide(g) == Route::Key && WhyOf(g) == Why::TooSlow);
        g.t.speed = 3.0f; g.t.force = 1.0f;
        CHECK(Decide(g) == Route::Key && WhyOf(g) == Why::TooWeak);
    }
    // ---- a two-handed weapon: both hands on it, or the normal attack - no direct hit, no cleave ----
    {
        Gate g = Open();
        g.twoHanded = true;
        g.twoHandsOn = false;
        CHECK(Decide(g) == Route::Key && WhyOf(g) == Why::NotTwoHands);
        g.first = true;
        CHECK(WhyOf(g) == Why::NotTwoHands);   // not even the first touch
        g.extra = true;
        g.cleave = true;
        CHECK(WhyOf(g) == Why::NotTwoHands);   // nor the cleave
        g = Open();
        g.twoHanded = true;
        g.twoHandsOn = true;
        CHECK(Decide(g) == Route::Command);
        g.twoHanded = false;
        g.twoHandsOn = false;                  // a one-handed weapon in one hand: as ever
        CHECK(Decide(g) == Route::Command);
    }
    for (int w = 0; w < static_cast<int>(Why::kCount); ++w) CHECK(WhyName(static_cast<Why>(w))[0] != 0);
    CHECK(Mins{}.speed == 2.5f && Mins{}.force == 1.5f && Mins{}.length == 0.2f);   // the defaults (the author: 20 cm of the hand's path is enough)
    // ---- PerMonster::Known: rule 1's memory ----
    {
        PerMonster m;
        CHECK(!m.Known(9));
        m.Sent(9, 100);
        CHECK(m.Known(9) && !m.Known(10));
    }
    // ---- the look at the skills: fresh for kLookFreshMs, never from the future, never "never" ----
    CHECK(!Fresh(0, 1000));
    CHECK(Fresh(1000, 1000));
    CHECK(Fresh(1000, 1000 + kLookFreshMs));
    CHECK(!Fresh(1000, 1001 + kLookFreshMs));
    CHECK(!Fresh(2000, 1000));
    CHECK(kLookMs < kLookFreshMs);   // a live look is always fresh when the next is due
    // ---- what the UI thread found ----
    CHECK(Check(Good()) == Verdict::Go);
    {
        Found f = Good();
        f.hero = false;
        CHECK(Check(f) == Verdict::NoHero);
        for (const uint32_t m : {0u, 17u, 5u, 6u}) {
            f = Good();
            f.heroMode = m;
            CHECK(Check(f) == Verdict::HeroMode);
        }
        f = Good();
        f.heroMode = 7;   // in his attack animation: the host's gate decides (queued or refused), not us
        CHECK(Check(f) == Verdict::Go);
        f = Good();
        f.monster = false;
        CHECK(Check(f) == Verdict::NoMonster);
        for (const uint32_t m : {0u, 12u}) {
            f = Good();
            f.monsterMode = m;
            CHECK(Check(f) == Verdict::MonsterDying);
        }
        f = Good();
        f.skills = false;
        CHECK(Check(f) == Verdict::NoSkills);
        f = Good();
        f.left = 126;   // Bash cast with another button since: the left skill is no longer the attack slot's
        CHECK(Check(f) == Verdict::OtherSkill);
        f = Good();
        f.left = 126;
        f.slot = 126;   // Bash sits on the attack slot itself: the press would Bash too - sent
        CHECK(Check(f) == Verdict::Go);
    }
    // ---- [gameplay] melee_direct_client_cleave: which touch is an extra hit ----
    CHECK(!InAttack(0, 0, 6, 1100, 520));         // no direct hit yet: the first monster
    CHECK(InAttack(1000, 5, 6, 1100, 520));       // another monster while the attack on monster 5 plays
    CHECK(!InAttack(1000, 5, 5, 1100, 520));      // the same monster: never an extra touch (rule 2's)
    CHECK(!InAttack(1000, 5, 6, 1520, 520));      // the attack is over
    CHECK(!InAttack(2000, 5, 6, 1000, 520));      // (a clock going back)
    // ---- the message: "strike monster M for hero H" ----
    {
        uint8_t b[16] = {};
        CHECK(BuildStrike(1, 2, b, 8) == 0);           // too small a buffer
        CHECK(BuildStrike(0x01020304u, 0xA0B0C0D0u, b, sizeof b) == kStrikeBytes);
        uint32_t h = 0, m = 0;
        CHECK(ParseStrike(b, kStrikeBytes, &h, &m) && h == 0x01020304u && m == 0xA0B0C0D0u);
        CHECK(!ParseStrike(b, kStrikeBytes - 1, &h, &m));   // a wrong size
        b[0] = 2;
        CHECK(!ParseStrike(b, kStrikeBytes, &h, &m));       // another version
        BuildStrike(7, 0, b, sizeof b);
        CHECK(!ParseStrike(b, kStrikeBytes, &h, &m));       // no monster
        CHECK(!ParseStrike(nullptr, kStrikeBytes, &h, &m));
        CHECK(kStrikeBytes <= 224);                          // D2RLoader's MaxPayloadSize
    }
    // ---- the host: a peer strikes for its own hero only ----
    {
        PeerHeroes p;
        CHECK(p.Accept(11, 3));           // the first request binds peer 11 to hero 3
        CHECK(p.Accept(11, 3));
        CHECK(!p.Accept(11, 4));          // the same peer naming another hero
        CHECK(!p.Accept(12, 3));          // another peer naming hero 3
        CHECK(p.Accept(12, 4));
        CHECK(!p.Accept(0, 5) && !p.Accept(13, 0));
        p.Forget(11);                     // peer 11 gone: hero 3 is free again
        CHECK(p.Accept(13, 3));
        PeerHeroes full;
        for (uint32_t i = 1; i <= PeerHeroes::kSlots; ++i) CHECK(full.Accept(100 + i, i));
        CHECK(!full.Accept(999, 99));     // no room: refused
    }
    // ---- the command is the game's own: left skill on a unit, a monster ----
    CHECK(kCmdLeftOnUnit == 6 && kUnitMonster == 1);
    for (int v = 0; v <= static_cast<int>(Verdict::OtherSkill); ++v) CHECK(Name(static_cast<Verdict>(v))[0] != '?');

    std::printf("melee_direct_client_test: %d checks, %d failed\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
