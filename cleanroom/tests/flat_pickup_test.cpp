// [input] flat_pickup_key_on / flat_item_hitbox (vr/flat_pickup.h): which item the crosshair's ray meets by its hitbox, the key's nearest item
// ahead, the key's choice, the Alt rule and the key's press (aim, the button down and always up).
#include "flat_pickup.h"

#include <cmath>
#include <cstdio>

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; }
}

using namespace flatpickup;
using flatground::kShear;

static void RayTo(const float eye[3], const float p[3], float d[3]) {
    for (int i = 0; i < 3; ++i) d[i] = p[i] - eye[i];
    const float l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    for (int i = 0; i < 3; ++i) d[i] /= l;
}

int main() {
    Check(KeyOf('E') == 'E' && KeyOf('F') == 'F' && KeyOf(0x20) == 0x20 && KeyOf(0) == 'E' && KeyOf(-3) == 'E' && KeyOf('W') == 'E' && KeyOf(0x12) == 'E',
          "the key: its code; none, W (walks) or Alt (the names): E");

    for (const float floor : {0.0f, 6.0f, -4.0f}) {   // base ground, a ledge, a pit
        const float eye[3] = {100.0f + floor * kShear, floor + 5.3f, 100.0f + floor * kShear};
        Item items[3];
        items[0] = {11, 109.0f, 101.0f, floor, 4.5f};   // 4.5 cells ahead (+x)
        items[1] = {12, 115.0f, 101.0f, floor, 7.5f};   // further along
        items[2] = {13, 101.0f, 115.0f, floor, 7.5f};   // off to the side (+z)
        auto at = [&](const Item& it, float up, float p[3]) { p[0] = it.x + floor * kShear; p[1] = floor + up; p[2] = it.z + floor * kShear; };
        float p[3], d[3], t = 0.0f;
        at(items[0], 0.3f, p);
        RayTo(eye, p, d);
        Check(RayItem(items, 3, eye, d, INFINITY, &t) == 0 && t > 0.0f, "the crosshair on the near item: it");
        at(items[1], 0.3f, p);
        RayTo(eye, p, d);
        Check(RayItem(items, 3, eye, d, INFINITY) == 1, "the crosshair on the far one: the far one (the near one is not in the way)");
        Check(RayItem(items, 3, eye, d, 3.0f) == -1, "a body met first (t 3): no item");
        at(items[2], 0.3f, p);
        RayTo(eye, p, d);
        Check(RayItem(items, 3, eye, d, INFINITY) == 2, "the item off to the side");
        p[0] = 109.0f + floor * kShear; p[1] = floor; p[2] = 106.0f + floor * kShear;   // the floor 2.5 cells beside the near item
        RayTo(eye, p, d);
        Check(RayItem(items, 3, eye, d, INFINITY) == -1, "the floor beside an item: none (only its hitbox)");
        p[0] = 109.0f + floor * kShear; p[1] = floor + 30.0f; p[2] = 101.0f + floor * kShear;   // up into the sky over it
        RayTo(eye, p, d);
        Check(RayItem(items, 3, eye, d, INFINITY) == -1, "looking up past it: none");
    }

    {   // the key's nearest item ahead
        Item items[4];
        items[0] = {21, 106.0f, 100.0f, 0.0f, 3.0f};   // 3 cells ahead (+x)
        items[1] = {22, 104.0f, 104.0f, 0.0f, 2.8f};   // 2.8 cells at 45 deg
        items[2] = {23, 96.0f, 100.0f, 0.0f, 2.0f};    // 2 cells behind
        items[3] = {24, 100.5f, 100.2f, 0.0f, 0.3f};   // under the feet
        Check(AheadItem(items, 3, 100.0f, 100.0f, 1.0f, 0.0f, 5.0f) == 1, "ahead: the nearer one inside the cone (45 deg) - not the one behind");
        Check(AheadItem(items, 3, 100.0f, 100.0f, 1.0f, -1.0f, 5.0f) == 0, "turned 45 deg the other way: the one 90 deg off is out, the one at 45 in");
        Check(AheadItem(items, 3, 100.0f, 100.0f, -1.0f, 0.0f, 5.0f) == 2, "turned round: the one that was behind");
        Check(AheadItem(items, 3, 100.0f, 100.0f, 1.0f, 0.0f, 2.5f) == -1, "reach 2.5 cells: none ahead");
        Check(AheadItem(items, 4, 100.0f, 100.0f, 0.0f, -1.0f, 5.0f) == 3, "an item under the feet counts whatever the heading");
        Check(AheadItem(items, 3, 100.0f, 100.0f, 0.0f, 0.0f, 5.0f) == -1, "no heading: none");
    }

    Check(ChooseKey(true, true, true) == From::Hover && ChooseKey(false, true, true) == From::Crosshair && ChooseKey(false, false, true) == From::Ahead &&
          ChooseKey(false, false, false) == From::None, "the key: the hovered name, the crosshair's item, the nearest ahead, in that order");
    Check(HitboxAnswers(true, true, true, false), "Alt held, an item met, the game hovers no item: ours");
    Check(!HitboxAnswers(true, true, true, true), "the game's own hover is an item (its name under the crosshair): the game's");
    Check(!HitboxAnswers(true, false, true, false) && !HitboxAnswers(false, true, true, false) && !HitboxAnswers(true, true, false, false),
          "no Alt, the box off, nothing met: the game's");

    {   // the press: aimed, at least 50 ms, then down, 60 ms, up
        Machine m;
        In in;
        in.wanted = in.focused = true;
        in.lookupHooked = true;
        in.target = 77;
        in.now = 1000;
        Out o = m.Step(in);
        Check(!o.aim && !o.down, "no press: nothing");
        in.pressed = true;
        o = m.Step(in);
        Check(o.aim && o.id == 77 && !o.down, "pressed: aimed at the item, no button yet");
        in.pressed = false;
        in.target = 78;   // the crosshair moves on: the aimed one stays
        in.now = 1030;
        o = m.Step(in);
        Check(o.aim && o.id == 77 && !o.down, "30 ms: still aiming the same item");
        in.now = 1060;
        o = m.Step(in);
        Check(o.down && o.aim && o.id == 77, "50 ms on: the button down, the lookup still answered");
        in.now = 1100;
        o = m.Step(in);
        Check(!o.up && m.ButtonDown(), "40 ms down: still down");
        in.now = 1125;
        o = m.Step(in);
        Check(o.up && o.aim, "60 ms: up, still answering a moment");
        in.now = 1300;
        o = m.Step(in);
        Check(!o.aim && m.stage() == Machine::Stage::Idle, "then idle");
    }
    {   // not hooked: the game's hover must come to it, else no click
        Machine m;
        In in;
        in.wanted = in.focused = in.pressed = true;
        in.target = 5;
        in.now = 0;
        m.Step(in);
        in.pressed = false;
        in.now = 100;
        Check(!m.Step(in).down, "not hooked, no hover yet: no click");
        in.hoverIsTarget = true;
        in.now = 200;
        Check(m.Step(in).down, "the hover on it: clicked");
        Machine m2;
        in.hoverIsTarget = false;
        in.pressed = true;
        in.now = 0;
        m2.Step(in);
        in.pressed = false;
        in.now = 800;
        const Out o = m2.Step(in);
        Check(!o.down && !o.aim && o.said, "700 ms without the hover: given up, said why");
    }
    {   // gone, unwanted, a press with nothing: no button; a button that went down comes up when no longer wanted
        Machine m;
        In in;
        in.wanted = in.focused = in.pressed = in.lookupHooked = true;
        in.target = 0;
        Out o = m.Step(in);
        Check(!o.aim && o.said, "nothing to take: nothing aimed, said");
        in.target = 9;
        in.focused = false;
        o = m.Step(in);
        Check(!o.aim, "the window not in front: nothing");
        in.focused = true;
        m.Step(in);
        in.pressed = false;
        in.gone = true;
        in.now = 60;
        o = m.Step(in);
        Check(!o.aim && !o.down, "the item taken meanwhile: no click");
        in.gone = false;
        in.pressed = true;
        in.now = 100;
        m.Step(in);
        in.pressed = false;
        in.now = 160;
        Check(m.Step(in).down, "down");
        in.wanted = false;   // a menu opened
        in.now = 170;
        o = m.Step(in);
        Check(o.up, "no longer wanted with the button down: it comes up at once");
    }

    if (g_fail) { std::printf("%d failure(s)\n", g_fail); return 1; }
    std::printf("flat_pickup_test: all passed\n");
    return 0;
}
