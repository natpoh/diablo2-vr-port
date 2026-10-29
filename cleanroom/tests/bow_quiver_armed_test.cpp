// [bow] quiver, armed (vr/bow_quiver.h Machine / DecideShot / Cleanup): the quiver ARMS a bow skill as its own state, as [gameplay] melee_skill_armed does - nothing is
// written into the game's slots for a choice; each release with a bow or a crossbow in hand is the armed skill, the plain arrow when the hero cannot pay it; with no bow
// the armed skill does nothing; the old quiver's arrow skill on the A slot is given back as the plain attack (the author, 2026-10-20: the Amazon's touches pressed A, the
// game read it as Multiple Shot with a sword in hand and did nothing - "presses=9 hits=0").
//
//   bow_quiver_armed_test        exit code 0 = all passed

#include "bow_quiver.h"
#include "melee_arm.h"   // InFlight: the release's answer (the wire's)

#include <cstdio>
#include <cstring>

using namespace bowquiver;

namespace {
int g_failed = 0, g_checks = 0;
void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_failed; std::printf("FAIL: %s\n", what); }
}

wheel::Snapshot g_snap;   // (large: not on the stack)

void Learn(int32_t id, int32_t level, int32_t owner = -1) {
    wheel::Skill& k = g_snap.catalog[g_snap.count++];
    k.id = id;
    k.owner = owner;
    k.level = level;
    k.known = k.available = true;
}
void Hero() {
    g_snap = wheel::Snapshot{};
    g_snap.session = 7;
    g_snap.at = 1000;
    g_snap.bank = 0;
    g_snap.ready = g_snap.editable = true;
    for (wheel::Skill& s : g_snap.slot) { s.id = -1; s.known = true; }
    g_snap.slot[kAttackSlot].id = 0;
    Learn(0, 1);    // Attack
    Learn(6, 5);    // Magic Arrow
    Learn(12, 3);   // Multiple Shot
    Learn(10, 4);   // Jab (no arrow)
}
In At(double t, bool drawing = false) {
    In in;
    in.t = t;
    in.allowed = true;
    in.drawing = drawing;
    in.snap = &g_snap;
    return in;
}
}  // namespace

