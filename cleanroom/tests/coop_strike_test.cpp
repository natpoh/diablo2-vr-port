// coop_strike_test.cpp - the pure part of [gameplay] melee_direct_client_host (vr/coop_strike.h): the messages packed and read back, the
// rewind, the host's history of places, the checks with their tolerances, the pace, the peers and the client's pending touches.
// No game, no network.
//   coop_strike_test.exe
#include "coop_strike.h"

#include <cmath>
#include <cstdio>

using namespace coopstrike;

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)
static bool Near(float a, float b, float eps = 0.01f) { return std::fabs(a - b) <= eps; }

static Swing ASwing() {
    Swing s;
    s.seq = 513;
    s.hand = 1;
    s.extra = true;
    s.twoHanded = true;
    s.twoHandsOn = false;
    s.hero = 0x01020304u;
    s.monster = 0xA0B0C0D0u;
    s.ageMs = 37;
    s.speed = 4.25f;
    s.force = -1.0f;
    s.length = 0.734f;
    s.heroX = 5123.5f;
    s.heroY = 4777.25f;
    s.monX = 5125.0f;
    s.monY = 4778.0f;
    s.skill = -1;
    return s;
}

// A hero (or monster) walking along x at `speed` cells/s, sampled every 25 ms from t0.
static void Walk(Track& tr, uint64_t t0, int samples, float x0, float y0, float speed, bool alive = true) {
    for (int i = 0; i < samples; ++i) {
        const uint64_t t = t0 + static_cast<uint64_t>(i) * 25;
        tr.Push({t, x0 + speed * static_cast<float>(i) * 0.025f, y0, alive});
    }
}

static Facts GoodFacts() {
    Facts f;
    f.off = false;
    f.peerHero = true;
    f.heroTrack = true;
    f.heroDist = 0.5f;
    f.heroAt = {1000, 100.0f, 100.0f, true};
    f.heroOk = true;
    f.monsterTrack = true;
    f.monAt = {1000, 103.0f, 100.0f, true};
    f.monsterNow = true;
    f.hostile = true;
    f.wall = false;
    return f;
}

