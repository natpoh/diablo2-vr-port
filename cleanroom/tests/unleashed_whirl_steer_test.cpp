// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed commit 09c3093ae312eeb6549a2302f76fa461e3dff35e by Berk (bearstonem)
// (https://github.com/bearstonem/d2r-pcvr/commit/09c3093ae312eeb6549a2302f76fa461e3dff35e), its steamvr/whirl_steer_test.cpp, for
// D2R VR Authentic's port (vr/unleashed_whirl_steer.h, [gameplay] whirlwind_stick_steer): the dead zone's, the way's and the turned view's
// checks kept; the T's and Unleashed's reach checks left out (not taken); the way through Authentic's own grid turn and point
// (vr/aim_point.h GridDir, Point) and Fresh's checks added. Stays MPL 2.0.
#include "unleashed_whirl_steer.h"
#include "aim_point.h"
#include <cmath>
#include <cstdio>
using namespace unleashed::whirlsteer;
static int g_fail = 0;
static void Check(bool ok, const char* why) {
    if (!ok) { std::printf("FAIL: %s\n", why); ++g_fail; }
}
static bool Near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }
// The view's level right and ahead in the world for a heading h (0 = -z ahead, growing as the view turns left).
static void View(float h, float right[3], float ahead[3]) {
    ahead[0] = -std::sin(h); ahead[1] = 0; ahead[2] = -std::cos(h);
    right[0] = std::cos(h);  right[1] = 0; right[2] = -std::sin(h);
}

int main() {
    {   // The game's own dead zone: steering begins where walking does.
        Check(!Pushed(0, 0) && !Pushed(.31f, 0) && !Pushed(0, -.31f) && !Pushed(.22f, .22f), "inside the dead zone: at rest");
        Check(Pushed(.32f, 0) && Pushed(0, -.5f) && Pushed(.25f, .25f) && Pushed(1, 1), "past it: pushed, any way");
        Check(!Pushed(NAN, .8f) && !Pushed(.8f, INFINITY), "nonsense is at rest");
        Check(Near(DeadZone * 32767.f, 10349.f, .01f), "the dead zone is the game's 10349 of 32767");
    }
    float r[3], a[3];
    View(0, r, a);
    {   // At rest: no way, the outputs untouched - the cast aims as before.
        float wx = 7, wz = 7;
        Check(!GroundWay(.1f, .2f, r, a, &wx, &wz) && wx == 7 && wz == 7, "a stick at rest gives no way, the outputs untouched");
        Check(!GroundWay(1, 0, nullptr, a, &wx, &wz) && !GroundWay(1, 0, r, a, nullptr, &wz), "no frame or no output: no way");
        const float zero[3] = {0, 0, 0};
        Check(!GroundWay(1, 0, zero, zero, &wx, &wz), "a frame with no level way: no way");
    }
    {   // Facing -z: ahead goes -z, right goes +x, back +z; a unit way however far the push.
        float wx = 0, wz = 0;
        Check(GroundWay(0, 1, r, a, &wx, &wz) && Near(wx, 0) && Near(wz, -1), "pushed ahead: where you face");
        Check(GroundWay(1, 0, r, a, &wx, &wz) && Near(wx, 1) && Near(wz, 0), "pushed right: to your right");
        Check(GroundWay(0, -.6f, r, a, &wx, &wz) && Near(wx, 0) && Near(wz, 1), "pulled back: behind you, a unit way");
        Check(GroundWay(-.5f, .5f, r, a, &wx, &wz) && Near(wx, -.70711f) && Near(wz, -.70711f), "ahead and left: halfway between");
    }
    {   // A tilted view frame (camF with some pitch left in): only the level way counts.
        const float rt[3] = {1, 0, 0}, at[3] = {0, -.5f, -.8660254f};
        float wx = 0, wz = 0;
        Check(GroundWay(0, 1, rt, at, &wx, &wz) && Near(wx, 0) && Near(wz, -1), "a pitched ahead still whirls level, at full length");
    }
    {   // Turned: the way turns with the view, as the walk does, for every heading and push; through the grid turn and the
        // point (Authentic's aimpoint, uncalibrated = the world's x and z) the spot is AheadCells cells out that way.
        for (int d = 0; d < 360; d += 15)
            for (int s = 0; s < 360; s += 30) {
                const float h = d * 3.1415927f / 180, ang = s * 3.1415927f / 180, x = .8f * std::sin(ang), y = .8f * std::cos(ang);
                View(h, r, a);
                float wx = 0, wz = 0;
                Check(GroundWay(x, y, r, a, &wx, &wz), "a push past the dead zone always has a way");
                const float ex = r[0] * x + a[0] * y, ez = r[2] * x + a[2] * y, en = std::hypot(ex, ez);
                Check(Near(wx, ex / en) && Near(wz, ez / en) && Near(std::hypot(wx, wz), 1), "the way is the walk's own, level, unit");
                float gx = 0, gy = 0, px = 0, py = 0;
                aimpoint::GridDir(std::atan2(wz, wx), true, false, false, 0, &gx, &gy);
                Check(aimpoint::Point(5000, 4000, gx, gy, AheadCells, &px, &py), "a point along the way");
                Check(Near(std::hypot(px - 5000, py - 4000), AheadCells, 1e-2f), "AheadCells out");
                Check(Near((px - 5000) / AheadCells, wx, 1e-3f) && Near((py - 4000) / AheadCells, wz, 1e-3f), "the grid's x and z are the world's");
            }
    }
    {   // Left-handed: the swap happens before the stick reaches here (the right stick walks) - the way is the stick's whichever thumb pushed it.
        View(0, r, a);
        float wx = 0, wz = 0;
        Check(GroundWay(-1, 0, r, a, &wx, &wz) && Near(wx, -1) && Near(wz, 0), "pushed left: to your left");
    }
    {   // The whirl's length and the skill.
        Check(AheadCells == 10.f && kWhirlwind == 151, "10 cells, skill 151");
        float px = 0, py = 0;
        Check(aimpoint::Point(3, 4000, -1, 0, AheadCells, &px, &py) == false, "at the map's edge: no point, the game's own stays");
    }
    {   // Fresh: the stick of the last quarter second, never from the future, never unread.
        Check(Fresh(1000, 1000, 250) && Fresh(1000, 1250, 250), "read now or 250 ms ago: fresh");
        Check(!Fresh(1000, 1251, 250), "older: stale");
        Check(!Fresh(1000, 999, 250) && !Fresh(0, 100, 250), "from the future, or never read: stale");
    }
    if (g_fail) { std::printf("unleashed_whirl_steer_test: %d FAILED\n", g_fail); return 1; }
    std::puts("unleashed_whirl_steer_test: ok");
    return 0;
}
