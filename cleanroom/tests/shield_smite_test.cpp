// Synthetic checks of vr/shield_smite.h: when a shield bash casts a Smite, and at whom. No game, no headset.
// Build: the `shield_smite_test` target; the exit code is 0 when everything passes.
#include "../../vr/hit_claim.h"
#include "../../vr/shield_smite.h"

#include <cstdio>
#include <cstdlib>

using namespace shieldsmite;
using stroke::Vec;

namespace {
void Check(bool ok, const char* why) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", why);
        std::exit(1);
    }
}

Gate OpenGate() {
    Gate g;
    g.enabled = g.shield = g.smiteSlot = g.playing = true;
    g.rightTrigger = 0;
    return g;
}

// The left hand carries the shield 0.45 m ahead of the hero and 1.1 m up; the hero stands at the origin.
struct Sim {
    Detector det;
    Gate gate = OpenGate();
    stroke::Sample sample;
    stroke::Sweep sweep;
    uint64_t session = 1;
    double now = 0;
    Sim() {
        sample.active = true;
        sample.epoch = 1;
        sample.context = 1;
        sample.hand.p = {-0.2f, 1.1f, -0.4f};
        Place();
        Step(8);
    }
    void Place() { sample.blade = ShieldBlade(sample.hand.p + Vec{0.0f, 0.0f, -0.05f}, {0, 1, 0}); }
    bool Step(int n = 1) {
        bool r = false;
        while (n--) {
            sample.time += 0.02;
            now = sample.time;
            r = det.Step(gate, sample, sweep);
        }
        return r;
    }
    bool Move(Vec d) {
        sample.hand.p = sample.hand.p + d;
        Place();
        return Step();
    }
    // Moves until a Pick succeeds against `monsters` or `frames` run out.
    bool Bash(Vec d, const Monster* monsters, size_t count, int frames, size_t* which = nullptr) {
        for (int i = 0; i < frames; ++i)
            if (Move(d)) {
                size_t index = 0;
                if (det.Pick(sweep, session, now, monsters, count, index)) {
                    if (which) *which = index;
                    return true;
                }
            }
        return false;
    }
};

meleetouch::MonsterRow Zombie() { return {2, 2, 72}; }
Monster At(uint32_t id, float x, float z, float upm = 4.0f) {
    Monster m;
    m.id = id;
    Check(meleetouch::BodyFor(Zombie(), x * upm, z * upm, 0.0f, upm, m.body), "a zombie's row reads as a body");
    return m;
}
}  // namespace

