// [input] vr_body_pick, vr_body_pick_hand (vr/vr_body_pick.h): which ray each view's pick goes along (flat, VR F1 - F3 through the pointer's pixel,
// F4's shift + click along the hand, the game's own camera: none), and F4's hand - what it meets first, and which of the controller's lookups
// that answers (an attack: a hostile monster; interact: an object or an NPC).
#include "vr_body_pick.h"

#include <cmath>
#include <cstdio>

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; }
}

using namespace vrbody;
using flatbody::Cap;
using flatground::kShear;

static void RayTo(const float eye[3], const float p[3], float d[3]) {
    for (int i = 0; i < 3; ++i) d[i] = p[i] - eye[i];
    const float l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    for (int i = 0; i < 3; ++i) d[i] /= l;
}

static MouseAsk Vr(int by, bool seen = true, int view = 2) {
    MouseAsk a;
    a.platform = 1;
    a.view = view;
    a.vrKey = true;
    a.flatKey = false;
    a.pickSeen = seen;
    a.pickBy = by;
    return a;
}

int main() {
    // ---- the mouse point: which ray --------------------------------------------------------------------------------------------------
    {
        MouseAsk flat;
        flat.platform = 0;
        flat.flatKey = true;
        Check(MouseRay(flat) == Ray::Pixel, "flat, flat_body_pick: the pixel through our camera, as before");
        flat.pickSeen = false;   // flat never asked who answered: unchanged
        flat.pickBy = 0;
        Check(MouseRay(flat) == Ray::Pixel, "flat: d2rcam's last pick plays no part");
        flat.flatKey = false;
        flat.vrKey = true;
        Check(MouseRay(flat) == Ray::None, "flat, flat_body_pick unticked: nothing, whatever vr_body_pick says");
    }
    Check(MouseRay(Vr(1, true, 1)) == Ray::Pixel && MouseRay(Vr(1, true, 2)) == Ray::Pixel && MouseRay(Vr(1, true, 3)) == Ray::Pixel,
          "VR F1 - F3, the pointer or the crosshair through our camera: the pixel");
    Check(MouseRay(Vr(2, true, 4)) == Ray::Aim, "VR F4, shift + click along the hand (the ray override answered): the hand's ray");
    Check(MouseRay(Vr(1, true, 4)) == Ray::None, "VR F4, the desktop cursor's pixel: nothing (no aim of the player's)");
    Check(MouseRay(Vr(1, true, 0)) == Ray::None, "VR, the view not known: nothing");
    Check(MouseRay(Vr(0)) == Ray::None, "the game's own camera answered (its own view): nothing of ours");
    Check(MouseRay(Vr(1, false)) == Ray::None, "the last pick was not this call's: nothing");
    {
        MouseAsk a = Vr(1);
        a.vrKey = false;
        a.flatKey = true;
        Check(MouseRay(a) == Ray::None, "VR, vr_body_pick unticked: nothing, whatever flat_body_pick says");
        a = Vr(2);
        a.menu = true;
        Check(MouseRay(a) == Ray::None, "a menu open (the laser on a panel): nothing");
        a = Vr(1);
        a.platform = 2;
        Check(MouseRay(a) == Ray::None, "an unknown platform: nothing");
    }
    Check(SamePick(640.0f, 360.0f, 640.2f, 359.9f, 3), "the same pixel a moment ago: this call's pick");
    Check(!SamePick(640.0f, 360.0f, 700.0f, 360.0f, 3), "another pixel: not");
    Check(!SamePick(640.0f, 360.0f, 640.0f, 360.0f, 500), "the same pixel half a second ago: not");

    // ---- VR F2: the third-person camera's crosshair through a monster's chest -----------------------------------------------------------
    {
        const float floor = 3.0f;   // a raised floor: the capsules stand where it is drawn
        Cap caps[2];
        caps[0] = {130.0f, 100.0f, floor, 9.0f, 2.1f, true, 1, 11, false};
        caps[1] = {60.0f, 100.0f, floor, 7.0f, 1.8f, true, 0, 0, false};    // a body behind the camera: never met
        const float cam[3] = {80.0f + floor * kShear, floor + 14.0f, 100.0f + floor * kShear};   // behind and above the hero
        float base[3], chest[3], d[3];
        flatbody::Base(caps[0], base);
        chest[0] = base[0] - 2.0f; chest[1] = base[1] + 5.5f; chest[2] = base[2];
        RayTo(cam, chest, d);
        flatbody::Hit hit;
        Check(flatbody::Pick(caps, 2, cam, d, INFINITY, &hit) && hit.index == 0, "F2's crosshair on its chest: the monster");
        const float tFloor = FloorT(cam, d, floor);
        Check(std::isfinite(tFloor) && tFloor > hit.t, "the floor lies beyond the chest along that ray");
        // the crosshair on the ground at its feet, a cell short: the floor first, nothing of ours (the game's own feet test stays)
        const float feet[3] = {base[0] - 4.0f, floor, base[2]};
        RayTo(cam, feet, d);
        Check(!flatbody::Pick(caps, 2, cam, d, FloorT(cam, d, floor), &hit), "the ground short of it: nothing (the game's own)");
    }

    // ---- VR F4: the hand -------------------------------------------------------------------------------------------------------------------
    // Game units: a player 1.75 m standing, the hero ~7 units tall (4 a metre). The hand at chest height a little forward and right of the eyes.
    const float floor = 0.0f, upm = 4.0f;
    const float hand[3] = {100.0f + 0.3f * upm, floor + 1.3f * upm, 100.0f + 0.2f * upm};
    Cap caps[5];
    caps[0] = {116.0f, 100.0f, floor, 8.0f, 2.0f, true, 1, 21, false};    // a zombie 4 m ahead (+x)
    caps[1] = {126.0f, 100.0f, floor, 8.0f, 2.0f, true, 2, 22, false};    // another behind it
    caps[2] = {100.0f, 120.0f, floor, 5.5f, 3.6f, false, 3, 23, false};   // a chest 5 m to the side (+z)
    caps[3] = {85.0f, 115.0f, floor, 9.0f, 2.0f, true, 4, 24, true};      // an NPC behind and to the side
    caps[4] = {100.0f, 140.0f, floor, 9.0f, 2.0f, true, 5, 25, false};    // a monster behind the chest (+z, farther)
    // the server: the zombies hostile and clear, the NPC not, the one behind the chest not known yet
    auto verdict = [](const Cap& c) { return c.id == 21 || c.id == 22 ? 1 : c.id == 24 ? 0 : -1; };
    float d[3], p[3], base[3];
    int met = -1;

    flatbody::Base(caps[0], base);   // the zombie's chest
    p[0] = base[0] - 2.0f; p[1] = base[1] + 5.0f; p[2] = base[2];
    RayTo(hand, p, d);
    Check(HandPick(caps, 5, hand, d, floor, Ask::Attack, verdict, &met) && met == 0, "the hand on the zombie's chest: the attack goes to it");
    Check(!HandPick(caps, 5, hand, d, floor, Ask::Interact, verdict, &met) && met == 0, "... interact: not to a hostile monster (the game's own)");
    p[1] = base[1] + 7.6f;   // its head, over the one behind
    RayTo(hand, p, d);
    Check(HandPick(caps, 5, hand, d, floor, Ask::Attack, verdict, &met) && met == 0, "its head: still the nearer one");

    flatbody::Base(caps[2], base);   // the chest's lid
    p[0] = base[0]; p[1] = base[1] + 3.0f; p[2] = base[2] - 3.0f;
    RayTo(hand, p, d);
    Check(HandPick(caps, 5, hand, d, floor, Ask::Interact, verdict, &met) && met == 2, "the hand on the chest: interact goes to it");
    Check(!HandPick(caps, 5, hand, d, floor, Ask::Attack, verdict, &met) && met == 2, "... an attack: never an object (the game's own)");

    flatbody::Base(caps[3], base);   // the NPC
    p[0] = base[0] + 1.5f; p[1] = base[1] + 5.0f; p[2] = base[2];
    RayTo(hand, p, d);
    Check(HandPick(caps, 5, hand, d, floor, Ask::Interact, verdict, &met) && met == 3, "the hand on an NPC: interact (talk) goes to it");
    Check(!HandPick(caps, 5, hand, d, floor, Ask::Attack, verdict, &met), "... an attack: never an NPC");
    {
        Cap npcHostile = caps[3];
        npcHostile.id = 21;   // an NPC the server calls hostile (a quest NPC turned): not one to talk to
        Check(!Answers(Ask::Interact, npcHostile, 1) && !Answers(Ask::Attack, npcHostile, 1), "an NPC called hostile: neither (the game's own)");
    }

    flatbody::Base(caps[4], base);   // the monster behind the chest, not known yet: neither, and pointing through the chest is the chest
    p[0] = base[0]; p[1] = base[1] + 5.0f; p[2] = base[2] - 2.0f;
    RayTo(hand, p, d);
    Check(!HandPick(caps, 5, hand, d, floor, Ask::Attack, verdict, &met), "through the chest at a monster behind it: no attack target");
    Check(met == 2, "... what the hand meets first is the chest");
    Check(!Answers(Ask::Attack, caps[4], -1) && Answers(Ask::Attack, caps[4], 1) && !Answers(Ask::Attack, caps[4], 0),
          "an attack: only on the server's verdict hostile and clear");

    // the hand hanging down: the floor first, nothing
    p[0] = hand[0] + 1.0f; p[1] = floor; p[2] = hand[2];
    RayTo(hand, p, d);
    Check(!HandPick(caps, 5, hand, d, floor, Ask::Attack, verdict, &met) && met == -1, "the hand pointing at the floor: nothing (the game's auto-target)");
    // pointing up into the air: nothing, and no floor either
    p[0] = hand[0] + 5.0f; p[1] = hand[1] + 10.0f; p[2] = hand[2] - 5.0f;
    RayTo(hand, p, d);
    Check(!std::isfinite(FloorT(hand, d, floor)) && HandMet(caps, 5, hand, d, floor) == -1, "the air: no floor, nothing met");

    // the interact skill asks as interact; any other as an attack
    Check(AskOf(357) == Ask::Interact && AskOf(0) == Ask::Attack && AskOf(-1) == Ask::Attack, "the lookup's kind from the controller's skill");
    {
        HandAsk h;
        h.f4 = h.key = h.on = true;
        Check(HandWanted(h), "F4, ticked, the mod on, no menu: the hand answers");
        h.menu = true;
        Check(!HandWanted(h), "a menu open (the laser's): not");
        h.menu = false;
        h.f4 = false;
        Check(!HandWanted(h), "F1 - F3: not (the pad's own there)");
        h.f4 = true;
        h.key = false;
        Check(!HandWanted(h), "vr_body_pick_hand unticked: not");
    }

    // ---- flat: the map click's target ([input] flat_body_target) ------------------------------------------------------------------------
    {
        FlatAsk a;
        a.flat = a.key = a.on = true;
        Check(FlatWanted(a), "flat, ticked, in a game area, no menu: the click's target is the crosshair's");
        a.menu = true;
        Check(!FlatWanted(a), "a menu open: not");
        a.menu = false;
        a.flat = false;
        Check(!FlatWanted(a), "VR: not (its own picks)");
        a.flat = true;
        a.key = false;
        Check(!FlatWanted(a), "flat_body_target unticked: not");
    }
    {
        float px[2];
        Check(FlatPixel(true, 1, 10, 300.0f, 200.0f, 1920.0f, 1080.0f, true, px) && px[0] == 300.0f && px[1] == 200.0f,
              "a fresh pick of ours: the game's own pixel");
        Check(FlatPixel(true, 1, 5000, 300.0f, 200.0f, 1920.0f, 1080.0f, true, px) && px[0] == 960.0f && px[1] == 540.0f,
              "an old pick, mouse look on: the crosshair, the viewport's middle");
        Check(!FlatPixel(true, 1, 5000, 300.0f, 200.0f, 1920.0f, 1080.0f, false, px), "an old pick, a free cursor: nothing");
        Check(!FlatPixel(true, 0, 10, 300.0f, 200.0f, 1920.0f, 1080.0f, true, px), "the game's own camera answered: nothing of ours");
        Check(!FlatPixel(false, 1, 10, 300.0f, 200.0f, 1920.0f, 1080.0f, true, px), "no pick known: nothing");
    }
    {   // the player's case: first person, a monster right in front, the crosshair on its chest
        const float floorY = 0.0f;
        Cap m;   // a zombie-sized body 3 world units ahead along +x (a cell and a half)
        m.x = 3.0f; m.z = 0.0f; m.floorY = floorY; m.height = 7.0f; m.radius = 1.8f; m.body = true; m.id = 7;
        Cap farther = m;   // and one farther on, on the same line
        farther.x = 14.0f; farther.id = 8;
        const Cap two[2] = {farther, m};
        const float eye[3] = {0.0f, 6.0f, 0.0f};
        float q[3] = {3.0f, 4.0f, 0.0f}, dir[3];   // its chest
        RayTo(eye, q, dir);
        float fl[3];   // where the ray lands on the floor without the body
        Check(flatground::PlaneHit(eye, dir, floorY, fl) && fl[0] > 8.0f, "up close the chest's ray reaches the floor well behind the monster (the old miss)");
        Check(ClickMet(two, 2, eye, dir, floorY) == 1, "... but it meets the near monster's body first: that one is the click's target");
        q[0] = 3.0f; q[1] = 20.0f;   // looking up over it
        RayTo(eye, q, dir);
        Check(ClickMet(two, 2, eye, dir, floorY) == -1, "looking up over them: nothing (the game's own answer)");
        q[0] = 1.0f; q[1] = 0.0f; q[2] = 6.0f;   // the floor beside it
        RayTo(eye, q, dir);
        Check(ClickMet(two, 2, eye, dir, floorY) == -1, "the floor beside it: nothing");
        // the eye inside a big body (a monster pressed against the hero): only while looking towards it
        Cap big = m;
        big.x = 1.0f; big.radius = 3.0f; big.height = 12.0f;
        float toward[3] = {1.0f, -0.2f, 0.0f}, away[3] = {-1.0f, -0.2f, 0.0f};
        for (float* v : {toward, away}) {
            const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            for (int i = 0; i < 3; ++i) v[i] /= l;
        }
        Check(ClickMet(&big, 1, eye, toward, floorY) == 0, "inside a big body looking at it: that one");
        Check(ClickMet(&big, 1, eye, away, floorY) == -1, "inside it looking away: not it");
    }
    {
        Cap mon; mon.body = true; mon.npc = false;
        Cap npc; npc.body = true; npc.npc = true;
        Cap door; door.body = false;
        Check(ClickAnswers(mon, -1, false) && ClickAnswers(mon, 1, false), "a monster, verdict unknown or hostile: the click's target");
        Check(!ClickAnswers(mon, 0, false), "a monster the server calls not hostile (a hireling, a summon) or walled off: the game's own");
        Check(ClickAnswers(npc, -1, false) && ClickAnswers(npc, 0, false), "an NPC: the click talks to it, as the game's click does");
        Check(ClickAnswers(door, -1, false), "an object (a door, a chest): the click's target");
        Check(!ClickAnswers(mon, 1, true) && !ClickAnswers(door, -1, true), "the game's own answer an item: kept (a pick-up stays a pick-up)");
    }

    if (g_fail) { std::printf("vr_body_pick_test: %d FAILED\n", g_fail); return 1; }
    std::printf("vr_body_pick_test: all passed\n");
    return 0;
}
