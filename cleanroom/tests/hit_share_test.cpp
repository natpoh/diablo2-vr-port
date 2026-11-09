// vr/hit_share.h: [multiplayer] share_hit_outcomes - the hello, the host's answer, the outcomes message, the host's peers and its batches
// per peer, and a record's way from the host's observers into the client's swing timer ring. No game, no network, no clock.
#include <cstdio>
#include <cstring>

#include "../../vr/hit_share.h"

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++g_fail; }
}

int main() {
    using namespace hitshare;
    using hitoutcome::Kind;

    // -- the hello: version, number, hero; nothing without both
    {
        uint8_t b[kMaxPayload];
        const uint32_t n = BuildHello(0xA1B2C3D4u, 77, b, sizeof b);
        Check(n == kHelloBytes, "hello: its size");
        uint32_t nonce = 0, hero = 0;
        Check(ParseHello(b, n, &nonce, &hero) && nonce == 0xA1B2C3D4u && hero == 77, "hello: back as it went");
        Check(BuildHello(0, 77, b, sizeof b) == 0 && BuildHello(5, 0, b, sizeof b) == 0, "hello: no number or no hero - none");
        Check(BuildHello(5, 77, b, kHelloBytes - 1) == 0, "hello: too small a buffer");
        Check(!ParseHello(b, n - 1, &nonce, &hero), "hello: short - refused");
        b[0] = kVersion + 1;
        Check(!ParseHello(b, n, &nonce, &hero), "hello: another version - refused");
    }

    // -- the host's answer: who asked, the host's number, its box
    {
        uint8_t b[kMaxPayload];
        uint32_t asker = 0, host = 0;
        bool sharing = false;
        uint32_t n = BuildState(11, 22, true, b, sizeof b);
        Check(n == kStateBytes && ParseState(b, n, &asker, &host, &sharing) && asker == 11 && host == 22 && sharing, "state: ticked");
        n = BuildState(11, 22, false, b, sizeof b);
        Check(ParseState(b, n, &asker, &host, &sharing) && !sharing, "state: not ticked");
        Check(BuildState(11, 0, true, b, sizeof b) == 0, "state: a host with no number sends none");
    }

    // -- a record from the observers into the wire and back into the client's ring
    {
        hitoutcome::Record d;
        d.kind = Kind::Damage;
        d.flags = hitoutcome::kCrit;
        d.attacker = 9;
        d.lifeBefore = 5120;
        d.lifeAfter = 3840;
        d.recordLife = 999;
        d.ms = 123456;
        Rec x;
        Check(RecOf(d, &x) && x.kind == Kind::Damage && x.took == 1280 && x.attacker == 9 && (x.flags & hitoutcome::kCrit), "damage: life before - after wins");
        d.lifeBefore = -1;
        Check(RecOf(d, &x) && x.took == 999, "damage: the record's own number when life was not read");
        hitoutcome::Record roll;
        roll.kind = Kind::Roll;
        roll.flags = hitoutcome::kBlockShield;
        roll.attacker = 9;
        Check(RecOf(roll, &x) && x.kind == Kind::Roll && x.flags == hitoutcome::kBlockShield && x.took == 0, "roll: its flags");
        roll.attacker = 0;
        Check(!RecOf(roll, &x), "no attacker: not sent");
        hitoutcome::Record none;
        Check(!RecOf(none, &x), "no kind: not sent");

        Rec back{Kind::Damage, hitoutcome::kCrit, 1280, 9};
        const hitoutcome::Record r = RecordOf(back, 5000);
        Check(r.kind == Kind::Damage && r.ms == 5000 && r.attacker == 9 && r.recordLife == 1280 && r.lifeBefore < 0 && r.lifeAfter < 0,
              "client: stamped with its own clock, the life as the record's number");
        Check(TookOf(r) == 1280, "client: the timer counts the same life");
        back.took = -5;
        Check(RecordOf(back, 1).recordLife == 0, "client: a negative life is none");
        const hitoutcome::Record rr = RecordOf(Rec{Kind::Roll, 0, 0, 9}, 1);
        Check(rr.kind == Kind::Roll && rr.flags == 0 && rr.recordLife == -1, "client: a roll of 0 (a miss) stays a roll");
    }

    // -- the outcomes message: up to kRecsPerMsg, every field back
    {
        Rec in[kRecsPerMsg];
        for (int i = 0; i < kRecsPerMsg; ++i) in[i] = Rec{i % 2 ? Kind::Damage : Kind::Roll, static_cast<uint16_t>(0x2000 + i), 256 * i, 1000u + i};
        uint8_t b[kMaxPayload];
        const uint32_t n = BuildOutcomes(0xBEEF, in, kRecsPerMsg, b, sizeof b);
        Check(n == kOutHead + kRecsPerMsg * kRecBytes && n <= kMaxPayload, "outcomes: a full message fits the payload");
        Rec out[kRecsPerMsg];
        int got = 0;
        uint32_t host = 0;
        Check(ParseOutcomes(b, n, &host, out, kRecsPerMsg, &got) && host == 0xBEEF && got == kRecsPerMsg, "outcomes: all back");
        bool same = true;
        for (int i = 0; i < got; ++i)
            same = same && out[i].kind == in[i].kind && out[i].flags == in[i].flags && out[i].took == in[i].took && out[i].attacker == in[i].attacker;
        Check(same, "outcomes: every field as it went");
        Check(BuildOutcomes(1, in, kRecsPerMsg + 1, b, sizeof b) == 0 && BuildOutcomes(1, in, 0, b, sizeof b) == 0, "outcomes: 0 or too many - none");
        Check(BuildOutcomes(1, in, 2, b, kOutHead + kRecBytes) == 0, "outcomes: too small a buffer");
        Check(!ParseOutcomes(b, n - 1, &host, out, kRecsPerMsg, &got), "outcomes: a cut message - refused");
        const uint32_t n2 = BuildOutcomes(7, in, 2, b, sizeof b);
        b[kOutHead] = 9;   // the first record: a kind this client does not know
        Check(ParseOutcomes(b, n2, &host, out, kRecsPerMsg, &got) && got == 1 && out[0].attacker == in[1].attacker, "outcomes: an unknown kind skipped");
        b[5] = 3;   // the count says more than the size holds
        Check(!ParseOutcomes(b, n2, &host, out, kRecsPerMsg, &got), "outcomes: count and size disagree - refused");
    }

    // -- the host's peers: own process left out, a hero per peer, gone when silent
    {
        Peers p;
        const uint32_t own = 500;
        Check(!p.Hello(1, own, 10, own, 1000), "peers: our own process (its number is ours) - not a peer");
        Check(p.Hello(2, 600, 20, own, 1000) && p.HandleOf(20, 1000) == 2, "peers: a joined hero");
        Check(p.Hello(2, 600, 21, own, 1500) && p.HandleOf(21, 1500) == 2 && p.HandleOf(20, 1500) == 0, "peers: a new game's hero takes its place");
        Check(p.Count(1500) == 1, "peers: one");
        Check(p.HandleOf(21, 1500 + kFreshMs + 1) == 0, "peers: silent too long - nothing more");
        p.Drop(2);
        Check(p.HandleOf(21, 1600) == 0, "peers: dropped on a disconnect");
        for (int i = 0; i < kMaxPeers; ++i) Check(p.Hello(100 + i, 700 + i, 30 + i, own, 2000), "peers: eight");
        Check(!p.Hello(200, 900, 99, own, 2000), "peers: a ninth fresh one - no room");
        Check(p.Hello(200, 900, 99, own, 2000 + kFreshMs + 1) && p.HandleOf(99, 2000 + kFreshMs + 1) == 200, "peers: a stale place is taken");
        Check(!p.Hello(0, 1, 1, own, 1) && !p.Hello(5, 0, 1, own, 1) && !p.Hello(5, 1, 0, own, 1), "peers: a hello without handle, number or hero");
    }

    // -- the batches: each peer gets its own hero's records only
    {
        Peers p;
        p.Hello(2, 600, 20, 1, 1000);
        p.Hello(3, 601, 30, 1, 1000);
        static Batches bt;
        bt.Clear();
        Check(bt.Add(p, Rec{Kind::Roll, 0, 0, 20}, 1000), "batch: hero 20's roll");
        Check(bt.Add(p, Rec{Kind::Damage, 0, 512, 30}, 1000), "batch: hero 30's blow");
        Check(bt.Add(p, Rec{Kind::Damage, 0, 256, 20}, 1000), "batch: hero 20's blow");
        Check(!bt.Add(p, Rec{Kind::Damage, 0, 256, 40}, 1000), "batch: the host's own hero (no peer) - dropped");
        int for2 = 0, for3 = 0, rightHero = 1;
        for (int i = 0; i < kMaxPeers; ++i) {
            const Batches::Batch& b = bt.All()[i];
            for (int k = 0; k < b.n; ++k) {
                if (b.handle == 2) { ++for2; rightHero &= b.r[k].attacker == 20; }
                if (b.handle == 3) { ++for3; rightHero &= b.r[k].attacker == 30; }
            }
        }
        Check(for2 == 2 && for3 == 1 && rightHero, "batch: two for peer 2, one for peer 3, each its own hero's");
        for (int i = 0; i < Batches::kMaxPerPeer + 5; ++i) bt.Add(p, Rec{Kind::Roll, 0, 0, 30}, 1000);
        uint32_t over = 0;
        for (int i = 0; i < kMaxPeers; ++i) if (bt.All()[i].handle == 3) over = bt.All()[i].over;
        Check(over == 6, "batch: past the cap counted, not kept");
        bt.Clear();
        Check(bt.All()[0].handle == 0 && bt.All()[0].n == 0, "batch: cleared");
    }

    if (g_fail) { std::fprintf(stderr, "%d failed\n", g_fail); return 1; }
    std::printf("hit_share_test: all passed\n");
    return 0;
}