int main() {
    // -- the slot table -------------------------------------------------------------------------------------------
    Check(SlotPress(0).buttons == kPadB && SlotPress(0).lt == 0, "slot 0 is B");
    Check(SlotPress(3).buttons == kPadRB && SlotPress(3).lt == 0, "slot 3 is RB");
    Check(SlotPress(5).buttons == kPadX && SlotPress(5).lt == 255, "slot 5 is LT + X");
    Check(SlotPress(8).buttons == kPadA && SlotPress(8).lt == 0, "slot 8 is A");
    Check(SlotPress(9).buttons == kPadA && SlotPress(9).lt == 255, "slot 9 is LT + A");
    Check(SlotPress(-1).buttons == 0 && SlotPress(10).buttons == 0, "no such slot presses nothing");
    {
        int on[kSlotCount];
        for (int& s : on) s = -1;
        Check(SmiteSlot([&](int s) { return on[s]; }) == -1, "no Smite on the bar: no slot");
        on[6] = kSmite;
        Check(SmiteSlot([&](int s) { return on[s]; }) == 6, "Smite on LT + Y");
        on[2] = kSmite;
        Check(SmiteSlot([&](int s) { return on[s]; }) == 2, "Smite on Y and LT + Y: the plain button first");
        on[8] = kSmite;
        Check(SmiteSlot([&](int s) { return on[s]; }) == 2, "A comes after the plain face buttons");
    }
    // -- how long the press is held (PulseMs) -----------------------------------------------------------------------
    Check(PulseMs(SlotPress(0)) == 65 && PulseMs(SlotPress(8)) == 65, "a plain slot: 65 ms");
    for (int s : {4, 5, 6, 7, 9}) Check(PulseMs(SlotPress(s)) == 180, "a slot of the left trigger's row: 180 ms");
    {   // The game's pad code as d2r-pcvr describes it: it polls 25 times a second; with LT seen it prepares the second bank, ready
        // ~50 ms after the poll that first saw LT; the button counts at a poll that sees it held with the bank ready. Our press
        // starts at any moment (an XR frame) and the polls run at any phase: 160 combinations, as d2r-pcvr's fixture.
        auto taken = [](double start, double phase, double holdMs) {
            double firstLt = -1.0;
            for (double poll = phase - 40.0; poll < start + holdMs + 200.0; poll += 40.0) {
                const bool held = poll >= start && poll < start + holdMs;
                if (!held) { if (firstLt >= 0.0) return false; continue; }
                if (firstLt < 0.0) firstLt = poll;
                if (poll - firstLt >= 50.0) return true;
            }
            return false;
        };
        int lost65 = 0, lost180 = 0, plainLost = 0;
        for (int i = 0; i < 16; ++i)
            for (int j = 0; j < 10; ++j) {
                const double start = i * 11.1, phase = j * 4.0;
                lost65 += !taken(start, phase, 65.0);
                lost180 += !taken(start, phase, PulseMs(SlotPress(5)));
                // a plain button needs no bank: one poll inside the press is enough
                const double first = phase + 40.0 * std::ceil((start - phase) / 40.0);
                plainLost += !(first < start + PulseMs(SlotPress(0)));
            }
        Check(lost65 == 160, "LT + a button held 65 ms: lost at every timing");
        Check(lost180 == 0, "LT + a button held 180 ms: taken at every timing");
        Check(plainLost == 0, "a plain button held 65 ms: taken at every timing");
    }

    // -- a monster as a capsule (melee_bodies.h, shared) ----------------------------------------------------------
    {
        stroke::Body b;
        Check(meleetouch::BodyFor(Zombie(), 8.0f, -4.0f, 0.0f, 4.0f, b), "a row that reads");
        Check(std::fabs(b.bottom.x - 2.0f) < 1e-4f && std::fabs(b.bottom.z + 1.0f) < 1e-4f, "the grid place in metres (4 units a metre)");
        Check(b.top.y > b.bottom.y && b.top.y < 2.2f, "a standing capsule, about a man's height");
        Check(!meleetouch::BodyFor(meleetouch::MonsterRow{}, 8.0f, -4.0f, 0.0f, 4.0f, b), "a row that does not read as a footprint is refused");
    }

    // -- the shield capsule ---------------------------------------------------------------------------------------
    {
        const stroke::Blade b = ShieldBlade({1, 2, 3}, {0, 5, 0});
        Check(std::fabs(stroke::Length(b.tip - b.base) - 0.4f) < 1e-5f && b.radius == kShieldRadius, "0.4 m long, radius 0.15");
        Check(std::fabs((b.base.y + b.tip.y) / 2 - 2) < 1e-5f, "centred on the attachment");
    }

    // -- a bash ---------------------------------------------------------------------------------------------------
    // The shield starts at x = -0.2 and sweeps right at 2 m/s; a monster stands to the right at x = 0.9, 0.45 ahead
    // of the hero - so the bash goes through it.
    const Monster right = At(7, 0.9f, -0.45f);
    {
        Sim s;
        size_t which = 99;
        Check(s.Bash({0.04f, 0, 0}, &right, 1, 40, &which) && which == 0, "a hard bash through a monster picks it");
        size_t index = 0;
        Check(!s.det.Pick(s.sweep, s.session, s.now, &right, 1, index), "one bash casts one Smite");
        for (int i = 0; i < 10; ++i) {
            if (s.Move({0.04f, 0, 0})) Check(!s.det.Pick(s.sweep, s.session, s.now, &right, 1, index), "the follow-through does not cast again");
        }
    }
    {
        Sim s;
        Check(!s.Bash({0.04f, 0, 0}, nullptr, 0, 40), "nobody there: nothing to cast at");
    }
    {
        Sim s;   // a monster the shield passes beside
        const Monster beside = At(8, 0.9f, 1.6f);
        Check(!s.Bash({0.04f, 0, 0}, &beside, 1, 40), "a monster out of the shield's way is not hit");
    }
    {
        Sim s;   // a slow hand: the shield is carried into the monster
        Check(!s.Bash({0.004f, 0, 0}, &right, 1, 200), "a slow push (0.2 m/s) is not a bash");
    }
    {
        Sim s;   // a resting shield beside / inside a monster
        const Monster here = At(9, -0.2f, -0.45f);
        Check(!s.Bash({0, 0, 0}, &here, 1, 100), "a resting shield inside a monster does nothing");
        size_t index = 0;
        s.Step(5);
        Check(!s.det.Pick(s.sweep, s.session, s.now, &here, 1, index), "and no sweep means no pick");
    }
    {
        Sim s;   // the nearest of two
        const Monster pair[2] = {At(10, 1.05f, -0.45f), At(11, 0.80f, -0.45f)};
        size_t which = 99;
        Check(s.Bash({0.04f, 0, 0}, pair, 2, 40, &which), "two monsters in the way: one is picked");
        Check(which == 0 || which == 1, "a valid index");
    }
    {
        Sim s;   // the trigger of the weapon hand held down: nothing, even through a monster
        s.gate.rightTrigger = 0.6f;
        Check(!s.Bash({0.04f, 0, 0}, &right, 1, 40), "the right trigger down: no cast");
        s.gate.rightTrigger = 0.0f;
        s.Step(12);
    }
    for (int what = 0; what < 6; ++what) {
        Sim s;
        switch (what) {
            case 0: s.gate.enabled = false; break;
            case 1: s.gate.shield = false; break;
            case 2: s.gate.smiteSlot = false; break;
            case 3: s.gate.playing = false; break;
            case 4: s.gate.f4 = false; break;   // F4 only: another view is a shut gate
            default: s.gate.rightTrigger = kTriggerDown; break;
        }
        Check(!Open(s.gate), "a shut gate");
        Check(!s.Bash({0.04f, 0, 0}, &right, 1, 40), "a shut gate casts nothing");
    }
    {
        Sim s;   // F4 only: the view changes in the middle of a bash - the stroke is dropped, and back in F4 only a new one casts
        bool fired = false;
        for (int i = 0; i < 6; ++i) s.Move({0.04f, 0, 0});
        s.gate.f4 = false;
        for (int i = 0; i < 6; ++i) {
            size_t index = 0;
            if (s.Move({0.04f, 0, 0}) && s.det.Pick(s.sweep, s.session, s.now, &right, 1, index)) fired = true;
        }
        Check(!fired, "another view: the bash goes on through the monster and casts nothing");
        s.gate.f4 = true;
        for (int i = 0; i < 3; ++i) {
            size_t index = 0;
            if (s.Move({0.04f, 0, 0}) && s.det.Pick(s.sweep, s.session, s.now, &right, 1, index)) fired = true;
        }
        Check(!fired, "back in F4 in the middle of the old stroke: it does not resume");
        s.sample.hand.p = {-0.2f, 1.1f, -0.4f};
        s.Place();
        s.Step(30);
        Check(s.Bash({0.04f, 0, 0}, &right, 1, 40), "a new bash in F4 casts again");
    }
    {
        Sim s;   // a trigger pulled in the middle of the stroke drops it; letting go does not resume it at once
        bool fired = false;
        for (int i = 0; i < 4; ++i) {
            s.gate.rightTrigger = (i == 1) ? 0.9f : 0.0f;
            size_t index = 0;
            if (s.Move({0.04f, 0, 0}) && s.det.Pick(s.sweep, s.session, s.now, &right, 1, index)) fired = true;
        }
        Check(!fired, "a stroke interrupted by the trigger does not cast");
    }
    {
        Sim s;   // the second bash, after a pause, casts again
        Check(s.Bash({0.04f, 0, 0}, &right, 1, 40), "first bash");
        s.sample.hand.p = {-0.2f, 1.1f, -0.4f};   // the hand back where it began (a tracking jump drops the stroke)
        s.Place();
        s.Step(30);                                // and 0.6 s of rest, beyond the minimum gap
        Check(s.Bash({0.04f, 0, 0}, &right, 1, 40), "a second bash after a pause casts again");
    }
    {
        Sim s;   // a new stroke inside the minimum gap touches the monster but casts nothing
        Check(s.Bash({0.04f, 0, 0}, &right, 1, 40), "first bash");
        size_t index = 0;
        int touched = 0, cast = 0;
        const double first = s.now;
        for (int i = 0; i < 30 && s.now - first < kMinGap - 0.05; ++i) {
            const Vec d = (i / 6) % 2 == 0 ? Vec{-0.04f, 0, 0} : Vec{0.04f, 0, 0};   // back, forward, back ...
            if (s.Move(d) && stroke::Touches(s.sweep.before, s.sweep.after, right.body)) {
                ++touched;
                cast += s.det.Pick(s.sweep, s.session, s.now, &right, 1, index);
            }
        }
        Check(touched > 0, "(the test does touch the monster again inside the gap)");
        Check(cast == 0, "no second request inside the minimum gap");
    }
    {
        Sim s;   // a new game session forgets the strokes under way
        s.Bash({0.04f, 0, 0}, &right, 1, 40);
        ++s.sample.epoch;
        Check(!s.Move({0.04f, 0, 0}), "a context change drops the stroke");
    }

    // -- the cast on its way --------------------------------------------------------------------------------------
    {
        Cast c;
        Check(!c.Open(0), "nothing under way");
        c.Begin(42, 120.5f, 88.25f, 10.0);
        Check(c.Open(10.1) && c.Monster() == 42 && c.CellX() == 120.5f && c.CellY() == 88.25f, "the monster waits for the game's lookups");
        Check(c.Open(10.0 + kWindow - 0.01), "still open just inside the window");
        Check(!c.Open(10.0 + kWindow + 0.01), "closed after the window: a refused press leaves nothing behind");
        c.Begin(43, 1, 2, 20.0);
        c.Finish();
        Check(!c.Open(20.01), "closed once the click has asked");
        c.Begin(44, 1, 2, 30.0);   // F4 only: the view changes while a cast waits - shield_smite.cpp Finish()es it, the lookups are the game's
        c.Finish();
        Check(!c.Open(30.01), "closed by a view change: the monster does not wait for some other cast");
    }
    // -- the bash speed ([gameplay] shield_min_speed) and the debug view's numbers ----------------------------------
    {
        const stroke::Params d = BashParams(0.8f), p{};
        Check(d.strokeSpeed == p.strokeSpeed && d.beginSpeed == p.beginSpeed && d.quietSpeed == p.quietSpeed && d.onsetExcursion == p.onsetExcursion &&
                  d.onsetWindow == p.onsetWindow,
              "the default bash speed is exactly the engine's default: today's behaviour");
        Check(Detector{}.Params().strokeSpeed == 0.8f, "a detector starts at the default bash speed");
        Check(BashParams(0.1f).strokeSpeed == 0.3f && BashParams(50.0f).strokeSpeed == 8.0f, "the bash speed is kept between 0.3 and 8");
        Check(BashParams(0.5f).beginSpeed < 1.2f && BashParams(3.0f).beginSpeed == 1.2f, "a lower bash speed lowers the speed that begins a stroke; a higher one leaves it");
        Check(BashParams(0.8f, 8.0f).onsetExcursion == p.onsetExcursion && std::fabs(BashParams(0.8f, 15.0f).onsetExcursion - 0.15f) < 1e-6f, "the start distance is the bash's own, in cm");
        Check(std::fabs(BashParams(0.8f, 1.0f).onsetExcursion - 0.02f) < 1e-6f && std::fabs(BashParams(0.8f, 99.0f).onsetExcursion - 0.30f) < 1e-6f, "... kept between 2 and 30 cm");
    }
    {   // a push of 10 cm at 2 m/s: a bash with the default start distance, not with 15 cm
        Sim def;
        bool counted = false;
        for (int i = 0; i < 3; ++i) counted |= def.Move({0.04f, 0, 0}) && def.det.Counting();
        Check(counted, "a 2 m/s push 10 cm long is a bash at the default start distance");
        Sim far;
        far.det.SetParams(BashParams(0.8f, 15.0f));
        counted = false;
        for (int i = 0; i < 3; ++i) counted |= far.Move({0.04f, 0, 0}) && far.det.Counting();
        Check(!counted, "... and not with the start distance at 15 cm");
        for (int i = 0; i < 4; ++i) counted |= far.Move({0.04f, 0, 0}) && far.det.Counting();
        Check(counted, "... until the push is longer");
    }
    {   // a 0.9 m/s push: no stroke at the default (it begins at 1.2), a bash with the speed set to 0.5
        Sim def;
        bool counted = false;
        for (int i = 0; i < 20; ++i) counted |= def.Move({0.018f, 0, 0}) && def.det.Counting();
        Check(!counted, "a 0.9 m/s push is not a bash at the default bash speed");
        Sim low;
        low.det.SetParams(BashParams(0.5f));
        counted = false;
        for (int i = 0; i < 20; ++i) counted |= low.Move({0.018f, 0, 0}) && low.det.Counting();
        Check(counted, "...and it is with the bash speed lowered to 0.5");
        Sim high;
        high.det.SetParams(BashParams(3.0f));
        counted = false;
        for (int i = 0; i < 20; ++i) counted |= high.Move({0.04f, 0, 0}) && high.det.Counting();   // 2 m/s
        Check(!counted, "a 2 m/s bash does not count with the bash speed set to 3");
    }
    {   // the observer: the shield's speed and stroke are read with the trigger down too, but a bash never counts
        Sim s;
        s.gate.rightTrigger = 0.9f;
        float seen = 0.0f;
        bool counted = false, stroke = false;
        for (int i = 0; i < 12; ++i) {
            counted |= s.Move({0.04f, 0, 0}) && s.det.Counting();
            counted |= s.det.Counting();
            seen = std::max(seen, s.det.Speed());
            stroke |= s.det.InStroke();
        }
        Check(seen > 1.5f && stroke, "trigger down: the shield's speed and its stroke are still read (the drawing shows them)");
        Check(!counted, "trigger down: and a bash never counts");
        Sim t;
        bool counting = false;
        for (int i = 0; i < 12; ++i) {
            t.Move({0.04f, 0, 0});
            counting |= t.det.Counting();
        }
        Check(counting, "trigger up: a hard push counts");
        Check(t.det.SinceRequest(100.0) > 1e6, "no bash asked for yet");
        size_t index = 0;
        const Monster right2 = At(7, 0.9f, -0.45f);
        Sim u;
        Check(u.Bash({0.04f, 0, 0}, &right2, 1, 40), "a bash");
        Check(u.det.SinceRequest(u.now + 0.1) < 0.2, "...is remembered for the gap");
        (void)index;
    }
    {   // the reasons
        using namespace skilltriggers;
        Facts f;
        f.enabled = true;
        f.playing = true;
        f.slot = true;
        f.heroKnown = true;
        f.haveNear = true;
        f.nearVerdict = Verdict::Hostile;
        f.speed = 2.0f;
        f.minSpeed = 0.8f;
        f.inStroke = true;
        f.mana = 20.0f;
        Check(Explain(f) == 0, "everything in order: a bash would count");
        Facts g = f;
        g.enabled = false;
        g.speed = 0.0f;
        Check(Explain(g) == kWhyOff, "off: only that");
        g = f;
        g.speed = 0.3f;
        Check(Explain(g) == kWhySlow, "too slow");
        g = f;
        g.inStroke = false;
        Check(Explain(g) == kWhyNoStroke, "fast enough, but no stroke yet");
        g = f;
        g.rightTrigger = 0.5f;
        Check(Explain(g) == kWhyTrigger, "right trigger down");
        g = f;
        g.slot = false;
        Check(Explain(g) == kWhyNoSlot, "no Smite slot");
        g = f;
        g.mana = 1.0f;
        Check(Explain(g) == kWhyNoMana, "no mana");
        g.mana = -1.0f;
        Check(Explain(g) == 0, "mana not read: not a reason");
        g = f;
        g.nearVerdict = Verdict::Refused;
        Check(Explain(g) == kWhyNotHostile, "not hostile");
        g.nearVerdict = Verdict::Unknown;
        Check(Explain(g) == 0, "hostility unknown: not a reason (the game decides)");
        g = f;
        g.haveNear = false;
        Check(Explain(g) == kWhyNoMonster, "no monster near");
        g.heroKnown = false;
        Check((Explain(g) & kWhyNoHero) && !(Explain(g) & kWhyNoMonster), "no hero: that, not 'no monster'");
        g = f;
        g.sinceRequest = 0.1;
        Check(Explain(g) == kWhyTooSoon, "too soon after the last bash");
        g = f;
        g.playing = false;
        Check(Explain(g) == kWhyNotPlaying, "not playing");
        g = f;
        g.speed = 0.1f;
        g.rightTrigger = 1.0f;
        g.slot = false;
        Check(Explain(g) == (kWhySlow | kWhyTrigger | kWhyNoSlot), "several reasons at once");
        Check(!(Explain(f) & (kWhyNoMana | kWhyNotHostile)), "the game's own checks are listed apart from ours");
        Check(kWhyNoMana > kWhyTooSoon, "(bit order: ours first)");
    }

    // -- the blow a bash makes: claimed at the press by the arm that wears the shield (the game's hit lands after its click closed the cast) ------------------------------------
    {
        Check(BashSide(-1) == 1 && BashSide(1) == 1 && BashSide(0) == 0, "the shield's side: the left with [hands] shoulder_draw off (-1) and on the left arm, the right after a hand swap");
        Check(BashSide(7) == 1, "(nonsense is the left, the game's)");
        // the race this exists for: the cast is finished at the game's click, before the hit
        hitclaim::Clear();
        Cast cast;
        const double t0 = 100.0;
        cast.Begin(7, 10.0f, 20.0f, t0);
        hitclaim::Press(BashSide(-1), (int64_t)(t0 * 1000));
        Check(cast.Open(t0 + 0.005), "the cast is open for the game's first lookups");
        cast.Finish();   // the click has asked for the target
        Check(!cast.Open(t0 + 0.020), "...and is over at the click");
        Check(hitclaim::Take((int64_t)(t0 * 1000) + 40) == 1, "the hit 40 ms after the press: the cast is over, the bash still owns the blow - the LEFT hand's");
        hitclaim::Clear();
        hitclaim::Press(BashSide(0), 5000);
        Check(hitclaim::Take(5040) == 0, "a bash with the shield on the right arm: the right hand's");
        hitclaim::Clear();
    }
    std::puts("shield_smite_test: all checks passed");
    return 0;
}
