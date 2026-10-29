// The hand orbs (vr/hand_orbs.h and the ORBS block of reshade/D2R_DepthFog.fx) without the game or a headset:
//
//   - where a ball sits: on the back of the wrist, along the forearm and out of the back of the hand, left / right mirrored, both on one wrist
//     side by side, the controller's grip when no arm is posed, bad numbers; life on the left wrist and mana on the right (and swapped, both on one),
//   - the levels eased (snap at the first sight, no overshoot, a long frame, NaN time), the heartbeat (only under the threshold, only with the pulse
//     ticked, only alive, the rhythm of the low-life vignette's gentle beat, fades in and out), the throb of the life ball,
//   - the fade with the distance, the off states (the setting off, the panel look chosen, unknown numbers) producing nothing,
//   - the uniforms: the hand frame into the world (the head's turn-only axes, the units per metre), off balls at opacity 0, the time wrapped,
//   - the effect's own HLSL (the text between the ORBS marks in reshade/D2R_DepthFog.fx) compiled and run on a software D3D11 device (WARP) over a
//     synthetic scene (a forearm and a wall as the game's depth): the fill levels, the liquid's level horizontal in the world (it tilts with a rolled
//     head), life red and mana blue, a nearer surface hiding a ball or cutting one sunk into it, the halo (and not through a nearer surface), the
//     opacity 0 picture bit for bit, parallax between the eyes,
//   - and, given a folder, the pictures to look at: the balls on a forearm at several levels, both eyes, the heartbeat, occluded and sunk.
//
//   hand_orbs_test [<folder for the pictures>]
//
// What it cannot say: how it looks in the lenses and in the game's own light, whether the posed arm's wrist is where this test's forearm is, the
// colours through the game's colour pipeline, how ReShade's own compiler takes the effect (the effect is also run through reshadefx's parser by
// hand, see the note).

#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#include "hand_orbs.h"
#include "d2r_vr_state.h"   // the hero class bits of the state block (shared/)
#include "hud_globes.h"

using Microsoft::WRL::ComPtr;
using namespace handorbs;

namespace {

int g_failed = 0, g_checks = 0;
void Expect(bool ok, const char* fmt, ...) {
    ++g_checks;
    if (ok) return;
    ++g_failed;
    char b[300];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    printf("  FAIL: %s\n", b);
}
bool Near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

// -- a posed arm: the forearm along -z (the hero's arm held out in front), the palm turned down, so the back of the hand is up (+y) --------------------------
Arm ArmAt(V3 elbow, V3 wrist, V3 across) {
    Arm a;
    a.ok = true;
    a.elbow = elbow;
    a.wrist = wrist;
    a.across = across;
    return a;
}
Arm RightArm() { return ArmAt({0.20f, -0.25f, -0.15f}, {0.20f, -0.25f, -0.45f}, {0.0f, -1.0f, 0.0f}); }
Arm LeftArm() { return ArmAt({-0.20f, -0.25f, -0.15f}, {-0.20f, -0.25f, -0.45f}, {0.0f, -1.0f, 0.0f}); }

Config OrbsOn() {
    Config c;
    c.on = true;
    c.style = handvitals::kStyleOrbs;
    return c;
}
Vitals Full() { return handvitals::FromStats(1000 * 256, 1000 * 256, 400 * 256, 400 * 256); }

In Base() {
    In in;
    in.cfg = OrbsOn();
    in.vitalsOk = true;
    in.v = Full();
    in.arm[0] = RightArm();
    in.arm[1] = LeftArm();
    in.time = 100.0;
    return in;
}

// ---- [hud] hand_vitals_orb_on_bone: on the forearm bone's own mesh box ------------------------------------------------------------------------------

// The right arm with its forearm's box: 26 cm along the bone (2 cm short of each joint), 8 cm across, 6 cm deep - the box's sides along the world's axes.
Arm BoxedRight() {
    Arm a = RightArm();
    a.hasBox = true;
    a.boxC = {0.20f, -0.25f, -0.30f};
    a.boxH[0] = {0.04f, 0.0f, 0.0f};
    a.boxH[1] = {0.0f, 0.03f, 0.0f};
    a.boxH[2] = {0.0f, 0.0f, 0.13f};
    return a;
}

void TestOnBone() {
    Config cfg = OrbsOn();
    cfg.orbShape = handvitals::kShapeMedallion;
    cfg.orbSizeCm = 5.0f;   // a base radius of 2.5 cm, its plate 0.5 cm thick
    Place p = OnBone(BoxedRight(), cfg, 0);
    Expect(p.ok && p.fromBone, "a boxed forearm: placed on the bone");
    Expect(Near(p.c.x, 0.20f, 1e-5f) && Near(p.c.y, -0.25f + 0.03f + 0.005f, 1e-5f) && Near(p.c.z, -0.30f - (0.13f - 0.025f), 1e-5f),
           "the medallion's plate on the box's back surface, one radius in from its wrist end (%.4f %.4f %.4f)", p.c.x, p.c.y, p.c.z);
    Expect(Near(p.axis.z, -1.0f, 1e-5f) && Near(p.back.y, 1.0f, 1e-5f), "its frame the forearm's: along the bone, out of the back of the wrist");
    cfg.orbShape = handvitals::kShapeBall;
    p = OnBone(BoxedRight(), cfg, 0);
    Expect(p.ok && Near(p.c.y, -0.25f + 0.03f + 0.025f, 1e-5f), "a full ball touches the surface: one radius out (%.4f)", p.c.y);
    cfg.orbShape = handvitals::kShapeMedallion;
    {   // the forearm twisted a quarter turn, its box with it: the deeper side now out of the back of the wrist
        Arm a = BoxedRight();
        a.boxH[0] = {0.03f, 0.0f, 0.0f};   // (its 8 cm now up and down, its 6 cm across)
        a.boxH[1] = {0.0f, 0.04f, 0.0f};
        p = OnBone(a, cfg, 0);
        Expect(p.ok && Near(p.c.y, -0.25f + 0.04f + 0.005f, 1e-5f), "a box turned with the bone: its own depth on that side (%.4f)", p.c.y);
    }
    {   // a thicker arm (another class, an armour): farther out, by its mesh alone
        Arm a = BoxedRight();
        a.boxH[1] = {0.0f, 0.05f, 0.0f};
        p = OnBone(a, cfg, 0);
        Expect(p.ok && Near(p.c.y, -0.25f + 0.05f + 0.005f, 1e-5f), "a thicker forearm: on its own surface (%.4f)", p.c.y);
    }
    {   // the whole arm moved and turned (held on a body, swung by the game's animation): the medallion goes with the bone
        Arm a = BoxedRight();
        const auto turn = [](V3 v) { return V3{-v.z, v.y, v.x}; };   // a quarter turn about +y
        a.elbow = hset::Add(turn(a.elbow), {1.0f, 0.5f, 0.0f});
        a.wrist = hset::Add(turn(a.wrist), {1.0f, 0.5f, 0.0f});
        a.boxC = hset::Add(turn(a.boxC), {1.0f, 0.5f, 0.0f});
        for (V3& h : a.boxH) h = turn(h);
        const Place q = OnBone(BoxedRight(), cfg, 0), m = OnBone(a, cfg, 0);
        const V3 want = hset::Add(turn(q.c), {1.0f, 0.5f, 0.0f});
        Expect(m.ok && Near(m.c.x, want.x, 1e-5f) && Near(m.c.y, want.y, 1e-5f) && Near(m.c.z, want.z, 1e-5f), "moved and turned with its bone");
    }
    {   // under a shield: rolled half a turn about the forearm, onto the box's other side
        cfg.orbShield.turnDeg = 180.0f;
        p = OnBone(BoxedRight(), cfg, 0, 0.0f, 1.0f);
        Expect(p.ok && Near(p.c.y, -0.25f - 0.03f - 0.005f, 1e-5f) && Near(p.back.y, -1.0f, 1e-4f), "under a shield: on the inner side of the wrist (%.4f)", p.c.y);
    }
    {   // both on one wrist: side by side across the forearm
        const Place a = OnBone(BoxedRight(), cfg, 0, -0.027f), b = OnBone(BoxedRight(), cfg, 0, 0.027f);
        Expect(a.ok && b.ok && Near(std::fabs(a.c.x - b.c.x), 0.054f, 1e-5f) && Near(a.c.y, b.c.y, 1e-6f), "two on one wrist: across the forearm, 5.4 cm apart");
    }
    Expect(!OnBone(RightArm(), cfg, 0).ok, "no box: not on the bone");
    {
        Arm a = BoxedRight();
        a.boxH[2].z = std::nanf("");
        Expect(!OnBone(a, cfg, 0).ok, "a box not a number: not on the bone");
    }
    {   // Step: the box wins, the spot's numbers and turns are not used; off, or no box: the spot as before
        In in = Base();
        in.arm[0] = BoxedRight();
        in.cfg.orbHands = 0;   // (life on the left wrist, mana on the right)
        in.cfg.orbRight.alongCm = 7.0f;
        in.cfg.orbRight.rotYDeg = 30.0f;
        State st;
        Set set = Step(st, in);
        const Orb& mana = set.orb[kMana];   // (the mana ball on the right wrist)
        const Place want = OnBone(BoxedRight(), in.cfg, 0);
        Expect(mana.on && Near(mana.c.x, want.c.x, 1e-5f) && Near(mana.c.y, want.c.y, 1e-5f) && Near(mana.c.z, want.c.z, 1e-5f) && Near(mana.along.z, -1.0f, 1e-5f),
               "Step puts it on the bone, the spot's 7 cm and 30 degrees left out");
        in.cfg.orbOnBone = false;
        State st2;
        set = Step(st2, in);
        Expect(set.orb[kMana].on && !Near(set.orb[kMana].c.z, want.c.z, 1e-3f), "unticked: the spot's numbers, as before");
        in.cfg.orbOnBone = true;
        in.arm[0] = RightArm();
        State st3;
        set = Step(st3, in);
        const Place old = Centre(RightArm(), Grip{}, in.cfg, 0);
        Expect(set.orb[kMana].on && Near(set.orb[kMana].c.z, old.c.z, 0.02f), "ticked but no box: the spot's place (%.4f, %.4f)", set.orb[kMana].c.z, old.c.z);
    }
    {   // the box carried on by the controller while the drawn arm is not read
        Carry c;
        Grip g;
        g.ok = g.hasAxes = true;
        g.pos = {0.20f, -0.25f, -0.50f};
        c.Learn(BoxedRight(), g, 1.0, 0.0f);
        Grip moved = g;
        moved.pos = hset::Add(g.pos, {0.1f, 0.0f, 0.0f});
        Arm a;
        Expect(c.Make(moved, &a) && a.hasBox && Near(a.boxC.x, 0.30f, 1e-5f) && Near(a.boxH[2].z, 0.13f, 1e-5f), "the box carried with the grip (%.4f)", a.boxC.x);
        c.Learn(RightArm(), g, 1.1, 0.05f);
        Expect(c.Make(g, &a) && !a.hasBox, "a drawn arm with no box: none carried");
    }
}

// ---- where it sits -------------------------------------------------------------------------------------------------------------------------

void TestPlacement() {
    printf("placement\n");
    const Config cfg = OrbsOn();   // 4 cm up, 3 cm along, 5 cm across
    Place p = Centre(RightArm(), Grip{}, cfg, 0);
    Expect(p.ok && Near(p.c.x, 0.20f, 1e-5f) && Near(p.c.y, -0.25f + 0.04f, 1e-5f) && Near(p.c.z, -0.45f - 0.03f, 1e-5f), "right wrist: 3 cm on along the arm, 4 cm up out of the back (%.3f %.3f %.3f)", p.c.x, p.c.y, p.c.z);
    p = Centre(LeftArm(), Grip{}, cfg, 1);
    Expect(p.ok && Near(p.c.x, -0.20f, 1e-5f) && Near(p.c.y, -0.21f, 1e-5f) && Near(p.c.z, -0.48f, 1e-5f), "left wrist likewise");
    Config c2 = cfg;
    c2.orbRight.upCm = 0.0f;
    c2.orbRight.alongCm = 0.0f;
    p = Centre(RightArm(), Grip{}, c2, 0);
    Expect(Near(p.c.x, 0.20f, 1e-6f) && Near(p.c.y, -0.25f, 1e-6f) && Near(p.c.z, -0.45f, 1e-6f), "up 0 along 0: on the wrist itself");
    c2.orbRight.alongCm = -2.0f;
    c2.orbRight.upCm = 6.0f;
    p = Centre(RightArm(), Grip{}, c2, 0);
    Expect(Near(p.c.z, -0.45f + 0.02f, 1e-5f) && Near(p.c.y, -0.25f + 0.06f, 1e-5f), "along -2 (toward the elbow), up 6");

    // the palm turned up: the back of the hand is down, and the ball goes with it (it is on the back of the hand, not on top of the world)
    p = Centre(ArmAt({0.2f, -0.25f, -0.15f}, {0.2f, -0.25f, -0.45f}, {0.0f, 1.0f, 0.0f}), Grip{}, cfg, 0);
    Expect(Near(p.c.y, -0.25f - 0.04f, 1e-5f), "palm up: the ball is under the wrist, on the back of the hand (y %.3f)", p.c.y);
    // the palm to the side: the ball goes to the other side
    p = Centre(ArmAt({0.2f, -0.25f, -0.15f}, {0.2f, -0.25f, -0.45f}, {-1.0f, 0.0f, 0.0f}), Grip{}, cfg, 0);
    Expect(Near(p.c.x, 0.20f + 0.04f, 1e-5f) && Near(p.c.y, -0.25f, 1e-5f), "the palm turned to the left: the back of the hand is to the right, so is the ball");
    // an arm not along z: the axis is the elbow -> wrist line, and `across` is made square to it first
    {
        const V3 elbow{0.1f, -0.3f, -0.1f}, wrist{0.3f, -0.1f, -0.4f};
        const V3 axis = hset::Unit(hset::Sub(wrist, elbow));
        const V3 across = hset::Unit(V3{0.3f, -0.9f, 0.2f});   // not square to the axis
        p = Centre(ArmAt(elbow, wrist, across), Grip{}, cfg, 0);
        const V3 d = hset::Sub(p.c, wrist);
        const float onAxis = hset::Dot(d, axis);
        const V3 rest = hset::Sub(d, hset::Mul(axis, onAxis));
        Expect(Near(onAxis, 0.03f, 1e-5f) && Near(Len(rest), 0.04f, 1e-5f), "a slanted arm: 3 cm along the forearm, 4 cm off it (%.4f %.4f)", onAxis, Len(rest));
        Expect(hset::Dot(rest, across) < 0.0f, "and against the palm's way");
    }
    // roll the forearm about its axis: the ball goes round with the back of the hand
    {
        const float a = 0.7f;   // the forearm's axis is z here: rolling it turns `across` in the x-y plane
        const V3 acrossXY{std::sin(a), -std::cos(a), 0.0f};
        p = Centre(ArmAt({0.2f, -0.25f, -0.15f}, {0.2f, -0.25f, -0.45f}, acrossXY), Grip{}, cfg, 0);
        Expect(Near(p.c.x - 0.20f, -0.04f * std::sin(a), 1e-5f) && Near(p.c.y + 0.25f, 0.04f * std::cos(a), 1e-5f), "rolled %.1f rad about the forearm: the ball turns with it", a);
    }
    // mirror: the left arm is the right arm reflected across x = 0 (the palm's way reflected too): the ball is its mirror image
    {
        const Arm r = ArmAt({0.21f, -0.31f, -0.12f}, {0.27f, -0.12f, -0.41f}, hset::Unit(V3{0.2f, -0.8f, 0.3f}));
        const Arm l = ArmAt({-0.21f, -0.31f, -0.12f}, {-0.27f, -0.12f, -0.41f}, hset::Unit(V3{-0.2f, -0.8f, 0.3f}));
        const Place pr = Centre(r, Grip{}, cfg, 0), pl = Centre(l, Grip{}, cfg, 1);
        Expect(pr.ok && pl.ok && Near(pr.c.x, -pl.c.x, 1e-5f) && Near(pr.c.y, pl.c.y, 1e-5f) && Near(pr.c.z, pl.c.z, 1e-5f), "left and right are mirrors");
    }
    // both on one wrist, side by side across the hand
    {
        const float r = cfg.orbSizeCm * 0.005f;
        const Place a = Centre(RightArm(), Grip{}, cfg, 0, -(r + 0.002f)), b = Centre(RightArm(), Grip{}, cfg, 0, r + 0.002f);
        const float apart = Len(hset::Sub(a.c, b.c));
        Expect(a.ok && b.ok && Near(apart, 2.0f * r + 0.004f, 1e-5f), "the two balls are a radius and a gap apart (%.4f)", apart);
        Expect(Near(a.c.y, b.c.y, 1e-6f) && Near(a.c.z, b.c.z, 1e-6f), "side by side across the hand, at the same height and along the arm");
    }
    // no arm: the controller's grip, the ball above it
    {
        Grip g;
        g.ok = true;
        g.pos = {0.25f, -0.2f, -0.5f};
        p = Centre(Arm{}, g, cfg, 0);
        Expect(p.ok && Near(p.c.x, 0.25f, 1e-6f) && Near(p.c.y, -0.2f + 0.04f, 1e-6f) && Near(p.c.z, -0.5f, 1e-6f), "no arm posed: 4 cm above the grip");
        p = Centre(RightArm(), g, cfg, 0);
        Expect(Near(p.c.x, 0.20f, 1e-5f) && Near(p.c.z, -0.48f, 1e-5f), "with an arm posed, the grip is not used");
    }
    // nothing to go by
    Expect(!Centre(Arm{}, Grip{}, cfg, 0).ok, "no arm and no grip: no place");
    Expect(!Centre(ArmAt({0, 0, 0}, {0, 0, 0}, {0, 1, 0}), Grip{}, cfg, 0).ok, "elbow on the wrist: no place");
    Expect(!Centre(ArmAt({0, 0, 0}, {0, 0, -0.3f}, {0, 0, -1.0f}), Grip{}, cfg, 0).ok, "the palm's way along the forearm: no place");
    Expect(!Centre(ArmAt({0, 0, 0}, {0, 0, -0.3f}, {0, 0, 0}), Grip{}, cfg, 0).ok, "no palm direction: no place");
    Expect(!Centre(ArmAt({std::nanf(""), 0, 0}, {0, 0, -0.3f}, {0, 1, 0}), Grip{}, cfg, 0).ok, "a NaN elbow: no place");
    Expect(!Centre(ArmAt({0, 0, 0}, {0, std::nanf(""), -0.3f}, {0, 1, 0}), Grip{}, cfg, 0).ok, "a NaN wrist: no place");
    Grip bad;
    bad.ok = true;
    bad.pos = {0, std::numeric_limits<float>::infinity(), 0};
    Expect(!Centre(Arm{}, bad, cfg, 0).ok, "an infinite grip: no place");
    // the offsets are clamped to the settings' ranges
    Config wild = cfg;
    wild.orbRight.upCm = 500.0f;
    wild.orbRight.alongCm = -500.0f;
    wild.orbRight.sideCm = std::nanf("");
    p = Centre(RightArm(), Grip{}, wild, 0);
    Expect(p.ok && Near(p.c.y, -0.25f + 0.10f, 1e-5f) && Near(p.c.z, -0.45f + 0.10f, 1e-5f) && Near(p.c.x, 0.20f, 1e-6f), "wild offsets clamp to 10 cm up and 10 cm back");

    // which wrist
    Config sw = cfg;
    sw.orbHands = 0;
    Expect(SideOf(sw, kLife) == 1 && SideOf(sw, kMana) == 0, "default: life on the left (side 1), mana on the right (side 0)");
    sw.orbHands = 1;
    Expect(SideOf(sw, kLife) == 0 && SideOf(sw, kMana) == 1, "swapped");
    sw.orbHands = 2;
    Expect(SideOf(sw, kLife) == 1 && SideOf(sw, kMana) == 1 && BothOnOne(sw), "both on the left");
    sw.orbHands = 3;
    Expect(SideOf(sw, kLife) == 0 && SideOf(sw, kMana) == 0 && BothOnOne(sw), "both on the right");
    sw.orbHands = 0;
    Expect(!BothOnOne(sw), "one each is not 'both'");
}


// ---- the three numbers on each wrist -----------------------------------------------------------------------------------------------------

// The right arm and the left arm held out in front, palms down, as RightArm() / LeftArm() have them: the forearm along -z, the back of the hand up (+y); then the thumbs
// are toward the body's middle: the left thumb to +x, the right thumb to -x.
void TestSpots() {
    printf("the three numbers on each wrist (X along, Y across, Z up), the mirror\n");
    Config cfg = OrbsOn();
    // defaults: along 3, across 0, up 4 on both wrists - where the one pair of numbers put them before
    Expect(cfg.orbLeft.alongCm == 3.0f && cfg.orbLeft.sideCm == 0.0f && cfg.orbLeft.upCm == 4.0f && cfg.orbRight.alongCm == 3.0f && cfg.orbRight.sideCm == 0.0f &&
               cfg.orbRight.upCm == 4.0f && !cfg.orbMirror, "defaults: 3 / 0 / 4 on each wrist, mirror off");
    {
        const Place l = Centre(LeftArm(), Grip{}, cfg, 1), r = Centre(RightArm(), Grip{}, cfg, 0);
        Expect(Near(l.c.x, -0.20f, 1e-6f) && Near(l.c.y, -0.21f, 1e-6f) && Near(l.c.z, -0.48f, 1e-6f) && Near(r.c.x, 0.20f, 1e-6f) && Near(r.c.y, -0.21f, 1e-6f) &&
                   Near(r.c.z, -0.48f, 1e-6f), "the defaults put both balls where they were before");
    }
    // each of the three moves the ball along its own axis of the wrist's frame, by exactly that many centimetres
    cfg.orbLeft = {0.0f, 0.0f, 0.0f};
    cfg.orbRight = {0.0f, 0.0f, 0.0f};
    {
        const Place l0 = Centre(LeftArm(), Grip{}, cfg, 1), r0 = Centre(RightArm(), Grip{}, cfg, 0);
        Expect(Near(l0.c.x, -0.20f, 1e-6f) && Near(l0.c.y, -0.25f, 1e-6f) && Near(l0.c.z, -0.45f, 1e-6f) && Near(r0.c.x, 0.20f, 1e-6f), "0 / 0 / 0: on the wrist");
        Config c = cfg;
        c.orbLeft.alongCm = 7.0f;
        Place l = Centre(LeftArm(), Grip{}, c, 1);
        Expect(Near(l.c.z, -0.45f - 0.07f, 1e-6f) && Near(l.c.x, l0.c.x, 1e-6f) && Near(l.c.y, l0.c.y, 1e-6f), "X 7: 7 cm on toward the knuckles, nothing else");
        c = cfg;
        c.orbLeft.alongCm = -7.0f;
        l = Centre(LeftArm(), Grip{}, c, 1);
        Expect(Near(l.c.z, -0.45f + 0.07f, 1e-6f), "X -7: toward the elbow");
        c = cfg;
        c.orbLeft.upCm = 6.0f;
        l = Centre(LeftArm(), Grip{}, c, 1);
        Expect(Near(l.c.y, -0.25f + 0.06f, 1e-6f) && Near(l.c.x, l0.c.x, 1e-6f) && Near(l.c.z, l0.c.z, 1e-6f), "Z 6: 6 cm out of the back of the hand, nothing else");
        c = cfg;
        c.orbLeft.upCm = -3.0f;
        l = Centre(LeftArm(), Grip{}, c, 1);
        Expect(Near(l.c.y, -0.25f - 0.03f, 1e-6f), "Z -3: into the hand");
        // across: + toward the thumb. The left hand, palm down, arm forward: the thumb is toward the body's middle, +x.
        c = cfg;
        c.orbLeft.sideCm = 5.0f;
        l = Centre(LeftArm(), Grip{}, c, 1);
        Expect(Near(l.c.x, l0.c.x + 0.05f, 1e-6f) && Near(l.c.y, l0.c.y, 1e-6f) && Near(l.c.z, l0.c.z, 1e-6f), "Y +5 on the left hand: 5 cm toward the thumb (toward the middle of the body, +x)");
        c.orbLeft.sideCm = -5.0f;
        l = Centre(LeftArm(), Grip{}, c, 1);
        Expect(Near(l.c.x, l0.c.x - 0.05f, 1e-6f), "Y -5: toward the little finger");
        // the right hand, palm down, arm forward: its thumb is toward the body's middle too, which is -x
        c = cfg;
        c.orbRight.sideCm = 5.0f;
        Place r = Centre(RightArm(), Grip{}, c, 0);
        Expect(Near(r.c.x, r0.c.x - 0.05f, 1e-6f) && Near(r.c.y, r0.c.y, 1e-6f) && Near(r.c.z, r0.c.z, 1e-6f), "Y +5 on the right hand: toward its thumb (toward the middle of the body, -x)");
        // all three at once, a sum
        c = cfg;
        c.orbRight = {2.0f, -3.0f, 5.0f};
        r = Centre(RightArm(), Grip{}, c, 0);
        Expect(Near(r.c.x, r0.c.x + 0.03f, 1e-6f) && Near(r.c.y, r0.c.y + 0.05f, 1e-6f) && Near(r.c.z, r0.c.z - 0.02f, 1e-6f), "X 2, Y -3, Z 5 together");
    }
    // each wrist has its own set, the other's is not touched
    {
        Config c = cfg;
        c.orbLeft = {1.0f, 2.0f, 3.0f};
        c.orbRight = {-4.0f, -5.0f, 6.0f};
        const Place l = Centre(LeftArm(), Grip{}, c, 1), r = Centre(RightArm(), Grip{}, c, 0);
        Expect(Near(l.c.z, -0.45f - 0.01f, 1e-6f) && Near(l.c.x, -0.20f + 0.02f, 1e-6f) && Near(l.c.y, -0.25f + 0.03f, 1e-6f), "the left wrist takes the left set");
        Expect(Near(r.c.z, -0.45f + 0.04f, 1e-6f) && Near(r.c.x, 0.20f + 0.05f, 1e-6f) && Near(r.c.y, -0.25f + 0.06f, 1e-6f), "the right wrist takes the right set (-5 toward the little finger = +x)");
        Expect(SpotFor(c, 1).alongCm == 1.0f && SpotFor(c, 0).alongCm == -4.0f, "SpotFor: the set of the side");
        // mirrored: the right takes the left's numbers, the same anatomically (so the two balls are mirror images)
        c.orbMirror = true;
        Expect(SpotFor(c, 0).sideCm == 2.0f && SpotFor(c, 1).sideCm == 2.0f && SpotFor(c, 0).upCm == 3.0f, "mirror: the right wrist reads the left's set");
        const Place rm = Centre(RightArm(), Grip{}, c, 0), lm = Centre(LeftArm(), Grip{}, c, 1);
        Expect(rm.ok && lm.ok && Near(rm.c.x, -lm.c.x, 1e-6f) && Near(rm.c.y, lm.c.y, 1e-6f) && Near(rm.c.z, lm.c.z, 1e-6f), "mirrored: the two balls are reflections across the body's middle (%.4f %.4f %.4f | %.4f %.4f %.4f)", rm.c.x, rm.c.y, rm.c.z, lm.c.x, lm.c.y, lm.c.z);
        // the right set is kept, only unused: unticked, it is back
        c.orbMirror = false;
        const Place back = Centre(RightArm(), Grip{}, c, 0);
        Expect(Len(hset::Sub(back.c, r.c)) < 1e-7f, "unticked: the right wrist's own set is back");
    }
    // mirror images on a real posed (slanted, turned) pair too: reflect the right arm across x = 0 to make the left one
    {
        const Arm r = ArmAt({0.21f, -0.31f, -0.12f}, {0.27f, -0.12f, -0.41f}, hset::Unit(V3{0.2f, -0.8f, 0.3f}));
        const Arm l = ArmAt({-0.21f, -0.31f, -0.12f}, {-0.27f, -0.12f, -0.41f}, hset::Unit(V3{-0.2f, -0.8f, 0.3f}));
        Config c = cfg;
        c.orbLeft = {2.5f, 3.5f, 5.0f};
        c.orbMirror = true;
        const Place pr = Centre(r, Grip{}, c, 0), pl = Centre(l, Grip{}, c, 1);
        Expect(pr.ok && pl.ok && Near(pr.c.x, -pl.c.x, 1e-5f) && Near(pr.c.y, pl.c.y, 1e-5f) && Near(pr.c.z, pl.c.z, 1e-5f), "a slanted pair, mirrored: reflections of each other");
        // not mirrored with the same numbers on both: the same still (anatomical Y)
        c.orbMirror = false;
        c.orbRight = c.orbLeft;
        const Place pr2 = Centre(r, Grip{}, c, 0);
        Expect(Len(hset::Sub(pr2.c, pr.c)) < 1e-6f, "the same three numbers typed on both wrists are the mirror too");
    }
    // the offsets turn with the hand: a hand tipped up at the wrist, the thumb side and the back go round with it
    {
        Arm a = LeftArm();
        const float t = 40.0f * 3.14159265f / 180.0f;
        a.hasPoint = true;
        a.point = {0.0f, std::sin(t), -std::cos(t)};
        a.across = {0.0f, -std::cos(t), -std::sin(t)};
        Config c = cfg;
        c.orbLeft = {2.0f, 3.0f, 4.0f};
        const Place p = Centre(a, Grip{}, c, 1);
        const V3 back{0.0f, std::cos(t), std::sin(t)};
        const V3 expect = hset::Add(hset::Add(a.wrist, hset::Mul(a.point, 0.02f)), hset::Add(hset::Mul(back, 0.04f), V3{0.03f, 0.0f, 0.0f}));
        Expect(p.ok && Len(hset::Sub(p.c, expect)) < 1e-5f, "a bent wrist: X along the hand, Z out of its back, Y across it (%.4f %.4f %.4f)", p.c.x, p.c.y, p.c.z);
    }
    // the grip stands in (no arm): only the height applies
    {
        Grip g;
        g.ok = true;
        g.pos = {0.25f, -0.2f, -0.5f};
        Config c = cfg;
        c.orbRight = {4.0f, 4.0f, 7.0f};
        const Place p = Centre(Arm{}, g, c, 0);
        Expect(p.ok && Near(p.c.x, 0.25f, 1e-6f) && Near(p.c.y, -0.2f + 0.07f, 1e-6f) && Near(p.c.z, -0.5f, 1e-6f), "without an arm: above the grip by Z of that wrist");
    }
    // Step: the balls take their own wrist's set, and the pair on one wrist is still side by side
    {
        State st;
        In in = Base();
        in.cfg.orbLeft = {1.0f, 0.0f, 2.0f};
        in.cfg.orbRight = {6.0f, 0.0f, 8.0f};
        Set s = Step(st, in);   // life on the left, mana on the right
        Expect(s.orb[kLife].on && s.orb[kMana].on && Near(s.orb[kLife].c.z, -0.45f - 0.01f, 1e-5f) && Near(s.orb[kLife].c.y, -0.25f + 0.02f, 1e-5f) &&
                   Near(s.orb[kMana].c.z, -0.45f - 0.06f, 1e-5f) && Near(s.orb[kMana].c.y, -0.25f + 0.08f, 1e-5f), "Step: life takes the left wrist's set, mana the right's");
        in.cfg.orbHands = 1;   // swapped: each ball goes to the other wrist and takes THAT wrist's set
        State st2;
        s = Step(st2, in);
        Expect(Near(s.orb[kLife].c.z, -0.45f - 0.06f, 1e-5f) && Near(s.orb[kMana].c.z, -0.45f - 0.01f, 1e-5f), "swapped: the set belongs to the wrist, not to the ball");
        in.cfg.orbHands = 0;
        in.cfg.orbMirror = true;
        State st3;
        s = Step(st3, in);
        Expect(Near(s.orb[kMana].c.z, -0.45f - 0.01f, 1e-5f) && Near(s.orb[kMana].c.y, -0.25f + 0.02f, 1e-5f), "mirrored: the right wrist's ball sits by the left's numbers");
        // both on the right wrist, the right set in the middle of them
        in.cfg.orbMirror = false;
        in.cfg.orbHands = 3;
        in.cfg.orbRight = {2.0f, 1.5f, 5.0f};
        State st4;
        s = Step(st4, in);
        const V3 mid = hset::Mul(hset::Add(s.orb[kLife].c, s.orb[kMana].c), 0.5f);
        const Place centre = Centre(RightArm(), Grip{}, in.cfg, 0);
        Expect(Len(hset::Sub(mid, centre.c)) < 1e-5f, "a pair on one wrist is centred on that wrist's spot");
        Expect(Near(centre.c.x, 0.20f - 0.015f, 1e-6f), "(and the right wrist's Y +1.5 is toward the thumb, -x)");
    }
    // the settings' ranges: -10 .. 10 each, a number that is not one is the default, either wrist
    {
        Config c;
        c.orbLeft = {-50.0f, 50.0f, std::nanf("")};
        c.orbRight = {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), 11.0f};
        const Config k = handvitals::Sanitized(c);
        Expect(k.orbLeft.alongCm == -10.0f && k.orbLeft.sideCm == 10.0f && k.orbLeft.upCm == 4.0f, "left: -10 .. 10, NaN is the default");
        Expect(k.orbRight.alongCm == 3.0f && k.orbRight.sideCm == 0.0f && k.orbRight.upCm == 10.0f, "right: infinities are the defaults, 11 is 10");
        c.orbLeft = {-10.0f, -10.0f, -10.0f};
        c.orbRight = {10.0f, 10.0f, 10.0f};
        const Config e = handvitals::Sanitized(c);
        Expect(e.orbLeft.alongCm == -10.0f && e.orbRight.upCm == 10.0f && e.orbLeft.sideCm == -10.0f, "the ends are allowed");
        // the extremes still give a place on the arm, far enough from the eyes to be seen, and nothing breaks
        Config x = OrbsOn();
        x.orbRight = {10.0f, 10.0f, 10.0f};
        x.orbLeft = {-10.0f, -10.0f, -10.0f};
        State st;
        In in = Base();
        in.cfg = x;
        const Set s = Step(st, in);
        Expect(s.any && Finite(s.orb[kLife].c) && Finite(s.orb[kMana].c), "the extremes give places");
    }
}

