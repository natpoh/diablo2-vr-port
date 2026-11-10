// [hud] flat_map_zoom (vr/flat_map_zoom.h): Serg's steps - 0 the round map as before, -1 out of the circle at the game's own zoom, -2.. the
// game draws it smaller an eighth of zoom a step down to its floor, + back into the circle; the zoom given to the game's scale call, the keys'
// and the wheel's directions, when a press is the zoom's; and the whole (unclipped) map's lookup (map_turn.h RoundAt).
#include "flat_map_zoom.h"
#include "map_turn.h"

#include <cmath>
#include <cstdio>

namespace {
int g_fails = 0;
void Check(bool ok, const char* what) {
    if (!ok) { ++g_fails; std::printf("FAIL: %s\n", what); }
}
bool Near(float a, float b) { return std::fabs(a - b) < 1e-5f; }
}   // namespace

int main() {
    using namespace flatzoom;
    {   // the steps: 0 in the circle, -1 out of it at the game's own zoom, then smaller
        Check(InCircle(0) && !InCircle(-1) && !InCircle(-4), "0 in the circle, -1 and less out of it");
        Check(!Steers(0) && !Steers(-1) && Steers(-2) && Steers(-4), "0 and -1 leave the game's zoom alone, -2.. steer it");
        Check(Steers(1) && Steers(4) && InCircle(1) && InCircle(4), "+1..+4 steer it, nearer, still in the circle");
        Check(Near(Arg(0.5f, 1), 0.625f) && Near(Arg(0.5f, 2), 0.75f) && Near(Arg(0.5f, 4), 1.0f), "+1 0.625, +2 0.75, +4 1.0 (twice its own)");
        Check(Near(Arg(1.5f, 4), mapzoom::kGameMax), "nearer: never over our ceiling");
        Check(Near(Times(0.5f, 4), 2.0f), "+4: twice the game's own size");
        Check(Arg(0.5f, 0) == 0.5f && Arg(0.5f, -1) == 0.5f && Arg(0.43f, -1) == 0.43f, "0, -1: the game's own zoom as it came");
        Check(Near(Arg(0.5f, -2), 0.375f) && Near(Arg(0.5f, -3), 0.25f) && Near(Arg(0.5f, -4), 0.125f), "-2 0.375, -3 0.25, -4 0.125 (the floor)");
        Check(Near(Arg(0.5f, -99), 0.125f) && Near(Arg(0.5f, 99), 1.0f), "a level past the range: kept in it");
        Check(Near(Arg(0.25f, -4), 0.125f) && Near(Arg(1.0f, -3), 0.75f), "another own zoom: an eighth a step, never under the floor");
        Check(Near(Arg(0.52f, -2), 0.375f), "the game's own rounded as it rounds it first");
        Check(Arg(0.0f, -3) == 0.0f && Arg(9.0f, -3) == 9.0f && std::isnan(Arg(NAN, -3)), "a zoom of the game's out of 0.05 - 4: untouched");
        for (int s = kMinSteps; s <= kMaxSteps; ++s) {
            const float z = Arg(0.5f, s), prev = Arg(0.5f, s + 1);
            if (std::fabs(z * 8.0f - std::round(z * 8.0f)) > 1e-4f) { Check(false, "every level a whole eighth (the game draws those)"); break; }
            if (s < kMaxSteps && z > prev) { Check(false, "each - never draws it bigger"); break; }
        }
        Check(Near(Times(0.5f, -4), 0.25f) && Near(Times(0.5f, -1), 1.0f) && Near(Times(0.5f, 0), 1.0f), "times the game's own size");
    }
    {   // the level after presses: - down to the least, + back into the circle and no further
        int s = 0;
        s = Stepped(s, -1);
        Check(s == -1 && !InCircle(s), "one -: out of the circle");
        for (int i = 0; i < 40; ++i) s = Stepped(s, -1);
        Check(s == kMinSteps, "pressed - on and on: the smallest");
        for (int i = 0; i < 3; ++i) s = Stepped(s, 1);
        Check(s == -1 && !InCircle(s), "+ back the same steps: the whole box again");
        s = Stepped(s, 1);
        Check(s == 0 && InCircle(s), "one more +: into the circle");
        Check(Stepped(s, 1) == 1, "+ in the circle: nearer");
        for (int i = 0; i < 40; ++i) s = Stepped(s, 1);
        Check(s == kMaxSteps && kMaxSteps == 4, "pressed + on and on: the nearest (+4)");
        s = Stepped(s, -1);
        Check(s == 3, "- from there: back a step");
        Check(Stepped(-2, 0) == -2, "no direction: the same");
        Check(Clamped(50) == kMaxSteps && Clamped(-50) == kMinSteps && Clamped(-2) == -2, "a level from the ini: kept in the range");
    }
    {   // the level kept (Keeper) - Serg's 0.202.2 run: zoomed out to -4, 1.5 s later the save; the ini still says 0 until our write lands
        Keeper k;
        Check(k.Ini(0) && k.Live() == 0, "keeper: the start takes the ini's 0");
        for (int i = 0; i < 4; ++i) k.Press(-1);
        Check(k.Live() == -4, "keeper: four - presses: -4");
        k.Writing(-4);   // the save starts
        Check(!k.Ini(0) && k.Live() == -4, "keeper: the old 0 seen again while our write is under way: -4 stays (the 0.202.2 snap back)");
        Check(!k.Ini(-4) && k.Live() == -4, "keeper: our own write coming back: -4");
        // a press while the next save is under way: our older write landing late must not take the newer press back
        k.Writing(-4);
        Check(k.Press(1) == -3 && k.Live() == -3, "keeper: + during a save: -3");
        Check(!k.Ini(-4) && k.Live() == -3, "keeper: the older write landing after it: -3 stays");
        // Settings (or a hand edit) sets another number: theirs
        Check(k.Ini(-2) && k.Live() == -2, "keeper: Settings sets -2: taken");
        Check(k.Ini(5) && k.Live() == kMaxSteps && k.Ini(-50) && k.Live() == kMinSteps, "keeper: a number past the range: kept in it");
        Check(!k.Ini(-50) && k.Live() == kMinSteps, "keeper: the same number again: nothing");
        Check(k.Press(-1) == kMinSteps, "keeper: - at the smallest: stays");
    }
    {   // the keys and the wheel
        Check(KeyDir(kVkOemPlus) == 1 && KeyDir(kVkAdd) == 1, "+ (main row, numpad): back toward the circle");
        Check(KeyDir(kVkOemMinus) == -1 && KeyDir(kVkSubtract) == -1, "- (main row, numpad): out and smaller");
        Check(KeyDir('W') == 0 && KeyDir(0x0D) == 0 && KeyDir(0x70) == 0, "other keys: not ours");
        // keys set in the game's panel ([hud] flat_map_zoom_in_key / _out_key)
        Check(KeyDir(0x21, 0x21, 0x22) == 1 && KeyDir(0x22, 0x21, 0x22) == -1, "keys of the player's own: Page Up nearer, Page Down farther");
        Check(KeyDir(kVkAdd, 0x21, 0x22) == 0 && KeyDir(kVkOemMinus, 0x21, 0x22) == 0, "... then + / - (and the numpad's) are the game's");
        Check(KeyDir(kVkAdd, kDefaultInKey, 0x22) == 1 && KeyDir(kVkSubtract, kDefaultInKey, 0x22) == 0, "the numpad's twin only with the default key");
        Check(KeyDir(0, 0, 0) == 0 && KeyDir(kVkOemPlus, 0, 0) == 0, "no keys: nothing is ours");
        Check(KeyOf(0x21, kDefaultInKey) == 0x21 && KeyOf(0, kDefaultInKey) == 0 && KeyOf(-3, kDefaultInKey) == kDefaultInKey &&
              KeyOf(999, kDefaultInKey) == kDefaultInKey, "a key from the ini: its code, 0 none, nonsense the default");
        Check(WheelDir(120, true) == 1 && WheelDir(-240, true) == -1, "Ctrl + wheel: away +, back -");
        Check(WheelDir(120, false) == 0 && WheelDir(0, true) == 0, "the wheel without Ctrl: the game's");
    }
    {   // when a press is the zoom's
        Check(Takes(true, true, true, true, false, false), "flat, in a game, the map open: ours");
        Check(!Takes(false, true, true, true, false, false), "the key off: the game's");
        Check(!Takes(true, false, true, true, false, false), "VR: the game's (its map has the laser's zoom)");
        Check(!Takes(true, true, false, true, false, false), "not in a game: the game's");
        Check(!Takes(true, true, true, false, false, false), "the map shut: the game's");
        Check(!Takes(true, true, true, true, true, false), "the chat line open: a letter");
        Check(!Takes(true, true, true, true, false, true), "a panel up: the game's");
    }
    {   // out of the circle: the whole box turned, past the circle's edge, nothing off the box (a 640 x 400 corner box at 1920 x 1080)
        const float sw = 1920.0f, sh = 1080.0f, w = 640.0f, h = 400.0f;
        const float box[4] = {1240.0f / sw, 50.0f / sh, 1880.0f / sw, 450.0f / sh};
        int lost = 0, past = 0, off = 0;
        for (int deg = 0; deg < 360; deg += 30) {
            const mapturn::Round r = mapturn::RoundOnScreen(mapturn::For(false, true, true, (float)deg * mapturn::kPi / 180.0f), box, w, h);
            float u, v;
            if (!mapturn::RoundAt(r, r.centre[0], r.centre[1], &u, &v, true)) ++lost;   // the hero: on the map
            // past the circle's edge (1.4 radii up): out of the circle there is map there, in it none
            const float py = r.centre[1] - 1.4f * r.radius[1];
            if (mapturn::RoundAt(r, r.centre[0], py, &u, &v, false)) ++past;
            if (mapturn::RoundAt(r, r.centre[0], py, &u, &v, true) && (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)) ++off;
            if (mapturn::RoundAt(r, r.centre[0] + 50.0f * r.radius[0], r.centre[1], &u, &v, true)) ++off;   // far off the box: nothing
        }
        Check(lost == 0, "whole: the hero on the map at every turn");
        Check(past == 0, "in the circle: nothing past its edge");
        Check(off == 0, "whole: only the box is read, nothing far off it");
        const mapturn::Round r = mapturn::RoundOnScreen(mapturn::For(false, true, true, 0.0f), box, w, h);
        float u, v;
        int reached = 0;   // out of the circle, north up: somewhere past the circle's edge the box still shows
        for (int a = 0; a < 360; a += 10) {
            const float ca = std::cos((float)a * mapturn::kPi / 180.0f), sa = std::sin((float)a * mapturn::kPi / 180.0f);
            reached += mapturn::RoundAt(r, r.centre[0] + 1.3f * r.radius[0] * ca, r.centre[1] + 1.3f * r.radius[1] * sa, &u, &v, true) ? 1 : 0;
        }
        Check(reached > 0, "whole: the box shows past the circle's edge");
    }
    std::printf(g_fails ? "flat_map_zoom_test: %d failed\n" : "flat_map_zoom_test: all passed\n", g_fails);
    return g_fails ? 1 : 0;
}
