// vr/shot_trace.h: the window of a shot ([debug] shot_trace) and the texts of its lines. No game, no clock: the milliseconds are given.
#include <cstdio>
#include <cstring>

#include "../../vr/shot_trace.h"

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++g_fail; }
}

int main() {
    using namespace shottrace;

    // -- a shot's window: opens at the press, closes 1.5 s after it is let go
    {
        Window w;
        bool fresh = true;
        Check(!w.Step(false, 100, &fresh) && !fresh && !w.Open(), "nothing pressed: closed");
        Check(w.Step(true, 1000, &fresh) && fresh && w.Shot() == 1, "a press opens shot 1");
        Check(w.SinceMs(1042) == 42 && w.SinceMs(900) == 0, "milliseconds since the press (never negative)");
        Check(w.Step(true, 1050, &fresh) && !fresh && w.Shot() == 1, "held: the same shot");
        Check(w.Step(false, 1100, &fresh) && !fresh, "let go: still watched (the tail)");
        Check(w.Step(false, 1100 + kTailMs - 1, &fresh), "one ms before the end of the tail: open");
        Check(!w.Step(false, 1100 + kTailMs, &fresh) && !w.Open(), "the tail is over: closed");
        Check(w.Step(true, 5000, &fresh) && fresh && w.Shot() == 2 && w.SinceMs(5010) == 10, "the next press is shot 2");
        // a press in the tail goes on with the same shot
        Check(w.Step(false, 5100, &fresh) && w.Step(true, 5600, &fresh) && !fresh && w.Shot() == 2, "pressed again in the tail: the same shot goes on");
        Check(w.Step(false, 5700, &fresh) && w.Step(false, 5700 + kTailMs - 1, &fresh), "and its tail starts over from the new release");
        Check(!w.Step(false, 5700 + kTailMs, &fresh), "closed after the new tail");
    }

    // -- the cap of lines for one shot, and of shots for a session
    {
        Window w;
        w.Step(true, 0);
        bool last = false;
        uint32_t n = 0, lastAt = 0;
        while (w.TakeLine(&last)) { ++n; if (last) lastAt = n; }
        Check(n == kMaxLines && lastAt == kMaxLines, "kMaxLines lines a shot, the last one flagged");
        w.Step(false, 10);
        w.Step(false, 10 + kTailMs);
        w.Step(true, 5000);
        Check(w.TakeLine(), "a new shot has its lines again");
        Window s;
        uint64_t t = 0;
        uint32_t opened = 0;
        for (uint32_t i = 0; i < kMaxShots + 5; ++i) {
            bool fresh = false;
            if (s.Step(true, t, &fresh) && fresh) ++opened;
            t += 10;
            s.Step(false, t);
            t += kTailMs;
            s.Step(false, t);
            t += 10;
        }
        Check(opened == kMaxShots, "kMaxShots shots a session");
        s.Reset();
        bool fresh = false;
        Check(s.Step(true, 0, &fresh) && fresh && s.Shot() == 1, "Reset starts over");
    }

    // -- the texts
    {
        Check(std::strcmp(CommandName(3), "run to a point") == 0 && std::strcmp(CommandName(1), "walk to a point") == 0, "the funnel's walks");
        Check(std::strcmp(CommandName(0x42), "the stick's direction") == 0, "the stick");
        Check(std::strstr(CommandName(5), "skill") != nullptr && std::strstr(CommandName(0x50), "another") != nullptr, "skills and the rest");
        Check(std::strcmp(UnitKindName(1), "monster") == 0 && std::strcmp(UnitKindName(4), "item") == 0 && std::strcmp(UnitKindName(9), "unit?") == 0, "the unit kinds");
        Check(std::strcmp(ModeName(1), "neutral") == 0 && std::strcmp(ModeName(10), "cast") == 0 && std::strcmp(ModeName(19), "knocked back") == 0 &&
                  std::strcmp(ModeName(99), "?") == 0, "the hero's modes");
        Check(ClickStandStill(0x0001) && !ClickStandStill(0x0100) && ClickRun(0x0100) && !ClickRun(0x0001), "the click's flag bytes: Stand Still low, Run high");
        Check(!ClickStandStill(0xDEAD0000ull) && !ClickRun(0xDEAD0000ull), "garbage above the word is not a flag");
        Check(std::strcmp(ClickTypeName(7), "pad cast, press (hand 1)") == 0 && std::strcmp(ClickTypeName(2), "left release") == 0 &&
                  std::strcmp(ClickTypeName(99), "another type") == 0, "the click types");
        char b[128];
        CommandFlagsText(0xA5, b, sizeof b);
        Check(std::strstr(b, "0xA5") && std::strstr(b, "[Stand Still]") && std::strstr(b, "[sent]") && !std::strstr(b, "[Run]"), "the command's flags: Stand Still and sent");
        CommandFlagsText(0x45, b, sizeof b);
        Check(std::strstr(b, "[Run]") && !std::strstr(b, "[Stand Still]"), "the command's flags: Run, no Stand Still");
        void* frames[3] = {(void*)0x7FF600101F78ull, (void*)0x7FF600102A95ull, (void*)0x7FFA11112222ull};
        const uintptr_t base = 0x7FF600000000ull;
        FramesText(frames, 3, base, 0x4000000, b, sizeof b);
        Check(std::strcmp(b, "0x101F78 <- 0x102A95 <- ?7FFA11112222") == 0, "frames as RVAs, a foreign one as it is");
        char tiny[12];
        FramesText(frames, 3, base, 0x4000000, tiny, sizeof tiny);
        Check(std::strlen(tiny) < sizeof tiny, "a small buffer is never overrun");
    }

    if (g_fail) { std::fprintf(stderr, "%d check(s) failed\n", g_fail); return 1; }
    std::puts("shot_trace_test: all passed");
    return 0;
}
