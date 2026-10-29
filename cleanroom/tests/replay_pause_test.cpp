// replay_pause_test - vr/replay_pause.h: the replay runs only after 240 settled pairs in a loaded area; a menu, a biome set or a
// game frame of over 150 ms between pairs starts the settle again - test-0.170's portal: the area loaded inside one 421 ms frame
// with the world flags never off.
#include "replay_pause.h"

#include <cstdio>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

using replaypause::Settle;
using replaypause::Why;

// n pairs 8.33 ms apart from *t; returns whether the last one may replay
static bool Pairs(Settle& s, int n, double* t, uint32_t area = 0, bool inWorld = true, Why* lastStop = nullptr) {
    bool on = false;
    for (int i = 0; i < n; ++i) {
        Why w;
        *t += 8333.0;
        on = s.Step(area, *t, inWorld, &w);
        if (lastStop && w != Why::kNone) *lastStop = w;
    }
    return on;
}

int main() {
    {   // settles after 240 pairs, not before
        Settle s; double t = 0;
        CHECK(!Pairs(s, 239, &t));
        CHECK(Pairs(s, 1, &t));
        CHECK(Pairs(s, 1000, &t));
    }
    {   // a menu: off at once, 240 pairs again after it
        Settle s; double t = 0;
        Pairs(s, 300, &t);
        Why w; t += 8333;
        CHECK(!s.Step(0, t, false, &w) && w == Why::kNoWorld);
        CHECK(!Pairs(s, 239, &t));
        CHECK(Pairs(s, 1, &t));
    }
    {   // the portal: a biome set with the world flags on - off at once, the reason told once
        Settle s; double t = 0;
        Pairs(s, 300, &t);
        Why w; t += 8333;
        CHECK(!s.Step(1, t, true, &w) && w == Why::kArea);
        t += 8333;
        CHECK(!s.Step(1, t, true, &w) && w == Why::kNone);   // the same area: settling, nothing more to tell
        CHECK(!Pairs(s, 238, &t, 1));
        CHECK(Pairs(s, 1, &t, 1));
    }
    {   // the portal's 421 ms frame without a biome set (a load seen only by its length)
        Settle s; double t = 0;
        Pairs(s, 300, &t);
        Why w; t += 421000;
        CHECK(!s.Step(0, t, true, &w) && w == Why::kLongFrame);
        CHECK(!Pairs(s, 239, &t));
        CHECK(Pairs(s, 1, &t));
    }
    {   // a 100 ms hitch is no load: the replay stays on
        Settle s; double t = 0;
        Pairs(s, 300, &t);
        Why w; t += 100000;
        CHECK(s.Step(0, t, true, &w) && w == Why::kNone);
    }
    {   // biome sets while still settling (several during one load) restart the count without telling
        Settle s; double t = 0;
        Pairs(s, 100, &t);
        Why w; t += 8333;
        CHECK(!s.Step(5, t, true, &w) && w == Why::kNone);
        CHECK(!Pairs(s, 239, &t, 5));
        CHECK(Pairs(s, 1, &t, 5));
    }
    {   // what kept it off, counted: two biome sets and a long frame while settling, then the count from 0
        Settle s; double t = 0;
        Pairs(s, 10, &t);
        Why w; t += 8333;
        s.Step(1, t, true, &w);
        t += 8333;
        s.Step(2, t, true, &w);
        t += 200000;
        s.Step(2, t, true, &w);
        uint32_t nw = 9, ar = 9, lf = 9;
        s.TakeRestarts(&nw, &ar, &lf);
        CHECK(nw == 0 && ar == 2 && lf == 1);
        s.TakeRestarts(&nw, &ar, &lf);
        CHECK(nw == 0 && ar == 0 && lf == 0);
    }
    {   // the same biome set again is no new area; another one, or none known yet, is
        CHECK(!replaypause::BiomeIsNew("act2_sewer", "act2_sewer"));
        CHECK(replaypause::BiomeIsNew("act2_town", "act2_sewer"));
        CHECK(replaypause::BiomeIsNew("", "act2_town"));
        CHECK(replaypause::BiomeIsNew(nullptr, "act2_town"));
    }
    {   // the first pair ever: no gap to judge
        Settle s;
        Why w;
        CHECK(!s.Step(0, 5e9, true, &w) && w == Why::kNone);
    }

    if (g_fail) { std::printf("replay_pause_test: %d FAILED\n", g_fail); return 1; }
    std::printf("replay_pause_test: 240 settled pairs before a replay; a menu, a biome set or a frame over 150 ms starts it again "
                "(the 0.170 portal's 421 ms load with the world flags on): all passed\n");
    return 0;
}