void TestHandFrame() {
    printf("the hand's own frame, the liquid's axis, the drawn arm from the model\n");
    const Config cfg = OrbsOn();
    // the hand turned up at the wrist: along = wrist -> knuckle, tipped 40 degrees up from the forearm's -z; the palm's normal turns with it
    {
        Arm a = RightArm();   // elbow (0.2 -0.25 -0.15), wrist (0.2 -0.25 -0.45), the palm down (across -y)
        const float t = 40.0f * 3.14159265f / 180.0f;
        a.hasPoint = true;
        a.point = {0.0f, std::sin(t), -std::cos(t)};                    // tipped up
        a.across = {0.0f, -std::cos(t), -std::sin(t)};                  // out of the palm, turned with it
        const Place p = Centre(a, Grip{}, cfg, 0);
        const V3 back{0.0f, std::cos(t), std::sin(t)};
        const V3 expect = hset::Add(hset::Add(a.wrist, hset::Mul(a.point, 0.03f)), hset::Mul(back, 0.04f));
        Expect(p.ok && Len(hset::Sub(p.c, expect)) < 1e-5f, "a bent wrist: the ball turns with the hand (%.4f %.4f %.4f)", p.c.x, p.c.y, p.c.z);
        const Place q = Centre(RightArm(), Grip{}, cfg, 0);
        Expect(Len(hset::Sub(p.c, q.c)) > 0.02f, "and is not where the forearm's frame would put it (%.1f cm apart)", Len(hset::Sub(p.c, q.c)) * 100.0f);
        Expect(p.hasArm && Near(p.armAxis.z, -1.0f, 1e-5f) && Near(p.armAxis.y, 0.0f, 1e-5f), "the liquid's axis is still the forearm's, elbow -> wrist");
    }
    // a hand frame that is not square: the palm's normal is made square to the hand's axis; a degenerate one is no place
    {
        Arm a = RightArm();
        a.hasPoint = true;
        a.point = {0.0f, 0.0f, -1.0f};
        a.across = {0.0f, -1.0f, -0.3f};   // not square
        Place p = Centre(a, Grip{}, cfg, 0);
        Expect(p.ok && Near(p.c.y, -0.25f + 0.04f, 1e-5f) && Near(p.c.z, -0.48f, 1e-5f), "made square to the hand's axis");
        a.across = {0.0f, 0.0f, -1.0f};
        Expect(!Centre(a, Grip{}, cfg, 0).ok, "the palm's normal along the hand: no place");
        a.across = {0.0f, -1.0f, 0.0f};
        a.point = {0.0f, 0.0f, 0.0f};
        p = Centre(a, Grip{}, cfg, 0);
        Expect(p.ok && Near(p.c.z, -0.48f, 1e-5f), "a zero hand axis falls back to the forearm's");
    }
    // the liquid's axis
    {
        State st;
        In in = Base();
        in.cfg.orbFill = handvitals::kFillElbow;
        Set s = Step(st, in);
        Expect(Near(s.orb[kLife].axis.z, -1.0f, 1e-5f) && Near(s.orb[kLife].axis.y, 0.0f, 1e-5f) && Near(s.orb[kMana].axis.z, -1.0f, 1e-5f), "along the arm toward the hand, the elbow side full (the forearm runs along -z here)");
        in.cfg.orbFill = handvitals::kFillWorldUp;
        s = Step(st, in);
        Expect(Near(s.orb[kLife].axis.y, 1.0f, 1e-6f) && Near(s.orb[kLife].axis.x, 0.0f, 1e-6f), "level in the world: up");
        // a slanted arm: the axis is the unit elbow -> wrist
        State st2;
        In sl = Base();
        sl.arm[1] = ArmAt({-0.1f, -0.30f, -0.10f}, {-0.3f, -0.1f, -0.4f}, hset::Unit(V3{0.1f, -0.8f, 0.3f}));
        sl.cfg.orbFill = handvitals::kFillElbow;
        s = Step(st2, sl);
        const V3 want = hset::Unit(V3{-0.2f, 0.2f, -0.3f});
        Expect(Len(hset::Sub(s.orb[kLife].axis, want)) < 1e-5f && Near(Len(s.orb[kLife].axis), 1.0f, 1e-5f), "a slanted arm: the axis is the unit elbow -> wrist");
        // the controller's grip alone: no arm, so up
        State st3;
        In g = Base();
        g.arm[0].ok = g.arm[1].ok = false;
        g.grip[0].ok = g.grip[1].ok = true;
        g.grip[0].pos = {0.25f, -0.2f, -0.5f};
        g.grip[1].pos = {-0.25f, -0.2f, -0.5f};
        s = Step(st3, g);
        Expect(s.any && Near(s.orb[kLife].axis.y, 1.0f, 1e-6f), "no arm posed: the liquid's axis is the world's up");
        // the uniforms carry it as a unit vector
        const float camR[3] = {1.0f, 0.0f, 0.0f}, camF[3] = {0.0f, 0.0f, -1.0f};
        State st4;
        In b4 = Base();
        b4.cfg.orbFill = handvitals::kFillElbow;
        const Set s4 = Step(st4, b4);
        const Uniforms u = Pack(s4, camR, camF, 4.0f, 1.0);
        Expect(Near(u.axis[kLife][2], -1.0f, 1e-5f) && Near(u.axis[kMana][2], -1.0f, 1e-5f) && Near(u.axis[kLife][0], 0.0f, 1e-6f), "OrbAxis holds the unit axis in the world's axes");
        Config c;
        c.orbFill = 7;
        Expect(handvitals::Sanitized(c).orbFill == handvitals::kFillOutOfHand, "a fill direction that is not one is the default, out of the hand");
        for (int f = 0; f <= 5; ++f) {
            c.orbFill = f;
            Expect(handvitals::Sanitized(c).orbFill == f, "fill direction %d survives Sanitized", f);
        }
    }
    // the drawn arm from the skeleton's world: eye-relative metres in the world's own axes
    {
        WorldArm m;
        m.elbow = {32.0f, 2.0f, -11.0f};
        m.wrist = {35.0f, 1.8f, -16.0f};
        m.across = hset::Unit(V3{0.1f, -1.0f, 0.2f});
        m.point = hset::Unit(V3{0.3f, 0.1f, -1.0f});
        m.face = hset::Unit(V3{0.0f, -1.0f, 0.1f});
        m.hand = true;
        const float eye[3] = {30.5f, 3.0f, -10.0f};
        const float perM = 8.0f;
        const Arm a = ArmFromWorld(eye, perM, m);
        Expect(a.ok && Near(a.elbow.x, 1.5f / 8.0f, 1e-6f) && Near(a.elbow.y, -1.0f / 8.0f, 1e-6f) && Near(a.elbow.z, -1.0f / 8.0f, 1e-6f), "the elbow: the world point, from the eye, in metres");
        Expect(Near(a.wrist.x, 4.5f / 8.0f, 1e-6f) && Near(a.wrist.y, -1.2f / 8.0f, 1e-6f) && Near(a.wrist.z, -6.0f / 8.0f, 1e-6f), "the wrist likewise");
        Expect(a.hasPoint && Near(Len(a.point), 1.0f, 1e-5f) && Len(hset::Sub(a.point, m.point)) < 1e-5f, "the hand's axis, unit");
        Expect(Near(Len(a.across), 1.0f, 1e-5f) && Len(hset::Sub(a.across, m.face)) < 1e-5f, "the palm's normal is the hand's own (face), unit");
        // the ball rides the wrist exactly: the hero walks and turns about the eye - the offset from the wrist to the ball is the same turned offset, and the eye-relative
        // frame does not care where the hero stands
        const Config cfg = OrbsOn();
        const Place p0 = Centre(a, Grip{}, cfg, 0);
        const float yaw = 0.9f, cy = std::cos(yaw), sy = std::sin(yaw);
        auto turn = [&](V3 v) { return V3{v.x * cy + v.z * sy, v.y, -v.x * sy + v.z * cy}; };
        const float shift[3] = {-40.0f, 5.0f, 22.0f};   // the whole scene (hero and eye) moved by this, and turned about the eye
        WorldArm w2 = m;
        const V3 eyeV{eye[0], eye[1], eye[2]};
        auto world2 = [&](V3 p) { return hset::Add(hset::Add(eyeV, turn(hset::Sub(p, eyeV))), V3{shift[0], shift[1], shift[2]}); };
        w2.elbow = world2(m.elbow);
        w2.wrist = world2(m.wrist);
        w2.across = turn(m.across);
        w2.point = turn(m.point);
        w2.face = turn(m.face);
        const float eye2[3] = {eye[0] + shift[0], eye[1] + shift[1], eye[2] + shift[2]};
        const Arm b = ArmFromWorld(eye2, perM, w2);
        const Place p1 = Centre(b, Grip{}, cfg, 0);
        Expect(p0.ok && p1.ok && Len(hset::Sub(turn(p0.c), p1.c)) < 1e-4f, "the hero moved and turned with the eye: the ball is exactly the turned place (%.5f)", Len(hset::Sub(turn(p0.c), p1.c)));
        Expect(Len(hset::Sub(turn(hset::Sub(p0.c, a.wrist)), hset::Sub(p1.c, b.wrist))) < 1e-4f, "and its offset from the wrist is the same offset, turned");
        // a bent wrist moves the ball with the hand
        WorldArm bent = m;
        bent.point = hset::Unit(V3{0.5f, 0.5f, -0.7f});
        bent.face = hset::Unit(V3{0.5f, -0.7f, 0.5f});
        const Place p2 = Centre(ArmFromWorld(eye, perM, bent), Grip{}, cfg, 0);
        Expect(p2.ok && Len(hset::Sub(p2.c, p0.c)) > 0.01f, "a bent wrist moves the ball with the hand");
        // no hand frame found: the forearm's frame, the palm's normal from the forearm's knuckle line (across)
        WorldArm nohand = m;
        nohand.hand = false;
        const Arm n = ArmFromWorld(eye, perM, nohand);
        Expect(n.ok && !n.hasPoint && Len(hset::Sub(n.across, m.across)) < 1e-5f, "without the hand's frame: no point, across from the forearm");
        // bad numbers
        Expect(!ArmFromWorld(eye, 0.0f, m).ok && !ArmFromWorld(eye, std::nanf(""), m).ok && !ArmFromWorld(eye, -3.0f, m).ok, "a units-per-metre that is not one: not ok");
        WorldArm nan = m;
        nan.wrist.y = std::nanf("");
        Expect(!ArmFromWorld(eye, perM, nan).ok, "a NaN wrist: not ok");
        WorldArm flat = m;
        flat.point = {0, 0, 0};
        Expect(Centre(ArmFromWorld(eye, perM, flat), Grip{}, cfg, 0).ok, "a zero hand axis falls back to the forearm's frame in Centre");
    }
}


// ---- the wrist's frame: always there (the drawn arm, or carried on by the controller), its fill directions, the medallion's numbers ------------------------------------------

// A grip at `pos` turned by `deg` about `axis` (unit): its axes as the world's.
Grip GripTurned(V3 pos, V3 axis, float deg) {
    const float h = 0.5f * deg * 3.14159265f / 180.0f, s = std::sin(h);
    const float q[4] = {axis.x * s, axis.y * s, axis.z * s, std::cos(h)};
    V3 ax[3];
    Grip g;
    if (!AxesFromQuat(q, ax)) return g;
    g.ok = true;
    g.hasAxes = true;
    g.pos = pos;
    g.x = ax[0]; g.y = ax[1]; g.z = ax[2];
    return g;
}
V3 TurnBy(const Grip& from, const Grip& to, V3 v) {   // v (in the world) carried by the rotation that takes `from`'s axes to `to`'s
    const V3 l{hset::Dot(v, from.x), hset::Dot(v, from.y), hset::Dot(v, from.z)};
    return hset::Add(hset::Add(hset::Mul(to.x, l.x), hset::Mul(to.y, l.y)), hset::Mul(to.z, l.z));
}
Arm HandArm(V3 wrist, V3 elbow, V3 point, V3 across) {
    Arm a = ArmAt(elbow, wrist, across);
    a.hasPoint = true;
    a.point = point;
    return a;
}

void TestFrameCarry() {
    printf("the wrist's frame every frame: the controller's axes, what is carried, where it came from\n");
    // the grip's axes from its orientation quaternion
    {
        V3 ax[3];
        const float id[4] = {0, 0, 0, 1};
        Expect(AxesFromQuat(id, ax) && Near(ax[0].x, 1, 1e-6f) && Near(ax[1].y, 1, 1e-6f) && Near(ax[2].z, 1, 1e-6f), "identity: the world's own axes");
        const float s = std::sin(0.7853982f), c = std::cos(0.7853982f), qy[4] = {0, s, 0, c};   // 90 degrees about y
        Expect(AxesFromQuat(qy, ax) && Near(ax[0].z, -1, 1e-5f) && Near(ax[2].x, 1, 1e-5f) && Near(ax[1].y, 1, 1e-5f), "90 degrees about y: x goes to -z, z to x");
        const float big[4] = {0, 2 * s, 0, 2 * c};
        Expect(AxesFromQuat(big, ax) && Near(Len(ax[0]), 1, 1e-5f) && Near(hset::Dot(ax[0], ax[1]), 0, 1e-5f), "a quaternion that is not unit is made so: orthonormal axes");
        const float zero[4] = {0, 0, 0, 0}, bad[4] = {std::nanf(""), 0, 0, 1};
        Expect(!AxesFromQuat(zero, ax) && !AxesFromQuat(bad, ax), "a quaternion that is not one: no axes");
    }
    const Arm drawn0 = HandArm({-0.20f, -0.25f, -0.45f}, {-0.20f, -0.25f, -0.15f}, {0, 0, -1}, {0, -1, 0});
    const Grip g0 = GripTurned({-0.19f, -0.24f, -0.50f}, {0.3f, 0.9f, 0.1f}, 25.0f);
    // Learn / Make: the same grip gives the drawn arm back; another pose of the grip gives the arm carried rigidly with it
    {
        Carry cr;
        cr.Learn(drawn0, g0, 1.0, 0.0f);
        Arm out;
        Expect(cr.have && cr.Make(g0, &out), "learned from one frame");
        Expect(Len(hset::Sub(out.wrist, drawn0.wrist)) < 1e-5f && Len(hset::Sub(out.elbow, drawn0.elbow)) < 1e-5f && Len(hset::Sub(out.across, drawn0.across)) < 1e-5f &&
                   out.hasPoint && Len(hset::Sub(out.point, drawn0.point)) < 1e-5f, "the same grip: the drawn arm back, wrist, elbow, palm and the hand's axis");
        const Grip g1 = GripTurned({0.1f, -0.1f, -0.3f}, {0, 1, 0}, 100.0f);   // the controller moved and turned
        Expect(cr.Make(g1, &out), "made for another pose");
        const V3 wantW = hset::Add(g1.pos, TurnBy(g0, g1, hset::Sub(drawn0.wrist, g0.pos)));
        Expect(Len(hset::Sub(out.wrist, wantW)) < 1e-5f, "the wrist keeps its place in the grip's axes (%.6f)", Len(hset::Sub(out.wrist, wantW)));
        Expect(Len(hset::Sub(out.point, TurnBy(g0, g1, drawn0.point))) < 1e-5f && Len(hset::Sub(out.across, TurnBy(g0, g1, drawn0.across))) < 1e-5f, "and the hand's axis and the palm's normal turn with it");
        Expect(Near(Len(out.point), 1.0f, 1e-5f) && Near(Len(out.across), 1.0f, 1e-5f), "unit vectors");
        // no axes, no grip, nothing learned: no frame
        Grip noAxes = g1;
        noAxes.hasAxes = false;
        Expect(!cr.Make(noAxes, &out) && !cr.Make(Grip{}, &out) && !Carry{}.Make(g1, &out), "a grip without axes, no grip, or nothing learned: not made");
        Carry c2;
        c2.Learn(drawn0, noAxes, 1.0, 0.0f);
        Expect(!c2.have, "a grip without axes teaches nothing");
        // the hand's axis is not always known: the forearm's frame is carried alone
        Arm forearmOnly = drawn0;
        forearmOnly.hasPoint = false;
        Carry c3;
        c3.Learn(forearmOnly, g0, 1.0, 0.0f);
        Expect(c3.have && !c3.hasPoint && c3.Make(g1, &out) && !out.hasPoint, "a frame without the hand's axis is carried without it");
        // a new frame is taken in slowly: 11 ms at a time, about 9 % of the way
        Arm moved = drawn0;
        moved.wrist.y += 0.01f;
        cr.Learn(moved, g0, 1.011, 0.011f);
        cr.Make(g0, &out);
        const float took = (out.wrist.y - drawn0.wrist.y) / 0.01f;
        Expect(took > 0.05f && took < 0.14f, "a new reading is taken in slowly (%.3f of the way in 11 ms)", took);
        Carry c4;
        c4.Learn(drawn0, g0, 1.0, 0.0f);
        c4.Learn(drawn0, GripTurned({std::nanf(""), 0, 0}, {0, 1, 0}, 0.0f), 1.1, 0.1f);
        c4.Make(g0, &out);
        Expect(Len(hset::Sub(out.wrist, drawn0.wrist)) < 1e-5f, "a NaN grip teaches nothing");
    }
    // the Tracker: which frame, and when it changes
    {
        Tracker tr;
        Wrist w;
        w.drawnOk = true;
        w.drawn = drawn0;
        w.drawnAgeS = 0.004;
        w.grip = g0;
        w.forearmOk = true;
        w.forearm = ArmAt({-0.2f, -0.25f, -0.15f}, {-0.2f, -0.25f, -0.44f}, {0, -1, 0});
        double t = 10.0;
        Resolved r = tr.Resolve(1, w, t);
        Expect(r.src == Src::Drawn && r.changed && r.from == Src::None && Len(hset::Sub(r.arm.wrist, drawn0.wrist)) < 1e-7f, "the first frame: the drawn wrist, reported as a change from nothing");
        for (int i = 0; i < 20; ++i) {
            t += 0.011;
            r = tr.Resolve(1, w, t);
            Expect(r.src == Src::Drawn && !r.changed, "frame %d: still the drawn wrist, nothing to say", i);
        }
        // the skeleton's world read fails: the drawn arm is half a second old. The frame is carried on by the controller, and it is where the drawn one was
        w.drawnAgeS = 0.5;
        t += 0.011;
        r = tr.Resolve(1, w, t);
        Expect(r.src == Src::Carried && r.changed && r.from == Src::Drawn && r.lastedS > 0.2 && r.lastedS < 0.3, "the drawn arm is not read: carried (the drawn one lasted %.2f s)", r.lastedS);
        Expect(Len(hset::Sub(r.arm.wrist, drawn0.wrist)) < 1e-5f && r.arm.hasPoint && Len(hset::Sub(r.arm.point, drawn0.point)) < 1e-5f, "at once, in the same place, with the hand's axis");
        // the controller turns the hand 90 degrees about the forearm while the read is down: the frame turns with it
        w.grip = GripTurned(g0.pos, {0, 0, -1}, 90.0f);
        for (int i = 0; i < 30; ++i) {
            t += 0.011;
            r = tr.Resolve(1, w, t);
            Expect(r.src == Src::Carried && !r.changed, "carried, frame %d: no log line", i);
        }
        const Grip& gg = w.grip;
        const V3 wantAcross = TurnBy(g0, gg, drawn0.across), wantPoint = TurnBy(g0, gg, drawn0.point);
        Expect(Len(hset::Sub(r.arm.across, wantAcross)) < 1e-5f && Len(hset::Sub(r.arm.point, wantPoint)) < 1e-5f, "the palm's normal and the hand's axis turned with the controller");
        const Place pl = Centre(r.arm, gg, OrbsOn(), 1);
        const Place pl0 = Centre(drawn0, g0, OrbsOn(), 1);
        Expect(pl.ok && pl0.ok && Len(hset::Sub(pl.back, TurnBy(g0, gg, pl0.back))) < 1e-4f, "so the medallion's back axis turns with it too (%.5f)", Len(hset::Sub(pl.back, TurnBy(g0, gg, pl0.back))));
        // and back: the drawn one again, the duration of the carry reported
        w.drawn = drawn0;
        w.drawnAgeS = 0.003;
        t += 0.011;
        r = tr.Resolve(1, w, t);
        Expect(r.src == Src::Drawn && r.changed && r.from == Src::Carried && r.lastedS > 0.3f && r.lastedS < 0.4f, "the drawn wrist is back: the carry lasted %.2f s", r.lastedS);
        // the other wrist keeps its own state
        Expect(tr.src(0) == Src::None && tr.src(1) == Src::Drawn, "the tracker's sides are separate");
    }
    // a flicker, the skeleton read missing every other frame at 90 Hz: no jump in the frame (the carried arm is the drawn one)
    {
        Tracker tr;
        Wrist w;
        w.drawnOk = true;
        w.drawn = drawn0;
        w.grip = g0;
        float worst = 0.0f;
        int changes = 0;
        Arm last = drawn0;
        for (int i = 0; i < 200; ++i) {
            const double t = 5.0 + i / 90.0;
            const float turn = 0.02f * (float)i;   // the hand swings: the controller turns about y, the drawn arm with it
            w.grip = GripTurned({-0.19f + 0.001f * (float)i, -0.24f, -0.50f}, {0, 1, 0}, turn);
            const Grip base = GripTurned(g0.pos, {0, 1, 0}, 0.0f);
            Arm a = drawn0;
            a.wrist = hset::Add(w.grip.pos, TurnBy(g0, w.grip, hset::Sub(drawn0.wrist, g0.pos)));
            a.elbow = hset::Add(w.grip.pos, TurnBy(g0, w.grip, hset::Sub(drawn0.elbow, g0.pos)));
            a.across = TurnBy(g0, w.grip, drawn0.across);
            a.point = TurnBy(g0, w.grip, drawn0.point);
            (void)base;
            w.drawn = a;
            w.drawnAgeS = i % 2 ? 0.2 : 0.005;
            const Resolved r = tr.Resolve(1, w, t);
            if (r.changed) ++changes;
            if (i > 0) worst = std::max(worst, Len(hset::Sub(r.arm.wrist, last.wrist)) - 0.0f);
            if (i > 2) Expect(Len(hset::Sub(r.arm.wrist, a.wrist)) < 2e-4f && Len(hset::Sub(r.arm.point, a.point)) < 2e-3f, "frame %d: carried or drawn, the same wrist (%.5f)", i, Len(hset::Sub(r.arm.wrist, a.wrist)));
            last = r.arm;
        }
        Expect(worst < 0.02f, "the wrist never jumps between the sources (the worst step %.4f m a frame, the hand moving)", worst);
        Expect(changes > 100, "every switch is a change for the log (%d)", changes);
    }
    // what stands in when nothing was drawn lately
    {
        Tracker tr;
        Wrist w;
        w.grip = g0;
        w.forearmOk = true;
        w.forearm = ArmAt({-0.2f, -0.25f, -0.15f}, {-0.2f, -0.25f, -0.44f}, {0, -1, 0});
        Resolved r = tr.Resolve(0, w, 1.0);
        Expect(r.src == Src::Forearm && r.arm.ok && !r.arm.hasPoint, "never drawn, the skeleton's forearm through the head's turn");
        w.forearmOk = false;
        r = tr.Resolve(0, w, 1.02);
        Expect(r.src == Src::GripPoint && !r.arm.ok && r.changed && r.from == Src::Forearm, "no forearm either: the controller's point");
        w.grip = Grip{};
        r = tr.Resolve(0, w, 1.04);
        Expect(r.src == Src::None && !r.arm.ok, "no controller: nothing");
        // a read that is too old (older than a second) is not the drawn wrist
        Tracker t2;
        Wrist w2;
        w2.drawnOk = true;
        w2.drawn = drawn0;
        w2.drawnAgeS = 0.2;
        w2.grip = g0;
        r = t2.Resolve(1, w2, 1.0);
        Expect(r.src == Src::GripPoint || r.src == Src::Forearm, "an old arm that was never fresh is not used as the drawn one (%s)", SrcName(r.src));
        // the carry does not last for ever: after 30 s without a drawn read the forearm stands in again
        Tracker t3;
        Wrist w3;
        w3.drawnOk = true;
        w3.drawn = drawn0;
        w3.drawnAgeS = 0.0;
        w3.grip = g0;
        w3.forearmOk = true;
        w3.forearm = ArmAt({-0.2f, -0.25f, -0.15f}, {-0.2f, -0.25f, -0.44f}, {0, -1, 0});
        t3.Resolve(1, w3, 100.0);
        w3.drawnOk = false;
        r = t3.Resolve(1, w3, 110.0);
        Expect(r.src == Src::Carried, "10 s later still carried");
        r = t3.Resolve(1, w3, 131.0);
        Expect(r.src == Src::Forearm, "31 s later the older fallback");
        t3.Reset();
        Expect(t3.src(1) == Src::None && !t3.carry(1).have, "Reset forgets it all");
    }
}

void TestFillDirections() {
    printf("the fill direction: a fixed direction of the wrist's frame, the same from anywhere\n");
    const Config base = OrbsOn();
    // the arms held out, palms down: the forearm along -z, the back of the hand up; the left thumb to the body's middle (+x), the right one to -x
    {
        const Place l = Centre(LeftArm(), Grip{}, base, 1), r = Centre(RightArm(), Grip{}, base, 0);
        Expect(l.hasArm && Near(l.back.y, 1.0f, 1e-6f) && Near(l.axis.z, -1.0f, 1e-6f) && Near(l.thumb.x, 1.0f, 1e-6f) && Near(r.thumb.x, -1.0f, 1e-6f), "the frame: back up, along -z, the left thumb +x, the right thumb -x");
        const struct { int fill; V3 left, right; const char* what; } cases[] = {
            {handvitals::kFillElbow, {0, 0, -1}, {0, 0, -1}, "along the arm, the elbow side full: the level rises toward the hand"},
            {handvitals::kFillHand, {0, 0, 1}, {0, 0, 1}, "along the arm the other way: the hand side full"},
            {handvitals::kFillThumb, {-1, 0, 0}, {1, 0, 0}, "across the hand, the thumb side full: rises toward the little finger"},
            {handvitals::kFillPinky, {1, 0, 0}, {-1, 0, 0}, "across the hand, the little finger side full: rises toward the thumb"},
            {handvitals::kFillOutOfHand, {0, 1, 0}, {0, 1, 0}, "out of the hand: rises from the skin up, the level parallel to the back of the hand"},
            {handvitals::kFillWorldUp, {0, 1, 0}, {0, 1, 0}, "level in the world"},
        };
        for (const auto& c : cases) {
            const V3 al = FillAxis(c.fill, l), ar = FillAxis(c.fill, r);
            Expect(Len(hset::Sub(al, c.left)) < 1e-5f && Len(hset::Sub(ar, c.right)) < 1e-5f, "%s (left %.1f %.1f %.1f, right %.1f %.1f %.1f)", c.what, al.x, al.y, al.z, ar.x, ar.y, ar.z);
        }
        // the thumb side of each hand is where the liquid sits for 'thumb side full'
        Expect(Near(hset::Dot(FillAxis(handvitals::kFillThumb, l), l.thumb), -1.0f, 1e-6f) && Near(hset::Dot(FillAxis(handvitals::kFillThumb, r), r.thumb), -1.0f, 1e-6f),
               "the thumb-full axis points away from the thumb on both hands (the liquid is on the low side)");
    }
    // the palm turned up: the back of the hand is down. The wrist-frame choices turn with the hand, the world's one does not
    {
        const Arm up = ArmAt({0.2f, -0.25f, -0.15f}, {0.2f, -0.25f, -0.45f}, {0.0f, 1.0f, 0.0f});
        const Place p = Centre(up, Grip{}, base, 0);
        Expect(Near(FillAxis(handvitals::kFillOutOfHand, p).y, -1.0f, 1e-6f) && Near(FillAxis(handvitals::kFillWorldUp, p).y, 1.0f, 1e-6f), "palm up: out of the hand is down, the world's level is still up");
    }
    // each choice in the state: the orb's axis, back and along
    {
        for (int f = 0; f <= 5; ++f) {
            In in = Base();
            in.cfg.orbFill = f;
            State st;
            const Set s = Step(st, in);
            const V3 want = FillAxis(f, Centre(LeftArm(), Grip{}, in.cfg, 1));
            Expect(Len(hset::Sub(s.orb[kLife].axis, want)) < 1e-6f && Near(s.orb[kLife].back.y, 1.0f, 1e-6f) && Near(s.orb[kLife].along.z, -1.0f, 1e-6f), "Step, fill %d: the axis, the back and the along of the orb", f);
        }
        In none = Base();
        none.arm[0].ok = none.arm[1].ok = false;
        none.grip[0].ok = none.grip[1].ok = true;
        none.grip[0].pos = {0.25f, -0.2f, -0.5f};
        none.grip[1].pos = {-0.25f, -0.2f, -0.5f};
        State st;
        const Set s = Step(st, none);
        Expect(s.any && Near(s.orb[kLife].axis.y, 1.0f, 1e-6f) && Near(s.orb[kLife].back.y, 1.0f, 1e-6f), "no frame at all: up");
    }
    // the same wrist seen from two places gives the same fill direction in the world: the frame is the hero's, the eyes only subtract their own position
    {
        WorldArm m;
        m.elbow = {32.0f, 2.0f, -11.0f};
        m.wrist = {35.0f, 1.8f, -16.0f};
        m.across = hset::Unit(V3{0.1f, -1.0f, 0.2f});
        m.point = hset::Unit(V3{0.3f, 0.1f, -1.0f});
        m.face = hset::Unit(V3{0.0f, -1.0f, 0.1f});
        m.hand = true;
        const float eyeA[3] = {30.5f, 3.0f, -10.0f}, eyeB[3] = {36.0f, 3.5f, -20.0f};   // the head somewhere else
        for (int f = 0; f <= 5; ++f) {
            Config cfg = OrbsOn();
            cfg.orbFill = f;
            In a, b;
            a.cfg = b.cfg = cfg;
            a.vitalsOk = b.vitalsOk = true;
            a.v = b.v = Full();
            a.time = b.time = 50.0;
            a.arm[1] = ArmFromWorld(eyeA, 8.0f, m);
            b.arm[1] = ArmFromWorld(eyeB, 8.0f, m);
            State sa, sb;
            const Set s1 = Step(sa, a), s2 = Step(sb, b);
            const float camR1[3] = {1, 0, 0}, camF1[3] = {0, 0, -1}, camR2[3] = {0.6f, 0, 0.8f}, camF2[3] = {-0.8f, 0, 0.6f};   // and the head turned
            const Uniforms u1 = Pack(s1, camR1, camF1, 8.0f, 1.0), u2 = Pack(s2, camR2, camF2, 8.0f, 1.0);
            bool same = true;
            for (int k = 0; k < 3; ++k) same = same && Near(u1.axis[kLife][k], u2.axis[kLife][k], 1e-5f) && Near(u1.back[kLife][k], u2.back[kLife][k], 1e-5f) && Near(u1.along[kLife][k], u2.along[kLife][k], 1e-5f);
            Expect(same && Len(hset::Sub(s1.orb[kLife].axis, s2.orb[kLife].axis)) < 1e-5f, "fill %d: seen from two places and with the head turned, the fill axis, the back and the along are the same world vectors", f);
            if (f != handvitals::kFillWorldUp) Expect(Len(hset::Sub(s1.orb[kLife].c, s2.orb[kLife].c)) > 0.1f, "(while the ball's place relative to the eyes is not)");
        }
    }
    // the medallion's numbers reach the effect
    {
        In in = Base();
        in.cfg.orbShape = handvitals::kShapeMedallion;
        in.cfg.orbPatina = 0.3f;
        in.cfg.orbMetal = 2;
        in.cfg.orbDiscFaces = 1;
        State st;
        Set s = Step(st, in);
        const float camR[3] = {1, 0, 0}, camF[3] = {0, 0, -1};
        Uniforms u = Pack(s, camR, camF, 4.0f, 1.0);
        Expect(u.med[0] == 1.0f && Near(u.med[1], 0.3f, 1e-6f) && u.med[2] == 2.0f && u.med[3] == 1.0f, "OrbMed: medallion, patina 0.3, silver, the disc fixed");
        Expect(Near(u.back[kLife][1], 1.0f, 1e-6f) && Near(u.along[kLife][2], -1.0f, 1e-6f), "OrbBack and OrbAlong: the wrist's frame");
        in.cfg.orbShape = handvitals::kShapeBall;
        in.cfg.orbMetal = 1;
        in.cfg.orbDiscFaces = 0;
        State st2;
        u = Pack(Step(st2, in), camR, camF, 4.0f, 1.0);
        Expect(u.med[0] == 0.0f && u.med[2] == 1.0f && u.med[3] == 0.0f, "the full ball, bronze, the disc to the eyes");
        Config c;
        c.orbShape = 9;
        c.orbPatina = 3.0f;
        c.orbMetal = 7;
        c.orbDiscFaces = 5;
        Config k = handvitals::Sanitized(c);
        Expect(k.orbShape == handvitals::kShapeMedallion && k.orbPatina == 1.0f && k.orbMetal == 0 && k.orbDiscFaces == 1, "Sanitized: a shape or metal that is not one is the default, the patina held in 0 - 1");
        c.orbPatina = std::nanf("");
        Expect(Near(handvitals::Sanitized(c).orbPatina, 0.55f, 1e-6f), "a patina that is not a number: 55 %%");
        const Config d;
        Expect(d.orbShape == handvitals::kShapeMedallion && d.orbFill == handvitals::kFillOutOfHand && d.orbMetal == 0 && d.orbDiscFaces == 1 && Near(d.orbPatina, 0.55f, 1e-6f), "the defaults: a medallion, out of the hand, gold, the disc fixed, patina 55 %%");
    }
    // the medallion's geometry from the Size slider: the base's diameter, the dome a little smaller, a hemisphere
    {
        Expect(Near(BaseRadius(5.0f), 0.025f, 1e-7f) && Near(2.0f * BaseRadius(7.0f), 0.07f, 1e-7f), "Size is the medallion's (the base's) diameter");
        const float rb = BaseRadius(5.0f);
        Expect(DomeRadius(rb) < rb && DomeRadius(rb) > 0.7f * rb && Near(DomeHeight(rb), DomeRadius(rb), 1e-9f), "the dome is a hemisphere a little narrower than the base (%.1f of it)", DomeRadius(rb) / rb);
        Expect(PlateThickness(rb) > 0.1f * rb && PlateThickness(rb) < 0.3f * rb && kBezelOuter > kDomeR && kBezelOuter < 1.0f && kBezelH < 0.2f, "the plate, the bezel ring between the dome and the rim");
    }
}

