// vr/hit_outcome.h: [debug] hit_outcome - the roll's names, the ring between the server's thread and the log, the lines. No game, no clock.
#include <cstdio>
#include <cstring>

#include "../../vr/hit_outcome.h"

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++g_fail; }
}

int main() {
    using namespace hitoutcome;

    // -- the roll's answer, as a label would name it
    {
        Check(OutcomeOf(0x0001) == Outcome::Hit && !std::strcmp(RollText(0x0001), "hit"), "bit 0: hit");
        Check(OutcomeOf(0x0010) == Outcome::Blocked && !std::strcmp(RollText(0x0010), "blocked (shield)"), "0x10: blocked by a shield");
        Check(OutcomeOf(0x8000) == Outcome::Blocked && !std::strcmp(RollText(0x8000), "blocked (weapon)"), "0x8000: blocked by a weapon");
        Check(OutcomeOf(0x0080) == Outcome::Dodged, "0x80: dodged");
        Check(OutcomeOf(0x0100) == Outcome::Avoided, "0x100: avoided");
        Check(OutcomeOf(0) == Outcome::Zero, "0: a miss or not rolled");
        Check(OutcomeOf(0x0040) == Outcome::Other, "a bit we have no name for: other");
        Check(OutcomeOf(0x0011) == Outcome::Hit, "the hit bit wins over the rest");
    }

    // -- the ring: in order, every field back as it went in
    {
        static Ring ring;
        uint32_t lost = 0;
        Record r;
        Check(!ring.Pop(&r, &lost) && lost == 0, "empty: nothing");
        Record a;
        a.kind = Kind::Damage;
        a.direct = true;
        a.flags = 0x2001;
        a.hitFlags = 0x80000001u;
        a.monClass = 12;
        a.monId = 345;
        a.lifeBefore = 5120;
        a.lifeAfter = 0;
        a.recordLife = 6400;
        a.path = 0;
        a.attacker = 0x1234567u;
        a.ms = 123456789012ull;
        ring.Push(a);
        Record b;
        b.kind = Kind::Roll;
        b.flags = 0x0010;
        b.monClass = 7;
        b.monId = 8;
        ring.Push(b);
        Check(ring.Pop(&r, &lost) && r.kind == Kind::Damage && r.direct && r.flags == 0x2001 && r.hitFlags == 0x80000001u && r.monClass == 12 &&
                  r.monId == 345 && r.lifeBefore == 5120 && r.lifeAfter == 0 && r.recordLife == 6400 && r.path == 0 && r.ms == 123456789012ull &&
                  r.attacker == 0x1234567u,
              "the first record back whole");
        Check(ring.Pop(&r, &lost) && r.kind == Kind::Roll && !r.direct && r.flags == 0x0010 && r.monClass == 7 && r.monId == 8 && r.lifeBefore == -1 &&
                  r.recordLife == -1 && r.path == -1 && r.attacker == 0,
              "the second, with its 'not read' fields");
        Check(!ring.Pop(&r, &lost) && lost == 0, "then nothing, nothing lost");

        // more than the ring holds before the reader comes: the oldest are counted as lost, the newest told
        for (uint32_t i = 0; i < Ring::kSize + 10; ++i) {
            Record x;
            x.kind = Kind::Roll;
            x.monId = i;
            ring.Push(x);
        }
        Check(ring.Pop(&r, &lost) && lost == 10 && r.monId == 10, "overrun: 10 lost, the next one is the 11th");
        uint32_t n = 1;
        while (ring.Pop(&r, &lost)) ++n;
        Check(n == Ring::kSize && r.monId == Ring::kSize + 9 && lost == 10, "the rest in order, the last one the newest");
    }

    // -- the tally and the lines
    {
        Tally t;
        Check(!t.Any(), "an empty tally says nothing");
        Record roll;
        roll.kind = Kind::Roll;
        roll.flags = 0;
        roll.direct = true;
        t.Add(roll);
        Record dmg;
        dmg.kind = Kind::Damage;
        dmg.flags = kCrit | 1;
        dmg.lifeBefore = 512;
        dmg.lifeAfter = 0;
        t.Add(dmg);
        Check(t.Any() && t.rolls[(int)Outcome::Zero] == 1 && t.directRolls == 1 && t.damage == 1 && t.crits == 1 && t.kills == 1, "counted");

        char b[400];
        Record rl;
        rl.kind = Kind::Roll;
        rl.flags = 0x0010;
        rl.monClass = 5;
        rl.monId = 77;
        Line(rl, b, sizeof b);
        Check(!std::strcmp(b, "roll: monster class 5 id 77: flags 0x0010 = blocked (shield)"), "a roll's line");

        Record d;
        d.kind = Kind::Damage;
        d.flags = 0x2001;
        d.hitFlags = 1;
        d.monClass = 5;
        d.monId = 77;
        d.lifeBefore = 2560;
        d.lifeAfter = 1280;
        d.recordLife = 1280;
        d.path = 0;
        d.direct = true;
        Line(d, b, sizeof b);
        Check(!std::strcmp(b, "damage: monster class 5 id 77: life 10.0 -> 5.0 (-5.0), record +0x134 5.0, result flags 0x2001 CRIT, hit flags 0x00000001, "
                              "melee (flag 0), in Direct hit's call"),
              "a damage line: the crit, the path, Direct hit");
        d.lifeAfter = 0;
        d.recordLife = -1;
        d.path = 3;
        d.direct = false;
        d.flags = 1;
        Line(d, b, sizeof b);
        Check(std::strstr(b, "killed: what was left") && std::strstr(b, "record +0x134 not read") && std::strstr(b, "missile or spell (flag 3)") &&
                  !std::strstr(b, "CRIT") && !std::strstr(b, "Direct"),
              "a kill, an unread record, a missile");
        d.lifeBefore = -1;
        Line(d, b, sizeof b);
        Check(std::strstr(b, "life not read") != nullptr, "life not read");

        TallyLine(t, b, sizeof b);
        Check(std::strstr(b, "rolls 1 (hit 0, blocked 0, dodged 0, avoided 0, 0 1, other 0; 1 of them in Direct hit's call), damage 1 (crit 1, killed 1)") != nullptr,
              "the 10 s line");
    }

    if (g_fail) { std::fprintf(stderr, "%d failed\n", g_fail); return 1; }
    std::puts("hit_outcome_test: all passed");
    return 0;
}
