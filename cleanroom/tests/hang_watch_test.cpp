// hang_watch_test.cpp - the hang watchdog's rule (vr/diag/hang_watch.h): when the stacks are written, when the minidump, and that a long
// frame that comes back is said to have come back. No game.
//   hang_watch_test.exe
#include "diag/hang_watch.h"

#include <cstdio>

using namespace hangwatch;

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

// Looks every 500 ms from `fromMs` to `toMs` with the count held at `presents`; the acts seen, in order, into `out` (Act::None left out).
static int Run(Watch& w, uint32_t presents, double fromMs, double toMs, bool paused, Act* out, int most) {
    int n = 0;
    for (double t = fromMs; t <= toMs; t += 500.0) {
        const Act a = Step(w, presents, t, paused);
        if (a != Act::None && n < most) out[n++] = a;
    }
    return n;
}

int main() {
    {   // nothing before the game's first present, however long (the loader's start, the title's first load)
        Watch w;
        Act acts[4];
        CHECK(Run(w, 0, 0.0, 120000.0, false, acts, 4) == 0);
        CHECK(!w.started);
        CHECK(Step(w, 1, 120500.0, false) == Act::None);   // the first present: watching from now
        CHECK(w.started);
    }
    {   // presents coming: never anything
        Watch w;
        Act acts[4];
        int n = 0;
        for (uint32_t i = 1; i < 2000; ++i)
            if (Step(w, i, i * 11.1, false) != Act::None) ++n;
        CHECK(n == 0);
        (void)acts;
    }
    {   // rivas_10 0.197.0: presents stop for good - the stacks at 5 s, the minidump at 15 s, once each
        Watch w;
        CHECK(Step(w, 100, 1000.0, false) == Act::None);
        Act acts[4];
        const int n = Run(w, 100, 1500.0, 70000.0, false, acts, 4);
        CHECK(n == 2);
        CHECK(acts[0] == Act::Stacks);
        CHECK(acts[1] == Act::Dump);
        Watch x;
        Step(x, 100, 1000.0, false);
        CHECK(Step(x, 100, 5999.0, false) == Act::None);
        CHECK(Step(x, 100, 6000.0, false) == Act::Stacks);
        CHECK(Step(x, 100, 15999.0, false) == Act::None);
        CHECK(Step(x, 100, 16000.0, false) == Act::Dump);
        CHECK(Step(x, 100, 60000.0, false) == Act::None);
    }
    {   // a 7 s loading frame: the stacks, then Back with how long it stood; the next stall is a new one
        Watch w;
        Step(w, 50, 0.0, false);
        CHECK(Step(w, 50, 5000.0, false) == Act::Stacks);
        CHECK(Step(w, 51, 7000.0, false) == Act::Back);
        CHECK(w.stoodMs == 7000.0);
        CHECK(Step(w, 51, 11999.0, false) == Act::None);
        CHECK(Step(w, 51, 12000.0, false) == Act::Stacks);
    }
    {   // a 4 s frame: nothing written, nothing said
        Watch w;
        Step(w, 7, 0.0, false);
        CHECK(Step(w, 7, 4000.0, false) == Act::None);
        CHECK(Step(w, 8, 4100.0, false) == Act::None);
    }
    {   // minimized: no presents is no hang; the wait starts again when the window is back
        Watch w;
        Step(w, 9, 0.0, false);
        Act acts[4];
        CHECK(Run(w, 9, 500.0, 60000.0, true, acts, 4) == 0);
        CHECK(Step(w, 9, 64999.0, false) == Act::None);
        CHECK(Step(w, 9, 65000.0, false) == Act::Stacks);
    }
    {   // minimized after the stacks: no minidump while minimized, and the stall is not forgotten
        Watch w;
        Step(w, 9, 0.0, false);
        CHECK(Step(w, 9, 5000.0, false) == Act::Stacks);
        CHECK(Step(w, 9, 20000.0, true) == Act::None);
        CHECK(Step(w, 9, 20500.0, false) == Act::Dump);
    }
    {   // a minidump of 0 ms: off - the stacks only
        Watch w;
        Limits l;
        l.dumpMs = 0.0;
        Step(w, 3, 0.0, false, l);
        CHECK(Step(w, 3, 5000.0, false, l) == Act::Stacks);
        CHECK(Step(w, 3, 900000.0, false, l) == Act::None);
    }
    {   // the count wraps (uint32): still a change
        Watch w;
        Step(w, 0xFFFFFFFFu, 0.0, false);
        CHECK(Step(w, 0u, 100.0, false) == Act::None);
        CHECK(Step(w, 0u, 5100.0, false) == Act::Stacks);
    }
    std::printf("%d of %d checks passed\n", g_n - g_fail, g_n);
    return g_fail ? 1 : 0;
}