// ---- the medallion's own turn about the wrist's three axes -----------------------------------------------------------------------------------------------------------------------------

V3 Mirrored(V3 v) { return {-v.x, v.y, v.z}; }   // across the sagittal plane (x = 0)
bool SameV(V3 a, V3 b, float eps = 1e-5f) { return Len(hset::Sub(a, b)) < eps; }

void TestRotation() {
    printf("the medallion's turn about the wrist's three axes\n");
    // the maths
    {
        Expect(SameV(RotateAbout({1, 0, 0}, {0, 0, 1}, 90.0f), {0, 1, 0}), "90 degrees about z takes x to y (the right-hand rule)");
        Expect(SameV(RotateAbout({0, 0, 1}, {0, 0, 1}, 77.0f), {0, 0, 1}) && SameV(RotateAbout({1, 2, 3}, {0, 1, 0}, 360.0f), {1, 2, 3}, 1e-4f), "the axis stays; a whole turn is none");
        const V3 u = hset::Unit(V3{1, 2, -1});
        Expect(Near(Len(RotateAbout({0.3f, -0.4f, 0.8f}, u, 123.0f)), Len(V3{0.3f, -0.4f, 0.8f}), 1e-5f), "a turn keeps the length");
        Expect(SameV(RotateAbout(RotateAbout({0.3f, 0.2f, 0.9f}, u, 40.0f), u, -40.0f), {0.3f, 0.2f, 0.9f}, 1e-5f), "and is undone by the opposite one");
    }
    // the arms held out, palms down: along -z, the back up, the left thumb to +x (the right to -x)
    const Config base = OrbsOn();
    auto frameOf = [&](const Arm& arm, int side, float rx, float ry, float rz) {
        Config c = base;
        (side == 1 ? c.orbLeft : c.orbRight).rotXDeg = rx;
        (side == 1 ? c.orbLeft : c.orbRight).rotYDeg = ry;
        (side == 1 ? c.orbLeft : c.orbRight).rotZDeg = rz;
        Place p = Centre(arm, Grip{}, c, side);
        TurnFrame(p, SpotFor(c, side), side);
        return p;
    };
    {
        const Place p0 = frameOf(LeftArm(), 1, 0, 0, 0);
        Place q = Centre(LeftArm(), Grip{}, base, 1);
        Expect(SameV(p0.back, q.back, 1e-9f) && SameV(p0.axis, q.axis, 1e-9f) && SameV(p0.thumb, q.thumb, 1e-9f) && SameV(p0.c, q.c, 1e-9f), "no turn: the frame exactly as it was (default 0 / 0 / 0 keeps today's look)");
        // X: round the hand's axis: + turns the face (the back) toward the thumb, the along stays
        Place l = frameOf(LeftArm(), 1, 90, 0, 0);
        Expect(SameV(l.back, {1, 0, 0}) && SameV(l.axis, {0, 0, -1}) && SameV(l.thumb, {0, -1, 0}), "X +90 on the left: the back goes to the thumb side (%.2f %.2f %.2f), the along stays, the thumb sinks", l.back.x, l.back.y, l.back.z);
        // Y: round the across axis: + turns the face toward the elbow, the along turns up to the back
        l = frameOf(LeftArm(), 1, 0, 90, 0);
        Expect(SameV(l.back, {0, 0, 1}) && SameV(l.axis, {0, 1, 0}) && SameV(l.thumb, {1, 0, 0}), "Y +90: the back goes toward the elbow (%.2f %.2f %.2f), the along comes up, the thumb stays", l.back.x, l.back.y, l.back.z);
        // Z: round the axis out of the back: + turns the along from the knuckles toward the little finger, the back stays
        l = frameOf(LeftArm(), 1, 0, 0, 90);
        Expect(SameV(l.back, {0, 1, 0}) && SameV(l.axis, {-1, 0, 0}) && SameV(l.thumb, {0, 0, -1}), "Z +90: the along goes to the little finger side (%.2f %.2f %.2f), the back stays", l.axis.x, l.axis.y, l.axis.z);
        // the same numbers on the right hand: the same anatomical turn (toward ITS thumb, which is on the other side of the room)
        Place r = frameOf(RightArm(), 0, 90, 0, 0);
        Expect(SameV(r.back, {-1, 0, 0}) && SameV(r.axis, {0, 0, -1}), "X +90 on the right: the back goes to ITS thumb side, -x (%.2f %.2f %.2f)", r.back.x, r.back.y, r.back.z);
        r = frameOf(RightArm(), 0, 0, 90, 0);
        Expect(SameV(r.back, {0, 0, 1}) && SameV(r.axis, {0, 1, 0}), "Y +90 on the right: the face toward the elbow, as on the left");
        r = frameOf(RightArm(), 0, 0, 0, 90);
        Expect(SameV(r.axis, {1, 0, 0}) && SameV(r.back, {0, 1, 0}), "Z +90 on the right: the along to ITS little finger side, +x");
        // the order: X, then Y, then Z, each about the wrist's own (original) axis
        l = frameOf(LeftArm(), 1, 30, -50, 70);
        const V3 A{0, 0, -1}, T{1, 0, 0}, B{0, 1, 0};
        V3 want = RotateAbout(RotateAbout(RotateAbout(B, A, 30.0f), T, -50.0f), B, 70.0f);
        Expect(SameV(l.back, want, 1e-5f), "X, then Y, then Z about the original axes (the back: %.3f %.3f %.3f)", l.back.x, l.back.y, l.back.z);
        want = RotateAbout(RotateAbout(RotateAbout(A, A, 30.0f), T, -50.0f), B, 70.0f);
        Expect(SameV(l.axis, want, 1e-5f), "...and the along");
        // still a square frame, still unit
        Expect(Near(Len(l.back), 1, 1e-5f) && Near(Len(l.axis), 1, 1e-5f) && Near(Len(l.thumb), 1, 1e-5f) && Near(hset::Dot(l.back, l.axis), 0, 1e-5f) && Near(hset::Dot(l.back, l.thumb), 0, 1e-5f) &&
                   Near(hset::Dot(l.axis, l.thumb), 0, 1e-5f), "an orthonormal frame after any turn");
        // the middle stays where it was: the turn is about the medallion's own centre
        Expect(SameV(l.c, q.c, 1e-9f), "the medallion's middle does not move with its turn");
        // the ends of the range and what is not a number
        Place e = frameOf(LeftArm(), 1, 180, 0, 0);
        Expect(SameV(e.back, {0, -1, 0}) && SameV(e.thumb, {-1, 0, 0}), "+180 round the hand: the back is down, the thumb on the other side");
        Expect(SameV(frameOf(LeftArm(), 1, -180, 0, 0).back, {0, -1, 0}), "-180 the same");
        Config wild = base;
        wild.orbLeft.rotXDeg = 900.0f;
        wild.orbLeft.rotYDeg = std::nanf("");
        wild.orbLeft.rotZDeg = -1.0e9f;
        const Config s = handvitals::Sanitized(wild);
        Expect(s.orbLeft.rotXDeg == 180.0f && s.orbLeft.rotYDeg == 0.0f && s.orbLeft.rotZDeg == -180.0f, "the angles are held in -180 .. 180, a number that is not one is 0");
        Place w = Centre(LeftArm(), Grip{}, wild, 1);
        TurnFrame(w, SpotFor(wild, 1), 1);
        Expect(Near(Len(w.back), 1.0f, 1e-5f) && Finite(w.back), "TurnFrame survives them too");
    }
    // the turn in the state: the orb's back, along and fill direction go with it, the fill options turn with the medallion and the world's one does not
    {
        for (int f = 0; f <= 5; ++f) {
            In in = Base();
            in.cfg.orbFill = f;
            in.cfg.orbLeft.rotXDeg = 40.0f;
            in.cfg.orbLeft.rotYDeg = -25.0f;
            in.cfg.orbLeft.rotZDeg = 60.0f;
            State st;
            const Set s = Step(st, in);
            Place pl = Centre(LeftArm(), Grip{}, in.cfg, 1);
            TurnFrame(pl, SpotFor(in.cfg, 1), 1);
            Expect(s.orb[kLife].on && SameV(s.orb[kLife].back, pl.back) && SameV(s.orb[kLife].along, pl.axis) && SameV(s.orb[kLife].axis, FillAxis(f, pl)), "fill %d: the orb carries the turned back, along and fill axis", f);
            if (f == handvitals::kFillWorldUp) Expect(SameV(s.orb[kLife].axis, {0, 1, 0}), "level in the world is not turned");
            if (f == handvitals::kFillOutOfHand) Expect(SameV(s.orb[kLife].axis, s.orb[kLife].back), "out of the hand stays the medallion's own up, whatever its turn");
            // the other wrist (mana, right) has its own set: untouched here
            Place pr = Centre(RightArm(), Grip{}, in.cfg, 0);
            Expect(SameV(s.orb[kMana].back, pr.back) && SameV(s.orb[kMana].along, pr.axis), "the right wrist's medallion has its own set: not turned by the left's");
        }
        // the uniforms carry the turned vectors
        In in = Base();
        in.cfg.orbLeft.rotZDeg = 90.0f;
        in.cfg.orbFill = handvitals::kFillOutOfHand;
        State st;
        const Set s = Step(st, in);
        const float camR[3] = {1, 0, 0}, camF[3] = {0, 0, -1};
        const Uniforms u = Pack(s, camR, camF, 4.0f, 1.0);
        Expect(Near(u.along[kLife][0], -1.0f, 1e-5f) && Near(u.back[kLife][1], 1.0f, 1e-5f) && Near(u.along[kMana][2], -1.0f, 1e-5f), "OrbAlong turned a quarter on the left (to -x), the right's untouched");
    }
    // mirror: the left wrist's set on the right wrist gives the mirror image of the left's medallion - positions, frame, fill axis, whatever the angles (the right arm is the left reflected)
    {
        const Arm L = HandArm({-0.27f, -0.12f, -0.41f}, {-0.21f, -0.31f, -0.12f}, hset::Unit(V3{-0.2f, 0.3f, -0.9f}), hset::Unit(V3{-0.2f, -0.8f, 0.3f}));
        const Arm R = HandArm({0.27f, -0.12f, -0.41f}, {0.21f, -0.31f, -0.12f}, hset::Unit(V3{0.2f, 0.3f, -0.9f}), hset::Unit(V3{0.2f, -0.8f, 0.3f}));
        const float angles[][3] = {{30, -40, 70}, {90, 0, 0}, {0, 90, 0}, {0, 0, 90}, {-120, 45, 170}, {180, -90, 30}};
        for (const auto& a : angles) {
            for (int f = 0; f <= 5; ++f) {
                In in;
                in.cfg = OrbsOn();
                in.cfg.orbHands = 0;                 // life on the left, mana on the right
                in.cfg.orbMirror = true;
                in.cfg.orbFill = f;
                in.cfg.orbLeft = {2.5f, 3.0f, 5.0f, a[0], a[1], a[2]};
                in.cfg.orbRight = {9.0f, -8.0f, -7.0f, 11.0f, 22.0f, 33.0f};   // not used while mirrored
                in.vitalsOk = true;
                in.v = Full();
                in.arm[1] = L;
                in.arm[0] = R;
                in.time = 20.0;
                State st;
                const Set s = Step(st, in);
                const Orb& lo = s.orb[kLife];
                const Orb& ro = s.orb[kMana];
                const bool same = SameV(ro.c, Mirrored(lo.c)) && SameV(ro.back, Mirrored(lo.back)) && SameV(ro.along, Mirrored(lo.along)) && SameV(ro.axis, Mirrored(lo.axis));
                Expect(lo.on && ro.on && same, "mirror (%g %g %g), fill %d: the right medallion is the left's reflected across the body's middle", a[0], a[1], a[2], f);
            }
        }
        // the same numbers typed on the right wrist, mirror off: the same mirror image (the angles are anatomical)
        In in;
        in.cfg = OrbsOn();
        in.cfg.orbLeft = {2.5f, 3.0f, 5.0f, 30, -40, 70};
        in.cfg.orbRight = in.cfg.orbLeft;
        in.vitalsOk = true;
        in.v = Full();
        in.arm[1] = L;
        in.arm[0] = R;
        in.time = 20.0;
        State st;
        const Set s = Step(st, in);
        Expect(SameV(s.orb[kMana].back, Mirrored(s.orb[kLife].back)) && SameV(s.orb[kMana].along, Mirrored(s.orb[kLife].along)) && SameV(s.orb[kMana].c, Mirrored(s.orb[kLife].c)),
               "the same three angles typed on both wrists are the mirror image too");
        // and the wrong way - the angles not negated on the right - would not be: this is what the sign does
        Place pl = Centre(L, Grip{}, in.cfg, 1);
        TurnFrame(pl, in.cfg.orbLeft, 1);
        Place pr = Centre(R, Grip{}, in.cfg, 0);
        const handvitals::OrbSpot spot = in.cfg.orbRight;
        const V3 A = pr.axis, T = pr.thumb, B = pr.back;
        const V3 wrongBack = hset::Unit(RotateAbout(RotateAbout(RotateAbout(B, A, spot.rotXDeg), T, spot.rotYDeg), B, spot.rotZDeg));
        Expect(!SameV(wrongBack, Mirrored(pl.back), 1e-3f), "(without the right hand's sign the medallion would not be the mirror image)");
    }
}

// ---- a wrist that wears a shield rolls its ball to the other side of the wrist -------------------------------------------------------------------------------------------------

