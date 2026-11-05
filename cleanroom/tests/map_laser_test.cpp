// [hud] map_click_big (vr/map_laser.h): the laser's ray on the map in the hand (flat and round), and the click's states - open on the
// map, the button flips the turn, any other press puts it back, the trigger taken only when the press starts on the map.
#include "map_laser.h"

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
    using namespace maplaser;
    // a map 0.2 x 0.1 m, 0.5 m ahead along -z, facing +z (the viewer)
    Quad q;
    q.at = {0.0f, 0.0f, -0.5f}; q.right = {1.0f, 0.0f, 0.0f}; q.up = {0.0f, 1.0f, 0.0f}; q.front = {0.0f, 0.0f, 1.0f};
    q.w = 0.2f; q.h = 0.1f; q.ok = true;
    {
        const Hit h = RayQuad({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, q);
        Check(h.hit && Near(h.t, 0.5f) && Near(h.u, 0.5f) && Near(h.v, 0.5f), "straight at its middle");
        const Hit k = RayQuad({0.09f, 0.04f, 0.0f}, {0.0f, 0.0f, -1.0f}, q);
        Check(k.hit && Near(k.u, 0.95f) && Near(k.v, 0.1f), "near its top right corner: u right, v down");
        Check(!RayQuad({0.11f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, q).hit, "past its right edge: no hit");
        Check(!RayQuad({0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f}, q).hit, "from behind: no hit");
        Check(!RayQuad({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, q).hit, "pointing away: no hit");
        Check(!RayQuad({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, q, 0.3f).hit, "farther than the reach: no hit");
        Quad off = q; off.ok = false;
        Check(!RayQuad({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, off).hit, "a quad not hung: no hit");
    }
    {   // the orb: only its ball (0.1 m across in a 0.16 m quad)
        Quad o = q; o.w = o.h = 0.16f; o.round = true; o.ball = 0.1f;
        Check(RayQuad({0.04f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, o).hit, "inside the ball");
        Check(!RayQuad({0.045f, 0.045f, 0.0f}, {0.0f, 0.0f, -1.0f}, o).hit, "in the quad's corner, off the ball: no hit");
    }
    {   // the click: open on the map, rotate on the button, close anywhere else
        Clicker c;
        Frame f = c.Step(true, true, false, false, false);
        Check(f.claim && f.beam && f.event == Event::None, "pointing at the map: the trigger is the laser's, the beam shows");
        f = c.Step(true, true, false, false, true);
        Check(f.event == Event::Open && c.Big() && f.claim, "a press on the map: big");
        f = c.Step(true, false, false, false, true);
        Check(f.event == Event::None && c.Big(), "held: nothing more");
        f = c.Step(true, false, false, false, false);
        Check(f.claim && f.beam, "big: the trigger stays the laser's anywhere");
        f = c.Step(true, false, true, true, true);
        Check(f.event == Event::Rotate && c.Big(), "a press on the button: rotate, still big");
        c.Step(true, false, true, true, false);
        f = c.Step(true, false, true, false, true);
        Check(f.event == Event::Close && !c.Big(), "a press on the big map: back to the hand");
        c.Step(true, true, false, false, false);   // (let go on the map: the trigger the laser's again)
        c.Step(true, true, false, false, true);
        Check(c.Big(), "opened again");
        c.Step(true, false, false, false, false);
        f = c.Step(true, false, false, false, true);
        Check(f.event == Event::Close && !c.Big(), "a press pointing away: back to the hand");
    }
    {   // the trigger: a press that began off the map stays the game's, even when the laser then comes onto the map
        Clicker c;
        Frame f = c.Step(true, false, false, false, true);
        Check(!f.claim && f.event == Event::None, "a press off the map: the game's (an attack)");
        f = c.Step(true, true, false, false, true);
        Check(!f.claim && f.event == Event::None && !c.Big(), "held onto the map: still the game's, nothing opens");
        f = c.Step(true, true, false, false, false);
        Check(f.claim, "let go on the map: now the laser's");
        f = c.Step(true, true, false, false, true);
        Check(f.event == Event::Open, "and a fresh press opens");
    }
    {   // not allowed (a menu opened, F4 left, the key unticked): back to the hand, nothing claimed once let go
        Clicker c;
        c.Step(true, true, false, false, false);
        c.Step(true, true, false, false, true);
        Frame f = c.Step(false, false, false, false, true);
        Check(f.event == Event::Close && !c.Big(), "not allowed while big: closed");
        Check(f.claim, "the held press stays the laser's until let go");
        f = c.Step(false, true, false, false, false);
        Check(!f.claim && !f.beam, "let go: nothing taken, no beam");
    }
    {   // the front / hand button while big: flips where it hangs, stays big; any other press still closes
        Clicker c;
        c.Step(true, true, false, false, false, false);
        c.Step(true, true, false, false, false, true);
        c.Step(true, false, true, false, false, false);
        Frame f = c.Step(true, false, true, false, true, true);
        Check(f.event == Event::Front && c.Big(), "a press on the front button: Front, still big");
        c.Step(true, false, true, false, true, false);
        f = c.Step(true, false, false, false, true, true);
        Check(f.event == Event::Front && c.Big(), "again (off the big map, on the button): Front");
        c.Step(true, false, false, false, false, false);
        f = c.Step(true, false, false, false, false, true);
        Check(f.event == Event::Close && !c.Big(), "a press away from both: closed");
    }
    {   // [hud] map_two_sizes: where the map is after each press, as xr.cpp keeps it (the front flag cleared at Open, flipped by Front;
        // the front button's hit counts only while FrontButton). 0 the orb in the hand, 1 the big map on the left hand, 2 the big map in front.
        struct Map {
            Clicker c;
            bool front = false;
            bool onHandKey = true, twoSizes = true;
            int rotates = 0;
            Event Press(bool onMap, bool onBig, bool onButton, bool onFront) {
                const bool fb = onFront && FrontButton(onHandKey, twoSizes);
                c.Step(true, onMap, onBig, onButton, fb, false);   // let go first: who has the trigger
                const Frame f = c.Step(true, onMap, onBig, onButton, fb, true);
                c.Step(true, onMap, onBig, onButton, fb, false);
                if (f.event == Event::Open) front = false;
                if (f.event == Event::Front) front = !front;
                if (f.event == Event::Rotate) ++rotates;
                return f.event;
            }
            int Where() const { return !c.Big() ? 0 : BigOnHand(onHandKey, twoSizes, front) ? 1 : 2; }
        };
        Map m;   // Serg's: map_big_on_hand ticked, two sizes on
        Check(m.Where() == 0, "two sizes: the orb in the hand to start");
        Check(m.Press(true, false, false, false) == Event::Open && m.Where() == 2, "two sizes: a click on the orb - the big map in front at once (no middle size)");
        Check(m.Press(false, true, true, false) == Event::Rotate && m.Where() == 2 && m.rotates == 1, "two sizes: the Rotate plate turns it and keeps it open");
        Check(m.Press(false, false, false, true) == Event::Close && m.Where() == 0,
              "two sizes: where the old button to the hand was - no button counts, so it is a press away: back to the orb");
        m.Press(true, false, false, false);
        Check(m.Press(false, true, false, false) == Event::Close && m.Where() == 0, "two sizes: a click on the big map - small in the orb again");
        Check(m.Press(true, false, false, false) == Event::Open && m.Where() == 2, "two sizes: a click on the orb again - big in front again");
        Check(!FrontButton(true, true) && !BigOnHand(true, true, false), "two sizes: no button to the hand hung, never held on the hand");

        Map old;   // two sizes off: the three steps as before
        old.twoSizes = false;
        Check(old.Press(true, false, false, false) == Event::Open && old.Where() == 1, "three steps: a click on the orb - the big map on the left hand");
        Check(old.Press(false, false, false, true) == Event::Front && old.Where() == 2, "three steps: its button - in front");
        Check(old.Press(false, true, false, true) == Event::Front && old.Where() == 1, "three steps: the button again - back on the hand");
        Check(old.Press(false, true, true, false) == Event::Rotate && old.Where() == 1, "three steps: Rotate keeps it where it is");
        Check(old.Press(false, true, false, false) == Event::Close && old.Where() == 0, "three steps: a click on the big map - the orb");
        Check(old.Press(true, false, false, false) == Event::Open && old.Where() == 1, "three steps: each opening starts on the hand");

        Map plain;   // map_big_on_hand unticked: in front, two sizes or not - as before
        plain.onHandKey = false; plain.twoSizes = false;
        Check(plain.Press(true, false, false, false) == Event::Open && plain.Where() == 2, "no map_big_on_hand: in front as before");
        Check(!FrontButton(false, false) && !FrontButton(false, true), "no map_big_on_hand: no button to the hand either way");
    }
    {   // beside the inventory on the hand: left of the panel, not over it; the button above the big map
        const V3 at{0.0f, 1.2f, -0.4f}, right{1.0f, 0.0f, 0.0f}, up{0.0f, 1.0f, 0.0f};
        const V3 m = BesideLeft(at, right, up, 0.40f, 0.30f, 0.10f, 0.10f);
        Check(Near(m.x, -0.27f) && Near(m.y, 1.2f + 0.10f), "the map's middle 2 cm left of the panel's edge, its top at the panel's top");
        Check(m.x + 0.05f < at.x - 0.20f, "it does not reach over the panel");
        const V3 b = ButtonAbove(at, up, 0.30f, 0.05f);
        Check(Near(b.y, 1.2f + 0.15f + 0.02f + 0.025f) && Near(b.x, 0.0f), "the button over the big map's top edge, centred");
    }
    {   // the big map in front: kept in the room, the lazy follow, the drag, the kept place
        const V3 eyes{0.0f, 1.6f, 0.0f};
        const Spot s = SpotFor(eyes, 0.0f, kDistM, kSideRad, kUpM);
        Check(s.ok && Near(s.at.z, -1.3f) && Near(s.at.y, 1.5f) && Near(s.yaw, 0.0f), "the standard place: 1.3 m ahead, 10 cm under the eyes");
        Check(Near(Follow(s, eyes, 0.5f, 0.016f).at.x, s.at.x), "the gaze 29 deg off: it stays (inside the dead zone)");
        Spot f = s;
        for (int i = 0; i < 300; ++i) f = Follow(f, eyes, 1.5707963f, 0.016f);
        float d, side, up;
        Relative(f, eyes, 1.5707963f, &d, &side, &up);
        Check(Near(d, 1.3f) && Near(up, -0.1f), "followed: its distance and height kept");
        Check(Near(std::fabs(side), 0.61f, 0.02f), "followed: it settles at the dead zone's edge (no snap to the middle)");
        const Spot one = Follow(s, eyes, 1.5707963f, 0.016f);
        Check(std::fabs(YawTo(eyes, one.at)) < 0.05f, "one frame: only a little way round (lazy)");
        Spot h = s;
        h = Follow(h, {0.0f, 1.2f, 0.0f}, 0.0f, 0.016f);
        Check(Near(h.at.y, 1.5f), "the head lower: the map's height stays");
        const Spot dr = Drag({0.2f, 1.4f, 0.0f}, {0.0f, 0.0f, -1.0f}, 2.0f, {0.0f, 0.1f, 0.0f}, eyes);
        Check(Near(dr.at.z, -2.0f) && Near(dr.at.y, 1.5f) && Near(dr.at.x, 0.2f), "dragged: the held point on the ray, the offset kept");
        Relative(dr, eyes, 0.0f, &d, &side, &up);
        Check(InView(d, side, up), "a place dragged ahead: seen");
        Check(!InView(1.3f, 2.5f, 0.0f), "behind and to the side: not seen - the standard place instead");
        Check(!InView(6.0f, 0.0f, 0.0f) && !InView(1.3f, 0.0f, 2.5f), "too far, too high: not seen");
        const Spot back = SpotFor(eyes, 0.0f, d, side, up);
        Check(Near(back.at.x, dr.at.x) && Near(back.at.z, dr.at.z) && Near(back.at.y, dr.at.y), "the kept place opens where it was let go");
        const Place p = PlaceAt(s, 2.0f);
        Check(Near(p.w, 1.2f) && Near(p.h, 0.6f) && p.button.y < p.at.y, "placed at the spot, the button under it");
    }
    {   // where it hangs
        const Place p = PlaceBig({0.0f, 1.6f, 0.0f}, 0.0f, 1.0f);
        Check(Near(p.at.z, -1.3f) && Near(p.at.y, 1.5f) && Near(p.w, 1.2f) && Near(p.h, 1.2f), "1.3 m ahead along -z, 10 cm under the eyes, square");
        Check(p.button.y < p.at.y - 0.5f * p.h && Near(p.button.x, 0.0f), "the button under it, centred");
        const Place r = PlaceBig({0.0f, 1.6f, 0.0f}, 1.5707963f, 2.0f);
        Check(Near(r.at.x, -1.3f, 1e-3f) && Near(r.h, 0.6f), "turned left a quarter: along -x; a wide map half as tall");
    }
    std::printf(g_fails ? "map_laser_test: %d failed\n" : "map_laser_test: all passed\n", g_fails);
    return g_fails ? 1 : 0;
}
