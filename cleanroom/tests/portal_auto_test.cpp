// [input] portal_look_horizon and portal_auto (vr/portal_auto.h, from sajmonsoft's flat fork): an arrival told from a walk, a border and a
// Teleport; the walk into a portal - aim, the game's hover, one click, released - and no bounce back through the portal one came out of.
#include "portal_auto.h"

#include <cstdio>

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; }
}

using namespace portalauto;

int main() {
    // ---- arrival ----
    {
        Arrival a;
        uint64_t t = 1000;
        float x = 100.0f;
        bool any = false;
        for (int i = 0; i < 300; ++i, t += 16, x += 0.5f) any = a.Step(t, 1, true, x, 50.0f) || any;   // walking
        Check(!any, "a walk is no arrival");
        any = false;
        for (int i = 0; i < 10; ++i, t += 2500) any = a.Step(t, 2 + (uint32_t)i, true, x += 0.5f, 50.0f) || any;   // the town's biome set every 2.5 s
        Check(!any, "biome sets while walking (town, a border) are no arrival");
        Arrival b;
        t = 1000;
        b.Step(t, 1, true, 10.0f, 10.0f);
        Check(!b.Step(t += 16, 1, true, 400.0f, 10.0f), "a long jump with no biome set (Teleport) is no arrival");
        b.Step(t += 16, 2, true, 400.0f, 10.0f);              // a waypoint: the biome set when the load starts
        for (int i = 0; i < 100; ++i) b.Step(t += 60, 2, false, 0.0f, 0.0f);   // the area loads: no place
        Check(b.Step(t += 16, 2, true, 5000.0f, 9000.0f), "carried thousands of units 6 s after the set: an arrival");
        Check(!b.Step(t += 16, 2, true, 5000.5f, 9000.0f), "told once");
        b.Step(t += 16, 3, true, 5001.0f, 9000.0f);
        Check(!b.Step(t + 21000, 3, true, 9000.0f, 9000.0f), "a jump over 20 s after the set: not this arrival");
    }
    Check(HorizonMousePitch(60.0f, -60.0f) == 0.0f && HorizonMousePitch(60.0f, -30.0f) == -30.0f, "the horizon: the game's 60 down plus the setting cancelled");

    // ---- the walk into a portal ----
    {
        Machine m;
        In in;
        in.wanted = true; in.moving = true; in.areaGen = 1; in.heroOk = true; in.radius = 4.0f; in.delayMs = 150;
        in.portal = true; in.portalX = 200.0f; in.portalZ = 300.0f;
        uint64_t t = 1000;
        auto at = [&](float heroX, float dist) { in.heroX = heroX; in.heroZ = 300.0f; in.dist = dist; };
        // standing by the portal at the start (as after arriving): not entered until walked away
        at(198.0f, 2.0f);
        Out o;
        for (int i = 0; i < 200; ++i) { in.now = t += 16; o = m.Step(in); Check(!o.force && !o.down, "at the start next to a portal: nothing (no bounce)"); }
        at(180.0f, 20.0f);
        in.now = t += 16; m.Step(in);
        Check(m.armed(), "walked away: armed");
        // walking in: near for 150 ms, then the point on the portal
        at(197.0f, 3.0f);
        in.now = t += 16; o = m.Step(in);
        Check(!o.force, "just near: not yet");
        in.now = t += 160; o = m.Step(in);
        Check(o.force && o.forceX == 200.0f && o.forceZ == 300.0f && !o.down, "near 150 ms: the game's mouse point on the portal");
        in.now = t += 16; o = m.Step(in);
        Check(o.force && !o.down, "no click until the game's hover says portal");
        in.hoverIsPortal = true;
        in.now = t += 16; o = m.Step(in);
        Check(o.down && o.force && m.stage() == Machine::Stage::Down, "the hover is the portal: the button down");
        in.now = t += 30; o = m.Step(in);
        Check(!o.up && o.force, "held a moment");
        in.now = t += 40; o = m.Step(in);
        Check(o.up && !o.force && !m.armed(), "60 ms later up, and not again until walked away");
        in.hoverIsPortal = false;
        // the new area: next to the portal one came out of
        in.areaGen = 2; at(5000.0f, 2.0f); in.portalX = 5002.0f; in.portalZ = 300.0f;
        for (int i = 0; i < 200; ++i) { in.now = t += 16; o = m.Step(in); Check(!o.force && !o.down, "arrived next to a portal: no bounce back"); }
        // the hover never comes: no click, and a pause
        at(5030.0f, 28.0f); in.now = t += 16; m.Step(in);
        at(5000.0f, 2.0f);
        in.now = t += 16; m.Step(in);
        in.now = t += 200; o = m.Step(in);
        Check(o.force, "aiming again");
        in.now = t += 800; o = m.Step(in);
        Check(!o.force && !o.down && !m.armed(), "no hover in 700 ms: given up, no click");
        // a button down when the mode is left: it comes up
        Machine k;
        In j = in;
        j.areaGen = 9; j.heroX = 0.0f; j.dist = 50.0f; j.now = 100; k.Step(j);
        j.now = 2000; k.Step(j);   // settled, away: armed
        j.dist = 1.0f; j.now = 2016; k.Step(j);
        j.now = 2200; k.Step(j);
        j.hoverIsPortal = true; j.now = 2216; o = k.Step(j);
        Check(o.down, "down");
        j.wanted = false; j.now = 2220; o = k.Step(j);
        Check(o.up && !o.force, "the mode left with the button down: it comes up");
        // not walking: never
        Machine s;
        In w = j;
        w.wanted = true; w.moving = false; w.hoverIsPortal = true; w.areaGen = 3; w.dist = 50.0f; w.now = 100; s.Step(w);
        w.now = 2000; s.Step(w);
        w.dist = 1.0f;
        bool clicked = false;
        for (int i = 0; i < 100; ++i) { w.now += 16; clicked = s.Step(w).down || clicked; }
        Check(!clicked, "standing by a portal without a move key: no click");
    }

    if (g_fail) { std::printf("portal_auto_test: %d FAILED\n", g_fail); return 1; }
    std::printf("portal_auto_test: all passed\n");
    return 0;
}