bool Exact(V3 a, V3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

void TestUnderShield() {
    printf("under a shield: the ball rolls to the other side of the wrist\n");
    const Config cfg = OrbsOn();   // 3 cm along, 0 across, 4 cm up; the arms held out along -z, palms down: the back of the hand is up, the left thumb on +x, the right on -x
    const V3 lw = LeftArm().wrist, rw = RightArm().wrist;

    // the settings: on by default, a half turn, nothing added; held in range
    {
        Config d;
        Expect(d.orbUnderShield && d.orbShield.turnDeg == 180.0f && d.orbShield.alongCm == 0.0f && d.orbShield.sideCm == 0.0f && d.orbShield.upCm == 0.0f &&
                   d.orbShield.rotXDeg == 0.0f && d.orbShield.rotYDeg == 0.0f && d.orbShield.rotZDeg == 0.0f,
               "defaults: ticked, a half turn (180), no offsets, no extra angles");
        Config w;
        w.orbShield.turnDeg = 900.0f;
        w.orbShield.alongCm = -50.0f;
        w.orbShield.sideCm = std::numeric_limits<float>::quiet_NaN();
        w.orbShield.upCm = 50.0f;
        w.orbShield.rotXDeg = -999.0f;
        w.orbShield.rotYDeg = std::numeric_limits<float>::infinity();
        w.orbShield.rotZDeg = 181.0f;
        const Config s = handvitals::Sanitized(w);
        Expect(s.orbShield.turnDeg == 180.0f && s.orbShield.alongCm == handvitals::kOrbSpotMin && s.orbShield.sideCm == 0.0f && s.orbShield.upCm == handvitals::kOrbSpotMax &&
                   s.orbShield.rotXDeg == -180.0f && s.orbShield.rotYDeg == 0.0f && s.orbShield.rotZDeg == 180.0f,
               "out of range is held in range, a number that is not one takes its default (turn 180, the rest 0)");
        Config t;
        t.orbShield.turnDeg = std::numeric_limits<float>::quiet_NaN();
        Expect(handvitals::Sanitized(t).orbShield.turnDeg == 180.0f, "a turn that is not a number is the half turn");
    }

    // the ease
    {
        Expect(Eased(0.0f) == 0.0f && Eased(1.0f) == 1.0f && Near(Eased(0.5f), 0.5f, 1e-6f), "the ease: 0 -> 0, 1 -> 1, half -> half");
        bool mono = true;
        for (int i = 0; i < 100; ++i) mono = mono && Eased((i + 1) * 0.01f) >= Eased(i * 0.01f);
        Expect(mono && Eased(-3.0f) == 0.0f && Eased(7.0f) == 1.0f && Eased(std::numeric_limits<float>::quiet_NaN()) == 0.0f, "...never goes back, is held in 0 - 1, and survives NaN");
    }

    // which wrist wears the shield
    {
        Expect(ShieldWrist(1, true) == 1 && ShieldWrist(0, true) == 1 && ShieldWrist(-1, true) == 1, "a shield in the left slot is on the left arm: strapped there, on the hand, or not strapped");
        Expect(ShieldWrist(3, true) == 0, "strapped to the RIGHT forearm (the hand swap): the right wrist");
        Expect(ShieldWrist(1, false) == -1 && ShieldWrist(3, false) == -1 && ShieldWrist(0, false) == -1, "no shield in the left slot: none (a stale strap does not count)");
        Expect(ShieldWrist(2, true) == -1, "a shield put away at the left shoulder ([hands] shoulder_draw_shield_stow) is on no wrist: the ball stays on the back of the hand");
    }

    // Centre: not under a shield it is exactly what it was; fully under, the other side of the wrist
    {
        const Place l0 = Centre(LeftArm(), Grip{}, cfg, 1), l0b = Centre(LeftArm(), Grip{}, cfg, 1, 0.0f, 0.0f, 0.0f);
        Expect(l0.ok && Exact(l0.c, l0b.c) && Exact(l0.back, l0b.back) && Exact(l0.thumb, l0b.thumb) && Exact(l0.axis, l0b.axis), "under 0: exactly the old place");
        Expect(SameV(l0.c, {lw.x, lw.y + 0.04f, lw.z - 0.03f}), "(the back of the left hand: 4 cm up, 3 cm along)");
        const Place l1 = Centre(LeftArm(), Grip{}, cfg, 1, 0.0f, 0.0f, 1.0f);
        Expect(l1.ok && SameV(l1.c, {lw.x, lw.y - 0.04f, lw.z - 0.03f}), "left wrist, under 1: on the inner side - 4 cm BELOW the wrist, the same 3 cm along (%.3f %.3f %.3f)", l1.c.x, l1.c.y, l1.c.z);
        Expect(SameV(l1.back, {0, -1, 0}) && SameV(l1.thumb, {-1, 0, 0}) && SameV(l1.axis, {0, 0, -1}), "...its back points down, the thumb has gone round to the other side, the along stays");
        const Place r1 = Centre(RightArm(), Grip{}, cfg, 0, 0.0f, 0.0f, 1.0f);
        Expect(r1.ok && SameV(r1.c, {rw.x, rw.y - 0.04f, rw.z - 0.03f}) && SameV(r1.back, {0, -1, 0}) && SameV(r1.thumb, {1, 0, 0}), "right wrist the same: below it, its thumb round on +x");
        // a roll of the arm: the ball goes round with the arm - palm up, the inner side is up
        const Arm palmUp = ArmAt({-0.2f, -0.25f, -0.15f}, {-0.2f, -0.25f, -0.45f}, {0.0f, 1.0f, 0.0f});
        const Place pu = Centre(palmUp, Grip{}, cfg, 1, 0.0f, 0.0f, 1.0f);
        Expect(SameV(pu.c, {-0.2f, -0.25f + 0.04f, -0.48f}), "palm up: the inner side of the wrist is up, the ball with it");
        // the middle of the whole thing stays on the circle round the hand's axis: the same distance from the wrist's line at every roll
        for (int k = 0; k <= 8; ++k) {
            const Place m = Centre(LeftArm(), Grip{}, cfg, 1, 0.0f, 0.0f, k / 8.0f);
            const V3 d = hset::Sub(m.c, lw);
            Expect(Near(hset::Dot(d, {0, 0, -1}), 0.03f, 1e-5f) && Near(std::sqrt(d.x * d.x + d.y * d.y), 0.04f, 1e-5f), "roll %d / 8: 3 cm along, always 4 cm round the axis", k);
        }
        // a part of the turn: the direction is anatomical - + sends the back toward the thumb, on either hand
        Config q = cfg;
        q.orbShield.turnDeg = 90.0f;
        Place pl = Centre(LeftArm(), Grip{}, q, 1, 0.0f, 0.0f, 1.0f), pr = Centre(RightArm(), Grip{}, q, 0, 0.0f, 0.0f, 1.0f);
        Expect(SameV(pl.back, {1, 0, 0}) && SameV(pl.c, {lw.x + 0.04f, lw.y, lw.z - 0.03f}), "turn +90, left: the back goes over the thumb side (+x)");
        Expect(SameV(pr.back, {-1, 0, 0}) && SameV(pr.c, {rw.x - 0.04f, rw.y, rw.z - 0.03f}), "turn +90, right: over ITS thumb side (-x): the mirror image");
        q.orbShield.turnDeg = -90.0f;
        pl = Centre(LeftArm(), Grip{}, q, 1, 0.0f, 0.0f, 1.0f);
        Expect(SameV(pl.back, {-1, 0, 0}), "turn -90, left: over the little finger side");
        // half way through the ramp value: half the turn
        pl = Centre(LeftArm(), Grip{}, cfg, 1, 0.0f, 0.0f, 0.5f);
        Expect(SameV(pl.back, {1, 0, 0}) && SameV(pl.c, {lw.x + 0.04f, lw.y, lw.z - 0.03f}), "under 0.5 with a half turn: a quarter turn, on the thumb side of the wrist");
        // the offsets are in the rolled frame, scaled by `under`
        Config o = cfg;
        o.orbShield.alongCm = 1.0f;
        o.orbShield.sideCm = 3.0f;
        o.orbShield.upCm = 2.0f;
        Place po = Centre(LeftArm(), Grip{}, o, 1, 0.0f, 0.0f, 1.0f);
        Expect(SameV(po.c, {lw.x - 0.03f, lw.y - 0.06f, lw.z - 0.04f}), "offsets: 1 along, 2 farther out of the skin (down), 3 across - along the rolled frame's thumb axis, which at the half turn points to the little finger side (-x) (%.3f %.3f %.3f)", po.c.x, po.c.y, po.c.z);
        po = Centre(LeftArm(), Grip{}, o, 1, 0.0f, 0.0f, 0.0f);
        Expect(Exact(po.c, l0.c), "...and none of them without the shield");
        po = Centre(LeftArm(), Grip{}, o, 1, 0.0f, 0.0f, 0.5f);
        Expect(Near(hset::Dot(hset::Sub(po.c, lw), {0, 0, -1}), 0.03f + 0.005f, 1e-5f), "...half of them at half the roll");
        // the hero class's depth offset is the skin's: it rides the rolled Z too
        po = Centre(LeftArm(), Grip{}, cfg, 1, 0.0f, 1.5f, 1.0f);
        Expect(SameV(po.c, {lw.x, lw.y - 0.055f, lw.z - 0.03f}), "a class depth of +1.5 cm: out of the skin on the new side as well");
        // two balls on one wrist: the lateral shift goes round with the roll
        const Place a = Centre(LeftArm(), Grip{}, cfg, 1, -0.03f, 0.0f, 1.0f), b = Centre(LeftArm(), Grip{}, cfg, 1, 0.03f, 0.0f, 1.0f);
        Expect(Near(Len(hset::Sub(a.c, b.c)), 0.06f, 1e-5f) && Near(a.c.y, b.c.y, 1e-5f) && !Near(a.c.x, b.c.x, 1e-3f), "two balls on a wrist stay side by side across the hand, on the new side");
        // no frame: the controller's point alone cannot roll
        Grip g;
        g.ok = true;
        g.pos = {0.1f, 0.2f, -0.3f};
        const Place gp = Centre(Arm{}, g, cfg, 1, 0.0f, 0.0f, 1.0f), gp0 = Centre(Arm{}, g, cfg, 1);
        Expect(gp.ok && Exact(gp.c, gp0.c) && !gp.hasArm, "no wrist frame: the grip point stands in, unrolled");
        // bad numbers
        Config bad = cfg;
        bad.orbShield.turnDeg = std::numeric_limits<float>::quiet_NaN();
        bad.orbShield.upCm = std::numeric_limits<float>::infinity();
        const Place pb = Centre(LeftArm(), Grip{}, bad, 1, 0.0f, 0.0f, 1.0f);
        Expect(pb.ok && Finite(pb.c) && Finite(pb.back), "numbers that are not numbers do not break it");
        Expect(Centre(LeftArm(), Grip{}, cfg, 1, 0.0f, 0.0f, std::numeric_limits<float>::quiet_NaN()).ok && Exact(Centre(LeftArm(), Grip{}, cfg, 1, 0.0f, 0.0f, std::numeric_limits<float>::quiet_NaN()).c, l0.c),
               "an `under` that is not a number is none");
    }

    // SpotUnder: the shield's angles are added to the wrist's own, wrapped
    {
        handvitals::OrbSpot s;
        s.rotXDeg = 170.0f;
        s.rotYDeg = -20.0f;
        handvitals::OrbUnderShield us;
        us.rotXDeg = 30.0f;
        us.rotYDeg = 10.0f;
        us.rotZDeg = -45.0f;
        const handvitals::OrbSpot none = SpotUnder(s, us, 0.0f), full = SpotUnder(s, us, 1.0f), half = SpotUnder(s, us, 0.5f);
        Expect(none.rotXDeg == 170.0f && none.rotYDeg == -20.0f && none.rotZDeg == 0.0f, "u 0: the wrist's own turn exactly");
        Expect(Near(full.rotXDeg, -160.0f, 1e-4f) && Near(full.rotYDeg, -10.0f, 1e-4f) && Near(full.rotZDeg, -45.0f, 1e-4f), "u 1: added, 170 + 30 wrapped to -160");
        Expect(Near(half.rotXDeg, 185.0f - 360.0f, 1e-4f) && Near(half.rotYDeg, -15.0f, 1e-4f) && Near(half.rotZDeg, -22.5f, 1e-4f), "u 0.5: half of them");
    }

    // Step: the ramp, the snap at the start, the other wrist, the tick, the fill direction
    {
        const auto lifeAt = [&](const Set& st) { return st.orb[kLife]; };   // (life is on the left wrist, mana on the right)
        const Place l0 = Centre(LeftArm(), Grip{}, cfg, 1), l1 = Centre(LeftArm(), Grip{}, cfg, 1, 0.0f, 0.0f, 1.0f);

        // a game that starts with a shield on: the ball is on the inner side at once, no slide
        State st;
        In in = Base();
        in.shieldWrist = 1;
        Set s = Step(st, in);
        Expect(lifeAt(s).on && SameV(lifeAt(s).c, l1.c) && SameV(lifeAt(s).back, {0, -1, 0}), "a first look with a shield on: already on the inner side (no slide at the start)");
        Expect(SameV(s.orb[kMana].c, Centre(RightArm(), Grip{}, cfg, 0).c), "the other wrist (mana, right) is where it always was");
        // the fill direction is the wrist's frame: out of the skin follows the roll, the world's up does not
        Expect(SameV(lifeAt(s).axis, {0, -1, 0}), "the liquid fills out of the skin where the ball is now (down)");
        {
            State sw;
            In iw = in;
            iw.cfg.orbFill = handvitals::kFillWorldUp;
            Expect(SameV(lifeAt(Step(sw, iw)).axis, {0, 1, 0}), "...and level in the world stays level in the world");
        }
        // the shield leaves: it rolls back in kUnderShieldS, smoothly, one way
        in.shieldWrist = -1;
        const float dt = 1.0f / 72.0f;
        float lastAng = 1e9f, maxStep = 0.0f;
        bool one = true;
        int framesToRest = -1;
        for (int i = 0; i < 40; ++i) {
            in.time += dt;
            s = Step(st, in);
            const V3 d = hset::Sub(lifeAt(s).c, lw);
            const float ang = std::atan2(d.x, d.y) * 57.29578f;   // 180 on the inner side, 0 on the back of the hand
            if (lastAng < 1e8f) {
                one = one && ang <= lastAng + 1e-3f;
                maxStep = std::max(maxStep, lastAng - ang);
            }
            lastAng = ang;
            if (framesToRest < 0 && SameV(lifeAt(s).c, l0.c, 1e-6f)) framesToRest = i + 1;
        }
        Expect(one, "rolling back: the ball only ever moves one way round");
        Expect(maxStep > 1.0f && maxStep < 15.0f, "...in steps of a few degrees a frame (the biggest %.1f), no jump", maxStep);
        Expect(framesToRest >= 20 && framesToRest <= 24, "...and rests on the back of the hand after %d frames at 72 Hz (about %.2f s)", framesToRest, kUnderShieldS);
        Expect(Exact(lifeAt(s).c, l0.c) && SameV(lifeAt(s).back, {0, 1, 0}), "...exactly where it was before the shield");

        // half way: 0.15 s on the clock is half the ramp, smoothstep 0.5, a quarter turn: on the thumb side of the wrist
        {
            State sh;
            In ih = Base();
            ih.shieldWrist = 1;
            Step(sh, ih);
            ih.shieldWrist = -1;
            for (int i = 0; i < 5; ++i) {
                ih.time += 0.03;
                s = Step(sh, ih);
            }
            Expect(SameV(lifeAt(s).c, {lw.x + 0.04f, lw.y, lw.z - 0.03f}, 1e-4f), "0.15 s into the roll back: half way (%.3f %.3f %.3f)", lifeAt(s).c.x, lifeAt(s).c.y, lifeAt(s).c.z);
            // and the shield comes back before it has finished: it turns round again from where it is, no jump
            ih.shieldWrist = 1;
            ih.time += dt;
            const V3 before = lifeAt(s).c;
            s = Step(sh, ih);
            Expect(Len(hset::Sub(lifeAt(s).c, before)) < 0.012f, "the shield back mid-roll: it turns round again from where it is (moved %.1f mm)", Len(hset::Sub(lifeAt(s).c, before)) * 1000.0f);
        }

        // the shield goes to the other arm: the old wrist rolls back while the new one rolls over
        {
            State sh;
            In ih = Base();
            ih.shieldWrist = 1;
            Step(sh, ih);
            ih.shieldWrist = 0;
            for (int i = 0; i < 30; ++i) {
                ih.time += dt;
                s = Step(sh, ih);
            }
            Expect(Exact(s.orb[kLife].c, l0.c), "the shield moved to the right arm: the left wrist's ball is back on the back of the hand");
            Expect(SameV(s.orb[kMana].c, Centre(RightArm(), Grip{}, cfg, 0, 0.0f, 0.0f, 1.0f).c) && SameV(s.orb[kMana].back, {0, -1, 0}), "...and the right wrist's ball (mana) is on its inner side");
        }

        // the tick off: the shield changes nothing, bit for bit
        {
            State sa, sb;
            In ia = Base(), ib = Base();
            ia.shieldWrist = 1;
            ia.cfg.orbUnderShield = false;
            ib.shieldWrist = -1;
            bool same = true;
            for (int i = 0; i < 10; ++i) {
                ia.time += dt;
                ib.time += dt;
                const Set a = Step(sa, ia), b = Step(sb, ib);
                for (int k = 0; k < 2; ++k)
                    same = same && Exact(a.orb[k].c, b.orb[k].c) && Exact(a.orb[k].back, b.orb[k].back) && Exact(a.orb[k].along, b.orb[k].along) &&
                           Exact(a.orb[k].axis, b.orb[k].axis) && a.orb[k].fill == b.orb[k].fill && a.orb[k].opacity == b.orb[k].opacity;
            }
            Expect(same, "unticked: a shield on the arm changes nothing (same as without the shield, bit for bit)");
        }
        // ticking it while a shield is on: it rolls over, not a jump
        {
            State sh;
            In ih = Base();
            ih.shieldWrist = 1;
            ih.cfg.orbUnderShield = false;
            Step(sh, ih);
            ih.cfg.orbUnderShield = true;
            ih.time += dt;
            s = Step(sh, ih);
            Expect(Len(hset::Sub(lifeAt(s).c, l0.c)) < 0.01f && !SameV(lifeAt(s).c, l0.c, 1e-5f), "ticked with a shield on: it starts to roll (a first small step)");
        }
        // the numbers are live: new coefficients move the ball in the next step
        {
            State sh;
            In ih = Base();
            ih.shieldWrist = 1;
            Step(sh, ih);
            ih.time += dt;
            ih.cfg.orbShield.upCm = 3.0f;
            s = Step(sh, ih);
            Expect(SameV(lifeAt(s).c, {lw.x, lw.y - 0.07f, lw.z - 0.03f}), "the up coefficient changed: the ball moved at once");
        }
        // both balls on one wrist, the shield on it: both roll, still side by side
        {
            State sh;
            In ih = Base();
            ih.cfg.orbHands = 2;
            Step(sh, ih);   // (no shield yet)
            const Set none = Step(sh, ih);
            State sh2;
            ih.shieldWrist = 1;
            const Set on = Step(sh2, ih);
            const float gap0 = Len(hset::Sub(none.orb[kLife].c, none.orb[kMana].c)), gap1 = Len(hset::Sub(on.orb[kLife].c, on.orb[kMana].c));
            Expect(on.orb[kLife].back.y < -0.99f && on.orb[kMana].back.y < -0.99f && Near(gap0, gap1, 1e-5f), "both balls on the shield's wrist: both on the inner side, the same gap (%.4f / %.4f)", gap0, gap1);
        }
        // the grip alone (no wrist frame): nothing rolls, nothing breaks
        {
            State sh;
            In ih = Base();
            ih.arm[1] = Arm{};
            ih.grip[1].ok = true;
            ih.grip[1].pos = {-0.2f, -0.2f, -0.4f};
            ih.shieldWrist = 1;
            const Set on = Step(sh, ih);
            ih.shieldWrist = -1;
            State sh2;
            const Set off = Step(sh2, ih);
            Expect(on.orb[kLife].on && Exact(on.orb[kLife].c, off.orb[kLife].c), "a wrist known only by the controller's point: unrolled, still drawn");
        }
        // the setting off: nothing is drawn and the ramp is forgotten
        {
            State sh;
            In ih = Base();
            ih.shieldWrist = 1;
            Step(sh, ih);
            ih.cfg.on = false;
            Step(sh, ih);
            Expect(sh.under[0] == 0.0f && sh.under[1] == 0.0f && !sh.haveUnder, "the medallions off: the ramp is forgotten with the rest");
        }
    }
}

// ---- the depth (Z) of the wrist medallions per hero class ------------------------------------------------------------------------------------------------------------------------

void TestHeroDepth() {
    printf("the wrist medallions' depth per hero class\n");
    // the classes, as the game's model folders name them (skel::kHeroClasses)
    {
        const char* keys[8] = {"amazon", "sorceress", "necromancer", "paladin", "barbarian", "druid", "assassin", "warlock"};
        const char* names[8] = {"Amazon", "Sorceress", "Necromancer", "Paladin", "Barbarian", "Druid", "Assassin", "Warlock"};
        bool ok = handvitals::kHeroCount == 8;
        for (int c = 0; c < 8; ++c) ok = ok && !strcmp(handvitals::HeroKey(c), keys[c]) && !strcmp(handvitals::HeroName(c), names[c]);
        Expect(ok, "the eight classes, their ini suffixes and their names");
        Expect(!strcmp(handvitals::HeroKey(-1), "") && !strcmp(handvitals::HeroKey(8), "") && !strcmp(handvitals::HeroName(-1), "unknown"), "a class that is not one has no key");
    }
    // the choice: the class's own value, 0 where it has none or the class is not known
    {
        Config c;
        for (int k = 0; k < 8; ++k) c.orbClassZCm[k] = 0.5f * (float)(k + 1);
        Expect(handvitals::ClassZCm(c, 0) == 0.5f && handvitals::ClassZCm(c, 3) == 2.0f && handvitals::ClassZCm(c, 7) == 4.0f, "each class has its own number");
        Expect(handvitals::ClassZCm(c, -1) == 0.0f && handvitals::ClassZCm(c, 8) == 0.0f && handvitals::ClassZCm(c, 99) == 0.0f, "a class not known (-1, or none of ours): no offset, the wrists' own Z");
        const Config none;
        bool zero = true;
        for (int k = -1; k < 9; ++k) zero = zero && handvitals::ClassZCm(none, k) == 0.0f;
        Expect(zero, "the default: no class has a value of its own, 0 for all");
        Config w;
        w.orbClassZCm[1] = 50.0f;
        w.orbClassZCm[2] = -50.0f;
        w.orbClassZCm[3] = std::nanf("");
        w.orbClassZCm[4] = std::numeric_limits<float>::infinity();
        Expect(handvitals::ClassZCm(w, 1) == 5.0f && handvitals::ClassZCm(w, 2) == -5.0f && handvitals::ClassZCm(w, 3) == 0.0f && handvitals::ClassZCm(w, 4) == 0.0f, "held in -5 .. 5, a number that is not one is 0");
        const Config s = handvitals::Sanitized(w);
        Expect(s.orbClassZCm[1] == 5.0f && s.orbClassZCm[2] == -5.0f && s.orbClassZCm[3] == 0.0f && s.orbClassZCm[4] == 0.0f && s.orbClassZCm[0] == 0.0f, "Sanitized does the same");
    }
    // the offset is on both wrists' Z, while that hero plays
    {
        auto place = [&](const Config& cfg, int hero, int side) {
            In in = Base();
            in.cfg = cfg;
            in.heroClass = hero;
            State st;
            const Set s = Step(st, in);
            return s.orb[side == 1 ? kLife : kMana].c;   // life is on the left wrist (side 1), mana on the right
        };
        Config cfg = OrbsOn();   // Z 4 on both wrists
        cfg.orbClassZCm[3] = 1.5f;     // paladin
        cfg.orbClassZCm[1] = -2.0f;    // sorceress
        const V3 plain0 = place(OrbsOn(), -1, 1), plain1 = place(OrbsOn(), -1, 0);
        const V3 pal0 = place(cfg, 3, 1), pal1 = place(cfg, 3, 0);
        Expect(Near(pal0.y - plain0.y, 0.015f, 1e-6f) && Near(pal1.y - plain1.y, 0.015f, 1e-6f), "a paladin: 1.5 cm more out of the back of the hand, on the left wrist and on the right");
        Expect(Near(pal0.x, plain0.x, 1e-7f) && Near(pal0.z, plain0.z, 1e-7f) && Near(pal1.x, plain1.x, 1e-7f), "and only the depth moves");
        const V3 sor0 = place(cfg, 1, 1), sor1 = place(cfg, 1, 0);
        Expect(Near(sor0.y - plain0.y, -0.02f, 1e-6f) && Near(sor1.y - plain1.y, -0.02f, 1e-6f), "a sorceress: 2 cm into the arm - her own number");
        const V3 amz0 = place(cfg, 0, 1), unk0 = place(cfg, -1, 1), bad0 = place(cfg, 42, 1);
        Expect(Near(amz0.y, plain0.y, 1e-7f) && Near(unk0.y, plain0.y, 1e-7f) && Near(bad0.y, plain0.y, 1e-7f), "an amazon with no value of her own, a hero not known, a class that is none: the wrists' own Z");
        // an ini with no class values behaves exactly as before the classes had any
        bool same = true;
        for (int hero = -1; hero < 8; ++hero) same = same && Near(place(OrbsOn(), hero, 1).y, plain0.y, 1e-9f) && Near(place(OrbsOn(), hero, 0).y, plain1.y, 1e-9f);
        Expect(same, "all zeros: every hero, and none, is where it was");
        // it adds to each wrist's own Z (they may differ), mirrored or not
        Config own = cfg;
        own.orbLeft.upCm = 6.0f;
        own.orbRight.upCm = 2.0f;
        const V3 o0 = place(own, 3, 1), o1 = place(own, 3, 0);
        Expect(Near(o0.y, -0.25f + 0.075f, 1e-6f) && Near(o1.y, -0.25f + 0.035f, 1e-6f), "on each wrist's own Z (6 + 1.5 and 2 + 1.5 cm)");
        own.orbMirror = true;
        const V3 m1 = place(own, 3, 0);
        Expect(Near(m1.y, -0.25f + 0.075f, 1e-6f), "with the mirror tick the right takes the left's Z and the class's offset too (%.4f)", m1.y);
        // the sum is held in the depth's range (10 cm)
        Config highZ = OrbsOn();
        highZ.orbLeft.upCm = 9.0f;
        highZ.orbClassZCm[3] = 5.0f;
        Expect(Near(place(highZ, 3, 1).y, -0.25f + 0.10f, 1e-6f), "9 + 5 is held at 10 cm");
        Config deep = OrbsOn();
        deep.orbLeft.upCm = -9.0f;
        deep.orbClassZCm[3] = -5.0f;
        Expect(Near(place(deep, 3, 1).y, -0.25f - 0.10f, 1e-6f), "-9 - 5 at -10 cm");
        // the rotation and the fill direction are not touched (the frame is the same)
        In a = Base(), b = Base();
        b.cfg.orbClassZCm[3] = 3.0f;
        b.heroClass = 3;
        a.cfg.orbLeft.rotXDeg = b.cfg.orbLeft.rotXDeg = 40.0f;
        State sa, sb;
        const Set s1 = Step(sa, a), s2 = Step(sb, b);
        Expect(SameV(s1.orb[kLife].back, s2.orb[kLife].back) && SameV(s1.orb[kLife].along, s2.orb[kLife].along) && SameV(s1.orb[kLife].axis, s2.orb[kLife].axis), "the frame, the turn and the fill direction are the same");
        // without a drawn arm (the controller's point): the depth is above the grip, offset included
        In g = Base();
        g.arm[0].ok = g.arm[1].ok = false;
        g.grip[0].ok = g.grip[1].ok = true;
        g.grip[0].pos = {0.25f, -0.2f, -0.5f};
        g.grip[1].pos = {-0.25f, -0.2f, -0.5f};
        g.cfg.orbClassZCm[3] = 1.0f;
        g.heroClass = 3;
        State sg;
        const Set s3 = Step(sg, g);
        Expect(Near(s3.orb[kLife].c.y, -0.2f + 0.05f, 1e-6f), "the point stand-in: Z and the class's offset above the grip (%.4f)", s3.orb[kLife].c.y);
    }
    // the hero's class on its way to D2R VR Settings: bits 8 - 12 of the state block's twoHanded word
    {
        bool ok = true;
        for (int c = 0; c < 8; ++c) ok = ok && D2RVRHeroOf(D2RVRHeroBits(c)) == c && (D2RVRHeroBits(c) & D2RVR_HERO_KNOWN);
        Expect(ok, "the class goes in and comes out for 0 - 7");
        Expect(D2RVRHeroOf(0) == -1 && D2RVRHeroBits(-1) == 0 && D2RVRHeroOf(D2RVRHeroBits(-1)) == -1, "no class: not known");
        const uint32_t two = D2RVR_TWO_HANDED | D2RVR_TWO_HANDED_KNOWN | D2RVR_LEFT_WEAPON;
        const uint32_t word = two | D2RVRHeroBits(3);
        Expect((word & D2RVR_TWO_HANDED) && (word & D2RVR_TWO_HANDED_KNOWN) && (word & D2RVR_LEFT_WEAPON) && !(word & D2RVR_TWO_HANDS_ON) && D2RVRHeroOf(word) == 3,
               "the older bits (two-handed, known, a weapon in the left hand) stay as they were beside it");
        Expect((D2RVRHeroBits(7) & 0xFu) == 0, "and the class does not touch bits 0 - 3");
    }
}

// ---- the depth and the along offset per hero class, under a shield ------------------------------------------------------------------------------------

void TestShieldClassDepth() {
    printf("the depth and the along offset per hero class, under a shield\n");
    // the numbers: each class its own, 0 for a class not known or with none, held in -5 .. 5
    {
        Config c;
        for (int k = 0; k < 8; ++k) {
            c.orbShieldClassUpCm[k] = 0.5f * (float)(k + 1);
            c.orbShieldClassAlongCm[k] = -0.25f * (float)(k + 1);
        }
        Expect(handvitals::ClassShieldUpCm(c, 0) == 0.5f && handvitals::ClassShieldUpCm(c, 3) == 2.0f && handvitals::ClassShieldUpCm(c, 7) == 4.0f, "each class has its own depth under a shield");
        Expect(handvitals::ClassShieldAlongCm(c, 0) == -0.25f && handvitals::ClassShieldAlongCm(c, 3) == -1.0f && handvitals::ClassShieldAlongCm(c, 7) == -2.0f, "...and its own along offset");
        Expect(handvitals::ClassShieldUpCm(c, -1) == 0.0f && handvitals::ClassShieldUpCm(c, 8) == 0.0f && handvitals::ClassShieldAlongCm(c, -1) == 0.0f && handvitals::ClassShieldAlongCm(c, 99) == 0.0f,
               "a class not known: none");
        const Config none;
        bool zero = true;
        for (int k = -1; k < 9; ++k) zero = zero && handvitals::ClassShieldUpCm(none, k) == 0.0f && handvitals::ClassShieldAlongCm(none, k) == 0.0f;
        Expect(zero, "the default: no class has a value of its own, 0 for all");
        Config w;
        w.orbShieldClassUpCm[1] = 50.0f;
        w.orbShieldClassUpCm[2] = -50.0f;
        w.orbShieldClassUpCm[3] = std::nanf("");
        w.orbShieldClassAlongCm[1] = std::numeric_limits<float>::infinity();
        w.orbShieldClassAlongCm[2] = 7.0f;
        w.orbShieldClassAlongCm[4] = -9.0f;
        Expect(handvitals::ClassShieldUpCm(w, 1) == 5.0f && handvitals::ClassShieldUpCm(w, 2) == -5.0f && handvitals::ClassShieldUpCm(w, 3) == 0.0f && handvitals::ClassShieldAlongCm(w, 1) == 0.0f &&
                   handvitals::ClassShieldAlongCm(w, 2) == 5.0f && handvitals::ClassShieldAlongCm(w, 4) == -5.0f,
               "held in -5 .. 5, a number that is not one is 0");
        const Config s = handvitals::Sanitized(w);
        Expect(s.orbShieldClassUpCm[1] == 5.0f && s.orbShieldClassUpCm[2] == -5.0f && s.orbShieldClassUpCm[3] == 0.0f && s.orbShieldClassAlongCm[1] == 0.0f && s.orbShieldClassAlongCm[2] == 5.0f &&
                   s.orbShieldClassAlongCm[4] == -5.0f && s.orbShieldClassUpCm[0] == 0.0f,
               "Sanitized does the same");
    }
    // where the ball goes: the left wrist (life) wears the shield; the left forearm runs along -z, its back is +y, the inner side -y (a half turn about the arm)
    {
        auto life = [&](const Config& cfg, int hero, int shieldWrist, int wrist = kLife) {
            In in = Base();
            in.cfg = cfg;
            in.heroClass = hero;
            in.shieldWrist = shieldWrist;
            State st;
            return Step(st, in).orb[wrist].c;
        };
        Config cfg = OrbsOn();                // 3 cm along, 4 cm up
        cfg.orbShieldClassUpCm[3] = 1.5f;     // paladin
        cfg.orbShieldClassAlongCm[3] = 2.0f;
        cfg.orbShieldClassUpCm[1] = -1.0f;    // sorceress: depth only
        const V3 plain = life(OrbsOn(), -1, 1);   // under a shield, nothing of the class's: on the inner side, 4 cm out of the skin
        Expect(SameV(plain, {-0.20f, -0.25f - 0.04f, -0.45f - 0.03f}), "under a shield the ball is on the inner side, 4 cm out of the skin (%.3f %.3f %.3f)", plain.x, plain.y, plain.z);
        const V3 pal = life(cfg, 3, 1);
        Expect(SameV(pal, {plain.x, plain.y - 0.015f, plain.z - 0.02f}), "a paladin with a shield: 1.5 cm deeper out of the inner side and 2 cm further toward the knuckles (%.4f %.4f %.4f)", pal.x - plain.x, pal.y - plain.y, pal.z - plain.z);
        const V3 sor = life(cfg, 1, 1);
        Expect(SameV(sor, {plain.x, plain.y + 0.01f, plain.z}), "a sorceress: 1 cm into the arm (her own number), no along offset");
        Expect(SameV(life(cfg, 0, 1), plain) && SameV(life(cfg, -1, 1), plain) && SameV(life(cfg, 42, 1), plain), "an amazon with no value of her own, a hero not known, a class that is none: the shield's own place");
        // no shield: the class's shield numbers do nothing at all
        Expect(SameV(life(cfg, 3, -1), life(OrbsOn(), -1, -1)) && SameV(life(cfg, 1, -1), life(OrbsOn(), -1, -1)), "with no shield the ball is on the back of the hand, where it was (the class's numbers are for the shield pose only)");
        // the other wrist keeps its place while the left wears the shield
        Expect(SameV(life(cfg, 3, 1, kMana), life(OrbsOn(), -1, -1, kMana)), "the other wrist (mana) is where it always was");
        // the hand swap: the shield is on the right wrist - the same numbers there, on its own inner side
        const V3 rplain = life(OrbsOn(), -1, 0, kMana), rpal = life(cfg, 3, 0, kMana);
        Expect(SameV(rpal, {rplain.x, rplain.y - 0.015f, rplain.z - 0.02f}), "a shield on the right wrist: the same offsets, on its own inner side");
        // on top of the shield's own, and of the class's own wrist depth - all in the rolled frame
        Config sum = cfg;
        sum.orbShield.upCm = 1.5f;
        sum.orbShield.alongCm = -2.0f;
        sum.orbClassZCm[3] = 0.5f;
        const V3 base = life(sum, 0, 1);   // an amazon: the shield's own 1.5 up and -2 along, nothing of the paladin's
        const V3 withPaladin = life(sum, 3, 1);
        Expect(SameV(base, {plain.x, plain.y - 0.015f, plain.z + 0.02f}), "the shield's own numbers are as they were (1.5 cm deeper, 2 cm toward the elbow)");
        Expect(SameV(withPaladin, {plain.x, plain.y - 0.015f - 0.015f - 0.005f, plain.z + 0.02f - 0.02f}), "a paladin: the shield's 1.5 + the class's depth under it 1.5 + the class's wrist Z 0.5 cm, and along -2 + 2 (%.4f %.4f %.4f)",
               withPaladin.x - plain.x, withPaladin.y - plain.y, withPaladin.z - plain.z);
        // the roll is a ramp: half under the shield, half the offset (the same direction as the rolled frame's out of the skin)
        {
            const handvitals::OrbSpot s = SpotFor(cfg, 1);
            (void)s;
            const Place half = Centre(LeftArm(), Grip{}, cfg, 1, 0.0f, 0.0f, 0.5f, 1.5f, 2.0f), half0 = Centre(LeftArm(), Grip{}, cfg, 1, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f);
            const V3 d = hset::Sub(half.c, half0.c);
            Expect(Near(Len(d), 0.5f * std::sqrt(0.015f * 0.015f + 0.02f * 0.02f), 1e-6f) && Near(hset::Dot(d, half.back), 0.5f * 0.015f, 1e-6f) && Near(hset::Dot(d, half.axis), 0.5f * 0.02f, 1e-6f),
                   "half way into the roll: half the offsets, along the rolled back and the hand's axis");
            const Place none0 = Centre(LeftArm(), Grip{}, cfg, 1, 0.0f, 0.0f, 0.0f, 1.5f, 2.0f), none1 = Centre(LeftArm(), Grip{}, cfg, 1, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
            Expect(SameV(none0.c, none1.c, 1e-9f), "not under a shield at all (u 0): nothing of it");
        }
        // a slanted arm, mirrored hands: the offsets follow the rolled frame's own axes on both wrists
        {
            const Arm l = ArmAt({-0.2f, -0.25f, -0.15f}, {-0.2f, -0.25f, -0.45f}, hset::Unit(V3{0.0f, -1.0f, 0.3f})), r = ArmAt({0.2f, -0.25f, -0.15f}, {0.2f, -0.25f, -0.45f}, hset::Unit(V3{0.0f, -1.0f, 0.3f}));
            const Place a = Centre(l, Grip{}, cfg, 1, 0.0f, 0.0f, 1.0f, 1.5f, 2.0f), a0 = Centre(l, Grip{}, cfg, 1, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
            const Place b = Centre(r, Grip{}, cfg, 0, 0.0f, 0.0f, 1.0f, 1.5f, 2.0f), b0 = Centre(r, Grip{}, cfg, 0, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
            const V3 da = hset::Sub(a.c, a0.c), db = hset::Sub(b.c, b0.c);
            Expect(Near(hset::Dot(da, a.back), 0.015f, 1e-6f) && Near(hset::Dot(da, a.axis), 0.02f, 1e-6f) && Near(hset::Dot(db, b.back), 0.015f, 1e-6f) && Near(hset::Dot(db, b.axis), 0.02f, 1e-6f),
                   "on a slanted arm, on either wrist: 1.5 cm along the rolled back and 2 cm along the hand's axis");
            Expect(Near(da.x, -db.x, 1e-6f) && Near(da.y, db.y, 1e-6f) && Near(da.z, db.z, 1e-6f), "and the two wrists are mirror images");
        }
        // a wrist whose frame is not known (the controller's point alone) cannot roll: nothing of it
        {
            Grip g;
            g.ok = true;
            g.pos = {0.1f, -0.3f, -0.4f};
            const Place only = Centre(Arm{}, g, cfg, 1, 0.0f, 0.0f, 1.0f, 1.5f, 2.0f), plainOnly = Centre(Arm{}, g, cfg, 1, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
            Expect(only.ok && SameV(only.c, plainOnly.c, 1e-9f), "the controller's point alone: no roll, no class offsets");
        }
    }
    // the ramp in time: the class's offsets come in and go out with the roll, no jump
    {
        Config cfg = OrbsOn();
        cfg.orbShieldClassUpCm[3] = 2.0f;
        In in = Base();
        in.cfg = cfg;
        in.heroClass = 3;
        State st;
        in.shieldWrist = -1;
        Step(st, in);   // the first look: no shield
        in.shieldWrist = 1;
        float lastY = 1e9f, maxJump = 0.0f;
        for (int i = 0; i < 30; ++i) {
            in.time += 1.0 / 72.0;
            const float y = Step(st, in).orb[kLife].c.y;
            if (lastY < 1e8f) maxJump = std::max(maxJump, std::fabs(y - lastY));
            lastY = y;
        }
        Expect(Near(lastY, -0.25f - 0.04f - 0.02f, 1e-6f) && maxJump < 0.012f, "going under a shield: the ball reaches its place with the class's 2 cm in it, in small steps (last %.4f, biggest step %.4f)", lastY, maxJump);
    }
    // an ini with no values behaves exactly as before: every hero, with and without a shield
    {
        bool same = true;
        for (int hero = -1; hero < 8; ++hero)
            for (int shield : {-1, 0, 1}) {
                In a = Base(), b = Base();
                a.heroClass = b.heroClass = hero;
                a.shieldWrist = b.shieldWrist = shield;
                b.cfg.orbShieldClassUpCm[2] = 0.0f;   // (explicit zeros)
                State sa, sb;
                const Set x = Step(sa, a), y = Step(sb, b);
                same = same && SameV(x.orb[kLife].c, y.orb[kLife].c, 1e-9f) && SameV(x.orb[kMana].c, y.orb[kMana].c, 1e-9f);
            }
        Expect(same, "all zeros: every hero, with a shield and without, is where it was");
    }
}

// ---- the medallion's turn per hero class, under a shield -----------------------------------------------------------------------------------------------

void TestShieldClassRot() {
    printf("the medallion's turn per hero class, under a shield\n");
    // the numbers: each class its own three, 0 for a class not known, held in -180 .. 180
    {
        Config c;
        for (int k = 0; k < 8; ++k) {
            c.orbShieldClassRotXDeg[k] = 10.0f * (float)(k + 1);
            c.orbShieldClassRotYDeg[k] = -5.0f * (float)(k + 1);
            c.orbShieldClassRotZDeg[k] = 2.0f * (float)(k + 1);
        }
        const handvitals::OrbTurn p = handvitals::ClassShieldRotDeg(c, 3);
        Expect(p.xDeg == 40.0f && p.yDeg == -20.0f && p.zDeg == 8.0f, "each class has its own three angles under a shield");
        const handvitals::OrbTurn u = handvitals::ClassShieldRotDeg(c, -1), v = handvitals::ClassShieldRotDeg(c, 8), w = handvitals::ClassShieldRotDeg(c, 99);
        Expect(u.xDeg == 0.0f && u.yDeg == 0.0f && u.zDeg == 0.0f && v.xDeg == 0.0f && w.zDeg == 0.0f, "a class not known: none");
        const Config none;
        bool zero = true;
        for (int k = -1; k < 9; ++k) {
            const handvitals::OrbTurn t = handvitals::ClassShieldRotDeg(none, k);
            zero = zero && t.xDeg == 0.0f && t.yDeg == 0.0f && t.zDeg == 0.0f;
        }
        Expect(zero, "the default: no class has a value of its own, 0 for all");
        Config wild;
        wild.orbShieldClassRotXDeg[1] = 900.0f;
        wild.orbShieldClassRotXDeg[2] = -900.0f;
        wild.orbShieldClassRotYDeg[1] = std::nanf("");
        wild.orbShieldClassRotZDeg[1] = std::numeric_limits<float>::infinity();
        wild.orbShieldClassRotZDeg[2] = 181.0f;
        const handvitals::OrbTurn a = handvitals::ClassShieldRotDeg(wild, 1), b = handvitals::ClassShieldRotDeg(wild, 2);
        Expect(a.xDeg == 180.0f && b.xDeg == -180.0f && a.yDeg == 0.0f && a.zDeg == 0.0f && b.zDeg == 180.0f, "held in -180 .. 180, a number that is not one is 0");
        const Config s = handvitals::Sanitized(wild);
        Expect(s.orbShieldClassRotXDeg[1] == 180.0f && s.orbShieldClassRotXDeg[2] == -180.0f && s.orbShieldClassRotYDeg[1] == 0.0f && s.orbShieldClassRotZDeg[1] == 0.0f && s.orbShieldClassRotZDeg[2] == 180.0f &&
                   s.orbShieldClassRotXDeg[0] == 0.0f,
               "Sanitized does the same");
    }
    // the spot: the class's three angles ride with the shield's, scaled by the roll's ramp, wrapped
    {
        handvitals::OrbSpot own;
        own.rotXDeg = 170.0f;
        own.rotYDeg = -20.0f;
        handvitals::OrbUnderShield us;
        us.rotXDeg = 10.0f;
        us.rotZDeg = -30.0f;
        handvitals::OrbTurn cls;
        cls.xDeg = 40.0f;
        cls.yDeg = 15.0f;
        cls.zDeg = 50.0f;
        const handvitals::OrbSpot full = SpotUnder(own, us, 1.0f, cls), half = SpotUnder(own, us, 0.5f, cls), zero = SpotUnder(own, us, 0.0f, cls);
        Expect(Near(full.rotXDeg, std::remainder(170.0f + 10.0f + 40.0f, 360.0f), 1e-4f) && Near(full.rotYDeg, -20.0f + 15.0f, 1e-4f) && Near(full.rotZDeg, -30.0f + 50.0f, 1e-4f),
               "all the way under: the wrist's own + the shield's + the class's, wrapped (X %.1f, Y %.1f, Z %.1f)", full.rotXDeg, full.rotYDeg, full.rotZDeg);
        Expect(Near(half.rotXDeg, std::remainder(170.0f + 0.5f * 50.0f, 360.0f), 1e-4f) && Near(half.rotYDeg, -20.0f + 7.5f, 1e-4f) && Near(half.rotZDeg, 10.0f, 1e-4f), "half way: half of them");
        Expect(Near(zero.rotXDeg, 170.0f, 1e-6f) && Near(zero.rotYDeg, -20.0f, 1e-6f) && Near(zero.rotZDeg, 0.0f, 1e-6f), "not under a shield: the wrist's own, exactly");
        Expect(SpotUnder(own, us, 1.0f).rotXDeg == SpotUnder(own, us, 1.0f, handvitals::OrbTurn{}).rotXDeg, "no class numbers: as before the classes had any");
    }
    // where the medallion turns to: the left forearm runs along -z, the back is +y, the inner side -y (the roll under a shield), the thumb then points -x
    {
        auto at = [&](const Config& cfg, int hero, int shieldWrist, int wrist = kLife) {
            In in = Base();
            in.cfg = cfg;
            in.heroClass = hero;
            in.shieldWrist = shieldWrist;
            State st;
            return Step(st, in).orb[wrist];
        };
        Config cfg = OrbsOn();
        cfg.orbShieldClassRotXDeg[3] = 30.0f;   // paladin: 30 degrees about the arm
        const Orb plain = at(OrbsOn(), -1, 1);
        Expect(SameV(plain.back, {0, -1, 0}) && SameV(plain.along, {0, 0, -1}), "under a shield with no turn: the back is the inner side (down), along the arm");
        const Orb pal = at(cfg, 3, 1);
        Expect(SameV(pal.back, {-0.5f, -0.866025f, 0.0f}, 1e-4f) && SameV(pal.along, {0, 0, -1}, 1e-5f), "a paladin's X 30: the medallion's face turned 30 degrees about the arm toward the thumb (%.3f %.3f %.3f)", pal.back.x, pal.back.y, pal.back.z);
        Expect(SameV(pal.c, plain.c, 1e-6f), "and only the turn - the medallion's middle is where it was");
        // it is the same turn the shield's own slider makes (they add)
        Config shared = OrbsOn();
        shared.orbShield.rotXDeg = 30.0f;
        const Orb sh = at(shared, 3, 1);
        Expect(SameV(sh.back, pal.back, 1e-6f) && SameV(sh.along, pal.along, 1e-6f) && SameV(sh.axis, pal.axis, 1e-6f), "the same as the shield's own X 30 (the class's number is added to it)");
        Config both = OrbsOn();
        both.orbShield.rotXDeg = 20.0f;
        both.orbShieldClassRotXDeg[3] = 30.0f;
        Config five = OrbsOn();
        five.orbShield.rotXDeg = 50.0f;
        Expect(SameV(at(both, 3, 1).back, at(five, 3, 1).back, 1e-5f) && !SameV(at(both, 0, 1).back, at(five, 0, 1).back, 1e-3f), "20 shared + 30 of the class is 50 for that class - and 20 for another");
        // Y and Z as the same sum of the two, about the wrist's own axes: Y about the across axis (the thumb), Z about the way out of the skin
        Config y = OrbsOn();
        y.orbShieldClassRotYDeg[3] = 40.0f;
        Config ys = OrbsOn();
        ys.orbShield.rotYDeg = 40.0f;
        Expect(SameV(at(y, 3, 1).back, at(ys, 3, 1).back, 1e-6f) && SameV(at(y, 3, 1).along, at(ys, 3, 1).along, 1e-6f), "Y: the same as the shield's own");
        Expect(SameV(at(y, 3, 1).back, {0, -0.766044f, 0.642788f}, 1e-4f), "Y 40: the face leans 40 degrees about the across axis (%.3f %.3f %.3f)", at(y, 3, 1).back.x, at(y, 3, 1).back.y, at(y, 3, 1).back.z);
        Config z = OrbsOn();
        z.orbShieldClassRotZDeg[3] = 50.0f;
        Config zs = OrbsOn();
        zs.orbShield.rotZDeg = 50.0f;
        Expect(SameV(at(z, 3, 1).back, {0, -1, 0}, 1e-5f) && SameV(at(z, 3, 1).along, at(zs, 3, 1).along, 1e-6f) && SameV(at(z, 3, 1).along, {0.766044f, 0.0f, -0.642788f}, 1e-4f),
               "Z 50: the face stays, the picture's up turns 50 degrees about it (the same as the shield's own)");
        // for that class only, and for the shield pose only
        Expect(SameV(at(cfg, 0, 1).back, plain.back) && SameV(at(cfg, -1, 1).back, plain.back) && SameV(at(cfg, 42, 1).back, plain.back), "an amazon with no value of her own, a hero not known, a class that is none: the shield's own turn");
        const Orb back = at(OrbsOn(), -1, -1);
        Expect(SameV(at(cfg, 3, -1).back, back.back) && SameV(at(cfg, 3, -1).along, back.along), "with no shield the medallion is on the back of the hand, not turned (the class's numbers are for the shield pose only)");
        Expect(SameV(at(cfg, 3, 1, kMana).back, at(OrbsOn(), -1, -1, kMana).back) && SameV(at(cfg, 3, 1, kMana).along, at(OrbsOn(), -1, -1, kMana).along), "the other wrist (mana) is where it always was");
        // the hand swap: a shield on the right wrist - the same numbers, the mirror image
        const Orb left = at(cfg, 3, 1), right = at(cfg, 3, 0, kMana);
        Expect(SameV(right.back, Mirrored(left.back), 1e-5f) && SameV(right.along, Mirrored(left.along), 1e-5f) && SameV(right.axis, Mirrored(left.axis), 1e-5f), "a shield on the right wrist: the same numbers turn it the mirror image of the left's");
        Config zy = OrbsOn();
        zy.orbShieldClassRotXDeg[5] = 25.0f;
        zy.orbShieldClassRotYDeg[5] = -35.0f;
        zy.orbShieldClassRotZDeg[5] = 70.0f;
        const Orb l2 = at(zy, 5, 1), r2 = at(zy, 5, 0, kMana);
        Expect(SameV(r2.back, Mirrored(l2.back), 1e-5f) && SameV(r2.along, Mirrored(l2.along), 1e-5f) && SameV(r2.c, Mirrored(l2.c), 1e-5f), "all three angles together: the same mirror image");
    }
    // the ramp in time: the class's turn comes in and goes out with the roll, in small steps
    {
        Config cfg = OrbsOn();
        cfg.orbShieldClassRotZDeg[3] = 90.0f;
        In in = Base();
        in.cfg = cfg;
        in.heroClass = 3;
        State st;
        in.shieldWrist = -1;
        Step(st, in);   // the first look: no shield
        in.shieldWrist = 1;
        const Orb target = [&] {
            In t = in;
            State s2;
            return Step(s2, t).orb[kLife];
        }();
        Orb last;
        float maxAngle = 0.0f, maxStep = 0.0f;
        V3 prev = Step(st, in).orb[kLife].along;
        bool first = true;
        for (int i = 0; i < 32; ++i) {
            in.time += 1.0 / 72.0;
            last = Step(st, in).orb[kLife];
            const float ang = std::acos(std::clamp(hset::Dot(last.along, prev), -1.0f, 1.0f)) * 57.29578f;
            if (!first) maxStep = std::max(maxStep, ang);
            first = false;
            maxAngle = std::max(maxAngle, std::acos(std::clamp(hset::Dot(last.along, V3{0, 0, -1}), -1.0f, 1.0f)) * 57.29578f);
            prev = last.along;
        }
        Expect(SameV(last.back, target.back, 1e-4f) && SameV(last.along, target.along, 1e-4f), "going under a shield: the medallion reaches its place with the class's turn in it");
        Expect(maxStep < 12.0f, "in small steps (the biggest turn in one frame: %.1f degrees)", maxStep);
    }
    // an ini with no values behaves exactly as before: every hero, with a shield and without
    {
        bool same = true;
        for (int hero = -1; hero < 8; ++hero)
            for (int shield : {-1, 0, 1}) {
                In a = Base(), b = Base();
                a.heroClass = b.heroClass = hero;
                a.shieldWrist = b.shieldWrist = shield;
                b.cfg.orbShieldClassRotXDeg[2] = 0.0f;   // (explicit zeros)
                b.cfg.orbShieldClassRotYDeg[5] = 0.0f;
                b.cfg.orbShieldClassRotZDeg[7] = 0.0f;
                State sa, sb;
                const Set x = Step(sa, a), y = Step(sb, b);
                for (int k = 0; k < 2; ++k)
                    same = same && SameV(x.orb[k].c, y.orb[k].c, 1e-9f) && SameV(x.orb[k].back, y.orb[k].back, 1e-9f) && SameV(x.orb[k].along, y.orb[k].along, 1e-9f) && SameV(x.orb[k].axis, y.orb[k].axis, 1e-9f);
            }
        Expect(same, "all zeros: every hero, with a shield and without, is turned as it was");
    }
}

void TestFade() {
    printf("fade\n");
    Expect(DistanceFade(0.0f) == 1.0f && DistanceFade(0.5f) == 1.0f && DistanceFade(0.85f) == 1.0f, "full inside arm's length");
    Expect(DistanceFade(1.25f) == 0.0f && DistanceFade(3.0f) == 0.0f, "gone from 1.25 m");
    float prev = 1.0f;
    bool mono = true;
    for (float d = 0.85f; d <= 1.25f; d += 0.01f) {
        const float f = DistanceFade(d);
        mono = mono && f <= prev + 1e-6f && f >= 0.0f && f <= 1.0f;
        prev = f;
    }
    Expect(mono, "falls smoothly between");
    Expect(Near(DistanceFade(1.05f), 0.5f, 0.01f), "half way at the middle");
    Expect(DistanceFade(std::nanf("")) == 0.0f && DistanceFade(-1.0f) == 0.0f, "NaN and negative: not drawn");
}

// ---- the levels, the heartbeat, what is drawn ------------------------------------------------------------------------------------------------------

Set Run(State& st, In in, double seconds, double dt = 1.0 / 90.0) {
    Set s;
    for (double t = 0.0; t < seconds - 1e-9; t += dt) {
        in.time += dt;
        s = Step(st, in);
    }
    return s;
}

void TestState() {
    printf("state\n");
    // the first sight: as it is, no easing from nothing
    {
        State st;
        In in = Base();
        in.v = handvitals::FromStats(250 * 256, 1000 * 256, 100 * 256, 400 * 256);
        const Set s = Step(st, in);
        Expect(s.any && s.orb[kLife].on && s.orb[kMana].on, "both on");
        Expect(Near(s.orb[kLife].fill, 0.25f, 1e-6f) && Near(s.orb[kMana].fill, 0.25f, 1e-6f), "the first look shows the levels as they are (%.3f %.3f)", s.orb[kLife].fill, s.orb[kMana].fill);
        Expect(s.orb[kLife].side == 1 && s.orb[kMana].side == 0 && s.orb[kLife].kind == kLife && s.orb[kMana].kind == kMana, "life left, mana right");
        Expect(Near(s.orb[kLife].radius, 0.025f, 1e-6f), "5 cm across: radius 2.5 cm");
        Expect(Near(s.orb[kLife].opacity, 0.95f, 1e-6f), "opacity from the setting");
        Expect(s.orb[kLife].c.x < 0.0f && s.orb[kMana].c.x > 0.0f, "life's ball is on the left of the head, mana's on the right (%.2f, %.2f)", s.orb[kLife].c.x, s.orb[kMana].c.x);
    }
    // easing
    {
        State st;
        In in = Base();
        in.v = handvitals::FromStats(1000 * 256, 1000 * 256, 400 * 256, 400 * 256);
        Step(st, in);
        in.v = handvitals::FromStats(200 * 256, 1000 * 256, 400 * 256, 400 * 256);
        float prev = 1.0f;
        bool mono = true, noOvershoot = true;
        double t = 0.0;
        float half = -1.0f;
        for (int i = 0; i < 400; ++i) {
            in.time += 1.0 / 90.0;
            t += 1.0 / 90.0;
            const float f = Step(st, in).orb[kLife].fill;
            mono = mono && f <= prev + 1e-6f;
            noOvershoot = noOvershoot && f >= 0.2f - 1e-6f;
            if (half < 0.0f && f <= 0.6f) half = (float)t;
            prev = f;
        }
        Expect(mono && noOvershoot, "life falling 100% -> 20%: monotone, no undershoot");
        Expect(prev == 0.2f, "and arrives at 20%% exactly (%.4f)", prev);
        Expect(half > 0.1f && half < 0.4f, "half way in about a quarter second (%.3f s)", half);
        Expect(Near(Step(st, in).orb[kMana].fill, 1.0f, 1e-6f), "mana did not move");
        // rising
        in.v = handvitals::FromStats(900 * 256, 1000 * 256, 400 * 256, 400 * 256);
        const Set s = Run(st, in, 0.1);
        Expect(s.orb[kLife].fill > 0.2f && s.orb[kLife].fill < 0.9f, "rising: on its way after 0.1 s (%.3f)", s.orb[kLife].fill);
        // a long frame counts as 0.1 s
        State st2;
        In b = Base();
        b.v = handvitals::FromStats(1000 * 256, 1000 * 256, 0, 400 * 256);
        Step(st2, b);
        b.v = handvitals::FromStats(0, 1000 * 256, 0, 400 * 256);
        b.time += 30.0;
        const float f1 = Step(st2, b).orb[kLife].fill;
        Expect(f1 > 0.5f && f1 < 1.0f, "a 30 s frame moves it 0.1 s worth (%.3f)", f1);
        // NaN / backwards time
        b.time = std::nan("");
        const float f2 = Step(st2, b).orb[kLife].fill;
        Expect(Near(f2, f1, 1e-6f), "a NaN time does not move it");
        b.time = 5.0;
        const float f3 = Step(st2, b).orb[kLife].fill;
        Expect(Near(f3, f1, 1e-6f), "time going back does not move it");
    }
    // the off states
    {
        State st;
        In in = Base();
        in.cfg.on = false;
        Set s = Step(st, in);
        Expect(!s.any && !s.orb[0].on && !s.orb[1].on, "the setting off: nothing");
        in = Base();
        in.cfg.style = handvitals::kStylePanel;
        s = Step(st, in);
        Expect(!s.any, "the palm panel chosen: no orbs");
        in = Base();
        in.vitalsOk = false;
        s = Step(st, in);
        Expect(!s.any, "numbers not fresh: nothing");
        in = Base();
        in.v = Vitals{};
        s = Step(st, in);
        Expect(!s.any, "numbers unknown: nothing");
        in = Base();
        in.arm[0].ok = in.arm[1].ok = false;
        s = Step(st, in);
        Expect(!s.any, "no arms and no grip: nothing");
        in = Base();
        in.arm[1].ok = false;
        s = Step(st, in);
        Expect(s.any && !s.orb[kLife].on && s.orb[kMana].on, "only the right arm posed: only mana's ball");
        in.grip[1].ok = true;
        in.grip[1].pos = {-0.2f, -0.2f, -0.5f};
        s = Step(st, in);
        Expect(s.orb[kLife].on && Near(s.orb[kLife].c.x, -0.2f, 1e-5f), "the left grip stands in for the arm that is not posed");
        // the setting comes back on: as if first seen
        in = Base();
        in.cfg.on = false;
        Step(st, in);
        in = Base();
        in.v = handvitals::FromStats(100 * 256, 1000 * 256, 400 * 256, 400 * 256);
        s = Step(st, in);
        Expect(Near(s.orb[kLife].fill, 0.1f, 1e-6f), "ticked again: no easing from the old level");
        // a gap in the reads, then numbers again: shown as they are
        in.vitalsOk = false;
        Step(st, in);
        in.vitalsOk = true;
        in.v = handvitals::FromStats(900 * 256, 1000 * 256, 400 * 256, 400 * 256);
        in.time += 0.02;
        s = Step(st, in);
        Expect(Near(s.orb[kLife].fill, 0.9f, 1e-6f), "after a gap in the reads the new level is shown at once");
    }
    // the distance
    {
        State st;
        In in = Base();
        in.arm[1] = ArmAt({-0.2f, -0.2f, -0.8f}, {-0.2f, -0.2f, -1.1f}, {0, -1, 0});   // held far out: ~1.1 m
        Set s = Step(st, in);
        Expect(s.orb[kLife].on && s.orb[kLife].opacity < 0.95f * 0.6f && s.orb[kMana].opacity > 0.9f, "a hand held far out fades its ball (%.2f), the near one does not (%.2f)", s.orb[kLife].opacity, s.orb[kMana].opacity);
        in.arm[1] = ArmAt({-0.2f, -0.2f, -1.2f}, {-0.2f, -0.2f, -1.5f}, {0, -1, 0});
        s = Step(st, in);
        Expect(!s.orb[kLife].on && s.orb[kLife].opacity == 0.0f, "past 1.25 m: not drawn");
    }
    // both on one wrist
    {
        State st;
        In in = Base();
        in.cfg.orbHands = 2;
        const Set s = Step(st, in);
        const float apart = Len(hset::Sub(s.orb[kLife].c, s.orb[kMana].c));
        Expect(s.orb[kLife].on && s.orb[kMana].on && s.orb[kLife].side == 1 && s.orb[kMana].side == 1, "both on the left wrist");
        Expect(apart >= 2.0f * s.orb[kLife].radius - 1e-6f && apart < 2.0f * s.orb[kLife].radius + 0.01f, "side by side, not overlapping (%.1f cm apart for 2 x %.1f cm)", apart * 100.0f, s.orb[kLife].radius * 100.0f);
        // the wrist's midpoint between them
        const V3 mid = hset::Mul(hset::Add(s.orb[kLife].c, s.orb[kMana].c), 0.5f);
        const Place centre = Centre(LeftArm(), Grip{}, in.cfg, 1);
        Expect(Len(hset::Sub(mid, centre.c)) < 1e-5f, "centred on the hand");
    }
    // the settings' clamps reach here (Sanitized)
    {
        State st;
        In in = Base();
        in.cfg.orbSizeCm = 100.0f;
        in.cfg.orbOpacity = 0.0f;
        const Set s = Step(st, in);
        Expect(Near(s.orb[kLife].radius, 0.045f, 1e-6f) && Near(s.orb[kLife].opacity, 0.3f, 1e-6f), "9 cm at most; 0.3 opacity at least");
    }
}

void TestBeat() {
    printf("heartbeat\n");
    // pulse off: never, whatever the life
    {
        State st;
        In in = Base();
        in.v = handvitals::FromStats(50 * 256, 1000 * 256, 400 * 256, 400 * 256);
        const Set s = Run(st, in, 3.0);
        Expect(s.orb[kLife].glow == 0.0f && Near(s.orb[kLife].radius, 0.025f, 1e-7f), "pulse off: no glow, no throb at 5%% life");
    }
    // on, under the threshold: the glow beats, bounded, periodic at 50 - 72 a minute
    {
        State st;
        In in = Base();
        in.cfg.pulse = true;
        in.cfg.lowFrac = 0.30f;
        in.v = handvitals::FromStats(50 * 256, 1000 * 256, 400 * 256, 400 * 256);   // 5%: severe
        Run(st, in, 1.5);   // fully in
        float lo = 2.0f, hi = -1.0f;
        std::vector<double> peaks;
        float prev = 0.0f, prev2 = 0.0f;
        double t = 0.0;
        for (int i = 0; i < 900; ++i) {
            in.time += 1.0 / 90.0;
            t += 1.0 / 90.0;
            const Set s = Step(st, in);
            const float g = s.orb[kLife].glow;
            lo = std::min(lo, g);
            hi = std::max(hi, g);
            if (prev > prev2 && prev >= g && prev > 0.8f) peaks.push_back(t);
            prev2 = prev;
            prev = g;
            Expect(s.orb[kMana].glow == 0.0f, "mana never beats");
            if (i > 0 && !(g >= 0.0f && g <= 1.0f)) Expect(false, "glow in range");
            Expect(s.orb[kLife].radius >= 0.025f - 1e-7f && s.orb[kLife].radius <= 0.025f * (1.0f + kThrob) + 1e-7f, "throb stays within 5%%");
        }
        Expect(lo < 0.02f && hi > 0.98f, "the glow goes from rest to the top of a beat (%.2f .. %.2f)", lo, hi);
        Expect(peaks.size() >= 7 && peaks.size() <= 13, "about one big beat a second in 10 s (%d of them)", (int)peaks.size());
        if (peaks.size() >= 3) {
            const double period = (peaks.back() - peaks.front()) / (double)(peaks.size() - 1);
            const double bpm = 60.0 / period;
            Expect(bpm > 55.0 && bpm < 75.0, "the rate is the gentle beat's near no life: ~72 a minute (%.1f)", bpm);
        }
        // a moderate life beats slower: 50 at the threshold
        State st2;
        In m = Base();
        m.cfg.pulse = true;
        m.v = handvitals::FromStats(299 * 256, 1000 * 256, 400 * 256, 400 * 256);   // just under 30%
        Run(st2, m, 1.5);
        std::vector<double> pk;
        float p1 = 0.0f, p0 = 0.0f;
        t = 0.0;
        for (int i = 0; i < 1800; ++i) {
            m.time += 1.0 / 90.0;
            t += 1.0 / 90.0;
            const float g = Step(st2, m).orb[kLife].glow;
            if (p1 > p0 && p1 >= g && p1 > 0.8f) pk.push_back(t);
            p0 = p1;
            p1 = g;
        }
        if (pk.size() >= 3) {
            const double bpm = 60.0 / ((pk.back() - pk.front()) / (double)(pk.size() - 1));
            Expect(bpm > 45.0 && bpm < 56.0, "just under the threshold: ~50 a minute (%.1f)", bpm);
        } else {
            Expect(false, "peaks found at the threshold: %d", (int)pk.size());
        }
        // fade in and out
        State st3;
        In f = Base();
        f.cfg.pulse = true;
        f.v = handvitals::FromStats(100 * 256, 1000 * 256, 400 * 256, 400 * 256);
        float most = 0.0f;
        for (int i = 0; i < 20; ++i) { f.time += 1.0 / 90.0; most = std::max(most, Step(st3, f).orb[kLife].glow); }
        Expect(most < 0.4f, "0.2 s in the beat is still faded in (%.2f)", most);
        // healed: it fades out within a second or so and stays 0
        f.v = handvitals::FromStats(800 * 256, 1000 * 256, 400 * 256, 400 * 256);
        Run(st3, f, 1.2);
        float after = 0.0f;
        for (int i = 0; i < 180; ++i) { f.time += 1.0 / 90.0; after = std::max(after, Step(st3, f).orb[kLife].glow); }
        Expect(after == 0.0f, "healed above the threshold: no beat after a moment (%.3f)", after);
        // 30% exactly is not under
        State st4;
        In e = Base();
        e.cfg.pulse = true;
        e.v = handvitals::FromStats(300 * 256, 1000 * 256, 400 * 256, 400 * 256);
        const Set s4 = Run(st4, e, 2.0);
        Expect(s4.orb[kLife].glow == 0.0f, "exactly at the threshold: not under it");
        // dead: nothing beats
        State st5;
        In d = Base();
        d.cfg.pulse = true;
        d.v = handvitals::FromStats(0, 1000 * 256, 400 * 256, 400 * 256);
        const Set s5 = Run(st5, d, 2.0);
        Expect(s5.orb[kLife].glow == 0.0f && s5.orb[kLife].fill == 0.0f && s5.orb[kLife].on, "dead: an empty ball, no beat");
    }
}

// ---- the uniforms -------------------------------------------------------------------------------------------------------------------------------

void TestPack() {
    printf("uniforms\n");
    State st;
    In in = Base();
    in.v = handvitals::FromStats(600 * 256, 1000 * 256, 100 * 256, 400 * 256);
    const Set s = Step(st, in);
    // the head's turn-only axes: looking along +x (yawed a quarter turn): right = +z... any frame must be taken through
    const float camR[3] = {1.0f, 0.0f, 0.0f}, camF[3] = {0.0f, 0.0f, -1.0f};   // the identity: forward is -z
    Uniforms u = Pack(s, camR, camF, 4.0f, 12.5);
    Expect(u.on, "on");
    // the identity frame: world = hand frame * upm
    Expect(Near(u.pos[kLife][0], s.orb[kLife].c.x * 4.0f, 1e-5f) && Near(u.pos[kLife][1], s.orb[kLife].c.y * 4.0f, 1e-5f) && Near(u.pos[kLife][2], s.orb[kLife].c.z * 4.0f, 1e-5f),
           "an upright head: the hand frame scaled by the units per metre");
    Expect(Near(u.pos[kLife][3], 0.025f * 4.0f, 1e-6f) && Near(u.pos[kMana][3], 0.025f * 4.0f, 1e-6f), "the radius in world units");
    Expect(Near(u.look[kLife][0], 0.6f, 1e-6f) && Near(u.look[kMana][0], 0.25f, 1e-6f), "the fill levels");
    Expect(u.look[kLife][3] == 0.0f && u.look[kMana][3] == 1.0f, "kinds: 0 life, 1 mana");
    Expect(Near(u.look[kLife][1], 0.95f, 1e-6f), "opacity");
    Expect(Near(u.time, 12.5f, 1e-5f), "the time");
    // a head turned a quarter to the right: camR = (0,0,-1)?? (right = the world's -z when looking along +x): the hand frame's x goes there
    const float camR2[3] = {0.0f, 0.0f, 1.0f}, camF2[3] = {1.0f, 0.0f, 0.0f};   // looking along +x, right is +z
    u = Pack(s, camR2, camF2, 1.0f, 0.0);
    const V3 c = s.orb[kLife].c;
    Expect(Near(u.pos[kLife][0], -c.z, 1e-5f) && Near(u.pos[kLife][1], c.y, 1e-5f) && Near(u.pos[kLife][2], c.x, 1e-5f), "a yawed head: forward (-z of the hand frame) goes along camF, x along camR (%.3f %.3f %.3f)", u.pos[kLife][0], u.pos[kLife][1], u.pos[kLife][2]);
    // the time wraps
    u = Pack(s, camR, camF, 1.0f, 7200.0 + 3.25);
    Expect(Near(u.time, 3.25f, 1e-3f), "time wraps at two hours (%.3f)", u.time);
    u = Pack(s, camR, camF, 1.0f, -9.0);
    Expect(u.time >= 0.0f && u.time < 7200.0f, "a negative time stays in range");
    u = Pack(s, camR, camF, 1.0f, std::nan(""));
    Expect(u.time == 0.0f, "NaN time: 0");
    // off balls are at opacity 0 (the effect draws nothing of them)
    In one = Base();
    one.arm[1].ok = false;
    State st2;
    const Set s1 = Step(st2, one);
    u = Pack(s1, camR, camF, 1.0f, 1.0);
    Expect(u.on && u.look[kLife][1] == 0.0f && u.look[kMana][1] > 0.9f, "an unposed arm's ball has opacity 0");
    // nothing: not on
    State st3;
    In off = Base();
    off.cfg.on = false;
    u = Pack(Step(st3, off), camR, camF, 1.0f, 1.0);
    Expect(!u.on && u.look[0][1] == 0.0f && u.look[1][1] == 0.0f, "off: not on, opacity 0 everywhere");
    // a bad scale: not on
    u = Pack(s, camR, camF, 0.0f, 1.0);
    Expect(!u.on, "units per metre 0: not on");
    u = Pack(s, camR, camF, std::nanf(""), 1.0);
    Expect(!u.on, "units per metre NaN: not on");
    // the game's own globes: the same balls, the picture look
    {
        State sg;
        In g = Base();
        g.cfg.style = handvitals::kStyleGlobes;
        const Set s2 = Step(sg, g);
        Expect(s2.any && s2.globes && s2.orb[kLife].on && s2.orb[kMana].on, "the game's own globes: both balls, the picture look");
        Expect(s2.orb[kLife].side == 1 && s2.orb[kMana].side == 0 && Near(s2.orb[kLife].radius, 0.025f, 1e-6f), "left and right, the same place and size as the glass balls");
        u = Pack(s2, camR, camF, 1.0f, 1.0, 0.94f);
        Expect(u.on && u.picture && Near(u.fit, 0.94f, 1e-6f), "the uniforms say picture, with the disc's fit");
        u = Pack(s2, camR, camF, 1.0f, 1.0, 7.0f);
        Expect(Near(u.fit, 1.0f, 1e-6f), "a fit past 1 is 1");
        u = Pack(s2, camR, camF, 1.0f, 1.0, std::nanf(""));
        Expect(u.fit == 0.0f, "a NaN fit is 0");
        u = Pack(s, camR, camF, 1.0f, 1.0, 0.94f);
        Expect(u.on && !u.picture, "the glass balls: no picture");
        State sp;
        In pn = Base();
        pn.cfg.style = handvitals::kStylePanel;
        Expect(!Step(sp, pn).any, "the palm panel look: no balls");
        In bad = Base();
        bad.cfg.style = 9;
        State sb;
        const Set s3 = Step(sb, bad);
        Expect(s3.any && !s3.globes, "a style that is not one: the glass balls (Sanitized)");
        // the setting's clamps know the third look
        Config c;
        c.style = handvitals::kStyleGlobes;
        Expect(handvitals::Sanitized(c).style == handvitals::kStyleGlobes, "style 2 survives Sanitized");
        c.style = -1;
        Expect(handvitals::Sanitized(c).style == handvitals::kStyleOrbs, "style -1 is the glass balls");
    }
}

// ---- the effect's own HLSL, run on a software D3D11 device -----------------------------------------------------------------------------------------

struct Img {
    int w = 0, h = 0;
    std::vector<float> px;   // r g b a per pixel
    const float* At(int x, int y) const { return &px[((size_t)std::clamp(y, 0, h - 1) * w + std::clamp(x, 0, w - 1)) * 4]; }
};

uint32_t Crc(const uint8_t* d, size_t n, uint32_t crc = 0xFFFFFFFFu) {
    static uint32_t table[256];
    static bool made = false;
    if (!made) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        made = true;
    }
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ d[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}
void Be32(std::vector<uint8_t>& v, uint32_t x) { for (int s = 24; s >= 0; s -= 8) v.push_back((uint8_t)(x >> s)); }
void Chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    Be32(out, (uint32_t)data.size());
    std::vector<uint8_t> td(type, type + 4);
    td.insert(td.end(), data.begin(), data.end());
    out.insert(out.end(), td.begin(), td.end());
    Be32(out, ~Crc(td.data(), td.size()));
}
bool WritePng(const std::string& path, int w, int h, const std::vector<uint8_t>& rgb) {
    std::vector<uint8_t> raw;
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), &rgb[(size_t)y * w * 3], &rgb[(size_t)y * w * 3] + (size_t)w * 3);
    }
    std::vector<uint8_t> z = {0x78, 0x01};   // zlib, stored blocks: simple, and every viewer reads it
    for (size_t at = 0; at < raw.size(); at += 65535) {
        const size_t len = std::min<size_t>(65535, raw.size() - at);
        z.push_back(at + len >= raw.size() ? 1 : 0);
        z.push_back((uint8_t)(len & 0xFF));
        z.push_back((uint8_t)(len >> 8));
        z.push_back((uint8_t)(~len & 0xFF));
        z.push_back((uint8_t)((~len >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + at, raw.begin() + at + len);
    }
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    Be32(z, (b << 16) | a);
    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A}, ihdr;
    Be32(ihdr, (uint32_t)w);
    Be32(ihdr, (uint32_t)h);
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
    Chunk(png, "IHDR", ihdr);
    Chunk(png, "IDAT", z);
    Chunk(png, "IEND", {});
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "wb") || !f) return false;
    fwrite(png.data(), 1, png.size(), f);
    fclose(f);
    return true;
}

