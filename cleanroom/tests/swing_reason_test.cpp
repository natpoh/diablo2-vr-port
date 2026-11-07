// vr/swing_reason.h and vr/swing_timer.h's refusals: [hud] swing_timer_reasons - each of melee_touch's refusals mapped to its word and numbers,
// the numbers as the picture writes them, and how a refusal shares the ring with the game's outcomes and "too soon". No game, no clock.
#include <cstdio>
#include <cstring>
#include <limits>

#include "../../vr/swing_reason.h"

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++g_fail; }
}

int main() {
    using namespace swingtimer;
    using swingreason::Refusal;

    // -- the strike rules' verdicts (a swing's or a thrust's contact)
    {
        strikedir::Judgement jd;
        jd.why = strikedir::Why::Shaft;
        Check(swingreason::FromRule(jd).text == Text::Shaft, "the shaft below the head: shaft");
        jd.why = strikedir::Why::Butt;
        Check(swingreason::FromRule(jd).text == Text::Handle, "the butt behind the front hand: handle");
        jd.why = strikedir::Why::NotHead;
        Check(swingreason::FromRule(jd).text == Text::NotTip, "outside the head's share: not the tip");
        jd.why = strikedir::Why::Away;
        Check(swingreason::FromRule(jd).text == Text::Away, "not toward the monster: wrong way");
        jd.why = strikedir::Why::Backward;
        Check(swingreason::FromRule(jd).text == Text::Away, "backward: wrong way too");
        jd.why = strikedir::Why::TooSmall;
        jd.travelCm = 12.3f;
        jd.needCm = 30.0f;
        const Refusal r = swingreason::FromRule(jd);
        Check(r.text == Text::Short && r.got == 1230 && r.need == 3000, "a swing too small: short, its travel against the kind's");
        jd.why = strikedir::Why::Counts;
        Check(swingreason::FromRule(jd).text == Text::None, "a strike: nothing to say");
    }
    // -- a swing's contact: the speed where it touched first, as melee_touch judges it
    {
        strikedir::Judgement jd;
        jd.why = strikedir::Why::Shaft;
        const Refusal slow = swingreason::FromSwing(3.14f, 5.0f, jd);
        Check(slow.text == Text::Slow && slow.got == 314 && slow.need == 500, "slower than the least speed: slow 3.14/5 (before the shaft)");
        Check(swingreason::FromSwing(6.0f, 5.0f, jd).text == Text::Shaft, "fast enough: the rule's verdict");
        jd.why = strikedir::Why::Counts;
        Check(swingreason::FromSwing(6.0f, 5.0f, jd).text == Text::None, "fast and a strike: none");
    }
    // -- a thrust's contact: speed, the rules, then its own distance and jab
    {
        strikedir::Judgement jd;
        thrust::Verdict v;
        v.why = thrust::Why::Weak;
        v.accel = 12.0f;
        v.needAccel = 30.0f;
        Check(swingreason::FromThrust(1.0f, 2.0f, jd, v).text == Text::Slow, "a thrust too slow: slow first");
        const Refusal push = swingreason::FromThrust(3.0f, 2.0f, jd, v);
        Check(push.text == Text::Push && push.got == 1200 && push.need == 3000, "too little jab: push 12/30");
        v.why = thrust::Why::Short;
        v.alongCm = 8.0f;
        v.needCm = 15.0f;
        const Refusal sh = swingreason::FromThrust(3.0f, 2.0f, jd, v);
        Check(sh.text == Text::Short && sh.got == 800 && sh.need == 1500, "a thrust too short: short 8/15");
        jd.why = strikedir::Why::Away;
        Check(swingreason::FromThrust(3.0f, 2.0f, jd, v).text == Text::Away, "the rules before the thrust's own distance");
        jd.why = strikedir::Why::Counts;
        v.why = thrust::Why::Counts;
        Check(swingreason::FromThrust(3.0f, 2.0f, jd, v).text == Text::None, "a counted thrust: none");
    }
    // -- a strike judged by its force (a weapon the weight model holds - zbiromir's bardiche)
    {
        strikeforce::Verdict v;
        v.why = strikeforce::Why::TooWeak;
        v.score = 0.62f;
        v.need = 1.1f;
        const Refusal w = swingreason::FromForce(v, false, false);
        Check(w.text == Text::Weak && w.got == 62 && w.need == 110, "too weak: weak 0.62/1.10");
        v.why = strikeforce::Why::NoHand;
        Check(swingreason::FromForce(v, false, false).text == Text::None, "no hand on it: nothing to tell the player");
        v.why = strikeforce::Why::Counts;
        Check(swingreason::FromForce(v, true, false).text == Text::Shaft, "strong enough but on the shaft: shaft");
        Check(swingreason::FromForce(v, false, true).text == Text::Handle, "... behind the front hand: handle");
        Check(swingreason::FromForce(v, false, false).text == Text::None, "a blow: none");
        v.why = strikeforce::Why::Shared;
        Check(swingreason::FromForce(v, true, false).text == Text::Shaft, "a shared blow on the shaft: shaft");
    }
    // -- a blade into a body with no stroke on
    {
        const Refusal s = swingreason::FromNoStroke(1.5f, 5.0f, 12.0f);
        Check(s.text == Text::Slow && s.got == 150 && s.need == 500, "under the stroke's speed: slow");
        const Refusal h = swingreason::FromNoStroke(6.0f, 5.0f, 12.0f);
        Check(h.text == Text::Short && h.got < 0 && h.need == 1200, "fast but not far enough: short, its own distance not known");
    }
    // -- the numbers under the word
    {
        char b[32];
        Numbers(Text::Slow, 314, 500, b, sizeof b);
        Check(!std::strcmp(b, "3.1/5.0"), "a speed: one decimal");
        Numbers(Text::Weak, 62, 110, b, sizeof b);
        Check(!std::strcmp(b, "0.62/1.10"), "a force: two decimals");
        Numbers(Text::Short, 1230, 3000, b, sizeof b);
        Check(!std::strcmp(b, "12/30"), "centimetres: whole");
        Numbers(Text::Push, 1250, 3000, b, sizeof b);
        Check(!std::strcmp(b, "13/30"), "an acceleration: whole, rounded");
        Numbers(Text::Short, -1, 1200, b, sizeof b);
        Check(b[0] == 0, "the swing's own number not known: none");
        Numbers(Text::Shaft, 100, 200, b, sizeof b);
        Check(b[0] == 0, "the shaft has no numbers");
        Numbers(Text::Damage, 100, 200, b, sizeof b);
        Check(b[0] == 0, "nor the game's outcomes");
        Check(Hundredths(-1.0f) == -1 && Hundredths(std::numeric_limits<float>::quiet_NaN()) == -1 && Hundredths(1.006f) == 101, "hundredths, -1 for what is not known");
        Check(IsReason(Text::Shaft) && IsReason(Text::Shield) && !IsReason(Text::TooSoon) && !IsReason(Text::Miss) && !IsReason(Text::None), "which texts are refusals");
    }
    // -- the ring: a refusal with no attack running shows alone, then melts away
    {
        State s;
        Check(s.Refused(Text::Shaft, -1, 0, 1000), "a shaft with no attack: shown");
        Look k = s.Frame(1000, Style::Ring, true);
        Check(k.visible && !k.timer && k.text == Text::Shaft && k.textAlpha == kAlphaSteps, "the word alone, full");
        Check(s.Frame(1000 + kReasonFadeFromMs + (kReasonMs - kReasonFadeFromMs) / 2, Style::Ring, true).textAlpha < kAlphaSteps, "fading");
        Check(!s.Frame(1000 + kReasonMs, Style::Ring, true).visible, "gone");
        Check(s.Frame(1000, Style::Ring, false).text == Text::Shaft, "with the numbers off too (its own tick decides)");
        Check(!s.Refused(Text::Damage, 0, 0, 1100) && !s.Refused(Text::None, 0, 0, 1100), "the game's outcomes are not refusals");
        s.Refused(Text::Slow, 314, 500, 1200);
        k = s.Frame(1250, Style::Bar, true);
        Check(k.text == Text::Slow && k.got == 314 && k.need == 500 && k.rise > 0, "the newest wins, its numbers kept; the bar's rises");
    }
    // -- with the game's outcomes: the newer one shows; a blow that just landed is not covered by another monster's shaft
    {
        State s;
        s.ServerStart(1000, 14, 13);
        s.Damage(256 * 40, false, 1050);
        Check(!s.Refused(Text::Shaft, -1, 0, 1100), "50 ms after a landed blow: the shaft of the same swing is not shown");
        Check(s.Frame(1100, Style::Ring, true).text == Text::Damage, "the damage stays");
        Check(s.Refused(Text::Shaft, -1, 0, 1050 + kReasonQuietMs), "a moment later: the next swing's shaft is shown");
        Check(s.Frame(1050 + kReasonQuietMs, Style::Ring, true).text == Text::Shaft, "over the older number");
        Check(s.LastRefusal() == Text::Shaft, "the last refusal is known");
        State n;   // no attack of ours around: the game's miss after a refusal, then a refusal after the miss
        n.Refused(Text::Shaft, -1, 0, 1000);
        n.Roll(0, 1100);
        Check(n.Frame(1100, Style::Ring, true).text == Text::Miss, "a newer outcome over the refusal");
        Check(n.Refused(Text::Short, 800, 1500, 1500) && n.Frame(1500, Style::Ring, true).text == Text::Short, "a newer refusal over the miss");
    }
    // -- "too soon" goes the same way: a touch while red, or one dropped (Refused TooSoon), its own shorter time
    {
        State s;
        s.ServerStart(1000, 14, 13);
        Check(s.Touch(1100), "red: too soon");
        Check(s.Frame(1100, Style::Ring, true).text == Text::TooSoon, "it says so");
        Check(s.Frame(1100 + kTooSoonMs, Style::Ring, true).text != Text::TooSoon, "for its own moment");
        Check(s.Refused(Text::TooSoon, -1, 0, 2000) && s.Frame(2000, Style::Ring, true).text == Text::TooSoon, "a dropped touch: too soon");
        s.Refused(Text::Away, -1, 0, 2100);
        Check(s.Frame(2100, Style::Ring, true).text == Text::Away, "a refusal after it: the newest");
    }
    // -- the look changes when the numbers do (a new picture is painted)
    {
        Look a, b;
        a.got = 314;
        b.got = 315;
        Check(a != b, "a different number: another picture");
    }

    if (g_fail == 0) std::printf("swing_reason_test: all passed\n");
    return g_fail == 0 ? 0 : 1;
}
