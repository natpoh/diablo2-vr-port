// vr/swing_timer.h: [hud] swing_timer - the attack's zones as the game's gate answers them, the client's measured clock, the outcomes of one
// swing merged, "too soon", the fade and the steps the picture moves in. No game, no clock.
#include <cstdio>

#include "../../vr/swing_timer.h"

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++g_fail; }
}

int main() {
    using namespace swingtimer;

    // -- the server's attack: 14 frames, the game's own green at 13 (one frame left), amber from 9 (five left)
    {
        const Attack a = FromFrames(1000, 14, 13);
        Check(a.valid && a.server && a.greenMs == 520 && a.amberMs == 360, "14 frames: green at 520 ms, amber at 360 ms");
        Check(ZoneAt(a, 1000) == Zone::Red && ZoneAt(a, 1359) == Zone::Red, "red until five frames are left");
        Check(ZoneAt(a, 1360) == Zone::Amber && ZoneAt(a, 1519) == Zone::Amber, "amber in the gate's last five frames");
        Check(ZoneAt(a, 1520) == Zone::Green && ZoneAt(a, 9000) == Zone::Green, "green from one frame left on");
        Check(Progress(a, 1000) == 0.0f && Progress(a, 1260) == 0.5f && Progress(a, 1520) == 1.0f && Progress(a, 5000) == 1.0f, "the fill is full when it turns green");
        Check(ZoneAt(a, 500) == Zone::Red, "a time before the start counts as the start");
    }
    // -- quick_blow_faster: green after its least wait, sooner than the animation; no amber before it
    {
        const Attack a = FromFrames(0, 14, 6);
        Check(a.greenMs == 240 && a.amberMs == 240, "faster: green at 6 frames, amber never before it");
        Check(ZoneAt(a, 239) == Zone::Red && ZoneAt(a, 240) == Zone::Green, "red, then green at once");
        const Attack b = FromFrames(0, 14, 40);
        Check(b.greenMs == 520, "a green past the end is held to the game's own");
        Check(!FromFrames(0, 0, 0).valid && !FromFrames(0, 5000, 10).valid, "no frames, or a silly count: no attack");
        const Attack c = FromFrames(0, 3, 2);
        Check(c.amberMs == 0 && c.greenMs == 80, "a short animation: amber from the start");
    }
    // -- the client's clock: guessed, then measured from the mode
    {
        Check(FromClient(0, 600).greenMs == 560 && FromClient(0, 600).amberMs == 400, "600 ms: green 40 ms before the end, amber 200 ms before");
        Check(FromClient(0, 100).amberMs == 0 && FromClient(0, 100).greenMs == 60, "a very short one: no negative times");
        ClientClock k;
        uint32_t len = 0;
        Check(!k.Sample(false, 7, 0, &len), "an unknown mode changes nothing");
        Check(k.Sample(true, 7, 100, &len) && len == kDefaultClientMs, "the first attack: the guess");
        Check(!k.Sample(true, 7, 300, &len), "still in it: no new start");
        k.Sample(true, 1, 580, &len);
        Check(k.Ms() == 480, "measured when he leaves the attack");
        Check(k.Sample(true, 8, 700, &len) && len == 480, "the next one expects the measured length");
        k.Sample(true, 1, 750, &len);
        Check(k.Ms() == 480, "a flicker too short to be an attack is not measured");
        Check(!k.Sample(true, 10, 800, &len), "a cast is no weapon attack");
    }
    // -- the state: server times win, the client's stand in for a joined player
    {
        State s;
        s.ClientMode(true, 1, 0);
        s.ServerStart(1000, 14, 13);
        s.ClientMode(true, 7, 1030);   // the client's mode follows the server's start
        Check(s.Current().server && s.Current().startMs == 1000, "with our server here its start stays");
        State j;
        j.ClientMode(true, 1, 0);
        j.ClientMode(true, 7, 500);
        Check(j.Current().valid && !j.Current().server && j.Current().startMs == 500 && j.Current().greenMs == kDefaultClientMs - kFrameMs,
              "joined: the client's start and guess");
        State o;
        o.ServerStart(100, 14, 13);
        o.ClientMode(true, 1, 3500);
        o.ClientMode(true, 7, 4000);
        Check(!o.Current().server && o.Current().startMs == 4000, "a server note long ago: the client times it");
    }
    // -- the look: shown after an attack, green stands, then it melts away; nothing before the first attack
    {
        State s;
        Check(!s.Frame(0, Style::Ring, true).visible, "no attack: no ring (go)");
        s.ServerStart(1000, 14, 13);
        const Look a = s.Frame(1000, Style::Ring, true);
        Check(a.visible && a.timer && a.step == 0 && a.zone == Zone::Red && a.timerAlpha == kAlphaSteps, "the start: an empty red ring");
        const Look b = s.Frame(1260, Style::Ring, true);
        Check(b.step == kSteps / 2 && b.zone == Zone::Red, "half way");
        const Look c = s.Frame(1520, Style::Ring, true);
        Check(c.step == kSteps && c.zone == Zone::Green && c.timerAlpha == kAlphaSteps, "full and green");
        Check(s.Frame(1520 + kGreenHoldMs, Style::Ring, true).timerAlpha == kAlphaSteps, "green stands its moment");
        const Look d = s.Frame(1520 + kGreenHoldMs + kFadeMs / 2, Style::Ring, true);
        Check(d.timer && d.timerAlpha == kAlphaSteps / 2, "then melts");
        Check(!s.Frame(1520 + kGreenHoldMs + kFadeMs, Style::Ring, true).visible, "gone: go");
        Check(s.Frame(1001, Style::Ring, true) == a, "a millisecond later: the same picture (steps)");
        Check(s.Frame(1000, Style::Bar, true) != a, "the bar is another picture");
        Check(s.Frame(9999, Style::Ring, true) == s.Frame(9999, Style::Bar, true), "nothing shown: one picture whatever the style");
    }
    // -- "too soon": only while red
    {
        State s;
        Check(!s.Touch(10), "no attack: a touch is no trouble");
        s.ServerStart(1000, 14, 13);
        Check(s.Touch(1100), "red: too soon");
        const Look a = s.Frame(1100, Style::Ring, true);
        Check(a.text == Text::TooSoon && a.textAlpha == kAlphaSteps, "it says so");
        Check(s.Frame(1100 + kTooSoonMs, Style::Ring, true).text != Text::TooSoon, "for a moment");
        Check(s.Frame(1100, Style::Ring, false).text == Text::TooSoon, "even with the numbers off");
        Check(!s.Touch(1400), "amber: the game keeps the attack - not too soon");
        Check(!s.Touch(1600), "green: in");
    }
    // -- the outcomes of one swing
    {
        State s;
        s.ServerStart(1000, 14, 13);
        s.Roll(0x0000, 1045);
        Check(s.Frame(1050, Style::Ring, true).text == Text::Miss, "a miss");
        s.Roll(0x0010, 1046);
        Check(s.Frame(1050, Style::Ring, true).text == Text::Blocked, "a block outranks a miss in the same swing");
        s.Roll(0x0001, 1047);
        s.Damage(30 * 256, false, 1047);
        Look l = s.Frame(1050, Style::Ring, true);
        Check(l.text == Text::Damage && l.value == 30 && l.count == 1 && !l.crit, "a landed blow outranks both");
        s.Roll(0x0000, 1048);
        Check(s.Frame(1050, Style::Ring, true).text == Text::Damage, "and a later miss does not take it back");
        s.Damage(41 * 256 + 200, true, 1049);   // another monster of the same swing, a crit
        l = s.Frame(1050, Style::Ring, true);
        Check(l.value == 30 + 42 && l.count == 2 && l.crit, "a cleave: summed, counted, the crit kept");
        Check(s.Frame(1050, Style::Ring, false).text == Text::None, "numbers off: no number");
        // the ring's number stays while the ring does
        Check(s.Frame(1520 + kGreenHoldMs, Style::Ring, true).text == Text::Damage, "the number stays with the ring");
        Check(s.Frame(1520 + kGreenHoldMs + kFadeMs, Style::Ring, true).text == Text::None, "and goes with it");
        // the next attack: a fresh label
        s.ServerStart(1600, 14, 13);
        s.Damage(5 * 256, false, 1640);
        l = s.Frame(1650, Style::Ring, true);
        Check(l.value == 5 && l.count == 1 && !l.crit, "the next swing starts its own count");
        Check(Whole(0) == 0 && Whole(10) == 1 && Whole(256) == 1 && Whole(384) == 2, "life in whole points, any loss at least 1");
    }
    // -- the bar's number rises and fades; outcomes with no attack of ours merge for a moment
    {
        State s;
        s.Damage(10 * 256, false, 5000);   // (a blow with no attack of ours timed: Direct hit's cleave extra)
        s.Damage(12 * 256, false, 5100);
        Look l = s.Frame(5100, Style::Bar, true);
        Check(l.visible && !l.timer && l.value == 22 && l.count == 2, "close together: one swing");
        Check(l.rise == static_cast<int>(100 * kRiseSteps / kRiseMs), "rising from its first outcome");
        Check(s.Frame(5000 + kRiseFadeFromMs, Style::Bar, true).textAlpha == kAlphaSteps, "full until it starts to fade");
        Check(s.Frame(5000 + kRiseMs, Style::Bar, true).text == Text::None, "gone at the top");
        s.Damage(7 * 256, false, 5400);
        Check(s.Frame(5400, Style::Bar, true).value == 7, "later: a new one");
        Check(s.Frame(5000 + kLabelMinMs - 1, Style::Ring, true).text == Text::Damage, "in the ring with no attack: its least time");
    }

    // -- [gameplay] swing_pace_joined: the client's clock only, once measured; hold near green, drop sooner
    {
        State s;
        s.ClientMode(true, 1, 0);
        s.ClientMode(true, 7, 100);   // the first attack: the guess - no pace on it
        Check(s.PaceAt(150) == Pace::Send, "the clock's first guess decides nothing");
        s.ClientMode(true, 1, 600);   // measured: 500 ms
        s.ClientMode(true, 7, 1000);  // green at 1000 + 460
        Check(s.PaceAt(1000) == Pace::Drop, "far from green: dropped (the host refuses it)");
        Check(s.PaceAt(1159) == Pace::Drop && s.PaceAt(1160) == Pace::Hold, "held from kHoldMs before green");
        Check(s.PaceAt(1459) == Pace::Hold && s.PaceAt(1460) == Pace::Send, "sent at green");
        State h;
        h.ServerStart(1000, 14, 13);
        Check(h.PaceAt(1001) == Pace::Send, "our server's times: our gate decides, nothing held");
        Check(State{}.PaceAt(5) == Pace::Send, "no attack: sent");
    }

    if (g_fail) { std::fprintf(stderr, "%d failed\n", g_fail); return 1; }
    std::puts("swing_timer_test: all passed");
    return 0;
}