// What the test shader takes: the effect's own uniforms (OrbPos, OrbLook, OrbTime) and a scene of its own.
struct Cb {
    float orbPos[2][4] = {};
    float orbLook[2][4] = {};
    float orbAxis[2][4] = {{0, 1, 0, 0}, {0, 1, 0, 0}};   // OrbAxis: the way the liquid fills (the world's up by default)
    float orbBack[2][4] = {{0, 1, 0, 0}, {0, 1, 0, 0}};   // OrbBack: out of the back of the hand (up by default)
    float orbAlong[2][4] = {{0, 0, -1, 0}, {0, 0, -1, 0}};  // OrbAlong: along the hand toward the knuckles (forward by default)
    float orbMed[4] = {0, 0.55f, 0, 0};                 // OrbMed: x 1 the medallion (0 the full ball), y patina, z metal, w 1 the globe's disc fixed on the wrist
    float eyeT[4] = {0, 0, 0, 0};      // xyz the eye, w OrbTime
    float camP[4] = {0.3f, 0.2f, 0, 0};  // tan of the half-fov x and y, roll (rad), w 1 = leave the orbs out
    float capA[4][4] = {};             // up to four capsules as the game's depth: from (w radius) ...
    float capB[4][4] = {};             // ... to
    float wall[4] = {0, 0, 0, 0};      // x: a wall's view depth (0 none), y: from this pixel column on, z, w: the picture's size
    float scene[4] = {4, 0, 0, 0};     // x: the number of capsules
    float camQ[4] = {0, 0, 0, 0};      // x: the camera's pitch (rad, negative looks down)
    float picP[4] = {0, 0.94f, 0, 0};  // OrbPic: x 1 = the game's own globes (the texture), y the disc's radius over half its square
};

const char* kPrelude = R"(
cbuffer C : register(b0)
{
    float4 OrbPos[2];
    float4 OrbLook[2];
    float4 OrbAxis[2];
    float4 OrbBack[2];
    float4 OrbAlong[2];
    float4 OrbMed;
    float4 EyeT;
    float4 CamP;
    float4 CapA[4];
    float4 CapB[4];
    float4 WallP;
    float4 SceneP;
    float4 CamQ;
    float4 PicP;
};
Texture2D<float4> GlobeTex : register(t0);
SamplerState GlobeSS : register(s0);
#define OrbTime (EyeT.w)
#define OrbPic (PicP.xy)
#define GlobeSmp 0
// the armed skill's icon in the mana medallion (0.173): off here, as vrcam sends it while nothing is armed - the medallion as it was
static const float4 OrbIcon = float4(0.0, 0.0, 0.70, 0.10);
static const float OrbIconLift = 0.0;
static const float OrbIconLiftM = 0.0;
#define OrbIconSmp 0
#define tex2Dlod(s, c) GlobeTex.SampleLevel(GlobeSS, (c).xy, (c).w)
float CapsuleHit(float3 ro, float3 rd, float3 pa, float3 pb, float r)
{
    const float3 ba = pb - pa, oa = ro - pa;
    const float baba = dot(ba, ba), bard = dot(ba, rd), baoa = dot(ba, oa), rdoa = dot(rd, oa), oaoa = dot(oa, oa);
    const float a = baba - bard * bard, b = baba * rdoa - baoa * bard, c = baba * oaoa - baoa * baoa - r * r * baba;
    float h = b * b - a * c;
    if (h >= 0.0 && abs(a) > 1e-8)
    {
        const float t = (-b - sqrt(h)) / a;
        const float y = baoa + t * bard;
        if (y > 0.0 && y < baba) return t;
        const float3 oc = y <= 0.0 ? oa : ro - pb;
        const float b2 = dot(rd, oc), c2 = dot(oc, oc) - r * r;
        h = b2 * b2 - c2;
        if (h > 0.0) return -b2 - sqrt(h);
    }
    return -1.0;
}
)";