int main() {
    Hero();
    const wheel::Snapshot before = g_snap;   // a choice never writes into the game's slots: the snapshot is the game's, read only - and nothing in Out asks for a write

    // ---- the list: the plain shot first, then the hero's arrows in the table's order
    {
        Entry l[kMaxList];
        const size_t n = BuildList(g_snap.catalog, g_snap.count, l, kMaxList);
        Check(n == 3 && l[0] == kPlain && l[1].id == 6 && l[2].id == 12, "the list: the plain shot, Magic Arrow, Multiple Shot (Jab is no arrow)");
    }

    // ---- arm, re-arm, disarm (the orbs)
    Machine m;
    double t = 1.0;
    Check(m.Armed().id < 0 && m.Current() == kPlain, "nothing armed at the start: the plain shot's orb stands out");
    Out o = m.Pick(At(t), {12, -1});
    Check(o.feedback == Feedback::Armed && m.Armed().id == 12, "an orb arms its skill");
    Check(m.Current().id == 12, "... and its orb stands out");
    t += 0.5;
    o = m.Pick(At(t), {6, -1});
    Check(o.feedback == Feedback::Switched && m.Armed().id == 6, "another orb arms that one instead");
    t += 0.05;
    o = m.Pick(At(t), {12, -1});
    Check(o.feedback == Feedback::None && m.Armed().id == 6, "a second pick within the gap is not taken");
    t += 0.5;
    o = m.Pick(At(t), {6, -1});
    Check(o.feedback == Feedback::Disarmed && m.Armed().id < 0, "the same orb again disarms");
    t += 0.5;
    o = m.Pick(At(t), kPlain);
    Check(o.feedback == Feedback::None && m.Armed().id < 0, "the plain shot's orb with nothing armed: nothing");
    t += 0.5;
    m.Pick(At(t), {12, -1});
    t += 0.5;
    o = m.Pick(At(t), kPlain);
    Check(o.feedback == Feedback::Disarmed && m.Armed().id < 0, "the plain shot's orb disarms");
    t += 0.5;
    o = m.Pick(At(t), {16, -1});
    Check(o.feedback == Feedback::Refused && m.Armed().id < 0, "a skill the hero does not have is refused");
    Check(std::memcmp(&before, &g_snap, sizeof g_snap) == 0, "no choice touched the game's slots");

    // ---- the buttons while drawn: the cycle, onto the plain shot = disarmed
    {
        Machine b;
        const bool set[2] = {true, true}, up[2] = {false, false}, next[2] = {true, false}, prev[2] = {false, true};
        double u = 10.0;
        Check(b.Step(At(u, false), set, next).feedback == Feedback::None && b.Armed().id < 0, "a button with the string not drawn: nothing");
        b.Step(At(u += 0.2, true), set, up);
        Out s = b.Step(At(u += 0.2, true), set, next);
        Check(s.take && s.feedback == Feedback::Armed && b.Armed().id == 6, "next while drawn: the first arrow armed");
        b.Step(At(u += 0.2, true), set, up);
        s = b.Step(At(u += 0.2, true), set, next);
        Check(s.feedback == Feedback::Switched && b.Armed().id == 12, "next again: the second");
        b.Step(At(u += 0.2, true), set, up);
        s = b.Step(At(u += 0.2, true), set, next);
        Check(s.feedback == Feedback::Disarmed && b.Armed().id < 0, "next past the last: onto the plain shot, disarmed");
        b.Step(At(u += 0.2, true), set, up);
        s = b.Step(At(u += 0.2, true), set, prev);
        Check(s.feedback == Feedback::Armed && b.Armed().id == 12, "prev from the plain shot: the last arrow");
        b.Step(At(u += 0.2, true), set, prev);
        Check(b.Armed().id == 12, "a held button is one press");
    }

    // ---- the shot
    {
        // Multiple Shot level 3: (4 + 1 * 2) << 8 = 6 points
        Shot s = DecideShot(12, true, 3, 20.0f, true);
        Check(s.skill == 12 && s.why == ShotWhy::Skill && s.canPay && s.cost > 5.99f && s.cost < 6.01f, "a release with a bow, the mana there: the armed skill");
        s = DecideShot(12, true, 3, 5.0f, true);
        Check(s.skill == 0 && s.why == ShotWhy::NoMana && !s.canPay, "a release the hero cannot pay: the plain arrow (quiver_plain_no_mana), the icon faint");
        s = DecideShot(12, true, 3, 5.0f, false);
        Check(s.skill == 12 && s.why == ShotWhy::ShortGameDecides && !s.canPay, "... quiver_plain_no_mana off: the skill, the game refuses it");
        s = DecideShot(12, true, -1, -1.0f, true);
        Check(s.skill == 12 && s.why == ShotWhy::ManaUnknown && s.levelGuessed, "the mana and the level not read: the skill, the game decides");
        s = DecideShot(12, true, 0, 50.0f, true);
        Check(s.skill == 0 && s.why == ShotWhy::NotLearned, "no level in it (an item gone): the plain arrow");
        s = DecideShot(-1, true, 3, 50.0f, true);
        Check(s.skill == 0 && s.why == ShotWhy::NotArmed, "nothing armed: the plain shot");
        s = DecideShot(10, true, 3, 50.0f, true);
        Check(s.skill == 0, "a skill that is no arrow (Jab) is never the shot");
        Check(LevelOf(&g_snap, {12, -1}) == 3 && LevelOf(&g_snap, {12, 99}) == 3 && LevelOf(&g_snap, {16, -1}) == 0 && LevelOf(nullptr, {12, -1}) == -1,
              "the level from the catalog: the entry's, any owner's, none, not read");
    }

    // ---- melee with an armed bow skill, and the weapon swap
    {
        Machine w;
        w.Pick(At(100.0), {12, -1});
        Shot s = DecideShot(w.Armed().id, false, 3, 50.0f, true);
        Check(s.skill == 0 && s.why == ShotWhy::NoBow, "a sword in hand (no bow, no crossbow): the armed bow skill does nothing - the plain attack");
        w.Forget();   // (the frames with no bow forget the controls' edges only)
        Check(w.Armed().id == 12, "the swap keeps it armed: it waits for the bow");
        s = DecideShot(w.Armed().id, true, 3, 50.0f, true);
        Check(s.skill == 12, "the bow (or a crossbow) back in hand: the release is the armed skill again");
        w.Clear();
        Check(w.Armed().id < 0, "another hero, the tick off: nothing armed");
    }

    // ---- the in-flight answer: one release, one cast
    {
        meleearm::InFlight f;
        f.Set(12, 5000);
        Check(f.Answer(5000) == 12 && f.Answer(5300) == 12, "the release's two reads: the armed skill");
        Check(f.Answer(5000 + meleearm::kWindowMs + 1) == 0, "after the window: as the game has it");
        f.Set(0, 6000);
        Check(f.Answer(6000) == 0, "a plain release: as the game has it");
    }

    // ---- the cleanup of the old quiver's arrow on A
    {
        Hero();
        g_snap.slot[kAttackSlot].id = 12;   // the old quiver's Multiple Shot, kept in the save
        Cleanup c;
        CleanIn ci;
        ci.on = true;
        ci.hero = 0x1234;
        ci.snap = &g_snap;
        ci.fresh = true;
        ci.ranged = true;   // the bow in hand at the load
        ci.t = 1.0;
        CleanOut o2 = c.Step(ci);
        Check(o2.submit && o2.req.op == wheel::Op::Assign && o2.req.slot == kAttackSlot && o2.req.skill == 0 && o2.req.owner == -1 && o2.req.prevSkill == 12 && o2.req.bank == 0,
              "the load: Multiple Shot on A is given back as the plain attack, even with the bow in hand");
        ci.t = 1.2;
        Check(!c.Step(ci).submit && c.Wants(), "one request at a time");
        ci.reply.sequence = o2.req.sequence;
        ci.reply.result = wheel::Result::Applied;
        ci.t = 1.3;
        Check(c.Step(ci).event == CleanEvent::Done, "the game took it");
        g_snap.slot[kAttackSlot].id = 0;
        ci.reply = wheel::Reply{};
        ci.t = 1.5;
        Check(!c.Step(ci).submit && !c.Wants(), "A holds the plain attack: nothing more, no reads wanted");

        g_snap.slot[kAttackSlot].id = 6;   // the player's own Magic Arrow on A with the skill panel, a bow in hand
        ci.t = 5.0;
        Check(!c.Step(ci).submit && c.Wants(), "a bow in hand after the load: the player's own choice on A is left alone (but watched)");
        ci.ranged = false;   // the bow put away / a weapon swap to a sword
        ci.t = 6.0;
        o2 = c.Step(ci);
        Check(o2.submit && o2.req.prevSkill == 6 && o2.req.skill == 0, "no bow in hand: a bow skill on A is given back as the plain attack");
        ci.reply.sequence = o2.req.sequence;
        ci.reply.result = wheel::Result::Refused;
        ci.t = 6.1;
        Check(c.Step(ci).event == CleanEvent::Refused, "refused (the slot changed meanwhile)");
        ci.reply = wheel::Reply{};
        ci.t = 6.5;
        Check(!c.Step(ci).submit, "not asked again at once");
        ci.t = 8.2;
        Check(c.Step(ci).submit, "asked again after the retry gap");
        int asks = 2;
        bool gaveUp = false;
        for (double u = 10.0; u < 40.0; u += 0.5) {
            ci.t = u;
            const CleanOut k = c.Step(ci);
            asks += k.submit;
            gaveUp = gaveUp || k.event == CleanEvent::GaveUp;
        }
        Check(asks == Cleanup::kMaxTries && gaveUp, "unanswered, given up after kMaxTries, said once");

        Cleanup off;
        ci = CleanIn{};
        ci.on = false;
        ci.hero = 1;
        ci.snap = &g_snap;
        ci.fresh = true;
        Check(!off.Step(ci).submit && !off.Wants(), "the quiver off: never a write");
        ci.on = true;
        ci.fresh = false;
        Check(!off.Step(ci).submit && off.Wants(), "no fresh read: nothing asked, the reads wanted");
        ci.fresh = true;
        g_snap.editable = false;
        Check(!off.Step(ci).submit, "the game not in its controller layout: nothing asked");
    }

    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
