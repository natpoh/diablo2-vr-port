// Offline checks of vr/quick_blow_probe.h ([debug] quick_blow_probe): one record per attack of our hero, the verdict on its blow (fired / re-armed
// before it by the attack path / the mode changed / ended with no trigger / no blow in the record), when it is handed back to be told (the next
// scheduler run, past its end by frame or by clock, once only), the caps, and the line's words. Run: quick_blow_probe_test.exe (exit 0 = all passed).
#include <cstdio>
#include <cstring>

#include "quick_blow_probe.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

using namespace qbprobe;

static bool Has(const Attack& a, const char* words) {
    char b[1200];
    Describe(a, b, sizeof b);
    return std::strstr(b, words) != nullptr;
}

int main() {
    // ---- the blow fires on the next frame: told when the next attack starts ----
    {
        Recorder r;
        Attack done;
        CHECK(!r.Scheduled(7, true, 4, 2, 2, 1000, 1001, 1014, 0, &done));   // (nothing before it)
        r.Triggered(1, 1001, 7, true);
        CHECK(r.Scheduled(7, true, 4, 2, 2, 1014, 1015, 1028, 560, &done));
        CHECK(done.number == 1 && done.blow && done.blowFrame == 1001);
        CHECK(Judge(done) == Verdict::Fired);
        CHECK(Has(done, "BLOW FIRED at frame 1001 (+1)") && Has(done, "blow moved from frame 4 to the start frame 2") && Has(done, "0x1@+1"));
        CHECK(r.Now().number == 2 && r.Now().active);
    }
    // ---- the attack path re-arms the hero's event before the moved blow's frame: LOST, re-armed, with the numbers ----
    {
        Recorder r;
        Attack done;
        r.Scheduled(7, true, 4, 2, 2, 2000, 2001, 2014, 0, &done);
        r.Rearmed(Who::PathStart, 2000, 2001, 2001, 2001);
        r.Triggered(kTickCode, 2001, 7, false);
        r.Rearmed(Who::PathStep, 2001, 2001, 2002, 2002);
        CHECK(r.DueByFrame(2015, &done));
        CHECK(Judge(done) == Verdict::Replaced);
        CHECK(Has(done, "quick blow LOST - re-armed by the attack path's start at frame 2000 before the blow's frame 2001"));
        CHECK(Has(done, "path start@+0 (pending +1, set +1, after +1)") && Has(done, "0x7A@+1"));
        CHECK(!r.DueByFrame(2016, &done));   // (once only)
    }
    // ---- a re-arm AFTER the blow fired: the blow counts ----
    {
        Recorder r;
        Attack done;
        r.Scheduled(7, true, 4, 2, 2, 3000, 3001, 3014, 0, &done);
        r.Triggered(1, 3001, 7, true);
        r.Rearmed(Who::PathStep, 3002, 3014, 3003, 3003);
        CHECK(r.DueByFrame(3020, &done) && Judge(done) == Verdict::Fired);
    }
    // ---- the scheduler runs for a walk before the end, no blow: the mode changed ----
    {
        Recorder r;
        Attack done;
        r.Scheduled(7, true, 4, 2, 2, 4000, 4001, 4014, 0, &done);
        r.GateAsked(2, 4000, 4, 0);
        CHECK(r.Scheduled(2, false, 4, kNotJudged, -1, 4000, 4005, 4005, 10, &done));
        CHECK(Judge(done) == Verdict::ModeChanged && done.replacedByMode && done.newMode == 2);
        CHECK(Has(done, "the mode changed to 2 at frame 4000") && Has(done, "mode 2 asked @+0: game 4, ours 0"));
        CHECK(!r.Now().active);
        CHECK(!r.DueByFrame(5000, &done));   // (a walk is no attack: nothing open)
    }
    // ---- no trigger at all, ended (by our clock: 13 frames x 40 ms + slack) ----
    {
        Recorder r;
        Attack done;
        r.Scheduled(7, true, 4, 2, 2, 5000, 5001, 5013, 1000, &done);
        CHECK(!r.DueByClock(1000 + 13 * 40 + 100, 300, &done));
        CHECK(r.DueByClock(1000 + 13 * 40 + 301, 300, &done));
        CHECK(Judge(done) == Verdict::Ended && Has(done, "the animation ended with no trigger at all") && Has(done, "triggers: none"));
    }
    // ---- 0.206.7's line: nothing queued by the scheduler (pending 0) is said so, not as a frame ----
    {
        Recorder r;
        Attack done;
        r.Scheduled(7, true, 4, 1, 2, 375, 0, 389, 1000, &done);
        CHECK(r.DueByFrame(390, &done) && Has(done, "NO MODECHANGE event queued by the scheduler") && Has(done, "blow moved from frame 3 to the start frame 2"));
        r.Scheduled(7, true, 4, 0, 2, 400, 401, 414, 2000, &done);
        CHECK(r.DueByFrame(415, &done) && Has(done, "blow on the start frame 2 already") && Has(done, "pending for 401 (+1)"));
    }
    // ---- the game's own frames (not moved), the record without an action ----
    {
        Recorder r;
        Attack done;
        r.Scheduled(8, true, 4, kNotJudged, -1, 6000, 6008, 6014, 0, &done);
        r.Triggered(1, 6008, 8, true);
        CHECK(r.DueByFrame(6015, &done) && Has(done, "the game's own frames") && Judge(done) == Verdict::Fired);
        r.Scheduled(7, true, 4, kNoAction, 2, 7000, 7014, 7014, 0, &done);
        CHECK(r.DueByFrame(7015, &done) && Judge(done) == Verdict::NoBlowInRecord && Has(done, "no blow in this animation's record"));
    }
    // ---- caps: more triggers, re-arms and gate asks than kept are counted ----
    {
        Recorder r;
        Attack done;
        r.Scheduled(7, true, 4, 2, 2, 8000, 8001, 8014, 0, &done);
        for (int i = 0; i < kTriggers + 3; ++i) r.Triggered(kTickCode, 8001 + i, 7, false);
        for (int i = 0; i < kRearms + 2; ++i) r.Rearmed(Who::PathStep, 8002 + i, 8003 + i, 8003 + i, 8003 + i);
        for (int i = 0; i < kGates + 1; ++i) r.GateAsked(2, 8002 + i, 4, 4);
        CHECK(r.DueByFrame(8015, &done));
        CHECK(done.nTrig == kTriggers && done.moreTrig == 3 && done.nRe == kRearms && done.moreRe == 2 && done.nGate == kGates && done.moreGate == 1);
        CHECK(Has(done, " +3") && Has(done, " +2") && Has(done, " +1"));
        char tiny[40];
        const int n = Describe(done, tiny, sizeof tiny);   // a short buffer: cut, never overrun
        CHECK(n < (int)sizeof tiny && tiny[sizeof tiny - 1] == '\0' || std::strlen(tiny) < sizeof tiny);
    }
    // ---- nothing open: the hooks do nothing ----
    {
        Recorder r;
        Attack done;
        r.Triggered(1, 10, 7, true);
        r.Rearmed(Who::PathStep, 10, 11, 11, 11);
        r.GateAsked(2, 10, 4, 0);
        CHECK(!r.DueByFrame(100, &done) && !r.DueByClock(100000, 0, &done) && !r.Now().active);
    }
    if (g_failures) { std::printf("quick_blow_probe_test: %d of %d FAILED\n", g_failures, g_checks); return 1; }
    std::printf("quick_blow_probe_test: all %d checks passed\n", g_checks);
    return 0;
}