const char* kMain = R"(
float4 VS(uint id : SV_VertexID) : SV_Position
{
    const float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}
float4 PS(float4 pos : SV_Position) : SV_Target
{
    float2 ndc = float2(pos.x / WallP.z * 2.0 - 1.0, 1.0 - pos.y / WallP.w * 2.0);
    const float3 d = float3(ndc.x * CamP.x, ndc.y * CamP.y, -1.0);
    const float cr = cos(CamP.z), sr = sin(CamP.z);
    const float3 r0 = float3(d.x * cr - d.y * sr, d.x * sr + d.y * cr, d.z);
    const float cp = cos(CamQ.x), sp = sin(CamQ.x);
    const float3 rd = normalize(float3(r0.x, r0.y * cp - r0.z * sp, r0.y * sp + r0.z * cp));
    const float3 ro = EyeT.xyz;
    const float3 back = float3(0.0, -sp, cp);
    float3 c = lerp(float3(0.045, 0.040, 0.036), float3(0.12, 0.10, 0.085), saturate(0.5 + 1.5 * rd.y));
    float sceneZ = 1e9;
    [loop] for (int i = 0; i < 4; ++i)
    {
        if (i >= (int)SceneP.x) break;
        const float t = CapsuleHit(ro, rd, CapA[i].xyz, CapB[i].xyz, CapA[i].w);
        if (t > 0.0 && t * dot(rd, -back) < sceneZ)
        {
            const float3 q = ro + rd * t, ba = CapB[i].xyz - CapA[i].xyz;
            const float k = saturate(dot(q - CapA[i].xyz, ba) / max(dot(ba, ba), 1e-6));
            const float3 n = normalize(q - (CapA[i].xyz + ba * k));
            c = float3(0.52, 0.33, 0.26) * (0.22 + 0.78 * saturate(dot(n, normalize(float3(-0.3, 0.8, 0.5)))));
            sceneZ = t * dot(rd, -back);
        }
    }
    if (WallP.x > 0.0 && pos.x > WallP.y)
    {
        c = float3(0.16, 0.20, 0.26);
        sceneZ = WallP.x;
    }
    if (CamP.w < 0.5) c = Orbs(c, ro, rd, back, sceneZ);
    return float4(c, 1.0);
}
)";

struct Gpu {
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11Buffer> cb;
    ComPtr<ID3D11ShaderResourceView> globe;
    ComPtr<ID3D11SamplerState> sampler;
    std::string error;

    // The globes' texture: RGBA float, premultiplied colour and alpha = how much of the world shows through (the interface layer's own convention).
    bool SetGlobeTexture(const std::vector<float>& rgba, int w, int h) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = (UINT)w;
        td.Height = (UINT)h;
        td.MipLevels = td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sd{rgba.data(), (UINT)(w * 16), 0};
        ComPtr<ID3D11Texture2D> tex;
        if (FAILED(dev->CreateTexture2D(&td, &sd, &tex))) return false;
        globe.Reset();
        return SUCCEEDED(dev->CreateShaderResourceView(tex.Get(), nullptr, &globe));
    }

    bool Init(const std::string& fx) {
        const size_t b = fx.find("// ORBS-BEGIN"), e = fx.find("// ORBS-END");
        if (b == std::string::npos || e == std::string::npos || e < b) { error = "the ORBS marks are not in the effect"; return false; }
        const size_t bodyAt = fx.find('\n', b) + 1;
        const std::string src = std::string(kPrelude) + fx.substr(bodyAt, e - bodyAt) + kMain;
        ComPtr<ID3DBlob> vsb, psb, errors;
        HRESULT hr = D3DCompile(src.data(), src.size(), "orbs.hlsl", nullptr, nullptr, "VS", "vs_5_0", 0, 0, &vsb, &errors);
        if (FAILED(hr)) { error = errors ? (const char*)errors->GetBufferPointer() : "vs failed"; return false; }
        hr = D3DCompile(src.data(), src.size(), "orbs.hlsl", nullptr, nullptr, "PS", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL1, 0, &psb, &errors);
        if (FAILED(hr)) { error = errors ? (const char*)errors->GetBufferPointer() : "ps failed"; return false; }
        if (errors && errors->GetBufferSize() > 0) printf("  [compiler warnings]\n%s\n", (const char*)errors->GetBufferPointer());
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, nullptr, &ctx))) { error = "no WARP device"; return false; }
        dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs);
        dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps);
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = (sizeof(Cb) + 15) & ~15u;
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(dev->CreateBuffer(&bd, nullptr, &cb))) { error = "no constant buffer"; return false; }
        D3D11_SAMPLER_DESC sm{};
        sm.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sm.AddressU = sm.AddressV = sm.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sm.MaxLOD = D3D11_FLOAT32_MAX;
        dev->CreateSamplerState(&sm, &sampler);
        SetGlobeTexture(std::vector<float>(2 * 2 * 4, 0.0f), 2, 2);   // (alpha 0 = opaque nothing: replaced by every test that uses it)
        return vs && ps;
    }

    bool Render(const Cb& c, int w, int h, Img* out) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = (UINT)w;
        td.Height = (UINT)h;
        td.MipLevels = td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> rt, stage;
        if (FAILED(dev->CreateTexture2D(&td, nullptr, &rt))) return false;
        td.Usage = D3D11_USAGE_STAGING;
        td.BindFlags = 0;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(dev->CreateTexture2D(&td, nullptr, &stage))) return false;
        ComPtr<ID3D11RenderTargetView> rtv;
        if (FAILED(dev->CreateRenderTargetView(rt.Get(), nullptr, &rtv))) return false;
        Cb k = c;
        k.wall[2] = (float)w;
        k.wall[3] = (float)h;
        ctx->UpdateSubresource(cb.Get(), 0, nullptr, &k, 0, 0);
        ID3D11RenderTargetView* rtvs[1] = {rtv.Get()};
        ctx->OMSetRenderTargets(1, rtvs, nullptr);
        const D3D11_VIEWPORT vp{0, 0, (float)w, (float)h, 0, 1};
        ctx->RSSetViewports(1, &vp);
        ctx->VSSetShader(vs.Get(), nullptr, 0);
        ctx->PSSetShader(ps.Get(), nullptr, 0);
        ID3D11Buffer* cbs[1] = {cb.Get()};
        ctx->PSSetConstantBuffers(0, 1, cbs);
        ID3D11ShaderResourceView* srvs[1] = {globe.Get()};
        ctx->PSSetShaderResources(0, 1, srvs);
        ID3D11SamplerState* sams[1] = {sampler.Get()};
        ctx->PSSetSamplers(0, 1, sams);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->Draw(3, 0);
        ctx->CopyResource(stage.Get(), rt.Get());
        D3D11_MAPPED_SUBRESOURCE m{};
        if (FAILED(ctx->Map(stage.Get(), 0, D3D11_MAP_READ, 0, &m))) return false;
        out->w = w;
        out->h = h;
        out->px.resize((size_t)w * h * 4);
        for (int y = 0; y < h; ++y) memcpy(&out->px[(size_t)y * w * 4], (const uint8_t*)m.pData + (size_t)y * m.RowPitch, (size_t)w * 16);
        ctx->Unmap(stage.Get(), 0);
        return true;
    }
};

constexpr int kW = 480, kH = 320;

// A pinhole camera like the shader's (no roll): where a world point lands, in pixels, and how many pixels a length at that depth is.
struct Cam {
    float eye[3] = {0, 0, 0};
    float tanX = 0.3f;
    float tanY() const { return tanX * (float)kH / (float)kW; }
    void Project(const float p[3], float* x, float* y, float* pxPerM) const {
        const float dx = p[0] - eye[0], dy = p[1] - eye[1], dz = eye[2] - p[2];   // dz: how far ahead
        *x = (0.5f + 0.5f * dx / dz / tanX) * (float)kW;
        *y = (0.5f - 0.5f * dy / dz / tanY()) * (float)kH;
        *pxPerM = 0.5f * (float)kW / tanX / dz;
    }
};

void SetOrb(Cb& cb, int i, float x, float y, float z, float r, float fill, float opacity, float glow, int kind) {
    cb.orbPos[i][0] = x; cb.orbPos[i][1] = y; cb.orbPos[i][2] = z; cb.orbPos[i][3] = r;
    cb.orbLook[i][0] = fill; cb.orbLook[i][1] = opacity; cb.orbLook[i][2] = glow; cb.orbLook[i][3] = (float)kind;
}
void NoOrb(Cb& cb, int i) { SetOrb(cb, i, 0, 0, 9.0f, 0.02f, 0.0f, 0.0f, 0.0f, 0); }
void SetCaps(Cb& cb, int i, const float a[3], const float b[3], float r) {
    for (int k = 0; k < 3; ++k) { cb.capA[i][k] = a[k]; cb.capB[i][k] = b[k]; }
    cb.capA[i][3] = r;
    cb.capB[i][3] = 0.0f;
}
bool Equal(const Img& a, const Img& b) { return a.w == b.w && a.h == b.h && a.px == b.px; }
// the two pictures differ at this pixel
bool Differs(const Img& a, const Img& b, int x, int y, float eps = 1e-6f) {
    const float* p = a.At(x, y);
    const float* q = b.At(x, y);
    return std::fabs(p[0] - q[0]) > eps || std::fabs(p[1] - q[1]) > eps || std::fabs(p[2] - q[2]) > eps;
}
bool Reddish(const float* p) { return p[0] > 0.30f && p[0] > 2.0f * p[1] && p[0] > 2.0f * p[2]; }
bool Bluish(const float* p) { return p[2] > 0.22f && p[2] > 1.6f * p[0] && p[2] > 1.1f * p[1]; }

// The rows between the glass's top and bottom (down the middle) where the pixel is the liquid's colour.
int LiquidRows(const Img& img, int cx, int cy, int rpx, bool red) {
    int n = 0;
    for (int y = cy - rpx; y <= cy + rpx; ++y) {
        const float* p = img.At(cx - rpx / 3, y);   // a little off the highlight
        if (red ? Reddish(p) : Bluish(p)) ++n;
    }
    return n;
}

// ---- the game's own globes ---------------------------------------------------------------------------------------------------------------------------

void TestGlobesGeometry() {
    printf("the game's own globes: where they are in its interface layer\n");
    using namespace hudglobes;
    // The real interface layer of a square 1920 x 1920 window at Safe Screen Percent 80 (a dump of it, measured: the globes' tops at y 1481, their middles ~1600)
    Boxes b = Compute(1920, 1920, 0.8f);
    const float k = 1920.0f / 2160.0f * 0.8f;
    Expect(b.ok, "1920 x 1920 at 80%%: the globes fit whole");
    Expect(Near(b.cx[0], 960.0f - 986.0f * k, 0.01f) && Near(b.cx[1], 960.0f + 988.0f * k, 0.01f), "the middles: %.1f and %.1f from the layout's x offsets", b.cx[0], b.cx[1]);
    Expect(Near(b.cy, 1920.0f * 0.9f - 178.0f * k, 0.01f) && Near(b.cy, 1601.4f, 0.5f), "the middle's height %.1f (a real dump's was ~1601)", b.cy);
    Expect(Near(b.radius, 160.0f * k, 1e-3f), "the cut disc's radius %.1f px", b.radius);
    Expect(b.half == (int)std::ceil(b.radius * kMargin) && b.fit > 0.9f && b.fit < 0.96f, "the square's half side %d, the disc fills %.3f of it", b.half, b.fit);
    Expect(b.top >= 0 && b.top + 2 * b.half <= 1920 && b.left[0] >= 0 && b.left[1] + 2 * b.half <= 1920, "both squares are inside the layer");
    Expect(b.cx[0] < 960.0f && b.cx[1] > 960.0f, "life on the left, mana on the right");
    Expect(Near(960.0f - b.cx[0], b.cx[1] - 960.0f, 3.0f), "about as far from the middle on either side");
    // The result in layout units does not depend on the layer's size: any size where the globes fit whole cuts the same disc of the layout
    struct Case { uint32_t w, h; float s; };
    const Case cases[] = {{1920, 1920, 0.8f}, {1536, 1536, 0.8f}, {2048, 2048, 0.8f}, {1920, 1080, 1.0f}, {2560, 1440, 1.0f}, {3840, 2160, 1.0f}, {1920, 1200, 1.0f}, {3440, 1440, 0.9f}};
    for (const Case& c : cases) {
        const Boxes x = Compute(c.w, c.h, c.s);
        const float kk = (float)c.h / 2160.0f * c.s;
        Expect(x.ok, "%u x %u at %.0f%%: whole", c.w, c.h, c.s * 100.0f);
        if (!x.ok) continue;
        Expect(Near(x.radius / kk, 160.0f, 0.01f) && Near((x.cx[1] - x.cx[0]) / kk, 988.0f + 986.0f, 0.05f) && Near(x.fit, b.fit, 0.03f),
               "%u x %u at %.0f%%: the same disc and the same spacing in the game's own units (%.2f units, fit %.3f)", c.w, c.h, c.s * 100.0f, x.radius / kk, x.fit);
    }
    // not whole: a window narrower than the panel, at a safe area that is not smaller
    Expect(!Compute(1536, 1536, 1.0f).ok, "1536 x 1536 at 100%%: the globes are cut off at the sides - nothing is cut");
    Expect(!Compute(100, 100, 0.8f).ok && !Compute(0, 0, 0.8f).ok, "a layer too small: nothing");
    Expect(!Compute(1920, 1920, 0.0f).ok && !Compute(1920, 1920, 2.0f).ok && !Compute(1920, 1920, std::nanf("")).ok, "a safe area out of range or NaN: nothing");
    Expect(!Compute(1920, 1920, 0.2f).ok, "a safe area under 30%%: nothing");
}

// A synthetic cut of the interface layer: the life globe's square on the left half, the mana globe's on the right, premultiplied colour and alpha = how
// much of the world shows through (the layer's own convention): a lit red / blue sphere inside the disc (opaque), a grey "statue" outside it (also opaque:
// it must not show), a green marker at the picture's top, and a hole (alpha 1) at the lower right.
std::vector<float> SyntheticGlobes(int side, float fit) {
    std::vector<float> px((size_t)side * 2 * side * 4);
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < 2 * side; ++x) {
            const int i = x / side, lx = x % side;
            const float u = ((float)lx + 0.5f) / (float)side * 2.0f - 1.0f, v = ((float)y + 0.5f) / (float)side * 2.0f - 1.0f;   // -1 .. 1, v down
            const float r = std::hypot(u, v) / fit;
            float c[4] = {0.5f, 0.5f, 0.5f, 0.0f};   // the statue
            if (r <= 1.0f) {
                const float z = std::sqrt(std::max(0.0f, 1.0f - r * r));
                const float lit = 0.35f + 0.65f * z;
                const float base[3] = {i == 0 ? 0.85f : 0.10f, i == 0 ? 0.05f : 0.20f, i == 0 ? 0.06f : 0.85f};
                for (int k = 0; k < 3; ++k) c[k] = base[k] * lit;
                c[3] = 0.0f;
                if (std::hypot(u - 0.0f, v + 0.6f * fit) < 0.07f) { c[0] = 0.0f; c[1] = 1.0f; c[2] = 0.0f; }   // the marker, at the picture's top
                if (std::hypot(u - 0.45f * fit, v - 0.45f * fit) < 0.09f) { c[0] = c[1] = c[2] = 0.0f; c[3] = 1.0f; }   // a hole: the world shows through
            }
            memcpy(&px[((size_t)y * 2 * side + x) * 4], c, sizeof c);
        }
    return px;
}

// The centre of the pixels that are mostly green (the marker) in an image; false if none.
bool GreenCentre(const Img& img, double* x, double* y) {
    double sx = 0.0, sy = 0.0, sw = 0.0;
    for (int j = 0; j < img.h; ++j)
        for (int i = 0; i < img.w; ++i) {
            const float* p = img.At(i, j);
            if (p[1] > 0.7f && p[1] > 2.0f * p[0] && p[1] > 2.0f * p[2]) { sx += i; sy += j; sw += 1.0; }
        }
    if (sw < 4.0) return false;
    *x = sx / sw;
    *y = sy / sw;
    return true;
}

void TestGlobesShader(Gpu& gpu, const Cam& cam, float R, float Z, int cx, int cy, int rpx) {
    printf("the game's own globes on the ball (WARP)\n");
    const int side = 128;
    const float fit = 0.94f;
    gpu.SetGlobeTexture(SyntheticGlobes(side, fit), 2 * side, side);
    auto base = [&]() {
        Cb c;
        c.camP[0] = cam.tanX;
        c.camP[1] = cam.tanY();
        c.scene[0] = 0;
        c.picP[0] = 1.0f;
        c.picP[1] = fit;
        NoOrb(c, 0);
        NoOrb(c, 1);
        return c;
    };
    Img bg, a, b;
    {
        Cb c = base();
        c.camP[3] = 1.0f;
        gpu.Render(c, kW, kH, &bg);
    }
    // the picture on the ball: the life globe's red, the mana globe's blue, the real shading
    Cb life = base();
    SetOrb(life, 0, 0, 0, Z, R, 0.3f, 1.0f, 0.0f, 0);
    gpu.Render(life, kW, kH, &a);
    const float* p = a.At(cx, cy);
    Expect(Reddish(p) && p[0] > 0.5f, "the life globe's picture, red, bright in the middle (%.2f %.2f %.2f)", p[0], p[1], p[2]);
    Cb mana = base();
    SetOrb(mana, 0, 0, 0, Z, R, 0.3f, 1.0f, 0.0f, 1);
    gpu.Render(mana, kW, kH, &b);
    p = b.At(cx, cy);
    Expect(Bluish(p) && p[2] > 0.5f, "the mana globe's picture comes from the right half of the texture, blue (%.2f %.2f %.2f)", p[0], p[1], p[2]);
    // shaded as the picture is: darker toward the edge (the sphere in the picture), the middle brightest
    Expect(a.At(cx + (int)(0.9f * rpx), cy)[0] < 0.75f * a.At(cx, cy)[0], "the picture's own shading is kept (the edge darker than the middle)");
    // the level of liquid is the picture's, not the fill uniform
    Cb life2 = life;
    SetOrb(life2, 0, 0, 0, Z, R, 1.0f, 1.0f, 0.0f, 0);
    Img c2;
    gpu.Render(life2, kW, kH, &c2);
    Expect(Equal(a, c2), "the fill uniform is not used: the game's own globe shows its own level");
    // the disc: the statue round the globe in the picture does not show; the ball's silhouette is the edge
    {
        const float* e = a.At(cx + (int)(0.97f * rpx), cy);
        Expect(std::fabs(e[0] - e[1]) > 0.02f || e[0] < 0.05f, "just inside the edge it is the globe, not the grey statue (%.2f %.2f %.2f)", e[0], e[1], e[2]);
        const float* o = a.At(cx + (int)(1.12f * rpx), cy);
        const float* ob = bg.At(cx + (int)(1.12f * rpx), cy);
        Expect(o[0] > ob[0] && o[0] < ob[0] + 0.2f && std::fabs((o[0] - ob[0]) - (o[1] - ob[1])) > 0.01f, "outside the edge only the red halo, no statue (%.3f over %.3f)", o[0], ob[0]);
    }
    // a hole in the picture (the world shows through): the world's pixel, bit for bit (the marker's partner at the lower right)
    {
        const int hx = cx + (int)(0.45f * rpx), hy = cy + (int)(0.45f * rpx);
        Expect(!Differs(a, bg, hx, hy), "where the picture's alpha is 1 the world shows through, bit for bit");
        Expect(Differs(a, bg, cx, cy) && Differs(a, bg, hx + (int)(0.3f * rpx), hy), "and the globe is there beside it");
    }
    // upright in the WORLD: the picture's top stays up whatever the head does
    {
        double mx = 0.0, my = 0.0;
        Expect(GreenCentre(a, &mx, &my) && my < cy - 0.4 * rpx && std::fabs(mx - cx) < 0.12 * rpx, "the marker at the picture's top is at the ball's top (%.0f, %.0f)", mx, my);
        Cb rolled = life;
        rolled.camP[2] = 0.5236f;   // the head rolled 30 degrees
        Img r;
        gpu.Render(rolled, kW, kH, &r);
        double rx = 0.0, ry = 0.0;
        const bool found = GreenCentre(r, &rx, &ry);
        Expect(found && std::fabs(rx - cx) > 0.2 * rpx && std::fabs(rx - cx) < 0.45 * rpx && ry < cy, "rolled 30 degrees the marker goes round the ball with the world's up, to (%.0f, %.0f)", rx, ry);
        Cb pitched = life;
        pitched.camQ[0] = -0.03f;   // looking a little down: the ball is a little above the view's middle, still the marker is up
        Img q;
        gpu.Render(pitched, kW, kH, &q);
        double qx, qy;
        Expect(GreenCentre(q, &qx, &qy), "looking down a little: the marker is still there");
    }
    // opacity 0, a wall in front, a halo and the heartbeat
    {
        Cb off = life;
        SetOrb(off, 0, 0, 0, Z, R, 0.3f, 0.0f, 0.0f, 0);
        Img z;
        gpu.Render(off, kW, kH, &z);
        Expect(Equal(z, bg), "opacity 0: the picture bit for bit");
        Cb wall = life;
        wall.wall[0] = -Z - 0.10f;
        wall.wall[1] = (float)cx;
        Cb wallBg = base();
        wallBg.camP[3] = 1.0f;
        wallBg.wall[0] = wall.wall[0];
        wallBg.wall[1] = wall.wall[1];
        Img w1, w0;
        gpu.Render(wall, kW, kH, &w1);
        gpu.Render(wallBg, kW, kH, &w0);
        bool hidden = true;
        for (int y = cy - rpx - 20; y <= cy + rpx + 20; ++y)
            for (int x = cx + 2; x <= cx + rpx + 20; ++x)
                if (Differs(w1, w0, x, y)) hidden = false;
        Expect(hidden, "a wall in front hides the picture ball, bit for bit");
        Cb beat = life;
        SetOrb(beat, 0, 0, 0, Z, R, 0.3f, 1.0f, 1.0f, 0);
        Img h1;
        gpu.Render(beat, kW, kH, &h1);
        Expect(h1.At(cx, cy)[0] > 1.2f * a.At(cx, cy)[0] - 1e-3f && h1.At(cx + (int)(1.2f * rpx), cy)[0] > a.At(cx + (int)(1.2f * rpx), cy)[0], "the heartbeat brightens the picture and its halo");
        Cb half = life;
        SetOrb(half, 0, 0, 0, Z, R, 0.3f, 0.5f, 0.0f, 0);
        Img hf;
        gpu.Render(half, kW, kH, &hf);
        Expect(std::fabs((hf.At(cx, cy)[0] - bg.At(cx, cy)[0]) - 0.5f * (a.At(cx, cy)[0] - bg.At(cx, cy)[0])) < 0.03f, "opacity 0.5 lays it over the picture at half strength");
    }
    // parallax: an eye 2 cm right sees the ball further left
    {
        Cb l = life, r = life;
        l.eyeT[0] = -0.01f;
        r.eyeT[0] = 0.01f;
        Img il, ir;
        gpu.Render(l, kW, kH, &il);
        gpu.Render(r, kW, kH, &ir);
        double lx = 0.0, ly = 0.0, rx = 0.0, ry = 0.0;
        Expect(GreenCentre(il, &lx, &ly) && GreenCentre(ir, &rx, &ry) && lx - rx > 20.0, "parallax: the ball is seen from each eye's own place (%.1f vs %.1f px)", rx, lx);
    }
    // the glass look is back when the picture is off (OrbPic.x 0)
    {
        Cb g = life;
        g.picP[0] = 0.0f;
        Img gi;
        gpu.Render(g, kW, kH, &gi);
        Expect(!Equal(gi, a) && LiquidRows(gi, cx, cy, rpx, true) > 10, "OrbPic.x = 0: the glass ball with its liquid again");
    }
}

// A real dump of the game's interface layer (Ctrl+F10: uitex_<id>_<W>x<H>_10.raw, R16G16B16A16_FLOAT behind a 16-byte header), on this machine only,
// given as the test's second argument (the third is the layer's Safe Screen Percent, 0.8 by default): the globes are cut out of it by hudglobes::Compute
// exactly as vr/hud.cpp does, and laid on the wrists in the pictures - to see what the medallions look like with the game's own art.
std::string g_dumpPath;
float g_dumpSafe = 0.8f;

float HalfToFloat(uint16_t h) {
    const uint32_t sign = (h >> 15) & 1u, exp = (h >> 10) & 31u, man = h & 1023u;
    float v;
    if (exp == 0) v = std::ldexp((float)man, -24);
    else if (exp == 31) v = man ? std::nanf("") : std::numeric_limits<float>::infinity();
    else v = std::ldexp((float)(man | 1024u), (int)exp - 25);
    return sign ? -v : v;
}

bool LoadDumpGlobes(std::vector<float>* tex, int* side, hudglobes::Boxes* boxes, std::vector<uint8_t>* previewRgb, int* previewW, int* previewH) {
    const size_t us = g_dumpPath.rfind('_');
    const size_t xs = g_dumpPath.rfind('x', us);
    const size_t ss = g_dumpPath.rfind('_', xs);
    if (us == std::string::npos || xs == std::string::npos || ss == std::string::npos) return false;
    const int w = atoi(g_dumpPath.substr(ss + 1, xs - ss - 1).c_str()), h = atoi(g_dumpPath.substr(xs + 1, us - xs - 1).c_str());
    FILE* f = nullptr;
    if (w <= 0 || h <= 0 || fopen_s(&f, g_dumpPath.c_str(), "rb") || !f) return false;
    std::vector<uint16_t> raw((size_t)w * h * 4);
    fseek(f, 16, SEEK_SET);
    const size_t got = fread(raw.data(), 2, raw.size(), f);
    fclose(f);
    if (got != raw.size()) return false;
    *boxes = hudglobes::Compute((uint32_t)w, (uint32_t)h, g_dumpSafe);
    if (!boxes->ok) return false;
    *side = 2 * boxes->half;
    tex->assign((size_t)(*side) * 2 * (*side) * 4, 0.0f);
    for (int i = 0; i < 2; ++i)
        for (int y = 0; y < *side; ++y)
            for (int x = 0; x < *side; ++x) {
                const size_t src = ((size_t)(boxes->top + y) * w + boxes->left[i] + x) * 4, dst = ((size_t)y * 2 * (*side) + i * (*side) + x) * 4;
                for (int k = 0; k < 4; ++k) (*tex)[dst + k] = HalfToFloat(raw[src + k]);
            }
    // a preview of the cut: the squares over a mid grey, as the layer is laid over the world (world * alpha + colour)
    *previewW = 2 * (*side);
    *previewH = *side;
    previewRgb->assign((size_t)(*previewW) * (*previewH) * 3, 0);
    for (size_t i = 0; i < (size_t)(*previewW) * (*previewH); ++i)
        for (int k = 0; k < 3; ++k) {
            const float v = 0.30f * std::clamp((*tex)[i * 4 + 3], 0.0f, 1.0f) + std::max(0.0f, (*tex)[i * 4 + k]);
            (*previewRgb)[i * 3 + k] = (uint8_t)std::clamp((int)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f), 0, 255);
        }
    return true;
}


void TestAxisShader(Gpu& gpu, const Cam& cam, float R, float Z, int cx, int cy, int rpx) {
    printf("the liquid along an axis (WARP)\n");
    auto base = [&]() {
        Cb c;
        c.camP[0] = cam.tanX;
        c.camP[1] = cam.tanY();
        c.scene[0] = 0;
        NoOrb(c, 0);
        NoOrb(c, 1);
        return c;
    };
    auto setAxis = [](Cb& c, float x, float y, float z) {
        const float l = std::sqrt(x * x + y * y + z * z);
        c.orbAxis[0][0] = x / l; c.orbAxis[0][1] = y / l; c.orbAxis[0][2] = z / l;
    };
    gpu.SetGlobeTexture(std::vector<float>(2 * 2 * 4, 0.0f), 2, 2);
    // the axis to the right (toward the hand): the liquid on the elbow (left) side, its surface a vertical line across the middle for half full
    {
        Cb c = base();
        SetOrb(c, 0, 0, 0, Z, R, 0.5f, 1.0f, 0.0f, 0);
        setAxis(c, 1, 0, 0);
        Img a;
        gpu.Render(c, kW, kH, &a);
        const float* lq = a.At(cx - rpx / 2, cy + rpx / 4);
        const float* em = a.At(cx + rpx / 2, cy + rpx / 4);
        Expect(Reddish(lq), "axis to the right, half full: liquid on the left (%.2f %.2f %.2f)", lq[0], lq[1], lq[2]);
        Expect(!Reddish(em), "and none on the right, toward the hand (%.2f %.2f %.2f)", em[0], em[1], em[2]);
        // the red columns: left of the middle, none right of it (below the highlight)
        int left = 0, right = 0;
        for (int x = cx - rpx; x <= cx + rpx; ++x) {
            const float* p = a.At(x, cy + rpx / 4);
            if (Reddish(p)) (x < cx ? left : right)++;
        }
        Expect(left > rpx / 2 && right < 6, "the liquid ends at the middle, across the axis: %d columns left, %d right", left, right);
        // the level follows the fill: a quarter full ends a quarter of the way across
        Cb q = c;
        SetOrb(q, 0, 0, 0, Z, R, 0.25f, 1.0f, 0.0f, 0);
        Img b;
        gpu.Render(q, kW, kH, &b);
        int lc = 0;
        for (int x = cx - rpx; x <= cx + rpx; ++x)
            if (Reddish(b.At(x, cy + rpx / 4))) ++lc;
        Expect(lc < left - rpx / 4 && lc > rpx / 6, "a quarter full: less of the width than half full (%d vs %d columns)", lc, left);
        // full and empty
        SetOrb(q, 0, 0, 0, Z, R, 1.0f, 1.0f, 0.0f, 0);
        gpu.Render(q, kW, kH, &b);
        Expect(Reddish(b.At(cx + rpx / 2, cy + rpx / 4)) && Reddish(b.At(cx - rpx / 2, cy + rpx / 4)), "full: liquid across the whole ball");
        SetOrb(q, 0, 0, 0, Z, R, 0.0f, 1.0f, 0.0f, 0);
        gpu.Render(q, kW, kH, &b);
        Expect(!Reddish(b.At(cx + rpx / 2, cy + rpx / 4)) && !Reddish(b.At(cx - rpx / 2, cy + rpx / 4)), "empty: none");
    }
    // the axis to the left: the mirror image
    {
        Cb c = base();
        SetOrb(c, 0, 0, 0, Z, R, 0.5f, 1.0f, 0.0f, 0);
        setAxis(c, -1, 0, 0);
        Img a;
        gpu.Render(c, kW, kH, &a);
        Expect(Reddish(a.At(cx + rpx / 2, cy + rpx / 4)) && !Reddish(a.At(cx - rpx / 2, cy + rpx / 4)), "axis to the left: the liquid is on the right");
    }
    // a slanted axis: up and to the right - the liquid in the lower left, the empty glass in the upper right
    {
        Cb c = base();
        SetOrb(c, 0, 0, 0, Z, R, 0.5f, 1.0f, 0.0f, 0);
        setAxis(c, 1, 1, 0);
        Img a;
        gpu.Render(c, kW, kH, &a);
        Expect(Reddish(a.At(cx - rpx / 2, cy + rpx / 2)) && !Reddish(a.At(cx + rpx / 2, cy - rpx / 2)), "a slanted axis: liquid at its low end, the glass empty at its high end");
    }
    // the axis is the ball's own: the same view with the world's up gives the old level surface
    {
        Cb c = base();
        SetOrb(c, 0, 0, 0, Z, R, 0.5f, 1.0f, 0.0f, 0);
        Img up, def;
        gpu.Render(c, kW, kH, &def);   // the default axis is the world's up
        setAxis(c, 0, 1, 0);
        gpu.Render(c, kW, kH, &up);
        Expect(Equal(up, def), "the world's up axis is the default (level in the world)");
        // the world's-up axis stays level when the camera rolls; an axis fixed to the (rolled) screen's right does not turn with it
        Cb r = c;
        r.camP[2] = 0.5236f;
        setAxis(r, 1, 0, 0);
        Img ra;
        gpu.Render(r, kW, kH, &ra);
        // the axis is the WORLD's +x; the head rolled 30 degrees: the liquid's edge tilts in the picture by 30 degrees, but the liquid is still on the world's left side
        Expect(Reddish(ra.At(cx - rpx / 2, cy + rpx / 4)) || Reddish(ra.At(cx - rpx / 2, cy + rpx / 2)), "a rolled head: the liquid stays on the axis's low side, in the world");
    }
    // the axis along the line of sight (toward the camera): the surface faces the viewer, the liquid is behind it - the ball is full of liquid from the front
    {
        Cb c = base();
        SetOrb(c, 0, 0, 0, Z, R, 0.5f, 1.0f, 0.0f, 0);
        setAxis(c, 0, 0, 1);
        Img a;
        gpu.Render(c, kW, kH, &a);
        const float* m = a.At(cx, cy);
        Expect(m[0] > 0.2f && m[0] > m[2], "the axis toward the eye: the liquid's surface faces us, the middle is liquid (%.2f %.2f %.2f)", m[0], m[1], m[2]);
        setAxis(c, 0, 0, -1);
        gpu.Render(c, kW, kH, &a);
        const float* n = a.At(cx, cy);
        Expect(n[0] > 0.1f, "the axis away from the eye: the liquid is on the near side, seen through the glass (%.2f %.2f %.2f)", n[0], n[1], n[2]);
    }
}

