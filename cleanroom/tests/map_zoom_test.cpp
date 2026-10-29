// [hud] map_stick_zoom (vr/map_zoom.h): the stick's push on the map, who has the stick, the game's zoom steps and what the picture magnifies.
#include "map_zoom.h"

#include <cmath>
#include <cstdio>

namespace {
int g_fails = 0;
void Check(bool ok, const char* what) {
    if (!ok) { ++g_fails; std::printf("FAIL: %s\n", what); }
}
bool Near(float a, float b, float tol = 1e-3f) { return std::fabs(a - b) <= tol; }
}   // namespace

int main() {
    using namespace mapzoom;
    {   // the game's rounding and the step below a zoom wanted
        Check(Near(GameRounded(0.5f), 0.5f) && Near(GameRounded(0.06f), 0.125f) && Near(GameRounded(0.56f), 0.5f), "the game's own rounding");
        Check(Near(GameStep(0.7f), 0.625f) && Near(GameStep(0.1f), 0.125f) && Near(GameStep(1.0f), 1.0f), "the whole step at or below");
        Check(Near(Remainder(0.7f, 0.625f), 1.12f) && Near(Remainder(0.5f, 0.625f), 1.0f) && Near(Remainder(NAN, 0.5f), 1.0f), "the rest magnified, never under 1");
        Check(Near(GameArg(0.5f, 0.0f), 0.5f), "not asked: the game's own");
        Check(Near(GameArg(0.5f, 0.9f), 0.875f), "asked: the step for it");
        Check(Near(GameArg(0.5f, 9.0f), 2.0f) && Near(GameArg(0.5f, 0.01f), 0.125f), "asked past the ends: held to 0.125 - 2");
        Check(Near(GameArg(77.0f, 0.9f), 77.0f), "a call whose own zoom is not a zoom: left alone");
    }
    {   // the stick
        Stick s;
        Frame f = s.Step(true, 0.0f, 0.2f, 0.02f);
        Check(!f.take && Near(f.factor, 1.0f), "under the dead zone: the game's");
        f = s.Step(true, 0.1f, 1.0f, 0.1f);
        Check(f.take && Near(f.factor, std::exp2(0.15f)), "up, full push on the map: zoom in, the stick the zoom's");
        f = s.Step(true, 0.0f, -1.0f, 0.1f);
        Check(f.take && f.factor < 1.0f, "down: zoom out");
        f = s.Step(false, 0.9f, 0.5f, 0.1f);
        Check(f.take && Near(f.factor, 1.0f), "the laser off the map mid-push: still the zoom's (no turn), no zoom");
        f = s.Step(false, 0.05f, 0.05f, 0.1f);
        Check(!f.take && !s.Held(), "back to the middle: the game's again");
        f = s.Step(true, 1.0f, 0.4f, 0.1f);
        Check(!f.take, "mostly sideways on the map: the turn's, not the zoom's");
        f = s.Step(false, 0.0f, 0.0f, 0.1f);
        f = s.Step(false, 0.0f, 1.0f, 0.1f);
        Check(!f.take, "a push off the map: the game's");
        f = s.Step(true, 0.0f, 1.0f, 0.1f);
        Check(!f.take, "that push held onto the map: still the game's (it began elsewhere)");
        f = s.Step(true, 0.0f, 0.0f, 0.1f);
        f = s.Step(true, 0.0f, 1.0f, 0.1f);
        Check(f.take, "centred, then pushed on the map: the zoom's");
        Check(Near(Clamp(9.0f, kHandMin, kHandMax), 4.0f) && Near(Clamp(NAN, kHandMin, kHandMax), 1.0f), "the hand map's zoom 1 - 4");
    }
    std::printf(g_fails ? "map_zoom_test: %d failed\n" : "map_zoom_test: all passed\n", g_fails);
    return g_fails ? 1 : 0;
}