int main() {
    // ---- the messages: packed and read back ----
    {
        const Swing s = ASwing();
        uint8_t b[64] = {};
        CHECK(BuildSwing(s, b, sizeof b) == kSwingBytes);
        CHECK(kSwingBytes <= 224);   // (D2RLoader's payload)
        Swing r;
        CHECK(ParseSwing(b, kSwingBytes, &r));
        CHECK(r.seq == 513 && r.hand == 1 && r.extra && r.twoHanded && !r.twoHandsOn);
        CHECK(r.hero == 0x01020304u && r.monster == 0xA0B0C0D0u && r.ageMs == 37);
        CHECK(Near(r.speed, 4.25f) && r.force == -1.0f && Near(r.length, 0.734f, 0.001f));
        CHECK(Near(r.heroX, 5123.5f) && Near(r.heroY, 4777.25f) && Near(r.monX, 5125.0f) && Near(r.monY, 4778.0f));
        CHECK(r.skill == -1);
        CHECK(!ParseSwing(b, kSwingBytes - 1, &r));          // a wrong size
        uint8_t bad[64];
        for (uint32_t i = 0; i < kSwingBytes; ++i) bad[i] = b[i];
        bad[0] = 9;
        CHECK(!ParseSwing(bad, kSwingBytes, &r));            // another version
        bad[0] = kVersion; bad[3] = 2;
        CHECK(!ParseSwing(bad, kSwingBytes, &r));            // a hand that is not one
        Swing noHero = s;
        noHero.hero = 0;
        CHECK(BuildSwing(noHero, b, sizeof b) == 0);
        Swing noPlace = s;
        noPlace.heroX = -1.0f;                               // the hero's place is checked on the host: it must come
        CHECK(BuildSwing(noPlace, b, sizeof b) == kSwingBytes && !ParseSwing(b, kSwingBytes, &r));
        CHECK(BuildSwing(s, b, kSwingBytes - 1) == 0);       // too small a buffer
        // measures beyond 16 bits clamp below "unknown"; NaN is unknown
        CHECK(Fix16(1000.0f, 100.0f) == kUnknown16 - 1);
        CHECK(Fix16(std::nanf(""), 100.0f) == kUnknown16 && Unfix16(kUnknown16, 100.0f) == -1.0f);
        CHECK(FixPos(-1.0f) == 0xFFFFFFFFu && UnfixPos(0xFFFFFFFFu) == -1.0f);
    }
    {
        Verdict v{65535, 1, Why::HeroMoved, 345};
        uint8_t b[16];
        CHECK(BuildVerdict(v, b, sizeof b) == kVerdictBytes);
        Verdict r;
        CHECK(ParseVerdict(b, kVerdictBytes, &r) && r.seq == 65535 && r.hand == 1 && r.why == Why::HeroMoved && r.rewindMs == 345);
        b[4] = static_cast<uint8_t>(Why::kCount);
        CHECK(!ParseVerdict(b, kVerdictBytes, &r));
        uint32_t stamp = 0, hero = 0;
        CHECK(BuildPing(0xDEADBEEFu, b, sizeof b) == kPingBytes && ParsePing(b, kPingBytes, &stamp) && stamp == 0xDEADBEEFu);
        CHECK(BuildPong(0x12345678u, 77, b, sizeof b) == kPongBytes && ParsePong(b, kPongBytes, &stamp, &hero) && stamp == 0x12345678u && hero == 77);
        CHECK(BuildPong(1, 0, b, sizeof b) == 0);
        bool on = false;
        CHECK(BuildSwingRule(true, b, sizeof b) == kSwingRuleBytes && ParseSwingRule(b, kSwingRuleBytes, &on) && on);
        CHECK(BuildSwingRule(false, b, sizeof b) == kSwingRuleBytes && ParseSwingRule(b, kSwingRuleBytes, &on) && !on);
        b[1] = 2;
        CHECK(!ParseSwingRule(b, kSwingRuleBytes, &on));
        // the round trip over the host's 32-bit clock, its wrap too
        uint32_t rtt = 0;
        CHECK(RttOf(1100, 1000, &rtt) && rtt == 100);
        CHECK(RttOf(50, 0xFFFFFFF0u, &rtt) && rtt == 66);
        CHECK(!RttOf(1000, 1100, &rtt));                     // a stamp from the future: no
        CHECK(!RttOf(10000, 1000, &rtt));                    // a lost pong answered late: no
    }
    // ---- the round trip, smoothed ----
    {
        Rtt r;
        CHECK(!r.Known() && r.Ms() == Rtt::kDefaultRttMs);
        r.Add(80);
        CHECK(!r.Known());
        r.Add(80);
        CHECK(r.Known() && r.Ms() == 80);
        r.Add(400);                                          // one late answer moves it a little
        CHECK(r.Ms() > 80 && r.Ms() < 130);
        for (int i = 0; i < 30; ++i) r.Add(60);
        CHECK(r.Ms() >= 60 && r.Ms() <= 63);
        r.Add(5000);                                         // not a trip
        CHECK(r.Ms() <= 63);
    }
    // ---- the rewind: the monster at now - age - rtt, the swing at now - age - rtt/2, capped ----
    {
        const Times t = RewindOf(10000, 120, 40);
        CHECK(!t.tooOld && t.rewindMs == 160 && t.monsterAt == 9840 && t.swingAt == 9900);
        CHECK(t.heroFrom == 9840 - kHeroSlackMs && t.heroTo == 10000);
        const Times old = RewindOf(10000, 500, 200);
        CHECK(old.tooOld && old.rewindMs == kMaxRewindMs && old.monsterAt == 10000 - kMaxRewindMs);
        const Times early = RewindOf(50, 100, 10);           // (a clock near its start: no wrap below zero)
        CHECK(early.monsterAt == 0 && early.heroFrom == 0);
    }
    // ---- the history: places between samples, after the newest, before the oldest ----
    {
        Track tr;
        tr.Reset(5);
        Walk(tr, 1000, 20, 100.0f, 50.0f, 8.0f);             // 8 cells/s: 0.2 cell a sample, 1000..1475 ms
        Sample s;
        CHECK(tr.At(1000, &s) && Near(s.x, 100.0f));
        CHECK(tr.At(1012, &s) && Near(s.x, 100.096f));       // between two: the line between them
        CHECK(tr.At(1475, &s) && Near(s.x, 103.8f));
        CHECK(tr.At(1475 + Track::kStaleMs, &s) && Near(s.x, 103.8f));   // a little after the newest: the newest
        CHECK(!tr.At(1476 + Track::kStaleMs, &s));           // long after: nothing (the unit is gone)
        CHECK(!tr.At(999, &s));                              // before the oldest: nothing
        tr.Push({1200, 0.0f, 0.0f, true});                   // (time going back is not taken)
        CHECK(tr.At(1200, &s) && Near(s.x, 101.6f));
        // the ring keeps the newest kN
        Track ring;
        ring.Reset(6);
        Walk(ring, 0, Track::kN + 10, 0.0f, 0.0f, 40.0f);    // 1 cell a sample
        CHECK(ring.Count() == Track::kN);
        CHECK(!ring.At(9 * 25, &s) && ring.At(10 * 25, &s) && Near(s.x, 10.0f));
        // the nearest place in a window
        float d = 0.0f;
        CHECK(tr.Nearest(102.0f, 50.0f, 1000, 1475, &d, &s) && Near(d, 0.0f) && s.t == 1250);
        CHECK(tr.Nearest(102.0f, 50.0f, 1000, 1100, &d, &s) && Near(d, 1.2f) && s.t == 1100);
        CHECK(!tr.Nearest(102.0f, 50.0f, 0, 900, &d, &s));
        // a death: alive until its sample
        Track dying;
        dying.Reset(7);
        dying.Push({1000, 1.0f, 1.0f, true});
        dying.Push({1025, 1.0f, 1.0f, false});
        CHECK(dying.At(1010, &s) && s.alive);
        CHECK(dying.At(1030, &s) && !s.alive);
    }
    {
        static History h;   // (big: not on the stack)
        h.Push(10, {100, 1.0f, 1.0f, true});
        h.Push(11, {100, 2.0f, 2.0f, true});
        CHECK(h.Find(10) && h.Find(11) && !h.Find(12) && !h.Find(0));
        CHECK(h.Get(12, false) == nullptr);
        for (uint32_t id = 100; id < 100 + History::kTracks; ++id) h.Push(id, {200 + id, 0.0f, 0.0f, true});   // full: the stalest goes
        CHECK(!h.Find(10) && h.Find(100 + History::kTracks - 1));
        h.Prune(200 + 100 + History::kTracks + 10000, 5000);
        CHECK(!h.Find(100));
    }
    // ---- the checks, each tolerance on both sides of its line ----
    {
        const Swing s = ASwing();
        const Times t = RewindOf(10000, 100, 30);
        const Limits l;
        float reach = 0.0f;
        CHECK(Judge(s, t, GoodFacts(), l, &reach) == Why::Struck && Near(reach, 3.0f));   // (the client's own picture: 1.7 cells apart - in; the host's places decide the rest)
        Swing near = s;
        near.heroX = 100.0f; near.heroY = 100.0f; near.monX = 103.0f; near.monY = 100.0f;
        CHECK(Judge(near, t, GoodFacts(), l, &reach) == Why::Struck && Near(reach, 3.0f));
        Facts f = GoodFacts();
        f.off = true;
        CHECK(Judge(near, t, f, l) == Why::Off);
        f = GoodFacts(); f.peerHero = false;
        CHECK(Judge(near, t, f, l) == Why::PeerHero);
        CHECK(Judge(near, RewindOf(10000, 500, 200), GoodFacts(), l) == Why::TooOld);
        f = GoodFacts(); f.heroTrack = false;
        CHECK(Judge(near, t, f, l) == Why::NoHero);
        f = GoodFacts(); f.heroOk = false;
        CHECK(Judge(near, t, f, l) == Why::HeroMode);
        f = GoodFacts(); f.heroDist = l.heroTolCells;          // on the line: still him
        CHECK(Judge(near, t, f, l) == Why::Struck);
        f.heroDist = l.heroTolCells + 0.01f;                   // past it: not where he says
        CHECK(Judge(near, t, f, l) == Why::HeroMoved);
        f = GoodFacts(); f.monsterTrack = false;
        CHECK(Judge(near, t, f, l) == Why::NoMonster);
        f = GoodFacts(); f.monsterNow = false;
        CHECK(Judge(near, t, f, l) == Why::NoMonster);
        f = GoodFacts(); f.monAt.alive = false;
        CHECK(Judge(near, t, f, l) == Why::MonsterDead);
        f = GoodFacts(); f.hostile = false;
        CHECK(Judge(near, t, f, l) == Why::NotHostile);
        f = GoodFacts(); f.wall = true;
        CHECK(Judge(near, t, f, l) == Why::Wall);
        f = GoodFacts(); f.monAt.x = f.heroAt.x + l.reachCells + l.tolCells;   // on the reach's line
        Swing far = near; far.monX = 100.0f + l.reachCells + l.tolCells;
        CHECK(Judge(far, t, f, l) == Why::Struck);
        f.monAt.x += 0.05f;                                    // past it
        CHECK(Judge(far, t, f, l) == Why::Far);
        f = GoodFacts();
        Swing liar = near; liar.monX = 100.0f + l.reachCells + l.tolCells + 1.0f;   // his own picture says the monster was out of reach
        CHECK(Judge(liar, t, f, l) == Why::Far);
        Swing noMon = near; noMon.monX = -1.0f;                // no monster place from the client: the host's alone decides
        CHECK(Judge(noMon, t, f, l) == Why::Struck);
    }
    // ---- the rewind against a walking monster: struck where it was, refused where it is now ----
    {
        Track mon;
        mon.Reset(50);
        Walk(mon, 9000, 41, 100.0f, 100.0f, 10.0f);          // 9000..10000 ms, 100 -> 110 cells along x
        Track hero;
        hero.Reset(8);
        Walk(hero, 9000, 41, 95.0f, 100.0f, 0.0f);           // the hero stands at 95
        const Times t = RewindOf(10000, 300, 100);           // the client saw the monster 400 ms ago: at 106
        Facts f = GoodFacts();
        CHECK(mon.At(t.monsterAt, &f.monAt) && Near(f.monAt.x, 106.0f));
        CHECK(hero.Nearest(95.0f, 100.0f, t.heroFrom, t.heroTo, &f.heroDist, &f.heroAt));
        Swing s = ASwing();
        s.heroX = 95.0f; s.heroY = 100.0f; s.monX = 101.0f; s.monY = 100.0f;
        Limits l;
        l.reachCells = 9.0f; l.tolCells = 2.0f;              // 11 cells: 106 - 95 is in, 110 - 95 is not
        CHECK(Judge(s, t, f, l) == Why::Struck);
        Facts now = f;
        CHECK(mon.At(10000, &now.monAt) && Near(now.monAt.x, 110.0f));
        l.reachCells = 9.0f; l.tolCells = 0.5f;
        CHECK(Judge(s, RewindOf(10000, 0, 0), now, l) == Why::Far);   // judged now, it would have been a miss
        // the hero far from where he says, over the whole window
        CHECK(hero.Nearest(99.5f, 100.0f, t.heroFrom, t.heroTo, &f.heroDist, &f.heroAt) && Near(f.heroDist, 4.5f));
        CHECK(Judge(s, t, f, Limits{}) == Why::HeroMoved);
    }
    // ---- the pace: the attack time, the slack, the cleave's further monsters ----
    {
        Pace p;
        const uint64_t atk = 500;
        CHECK(p.Check(1000, 1, false, atk) == Why::Struck);
        p.Note(1000, 1, false, atk);
        CHECK(p.Check(1100, 2, false, atk) == Why::TooFast);                 // another monster, no cleave: too soon
        CHECK(p.Check(1000 + 424, 2, false, atk) == Why::TooFast);           // under the slack (0.85 x 500 = 425)
        CHECK(p.Check(1000 + 425, 2, false, atk) == Why::Struck);            // on it: in (the swings' times are guesses)
        CHECK(p.Check(1100, 1, true, atk) == Why::Again);                    // the same monster in the same attack: never
        CHECK(p.Check(1100, 2, true, atk) == Why::Struck);                   // a cleave's further monster
        p.Note(1100, 2, true, atk);
        CHECK(p.AttackAt() == 1000);                                         // (a further monster does not start a new attack)
        CHECK(p.Check(1150, 2, true, atk) == Why::Again);
        for (uint32_t m = 3; m < 3 + kMaxExtras - 1; ++m) { CHECK(p.Check(1200, m, true, atk) == Why::Struck); p.Note(1200, m, true, atk); }
        CHECK(p.Check(1250, 99, true, atk) == Why::TooFast);                 // past kMaxExtras: the rate again
        CHECK(p.Check(1500, 1, false, atk) == Why::Struck);                  // the next attack: the first monster is new again
        p.Note(1500, 1, false, atk);
        CHECK(p.AttackAt() == 1500);
        CHECK(p.Check(1600, 2, true, atk) == Why::Struck);                   // its own cleave
        CHECK(p.Check(900, 7, false, atk) == Why::Struck);                   // (a swing time before the attack - a reordered message: not refused for the pace)
    }
    // ---- the peers: bound once, never another's hero or the host's own ----
    {
        Peers ps;
        CHECK(ps.Bind(11, 8, 1) != nullptr);
        CHECK(ps.Bind(11, 8, 1) == ps.Of(11));
        CHECK(ps.Bind(11, 9, 1) == nullptr);                 // the same peer, another hero
        CHECK(ps.Bind(12, 8, 1) == nullptr);                 // another peer, the bound hero
        CHECK(ps.Bind(12, 1, 1) == nullptr);                 // the host's own hero
        CHECK(ps.Bind(12, 9, 1) != nullptr && ps.Count() == 2);
        ps.Forget(11);
        CHECK(ps.Of(11) == nullptr && ps.Count() == 1 && ps.Bind(13, 8, 1) != nullptr);
        CHECK(ps.Bind(0, 5, 1) == nullptr && ps.Bind(14, 0, 1) == nullptr);
        Peers full;
        for (uint64_t i = 1; i <= Peers::kSlots; ++i) CHECK(full.Bind(i, static_cast<uint32_t>(100 + i), 1) != nullptr);
        CHECK(full.Bind(99, 999, 1) == nullptr);
    }
    // ---- the client's pending touches ----
    {
        Pending pd;
        pd.Add(1, 50, 0, 1000);
        pd.Add(2, 51, 1, 1010);
        Pending::Item it;
        CHECK(pd.Take(2, 1100, &it) && it.monster == 51 && it.hand == 1);
        CHECK(!pd.Take(2, 1100, &it));                       // answered once
        CHECK(!pd.Take(1, 1000 + kAnswerMs + 1, &it));       // too late: nothing pressed for it
        CHECK(!pd.Take(1, 1100, &it));                       // (and gone)
        for (uint16_t s = 0; s < Pending::kSlots + 4; ++s) pd.Add(s, 60u + s, 0, 2000u + s);
        CHECK(!pd.Take(0, 2100, &it) && pd.Take(Pending::kSlots + 3, 2100, &it));   // full: the oldest went
    }
    std::printf("coop_strike_test: %d checks, %d failed\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