// ---- the medallion on WARP: the effect's own HLSL, a dome on a metal base ----------------------------------------------------------------------------------------------------

void SetFrame(Cb& c, int i, V3 bk, V3 al, V3 fa) {
    const V3 b = hset::Unit(bk), a = hset::Unit(al), f = hset::Unit(fa);
    c.orbBack[i][0] = b.x; c.orbBack[i][1] = b.y; c.orbBack[i][2] = b.z;
    c.orbAlong[i][0] = a.x; c.orbAlong[i][1] = a.y; c.orbAlong[i][2] = a.z;
    c.orbAxis[i][0] = f.x; c.orbAxis[i][1] = f.y; c.orbAxis[i][2] = f.z;
}
bool GoldLike(const float* p) { return p[0] > 1.5f * p[2] && p[0] > 1.12f * p[1] && p[1] > 0.38f * p[0] && p[0] > 0.12f; }   // (red liquid is not gold: its green is nearly none)
void RingAverage(const Img& img, int cx, int cy, float r0, float r1, float out[3]) {   // the mean colour of the pixels between two radii
    double s[3] = {0, 0, 0};
    int n = 0;
    for (int y = cy - (int)r1; y <= cy + (int)r1; ++y)
        for (int x = cx - (int)r1; x <= cx + (int)r1; ++x) {
            const float d = std::sqrt((float)((x - cx) * (x - cx) + (y - cy) * (y - cy)));
            if (d < r0 || d > r1) continue;
            const float* p = img.At(x, y);
            for (int k = 0; k < 3; ++k) s[k] += p[k];
            ++n;
        }
    for (int k = 0; k < 3; ++k) out[k] = n ? (float)(s[k] / n) : 0.0f;
}
// the mean difference between a and b turned by k quarter turns about the corner (cx, cy), over the ring between two radii
float TurnedDiff(const Img& a, const Img& b, int cx, int cy, float r0, float r1, int k) {
    double sum = 0.0;
    int n = 0;
    for (int y = cy - (int)r1; y <= cy + (int)r1; ++y)
        for (int x = cx - (int)r1; x <= cx + (int)r1; ++x) {
            const float d = std::sqrt((float)((x - cx + 0.5f) * (x - cx + 0.5f) + (y - cy + 0.5f) * (y - cy + 0.5f)));
            if (d < r0 || d > r1) continue;
            const int sx = x - cx, sy = y - cy;
            int tx, ty;
            switch (k & 3) {
                case 0: tx = sx; ty = sy; break;
                case 1: tx = -sy - 1; ty = sx; break;
                case 2: tx = -sx - 1; ty = -sy - 1; break;
                default: tx = sy; ty = -sx - 1; break;
            }
            const float* p = a.At(x, y);
            const float* q = b.At(cx + tx, cy + ty);
            sum += (std::fabs(p[0] - q[0]) + std::fabs(p[1] - q[1]) + std::fabs(p[2] - q[2])) / 3.0;
            ++n;
        }
    return n ? (float)(sum / n) : 1.0f;
}

void TestMedallionShader(Gpu& gpu, const Cam& cam, float R, float Z, int cx, int cy, int rpx, const std::string& dir) {
    printf("the medallion on WARP (a dome on a gold base)\n");
    auto base = [&]() {
        Cb c;
        c.camP[0] = cam.tanX;
        c.camP[1] = cam.tanY();
        c.scene[0] = 0;
        c.orbMed[0] = 1.0f;   // the medallion
        c.orbMed[1] = 0.0f;   // clean, for the colours
        NoOrb(c, 0);
        NoOrb(c, 1);
        return c;
    };
    const V3 toEye{0, 0, 1}, up{0, 1, 0}, left{-1, 0, 0}, right{1, 0, 0};
    gpu.SetGlobeTexture(std::vector<float>(2 * 2 * 4, 0.0f), 2, 2);
    Img bg;
    {
        Cb c = base();
        c.camP[3] = 1.0f;
        gpu.Render(c, kW, kH, &bg);
    }
    // face-on: the base's ring round the dome, the dome with the liquid in it, the base's size from the Size slider
    {
        Cb c = base();
        SetOrb(c, 0, 0, 0, Z, R, 1.0f, 1.0f, 0.0f, 1);   // mana (a blue halo cannot pass for gold)
        SetFrame(c, 0, toEye, up, toEye);
        Img a;
        gpu.Render(c, kW, kH, &a);
        Expect(Bluish(a.At(cx, cy)) || a.At(cx, cy)[2] > a.At(cx, cy)[0], "face-on, full: the liquid in the dome (%.2f %.2f %.2f)", a.At(cx, cy)[0], a.At(cx, cy)[1], a.At(cx, cy)[2]);
        int lastGold = 0, firstGold = 0;
        for (int x = cx + rpx / 2; x < cx + 2 * rpx && x < kW; ++x)
            if (GoldLike(a.At(x, cy))) { if (!firstGold) firstGold = x; lastGold = x; }
        Expect(lastGold > cx + (int)(0.96f * rpx) && lastGold < cx + (int)(1.04f * rpx), "the base's edge is the Size's radius: the gold ends at %.2f of it", (float)(lastGold - cx) / (float)rpx);
        Expect(firstGold > cx + (int)(0.72f * rpx) && firstGold < cx + (int)(0.88f * rpx), "the gold begins where the dome ends, a little inside the rim: %.2f of the base's radius", (float)(firstGold - cx) / (float)rpx);
        const float* out = a.At(cx + (int)(1.25f * rpx), cy);
        Expect(!GoldLike(out), "outside the base only the halo");
    }
    // from the side: the dome is as high as it is wide at its foot (0.8), the plate is a fifth of the radius thick under the plane
    {
        Cb c = base();
        SetOrb(c, 0, 0, 0, Z, R, 1.0f, 1.0f, 0.0f, 1);   // full: the dome is liquid to its top, which shows against the dark
        SetFrame(c, 0, up, V3{0, 0, -1}, up);
        Img a;
        gpu.Render(c, kW, kH, &a);
        int top = cy, bottom = cy;
        // (the object is much brighter than the halo round it, which is the 0.12 at most: a threshold between them)
        for (int y = cy; y > 0; --y) { if (Differs(a, bg, cx, y, 0.16f)) top = y; else if (top != cy) break; }
        for (int y = cy; y < kH - 1; ++y) { if (Differs(a, bg, cx, y, 0.16f)) bottom = y; else if (bottom != cy) break; }
        const float dome = (float)(cy - top) / (float)rpx, plate = (float)(bottom - cy) / (float)rpx;
        Expect(dome > 0.74f && dome < 0.86f, "from the side the dome is %.2f of the base's radius high (DomeHeight: 0.8)", dome);
        Expect(plate > 0.14f && plate < 0.26f, "and the plate %.2f of it thick (0.2)", plate);
        int xl = kW, xr = 0;   // the plate's side is metal: the gold along its row (the halo is not gold)
        for (int x = 0; x < kW; ++x) if (GoldLike(a.At(x, cy + (int)(0.1f * rpx)))) { xl = std::min(xl, x); xr = std::max(xr, x); }
        const float width = (float)(xr - xl) / (2.0f * (float)rpx);
        Expect(width > 0.92f && width < 1.06f, "and as wide as the base's diameter (%.2f)", width);
        Expect(GoldLike(a.At(cx + (int)(0.9f * rpx), cy + (int)(0.1f * rpx))), "the plate's side is metal");
    }
    // the fill along the wrist's axes, from the side (the wrist's back up in the picture, along away from us, across to the right)
    {
        auto reds = [&](const Img& a, int x) {
            int n = 0;
            for (int y = cy - rpx; y <= cy; ++y) if (Reddish(a.At(x, y))) ++n;
            return n;
        };
        const float fills[3] = {0.25f, 0.5f, 1.0f};
        int rows[3] = {};
        for (int k = 0; k < 3; ++k) {
            Cb c = base();
            SetOrb(c, 0, 0, 0, Z, R, fills[k], 1.0f, 0.0f, 0);
            SetFrame(c, 0, up, V3{0, 0, -1}, up);   // out of the hand: from the skin up
            Img a;
            gpu.Render(c, kW, kH, &a);
            rows[k] = reds(a, cx + rpx / 4);
        }
        Expect(rows[1] > rows[0] + 8 && rows[2] > rows[1] + 8, "out of the hand: the liquid rises with the fill (%d, %d, %d rows)", rows[0], rows[1], rows[2]);
        Expect(rows[1] > (int)(0.28f * rpx) && rows[1] < (int)(0.52f * rpx), "half full is half of the dome's height, %d of %d rows (the level runs over the dome's own reach)", rows[1], (int)(0.8f * rpx));
        Expect(rows[2] > (int)(0.62f * rpx), "full: the dome's whole height (%d rows)", rows[2]);
        Expect(rows[0] > (int)(0.10f * rpx) && rows[0] < (int)(0.30f * rpx), "a quarter full: a quarter of it (%d rows)", rows[0]);
        // across the hand: the thumb side full = the axis pointing the other way; the level is a plane across the dome
        Cb c = base();
        SetOrb(c, 0, 0, 0, Z, R, 0.5f, 1.0f, 0.0f, 0);
        SetFrame(c, 0, up, V3{0, 0, -1}, right);   // rises to the right: the left side is full
        Img a;
        gpu.Render(c, kW, kH, &a);
        Expect(Reddish(a.At(cx - (int)(0.40f * rpx), cy - (int)(0.2f * rpx))) && !Reddish(a.At(cx + (int)(0.40f * rpx), cy - (int)(0.2f * rpx))), "across the hand, half full: the liquid on the low side, the other half of the dome empty");
        SetFrame(c, 0, up, V3{0, 0, -1}, left);
        gpu.Render(c, kW, kH, &a);
        Expect(!Reddish(a.At(cx - (int)(0.40f * rpx), cy - (int)(0.2f * rpx))) && Reddish(a.At(cx + (int)(0.40f * rpx), cy - (int)(0.2f * rpx))), "and the other way: the mirror image");
        // the dome's reach: out of the hand 100 % fills the dome to the top; across the hand 100 % fills it from side to side
        SetOrb(c, 0, 0, 0, Z, R, 1.0f, 1.0f, 0.0f, 0);
        SetFrame(c, 0, up, V3{0, 0, -1}, right);
        gpu.Render(c, kW, kH, &a);
        Expect(Reddish(a.At(cx - (int)(0.4f * rpx), cy - (int)(0.2f * rpx))) && Reddish(a.At(cx + (int)(0.4f * rpx), cy - (int)(0.2f * rpx))) && Reddish(a.At(cx, cy - (int)(0.6f * rpx))), "across the hand, full: the whole dome");
        // along the arm (away from the eye here): the level plane faces us, the liquid is on the near side or behind it - either way the dome is not empty
        SetOrb(c, 0, 0, 0, Z, R, 0.5f, 1.0f, 0.0f, 0);
        SetFrame(c, 0, up, V3{0, 0, -1}, V3{0, 0, -1});
        gpu.Render(c, kW, kH, &a);
        int any = 0;
        for (int y = cy - rpx; y <= cy; ++y) if (Reddish(a.At(cx, y))) ++any;
        Expect(any > 10, "along the arm, half full: liquid in the dome (%d rows)", any);
    }
    // the metals and the patina
    {
        auto ring = [&](float metal, float patina, float avg[3]) {
            Cb c = base();
            c.orbMed[1] = patina;
            c.orbMed[2] = metal;
            SetOrb(c, 0, 0, 0, Z, R, 1.0f, 1.0f, 0.0f, 1);
            SetFrame(c, 0, toEye, up, toEye);
            Img a;
            gpu.Render(c, kW, kH, &a);
            RingAverage(a, cx, cy, 0.93f * rpx, 0.99f * rpx, avg);   // the lip (the plate's outer ring)
        };
        float gold[3], bronze[3], silver[3], aged[3], agedS[3];
        ring(0.0f, 0.0f, gold);
        ring(1.0f, 0.0f, bronze);
        ring(2.0f, 0.0f, silver);
        ring(0.0f, 1.0f, aged);
        ring(2.0f, 1.0f, agedS);
        Expect(gold[0] > gold[1] && gold[1] > gold[2] && gold[0] > 2.0f * gold[2], "clean gold is yellow-orange: %.2f %.2f %.2f", gold[0], gold[1], gold[2]);
        Expect(bronze[0] > bronze[1] && bronze[1] > bronze[2] && bronze[1] / bronze[0] < gold[1] / gold[0] - 0.04f, "bronze is redder than gold (G/R %.2f against %.2f)", bronze[1] / bronze[0], gold[1] / gold[0]);
        Expect(std::max({silver[0], silver[1], silver[2]}) < 1.22f * std::min({silver[0], silver[1], silver[2]}), "silver is nearly neutral: %.2f %.2f %.2f", silver[0], silver[1], silver[2]);
        const float lg = gold[0] + gold[1] + gold[2], la = aged[0] + aged[1] + aged[2];
        Expect(la < 0.8f * lg, "the patina darkens the metal (%.2f against %.2f)", la, lg);
        Expect(aged[1] / aged[0] > gold[1] / gold[0] + 0.12f, "and turns it green: G/R %.2f against %.2f clean", aged[1] / aged[0], gold[1] / gold[0]);
        Expect(agedS[0] + agedS[1] + agedS[2] < 0.85f * (silver[0] + silver[1] + silver[2]), "aged silver is darker too");
    }
    // the metal is fixed on the medallion, not on the picture or the world: the wrist's frame turned a quarter turn about its back axis turns the ring's pattern with it
    {
        Cb a0 = base(), a1 = base();
        for (Cb* c : {&a0, &a1}) {
            c->orbMed[1] = 1.0f;
            SetOrb(*c, 0, 0, 0, Z, R, 1.0f, 1.0f, 0.0f, 1);
        }
        SetFrame(a0, 0, toEye, up, toEye);
        SetFrame(a1, 0, toEye, right, toEye);   // turned a quarter
        Img i0, i1;
        gpu.Render(a0, kW, kH, &i0);
        gpu.Render(a1, kW, kH, &i1);
        const float same = TurnedDiff(i0, i1, cx, cy, 0.82f * rpx, 0.99f * rpx, 0);
        float best = 1.0f;
        for (int k = 1; k < 4; ++k) best = std::min(best, TurnedDiff(i0, i1, cx, cy, 0.82f * rpx, 0.99f * rpx, k));
        Expect(best < 0.015f && best < 0.4f * same, "a quarter turn of the frame is a quarter turn of the pattern: %.4f turned back, %.4f not", best, same);
    }
    // the picture fixed to the world's lights: the same medallion lit the same, whichever way it is turned about its axis
    {
        Cb c = base();
        c.orbMed[1] = 0.0f;
        SetOrb(c, 0, 0, 0, Z, R, 1.0f, 1.0f, 0.0f, 1);
        SetFrame(c, 0, toEye, up, toEye);
        Img a;
        gpu.Render(c, kW, kH, &a);
        SetFrame(c, 0, toEye, right, toEye);
        Img b;
        gpu.Render(c, kW, kH, &b);
        // the dome (glass, highlight, liquid) is symmetric about the axis and the light is the world's: the same picture inside 0.7 of the radius
        double d = 0.0;
        int n = 0;
        for (int y = cy - (int)(0.7f * rpx); y <= cy + (int)(0.7f * rpx); ++y)
            for (int x = cx - (int)(0.7f * rpx); x <= cx + (int)(0.7f * rpx); ++x) {
                if ((x - cx) * (x - cx) + (y - cy) * (y - cy) > (int)(0.65f * rpx) * (int)(0.65f * rpx)) continue;
                const float* p = a.At(x, y);
                const float* q = b.At(x, y);
                d += std::fabs(p[0] - q[0]) + std::fabs(p[1] - q[1]) + std::fabs(p[2] - q[2]);
                ++n;
            }
        Expect(d / n < 0.01, "the dome looks the same whichever way the frame is turned about its axis: the light is the world's (%.4f)", d / n);
    }
    // what stands in front of it, behind it, or inside it
    {
        Cb c = base();
        SetOrb(c, 0, 0, 0, Z, R, 0.6f, 1.0f, 0.0f, 0);
        SetFrame(c, 0, V3{0, 0.7f, 0.7f}, V3{0, 0.7f, -0.7f}, V3{0, 0.7f, 0.7f});
        Img a;
        gpu.Render(c, kW, kH, &a);
        Expect(Differs(a, bg, cx, cy) && Differs(a, bg, cx, cy - rpx / 2), "an oblique medallion is drawn");
        Cb off = c;
        SetOrb(off, 0, 0, 0, Z, R, 0.6f, 0.0f, 0.0f, 0);
        Img o;
        gpu.Render(off, kW, kH, &o);
        Expect(Equal(o, bg), "opacity 0: the picture bit for bit");
        Cb wall = c;
        wall.wall[0] = 0.30f;   // a wall nearer than the medallion (0.42 m)
        wall.wall[1] = 0.0f;
        Img w, wbg;
        gpu.Render(wall, kW, kH, &w);
        Cb wallBg = wall;
        wallBg.camP[3] = 1.0f;
        gpu.Render(wallBg, kW, kH, &wbg);
        Expect(Equal(w, wbg), "a wall in front of it hides all of it, bit for bit");
        // a capsule (the forearm) in front of the middle: it hides the middle, not the sides
        Cb arm = c;
        const float a0[3] = {-0.2f, 0.0f, Z + 0.03f}, a1[3] = {0.2f, 0.0f, Z + 0.03f};
        SetCaps(arm, 0, a0, a1, 0.01f);
        arm.scene[0] = 1;
        Img ar, armBg;
        gpu.Render(arm, kW, kH, &ar);
        Cb armOff = arm;
        armOff.camP[3] = 1.0f;
        gpu.Render(armOff, kW, kH, &armBg);
        Expect(!Differs(ar, armBg, cx, cy) && Differs(ar, armBg, cx, cy - (int)(0.6f * rpx)), "a forearm in front hides what it covers and only that");
        // seen from under (the back of the hand turned away): the base's underside, the dome and its liquid are not seen
        Cb under = base();
        SetOrb(under, 0, 0, 0, Z, R, 1.0f, 1.0f, 0.0f, 0);
        SetFrame(under, 0, V3{0, 0, -1}, up, V3{0, 0, -1});
        Img u;
        gpu.Render(under, kW, kH, &u);
        const float* m = u.At(cx, cy);
        Expect(Differs(u, bg, cx, cy) && !Reddish(m), "from under, the base's underside, no liquid in sight (%.2f %.2f %.2f)", m[0], m[1], m[2]);
    }
    // the old effect's behaviour is the default: OrbMed.x 0 is the full ball
    {
        Cb c = base();
        c.orbMed[0] = 0.0f;
        SetOrb(c, 0, 0, 0, Z, R, 1.0f, 1.0f, 0.0f, 0);
        Img ball;
        gpu.Render(c, kW, kH, &ball);
        Expect(Reddish(ball.At(cx, cy)) && !GoldLike(ball.At(cx + (int)(0.95f * rpx), cy)), "OrbMed.x 0: the full glass ball, no base");
    }
    // the game's own globe on the base: the picture on the plane under the bezel, its up along the hand
    {
        const int side = 128;
        const float fit = 0.94f;
        gpu.SetGlobeTexture(SyntheticGlobes(side, fit), 2 * side, side);
        auto globes = [&]() {
            Cb c = base();
            c.picP[0] = 1.0f;
            c.picP[1] = fit;
            return c;
        };
        Cb c = globes();
        SetOrb(c, 0, 0, 0, Z, R, 0.3f, 1.0f, 0.0f, 0);
        SetFrame(c, 0, toEye, up, toEye);
        Img a;
        gpu.Render(c, kW, kH, &a);
        const float* p = a.At(cx, cy);
        Expect(Reddish(p) && p[0] > 0.4f, "the life globe's picture in the middle of the base (%.2f %.2f %.2f)", p[0], p[1], p[2]);
        Expect(GoldLike(a.At(cx + (int)(0.95f * rpx), cy)), "and the gold base round it");
        double mx = 0, my = 0;
        Expect(GreenCentre(a, &mx, &my) && my < cy - 0.3 * rpx && std::fabs(mx - cx) < 0.15 * rpx, "its marker is up when the hand's axis is up (%.0f, %.0f)", mx, my);
        SetFrame(c, 0, toEye, right, toEye);
        Img b;
        gpu.Render(c, kW, kH, &b);
        Expect(GreenCentre(b, &mx, &my) && mx > cx + 0.3 * rpx && std::fabs(my - cy) < 0.15 * rpx, "turn the frame a quarter and the picture turns with it: the marker goes to the right (%.0f, %.0f)", mx, my);
        // the ball shape: the disc is fixed on the wrist (OrbMed.w 1) or turns to the eyes (0)
        Cb ball = globes();
        ball.orbMed[0] = 0.0f;
        ball.orbMed[3] = 1.0f;
        SetOrb(ball, 0, 0, 0, Z, R, 0.3f, 1.0f, 0.0f, 0);
        SetFrame(ball, 0, toEye, right, toEye);
        Img f;
        gpu.Render(ball, kW, kH, &f);
        Expect(GreenCentre(f, &mx, &my) && mx > cx + 0.3 * rpx && std::fabs(my - cy) < 0.15 * rpx, "a ball's disc fixed on the wrist: its up is along the hand (the marker to the right: %.0f, %.0f)", mx, my);
        ball.orbMed[3] = 0.0f;
        Img e;
        gpu.Render(ball, kW, kH, &e);
        Expect(GreenCentre(e, &mx, &my) && my < cy - 0.3 * rpx && std::fabs(mx - cx) < 0.15 * rpx, "turning to the eyes: upright in the world whatever the hand does (%.0f, %.0f)", mx, my);
        // fixed on the wrist, seen from an angle: the disc is foreshortened with the wrist, not turned to the eyes
        Cb tilted = globes();
        tilted.orbMed[0] = 0.0f;
        tilted.orbMed[3] = 1.0f;
        SetOrb(tilted, 0, 0, 0, Z, R, 0.3f, 1.0f, 0.0f, 0);
        SetFrame(tilted, 0, V3{0, 0.95f, 0.31f}, V3{0, 0.31f, -0.95f}, up);   // the back of the hand mostly up: the disc is seen at a slant
        Img t;
        gpu.Render(tilted, kW, kH, &t);
        int rows = 0, cols = 0;
        for (int y = 0; y < kH; ++y) if (Differs(t, bg, cx, y, 0.05f)) ++rows;
        for (int x = 0; x < kW; ++x) if (Differs(t, bg, x, cy, 0.05f)) ++cols;
        Expect(cols > 1.6f * rows && rows > 8, "seen at a slant the fixed disc is squashed (%d columns, %d rows) - a billboard would be round", cols, rows);
        gpu.SetGlobeTexture(std::vector<float>(2 * 2 * 4, 0.0f), 2, 2);
    }
    // the turn drives the effect: the wrist's frame turned by TurnFrame, handed to the shader - the globe's picture turns with the medallion (a quarter turn about its axis out of the hand)
    {
        const int side = 128;
        const float fit = 0.94f;
        gpu.SetGlobeTexture(SyntheticGlobes(side, fit), 2 * side, side);
        Cb c = base();
        c.picP[0] = 1.0f;
        c.picP[1] = fit;
        SetOrb(c, 0, 0, 0, Z, R, 0.3f, 1.0f, 0.0f, 0);
        Place p;   // the back of the left hand toward the eye, its along up, the thumb to the right
        p.hasArm = true;
        p.axis = {0, 1, 0};
        p.armAxis = {0, 1, 0};
        p.back = {0, 0, 1};
        p.thumb = {1, 0, 0};
        handvitals::OrbSpot spot;
        auto render = [&](float rz) {
            Place q = p;
            spot.rotZDeg = rz;
            TurnFrame(q, spot, 1);
            SetFrame(c, 0, q.back, q.axis, q.back);
            Img a;
            gpu.Render(c, kW, kH, &a);
            return a;
        };
        double mx = 0, my = 0;
        Img a0 = render(0.0f);
        Expect(GreenCentre(a0, &mx, &my) && my < cy - 0.3 * rpx && std::fabs(mx - cx) < 0.15 * rpx, "no turn: the marker is up (%.0f, %.0f)", mx, my);
        Img a1 = render(90.0f);
        Expect(GreenCentre(a1, &mx, &my) && mx < cx - 0.3 * rpx && std::fabs(my - cy) < 0.15 * rpx, "Z +90 on the left: the picture turns with the medallion, its up goes toward the little finger (to the left: %.0f, %.0f)", mx, my);
        Img a2 = render(-90.0f);
        Expect(GreenCentre(a2, &mx, &my) && mx > cx + 0.3 * rpx && std::fabs(my - cy) < 0.15 * rpx, "Z -90: the other way");
        gpu.SetGlobeTexture(std::vector<float>(2 * 2 * 4, 0.0f), 2, 2);
        // a tilt about the along axis shows the medallion's dome from the side: the dome's top moves off the middle (face-on it was dead centre)
        Cb m = base();
        SetOrb(m, 0, 0, 0, Z, R, 1.0f, 1.0f, 0.0f, 1);
        Place q = p;
        spot = handvitals::OrbSpot{};
        spot.rotXDeg = 50.0f;
        TurnFrame(q, spot, 1);
        SetFrame(m, 0, q.back, q.axis, q.back);
        Img tilted;
        gpu.Render(m, kW, kH, &tilted);
        SetFrame(m, 0, p.back, p.axis, p.back);
        Img flat;
        gpu.Render(m, kW, kH, &flat);
        int changed = 0;
        for (int y = cy - rpx; y <= cy + rpx; ++y)
            for (int xx = cx - rpx; xx <= cx + rpx; ++xx) if (Differs(tilted, flat, xx, y, 0.05f)) ++changed;
        Expect(changed > rpx * rpx / 4, "X +50 tilts the whole medallion: a good part of the picture changes (%d pixels)", changed);
    }
    if (dir.empty()) return;
    // ---- the pictures ----
    const std::string sep = "\\";
    auto toRgb = [&](const Img& img) {
        std::vector<uint8_t> rgb((size_t)img.w * img.h * 3);
        for (size_t i = 0; i < (size_t)img.w * img.h; ++i)
            for (int k = 0; k < 3; ++k) rgb[i * 3 + k] = (uint8_t)std::clamp((int)std::lround(std::clamp(img.px[i * 4 + k], 0.0f, 1.0f) * 255.0f), 0, 255);
        return rgb;
    };
    auto sheet = [&](const std::vector<Img>& imgs, int cols, const std::string& name) {
        const int rowsN = ((int)imgs.size() + cols - 1) / cols, gap = 6;
        const int W = cols * kW + (cols + 1) * gap, H = rowsN * kH + (rowsN + 1) * gap;
        std::vector<uint8_t> rgb((size_t)W * H * 3, 24);
        for (size_t i = 0; i < imgs.size(); ++i) {
            const std::vector<uint8_t> t = toRgb(imgs[i]);
            const int x0 = gap + (int)(i % cols) * (kW + gap), y0 = gap + (int)(i / cols) * (kH + gap);
            for (int y = 0; y < kH; ++y) memcpy(&rgb[((size_t)(y0 + y) * W + x0) * 3], &t[(size_t)y * kW * 3], (size_t)kW * 3);
        }
        Expect(WritePng(dir + sep + name, W, H, rgb), "wrote %s", name.c_str());
    };
    // 0. the turns: none, X +50 / -50 (the face toward the thumb / the little finger), Y +50 (toward the elbow), Z +45 (the base's beads and the picture's up turn)
    {
        std::vector<Img> imgs;
        const float rots[6][3] = {{0, 0, 0}, {50, 0, 0}, {-50, 0, 0}, {0, 50, 0}, {0, -50, 0}, {0, 0, 45}};
        for (int k = 0; k < 6; ++k) {
            Cb c = base();
            c.orbMed[1] = 0.55f;
            SetOrb(c, 0, 0, 0, Z, R, 0.6f, 1.0f, 0.0f, k % 2);
            Place q;
            q.hasArm = true; q.axis = {0, 0.5f, -0.87f}; q.armAxis = q.axis; q.back = {0, 0.87f, 0.5f}; q.thumb = {1, 0, 0};
            handvitals::OrbSpot sp;
            sp.rotXDeg = rots[k][0]; sp.rotYDeg = rots[k][1]; sp.rotZDeg = rots[k][2];
            TurnFrame(q, sp, 1);
            SetFrame(c, 0, q.back, q.axis, q.back);
            c.eyeT[3] = 1.0f + (float)k;
            Img img;
            gpu.Render(c, kW, kH, &img);
            imgs.push_back(img);
        }
        sheet(imgs, 3, "hand_orbs_medallion_turns.png");
    }
    // 1. the metals and the patina, face-on, close: gold / bronze / silver x patina 0 / 55 / 100 %
    {
        std::vector<Img> imgs;
        for (int m = 0; m < 3; ++m)
            for (int k = 0; k < 3; ++k) {
                Cb c = base();
                c.orbMed[1] = k == 0 ? 0.0f : k == 1 ? 0.55f : 1.0f;
                c.orbMed[2] = (float)m;
                SetOrb(c, 0, 0, 0, Z, R, 0.6f, 1.0f, 0.0f, m == 1 ? 1 : 0);
                SetFrame(c, 0, V3{0.0f, 0.5f, 0.85f}, V3{0.0f, 0.85f, -0.5f}, V3{0.0f, 0.5f, 0.85f});
                c.eyeT[3] = 2.0f;
                Img img;
                gpu.Render(c, kW, kH, &img);
                imgs.push_back(img);
            }
        sheet(imgs, 3, "hand_orbs_medallion_metals.png");
    }
    // 2. the levels, life and mana, 100 / 70 / 35 / 8 and 100 / 50 / 15 / 0, the back of the hand tilted toward the eyes; and the fill directions
    {
        std::vector<Img> imgs;
        const float lf[4] = {1.0f, 0.7f, 0.35f, 0.08f}, mf[4] = {1.0f, 0.5f, 0.15f, 0.0f};
        for (int k = 0; k < 4; ++k) {
            Cb c = base();
            c.orbMed[1] = 0.55f;
            SetOrb(c, 0, -0.03f, 0, Z, R, lf[k], 1.0f, k == 3 ? 0.85f : 0.0f, 0);
            SetOrb(c, 1, 0.03f, 0, Z, R, mf[k], 1.0f, 0.0f, 1);
            for (int o = 0; o < 2; ++o) SetFrame(c, o, V3{0.0f, 0.62f, 0.78f}, V3{0.0f, 0.78f, -0.62f}, V3{0.0f, 0.62f, 0.78f});
            c.eyeT[3] = 1.3f * (float)k;
            Img img;
            gpu.Render(c, kW, kH, &img);
            imgs.push_back(img);
        }
        sheet(imgs, 2, "hand_orbs_medallion_levels.png");
        imgs.clear();
        // the six fill directions on a hand held forward with the back up, seen from above and behind: the wrist's frame is back (0,1,0)-ish, along away, across to the right
        const V3 fa[6] = {V3{0, 0.3f, -0.95f}, V3{0, -0.3f, 0.95f}, V3{-1, 0, 0}, V3{1, 0, 0}, V3{0, 0.95f, 0.3f}, V3{0, 1, 0}};   // elbow, hand, thumb, pinky (the left hand), out of the hand, the world's
        for (int k = 0; k < 6; ++k) {
            Cb c = base();
            c.orbMed[1] = 0.55f;
            SetOrb(c, 0, 0, 0, Z, R, 0.5f, 1.0f, 0.0f, 0);
            SetOrb(c, 1, 0.0f, 0.0f, 9.0f, 0.02f, 0.0f, 0.0f, 0.0f, 1);
            SetFrame(c, 0, V3{0, 0.95f, 0.3f}, V3{0, 0.3f, -0.95f}, fa[k]);
            c.eyeT[3] = 1.0f + (float)k;
            Img img;
            gpu.Render(c, kW, kH, &img);
            imgs.push_back(img);
        }
        sheet(imgs, 3, "hand_orbs_medallion_fill_directions.png");
    }
    // 3. on the forearms from the head, both eyes, and the left wrist close up
    {
        const float lElbow[3] = {-0.22f, -0.32f, -0.12f}, lWrist[3] = {-0.17f, -0.22f, -0.42f}, lKnuckle[3] = {-0.15f, -0.19f, -0.53f};
        const float rElbow[3] = {0.22f, -0.32f, -0.12f}, rWrist[3] = {0.17f, -0.22f, -0.42f}, rKnuckle[3] = {0.15f, -0.19f, -0.53f};
        auto arms = [&](Cb& c) {
            SetCaps(c, 0, lElbow, lWrist, 0.032f);
            SetCaps(c, 1, lWrist, lKnuckle, 0.038f);
            SetCaps(c, 2, rElbow, rWrist, 0.032f);
            SetCaps(c, 3, rWrist, rKnuckle, 0.038f);
            c.scene[0] = 4;
        };
        Config cfg = OrbsOn();
        In in = Base();
        in.cfg = cfg;
        in.arm[1] = ArmAt({lElbow[0], lElbow[1], lElbow[2]}, {lWrist[0], lWrist[1], lWrist[2]}, hset::Unit(V3{0.0f, -1.0f, 0.2f}));
        in.arm[0] = ArmAt({rElbow[0], rElbow[1], rElbow[2]}, {rWrist[0], rWrist[1], rWrist[2]}, hset::Unit(V3{0.0f, -1.0f, 0.2f}));
        in.v = handvitals::FromStats(720 * 256, 1000 * 256, 130 * 256, 400 * 256);
        State st;
        const Set s = Step(st, in);
        const float camR[3] = {1, 0, 0}, camF[3] = {0, 0, -1};
        const Uniforms u = Pack(s, camR, camF, 1.0f, 3.0);
        std::vector<Img> imgs;
        auto put = [&](Cb& c) {
            for (int k = 0; k < 2; ++k) {
                SetOrb(c, k, u.pos[k][0], u.pos[k][1], u.pos[k][2], u.pos[k][3], u.look[k][0], u.look[k][1], 0.0f, k);
                for (int a = 0; a < 3; ++a) { c.orbAxis[k][a] = u.axis[k][a]; c.orbBack[k][a] = u.back[k][a]; c.orbAlong[k][a] = u.along[k][a]; }
            }
            c.orbMed[1] = 0.55f;
        };
        for (int eye = 0; eye < 2; ++eye) {
            Cb c = base();
            arms(c);
            c.camP[0] = 0.55f;
            c.camP[1] = 0.55f * (float)kH / (float)kW;
            c.camQ[0] = -0.55f;
            c.eyeT[0] = eye ? 0.032f : -0.032f;
            c.eyeT[3] = 3.0f;
            put(c);
            Img img;
            gpu.Render(c, kW, kH, &img);
            imgs.push_back(img);
        }
        for (int roll = 0; roll < 2; ++roll) {
            Cb c = base();
            arms(c);
            c.camP[0] = 0.16f;
            c.camP[1] = 0.16f * (float)kH / (float)kW;
            c.camP[2] = roll ? 0.45f : 0.0f;
            c.camQ[0] = -0.36f;
            c.eyeT[0] = -0.17f;
            c.eyeT[1] = -0.05f;
            c.eyeT[2] = -0.10f;
            c.eyeT[3] = 5.0f;
            put(c);
            Img img;
            gpu.Render(c, kW, kH, &img);
            imgs.push_back(img);
        }
        sheet(imgs, 2, "hand_orbs_medallion_arms.png");
    }
    // 4. the game's own globes on the base, and the full glass ball again for comparison
    {
        const int side = 128;
        const float fit = 0.94f;
        std::vector<Img> imgs;
        gpu.SetGlobeTexture(SyntheticGlobes(side, fit), 2 * side, side);
        for (int k = 0; k < 2; ++k) {
            Cb c = base();
            c.picP[0] = 1.0f;
            c.picP[1] = fit;
            c.orbMed[1] = 0.55f;
            SetOrb(c, 0, -0.03f, 0, Z, R, 0.5f, 1.0f, 0.0f, 0);
            SetOrb(c, 1, 0.03f, 0, Z, R, 0.5f, 1.0f, 0.0f, 1);
            for (int o = 0; o < 2; ++o) SetFrame(c, o, V3{0.0f, 0.62f, 0.78f}, V3{0.0f, 0.78f, -0.62f}, V3{0.0f, 0.62f, 0.78f});
            c.eyeT[3] = 2.0f;
            if (k == 1) { c.orbMed[0] = 0.0f; c.orbMed[3] = 1.0f; }
            Img img;
            gpu.Render(c, kW, kH, &img);
            imgs.push_back(img);
        }
        gpu.SetGlobeTexture(std::vector<float>(2 * 2 * 4, 0.0f), 2, 2);
        {
            Cb c = base();
            c.orbMed[0] = 0.0f;
            SetOrb(c, 0, -0.03f, 0, Z, R, 0.5f, 1.0f, 0.0f, 0);
            SetOrb(c, 1, 0.03f, 0, Z, R, 0.5f, 1.0f, 0.0f, 1);
            c.eyeT[3] = 2.0f;
            Img img;
            gpu.Render(c, kW, kH, &img);
            imgs.push_back(img);
        }
        sheet(imgs, 3, "hand_orbs_medallion_globes.png");
    }
}

void TestShader(const std::string& dir) {
    printf("the effect's orbs on WARP\n");
#ifndef ORBS_FX_PATH
    printf("  (no effect path: skipped)\n");
    (void)dir;
    return;
#else
    FILE* f = nullptr;
    if (fopen_s(&f, ORBS_FX_PATH, "rb") || !f) { Expect(false, "could not read the effect %s", ORBS_FX_PATH); return; }
    std::string fx;
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) fx.append(buf, n);
    fclose(f);
    Gpu gpu;
    if (!gpu.Init(fx)) { Expect(false, "the effect's ORBS block does not compile / run: %s", gpu.error.c_str()); return; }
    Expect(true, "the ORBS block compiles (ps_5_0) and WARP runs it");

    Cam cam;
    cam.tanX = 0.12f;
    const float R = 0.025f;
    const float Z = -0.42f;
    float cx0, cy0, ppm;
    const float centre[3] = {0, 0, Z};
    cam.Project(centre, &cx0, &cy0, &ppm);
    const int cx = (int)cx0, cy = (int)cy0, rpx = (int)(R * ppm);
    Expect(rpx > 60 && rpx < 140, "the test camera puts the ball at ~%d px radius", rpx);

    auto base = [&]() {
        Cb c;
        c.camP[0] = cam.tanX;
        c.camP[1] = cam.tanY();
        c.scene[0] = 0;   // no forearm
        NoOrb(c, 0);
        NoOrb(c, 1);
        return c;
    };
    Img bg, a;
    {
        Cb c = base();
        c.camP[3] = 1.0f;   // no orbs
        if (!gpu.Render(c, kW, kH, &bg)) { Expect(false, "render"); return; }
    }
    // opacity 0 is the picture bit for bit
    {
        Cb c = base();
        SetOrb(c, 0, 0, 0, Z, R, 1.0f, 0.0f, 0.0f, 0);
        SetOrb(c, 1, 0.06f, 0, Z, R, 0.5f, 0.0f, 0.0f, 1);
        gpu.Render(c, kW, kH, &a);
        Expect(Equal(a, bg), "opacity 0: every pixel as it went in, bit for bit");
    }
    // a ball far off to the side of the picture, one behind the eye: nothing of them in the picture
    {
        Cb c = base();
        SetOrb(c, 0, 0.0f, 0.0f, 0.5f, R, 1.0f, 1.0f, 0.0f, 0);   // behind the eye
        SetOrb(c, 1, 3.0f, 0.0f, Z, R, 1.0f, 1.0f, 0.0f, 1);   // 3 m to the side: out of the picture
        gpu.Render(c, kW, kH, &a);
        bool same = true;
        for (int y = 0; y < kH && same; ++y)
            for (int x = 0; x < kW; ++x)
                if (Differs(a, bg, x, y, 0.02f)) { same = false; break; }
        Expect(same, "a ball behind the eye and one far out of the picture change nothing that shows");
    }
    // fill levels: the liquid's column rises with the level
    int rows[5] = {};
    const float fills[5] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
    for (int k = 0; k < 5; ++k) {
        Cb c = base();
        SetOrb(c, 0, 0, 0, Z, R, fills[k], 1.0f, 0.0f, 0);
        gpu.Render(c, kW, kH, &a);
        rows[k] = LiquidRows(a, cx, cy, rpx, true);
    }
    Expect(rows[0] < 4 && rows[0] < rows[1] && rows[1] < rows[2] && rows[2] < rows[3] && rows[3] < rows[4], "the red column rises with the level: %d %d %d %d %d rows", rows[0], rows[1], rows[2], rows[3], rows[4]);
    Expect(std::abs(rows[2] - rpx) < 22, "half full is about half the glass (%d of %d rows)", rows[2], 2 * rpx);
    Expect(rows[4] > 2 * rpx - 40, "full is all of it (%d of %d)", rows[4], 2 * rpx);
    // mana is blue
    {
        Cb c = base();
        SetOrb(c, 0, 0, 0, Z, R, 1.0f, 1.0f, 0.0f, 1);
        gpu.Render(c, kW, kH, &a);
        const float* p = a.At(cx - rpx / 3, cy + rpx / 3);
        Expect(Bluish(p), "mana's liquid is blue (%.2f %.2f %.2f)", p[0], p[1], p[2]);
        Expect(LiquidRows(a, cx, cy, rpx, false) > 2 * rpx - 40, "and fills the glass");
        SetOrb(c, 0, 0, 0, Z, R, 0.5f, 1.0f, 0.0f, 1);
        gpu.Render(c, kW, kH, &a);
        const int half = LiquidRows(a, cx, cy, rpx, false);
        Expect(std::abs(half - rpx) < 22, "half a mana ball is about half the glass (%d)", half);
    }
    // a life ball is red and not blue
    {
        Cb c = base();
        SetOrb(c, 0, 0, 0, Z, R, 1.0f, 1.0f, 0.0f, 0);
        gpu.Render(c, kW, kH, &a);
        const float* p = a.At(cx - rpx / 3, cy + rpx / 3);
        Expect(Reddish(p), "life's liquid is red (%.2f %.2f %.2f)", p[0], p[1], p[2]);
    }
    // the liquid's level is horizontal in the WORLD: a rolled head sees it tilted, an upright one level
    {
        Cb c = base();
        SetOrb(c, 0, 0, 0, Z, R, 0.5f, 1.0f, 0.0f, 0);
        auto boundary = [&](const Img& img, int x) {   // the first reddish row from the top at this column
            for (int y = cy - rpx; y <= cy + rpx; ++y) {
                const int dx = x - cx, dy = y - cy;
                if (dx * dx + dy * dy > (int)(0.8f * rpx) * (int)(0.8f * rpx)) continue;   // (not the halo outside the glass)
                if (Reddish(img.At(x, y))) return y;
            }
            return -1;
        };
        gpu.Render(c, kW, kH, &a);
        const int l0 = boundary(a, cx - rpx / 2), r0 = boundary(a, cx + rpx / 2);
        Expect(l0 > 0 && r0 > 0 && std::abs(l0 - r0) <= 12, "upright: the level is level across the ball (%d vs %d)", l0, r0);
        c.camP[2] = 0.5236f;   // the head rolled 30 degrees
        gpu.Render(c, kW, kH, &a);
        const int l1 = boundary(a, cx - rpx / 2), r1 = boundary(a, cx + rpx / 2);
        Expect(l1 > 0 && r1 > 0 && std::abs(l1 - r1) > rpx / 4, "rolled 30 degrees: the level tilts in the picture (%d vs %d)", l1, r1);
    }
    // a nearer surface hides the ball: a wall in front of it over the right half of the picture
    {
        Cb c = base();
        SetOrb(c, 0, 0, 0, Z, R, 1.0f, 1.0f, 0.3f, 0);
        c.wall[0] = -Z - 0.10f;   // 0.32 m: nearer than the ball's 0.395 - 0.445
        c.wall[1] = (float)cx;
        gpu.Render(c, kW, kH, &a);
        Cb w = base();
        w.camP[3] = 1.0f;
        w.wall[0] = c.wall[0];
        w.wall[1] = c.wall[1];
        Img wall;
        gpu.Render(w, kW, kH, &wall);
        bool hidden = true;
        for (int y = cy - rpx - 30; y <= cy + rpx + 30; ++y)
            for (int x = cx + 2; x <= cx + rpx + 30; ++x)
                if (Differs(a, wall, x, y)) hidden = false;
        Expect(hidden, "a wall in front hides the ball and its halo (the pixels are the wall's, bit for bit)");
        Expect(Differs(a, wall, cx - rpx / 2, cy) && Reddish(a.At(cx - rpx / 2, cy + rpx / 3)), "and the left half, with no wall, shows it");
    }
    // a surface through the ball's middle cuts it: the part in front of it shows, the rest is the surface's
    {
        Cb c = base();
        SetOrb(c, 0, 0, 0, Z, R, 1.0f, 1.0f, 0.0f, 0);
        Cb w = c;
        w.wall[0] = -Z;   // the ball's middle depth
        w.wall[1] = (float)cx;
        Img whole, cut;
        gpu.Render(c, kW, kH, &whole);
        gpu.Render(w, kW, kH, &cut);
        const int px = cx + rpx / 2, py = cy + rpx / 4;
        const float* q = cut.At(px, py);
        Expect(Differs(whole, cut, px, py, 0.01f), "sunk to its middle: the right half is not the whole ball's (%.2f vs %.2f)", q[0], whole.At(px, py)[0]);
        Expect(q[0] > 0.08f, "but the glass and the liquid in front of the surface still show (%.2f %.2f %.2f)", q[0], q[1], q[2]);
        const float* edge = cut.At(cx + rpx + 6, cy);
        Expect(edge[2] >= 0.26f - 1e-4f && edge[2] < 0.30f, "past the ball the wall shows (a little halo over it at most: %.3f)", edge[2]);
    }
    // the halo: a glow outside the glass, gone by two and a half radii, not through a nearer surface
    {
        Cb c = base();
        SetOrb(c, 0, 0, 0, Z, R, 1.0f, 1.0f, 0.0f, 0);
        gpu.Render(c, kW, kH, &a);
        const float* in = a.At(cx + (int)(1.25f * rpx), cy);
        const float* b0 = bg.At(cx + (int)(1.25f * rpx), cy);
        Expect(in[0] > b0[0] + 0.02f && in[0] > in[2], "a red halo just outside the glass (%.3f over %.3f)", in[0], b0[0]);
        Cb m = c;
        SetOrb(m, 0, 0, 0, Z, R, 1.0f, 1.0f, 0.0f, 1);
        gpu.Render(m, kW, kH, &a);
        const float* mi = a.At(cx + (int)(1.25f * rpx), cy);
        Expect(mi[2] > b0[2] + 0.02f && mi[2] > mi[0], "mana's halo is blue (%.3f over %.3f)", mi[2], b0[2]);
        gpu.Render(c, kW, kH, &a);
        const float* farPx = a.At(cx + (int)(2.8f * rpx), cy);
        Expect(std::fabs(farPx[0] - bg.At(cx + (int)(2.8f * rpx), cy)[0]) < 0.008f, "all but gone by 2.8 radii (%.4f over %.4f)", farPx[0], bg.At(cx + (int)(2.8f * rpx), cy)[0]);
        {   // and exactly gone from 3.6 radii (the camera pulled back so that it is in the picture)
            Cb wide = c;
            wide.camP[0] = 0.4f;
            wide.camP[1] = 0.4f * (float)kH / (float)kW;
            Cb wideBg = wide;
            wideBg.camP[3] = 1.0f;
            Img wa, wb;
            gpu.Render(wide, kW, kH, &wa);
            gpu.Render(wideBg, kW, kH, &wb);
            Cam wc;
            wc.tanX = 0.4f;
            float wx, wy, wppm;
            wc.Project(centre, &wx, &wy, &wppm);
            const int wr = (int)(R * wppm), wxc = (int)wx, wyc = (int)wy;
            Expect(wxc + (int)(3.7f * wr) < kW && !Differs(wa, wb, wxc + (int)(3.7f * wr), wyc) && Differs(wa, wb, wxc + (int)(1.3f * wr), wyc), "exactly gone from 3.6 radii out, there at 1.3");
        }
        // a glowing (heartbeat) ball: the halo is stronger
        Cb g = c;
        SetOrb(g, 0, 0, 0, Z, R, 1.0f, 1.0f, 1.0f, 0);
        Img glow;
        gpu.Render(g, kW, kH, &glow);
        Expect(glow.At(cx + (int)(1.25f * rpx), cy)[0] > a.At(cx + (int)(1.25f * rpx), cy)[0] + 0.01f, "the heartbeat makes the halo stronger");
        Expect(glow.At(cx - rpx / 3, cy + rpx / 3)[0] > a.At(cx - rpx / 3, cy + rpx / 3)[0], "and the liquid brighter");
        // opacity scales it
        Cb h = c;
        SetOrb(h, 0, 0, 0, Z, R, 1.0f, 0.5f, 0.0f, 0);
        Img half;
        gpu.Render(h, kW, kH, &half);
        const float* pf = a.At(cx - rpx / 3, cy + rpx / 3);
        const float* ph = half.At(cx - rpx / 3, cy + rpx / 3);
        const float* pb = bg.At(cx - rpx / 3, cy + rpx / 3);
        Expect(std::fabs((ph[0] - pb[0]) - 0.5f * (pf[0] - pb[0])) < 0.02f, "opacity 0.5 lays the ball over the picture at half strength (%.3f vs %.3f)", ph[0] - pb[0], 0.5f * (pf[0] - pb[0]));
    }
    // parallax: each eye sees the ball from its own place
    {
        Cb c = base();
        SetOrb(c, 0, 0, 0, Z, R, 1.0f, 1.0f, 0.0f, 0);
        Img left, right;
        c.eyeT[0] = -0.01f;
        gpu.Render(c, kW, kH, &left);
        c.eyeT[0] = 0.01f;
        gpu.Render(c, kW, kH, &right);
        auto centroid = [&](const Img& img) {
            double sx = 0.0, sw = 0.0;
            for (int y = 0; y < kH; ++y)
                for (int x = 0; x < kW; ++x) {
                    const float* p = img.At(x, y);
                    if (Reddish(p)) { sx += x; sw += 1.0; }
                }
            return sw > 0.0 ? sx / sw : -1.0;
        };
        const double cl = centroid(left), cr = centroid(right);
        Expect(cl > 0.0 && cr > 0.0 && cl - cr > 20.0, "an eye 2 cm to the right sees the ball further left: %.1f px vs %.1f px (parallax)", cr, cl);
    }
    // two balls: the nearer one in front, whatever the order
    {
        Cb c = base();
        SetOrb(c, 0, 0.012f, 0, Z, R, 1.0f, 1.0f, 0.0f, 0);          // life, farther
        SetOrb(c, 1, -0.012f, 0, Z + 0.03f, R, 1.0f, 1.0f, 0.0f, 1);  // mana, 3 cm nearer, overlapping
        gpu.Render(c, kW, kH, &a);
        Cb d = c;
        SetOrb(d, 0, -0.012f, 0, Z + 0.03f, R, 1.0f, 1.0f, 0.0f, 1);
        SetOrb(d, 1, 0.012f, 0, Z, R, 1.0f, 1.0f, 0.0f, 0);
        Img swapped;
        gpu.Render(d, kW, kH, &swapped);
        Expect(Equal(a, swapped), "two balls overlapping: the nearer is over the other whichever is first in the list");
    }

    TestGlobesShader(gpu, cam, R, Z, cx, cy, rpx);
    TestAxisShader(gpu, cam, R, Z, cx, cy, rpx);
    TestMedallionShader(gpu, cam, R, Z, cx, cy, rpx, dir);

    if (dir.empty()) return;
    // ---- the pictures ----
    const std::string sep = "\\";
    auto toRgb = [&](const Img& img) {
        std::vector<uint8_t> rgb((size_t)img.w * img.h * 3);
        for (size_t i = 0; i < (size_t)img.w * img.h; ++i)
            for (int k = 0; k < 3; ++k) rgb[i * 3 + k] = (uint8_t)std::clamp((int)std::lround(std::clamp(img.px[i * 4 + k], 0.0f, 1.0f) * 255.0f), 0, 255);   // (the game's colours are display colours: no gamma)
        return rgb;
    };
    auto sheet = [&](const std::vector<Img>& imgs, int cols, const std::string& name) {
        const int rowsN = ((int)imgs.size() + cols - 1) / cols, gap = 6;
        const int W = cols * kW + (cols + 1) * gap, H = rowsN * kH + (rowsN + 1) * gap;
        std::vector<uint8_t> rgb((size_t)W * H * 3, 24);
        for (size_t i = 0; i < imgs.size(); ++i) {
            const std::vector<uint8_t> t = toRgb(imgs[i]);
            const int x0 = gap + (int)(i % cols) * (kW + gap), y0 = gap + (int)(i / cols) * (kH + gap);
            for (int y = 0; y < kH; ++y) memcpy(&rgb[((size_t)(y0 + y) * W + x0) * 3], &t[(size_t)y * kW * 3], (size_t)kW * 3);
        }
        Expect(WritePng(dir + sep + name, W, H, rgb), "wrote %s", name.c_str());
    };
    // 1. the levels, close up: life 100 / 70 / 35 / 8 and mana 100 / 50 / 15 / 0
    {
        std::vector<Img> imgs;
        const float lf[4] = {1.0f, 0.7f, 0.35f, 0.08f}, mf[4] = {1.0f, 0.5f, 0.15f, 0.0f};
        for (int k = 0; k < 4; ++k) {
            Cb c = base();
            SetOrb(c, 0, -0.03f, 0, Z, R, lf[k], 1.0f, k == 3 ? 0.85f : 0.0f, 0);
            SetOrb(c, 1, 0.03f, 0, Z, R, mf[k], 1.0f, 0.0f, 1);
            c.eyeT[3] = 1.3f * (float)k;
            Img img;
            gpu.Render(c, kW, kH, &img);
            imgs.push_back(img);
        }
        sheet(imgs, 2, "hand_orbs_levels.png");
    }
    // 2. on the forearms, from the head: the hero's two arms held out, life on the left wrist, mana on the right - both eyes
    {
        const float lElbow[3] = {-0.22f, -0.32f, -0.12f}, lWrist[3] = {-0.17f, -0.22f, -0.42f}, lKnuckle[3] = {-0.15f, -0.19f, -0.53f};
        const float rElbow[3] = {0.22f, -0.32f, -0.12f}, rWrist[3] = {0.17f, -0.22f, -0.42f}, rKnuckle[3] = {0.15f, -0.19f, -0.53f};
        auto arms = [&](Cb& c) {
            SetCaps(c, 0, lElbow, lWrist, 0.032f);
            SetCaps(c, 1, lWrist, lKnuckle, 0.038f);
            SetCaps(c, 2, rElbow, rWrist, 0.032f);
            SetCaps(c, 3, rWrist, rKnuckle, 0.038f);
            c.scene[0] = 4;
        };
        // the orbs through the hand-orbs maths: 4 cm up out of the back of the hand, 3 cm along, from poses like these
        Config cfg = OrbsOn();
        cfg.orbHands = 0;
        In in = Base();
        in.cfg = cfg;
        in.arm[1] = ArmAt({lElbow[0], lElbow[1], lElbow[2]}, {lWrist[0], lWrist[1], lWrist[2]}, hset::Unit(V3{0.0f, -1.0f, 0.2f}));
        in.arm[0] = ArmAt({rElbow[0], rElbow[1], rElbow[2]}, {rWrist[0], rWrist[1], rWrist[2]}, hset::Unit(V3{0.0f, -1.0f, 0.2f}));
        in.v = handvitals::FromStats(720 * 256, 1000 * 256, 130 * 256, 400 * 256);
        State st;
        const Set s = Step(st, in);
        std::vector<Img> imgs;
        for (int eye = 0; eye < 2; ++eye) {
            Cb c = base();
            arms(c);
            c.camP[0] = 0.55f;
            c.camP[1] = 0.55f * (float)kH / (float)kW;
            c.camQ[0] = -0.55f;   // looking down at his hands
            c.eyeT[0] = eye ? 0.032f : -0.032f;
            c.eyeT[3] = 3.0f;
            for (int k = 0; k < 2; ++k) { SetOrb(c, k, s.orb[k].c.x, s.orb[k].c.y, s.orb[k].c.z, s.orb[k].radius, s.orb[k].fill, s.orb[k].opacity, 0.0f, k); c.orbAxis[k][0] = s.orb[k].axis.x; c.orbAxis[k][1] = s.orb[k].axis.y; c.orbAxis[k][2] = s.orb[k].axis.z; }
            Img img;
            gpu.Render(c, kW, kH, &img);
            imgs.push_back(img);
        }
        // and a closer look at the left wrist, level and rolled (the liquid stays level in the world)
        for (int roll = 0; roll < 2; ++roll) {
            Cb c = base();
            arms(c);
            c.camP[0] = 0.16f;
            c.camP[1] = 0.16f * (float)kH / (float)kW;
            c.camP[2] = roll ? 0.45f : 0.0f;
            c.camQ[0] = -0.36f;
            c.eyeT[0] = -0.17f;
            c.eyeT[1] = -0.05f;
            c.eyeT[2] = -0.10f;
            c.eyeT[3] = 5.0f;
            for (int k = 0; k < 2; ++k) { SetOrb(c, k, s.orb[k].c.x, s.orb[k].c.y, s.orb[k].c.z, s.orb[k].radius, s.orb[k].fill, s.orb[k].opacity, 0.0f, k); c.orbAxis[k][0] = s.orb[k].axis.x; c.orbAxis[k][1] = s.orb[k].axis.y; c.orbAxis[k][2] = s.orb[k].axis.z; }
            Img img;
            gpu.Render(c, kW, kH, &img);
            imgs.push_back(img);
        }
        sheet(imgs, 2, "hand_orbs_arms.png");
        const std::string name = "hand_orbs_left_eye.png";
        Expect(WritePng(dir + sep + name, kW, kH, toRgb(imgs[0])), "wrote %s", name.c_str());
    }
    // 2b. the liquid along an axis (toward the hand, here to the right): 25 / 50 / 75 % of the way, and a slanted axis
    {
        std::vector<Img> imgs;
        const float f[4] = {0.25f, 0.5f, 0.75f, 0.5f}, ax[4][3] = {{1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {1, 0.7f, 0}};
        for (int k = 0; k < 4; ++k) {
            Cb c = base();
            SetOrb(c, 0, -0.03f, 0, Z, R, f[k], 1.0f, 0.0f, 0);
            SetOrb(c, 1, 0.03f, 0, Z, R, f[k], 1.0f, 0.0f, 1);
            for (int o = 0; o < 2; ++o) {
                const float l = std::sqrt(ax[k][0] * ax[k][0] + ax[k][1] * ax[k][1] + ax[k][2] * ax[k][2]);
                for (int a = 0; a < 3; ++a) c.orbAxis[o][a] = ax[k][a] / l;
            }
            c.eyeT[3] = 1.0f + (float)k;
            Img img;
            gpu.Render(c, kW, kH, &img);
            imgs.push_back(img);
        }
        sheet(imgs, 2, "hand_orbs_axis.png");
    }
    // 3. a nearer surface in front (the other arm, a monster) and a ball sunk into the arm
    {
        std::vector<Img> imgs;
        for (int k = 0; k < 3; ++k) {
            Cb c = base();
            SetOrb(c, 0, 0, 0, Z, R, 0.65f, 1.0f, 0.0f, 0);
            if (k == 1) { c.wall[0] = -Z - 0.10f; c.wall[1] = (float)cx; }
            if (k == 2) { c.wall[0] = -Z; c.wall[1] = (float)cx; }
            c.eyeT[3] = 2.0f;
            Img img;
            gpu.Render(c, kW, kH, &img);
            imgs.push_back(img);
        }
        sheet(imgs, 3, "hand_orbs_occluded.png");
    }
    if (!g_dumpPath.empty()) {   // the medallions with the game's own art, from a real dump of its interface layer
        std::vector<float> tex;
        std::vector<uint8_t> prev;
        int side = 0, pw = 0, ph = 0;
        hudglobes::Boxes bx;
        if (!LoadDumpGlobes(&tex, &side, &bx, &prev, &pw, &ph)) {
            Expect(false, "the layer dump %s could not be read or the globes do not fit in it at %.0f%%", g_dumpPath.c_str(), g_dumpSafe * 100.0f);
        } else {
            Expect(WritePng(dir + sep + "hand_orbs_game_globes_cut.png", pw, ph, prev), "wrote hand_orbs_game_globes_cut.png (%d x %d: the two squares cut out)", pw, ph);
            gpu.SetGlobeTexture(tex, 2 * side, side);
            const float lElbow[3] = {-0.22f, -0.32f, -0.12f}, lWrist[3] = {-0.17f, -0.22f, -0.42f}, lKnuckle[3] = {-0.15f, -0.19f, -0.53f};
            const float rElbow[3] = {0.22f, -0.32f, -0.12f}, rWrist[3] = {0.17f, -0.22f, -0.42f}, rKnuckle[3] = {0.15f, -0.19f, -0.53f};
            Config cfg = OrbsOn();
            cfg.style = handvitals::kStyleGlobes;
            In in = Base();
            in.cfg = cfg;
            in.arm[1] = ArmAt({lElbow[0], lElbow[1], lElbow[2]}, {lWrist[0], lWrist[1], lWrist[2]}, hset::Unit(V3{0.0f, -1.0f, 0.2f}));
            in.arm[0] = ArmAt({rElbow[0], rElbow[1], rElbow[2]}, {rWrist[0], rWrist[1], rWrist[2]}, hset::Unit(V3{0.0f, -1.0f, 0.2f}));
            State st;
            const Set set = Step(st, in);
            std::vector<Img> imgs;
            for (int view = 0; view < 4; ++view) {
                Cb c;
                c.camP[0] = view < 2 ? 0.55f : 0.16f;
                c.camP[1] = c.camP[0] * (float)kH / (float)kW;
                c.camQ[0] = view < 2 ? -0.55f : -0.36f;
                c.camP[2] = view == 3 ? 0.45f : 0.0f;
                c.picP[0] = 1.0f;
                c.picP[1] = bx.fit;
                c.eyeT[0] = view < 2 ? (view ? 0.032f : -0.032f) : -0.17f;
                c.eyeT[1] = view < 2 ? 0.0f : -0.05f;
                c.eyeT[2] = view < 2 ? 0.0f : -0.10f;
                const float lk[4][3] = {{lElbow[0], lElbow[1], lElbow[2]}, {lWrist[0], lWrist[1], lWrist[2]}, {rElbow[0], rElbow[1], rElbow[2]}, {rWrist[0], rWrist[1], rWrist[2]}};
                SetCaps(c, 0, lk[0], lk[1], 0.032f);
                SetCaps(c, 1, lWrist, lKnuckle, 0.038f);
                SetCaps(c, 2, lk[2], lk[3], 0.032f);
                SetCaps(c, 3, rWrist, rKnuckle, 0.038f);
                c.scene[0] = 4;
                for (int k = 0; k < 2; ++k) { SetOrb(c, k, set.orb[k].c.x, set.orb[k].c.y, set.orb[k].c.z, set.orb[k].radius, set.orb[k].fill, set.orb[k].opacity, 0.0f, k); c.orbAxis[k][0] = set.orb[k].axis.x; c.orbAxis[k][1] = set.orb[k].axis.y; c.orbAxis[k][2] = set.orb[k].axis.z; }
                Img img;
                gpu.Render(c, kW, kH, &img);
                imgs.push_back(img);
            }
            sheet(imgs, 2, "hand_orbs_game_globes.png");
        }
    }
    printf("  pictures in %s\n", dir.c_str());
#endif
}

}  // namespace

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "";
    if (argc > 2) g_dumpPath = argv[2];
    if (argc > 3) g_dumpSafe = (float)atof(argv[3]);
    TestPlacement();
    TestOnBone();
    TestSpots();
    TestHandFrame();
    TestFrameCarry();
    TestFillDirections();
    TestRotation();
    TestUnderShield();
    TestHeroDepth();
    TestShieldClassDepth();
    TestShieldClassRot();
    TestFade();
    TestState();
    TestBeat();
    TestPack();
    TestGlobesGeometry();
    TestShader(dir);
    printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
